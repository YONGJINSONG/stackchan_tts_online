#if defined(ENABLE_CAMERA)

#include <Arduino.h>
#include <M5Unified.h>
#include <Avatar.h>
#include "Camera.h"
#include <SPIFFS.h>
#include <SD.h>
#include <string.h>
#include <esp_heap_caps.h>
#include <base64.h>
#include "DebugTools.h"
#include "share/SdBus.h"
#include "share/Mutex.h"
#include "Robot.h"
#include "CameraVision.h"
#include "CameraDmaConfig.h"
#include "yuv.h"
#include "Gesture.h"
#include "llm/ChatGPT/FunctionCall.h"
#if defined(REALTIME_API)
#include "llm/RealtimeLLMBase.h"
#endif
#include "driver/gpio.h"

using namespace m5avatar;
extern Avatar avatar;
namespace m5avatar {
extern volatile bool g_avatar_render_pause;
extern volatile bool g_avatar_sd_paused;
}


bool isSubWindowON = true;
bool isSilentMode = false;

static camera_config_t camera_config = {
    .pin_pwdn     = -1,
    .pin_reset    = -1,
    // Official CoreS3: sensor XCLK is not an ESP GPIO (board clock).
    .pin_xclk     = -1,
    // sda=-1 selects SCCB_Use_Port(sccb_i2c_port). Do not SCCB_Init a
    // second driver on 11/12 — that races CoreS3 In_I2C (I2C_NUM_1).
    .pin_sscb_sda = -1,
    .pin_sscb_scl = -1,

    .pin_d7 = 47,
    .pin_d6 = 48,
    .pin_d5 = 16,
    .pin_d4 = 15,
    .pin_d3 = 42,
    .pin_d2 = 41,
    .pin_d1 = 40,
    .pin_d0 = 39,

    .pin_vsync = 46,
    .pin_href  = 38,
    .pin_pclk  = 45,

    .xclk_freq_hz = 20000000,
    .ledc_timer   = LEDC_TIMER_3,
    .ledc_channel = LEDC_CHANNEL_6,

    .pixel_format = PIXFORMAT_RGB565,
    //.pixel_format = PIXFORMAT_JPEG,
    // GC0308 cannot output JPEG. RGB565 at VGA is unreliable while Wi-Fi is
    // active; QVGA also matches the CoreS3 LCD and keeps DMA pressure low.
    .frame_size   = FRAMESIZE_QVGA,   // QVGA(320x240)
    .jpeg_quality = 0,
    //.fb_count     = 2,
    .fb_count     = 1,
    .fb_location  = CAMERA_FB_IN_PSRAM,
    .grab_mode    = CAMERA_GRAB_WHEN_EMPTY,
    .sccb_i2c_port = 1,
};

static bool camera_session_open = false;
static bool camera_servo_suspended = false;
static bool camera_audio_held = false;
static bool camera_ws_paused = false;

#if defined(REALTIME_API)
extern volatile bool g_inAiMod;
static void camera_pause_websocket(void) {
    if (camera_ws_paused) return;
    if (!g_inAiMod || !robot || !robot->llm) return;
    RealtimeLLMBase* rt = (RealtimeLLMBase*)robot->llm;
    if (rt->isRealtimeRecording()) rt->pauseRealtimeRecord(true);
    rt->suspendWebSocketLoopTask();
    camera_ws_paused = true;
    Serial.println("[camera] websocket paused for bounce DMA");
}

static void camera_resume_websocket(void) {
    if (!camera_ws_paused) return;
    camera_ws_paused = false;
    if (!robot || !robot->llm) return;
    ((RealtimeLLMBase*)robot->llm)->resumeWebSocketLoopTask();
    Serial.println("[camera] websocket resumed");
}
#else
static void camera_pause_websocket(void) {}
static void camera_resume_websocket(void) {}
#endif

static uint16_t* s_preview_rgb = nullptr;
static int s_preview_w = 0;
static int s_preview_h = 0;

static bool camera_ensure_preview_buf() {
    if (s_preview_rgb != nullptr) return true;
    s_preview_rgb = (uint16_t*)heap_caps_malloc(
        320 * 240 * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_preview_rgb == nullptr) {
        Serial.println("[camera] preview RGB alloc failed");
        return false;
    }
    return true;
}

// Keep s_preview_rgb in the same byte layout as a GC0308 RGB565_BE frame:
// [high byte, low byte] in memory. On little-endian ESP32 this means the
// uint16_t storage value itself is byte-swapped.
static uint16_t camera_rgb888_to_565_be_storage(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((v << 8) | (v >> 8));
}

// GC0308 default 0x24=0xa2 is YCbYCr (Y0 U Y1 V). Displayed as RGB565 that
// is teal with vertical chroma stripes — the preview/SD symptom.
static void camera_yuyv_to_rgb565(const uint8_t* src, uint16_t* dst, int w, int h) {
    const int pairs = (w * h) / 2;
    for (int i = 0; i < pairs; i++) {
        uint8_t y0 = src[0];
        uint8_t u  = src[1];
        uint8_t y1 = src[2];
        uint8_t v  = src[3];
        src += 4;
        uint8_t r, g, b;
        yuv2rgb(y0, u, v, &r, &g, &b);
        *dst++ = camera_rgb888_to_565_be_storage(r, g, b);
        yuv2rgb(y1, u, v, &r, &g, &b);
        *dst++ = camera_rgb888_to_565_be_storage(r, g, b);
    }
}

// Odd bytes clustered near 128 with lower variance than even bytes → YUYV.
static bool camera_fb_looks_yuyv(const uint8_t* p, size_t len) {
    if (!p || len < 8192) return true;
    const uint8_t* s = p + 2048;
    const int n = 512;
    uint32_t even_sum = 0, odd_sum = 0, even_sq = 0, odd_sq = 0;
    for (int i = 0; i < n; i++) {
        uint8_t e = s[i * 2];
        uint8_t o = s[i * 2 + 1];
        even_sum += e;
        odd_sum += o;
        even_sq += (uint32_t)e * e;
        odd_sq += (uint32_t)o * o;
    }
    int even_mean = (int)(even_sum / n);
    int odd_mean = (int)(odd_sum / n);
    int even_var = (int)(even_sq / n) - even_mean * even_mean;
    int odd_var = (int)(odd_sq / n) - odd_mean * odd_mean;
    bool yuv = (odd_mean > 80 && odd_mean < 176 && odd_var < even_var);
    Serial.printf("[camera] yuyv heuristic even_mean=%d odd_mean=%d even_var=%d odd_var=%d -> %s\n",
                  even_mean, odd_mean, even_var, odd_var, yuv ? "YUYV" : "RGB565");
    return yuv;
}

static void camera_store_preview_rgb(camera_fb_t* fb) {
    s_preview_w = 0;
    s_preview_h = 0;
    if (!fb || !fb->buf || fb->width == 0 || fb->height == 0) return;
    size_t need = (size_t)fb->width * (size_t)fb->height * sizeof(uint16_t);
    if (fb->len < need) return;
    if (!camera_ensure_preview_buf()) return;
    int w = fb->width > 320 ? 320 : (int)fb->width;
    int h = fb->height > 240 ? 240 : (int)fb->height;
    if ((w & 1) != 0) w &= ~1;
    Serial.printf("[camera] fb head %02x %02x %02x %02x %02x %02x %02x %02x\n",
                  fb->buf[0], fb->buf[1], fb->buf[2], fb->buf[3],
                  fb->buf[4], fb->buf[5], fb->buf[6], fb->buf[7]);
    if (camera_fb_looks_yuyv(fb->buf, need)) {
        camera_yuyv_to_rgb565(fb->buf, s_preview_rgb, w, h);
    } else {
        memcpy(s_preview_rgb, fb->buf, (size_t)w * (size_t)h * sizeof(uint16_t));
    }
    s_preview_w = w;
    s_preview_h = h;
}

bool camera_push_preview_rgb565(void) {
    if (s_preview_rgb == nullptr || s_preview_w <= 0 || s_preview_h <= 0) return false;
    // GC0308 RGB565_BE bytes already match M5GFX's swap565_t memory layout.
    // setSwapBytes(true) instead treats them as native rgb565_t and swaps the
    // red/green/blue bit fields, which only corrupts the on-device preview.
    bool oldSwap = M5.Display.getSwapBytes();
    M5.Display.setSwapBytes(false);
    M5.Display.pushImage(0, 0, s_preview_w, s_preview_h, s_preview_rgb);
    M5.Display.setSwapBytes(oldSwap);
    return true;
}

static void camera_release_xclk_pin(void) {
    if (camera_config.pin_xclk < 0) return;
    ledcDetachPin(camera_config.pin_xclk);
    gpio_reset_pin((gpio_num_t)camera_config.pin_xclk);
    Serial.printf("[camera] XCLK GPIO%d released\n", camera_config.pin_xclk);
}

// AW9523 P1_0 is GC0308 CAM_RST (active low). Pulse while In_I2C is still up.
// Other CoreS3 services share this bus, so tolerate a transient failed RMW and
// never probe the sensor while reset is still asserted.
static bool camera_pulse_gc0308_reset(void) {
    static constexpr uint8_t kAw9523 = 0x58;
    static constexpr uint8_t kP1Out = 0x03;
    static constexpr uint8_t kCamRst = 1u << 0;
    bool lo = M5.In_I2C.bitOff(kAw9523, kP1Out, kCamRst, 400000);
    delay(10);
    bool hi = false;
    int high_attempts = 0;
    for (; high_attempts < 5 && !hi; ++high_attempts) {
        hi = M5.In_I2C.bitOn(kAw9523, kP1Out, kCamRst, 400000);
        if (!hi) delay(10);
    }
    delay(20);
    Serial.printf("[camera] AW9523 CAM_RST pulse lo=%d hi=%d high_attempts=%d\n",
                  lo ? 1 : 0, hi ? 1 : 0, high_attempts);
    return hi;
}

static void camera_log_dma_heap(const char* stage) {
    Serial.printf("[camera] DMA heap %s: free=%u largest=%u config_max=%u\n",
                  stage,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
                  (unsigned)CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX);
}

static esp_err_t camera_driver_initialize(int attempts) {
    camera_config.sccb_i2c_port = M5.In_I2C.getPort();
    esp_err_t last_err = ESP_FAIL;

    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (!camera_pulse_gc0308_reset()) {
            last_err = ESP_ERR_TIMEOUT;
            Serial.printf("[camera] reset release failed; skipping init attempt %d/%d\n",
                          attempt + 1, attempts);
            delay(80);
            continue;
        }
        Serial.printf("[camera] using existing In_I2C port=%d init attempt=%d/%d\n",
                      camera_config.sccb_i2c_port, attempt + 1, attempts);
        camera_log_dma_heap("before init");

        last_err = esp_camera_init(&camera_config);
        if (last_err != ESP_OK) {
            Serial.printf("[camera] init attempt %d failed: 0x%x\n",
                          attempt + 1, (unsigned)last_err);
            delay(80);
            continue;
        }

        bool warmup_ok = true;
        for (int i = 0; i < 2; ++i) {
            camera_fb_t* warm = esp_camera_fb_get();
            if (!warm) {
                Serial.printf("[camera] warmup frame %d missed\n", i);
                warmup_ok = false;
                last_err = ESP_ERR_TIMEOUT;
                break;
            }
            Serial.printf("[camera] warmup frame %d ok: %ux%u %u bytes\n",
                          i, (unsigned)warm->width, (unsigned)warm->height,
                          (unsigned)warm->len);
            esp_camera_fb_return(warm);
        }

        if (warmup_ok) return ESP_OK;

        esp_camera_deinit();
        delay(80);
        Serial.println("[camera] driver fully deinitialized before retry");
    }

    return last_err;
}

static bool camera_reinitialize_driver_once() {
    if (camera_session_open) {
        esp_camera_deinit();
        camera_session_open = false;
        delay(80);
    }

    esp_err_t err = camera_driver_initialize(1);
    camera_session_open = (err == ESP_OK);
    if (!camera_session_open) {
        Serial.printf("[camera] driver recovery failed: 0x%x\n", (unsigned)err);
    }
    return camera_session_open;
}

static camera_fb_t* camera_get_frame_with_recovery() {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) return fb;

    Serial.println("[camera] frame missed; performing one full driver recovery");
    if (!camera_reinitialize_driver_once()) return nullptr;
    return esp_camera_fb_get();
}

static esp_err_t camera_session_begin(void){

    Serial.println("[camera] waiting for sensor bus");
    if (!camera_sensor_bus_try_lock(3000)) {
        Serial.println("[camera] sensor bus timeout");
        return ESP_ERR_TIMEOUT;
    }
    if (camera_session_open) {
        Serial.println("[camera] invalid open session");
        camera_sensor_bus_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    Serial.println("[camera] sensor bus acquired");
    camera_set_hardware_busy(true);
    {
        uint32_t t0 = millis();
        while (g_gesture_playing && (millis() - t0 < 2000)) delay(10);
    }
    g_avatar_render_pause = true;
    {
        uint32_t t0 = millis();
        while (!g_avatar_sd_paused && (millis() - t0 < 400)) delay(2);
    }
    enterMutexAudio();
    camera_audio_held = true;
    while (M5.Speaker.isPlaying()) delay(2);
    if (M5.Mic.isRunning()) M5.Mic.end();
    if (M5.Speaker.isRunning()) M5.Speaker.end();
    delay(20);
    camera_pause_websocket();
    if (robot && robot->servo
        && robot->servo->servoType() == ServoType::PWM) {
        uint32_t waitStart = millis();
        while (robot->servo->isMoving() && millis() - waitStart < 5000) delay(10);
        if (robot->servo->isMoving()) {
            Serial.println("[camera] servo wait timeout; suspending PWM");
        }
        camera_servo_suspended = robot->servo->suspendPwmForCamera();
    } else if (robot && robot->servo
               && robot->servo->servoType() == ServoType::M5_SCS) {
        Serial.println("[camera] M5_SCS remains on PY32/Serial2; no PWM GPIO detach");
    }

    // CoreS3 camera SCCB and PY32 share the already initialized internal bus.
    // The camera driver borrows that port and therefore must not release,
    // install, or later re-begin it.
    esp_err_t err = camera_driver_initialize(2);
    if (err != ESP_OK) {
        camera_release_xclk_pin();
        if (camera_servo_suspended && robot && robot->servo) robot->servo->resumePwmAfterCamera();
        camera_servo_suspended = false;
        camera_set_hardware_busy(false);
        camera_sensor_bus_unlock();
        if (camera_audio_held) {
            exitMutexAudio();
            camera_audio_held = false;
        }
        camera_resume_websocket();
        Serial.printf("[camera] init failed after recovery: 0x%x\n", (unsigned)err);
        camera_log_dma_heap("after init failure");
        M5.Display.println("Camera Init Failed");
        return err;
    }

    camera_session_open = true;
    Serial.println("[camera] RGB565 capture: internal DMA bounce -> PSRAM FB");
    Serial.println("[camera] psram direct DMA = OFF");
    Serial.println("[camera] initialized QVGA RGB565");
    camera_log_dma_heap("after init");
    return ESP_OK;
}

static void camera_session_end(void) {
    if (camera_session_open) {
        esp_camera_deinit();
        camera_session_open = false;
    }
    camera_release_xclk_pin();
    if (camera_servo_suspended && robot && robot->servo) robot->servo->resumePwmAfterCamera();
    camera_servo_suspended = false;
    camera_set_hardware_busy(false);
    camera_sensor_bus_unlock();
    if (camera_audio_held) {
        exitMutexAudio();
        camera_audio_held = false;
    }
    camera_resume_websocket();
    Serial.printf("[camera] session closed, borrowed In_I2C port=%d preserved\n",
                  camera_config.sccb_i2c_port);
    camera_log_dma_heap("after deinit");
}

esp_err_t camera_init(void) {
    Serial.println("[camera] lazy capture ready");
    return ESP_OK;
}

#if defined(ENABLE_FACE_DETECT)
static void draw_face_boxes(fb_data_t *fb, std::list<dl::detect::result_t> *results, int face_id)
{
    int x, y, w, h;
    uint32_t color = FACE_COLOR_YELLOW;
    if (face_id < 0)
    {
        color = FACE_COLOR_RED;
    }
    else if (face_id > 0)
    {
        color = FACE_COLOR_GREEN;
    }
    if(fb->bytes_per_pixel == 2){
        //color = ((color >> 8) & 0xF800) | ((color >> 3) & 0x07E0) | (color & 0x001F);
        color = ((color >> 16) & 0x001F) | ((color >> 3) & 0x07E0) | ((color << 8) & 0xF800);
    }
    int i = 0;
    for (std::list<dl::detect::result_t>::iterator prediction = results->begin(); prediction != results->end(); prediction++, i++)
    {
        // rectangle box
        x = (int)prediction->box[0];
        y = (int)prediction->box[1];

        // yが負の数のときにfb_gfx_drawFastHLine()でメモリ破壊してリセットする不具合の対策
        if(y < 0){
           y = 0;
        }

        w = (int)prediction->box[2] - x + 1;
        h = (int)prediction->box[3] - y + 1;

        //Serial.printf("x:%d y:%d w:%d h:%d\n", x, y, w, h);

        if((x + w) > fb->width){
            w = fb->width - x;
        }
        if((y + h) > fb->height){
            h = fb->height - y;
        }

        //Serial.printf("x:%d y:%d w:%d h:%d\n", x, y, w, h);

        //fb_gfx_fillRect(fb, x+10, y+10, w-20, h-20, FACE_COLOR_RED);  //モザイク
        fb_gfx_drawFastHLine(fb, x, y, w, color);
        fb_gfx_drawFastHLine(fb, x, y + h - 1, w, color);
        fb_gfx_drawFastVLine(fb, x, y, h, color);
        fb_gfx_drawFastVLine(fb, x + w - 1, y, h, color);

#if TWO_STAGE
        // landmarks (left eye, mouth left, nose, right eye, mouth right)
        int x0, y0, j;
        for (j = 0; j < 10; j+=2) {
            x0 = (int)prediction->keypoint[j];
            y0 = (int)prediction->keypoint[j+1];
            fb_gfx_fillRect(fb, x0, y0, 3, 3, color);
        }
#endif
    }
}
#endif  // ENABLE_FACE_DETECT (draw_face_boxes)

bool camera_capture_and_face_detect(void){
  bool isDetected = false;

  if (camera_session_begin() != ESP_OK) return false;

  //acquire a frame
  camera_fb_t *fb = camera_get_frame_with_recovery();
  if (!fb) {
    //Serial.println("Camera Capture Failed");
    M5.Display.println("Camera Capture Failed");
    camera_session_end();
    return false;
  }

#if defined(ENABLE_FACE_DETECT)
  int face_id = 0;

#if TWO_STAGE
  HumanFaceDetectMSR01 s1(0.1F, 0.5F, 10, 0.2F);
  HumanFaceDetectMNP01 s2(0.5F, 0.3F, 5);
  std::list<dl::detect::result_t> &candidates = s1.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3});
  std::list<dl::detect::result_t> &results = s2.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3}, candidates);
#else
  HumanFaceDetectMSR01 s1(0.3F, 0.5F, 10, 0.2F);
  std::list<dl::detect::result_t> &results = s1.infer((uint16_t *)fb->buf, {(int)fb->height, (int)fb->width, 3});
#endif


  
  if (results.size() > 0) {
    //Serial.printf("Face detected : %d\n", results.size());

    isDetected = true;

    fb_data_t rfb;
    rfb.width = fb->width;
    rfb.height = fb->height;
    rfb.data = fb->buf;
    rfb.bytes_per_pixel = 2;
    rfb.format = FB_RGB565;

    draw_face_boxes(&rfb, &results, face_id);

  }
#endif  //ENABLE_FACE_DETECT

  if(isSubWindowON){
    avatar.updateSubWindowCam565(fb->buf);
  }

  //Serial.println("\n<<< heap size before fb return >>>");  
  //check_heap_free_size();

  //return the frame buffer back to the driver for reuse
  esp_camera_fb_return(fb);
  camera_session_end();

  //Serial.println("<<< heap size after fb return >>>");  
  //check_heap_free_size();

  return isDetected;
}



bool camera_capture_base64(String& out)
{
  if (camera_session_begin() != ESP_OK) return false;
  //acquire a frame
  camera_fb_t *fb = camera_get_frame_with_recovery();

  if (!fb) {
    Serial.println("Camera Capture Failed");
    camera_session_end();
    return false;
  }

  size_t jpg_buf_len = 0;
  uint8_t *jpg_buf   = NULL;
  int ret;
  bool jpeg_converted = frame2jpg(fb, 80, &jpg_buf, &jpg_buf_len);
  esp_camera_fb_return(fb);
  fb = NULL;
  if (!jpeg_converted) {
    Serial.println("JPEG compression failed");
    camera_session_end();
    return false;
  }

  camera_session_end();

#if 1 //debug
  File fdst = SPIFFS.open("/capture.jpg", FILE_WRITE);
  if ((ret = fdst.write(jpg_buf, jpg_buf_len)) < jpg_buf_len) {
    Serial.printf("write spiffs failed: %d - %d\n", ret, jpg_buf_len);
    fdst.close();
    free(jpg_buf);
    return false;
  }
  fdst.close();
#endif


  out = base64::encode(jpg_buf, jpg_buf_len);

#if 0 //debug
  fdst = SPIFFS.open("/capture_base64.txt", FILE_WRITE);
  if ((ret = fdst.write((const uint8_t*)out.c_str(), out.length())) < out.length()) {
    Serial.printf("write spiffs failed: %d - %d\n", ret, out.length());
    return false;
  }
#endif

  free(jpg_buf);
  jpg_buf = NULL;
  
  return true;
}


static void put_le16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

// s_preview_rgb stores GC0308 RGB565_BE bytes. Reassemble the native RGB565
// value before expanding it to the BMP's BGR888 byte order.
static void rgb565_to_bgr(uint16_t p, uint8_t& b, uint8_t& g, uint8_t& r) {
    uint16_t v = (uint16_t)((p << 8) | (p >> 8));
    r = (uint8_t)(((v >> 11) & 0x1F) << 3);
    g = (uint8_t)(((v >> 5) & 0x3F) << 2);
    b = (uint8_t)((v & 0x1F) << 3);
}

static bool camera_write_preview_bmp(const char* destPath) {
    if (s_preview_rgb == nullptr || s_preview_w <= 0 || s_preview_h <= 0) {
        Serial.println("[camera] no RGB565 to write");
        return false;
    }
    const int w = s_preview_w;
    const int h = s_preview_h;
    const int rowStride = (w * 3 + 3) & ~3;
    const uint32_t pixelBytes = (uint32_t)rowStride * (uint32_t)h;
    const uint32_t fileSize = 54 + pixelBytes;

    uint8_t hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B';
    hdr[1] = 'M';
    put_le32(hdr + 2, fileSize);
    put_le32(hdr + 10, 54);
    put_le32(hdr + 14, 40);
    put_le32(hdr + 18, (uint32_t)w);
    put_le32(hdr + 22, (uint32_t)(-h));  // top-down
    put_le16(hdr + 26, 1);
    put_le16(hdr + 28, 24);
    put_le32(hdr + 34, pixelBytes);

    uint8_t* row = (uint8_t*)heap_caps_malloc((size_t)rowStride, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (row == nullptr) row = (uint8_t*)malloc((size_t)rowStride);
    if (row == nullptr) {
        Serial.println("[camera] BMP row alloc failed");
        return false;
    }

    sd_bus_lock();
    SD.mkdir("/app");
    SD.mkdir("/app/AiStackChanEx");
    String photoDir = String(APP_DATA_PATH) + "photo";
    SD.mkdir(photoDir.c_str());
    if (SD.exists(destPath)) SD.remove(destPath);
    File f = SD.open(destPath, FILE_WRITE);
    bool ok = false;
    size_t wrote = 0;
    if (f) {
        wrote = f.write(hdr, 54);
        ok = (wrote == 54);
        for (int y = 0; ok && y < h; y++) {
            memset(row, 0, (size_t)rowStride);
            for (int x = 0; x < w; x++) {
                uint8_t b, g, r;
                rgb565_to_bgr(s_preview_rgb[y * w + x], b, g, r);
                row[x * 3 + 0] = b;
                row[x * 3 + 1] = g;
                row[x * 3 + 2] = r;
            }
            size_t n = f.write(row, (size_t)rowStride);
            if (n != (size_t)rowStride) ok = false;
            wrote += n;
        }
        f.close();
    }
    sd_bus_unlock();
    free(row);

    if (!ok) {
        Serial.println("[camera] SD BMP write failed");
        return false;
    }
    Serial.printf("[camera] saved %s (%u bytes)\n", destPath, (unsigned)wrote);
    return true;
}

bool camera_capture_save_to(const char* destPath, String& outPath)
{
  if (destPath == nullptr || destPath[0] == '\0') return false;
  Serial.printf("[camera] capture request: %s\n", destPath);
  if (!camera_ensure_preview_buf()) return false;
  if (camera_session_begin() != ESP_OK) return false;
  Serial.println("[camera] waiting for frame");
  camera_fb_t *fb = camera_get_frame_with_recovery();
  if (!fb) {
    Serial.println("[camera] frame capture failed");
    camera_session_end();
    return false;
  }
  Serial.printf("[camera] frame ready: %ux%u, %u bytes, format=%d\n",
                (unsigned)fb->width, (unsigned)fb->height,
                (unsigned)fb->len, (int)fb->format);
  camera_store_preview_rgb(fb);
  esp_camera_fb_return(fb);
  fb = NULL;
  camera_session_end();

  if (s_preview_rgb == nullptr || s_preview_w <= 0 || s_preview_h <= 0) {
    Serial.println("[camera] preview RGB copy failed");
    return false;
  }
  if (!camera_write_preview_bmp(destPath)) return false;
  outPath = destPath;
  return true;
}

bool camera_capture_save_sd(String& outPath)
{
  String path = String(APP_DATA_PATH) + "photo/" + String(millis()) + ".bmp";
  return camera_capture_save_to(path.c_str(), outPath);
}

#endif
