#include "VoiceLite.h"
#include "TutorConfig.h"
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include "share/SdBus.h"

namespace {
void* audioAlloc(size_t bytes) {
#if CONFIG_SPIRAM
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p) return p;
#endif
  return heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
}
uint16_t readLE16(File& f) {
  uint8_t b[2]={0,0}; if (f.read(b,2)!=2) return 0;
  return (uint16_t)b[0] | ((uint16_t)b[1]<<8);
}
uint32_t readLE32(File& f) {
  uint8_t b[4]={0,0,0,0}; if (f.read(b,4)!=4) return 0;
  return (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
}
void writeLE16(File& f, uint16_t v) {
  uint8_t b[2]={(uint8_t)(v&255),(uint8_t)((v>>8)&255)}; f.write(b,2);
}
void writeLE32(File& f, uint32_t v) {
  uint8_t b[4]={(uint8_t)(v&255),(uint8_t)((v>>8)&255),(uint8_t)((v>>16)&255),(uint8_t)((v>>24)&255)}; f.write(b,4);
}
bool writeWavHeader(File& f, uint32_t rate, uint32_t dataBytes) {
  if (!f) return false;
  f.write((const uint8_t*)"RIFF",4); writeLE32(f,36+dataBytes); f.write((const uint8_t*)"WAVE",4);
  f.write((const uint8_t*)"fmt ",4); writeLE32(f,16); writeLE16(f,1); writeLE16(f,1);
  writeLE32(f,rate); writeLE32(f,rate*2); writeLE16(f,2); writeLE16(f,16);
  f.write((const uint8_t*)"data",4); writeLE32(f,dataBytes); return true;
}
}

void VoiceLite::setError(const String& message) {
  _lastError = message;
  Serial.println("[VOICE LITE] " + message);
}

uint32_t VoiceLite::textHash(const String& text) {
  uint32_t h = 2166136261UL;
  const uint8_t* p = (const uint8_t*)text.c_str();
  for (size_t i=0;i<text.length();++i) { h ^= p[i]; h *= 16777619UL; }
  return h;
}

String VoiceLite::audioPathForText(const String& text) const {
  char buf[16];
  snprintf(buf,sizeof(buf),"t_%08lx.wav",(unsigned long)textHash(text));
  String root=_settings.audioRoot;
  if (root.endsWith("/")) root.remove(root.length()-1);
  return root + "/" + String(buf);
}

bool VoiceLite::loadSettings(const char* path) {
  if (!_fs || !_fs->exists(path)) { setError(String("config missing: ")+path); return false; }
  File f=_fs->open(path,FILE_READ); if(!f){setError("cannot open voice_lite.json");return false;}
  JsonDocument d; auto err=deserializeJson(d,f); f.close();
  if(err){setError(String("voice_lite JSON: ")+err.c_str());return false;}
  _settings.enabled=d["enabled"]|true;
  _settings.localAudio=d["local_audio"]|true;
  _settings.missingAudioTone=d["missing_audio_tone"]|true;
  _settings.autoListen=d["auto_listen"]|false;
  _settings.inputMode=String((const char*)(d["input_mode"]|"button"));
  _settings.audioRoot=String((const char*)(d["audio_root"]|"/kids_tutor/audio/text"));
  JsonObject c=d["cloud_stt"].as<JsonObject>();
  if(!c.isNull()) {
    _settings.cloudEnabled=c["enabled"]|false;
    _settings.wifiSsid=String((const char*)(c["wifi_ssid"]|""));
    _settings.wifiPassword=String((const char*)(c["wifi_password"]|""));
    _settings.cloudUrl=String((const char*)(c["url"]|""));
    _settings.cloudApiKey=String((const char*)(c["api_key"]|""));
    _settings.cloudModel=String((const char*)(c["model"]|""));
    _settings.tlsInsecure=c["tls_insecure"]|false;
  }
  _settings.maxRecordMs=d["max_record_ms"]|VOICE_DEFAULT_MAX_RECORD_MS;
  _settings.silenceMs=d["silence_ms"]|VOICE_DEFAULT_SILENCE_MS;
  _settings.vadThreshold=d["vad_threshold"]|VOICE_DEFAULT_VAD_THRESHOLD;
  return _settings.enabled;
}

bool VoiceLite::begin(fs::FS& fs, const char* configPath) {
  _fs=&fs; _inputReady=false; _audioReady=false; _lastError="";
  // On CoreS3 the SD MISO/LCD control line is shared. Keep the display out
  // of the bus while reading the local voice configuration and file list.
  sd_bus_lock();
  bool settingsLoaded = loadSettings(configPath);
  if(!settingsLoaded) { sd_bus_unlock(); return false; }
  if(_settings.localAudio) {
    _fs->mkdir("/kids_tutor/audio"); _fs->mkdir(_settings.audioRoot);
    File dir=_fs->open(_settings.audioRoot);
    if(dir && dir.isDirectory()) {
      File e=dir.openNextFile();
      while(e) { String n=e.name(); n.toLowerCase(); if(!e.isDirectory() && n.endsWith(".wav")){_audioReady=true;e.close();break;} e.close(); e=dir.openNextFile(); }
      dir.close();
    }
  }
  sd_bus_unlock();
  String m=_settings.inputMode; m.toLowerCase();
  if(m=="espsr") {
    _inputReady=_offline.begin(fs);
    if(!_inputReady) setError("ESP-SR hooks/model not available; button fallback active");
  } else if(m=="cloud" && _settings.cloudEnabled && _settings.cloudUrl.length()) {
    _inputReady=connectWiFi();
  }
  Serial.println("[VOICE LITE] audio="+String(_audioReady?"local-wav":"tone-fallback")+
                 " input="+modeLabel());
  return true;
}

String VoiceLite::modeLabel() const {
  String m=_settings.inputMode; m.toLowerCase();
  if(m=="espsr" && _inputReady) return "ESP-SR";
  if(m=="cloud" && _inputReady) return "CLOUD STT";
  return "BUTTON";
}

void VoiceLite::missingTone() {
  if(!_settings.missingAudioTone) return;
  M5.Mic.end(); M5.Speaker.begin(); M5.Speaker.setVolume(TUTOR_TONE_VOLUME);
  M5.Speaker.tone(850,65); delay(90); M5.Speaker.end();
}

bool VoiceLite::parseWav(File& f, uint32_t& dataOffset, uint32_t& dataBytes,
                         uint32_t& sampleRate, uint16_t& channels, uint16_t& bitsPerSample) {
  char riff[4],wave[4];
  if(f.read((uint8_t*)riff,4)!=4)return false; readLE32(f);
  if(f.read((uint8_t*)wave,4)!=4)return false;
  if(memcmp(riff,"RIFF",4)||memcmp(wave,"WAVE",4))return false;
  bool fmt=false,data=false;
  while(f.available()) {
    char id[4]; if(f.read((uint8_t*)id,4)!=4)break;
    uint32_t sz=readLE32(f), pos=f.position();
    if(!memcmp(id,"fmt ",4)) {
      uint16_t format=readLE16(f); channels=readLE16(f); sampleRate=readLE32(f);
      readLE32(f); readLE16(f); bitsPerSample=readLE16(f);
      if(format!=1 && format!=0xFFFE)return false; fmt=true;
    } else if(!memcmp(id,"data",4)) { dataOffset=f.position(); dataBytes=sz; data=true; break; }
    f.seek(pos+sz+(sz&1));
  }
  return fmt&&data;
}

bool VoiceLite::playWav(const String& path) {
  // CoreS3 shares LCD D/C with SD MISO — pause avatar draw while reading WAV.
  sd_bus_lock();
  if(!_fs || !_fs->exists(path)) { sd_bus_unlock(); return false; }
  File f=_fs->open(path,FILE_READ);
  if(!f){ sd_bus_unlock(); return false; }
  uint32_t off=0,bytes=0,rate=0; uint16_t ch=0,bits=0;
  if(!parseWav(f,off,bytes,rate,ch,bits)||bits!=16||ch<1||ch>2){
    f.close(); sd_bus_unlock(); setError("WAV must be PCM16 mono/stereo"); return false;
  }
  f.seek(off); int16_t* pcm=(int16_t*)audioAlloc(bytes);
  if(!pcm){ f.close(); sd_bus_unlock(); setError("not enough RAM for WAV"); return false; }
  size_t got=f.read((uint8_t*)pcm,bytes); f.close();
  sd_bus_unlock();
  if(got!=bytes){ free(pcm); return false; }
  size_t samples=bytes/2;
  if(ch==2){size_t frames=samples/2; for(size_t i=0;i<frames;++i){int32_t v=(int32_t)pcm[2*i]+pcm[2*i+1];pcm[i]=(int16_t)(v/2);}samples=frames;}
  M5.Mic.end(); M5.Speaker.begin(); M5.Speaker.setVolume(TUTOR_WAV_VOLUME);
  bool ok=M5.Speaker.playRaw(pcm,samples,rate,false,1,0);
  if(ok) while(M5.Speaker.isPlaying()){M5.update();delay(2);}
  free(pcm);
  M5.Speaker.end();
  // Button-only tutoring does not consume the microphone. Leaving it stopped
  // avoids creating a mic_task between back-to-back local WAV clips. Voice
  // input modes restore it here so their listen path remains unchanged.
  if (_inputReady) M5.Mic.begin();
  return ok;
}

bool VoiceLite::playTextWav(const String& text) {
  if(!_settings.localAudio || text.length()==0)return false;
  String path=audioPathForText(text);
  if(!_fs->exists(path)) { Serial.println("[AUDIO MISS] "+path+" :: "+text); return false; }
  Serial.println("[LOCAL WAV] "+path);
  return playWav(path);
}

bool VoiceLite::speak(const String& text, const String& languageHint) {
  (void)languageHint;
  Serial.println("[SAY] "+text);
  if(playTextWav(text)) return true;
  missingTone(); return false;
}

bool VoiceLite::speakQuestion(const Question& q, const String& languageHint) {
  String text=q.tts.length()?q.tts:q.question;
  return speak(text,languageHint);
}

bool VoiceLite::connectWiFi() {
  if(_settings.wifiSsid.length()==0 || _settings.wifiSsid=="YOUR_WIFI_SSID") { setError("cloud STT Wi-Fi not configured");return false; }
  WiFi.mode(WIFI_STA); if(WiFi.status()==WL_CONNECTED)return true;
  WiFi.begin(_settings.wifiSsid.c_str(),_settings.wifiPassword.c_str());
  uint32_t s=millis(); while(WiFi.status()!=WL_CONNECTED && millis()-s<15000){delay(250);}
  if(WiFi.status()!=WL_CONNECTED){setError("Wi-Fi failed; button fallback active");return false;}
  Serial.print("[VOICE LITE] IP: ");Serial.println(WiFi.localIP());return true;
}

bool VoiceLite::recordWav(const char* path, bool& heardVoice) {
  heardVoice=false;
  const uint32_t maxSamples=(VOICE_SAMPLE_RATE*_settings.maxRecordMs)/1000;
  int16_t* pcm=(int16_t*)audioAlloc(maxSamples*sizeof(int16_t));
  if(!pcm){setError("not enough RAM/PSRAM for microphone");return false;}
  while(M5.Speaker.isPlaying())delay(2); M5.Speaker.end(); M5.Mic.begin(); delay(100);
  if(!M5.Mic.isEnabled()){free(pcm);setError("microphone unavailable; use buttons");return false;}
  size_t used=0; uint32_t silentMs=0; uint32_t voiceStart=0;
  const uint32_t chunkMs=(VOICE_RECORD_CHUNK*1000UL)/VOICE_SAMPLE_RATE;
  while(used+VOICE_RECORD_CHUNK<=maxSamples) {
    if(!M5.Mic.record(pcm+used,VOICE_RECORD_CHUNK,VOICE_SAMPLE_RATE)){delay(1);continue;}
    while(M5.Mic.isRecording()){M5.update();delay(1);}
    uint16_t peak=0; for(size_t i=0;i<VOICE_RECORD_CHUNK;++i){int32_t v=pcm[used+i];if(v<0)v=-v;if(v>32767)v=32767;if(v>peak)peak=(uint16_t)v;}
    used+=VOICE_RECORD_CHUNK;
    if(peak>=_settings.vadThreshold){if(!heardVoice)voiceStart=millis();heardVoice=true;silentMs=0;}
    else if(heardVoice){silentMs+=chunkMs;if(millis()-voiceStart>450 && silentMs>=_settings.silenceMs)break;}
  }
  while(M5.Mic.isRecording())delay(1); M5.Mic.end();
  if(!heardVoice||used<VOICE_SAMPLE_RATE/5){free(pcm);setError("no voice detected");return false;}
  _fs->remove(path); File f=_fs->open(path,FILE_WRITE); if(!f){free(pcm);setError("cannot create answer.wav");return false;}
  uint32_t bytes=used*sizeof(int16_t); writeWavHeader(f,VOICE_SAMPLE_RATE,bytes); size_t wr=f.write((uint8_t*)pcm,bytes);f.close();free(pcm);
  return wr==bytes;
}

bool VoiceLite::cloudTranscribe(const char* path, String& transcript, const String& languageHint) {
  transcript=""; if(!_settings.cloudEnabled||!_settings.cloudUrl.length())return false;
  if(WiFi.status()!=WL_CONNECTED && !connectWiFi())return false;
  File f=_fs->open(path,FILE_READ); if(!f){setError("answer.wav missing");return false;} size_t sz=f.size();
  String boundary="----StackchanLiteBoundary";
  String pre="--"+boundary+"\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n"+_settings.cloudModel+"\r\n";
  if(languageHint.length()) pre+="--"+boundary+"\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\n"+languageHint+"\r\n";
  pre+="--"+boundary+"\r\nContent-Disposition: form-data; name=\"file\"; filename=\"answer.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
  String post="\r\n--"+boundary+"--\r\n";
  size_t total=pre.length()+sz+post.length(); uint8_t* body=(uint8_t*)audioAlloc(total); if(!body){f.close();setError("not enough RAM for cloud upload");return false;}
  memcpy(body,pre.c_str(),pre.length()); size_t got=f.read(body+pre.length(),sz);f.close();memcpy(body+pre.length()+sz,post.c_str(),post.length()); if(got!=sz){free(body);return false;}
  HTTPClient http; http.setTimeout(VOICE_HTTP_TIMEOUT_MS); int code=-1;
  WiFiClientSecure secureClient;
  if(_settings.cloudUrl.startsWith("https://")) {
    if(_settings.tlsInsecure)secureClient.setInsecure();
    if(!http.begin(secureClient,_settings.cloudUrl)){free(body);setError("cannot open cloud STT URL");return false;}
    if(_settings.cloudApiKey.length())http.addHeader("Authorization","Bearer "+_settings.cloudApiKey);
    http.addHeader("Content-Type","multipart/form-data; boundary="+boundary); code=http.POST(body,total);
  } else {
    if(!http.begin(_settings.cloudUrl)){free(body);setError("cannot open cloud STT URL");return false;}
    if(_settings.cloudApiKey.length())http.addHeader("Authorization","Bearer "+_settings.cloudApiKey);
    http.addHeader("Content-Type","multipart/form-data; boundary="+boundary); code=http.POST(body,total);
  }
  free(body); String resp=http.getString(); http.end();
  if(code<200||code>=300){setError("cloud STT HTTP "+String(code));return false;}
  JsonDocument d; if(deserializeJson(d,resp)){setError("cloud STT JSON parse failed");return false;}
  transcript=String((const char*)(d["text"]|"")); transcript.trim();
  if(!transcript.length()){setError("cloud STT returned empty text");return false;} return true;
}

bool VoiceLite::listen(String& transcript, const String& languageHint) {
  transcript="";
  String m=_settings.inputMode; m.toLowerCase();
  if(!_inputReady) return false;
  if(m=="espsr") return _offline.listen(transcript, _settings.maxRecordMs);
  if(m=="cloud") {
    bool heard=false; if(!recordWav(VOICE_RECORD_PATH,heard))return false;
    return cloudTranscribe(VOICE_RECORD_PATH,transcript,languageHint);
  }
  return false;
}
