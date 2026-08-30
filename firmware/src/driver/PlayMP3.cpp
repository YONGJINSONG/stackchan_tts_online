#include <Arduino.h>
#include <M5Unified.h>
#include <SD.h>
#include <SPIFFS.h>
#include <AudioOutput.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include "AudioFileSourceHTTPSStream.h"
#include "AudioFileSourceSD.h"
#include "AudioFileSourceSPIFFS.h"
#include "AudioOutputM5Speaker.h"
#include "PlayMP3.h"
#include "Avatar.h"
#include "share/SdBus.h"   // CoreS3: SDストリーミング中はアバター描画を止めてGPIO35をMISO入力に固定

using namespace m5avatar;

extern Avatar avatar;
extern bool servo_home;

/// set M5Speaker virtual channel (0-7)
//static constexpr uint8_t m5spk_virtual_channel = 0;
uint8_t m5spk_virtual_channel = 0;

AudioOutputM5Speaker out(&M5.Speaker, m5spk_virtual_channel);
AudioGeneratorMP3 *mp3;

int preallocateBufferSize = 30*1024;
uint8_t *preallocateBuffer;




void mp3_init(void)
{
    mp3 = new AudioGeneratorMP3();
    //out = new AudioOutputM5Speaker(&M5.Speaker, m5spk_virtual_channel);

    //TTS MP3用バッファ （PSRAMから確保される）
    preallocateBuffer = (uint8_t *)malloc(preallocateBufferSize);
    if (!preallocateBuffer) {
        M5.Display.printf("FATAL ERROR:  Unable to preallocate %d bytes for app\n", preallocateBufferSize);
        for (;;) { delay(1000); }
    }

    audioLogger = &Serial;
}

static volatile bool s_mp3Running = false;
static volatile bool s_mp3Stop = false;
static volatile bool s_mp3StoppedByUser = false;

void playMP3_request_stop() {
  s_mp3Stop = true;
}

bool playMP3_is_running() {
  return s_mp3Running;
}

bool playMP3_was_stopped() {
  return s_mp3StoppedByUser;
}

static bool claim_speaker_i2s() {
  if (M5.Mic.isEnabled()) M5.Mic.end();
  delay(40);
  if (M5.Speaker.isEnabled()) M5.Speaker.end();
  delay(40);
  if (M5.Speaker.begin()) return true;
  Serial.println("mp3 Speaker.begin retry");
  delay(80);
  M5.Speaker.end();
  delay(40);
  return M5.Speaker.begin();
}

bool playMP3(AudioFileSourceBuffer *buff, uint32_t maxPlayMs){
  s_mp3Stop = false;
  s_mp3StoppedByUser = false;
  s_mp3Running = true;

  if (!claim_speaker_i2s()) {
    Serial.println("mp3 Speaker.begin failed");
    s_mp3Running = false;
    delay(40);
    M5.Mic.begin();
    return false;
  }

  if (!mp3->begin(buff, &out)) {
    Serial.println("mp3 begin failed");
    M5.Speaker.end();
    delay(40);
    M5.Mic.begin();
    s_mp3Running = false;
    return false;
  }
  Serial.println("mp3 start");

  const bool touchStop = (maxPlayMs == 0);
  uint32_t startMs = millis();
  bool ok = true;
  while(mp3->isRunning()) {
    if (touchStop) {
      M5.update();
      if (M5.Touch.getCount()) {
        auto t = M5.Touch.getDetail();
        if (t.wasReleased()) s_mp3Stop = true;
      }
    }
    if (s_mp3Stop) {
      mp3->stop();
      s_mp3StoppedByUser = true;
      Serial.println("mp3 stop (touch/stop)");
      break;
    }
    if (!mp3->loop()) {
      mp3->stop();
      Serial.println("mp3 stop");
    }
    if (maxPlayMs > 0 && (millis() - startMs > maxPlayMs)) {
      mp3->stop();
      Serial.printf("mp3 stop (timeout %us)\n", (unsigned)(maxPlayMs / 1000));
      break;
    }
    delay(1);
  }

  uint32_t playedMs = millis() - startMs;
  if (!s_mp3StoppedByUser && playedMs < 250) {
    Serial.printf("mp3 too short (%ums) — treat as fail\n", (unsigned)playedMs);
    ok = false;
  }

  M5.Speaker.stop();
  M5.Speaker.end();
  delay(40);
  if (!M5.Mic.begin()) {
    Serial.println("mp3 Mic.begin failed");
  }
  s_mp3Running = false;
  return ok || s_mp3StoppedByUser;
}

bool playMP3SPIFFS(const char *filename)
{
  bool result;

  if (SPIFFS.exists(filename)) {
    AudioFileSourceSPIFFS *file_mp3 = new AudioFileSourceSPIFFS(filename);
    Serial.println("Open mp3");
    
    if( !file_mp3->isOpen() ){
      delete file_mp3;
      file_mp3 = nullptr;
      Serial.println("failed to open mp3 file");
      result = false;
    }
    else{
      AudioFileSourceBuffer *buff = new AudioFileSourceBuffer(file_mp3, preallocateBuffer, preallocateBufferSize);
      avatar.setExpression(Expression::Happy);
      servo_home = false;

      result = playMP3(buff);
      
      avatar.setExpression(Expression::Neutral);
      servo_home = true;

      delete file_mp3;
      delete buff;
    }
  }else{
    Serial.println("mp3 file is not exist");
    result = false;
  }
  return result;
}


bool playMP3SD(const char *filename)
{
  bool result = false;

  sd_bus_lock();   // CoreS3: ストリーミング中ずっとGPIO35をMISO入力に保つ(描画衝突→音切れ防止)

  if (SD.exists(filename)) {

    AudioFileSourceSD *file_mp3 = new AudioFileSourceSD(filename);
    Serial.println("Open mp3");
    
    if( !file_mp3->isOpen() ){
      delete file_mp3;
      //file_mp3 = nullptr;
      Serial.println("failed to open mp3 file");
      result = false;
    }
    else{
      AudioFileSourceBuffer *buff = new AudioFileSourceBuffer(file_mp3, preallocateBuffer, preallocateBufferSize);
      // 재생 전 표정을 저장했다가 끝난 뒤 복원한다. (감정 효과음 재생이 방금 설정된
      // 감정 표정 — 예: 슬픔 — 을 Happy/Neutral 로 덮어써서 "소리만 감정, 눈은 평범"
      // 해지던 문제 수정.) servo_home 도 동일하게 복원.
      Expression prevExpr = avatar.getExpression();
      bool prevServoHome = servo_home;
      servo_home = false;

      result = playMP3(buff, 0);

      avatar.setExpression(prevExpr);
      servo_home = prevServoHome;

      delete file_mp3;
      delete buff;
    }
  }else{
    Serial.println("mp3 file is not exist");
    result = false;
  }

  sd_bus_unlock();
  return result;
}
