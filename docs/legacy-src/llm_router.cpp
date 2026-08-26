#include "llm_router.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static const char* OPENAI_API_KEY = "sk-xxxxxxxx";  // TODO: NVS/SecureStorage로 이관
static const char* CHAT_URL = "https://api.openai.com/v1/chat/completions";

// tool 스키마 정의 (앞서 정리한 6개 함수: take_photo, enroll_face, delete_face,
// play_sd_content, web_search, start_timer). 실제 값은 stackchan_function_schemas.json과 동일.
static const char* TOOLS_JSON = R"JSON(
[
  {"type":"function","function":{"name":"take_photo",
    "description":"사진 촬영 후 저장/프린터/디스플레이 전송/얼굴 인식/사물 인식 중 하나 수행",
    "parameters":{"type":"object","properties":{
      "output":{"type":"string","enum":["save","print","display","recognize_face","recognize_object"]}
    },"required":["output"]}}},
  {"type":"function","function":{"name":"enroll_face",
    "description":"새 가족 구성원 얼굴 등록. name이 비어있으면 먼저 되묻는다.",
    "parameters":{"type":"object","properties":{
      "name":{"type":"string"}
    },"required":["name"]}}},
  {"type":"function","function":{"name":"delete_face",
    "description":"등록된 얼굴 삭제",
    "parameters":{"type":"object","properties":{
      "name":{"type":"string"}
    },"required":["name"]}}},
  {"type":"function","function":{"name":"play_sd_content",
    "description":"SD카드 공부 프로그램 구동 또는 음악 재생",
    "parameters":{"type":"object","properties":{
      "content_type":{"type":"string","enum":["study_program","music"]},
      "name":{"type":"string"}
    },"required":["content_type"]}}},
  {"type":"function","function":{"name":"web_search",
    "description":"인터넷 검색 (SafeSearch 항상 적용)",
    "parameters":{"type":"object","properties":{
      "query":{"type":"string"}
    },"required":["query"]}}},
  {"type":"function","function":{"name":"start_timer",
    "description":"일반 사용 시간 제한 타이머 시작 (공부 프로그램 제외)",
    "parameters":{"type":"object","properties":{
      "duration_minutes":{"type":"integer"}
    },"required":[]}}}
]
)JSON";

// 세션 동안 누적되는 대화 기록 (문맥 유지를 위해 매 호출에 함께 전송)
static JsonDocument conversation;  // conversation["messages"] = JsonArray

static void ensureInit() {
  if (!conversation["messages"].is<JsonArray>()) {
    conversation["messages"].to<JsonArray>();
  }
}

// 함수 이름 -> 인자 파싱 -> 핸들러 디스패치
static String dispatchToolCall(const String& name, JsonObject args) {
  if (name == "take_photo") {
    return handleTakePhoto(args["output"] | "save");
  } else if (name == "enroll_face") {
    return handleEnrollFace(args["name"] | "");
  } else if (name == "delete_face") {
    return handleDeleteFace(args["name"] | "");
  } else if (name == "play_sd_content") {
    return handlePlaySdContent(args["content_type"] | "", args["name"] | "");
  } else if (name == "web_search") {
    return handleWebSearch(args["query"] | "");
  } else if (name == "start_timer") {
    return handleStartTimer(args["duration_minutes"] | 30);
  }
  return "지원하지 않는 함수 호출입니다.";
}

// GPT-4o mini에 현재 conversation을 보내고 응답 메시지 JsonObject를 반환.
// 실패 시 빈 JsonObject 반환.
static JsonObject callChatCompletions() {
  static JsonDocument responseDoc;  // 매 호출마다 재사용

  WiFiClientSecure client;
  client.setInsecure();  // TODO: 프로덕션에서는 루트 CA 인증서로 교체 필요

  HTTPClient http;
  http.begin(client, CHAT_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + OPENAI_API_KEY);

  JsonDocument reqDoc;
  reqDoc["model"] = "gpt-4o-mini";
  reqDoc["messages"] = conversation["messages"];
  JsonDocument toolsDoc;
  deserializeJson(toolsDoc, TOOLS_JSON);
  reqDoc["tools"] = toolsDoc;

  String body;
  serializeJson(reqDoc, body);

  int code = http.POST(body);
  if (code != 200) {
    Serial.printf("[LLM] API 호출 실패, code=%d\n", code);
    http.end();
    return JsonObject();
  }

  String payload = http.getString();
  http.end();

  responseDoc.clear();
  DeserializationError err = deserializeJson(responseDoc, payload);
  if (err) {
    Serial.printf("[LLM] 응답 파싱 실패: %s\n", err.c_str());
    return JsonObject();
  }

  return responseDoc["choices"][0]["message"];
}

String routeUserUtterance(const String& userText) {
  ensureInit();

  JsonArray messages = conversation["messages"];
  JsonObject userMsg = messages.add<JsonObject>();
  userMsg["role"] = "user";
  userMsg["content"] = userText;

  // 최대 3회까지 tool_call -> 재호출 루프 허용 (무한루프 방지)
  for (int i = 0; i < 3; i++) {
    JsonObject message = callChatCompletions();
    if (message.isNull()) {
      return "죄송해요, 지금은 대답하기 어려워요.";
    }

    JsonArray toolCalls = message["tool_calls"];
    if (toolCalls.isNull() || toolCalls.size() == 0) {
      // 최종 자연어 응답
      String finalText = message["content"] | "";
      JsonObject assistantMsg = messages.add<JsonObject>();
      assistantMsg["role"] = "assistant";
      assistantMsg["content"] = finalText;
      return finalText;
    }

    // assistant의 tool_call 요청 메시지를 대화 기록에 그대로 추가
    JsonObject assistantMsg = messages.add<JsonObject>();
    assistantMsg["role"] = "assistant";
    assistantMsg["tool_calls"] = toolCalls;

    // 각 tool_call 실행 후 결과를 tool 메시지로 추가
    for (JsonObject call : toolCalls) {
      String callId = call["id"] | "";
      String fnName = call["function"]["name"] | "";
      String argsStr = call["function"]["arguments"] | "{}";

      JsonDocument argsDoc;
      deserializeJson(argsDoc, argsStr);
      String result = dispatchToolCall(fnName, argsDoc.as<JsonObject>());

      JsonObject toolMsg = messages.add<JsonObject>();
      toolMsg["role"] = "tool";
      toolMsg["tool_call_id"] = callId;
      toolMsg["content"] = result;
    }
    // 루프 재진입: tool 결과를 반영한 최종 응답을 다시 요청
  }

  return "요청을 처리하는 데 시간이 너무 오래 걸려요.";
}
