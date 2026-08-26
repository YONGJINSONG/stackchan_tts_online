#pragma once
#include <Arduino.h>

// ---------------- Storage ----------------
#ifndef TUTOR_SD_CS
#define TUTOR_SD_CS 4
#endif

#define ENGLISH_DB_PATH "/kids_tutor/db/english.ndjson"
#define ENGLISH_IDX_PATH "/kids_tutor/db/english.idx"
#define MATH_DB_PATH    "/kids_tutor/db/math.ndjson"
#define MATH_IDX_PATH   "/kids_tutor/db/math.idx"
#define MATH6_DB_PATH   "/kids_tutor/db/math_6yo.ndjson"
#define MATH6_IDX_PATH  "/kids_tutor/db/math_6yo.idx"
#define CURRICULUM_DB_PATH "/kids_tutor/db/curriculum.ndjson"
#define CURRICULUM_IDX_PATH "/kids_tutor/db/curriculum.idx"
#define STUDENT_CONFIG_PATH "/kids_tutor/config/student.json"
#define CURRICULUM_CONFIG_PATH "/kids_tutor/config/curriculum.json"
#define REVIEW_QUEUE_PATH "/kids_tutor/progress/review_queue.json"
#define ADAPTIVE_STATE_PATH "/kids_tutor/progress/adaptive_state.json"
#define HISTORY_CSV_PATH "/kids_tutor/reports/history.csv"
#define REPORTS_DIR "/kids_tutor/reports"
#define VOICE_LITE_CONFIG_PATH "/kids_tutor/config/voice_lite.json"
#define VOICE_RECORD_PATH "/kids_tutor/tmp/answer.wav"

// Servo is owned by the host StackChan firmware (ServoCustom). Do not init again.
#ifndef TUTOR_ENABLE_SERVO
#define TUTOR_ENABLE_SERVO 0
#endif

// ---------------- Voice ----------------
// Local SD WAV output only. Answers are selected with A/B/C buttons (no Whisper).
#ifndef TUTOR_ENABLE_VOICE
#define TUTOR_ENABLE_VOICE 1
#endif

#ifndef TUTOR_ENABLE_ESPSR
#define TUTOR_ENABLE_ESPSR 0
#endif

#ifndef TUTOR_VOICE_AUTOLISTEN
#define TUTOR_VOICE_AUTOLISTEN 0
#endif

constexpr uint32_t VOICE_SAMPLE_RATE = 16000;
constexpr uint16_t VOICE_RECORD_CHUNK = 256;
constexpr uint32_t VOICE_DEFAULT_MAX_RECORD_MS = 5000;
constexpr uint32_t VOICE_DEFAULT_SILENCE_MS = 850;
constexpr uint16_t VOICE_DEFAULT_VAD_THRESHOLD = 550;
constexpr uint32_t VOICE_HTTP_TIMEOUT_MS = 45000;

// ---------------- Learning ----------------
constexpr uint8_t START_LEVEL = 1;
constexpr uint8_t MAX_LEVEL = 5;
constexpr uint8_t LEVEL_UP_STREAK = 5;
constexpr uint8_t WRONG_BEFORE_EXPLAIN = 3;
constexpr float VOICE_FUZZY_THRESHOLD = 0.78f;
