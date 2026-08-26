#pragma once
#include <Arduino.h>
#include <FS.h>
#include "Question.h"
#include "OfflineSpeechInput.h"

struct VoiceLiteSettings {
  bool enabled = true;
  bool localAudio = true;
  bool missingAudioTone = true;
  bool autoListen = false;
  String inputMode = "button";      // button | espsr | cloud
  String audioRoot = "/kids_tutor/audio/text";

  // Optional cloud STT. Runtime works without this.
  bool cloudEnabled = false;
  String wifiSsid;
  String wifiPassword;
  String cloudUrl;
  String cloudApiKey;
  String cloudModel;
  bool tlsInsecure = false;

  uint32_t maxRecordMs = 5000;
  uint32_t silenceMs = 850;
  uint16_t vadThreshold = 550;
};

class VoiceLite {
public:
  bool begin(fs::FS& fs, const char* configPath);
  bool speak(const String& text, const String& languageHint = "en");
  bool speakQuestion(const Question& q, const String& languageHint = "en");
  bool listen(String& transcript, const String& languageHint = "en");

  bool audioReady() const { return _audioReady; }
  bool inputReady() const { return _inputReady; }
  bool autoListen() const { return _settings.autoListen && _inputReady; }
  String modeLabel() const;
  const String& lastError() const { return _lastError; }

  static uint32_t textHash(const String& text);
  String audioPathForText(const String& text) const;

private:
  fs::FS* _fs = nullptr;
  VoiceLiteSettings _settings;
  bool _inputReady = false;
  bool _audioReady = false;
  String _lastError;
  OfflineSpeechInput _offline;

  bool loadSettings(const char* path);
  bool playTextWav(const String& text);
  bool playWav(const String& path);
  bool parseWav(File& f, uint32_t& dataOffset, uint32_t& dataBytes,
                uint32_t& sampleRate, uint16_t& channels, uint16_t& bitsPerSample);
  bool recordWav(const char* path, bool& heardVoice);
  bool cloudTranscribe(const char* path, String& transcript, const String& languageHint);
  bool connectWiFi();
  void setError(const String& message);
  void missingTone();
};
