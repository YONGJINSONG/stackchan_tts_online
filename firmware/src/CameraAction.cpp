#include "CameraAction.h"

#include <SD.h>
#include <M5Unified.h>
#include <Avatar.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_system.h>

#include "share/SdBus.h"
#include "llm/ChatGPT/FunctionCall.h"
#include "Gesture.h"

#if defined(ENABLE_CAMERA)
#include "driver/Camera.h"
#include "driver/WavClip.h"
#endif

using namespace m5avatar;

extern Avatar avatar;

namespace m5avatar {
extern volatile bool g_avatar_render_pause;
extern volatile bool g_avatar_sd_paused;
}

namespace {

enum class ActionState : uint8_t {
    Idle,
    Centering,
    Countdown,
    Running,
    ReviewSave,
    Saving,
    Done
};

SemaphoreHandle_t s_mux = nullptr;

ActionState s_state = ActionState::Idle;

String s_result;
String s_tempPath;
String s_savedPath;

bool s_abandonWhenDone = false;
bool s_saved = false;

int s_countdownDigit = 0;
uint32_t s_countdownNextMs = 0;

constexpr int kBarH = 72;
constexpr size_t kMaxPreviewJpegSize = 1024 * 1024;

// 수정:
// "\.jpg"가 아니라 ".jpg"
const char* kPreviewPath =
    "/app/AiStackChanEx/photo/.preview.bmp";


// ============================================================
// Mutex
// ============================================================

void ensure_mux() {
    if (s_mux == nullptr) {
        s_mux = xSemaphoreCreateMutex();
    }
}

void lock_action() {
    ensure_mux();

    if (s_mux != nullptr) {
        xSemaphoreTake(
            s_mux,
            portMAX_DELAY
        );
    }
}

void unlock_action() {
    if (s_mux != nullptr) {
        xSemaphoreGive(s_mux);
    }
}


// ============================================================
// Time
// millis() overflow-safe
// ============================================================

bool time_reached(
    uint32_t now,
    uint32_t target
) {
    return static_cast<int32_t>(
        now - target
    ) >= 0;
}


// ============================================================
// Path
// ============================================================

String photo_dir() {
    String dir =
        String(APP_DATA_PATH);

    if (!dir.endsWith("/")) {
        dir += "/";
    }

    dir += "photo";

    return dir;
}

bool ensure_photo_dir() {
    String dir =
        photo_dir();

    sd_bus_lock();

    bool ok =
        SD.exists(
            dir.c_str()
        );

    if (!ok) {
        ok =
            SD.mkdir(
                dir.c_str()
            );
    }

    sd_bus_unlock();

    if (!ok) {
        Serial.printf(
            "[camera-action] failed to create photo dir: %s\n",
            dir.c_str()
        );
    }

    return ok;
}


// ============================================================
// UI
// ============================================================

void pause_avatar_for_ui() {
    g_avatar_render_pause = true;
    uint32_t t0 = millis();
    while (!g_avatar_sd_paused && (millis() - t0 < 400)) {
        delay(2);
    }
}

void prepare_overlay_text() {
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.setFont(&fonts::efontKR_16);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.clearClipRect();
}

void show_countdown_digit(
    int digit
) {
    pause_avatar_for_ui();
    avatar.setSpeechText("");
    avatar.set_isSubWindowEnable(false);

    const int w = M5.Display.width();
    const int h = M5.Display.height();

    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(12);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", digit);
    M5.Display.drawString(buf, w / 2, h / 2);

    prepare_overlay_text();

    Serial.printf(
        "[camera-action] countdown %d\n",
        digit
    );
}

int bar_top() {
    int h =
        M5.Display.height();

    return (h > kBarH)
        ? (h - kBarH)
        : 0;
}

void draw_buttons(
    const char* prompt,
    const char* left,
    const char* right
) {
    const int w = M5.Display.width();
    const int h = M5.Display.height();
    const int top = bar_top();

    prepare_overlay_text();

    M5.Display.fillRect(0, top, w, h - top, TFT_BLACK);

    int btnTop = top + 4;

    if (prompt != nullptr && prompt[0] != '\0') {
        M5.Display.setTextDatum(textdatum_t::top_center);
        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.drawString(prompt, w / 2, top + 2);
        btnTop = top + 22;
    }

    const int btnH = h - btnTop - 4;
    const int half = w / 2;
    const int btnW = half - 8;

    M5.Display.fillRoundRect(4, btnTop, btnW, btnH, 8, TFT_DARKGREY);
    M5.Display.fillRoundRect(half + 4, btnTop, w - half - 8, btnH, 8, TFT_DARKGREY);

    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREY);
    M5.Display.drawString(left, 4 + btnW / 2, btnTop + btnH / 2);
    M5.Display.drawString(right, half + 4 + (w - half - 8) / 2, btnTop + btnH / 2);

    prepare_overlay_text();
}


#if defined(ENABLE_CAMERA)


// ============================================================
// File helpers
// ============================================================

bool remove_file_if_exists(
    const String& path
) {
    if (!path.length()) {
        return true;
    }

    sd_bus_lock();

    bool ok = true;

    if (
        SD.exists(
            path.c_str()
        )
    ) {
        ok =
            SD.remove(
                path.c_str()
            );
    }

    sd_bus_unlock();

    if (!ok) {
        Serial.printf(
            "[camera-action] failed to remove: %s\n",
            path.c_str()
        );
    }

    return ok;
}


// ============================================================
// Preview
// ============================================================

void draw_preview_jpeg(
    const String& path
) {
    pause_avatar_for_ui();
    avatar.set_isSubWindowEnable(false);
    avatar.setSpeechText("");
    prepare_overlay_text();

    const int w = M5.Display.width();
    const int h = M5.Display.height();

    M5.Display.fillScreen(TFT_BLACK);

    bool ok = camera_push_preview_rgb565();
    if (ok) {
        Serial.println("[camera-action] preview from RGB565");
        return;
    }

    sd_bus_lock();
    File f = SD.open(path.c_str(), FILE_READ);
    if (f) {
        size_t n = f.size();
        if (n > 0 && n <= kMaxPreviewJpegSize) {
            uint8_t* buf = static_cast<uint8_t*>(ps_malloc(n));
            if (buf == nullptr) {
                buf = static_cast<uint8_t*>(malloc(n));
            }
            if (buf != nullptr) {
                size_t got = f.read(buf, n);
                f.close();
                sd_bus_unlock();
                if (got == n) {
                    M5Canvas spr(&M5.Display);
                    spr.setPsram(true);
                    spr.setColorDepth(16);
                    if (spr.createSprite(w, h)) {
                        spr.fillSprite(TFT_BLACK);
                        ok = spr.drawJpg(buf, n, 0, 0, w, h);
                        if (ok) {
                            spr.pushSprite(0, 0);
                        }
                        spr.deleteSprite();
                    }
                    if (!ok) {
                        ok = M5.Display.drawJpg(buf, n, 0, 0, w, h);
                    }
                }
                free(buf);
            } else {
                Serial.printf(
                    "[camera-action] preview alloc failed: %u bytes\n",
                    static_cast<unsigned>(n)
                );
                f.close();
                sd_bus_unlock();
            }
        } else {
            Serial.printf(
                "[camera-action] invalid preview size: %u bytes\n",
                static_cast<unsigned>(n)
            );
            f.close();
            sd_bus_unlock();
        }
    } else {
        Serial.printf(
            "[camera-action] preview open failed: %s\n",
            path.c_str()
        );
        sd_bus_unlock();
    }

    if (!ok) {
        prepare_overlay_text();
        M5.Display.drawString("미리보기 실패", 20, 40);
    }
}

void show_save_ui(
    const String& path
) {
    draw_preview_jpeg(path);

    draw_buttons(
        "",
        "저장하기",
        "저장안함"
    );
}


/*
 * Preview가 끝났을 때 Avatar UI 복구
 *
 * 현재 코드는 preview 시작 시
 * set_isSubWindowEnable(false)를 호출하므로
 * 종료 시 true로 되돌린다.
 *
 * 프로젝트 전체에서 SubWindow를 항상 false로
 * 사용하는 설정이라면 아래 true 한 줄만 제거하면 된다.
 */
void finish_preview_ui() {
    avatar.setSpeechText("");
    avatar.set_isSubWindowEnable(true);
    prepare_overlay_text();
    g_avatar_render_pause = false;
}


// ============================================================
// Result
// 이 함수 호출 시 action mutex가 잡혀 있어야 함
// ============================================================

String build_result_locked() {
    String msg =
        s_saved
            ? "사진을 저장했어요."
            : "사진을 저장하지 않았어요.";

    String json =
        "{\"result\":\"";

    json += msg;

    json += "\",\"saved\":";

    json +=
        s_saved
            ? "true"
            : "false";

    if (
        s_saved &&
        s_savedPath.length()
    ) {
        json +=
            ",\"path\":\"";

        json +=
            s_savedPath;

        json += "\"";
    }

    json += "}";

    return json;
}


// ============================================================
// Camera capture
// ============================================================

bool capture_preview(
    String& capturedPath
) {
    capturedPath = "";

    if (!ensure_photo_dir()) {
        return false;
    }

    /*
     * 이전 촬영 도중 reset / power-off 때문에
     * 남아 있을 수 있는 stale preview 제거
     */
    remove_file_if_exists(
        String(kPreviewPath)
    );

    String actualPath;

    bool ok =
        camera_capture_save_to(
            kPreviewPath,
            actualPath
        );

    if (!ok) {
        Serial.println(
            "[camera-action] camera_capture_save_to failed"
        );

        // 부분 파일이 생겼을 수도 있으므로 삭제
        remove_file_if_exists(
            String(kPreviewPath)
        );

        return false;
    }

    /*
     * Camera 구현이 path를 반환하지 않는 경우
     * 요청한 preview path 사용
     */
    if (!actualPath.length()) {
        actualPath =
            String(kPreviewPath);
    }

    /*
     * 실제 파일 존재 + 크기 검증
     */
    sd_bus_lock();

    bool valid =
        SD.exists(
            actualPath.c_str()
        );

    size_t fileSize = 0;

    if (valid) {
        File f =
            SD.open(
                actualPath.c_str(),
                FILE_READ
            );

        if (f) {
            fileSize =
                f.size();

            f.close();
        }

        valid =
            fileSize > 0;
    }

    sd_bus_unlock();

    if (!valid) {
        Serial.println(
            "[camera-action] captured file invalid"
        );

        remove_file_if_exists(
            actualPath
        );

        return false;
    }

    capturedPath =
        actualPath;

    Serial.printf(
        "[camera-action] capture ok: %s (%u bytes)\n",
        capturedPath.c_str(),
        static_cast<unsigned>(
            fileSize
        )
    );

    return true;
}


// ============================================================
// Unique permanent filename
//
// 반드시 sd_bus_lock() 상태에서 호출
// ============================================================

String make_unique_photo_path_locked() {
    String dir =
        photo_dir();

    /*
     * millis() 단독은 재부팅 후 충돌 가능.
     *
     * millis() + esp_random()을 조합해
     * 사실상 충돌을 제거한다.
     */
    for (
        int retry = 0;
        retry < 32;
        ++retry
    ) {
        uint32_t now =
            millis();

        uint32_t rnd =
            esp_random();

        String path =
            dir +
            "/" +
            String(now) +
            "_" +
            String(rnd, HEX) +
            ".bmp";

        if (
            !SD.exists(
                path.c_str()
            )
        ) {
            return path;
        }
    }

    return "";
}


// ============================================================
// Save preview permanently
// ============================================================

bool commit_save(
    const String& sourcePath,
    String& savedPath
) {
    savedPath = "";

    if (!sourcePath.length()) {
        Serial.println(
            "[camera-action] commit failed: empty source"
        );

        return false;
    }

    if (!ensure_photo_dir()) {
        return false;
    }

    sd_bus_lock();

    if (
        !SD.exists(
            sourcePath.c_str()
        )
    ) {
        Serial.printf(
            "[camera-action] commit source missing: %s\n",
            sourcePath.c_str()
        );

        sd_bus_unlock();
        return false;
    }

    String dest =
        make_unique_photo_path_locked();

    if (!dest.length()) {
        Serial.println(
            "[camera-action] unique filename generation failed"
        );

        sd_bus_unlock();
        return false;
    }


    // --------------------------------------------------------
    // 1. rename 우선
    // 같은 SD filesystem이면 가장 빠름
    // --------------------------------------------------------

    bool renamed =
        SD.rename(
            sourcePath.c_str(),
            dest.c_str()
        );

    if (renamed) {
        sd_bus_unlock();

        savedPath =
            dest;

        Serial.printf(
            "[camera-action] saved by rename: %s\n",
            dest.c_str()
        );

        return true;
    }


    // --------------------------------------------------------
    // 2. rename 실패 시 copy fallback
    // --------------------------------------------------------

    Serial.println(
        "[camera-action] rename failed, trying copy"
    );

    /*
     * FILE_WRITE가 환경에 따라 append처럼 동작할
     * 가능성에 대비해 destination을 먼저 제거
     */
    if (
        SD.exists(
            dest.c_str()
        )
    ) {
        SD.remove(
            dest.c_str()
        );
    }

    File src =
        SD.open(
            sourcePath.c_str(),
            FILE_READ
        );

    File dst =
        SD.open(
            dest.c_str(),
            FILE_WRITE
        );

    if (!src || !dst) {
        if (src) {
            src.close();
        }

        if (dst) {
            dst.close();
        }

        SD.remove(
            dest.c_str()
        );

        sd_bus_unlock();

        Serial.println(
            "[camera-action] copy open failed"
        );

        return false;
    }

    const size_t expectedSize =
        src.size();

    size_t totalWritten = 0;

    bool copyOk = true;

    uint8_t buf[4096];

    while (true) {
        size_t n =
            src.read(
                buf,
                sizeof(buf)
            );

        if (n == 0) {
            break;
        }

        size_t written =
            dst.write(
                buf,
                n
            );

        if (written != n) {
            Serial.printf(
                "[camera-action] SD write failed: %u/%u\n",
                static_cast<unsigned>(written),
                static_cast<unsigned>(n)
            );

            copyOk = false;
            break;
        }

        totalWritten +=
            written;
    }

    dst.flush();

    src.close();
    dst.close();

    if (
        !copyOk ||
        totalWritten != expectedSize
    ) {
        SD.remove(
            dest.c_str()
        );

        sd_bus_unlock();

        Serial.printf(
            "[camera-action] copy incomplete: %u/%u\n",
            static_cast<unsigned>(
                totalWritten
            ),
            static_cast<unsigned>(
                expectedSize
            )
        );

        return false;
    }


    /*
     * 완전한 복사가 확인된 뒤에만
     * preview 원본 삭제
     */
    bool sourceRemoved =
        SD.remove(
            sourcePath.c_str()
        );

    if (!sourceRemoved) {
        Serial.printf(
            "[camera-action] warning: preview remained: %s\n",
            sourcePath.c_str()
        );
    }

    sd_bus_unlock();

    savedPath =
        dest;

    Serial.printf(
        "[camera-action] saved by copy: %s\n",
        dest.c_str()
    );

    return true;
}

#endif

} // namespace


// ============================================================
// Public API
// ============================================================

bool camera_action_request_save() {

#if !defined(ENABLE_CAMERA)

    return false;

#else

    lock_action();

    if (
        s_state !=
        ActionState::Idle
    ) {
        ActionState state =
            s_state;

        unlock_action();

        Serial.printf(
            "[camera-action] rejected: busy state=%d\n",
            static_cast<int>(
                state
            )
        );

        return false;
    }

    s_result = "";

    s_tempPath = "";
    s_savedPath = "";

    s_saved = false;

    s_abandonWhenDone = false;

    s_countdownDigit = 0;
    s_countdownNextMs = 0;

    // Reserve the action before moving the servo. The countdown must not
    // consume time while a previous gesture is still finishing.
    s_state =
        ActionState::Centering;

    unlock_action();

    // Stop queued motion and command the configured origin before displaying
    // the countdown. The hold remains active until capture/review completes.
    gesture_set_motion_hold(true, true);

    lock_action();

    // The action may have been abandoned while waiting for a gesture task.
    if (s_state != ActionState::Centering) {
        unlock_action();
        gesture_set_motion_hold(false);
        return false;
    }

    s_countdownDigit = 3;
    s_countdownNextMs = millis() + 1000;
    s_state = ActionState::Countdown;

    unlock_action();

    show_countdown_digit(3);

    Serial.println(
        "[camera-action] servo at home; save queued"
    );

    return true;

#endif
}


bool camera_action_is_ui_active() {

#if !defined(ENABLE_CAMERA)

    return false;

#else

    lock_action();

    ActionState state =
        s_state;

    unlock_action();

    /*
     * Done은 사용자 UI가 끝난 상태.
     * 결과 소비 전이라도 카메라 화면 자체는 비활성.
     */
    return
        state == ActionState::Centering ||
        state == ActionState::Countdown ||
        state == ActionState::Running ||
        state == ActionState::ReviewSave ||
        state == ActionState::Saving;

#endif
}


void camera_action_on_touch(
    int16_t x,
    int16_t y
) {

#if !defined(ENABLE_CAMERA)

    (void)x;
    (void)y;

#else

    const int w =
        M5.Display.width();

    const int top =
        bar_top();

    if (y < top) {
        return;
    }


    // --------------------------------------------------------
    // ReviewSave → Saving 즉시 전환
    //
    // 저장 버튼 연타 / double touch race 방지
    // --------------------------------------------------------

    String tempPath;

    lock_action();

    if (
        s_state !=
        ActionState::ReviewSave
    ) {
        unlock_action();
        return;
    }

    s_state =
        ActionState::Saving;

    tempPath =
        s_tempPath;

    unlock_action();


    const bool saveSelected =
        x < (w / 2);


    // ========================================================
    // 저장하기
    // ========================================================

    if (saveSelected) {
        String permanentPath;

        bool ok =
            commit_save(
                tempPath,
                permanentPath
            );

        if (!ok) {
            /*
             * commit 실패 시 남아 있는 preview 삭제
             */
            remove_file_if_exists(
                tempPath
            );

            lock_action();

            bool abandoned =
                s_abandonWhenDone;

            s_tempPath = "";
            s_savedPath = "";
            s_saved = false;

            s_abandonWhenDone = false;

            if (abandoned) {
                s_result = "";

                s_state =
                    ActionState::Idle;
            } else {
                s_result =
                    "{\"error\":\"사진을 저장하지 못했어요.\"}";

                s_state =
                    ActionState::Done;
            }

            unlock_action();

            finish_preview_ui();

            if (abandoned) gesture_set_motion_hold(false);

            Serial.println(
                "[camera-action] permanent save failed"
            );

            return;
        }


        lock_action();

        bool abandoned =
            s_abandonWhenDone;

        s_tempPath = "";

        s_savedPath =
            permanentPath;

        s_saved = true;

        s_abandonWhenDone = false;

        if (abandoned) {
            /*
             * 저장 I/O 도중 abandon이 들어온 경우
             * 이미 정상 저장된 사진은 보존하지만
             * Function Call 결과는 폐기.
             */
            s_result = "";

            s_state =
                ActionState::Idle;

            s_savedPath = "";
            s_saved = false;

        } else {
            s_result =
                build_result_locked();

            s_state =
                ActionState::Done;
        }

        unlock_action();

        finish_preview_ui();

        if (abandoned) gesture_set_motion_hold(false);

        Serial.println(
            "[camera-action] save completed"
        );

        return;
    }


    // ========================================================
    // 저장안함
    // ========================================================

    remove_file_if_exists(
        tempPath
    );

    lock_action();

    bool abandoned =
        s_abandonWhenDone;

    s_tempPath = "";
    s_savedPath = "";

    s_saved = false;

    s_abandonWhenDone = false;

    if (abandoned) {
        s_result = "";

        s_state =
            ActionState::Idle;
    } else {
        s_result =
            build_result_locked();

        s_state =
            ActionState::Done;
    }

    unlock_action();

    finish_preview_ui();

    if (abandoned) gesture_set_motion_hold(false);

    Serial.println(
        "[camera-action] preview discarded"
    );

#endif
}


void camera_action_process_pending() {

#if defined(ENABLE_CAMERA)

    lock_action();

    if (
        s_state !=
        ActionState::Countdown
    ) {
        unlock_action();
        return;
    }

    uint32_t now =
        millis();

    if (
        !time_reached(
            now,
            s_countdownNextMs
        )
    ) {
        unlock_action();
        return;
    }


    // ========================================================
    // Countdown
    // 3 → 2 → 1
    // ========================================================

    if (s_countdownDigit > 1) {
        s_countdownDigit--;

        int digit =
            s_countdownDigit;

        s_countdownNextMs =
            now + 1000;

        unlock_action();

        show_countdown_digit(
            digit
        );

        return;
    }


    // ========================================================
    // Capture 시작
    // ========================================================

    s_state =
        ActionState::Running;

    unlock_action();


    /*
     * "1" 표시 제거
     */
    avatar.setSpeechText("");

    /*
     * Avatar renderer가 speech text 제거를
     * 한 프레임 반영할 수 있도록 짧게 대기.
     *
     * 카메라 안정화 목적이 아니라 UI 목적.
     */
    delay(40);


    /*
     * 셔터음은 카운트다운마다가 아니라
     * 실제 촬영 직전에 딱 한 번만 실행.
     */
    wav_clip_play_shutter(
        false
    );

    delay(80);

    Serial.println(
        "[camera-action] capture started on loop task"
    );

    String capturedPath;

    bool ok =
        capture_preview(
            capturedPath
        );


    // ========================================================
    // 촬영 도중 abandon 확인
    // ========================================================

    lock_action();

    bool abandoned =
        s_abandonWhenDone;

    if (abandoned) {
        s_result = "";

        s_state =
            ActionState::Idle;

        s_abandonWhenDone = false;

        s_tempPath = "";
        s_savedPath = "";
        s_saved = false;

        unlock_action();

        if (capturedPath.length()) {
            remove_file_if_exists(
                capturedPath
            );
        }

        finish_preview_ui();

        gesture_set_motion_hold(false);

        Serial.println(
            "[camera-action] capture abandoned"
        );

        return;
    }


    // ========================================================
    // Capture 실패
    // ========================================================

    if (!ok) {
        s_result =
            "{\"error\":\"사진 촬영에 실패했어요.\"}";

        s_state =
            ActionState::Done;

        s_tempPath = "";
        s_savedPath = "";
        s_saved = false;

        unlock_action();

        finish_preview_ui();

        gesture_set_motion_hold(false);

        Serial.println(
            "[camera-action] capture failed"
        );

        return;
    }


    /*
     * 아직 ReviewSave 상태로 만들지 않는다.
     *
     * 먼저 화면을 그린 다음 ReviewSave로
     * 바꿔야 preview를 그리는 도중 들어오는
     * 잘못된 touch를 막을 수 있다.
     */
    s_tempPath =
        capturedPath;

    unlock_action();


    // ========================================================
    // Preview 화면 표시
    // ========================================================

    show_save_ui(
        capturedPath
    );


    // ========================================================
    // Preview 그리는 동안 abandon이 들어왔는지 확인
    // ========================================================

    lock_action();

    abandoned =
        s_abandonWhenDone;

    if (abandoned) {
        s_result = "";

        s_state =
            ActionState::Idle;

        s_abandonWhenDone = false;

        s_tempPath = "";
        s_savedPath = "";
        s_saved = false;

        unlock_action();

        remove_file_if_exists(
            capturedPath
        );

        finish_preview_ui();

        gesture_set_motion_hold(false);

        Serial.println(
            "[camera-action] preview abandoned"
        );

        return;
    }


    /*
     * 화면이 완전히 표시된 후에야
     * 버튼 touch 허용
     */
    s_state =
        ActionState::ReviewSave;

    unlock_action();

    Serial.println(
        "[camera-action] review ready"
    );

#endif
}


bool camera_action_take_result(
    String& result
) {

#if !defined(ENABLE_CAMERA)

    (void)result;
    return false;

#else

    lock_action();

    if (
        s_state !=
        ActionState::Done
    ) {
        unlock_action();
        return false;
    }

    result =
        s_result;

    s_result = "";

    s_state =
        ActionState::Idle;

    s_tempPath = "";
    s_savedPath = "";

    s_saved = false;

    s_abandonWhenDone = false;

    s_countdownDigit = 0;
    s_countdownNextMs = 0;

    unlock_action();

    gesture_set_motion_hold(false);

    Serial.println(
        "[camera-action] result consumed"
    );

    return true;

#endif
}


void camera_action_abandon() {

#if defined(ENABLE_CAMERA)

    String cleanupPath;

    bool restorePreviewUi = false;
    bool clearCountdownText = false;


    lock_action();


    // ========================================================
    // 실제 Camera / SD I/O 도중에는
    // 즉시 state를 날리지 않는다.
    //
    // 현재 작업 완료 후 해당 함수가
    // s_abandonWhenDone을 보고 정리한다.
    // ========================================================

    if (
        s_state == ActionState::Running ||
        s_state == ActionState::Saving
    ) {
        s_abandonWhenDone = true;

        unlock_action();

        Serial.println(
            "[camera-action] abandon requested during I/O"
        );

        return;
    }


    // ========================================================
    // ReviewSave
    // ========================================================

    if (
        s_state ==
        ActionState::ReviewSave
    ) {
        if (
            !s_saved &&
            s_tempPath.length()
        ) {
            cleanupPath =
                s_tempPath;
        }

        restorePreviewUi =
            true;
    }


    // ========================================================
    // Countdown
    // ========================================================

    if (
        s_state == ActionState::Centering ||
        s_state == ActionState::Countdown
    ) {
        clearCountdownText =
            true;
    }


    // ========================================================
    // Reset state
    // ========================================================

    s_result = "";

    s_state =
        ActionState::Idle;

    s_abandonWhenDone = false;

    s_countdownDigit = 0;
    s_countdownNextMs = 0;

    s_tempPath = "";
    s_savedPath = "";

    s_saved = false;

    unlock_action();

    gesture_set_motion_hold(false);


    // ========================================================
    // Cleanup outside action mutex
    // ========================================================

    if (cleanupPath.length()) {
        remove_file_if_exists(
            cleanupPath
        );
    }

    if (restorePreviewUi) {
        finish_preview_ui();
    } else if (clearCountdownText) {
        avatar.setSpeechText("");
    }

    Serial.println(
        "[camera-action] abandoned"
    );

#endif
}
