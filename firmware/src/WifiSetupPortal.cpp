#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESP32WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include "WifiSetupPortal.h"
#include "WifiConfig.h"

namespace {

constexpr char AP_SSID[] = "StackChan-Setup";
constexpr byte DNS_PORT = 53;

DNSServer dnsServer;
ESP32WebServer portal(80);
bool g_saved = false;

String jsonEscape(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\\' || c == '"') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if (c == '\r') o += "\\r";
    else o += c;
  }
  return o;
}

const char PORTAL_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>스택쨩 Wi-Fi 설정</title>
<style>
body{font-family:sans-serif;margin:1em;background:#f4f5f7;color:#222}
.box{max-width:22em;margin:auto;background:#fff;padding:1.2em;border-radius:10px;box-shadow:0 1px 4px #0001}
h1{font-size:1.2em;margin:0 0 .8em}
label{display:block;margin:.7em 0 .3em;font-weight:bold;font-size:.9em}
input{width:100%;padding:.55em;border:1px solid #ccc;border-radius:6px;box-sizing:border-box;font-size:1em}
button{margin-top:.9em;width:100%;padding:.7em;border:none;border-radius:6px;background:#0077cc;color:#fff;font-size:1em}
button.ghost{background:#e9eaed;color:#333}
.note{font-size:.85em;color:#666;margin-top:.8em}
#scanList{margin:.5em 0;max-height:10em;overflow:auto}
#scanList button{width:auto;margin:.2em .2em 0 0;padding:.35em .7em;font-size:.85em}
.status{margin-top:.6em;min-height:1.2em}
.ok{color:#070}.err{color:#c22}
</style>
</head>
<body>
<div class="box">
  <h1>스택쨩 Wi-Fi 설정</h1>
  <p class="note">집 Wi-Fi 이름과 비밀번호를 입력하세요. 저장하면 기기가 다시 시작합니다.</p>
  <label for="ssid">Wi-Fi 이름 (SSID)</label>
  <input id="ssid" type="text" autocomplete="off" autocapitalize="none">
  <label for="pwd">비밀번호</label>
  <input id="pwd" type="password" autocomplete="off">
  <button type="button" id="saveBtn">저장하고 재부팅</button>
  <button type="button" id="scanBtn" class="ghost">주변 Wi-Fi 검색</button>
  <div id="scanList"></div>
  <div id="status" class="status"></div>
</div>
<script>
const $=id=>document.getElementById(id);
function setStatus(msg,ok){$('status').textContent=msg;$('status').className='status '+(ok?'ok':'err');}
$('scanBtn').onclick=()=>{
  setStatus('검색 중...',true);
  fetch('/scan').then(r=>r.json()).then(d=>{
    const box=$('scanList'); box.innerHTML='';
    (d.aps||[]).forEach(ap=>{
      const b=document.createElement('button');
      b.type='button'; b.className='ghost';
      b.textContent=ap.ssid+(ap.rssi?' ('+ap.rssi+')':'');
      b.onclick=()=>{$('ssid').value=ap.ssid;};
      box.appendChild(b);
    });
    setStatus((d.aps&&d.aps.length)?('찾음: '+d.aps.length+'개'):'주변 AP 없음',true);
  }).catch(e=>setStatus('검색 실패: '+e,false));
};
$('saveBtn').onclick=()=>{
  const ssid=$('ssid').value.trim();
  const password=$('pwd').value;
  if(!ssid){setStatus('SSID를 입력하세요.',false);return;}
  setStatus('저장 중...',true);
  const body=JSON.stringify({ssid:ssid,password:password});
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:body})
    .then(r=>r.text().then(t=>({ok:r.ok,t})))
    .then(o=>{
      if(o.ok){setStatus('저장됨. 재부팅합니다...',true);}
      else setStatus('실패: '+o.t,false);
    }).catch(e=>setStatus('실패: '+e,false));
};
</script>
</body>
</html>
)HTML";

void drawPortalScreen() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(8, 20);
  M5.Display.println("Wi-Fi Setup");
  M5.Display.setTextSize(1);
  M5.Display.setCursor(8, 60);
  M5.Display.println("1) Phone: join Wi-Fi");
  M5.Display.setCursor(8, 80);
  M5.Display.printf("   %s", AP_SSID);
  M5.Display.setCursor(8, 110);
  M5.Display.println("2) Open browser:");
  M5.Display.setCursor(8, 130);
  M5.Display.println("   http://192.168.4.1");
  M5.Display.setCursor(8, 170);
  M5.Display.println("Enter home Wi-Fi SSID");
  M5.Display.setCursor(8, 190);
  M5.Display.println("and password, then Save.");
}

void handleRoot() {
  portal.sendHeader("Cache-Control", "no-cache");
  portal.send_P(200, "text/html", PORTAL_HTML);
}

void handleSave() {
  String body = portal.arg("plain");
  if (body.length() == 0) {
    // form-urlencoded fallback
    String ssid = portal.arg("ssid");
    String pwd = portal.arg("password");
    if (ssid.length() == 0) {
      portal.send(400, "text/plain", "ssid required");
      return;
    }
    body = String("{\"networks\":[{\"ssid\":\"") + jsonEscape(ssid)
         + "\",\"password\":\"" + jsonEscape(pwd) + "\"}]}";
  } else {
    // JSON {ssid, password} from fetch
    DynamicJsonDocument in(512);
    if (deserializeJson(in, body)) {
      portal.send(400, "text/plain", "invalid JSON");
      return;
    }
    const char* ssid = in["ssid"] | "";
    const char* pwd = in["password"] | "";
    if (!ssid || !*ssid) {
      portal.send(400, "text/plain", "ssid required");
      return;
    }
    body = String("{\"networks\":[{\"ssid\":\"") + jsonEscape(String(ssid))
         + "\",\"password\":\"" + jsonEscape(String(pwd)) + "\"}]}";
  }

  if (!SPIFFS.begin(true)) {
    portal.send(500, "text/plain", "SPIFFS failed");
    return;
  }
  if (!wifi_set_json(body)) {
    portal.send(500, "text/plain", "save failed");
    return;
  }
  portal.send(200, "text/plain; charset=utf-8", "ok");
  g_saved = true;
}

void handleScan() {
  // Prefer AP+STA so SoftAP stays up while scanning.
  WiFi.mode(WIFI_AP_STA);
  delay(50);
  String out = wifi_scan_json();
  portal.sendHeader("Cache-Control", "no-cache");
  portal.send(200, "application/json", out);
}

void handleCaptive() {
  // Android/iOS captive portal probes — redirect to our form.
  portal.sendHeader("Location", "http://192.168.4.1/", true);
  portal.send(302, "text/plain", "");
}

void handleNotFound() {
  handleCaptive();
}

}  // namespace


void wifi_setup_portal_run(void) {
  Serial.println("[wifi-portal] starting SoftAP StackChan-Setup");
  g_saved = false;

  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_AP);
  // Open SoftAP — phone joins without a password.
  bool ok = WiFi.softAP(AP_SSID);
  delay(200);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[wifi-portal] softAP=%d IP=%s\n", (int)ok, ip.toString().c_str());

  drawPortalScreen();

  dnsServer.start(DNS_PORT, "*", ip);

  portal.on("/", HTTP_GET, handleRoot);
  portal.on("/save", HTTP_POST, handleSave);
  portal.on("/scan", HTTP_GET, handleScan);
  portal.on("/generate_204", HTTP_GET, handleCaptive);      // Android
  portal.on("/gen_204", HTTP_GET, handleCaptive);
  portal.on("/hotspot-detect.html", HTTP_GET, handleCaptive); // Apple
  portal.on("/connecttest.txt", HTTP_GET, handleCaptive);
  portal.on("/ncsi.txt", HTTP_GET, handleCaptive);
  portal.onNotFound(handleNotFound);
  portal.begin();

  Serial.println("[wifi-portal] waiting for credentials (no timeout)");
  while (!g_saved) {
    dnsServer.processNextRequest();
    portal.handleClient();
    delay(2);
  }

  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(8, 40);
  M5.Display.setTextSize(2);
  M5.Display.println("Saved!");
  M5.Display.setTextSize(1);
  M5.Display.setCursor(8, 90);
  M5.Display.println("Rebooting...");
  Serial.println("[wifi-portal] credentials saved — restarting");
  delay(1200);
  ESP.restart();
}
