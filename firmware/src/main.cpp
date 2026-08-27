#include <Arduino.h>
//#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>
#include "share/Version.h"
#include "share/Mutex.h"
#include "share/SDUtil.h"
#include "share/DefaultParams.h"
#include <M5Unified.h>
#include <nvs.h>
#include <Avatar.h>
#include <faces/CatFace.h>
#include "face/LayeredFace.h"
#include "Volume.h"
#include "Gesture.h"
#include "IdleMotion.h"
#include "IdleTalk.h"
#include "PetReaction.h"
#include "TouchReaction.h"
#include "BatteryReaction.h"
#include "NightMode.h"
#include "CameraVision.h"
#include "CameraAction.h"
#include "Proximity.h"
#include "WifiConfig.h"
#include "WifiSetupPortal.h"
#include "face/RoboEyesView.h"   // 감정별 눈 색 설정 로드
#include "Persona.h"
#include "DiagLog.h"
#include "Sfx.h"
#include "ServoTrim.h"   // 서보 홈 보정 오프셋
#include "usage_timer.h"
#include "StackchanExConfig.h"
#include "Robot.h"
#include "mod/ModManager.h"
#include "mod/ModBase.h"
#include "mod/AiStackChan/AiStackChanMod.h"
#include "mod/AiStackChan/RealtimeAiMod.h"
#include "mod/Pomodoro/PomodoroMod.h"
#include "mod/PhotoFrame/PhotoFrameMod.h"
#include "mod/KidsTutor/KidsTutorMod.h"
#include "mod/RoboEyes/RoboEyesMod.h"
#include "mod/StatusMonitor/StatusMonitorMod.h"
#include "mod/VolumeSetting/VolumeSettingMod.h"
#include "mod/QRdisplay/QRdisplayMod.h"
#include "mod/EspNowRemote/EspNowRemoteMod.h"

#include "driver/PlayMP3.h"   //lipSync
#include "driver/TapDetect.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiMulti.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include <ArduinoYaml.h>   // YAMLDuino — re-read SC_SecConfig.yaml for wifi.networks[]
#include "SpiRamJsonDocument.h"
#include <ESP8266FtpServer.h>

#include "llm/ChatGPT/ChatGPT.h"
#include "llm/ChatGPT/FunctionCall.h"
#include "llm/ChatHistory.h"
#include "llm/Gemini/GeminiLive.h"

#include "WebAPI.h"

#if defined( ENABLE_CAMERA )
#include "driver/Camera.h"
#endif    //ENABLE_CAMERA

#include "driver/WatchDog.h"
#include "SDUpdater.h"
#include "DebugTools.h"

#if defined(USE_AUDIO_MODULE)
#include "driver/M5AudioModule.h"
#endif

StackchanExConfig system_config;
Robot* robot;
bool isOffline = false;


// NTP接続情報　NTP connection information.
const char* NTPSRV      = "ntp.jst.mfeed.ad.jp";    // NTPサーバーアドレス NTP server address.
const long  GMT_OFFSET  = 9 * 3600;                 // GMT-TOKYO(時差９時間）9 hours time difference.
const int   DAYLIGHT_OFFSET = 0;                    // サマータイム設定なし No daylight saving time setting

//bool servo_home = false;
bool servo_home = true;
volatile bool espnow_remote_servo_override = false;

// 웹 조이스틱 수동 머리 제어 (WebAPI /servo_move 가 설정). 활성 동안 idle/제스처/시선/홈
// 등 다른 서보 소스를 모두 무시하고 즉시 따라감. 일정 시간 갱신 없으면 자동 해제.
volatile uint32_t g_servoManualUntil = 0;   // 만료 시각(millis), 0=비활성
volatile int      g_servoManualX = 0;        // 목표 팬 각도(deg)
volatile int      g_servoManualY = 0;        // 목표 틸트 각도(deg)

using namespace m5avatar;
Avatar avatar;
Face* customFace;
const Expression expressions_table[] = {
  Expression::Neutral,
  Expression::Happy,
  Expression::Sleepy,
  Expression::Doubt,
  Expression::Sad,
  Expression::Angry
};

FtpServer ftpSrv;   //set #define FTP_DEBUG in ESP8266FtpServer.h to see ftp verbose on serial


void lipSync(void *args)
{
  float gazeX, gazeY;
  int level = 0;
  DriveContext *ctx = (DriveContext *)args;
  Avatar *avatar = ctx->getAvatar();
  for (;;)
  {
#ifdef REALTIME_API
#ifdef REALTIME_API_WITH_TTS
    level = robot->tts->getLevel();
#else
    level = ((RealtimeLLMBase*)(robot->llm))->getAudioLevel();
#endif
#else
    level = robot->tts->getLevel();
#endif
    if(level<100) level = 0;
    if(level > 15000)
    {
      level = 15000;
    }
    float open = (float)level/15000.0;
    avatar->setMouthOpenRatio(open);
    avatar->getGaze(&gazeY, &gazeX);
    // LayeredFace: skip head tilt — rotation forces slow pushRotateZoom every frame.
    if (!layered_face_active()) {
      avatar->setRotation(gazeX * 5);
    } else {
      avatar->setRotation(0);
    }
    delay(100);
  }
}


void servo(void *args)
{
  float gazeX, gazeY;
  DriveContext *ctx = (DriveContext *)args;
  Avatar *avatar = ctx->getAvatar();
  // 시선 추종을 200ms 주기로 자주 확인해 눈 이동과 거의 동시에 머리가 따라가게 한다.
  // 단 목표 각도가 바뀌었을 때만 moveTo 를 호출해 매 틱 미세 진동/소음을 막는다.
  int prevTgtX = 999, prevTgtY = 999;   // 마지막으로 명령한 목표 각도(초기값=강제 첫 이동)
  for (;;)
  {
#ifdef USE_SERVO
    // 웹 조이스틱 수동 제어 — 최우선. 다른 소스(idle/제스처/시선/홈/espnow) 무시하고 즉시 추종.
    if(millis() < g_servoManualUntil)
    {
      robot->servo->moveTo(g_servoManualX, g_servoManualY, 60);   // 짧은 이징=빠른 추종
      prevTgtX = prevTgtY = 999;   // 수동 종료 후 시선 추종이 다시 명령하도록 리셋
      delay(25);
      continue;
    }

    if(espnow_remote_servo_override)
    {
      delay(100);
      continue;
    }

    if(millis() < gesture_suppress_until){
      prevTgtX = prevTgtY = 999;   // 제스처 종료 후 시선 위치로 복귀 명령 보장
      delay(100);
      continue;
    }

    // 목표 각도 계산: 발화 중/idle 모두 시선을 따라가되, idle 중 수면이거나 시선이
    // 거의 중앙이면 정면(0,0). (발화 중엔 항상 시선 추종.)
    int tgtX, tgtY;
    if(!servo_home)
    {
      avatar->getGaze(&gazeY, &gazeX);
      tgtX = (int)(15.0 * gazeX);
      tgtY = (int)(10.0 * gazeY);
    }
    else if(night_mode_is_sleeping())
    {
      tgtX = 0; tgtY = 0;
    }
    else
    {
      avatar->getGaze(&gazeY, &gazeX);
      if(fabs(gazeX) < 0.15 && fabs(gazeY) < 0.15) { tgtX = 0; tgtY = 0; }   // 시선 중앙 → 정면
      else { tgtX = (int)(15.0 * gazeX); tgtY = (int)(10.0 * gazeY); }
    }

    // 목표가 바뀐 경우에만 이동(떨림/소음 방지). 이징 200ms 로 부드럽게.
    if(tgtX != prevTgtX || tgtY != prevTgtY)
    {
      if(tgtX == 0 && tgtY == 0) robot->servo->moveToOrigin();
      else                       robot->servo->moveTo(tgtX, tgtY, 200);
      prevTgtX = tgtX; prevTgtY = tgtY;
    }
#endif
    delay(200);
  }
}

void battery_check(void *args) {
  DriveContext *ctx = (DriveContext *)args;
  Avatar *avatar = ctx->getAvatar();
  for (;;)
  {
    int32_t batteryLevel = M5.Power.getBatteryLevel();
    if((batteryLevel < 95) && (batteryLevel != 0)){
      avatar->setBatteryIcon(true);
      avatar->setBatteryStatus(M5.Power.isCharging(), M5.Power.getBatteryLevel());
    }
    else{
      avatar->setBatteryIcon(false);    
    }
    delay(60000);
  }
}

bool Wifi_connection_check() {
  unsigned long start_millis = millis();

  // 前回接続時情報で接続する
  while (WiFi.status() != WL_CONNECTED) {
    M5.Display.print(".");
    Serial.print(".");
    delay(1000);
    // 5秒以上接続できなかったら抜ける
    if ( 5000 < (millis() - start_millis) ) {
      //break;
      return false;
    }
  }
  return true;
}

void time_sync(const char* ntpsrv, long gmt_offset, int daylight_offset) {
  struct tm timeInfo; 
  char buf[60];

  configTime(gmt_offset, daylight_offset, ntpsrv);          // NTPサーバと同期

  if (getLocalTime(&timeInfo)) {                            // timeinfoに現在時刻を格納
    Serial.print("NTP : ");                                 // シリアルモニターに表示
    Serial.println(ntpsrv);                                 // シリアルモニターに表示

    sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d\n",     // 表示内容の編集
    timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday,
    timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);

    Serial.println(buf);                                    // シリアルモニターに表示
  }
  else {
    Serial.print("NTP Sync Error ");                        // シリアルモニターに表示
  }
}



ModBase* init_mod(void)
{
  ModBase* mod;
  if(!isOffline || robot->isAllOfflineService()){
#if defined(REALTIME_API)
    add_mod(new RealtimeAiMod(isOffline));
#else
    add_mod(new AiStackChanMod(isOffline));
#endif
  }
  add_mod(new StatusMonitorMod());
  // VolumeSettingMod 제거: 볼륨은 설정 페이지(/settings.html)에서 조정 — 모드 사이클에서 뺌.
  //add_mod(new VolumeSettingMod());
  //add_mod(new EspNowRemoteMod());
  //add_mod(new PomodoroMod(isOffline));
  add_mod(new PhotoFrameMod(isOffline));
  add_mod(new KidsTutorMod());
  add_mod(new RoboEyesMod(isOffline));   // RoboEyes 표정 모드 (1단계 검증)
  //add_mod(new QRdisplayMod());
  mod = get_current_mod();
  mod->init();
  return mod;
}


void sw_tone()
{
  enterMutexAudio();
  M5.Mic.end();
  M5.Speaker.begin();
  delay(300);     // AtomS3Rはこのdelayがないと鳴らないときがある
  M5.Speaker.tone(1000, 100);
  delay(500);

  M5.Speaker.end();
  M5.Mic.begin();
  exitMutexAudio();
}
  
void alarm_tone()
{
  enterMutexAudio();
  M5.Mic.end();
  M5.Speaker.begin();

  for(int i=0; i<5; i++){
    M5.Speaker.tone(1200, 50);
    delay(100);
    M5.Speaker.tone(1200, 50);
    delay(100);
    M5.Speaker.tone(1200, 50);
    delay(1000);  
  }

  M5.Speaker.end();
  M5.Mic.begin();
  exitMutexAudio();
}

void init_mic_spk()
{
#if defined(USE_AUDIO_MODULE)
  initAudioModule();
#endif

  {
    auto micConfig = M5.Mic.config();
    //micConfig.stereo = false;
    micConfig.sample_rate = 16000;
#if defined(USE_AUDIO_MODULE)
    micConfig.pin_data_in = SYS_I2S_DIN_PIN;
    micConfig.pin_bck = SYS_I2S_SCLK_PIN;
    micConfig.pin_mck = SYS_I2S_MCLK_PIN;
    micConfig.pin_ws = SYS_I2S_LRCK_PIN;
#endif
    M5.Mic.config(micConfig);
  }
  M5.Mic.begin();

  { /// custom setting
    auto spk_cfg = M5.Speaker.config();
    /// Increasing the sample_rate will improve the sound quality instead of increasing the CPU load.
    spk_cfg.sample_rate = 96000; // 24kHz Realtime audio × integer 4 — avoids non-integer resample noise (was 128000 = 5.33×)
    spk_cfg.task_pinned_core = APP_CPU_NUM;
    // Noise tuning: halve digital gain to soften residual "프프프" soft-clipping;
    // enlarge DMA buffers to reduce underrun risk when WS task delays audio delivery.
    spk_cfg.magnification = 8;     // default 16
    spk_cfg.dma_buf_len = 512;     // default 256
    spk_cfg.dma_buf_count = 12;    // default 8

#if defined(USE_AUDIO_MODULE)
    spk_cfg.pin_data_out = SYS_I2S_DOUT_PIN;
    spk_cfg.pin_bck = SYS_I2S_SCLK_PIN;
    spk_cfg.pin_mck = SYS_I2S_MCLK_PIN;
    spk_cfg.pin_ws = SYS_I2S_LRCK_PIN;
#endif
    M5.Speaker.config(spk_cfg);
  }
  //M5.Speaker.begin();
}

// === Multi-network Wi-Fi (custom extension) ===========================================
static void configureWifiCountryKR()
{
  // Keep the station scan range explicit for Korean 2.4 GHz networks
  // (channels 1–13), including routers configured on channel 12 or 13.
  esp_err_t result = esp_wifi_set_country_code("KR", false);
  Serial.printf("[wifi] country=KR (2.4 GHz channels 1-13), result=%d\n", (int)result);
}

static void printSsidBytes(const char* label, const String& ssid)
{
  Serial.printf("[wifi] %s ssid='%s' length=%u bytes=", label, ssid.c_str(), (unsigned)ssid.length());
  for (unsigned i = 0; i < ssid.length(); i++) {
    Serial.printf("%02X", static_cast<unsigned char>(ssid[i]));
    if (i + 1 < ssid.length()) Serial.print(':');
  }
  Serial.println();
}

// Reads `wifi.networks: [{ssid, password}, ...]` from /yaml/SC_SecConfig.yaml on the SD
// card and lets WiFiMulti pick whichever AP is in range with the strongest signal.
// This is how home/office auto-switching works without re-flashing. The stackchan-arduino
// library only consumes single ssid/password; the `networks` key is our addition.
static bool tryMultiNetworkWifi()
{
  // (1) Web-editable networks from SPIFFS /wifi.json take priority. These are
  //     set via the settings page and applied on reboot.
  {
    static WiFiMulti spiffsMulti;
    String ssids[WIFI_CFG_MAX_NETWORKS], pwds[WIFI_CFG_MAX_NETWORKS];
    int n = wifi_config_get_networks(ssids, pwds, WIFI_CFG_MAX_NETWORKS);
    if (n > 0) {
      // Scan exactly as the setup portal does.  WiFiMulti occasionally misses
      // a just-booted AP, while a connection pinned to the scan result's BSSID
      // and channel is reliable (and also works for a hidden SSID).
      int visible = WiFi.scanNetworks(false, true);
      int matchedIndex = -1;
      int matchedChannel = 0;
      int matchedRssi = -127;
      uint8_t matchedBssid[6] = {0};
      for (int ap = 0; ap < visible; ap++) {
        String scannedSsid = WiFi.SSID(ap);
        for (int saved = 0; saved < n; saved++) {
          if (scannedSsid == ssids[saved] && WiFi.RSSI(ap) > matchedRssi) {
            matchedIndex = saved;
            matchedChannel = WiFi.channel(ap);
            matchedRssi = WiFi.RSSI(ap);
            memcpy(matchedBssid, WiFi.BSSID(ap), sizeof(matchedBssid));
          }
        }
      }
      if (matchedIndex >= 0) {
        Serial.printf("[multi-wifi] saved AP visible: %s channel=%d RSSI=%d; connecting directly\n",
                      ssids[matchedIndex].c_str(), matchedChannel, matchedRssi);
      } else {
        Serial.printf("[multi-wifi] boot scan found %d AP(s), but none matched /wifi.json\n", visible);
        for (int saved = 0; saved < n; saved++) {
          printSsidBytes("saved", ssids[saved]);
        }
        for (int ap = 0; ap < visible; ap++) {
          Serial.printf("[wifi] scan[%d] ssid='%s' channel=%d RSSI=%d\n",
                        ap, WiFi.SSID(ap).c_str(), WiFi.channel(ap), WiFi.RSSI(ap));
        }
      }
      WiFi.scanDelete();

      if (matchedIndex >= 0) {
        WiFi.disconnect(false, false);
        delay(150);
        WiFi.begin(ssids[matchedIndex].c_str(), pwds[matchedIndex].c_str(),
                   matchedChannel, matchedBssid);
        uint32_t bssidConnectStarted = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - bssidConnectStarted < 15000) {
          delay(250);
        }
        if (WiFi.status() == WL_CONNECTED) {
          Serial.printf("[multi-wifi] connected to scanned AP (SPIFFS): %s  IP=%s  RSSI=%d\n",
                        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
          return true;
        }
        Serial.printf("[multi-wifi] scanned AP connection failed (wl_status=%d)\n", WiFi.status());
        WiFi.disconnect(false, false);
        delay(150);
      }

      for (int i = 0; i < n; i++) spiffsMulti.addAP(ssids[i].c_str(), pwds[i].c_str());
      Serial.printf("[multi-wifi] %d network(s) from /wifi.json — connecting (up to 15s)\n", n);
      if (spiffsMulti.run(15000) == WL_CONNECTED) {
        Serial.printf("[multi-wifi] connected (SPIFFS): %s  IP=%s  RSSI=%d\n",
                      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
      }
      Serial.println("[multi-wifi] WiFiMulti did not select a saved AP; trying each saved AP directly");

      // WiFiMulti can report "no matching wifi found" during the first scan
      // after boot even though the AP becomes visible a moment later. Retry
      // every persisted network with the normal STA connection path before
      // falling back to the older SD configuration or reopening the portal.
      for (int i = 0; i < n; i++) {
        Serial.printf("[multi-wifi] direct connect: %s\n", ssids[i].c_str());
        WiFi.disconnect(false, false);
        uint32_t disconnectStarted = millis();
        while (WiFi.status() == WL_IDLE_STATUS && millis() - disconnectStarted < 1000) {
          delay(20);
        }
        WiFi.begin(ssids[i].c_str(), pwds[i].c_str());
        uint32_t connectStarted = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - connectStarted < 15000) {
          delay(250);
        }
        if (WiFi.status() == WL_CONNECTED) {
          Serial.printf("[multi-wifi] connected directly (SPIFFS): %s  IP=%s  RSSI=%d\n",
                        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
          return true;
        }
        Serial.printf("[multi-wifi] direct connect failed (wl_status=%d)\n", WiFi.status());
        WiFi.disconnect(false, false);  // finish this attempt before the next WiFi.begin()
        delay(100);
      }
      Serial.println("[multi-wifi] /wifi.json networks not in range — falling back to SD/YAML");
    }
  }

  // (2) Fall back to SD-card YAML wifi.networks[] (home/office defaults).
  const char* PATH = "/yaml/SC_SecConfig.yaml";
  if (!SD.exists(PATH)) {
    Serial.println("[multi-wifi] /yaml/SC_SecConfig.yaml not found, skip");
    return false;
  }
  File f = SD.open(PATH, "r");
  if (!f) {
    Serial.println("[multi-wifi] open failed, skip");
    return false;
  }
  String body = f.readString();
  f.close();
  if (body.length() == 0) {
    Serial.println("[multi-wifi] empty file, skip");
    return false;
  }

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeYml(doc, body.c_str());
  if (err) {
    Serial.printf("[multi-wifi] yaml parse error: %s\n", err.c_str());
    return false;
  }

  JsonArrayConst nets = doc["wifi"]["networks"].as<JsonArrayConst>();
  if (nets.isNull() || nets.size() == 0) {
    Serial.println("[multi-wifi] no wifi.networks[] in YAML — falling back to single-ssid flow");
    return false;
  }

  static WiFiMulti wifiMulti;
  int added = 0;
  for (JsonObjectConst net : nets) {
    const char* ssid = net["ssid"] | (const char*)nullptr;
    const char* pwd = net["password"] | (const char*)nullptr;
    if (ssid && ssid[0] != '\0') {
      wifiMulti.addAP(ssid, pwd ? pwd : "");
      Serial.printf("[multi-wifi] AP: %s\n", ssid);
      added++;
    }
  }
  if (added == 0) {
    Serial.println("[multi-wifi] networks[] had no usable entries");
    return false;
  }

  Serial.printf("[multi-wifi] scanning + connecting to strongest of %d APs (up to 15s)\n", added);
  uint8_t res = wifiMulti.run(15000);
  if (res == WL_CONNECTED) {
    Serial.printf("[multi-wifi] connected: %s  IP=%s  RSSI=%d\n",
                  WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
    return true;
  }
  Serial.printf("[multi-wifi] no in-range AP connected (wl_status=%d), falling back\n", res);
  return false;
}

#if !defined(ARDUINO_M5STACK_ATOMS3R)
static bool mountSettingsSdCard()
{
  // Some CoreS3 cards are not ready immediately after an ESP.restart(),
  // especially after upload/reset. Retry with progressively safer SPI clocks
  // before treating the SD card as unavailable.
  static const uint32_t frequencies[] = {25000000, 10000000, 4000000};
  for (uint32_t frequency : frequencies) {
    for (int attempt = 1; attempt <= 2; attempt++) {
      Serial.printf("[sd] mount attempt %d at %u Hz\n", attempt, (unsigned)frequency);
      if (SD.begin(GPIO_NUM_4, SPI, frequency, "/sd", 12)) {
        Serial.printf("[sd] mounted at %u Hz\n", (unsigned)frequency);
        return true;
      }
      delay(300);
    }
  }
  Serial.println("[sd] mount failed after retries");
  return false;
}
#endif

void setup()
{
  auto cfg = M5.config();

#if defined(ARDUINO_M5STACK_ATOMS3R)
  cfg.internal_spk = false;
  cfg.internal_mic = false;
  cfg.external_speaker.atomic_echo = true;
#endif
  cfg.serial_baudrate = 115200;   //M5Unified 0.1.17からデフォルトが0になったため設定
  M5.begin(cfg);

  /// シリアル出力のログレベルを VERBOSEに設定
  //M5.Log.setLogLevel(m5::log_target_serial, ESP_LOG_VERBOSE);


#if defined(ARDUINO_M5STACK_ATOMS3R)
  M5.Lcd.setTextSize(2);
  M5.Lcd.printf("Ver.%s\n", FW_VERSION);
#else
  M5.Lcd.setFont(&fonts::efontKR_16);
  M5.Lcd.setTextSize(1);
  M5.Lcd.println("AI Stack-chan Ex [^_^]");
  M5.Lcd.printf("Firmware Version: %s\n", FW_VERSION);
  M5.Lcd.println("한국어 폰트 활성화");  // KR font verification
#endif

  initMutex();
  camera_vision_init();

#if defined(ENABLE_SD_UPDATER)
  // ***** for SD-Updater *********************
  SDU_lobby("AiStackChanEx");
  // ******************************************
#endif

  //auto brightness = M5.Display.getBrightness();
  //Serial.printf("Brightness: %d\n", brightness);

  init_mic_spk();

  /// settings
#if defined(ARDUINO_M5STACK_ATOMS3R)
  if (SPIFFS.begin()) {
    // この関数ですべてのYAMLファイル(Basic, Secret, Extend)を読み込む
    system_config.loadConfig(SPIFFS, "/SC_ExConfig.yaml", 2048,
                                     "/SC_SecConfig.yaml", 2048,
                                     "/SC_BasicConfig.yaml", 2048);
#else
  if (mountSettingsSdCard()) {
    // この関数ですべてのYAMLファイル(Basic, Secret, Extend)を読み込む
    system_config.loadConfig(SD, "/app/AiStackChanEx/SC_ExConfig.yaml");
#endif
    // Wifi設定読み込み
    wifi_s* wifi_info = system_config.getWiFiSetting();
    Serial.printf("\nSSID: %s\n",wifi_info->ssid.c_str());
    Serial.println("Key: [redacted]");

    // 前回設定で接続
    Serial.println("Connecting to WiFi");
    WiFi.disconnect();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    configureWifiCountryKR();

    // Multi-network first (home/office auto-switch). If that succeeds, we skip the
    // upstream NVS-then-single-ssid flow entirely.
    bool multi_wifi_connected = tryMultiNetworkWifi();
    if (!multi_wifi_connected) {
    WiFi.begin();
    if(Wifi_connection_check()){
      Serial.println("Successfully connected to Wi-Fi using the previous settings.");
    }else{
      // 前回設定での接続に失敗。SDカード設定による接続にトライ。
      Serial.println("The previous WiFi connection failed. Attempting to connect using the SD card settings.");
      if(wifi_info->ssid.length() == 0){
        // No SD SSID — SoftAP captive portal (blocks until save, then reboots).
        Serial.println("Can't get WiFi settings. Starting SoftAP portal.");
        wifi_setup_portal_run();
      }else{
        WiFi.begin(wifi_info->ssid.c_str(), wifi_info->password.c_str());
        if(Wifi_connection_check()){
          // SDカード設定による接続に成功。
          Serial.println("Successfully established a Wi-Fi connection via the SD card settings.");
        }else{
          // SD / NVS failed — SoftAP captive portal (blocks until save, then reboots).
          Serial.println("WiFi connection failed. Starting SoftAP portal.");
          wifi_setup_portal_run();
        }
      }
    }
    }  // end !multi_wifi_connected

    if(!isOffline){
      // Realtime API audio latency tuning: disable WiFi power-save to avoid
      // ~100ms periodic sleep hits that cause choppy streaming.
      // (Note: WiFi.setTxPower(WIFI_POWER_19_5dBm) was tried but appears to cause
      // USB-power brownouts on CoreS3 — mic stops streaming. Kept at default.)
      WiFi.setSleep(false);
      Serial.println(WiFi.localIP());
      M5.Lcd.println(WiFi.localIP());
      delay(1000);

      //Webサーバ設定
      init_web_server();
      //FTPサーバ設定（SPIFFS用）
      ftpSrv.begin("stackchan","stackchan");    //username, password for ftp.  set ports in ESP8266FtpServer.h  (default 21, 50009 for PASV)
      Serial.println("FTP server started");
      M5.Lcd.println("FTP server started");

      //時刻同期
      time_sync(NTPSRV, GMT_OFFSET, DAYLIGHT_OFFSET);

      // Start background data prefetcher (weather/air/meals/todos/schedules).
      // The 5-minute refresh task lets the realtime tool calls read cached data
      // instantly instead of blocking the WebSocket task with HTTPS fetches.
      start_external_data_prefetch();

      gesture_init();
    }else{
      M5.Lcd.print("Can't connect to WiFi. Start offline mode.\n");
    }

    robot = new Robot(system_config);

    //SD.end();
  } else {
    M5.Lcd.print("Failed to load SD card settings. System reset after 5 seconds.");
    delay(5000);
    ESP.restart();
    //WiFi.begin();
  }
  
  mp3_init();

  //mod設定

#if defined(ARDUINO_M5STACK_ATOMS3R)
#if defined(CAT_FACE)
  customFace = new CatFace();
  avatar.setFace(customFace);
#endif
  avatar.setScale(0.5);
  avatar.setPosition(-56, -96);
  avatar.init();
#else
  // Prefer SD layered PNG face (/face/base|eyes|mouth|...). Else vector Face.
  // Must run BEFORE avatar.init(16) — init() calls face->initSprites() on the
  // current face; swapping after would leave our new face uninitialized.
  if (LayeredFace::sdReady()) {
    customFace = new LayeredFace();
    avatar.setFace(customFace);
    layered_face_set_active(true);
    Serial.println("[face] LayeredFace (SD /face)");
  } else {
    customFace = new Face(
        new Mouth(50, 90, 4, 60),     new BoundingRect(148, 163),
        new Eye(18, false),           new BoundingRect(93,  90),     // R eye
        new Eye(18, true),            new BoundingRect(96,  230),    // L eye
        new Eyeblow(32, 0, false),    new BoundingRect(67,  96),
        new Eyeblow(32, 0, true),     new BoundingRect(72,  230)
    );
    avatar.setFace(customFace);
    layered_face_set_active(false);
    Serial.println("[face] vector avatar with bigger eyes (r=18)");
  }

  //avatar.init();
  avatar.init(16);
#endif

  // Active mods can take over the renderer, so start them only after the
  // selected face and Avatar sprites are initialized.
  init_mod();

  avatar.addTask(lipSync, "lipSync", 2048, 2);
  avatar.addTask(servo, "servo", 2048);
  avatar.addTask(battery_check, "battery_check", 2048);
  avatar.setSpeechFont(&fonts::efontKR_16);

  Serial.printf("Speaker volume (yaml): %d\n", system_config.getExConfig().audio.speaker_volume);
  if(0 != system_config.getExConfig().audio.speaker_volume){
    robot->spk_volume = system_config.getExConfig().audio.speaker_volume;
  }else{
    robot->spk_volume = DEFAULT_SPEAKER_VOLUME;
  }
  Serial.printf("Speaker volume (set): %d\n", robot->spk_volume);
  M5.Speaker.setVolume(robot->spk_volume);
  // SPIFFS-persisted override (set via web UI /volume_set). Falls through to the
  // setVolume above if no /volume.txt exists.
  volume_init();

  // Persistent diagnostic log on SPIFFS (/diag.log, downloadable via FTP). Logs the
  // boot reset reason + a 20s heartbeat so silent hangs / crashes can be diagnosed.
  diag_log_init();

  // Situational MP3 sound effects from SD (/app/AiStackChanEx/sfx/<name>.mp3).
  sfx_init();

  // 서보 홈(중앙) 보정 오프셋 로드 — 비뚤어진 홈 위치를 바로잡음.
  servo_trim_load();

  // Photo-frame settings (slide interval / album folder) so the web UI shows saved values.
  photoframe_config_load();

  // Persona presets (기본/여자친구/친구/비서). Seeds from the current /data.json role
  // on first boot so the existing family persona is preserved. Needs robot->llm ready.
  persona_init();

#if defined(ENABLE_TAP_DETECT)
  invokeDoubleTapDetectTask();
#endif

  //init_watchdog();

  // Idle "alive & cute" motion engine (random expression/gesture/gaze/blink).
  // Breaks the "last conversation expression stays frozen" behaviour. Tunable
  // from the settings web page. Needs avatar + robot ready (done above).
  idle_motion_init();

  // Proximity reaction (eyes grow as something approaches; surprise when close).
  // Uses the CoreS3 built-in LTR-553 sensor on the internal I2C bus. Disables
  // itself gracefully if the sensor is not detected.
  proximity_init();

  // Lifelike extras:
  //  - proactive talk when idle / jokes / greet-on-approach  (online realtime)
  //  - "pet me" reaction via the IMU (pick up / stroke)
  //  - night mode: dim screen, sleepy bias, bedtime greeting
  idle_talk_init();
  pet_reaction_init();
  touch_reaction_init();
  battery_reaction_init();
  night_mode_init();
  roboeyes_eyecolor_init();   // 감정별 눈 그라데이션 색(SPIFFS) 로드

  //ヒープメモリ残量確認(デバッグ用)
  check_heap_free_size();
  check_heap_largest_free_block();

  // Boot sound effect (SD), if the "boot" event has a sound bound. Last, so init
  // finishes fast before the (blocking) playback.
  sfx_play_event("boot");
}



void loop()
{
  //get_elapsed_time_micro("loop() start");
  bool cameraBusy = camera_is_busy();
  //get_elapsed_time_micro("M5.update time");

  // Sensor polling — kept on the main loop (right after M5.update) so all
  // internal-I2C access (proximity LTR-553, IMU, backlight) stays serialized
  // with the framework and never races on the shared bus. Each is self-throttled.
  bool touchConsumed = false;
  if (!cameraBusy) {
    camera_sensor_bus_lock();
    cameraBusy = camera_is_busy();
    if (!cameraBusy) {
      M5.update();
      proximity_tick();
      pet_reaction_tick();
      touchConsumed = touch_reaction_tick();
      battery_reaction_tick();
      night_mode_tick();
    }
    camera_sensor_bus_unlock();
  }
  idle_talk_tick();   // proactive speech (kept on main loop for WS-write safety)
  checkUsageTimer();
  kids_tutor_process_pending();  // defer Realtime function-call UI/SD work to this task
  camera_action_process_pending();

  ModBase* mod = get_current_mod();
  mod->idle();
  //get_elapsed_time_micro("Mod idle time");

  if (M5.BtnA.wasPressed())
  {
    mod->btnA_pressed();
  }

  if (M5.BtnA.pressedFor(2000))
  {
    mod->btnA_longPressed();
  }

  if (M5.BtnB.wasPressed())
  {
    mod->btnB_pressed();
  }

  if (M5.BtnB.pressedFor(2000))
  {
    mod->btnB_longPressed();
  }

  if (M5.BtnC.wasPressed())
  {
    mod->btnC_pressed();
  }

#if defined(ARDUINO_M5STACK_Core2) || defined( ARDUINO_M5STACK_CORES3 )
  auto count = (cameraBusy || camera_is_busy()) ? 0 : M5.Touch.getCount();
  if (count)
  {
    auto t = M5.Touch.getDetail();
    // A flick switches modes; a tap (release without flick) goes to the mod.
    // Splitting them this way means a swipe to leave the photo frame no longer
    // gets eaten as a "next photo" tap — flick = mode switch only, tap = mod action.
    if ((t.wasFlicked() || t.wasReleased()) && night_mode_wake_display())
    {
      // First touch after an explicit display-off request only wakes the LCD.
    }
    else if (touchConsumed)
    {
      // A pet stroke owns its release; do not toggle recording or change mode.
    }
    else if (t.wasFlicked())
    {
      int16_t dx = t.distanceX();
      int16_t dy = t.distanceY();

      if(abs(dx) >= abs(dy))
      {
        if(dx > 0){
          change_mod(true);
        }
        else{
          change_mod();
        }
      }
    }
    else if (t.wasReleased())
    {
      mod->display_touched(t.x, t.y);
    }
  }
#endif

#if defined(ENABLE_TAP_DETECT)
  if(doubleTapDetected){
    Serial.println("loop(): Double tap detected");
    mod->doubleTapped(detectedAcc[0], detectedAcc[1], detectedAcc[2]);
    doubleTapDetected = false;
  }

  // Modで重い処理をしている場合はダブルタップ検出を停止する
  if(mod->isBusy()){
    stopDoubleTapDetectTask();
  }else{
    resumeDoubleTapDetectTask();
  }
#endif
  //get_elapsed_time_micro("Callback process time");

  if(!isOffline){
    web_server_handle_client();
    ftpSrv.handleFTP();
  }

  //get_elapsed_time_micro("Web event process time");
  
  //reset_watchdog();
}
