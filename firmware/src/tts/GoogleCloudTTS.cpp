#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <ctype.h>
#include <esp_heap_caps.h>
#include "libb64/cdecode.h"
#include "rootCA/rootCAgoogle.h"
#include "tts/GoogleCloudTTS.h"
#include "SpiRamJsonDocument.h"
#include "driver/PlayMP3.h"

namespace {

bool mostly_korean(const String& text) {
  int hangul = 0;
  int latin = 0;
  for (unsigned i = 0; i < text.length();) {
    uint8_t c = (uint8_t)text[i];
    if (c < 0x80) {
      if (isalpha(c)) latin++;
      i++;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < text.length()) {
      uint32_t cp = ((uint32_t)(c & 0x0F) << 12)
                  | ((uint32_t)((uint8_t)text[i + 1] & 0x3F) << 6)
                  | (uint32_t)((uint8_t)text[i + 2] & 0x3F);
      if (cp >= 0xAC00 && cp <= 0xD7A3) hangul++;
      i += 3;
    } else if ((c & 0xE0) == 0xC0) {
      i += 2;
    } else if ((c & 0xF8) == 0xF0) {
      i += 4;
    } else {
      i++;
    }
  }
  return hangul >= latin;
}

bool looks_like_google_voice(const String& voice) {
  if (voice.length() == 0) return false;
  if (voice.startsWith("ko-") || voice.startsWith("en-")) return true;
  if (voice.indexOf("Wavenet") >= 0 || voice.indexOf("Standard") >= 0
      || voice.indexOf("Neural") >= 0 || voice.indexOf("Chirp") >= 0) {
    return true;
  }
  return false;
}

String pick_voice(const tts_param_t& param, const String& text) {
  const bool ko = mostly_korean(text);
  if (!ko) return String("en-US-Wavenet-C");
  if (looks_like_google_voice(param.voice) && !param.voice.startsWith("en-")) {
    return param.voice;
  }
  return String("ko-KR-Wavenet-A");
}

String language_for_voice(const String& voice) {
  if (voice.startsWith("en-")) return String("en-US");
  return String("ko-KR");
}

int b64_decode(const char* input, char* output) {
  base64_decodestate s;
  base64_init_decodestate(&s);
  int cnt = base64_decode_block(input, strlen(input), output, &s);
  output[cnt] = 0;
  return cnt;
}

}  // namespace


void GoogleCloudTTS::stream(String text) {
  if (text.length() == 0) return;
  if (param.api_key.length() == 0) {
    Serial.println("[gtts] missing apikey.tts");
    return;
  }

  String voice = pick_voice(param, text);
  String lang = language_for_voice(voice);
  Serial.printf("[gtts] voice=%s lang=%s chars=%d\n", voice.c_str(), lang.c_str(), text.length());

  DynamicJsonDocument req(text.length() + 512);
  req["input"]["text"] = text;
  req["voice"]["languageCode"] = lang;
  req["voice"]["name"] = voice;
  req["audioConfig"]["audioEncoding"] = "MP3";
  String body;
  serializeJson(req, body);

  String url = String("https://texttospeech.googleapis.com/v1/text:synthesize?key=") + param.api_key;

  WiFiClientSecure *client = new WiFiClientSecure;
  if (!client) {
    Serial.println("[gtts] unable to create TLS client");
    return;
  }
  client->setCACert(root_ca_google);

  String payload;
  {
    HTTPClient https;
    https.setTimeout(20000);
    if (!https.begin(*client, url)) {
      Serial.println("[gtts] https begin failed");
      delete client;
      return;
    }
    https.addHeader("Content-Type", "application/json");
    int code = https.POST((uint8_t*)body.c_str(), body.length());
    if (code == HTTP_CODE_OK) {
      payload = https.getString();
    } else {
      Serial.printf("[gtts] HTTP %d %s\n", code, https.errorToString(code).c_str());
      String err = https.getString();
      if (err.length()) Serial.println(err.substring(0, 240));
    }
    https.end();
  }
  delete client;
  body = String();

  if (payload.length() == 0) return;

  SpiRamJsonDocument doc(payload.length() + 2048);
  DeserializationError err = deserializeJson(doc, payload);
  payload = String();
  if (err) {
    Serial.printf("[gtts] json error: %s\n", err.c_str());
    return;
  }
  const char* b64 = doc["audioContent"] | "";
  if (!b64 || !*b64) {
    Serial.println("[gtts] empty audioContent");
    return;
  }

  size_t b64len = strlen(b64);
  size_t maxOut = (b64len * 3) / 4 + 8;
  char* mp3buf = (char*)heap_caps_malloc(maxOut, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!mp3buf) mp3buf = (char*)malloc(maxOut);
  if (!mp3buf) {
    Serial.println("[gtts] OOM decoding mp3");
    return;
  }
  int n = b64_decode(b64, mp3buf);
  doc.clear();

  if (n <= 0) {
    Serial.println("[gtts] base64 decode failed");
    free(mp3buf);
    return;
  }

  if (!SPIFFS.begin(true)) {
    Serial.println("[gtts] SPIFFS mount failed");
    free(mp3buf);
    return;
  }
  File f = SPIFFS.open("/gtts.mp3", FILE_WRITE);
  if (!f) {
    Serial.println("[gtts] SPIFFS open failed");
    free(mp3buf);
    return;
  }
  size_t wrote = f.write((const uint8_t*)mp3buf, n);
  f.close();
  free(mp3buf);
  if ((int)wrote != n) {
    Serial.printf("[gtts] short write %d / %d\n", (int)wrote, n);
    return;
  }

  playMP3SPIFFS("/gtts.mp3");
}
