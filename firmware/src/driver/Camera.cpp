#if defined(ENABLE_CAMERA)

#include <Arduino.h>
#include <M5Unified.h>
#include <Avatar.h>
#include "Camera.h"
#include <SPIFFS.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <base64.h>
#include "DebugTools.h"
#include "share/SdBus.h"
#include "share/Mutex.h"
#include "Robot.h"
#include "CameraVision.h"
#include "CameraDmaConfig.h"
#include "cam_hal.h"
#include "llm/ChatGPT/FunctionCall.h"
using namespace m5avatar;
extern Avatar avatar;
namespace m5avatar {
extern volatile bool g_avatar_render_pause;
}


bool isSubWindowON = true;
bool isSilentMode = false;

static camera_config_t camera_config = {
    .pin_pwdn     = -1,
    .pin_reset    = -1,
    .pin_xclk     = 2,
    .pin_sscb_sda = 12,
    .pin_sscb_scl = 11,

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
};

static bool camera_session_open = false;
static bool camera_servo_suspended = false;
static bool camera_audio_held = false;

static void camera_log_dma_heap(const char* stage) {
    Serial.printf("[camera] DMA heap %s: free=%u largest=%u config_max=%u\n",
                  stage,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
                  (unsigned)CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX);
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
    Serial.println("[camera] eof-ovf fix: psram dma");
    g_avatar_render_pause = true;
    camera_set_hardware_busy(true);
    enterMutexAudio();
    camera_audio_held = true;
    while (M5.Speaker.isPlaying()) delay(2);
    if (M5.Mic.isEnabled()) M5.Mic.end();
    if (M5.Speaker.isEnabled()) M5.Speaker.end();
    delay(20);
    if (robot && robot->servo) {
        uint32_t waitStart = millis();
        while (robot->servo->isMoving() && millis() - waitStart < 5000) delay(10);
        if (robot->servo->isMoving()) {
            Serial.println("[camera] servo wait timeout; suspending PWM");
        }
        camera_servo_suspended = robot->servo->suspendPwmForCamera();
    }

    //initialize the camera
    M5.In_I2C.release();
    Serial.println("[camera] internal I2C released; initializing GC0308");
    camera_log_dma_heap("before init");
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        M5.In_I2C.begin();
        if (camera_servo_suspended && robot && robot->servo) robot->servo->resumePwmAfterCamera();
        camera_servo_suspended = false;
        camera_set_hardware_busy(false);
        camera_sensor_bus_unlock();
        if (camera_audio_held) {
            exitMutexAudio();
            camera_audio_held = false;
        }
        Serial.printf("[camera] init failed: 0x%x (I2C restored)\n", (unsigned)err);
        camera_log_dma_heap("after init failure");
        //Serial.println("Camera Init Failed");
        M5.Display.println("Camera Init Failed");
        return err;
    }

    // esp_camera_init() enables VSYNC before we return. Stop the stream
    // before touching PCLK so a 20 MHz burst cannot overflow the EOF queue.
    cam_stop();
    sensor_t *s = esp_camera_sensor_get();
    s->set_hmirror(s, 0);

    // Keep CoreS3's 20 MHz XCLK, but slow the GC0308 pixel stream enough for
    // the smaller internal DMA buffer. Register 0x28 uses bits 6:4 as
    // (divider - 1) and bits 2:0 as the high clocks per output pulse. 0x32 is
    // therefore a balanced 2:2 duty cycle at XCLK / 4 (about 5 MHz PCLK).
    s->set_reg(s, 0xfe, 0xff, 0x00);
    int pclkResult = s->set_reg(s, 0x28, 0x77, 0x32);
    int pclkConfig = s->get_reg(s, 0x28, 0x77);
    Serial.printf("[camera] GC0308 PCLK /4 set: result=%d reg=0x%02x\n",
                  pclkResult, pclkConfig);
    delay(50);
    cam_start();
    Serial.println("[camera] stream restarted");
    for (int i = 0; i < 2; i++) {
        camera_fb_t *warm = cam_take(pdMS_TO_TICKS(500));
        if (!warm) {
            Serial.printf("[camera] warmup frame %d missed\n", i);
            break;
        }
        cam_give(warm);
    }

    camera_session_open = true;
    Serial.println("[camera] initialized QVGA RGB565");
    camera_log_dma_heap("after init");
    return ESP_OK;
}

static void camera_session_end(void) {
    if (camera_session_open) {
        esp_camera_deinit();
        camera_session_open = false;
    }
    bool restored = M5.In_I2C.begin();
    if (camera_servo_suspended && robot && robot->servo) robot->servo->resumePwmAfterCamera();
    camera_servo_suspended = false;
    camera_set_hardware_busy(false);
    camera_sensor_bus_unlock();
    if (camera_audio_held) {
        exitMutexAudio();
        camera_audio_held = false;
    }
    Serial.printf("[camera] session closed, internal I2C restored=%d\n", restored ? 1 : 0);
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
  camera_fb_t *fb = esp_camera_fb_get();
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
  camera_fb_t *fb = esp_camera_fb_get();

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


bool camera_capture_save_to(const char* destPath, String& outPath)
{
  if (destPath == nullptr || destPath[0] == '\0') return false;
  Serial.printf("[camera] capture request: %s\n", destPath);
  if (camera_session_begin() != ESP_OK) return false;
  Serial.println("[camera] waiting for frame");
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[camera] frame capture failed");
    camera_session_end();
    return false;
  }
  Serial.printf("[camera] frame ready: %ux%u, %u bytes, format=%d\n",
                (unsigned)fb->width, (unsigned)fb->height,
                (unsigned)fb->len, (int)fb->format);

  size_t jpg_buf_len = 0;
  uint8_t *jpg_buf   = NULL;
  Serial.println("[camera] JPEG conversion started");
  bool jpeg_converted = frame2jpg(fb, 80, &jpg_buf, &jpg_buf_len);
  esp_camera_fb_return(fb);
  if (!jpeg_converted) {
    Serial.println("[camera] JPEG conversion failed");
    camera_session_end();
    return false;
  }
  Serial.printf("[camera] JPEG conversion complete: %u bytes\n",
                (unsigned)jpg_buf_len);
  camera_session_end();

  sd_bus_lock();
  SD.mkdir("/app");
  SD.mkdir("/app/AiStackChanEx");
  String photoDir = String(APP_DATA_PATH) + "photo";
  SD.mkdir(photoDir.c_str());
  // FILE_WRITE appends on Arduino SD. A preview left by a reset must not be
  // concatenated with the next JPEG.
  if (SD.exists(destPath)) SD.remove(destPath);
  File f = SD.open(destPath, FILE_WRITE);
  bool ok = false;
  if (f) {
    size_t wrote = f.write(jpg_buf, jpg_buf_len);
    f.close();
    ok = (wrote == jpg_buf_len);
  }
  sd_bus_unlock();
  free(jpg_buf);

  if (!ok) {
    Serial.println("[camera] SD write failed");
    return false;
  }
  outPath = destPath;
  Serial.printf("[camera] saved %s (%d bytes)\n", destPath, (int)jpg_buf_len);
  return true;
}

bool camera_capture_save_sd(String& outPath)
{
  String path = String(APP_DATA_PATH) + "photo/" + String(millis()) + ".jpg";
  return camera_capture_save_to(path.c_str(), outPath);
}

#endif
