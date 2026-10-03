#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <time.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>


// Hardware configuration
namespace Pins {
  constexpr uint8_t SW1 = D0; // GPIO2
  constexpr uint8_t SW2 = D1; // GPIO3

  constexpr uint8_t SW3 = D2; // GPIO4 
  constexpr uint8_t SW4 = D3; // GPIO5 

  constexpr uint8_t BUZZER = D4; // GPIO6 

  constexpr uint8_t TFT_BL  = D5;  // GPIO7 
  constexpr uint8_t TFT_DC  = D6;  // GPIO21 
  constexpr uint8_t TFT_RST = D7;  // GPIO20 
  constexpr uint8_t TFT_SCK = D8;  // GPIO8  
  constexpr uint8_t TFT_CS  = D9;  // GPIO9  
  constexpr uint8_t TFT_MOSI = D10; // GPIO10 
}

constexpr uint16_t TFT_NATIVE_W = 240;
constexpr uint16_t TFT_NATIVE_H = 320;
constexpr uint8_t TFT_ROTATION = 1; // landscape; change to 0/1/2/3 as required

// Firmware constants
constexpr uint8_t MAX_ALARMS = 8;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 60;
constexpr uint32_t LONG_PRESS_MS = 900;
constexpr uint32_t DISPLAY_REFRESH_MS = 500;
constexpr int DEFAULT_TZ_MINUTES = 240; // UTC+04:00
constexpr uint16_t DEFAULT_BEEP_HZ = 1800;
constexpr uint8_t DEFAULT_BRIGHTNESS = 210;
constexpr char AP_NAME[] = "NovaClock-Setup";
constexpr char AP_PASSWORD[] = "novaclock"; // local setup AP only
constexpr char MDNS_NAME[] = "novaclock";

WebServer server(80);
Preferences prefs;
Adafruit_ST7789 tft(&SPI, Pins::TFT_CS, Pins::TFT_DC, Pins::TFT_RST);

// Data model
struct Alarm {
  bool enabled = false;
  uint8_t hour = 7;
  uint8_t minute = 0;
  char label[21] = "Alarm";
};

Alarm alarms[MAX_ALARMS];

String displayStyle = "digital";    // digital, minimal, info
String displayFont = "large";       // small, large, xl
String soundPattern = "double";     // beep, double, triple, pulse
String deviceName = "Nova Clock";
int tzMinutes = DEFAULT_TZ_MINUTES;
uint8_t brightness = DEFAULT_BRIGHTNESS;
bool nightMode = false;
bool showAlarmList = false;

String lastAlarmMinute = "";
int activeAlarmIndex = -1;
uint32_t lastDisplayDraw = 0;
uint32_t lastWiFiRetry = 0;

// Non-blocking buzzer sequencer
struct ToneStep {
  uint16_t frequency;
  uint16_t duration;
  uint16_t gap;
};

ToneStep soundSteps[4];
uint8_t soundStepCount = 0;
uint8_t soundStepIndex = 0;
bool soundPlaying = false;
uint32_t soundStepStarted = 0;
bool soundInGap = false;
uint16_t alarmSoundFrequency = DEFAULT_BEEP_HZ;

void stopTone() {
  ledcWriteTone(Pins::BUZZER, 0);
}

void startTone(uint16_t frequency) {
  if (frequency == 0) {
    stopTone();
    return;
  }
  ledcWriteTone(Pins::BUZZER, frequency);
}

void startSoundPattern(const String &pattern, uint16_t frequency) {
  alarmSoundFrequency = constrain(frequency, 200, 5000);
  soundStepCount = 0;

  if (pattern == "triple") {
    soundSteps[0] = {alarmSoundFrequency, 100, 80};
    soundSteps[1] = {alarmSoundFrequency, 100, 80};
    soundSteps[2] = {alarmSoundFrequency, 180, 120};
    soundStepCount = 3;
  } else if (pattern == "pulse") {
    soundSteps[0] = {alarmSoundFrequency, 70, 150};
    soundSteps[1] = {alarmSoundFrequency, 260, 100};
    soundStepCount = 2;
  } else if (pattern == "beep") {
    soundSteps[0] = {alarmSoundFrequency, 220, 120};
    soundStepCount = 1;
  } else {
    soundSteps[0] = {alarmSoundFrequency, 120, 90};
    soundSteps[1] = {alarmSoundFrequency, 220, 120};
    soundStepCount = 2;
  }

  soundStepIndex = 0;
  soundPlaying = true;
  soundInGap = false;
  soundStepStarted = millis();
  startTone(soundSteps[0].frequency);
  Serial.printf("[BUZZER] pattern=%s freq=%u Hz\n", pattern.c_str(), alarmSoundFrequency);
}

void serviceSound() {
  if (!soundPlaying || soundStepIndex >= soundStepCount) return;

  const uint32_t now = millis();
  const ToneStep &step = soundSteps[soundStepIndex];

  if (!soundInGap && now - soundStepStarted >= step.duration) {
    stopTone();
    soundInGap = true;
    soundStepStarted = now;
  }

  if (soundInGap && now - soundStepStarted >= step.gap) {
    ++soundStepIndex;
    if (soundStepIndex >= soundStepCount) {
      soundPlaying = false;
      stopTone();
      return;
    }
    soundInGap = false;
    soundStepStarted = now;
    startTone(soundSteps[soundStepIndex].frequency);
  }
}

// Persistence
String alarmKey(uint8_t index, const char *field) {
  return "a" + String(index) + field;
}

void saveSettings() {
  prefs.begin("nova", false);
  prefs.putString("style", displayStyle);
  prefs.putString("font", displayFont);
  prefs.putString("sound", soundPattern);
  prefs.putString("name", deviceName);
  prefs.putInt("tzmin", tzMinutes);
  prefs.putUChar("bright", brightness);
  prefs.putBool("night", nightMode);

  for (uint8_t i = 0; i < MAX_ALARMS; ++i) {
    prefs.putBool(alarmKey(i, "e").c_str(), alarms[i].enabled);
    prefs.putUChar(alarmKey(i, "h").c_str(), alarms[i].hour);
    prefs.putUChar(alarmKey(i, "m").c_str(), alarms[i].minute);
    prefs.putString(alarmKey(i, "l").c_str(), alarms[i].label);
  }
  prefs.end();
}

void loadSettings() {
  prefs.begin("nova", true);
  displayStyle = prefs.getString("style", "digital");
  displayFont = prefs.getString("font", "large");
  soundPattern = prefs.getString("sound", "double");
  deviceName = prefs.getString("name", "Nova Clock");
  tzMinutes = prefs.getInt("tzmin", DEFAULT_TZ_MINUTES);
  brightness = prefs.getUChar("bright", DEFAULT_BRIGHTNESS);
  nightMode = prefs.getBool("night", false);

  alarms[0].enabled = prefs.getBool("a0e", true);
  alarms[0].hour = prefs.getUChar("a0h", 7);
  alarms[0].minute = prefs.getUChar("a0m", 0);
  String defaultLabel = prefs.getString("a0l", "Morning");
  strncpy(alarms[0].label, defaultLabel.c_str(), sizeof(alarms[0].label) - 1);

  for (uint8_t i = 1; i < MAX_ALARMS; ++i) {
    alarms[i].enabled = prefs.getBool(alarmKey(i, "e").c_str(), false);
    alarms[i].hour = prefs.getUChar(alarmKey(i, "h").c_str(), 8);
    alarms[i].minute = prefs.getUChar(alarmKey(i, "m").c_str(), 0);
    String label = prefs.getString(alarmKey(i, "l").c_str(), "Alarm");
    strncpy(alarms[i].label, label.c_str(), sizeof(alarms[i].label) - 1);
    alarms[i].label[sizeof(alarms[i].label) - 1] = '\0';
  }
  prefs.end();
}

// Time
void configureClock() {
  configTime(tzMinutes * 60L, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
}

bool readLocalTime(struct tm &t) {
  return getLocalTime(&t, 20);
}

String currentTimeString() {
  struct tm t;
  if (!readLocalTime(t)) return "--:--:--";
  char buf[9];
  strftime(buf, sizeof(buf), "%H:%M:%S", &t);
  return String(buf);
}


// TFT UI
void setBacklight() {
  uint8_t level = nightMode ? 25 : brightness;
  ledcWrite(Pins::TFT_BL, level);
}

void centerText(const String &text, int16_t y, uint8_t size, uint16_t color = ST77XX_WHITE) {
  int16_t x1, y1;
  uint16_t w, h;
  tft.setTextSize(size);
  tft.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int16_t x = (tft.width() - (int16_t)w) / 2;
  tft.setCursor(x, y);
  tft.setTextColor(color);
  tft.print(text);
}

void drawBootAnimation() {
  tft.fillScreen(ST77XX_BLACK);
  centerText("NOVA", 22, 3, 0xFFE0);
  centerText("CLOCK", 58, 3, 0xFFE0);
  tft.drawLine(40, 99, tft.width() - 40, 99, 0xFFE0);
  for (uint8_t i = 0; i < 5; ++i) {
    tft.fillCircle(tft.width() / 2 - 20 + i * 10, 120, 3, 0xFFE0);
    delay(70);
  }
  delay(250);
}

uint8_t clockTextSize() {
  if (displayFont == "small") return 3;
  if (displayFont == "xl") return 5;
  return 4;
}

void drawClockFace() {
  struct tm t;
  tft.fillScreen(ST77XX_BLACK);

  if (!readLocalTime(t)) {
    centerText("SYNCING TIME", 48, 2, 0xFFE0);
    centerText(WiFi.status() == WL_CONNECTED ? "Wi-Fi connected" : "No network", 82, 1, ST77XX_WHITE);
    return;
  }

  char hm[6];
  strftime(hm, sizeof(hm), "%H:%M", &t);
  char sec[3];
  strftime(sec, sizeof(sec), "%S", &t);

  if (displayStyle == "minimal") {
    centerText(String(hm), 38, clockTextSize(), 0xFFE0);
    centerText(String(sec), 88, 2, ST77XX_WHITE);
  } else if (displayStyle == "info") {
    centerText(String(hm), 26, clockTextSize(), 0xFFE0);
    char day[32];
    strftime(day, sizeof(day), "%a, %d %b", &t);
    centerText(String(day), 80, 2, ST77XX_WHITE);
    String wifi = WiFi.status() == WL_CONNECTED ? "Wi-Fi" : "AP mode";
    centerText(wifi, 108, 1, 0x7BEF);
  } else {
    centerText(String(hm), 30, clockTextSize(), 0xFFE0);
    centerText(String(":"), 31, clockTextSize(), 0xFFE0);
    String secText = String(sec) + " s";
    centerText(secText, 92, 1, ST77XX_WHITE);
  }

  uint8_t active = 0;
  for (const auto &alarm : alarms) if (alarm.enabled) ++active;
  String alarmLine = String(active) + " active alarm" + (active == 1 ? "" : "s");
  centerText(alarmLine, tft.height() - 24, 1, 0x7BEF);
}

void drawAlarmList() {
  tft.fillScreen(ST77XX_BLACK);
  centerText("ACTIVE ALARMS", 8, 2, 0xFFE0);

  uint8_t row = 0;
  for (uint8_t i = 0; i < MAX_ALARMS; ++i) {
    if (!alarms[i].enabled) continue;
    char tm[6];
    snprintf(tm, sizeof(tm), "%02u:%02u", alarms[i].hour, alarms[i].minute);
    String line = String(tm) + "  " + String(alarms[i].label);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, 40 + row * 18);
    tft.print(line.substring(0, 32));
    if (++row >= 7) break;
  }

  if (row == 0) centerText("No active alarms", 58, 1, ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setTextColor(0x7BEF);
  tft.setCursor(10, tft.height() - 16);
  tft.print("SW1: back");
}

void updateDisplay() {
  if (showAlarmList) drawAlarmList();
  else drawClockFace();
  lastDisplayDraw = millis();
}

void displayBegin() {
  SPI.begin(Pins::TFT_SCK, -1, Pins::TFT_MOSI, Pins::TFT_CS);
  tft.init(TFT_NATIVE_W, TFT_NATIVE_H);
  tft.setRotation(TFT_ROTATION);
  tft.fillScreen(ST77XX_BLACK);
  setBacklight();
}

// Alarm logic
void dismissCurrentAlarm() {
  if (activeAlarmIndex < 0) return;
  Serial.printf("[ALARM] Dismissed: %s\n", alarms[activeAlarmIndex].label);
  activeAlarmIndex = -1;
  soundPlaying = false;
  stopTone();
}

void triggerAlarm(uint8_t index) {
  activeAlarmIndex = index;
  showAlarmList = false;
  startSoundPattern(soundPattern, alarmSoundFrequency);
  Serial.printf("[ALARM] Triggered %s at %02u:%02u\n",
                alarms[index].label, alarms[index].hour, alarms[index].minute);
}

void serviceAlarms() {
  struct tm t;
  if (!readLocalTime(t)) return;

  char minuteKey[24];
  strftime(minuteKey, sizeof(minuteKey), "%Y-%m-%d %H:%M", &t);
  if (lastAlarmMinute == minuteKey) return;

  char hm[6];
  strftime(hm, sizeof(hm), "%H:%M", &t);

  for (uint8_t i = 0; i < MAX_ALARMS; ++i) {
    if (!alarms[i].enabled) continue;
    char alarmTime[6];
    snprintf(alarmTime, sizeof(alarmTime), "%02u:%02u", alarms[i].hour, alarms[i].minute);
    if (String(alarmTime) == String(hm)) {
      triggerAlarm(i);
      break;
    }
  }
  lastAlarmMinute = String(minuteKey);
}

// Buttons
struct ButtonState {
  uint8_t pin;
  bool lastLevel;
  uint32_t pressedAt;
  uint32_t changedAt;
};

ButtonState buttons[4] = {
  {Pins::SW1, HIGH, 0, 0},
  {Pins::SW2, HIGH, 0, 0},
  {Pins::SW3, HIGH, 0, 0},
  {Pins::SW4, HIGH, 0, 0},
};

void cycleDisplayStyle() {
  if (displayStyle == "digital") displayStyle = "minimal";
  else if (displayStyle == "minimal") displayStyle = "info";
  else displayStyle = "digital";
  saveSettings();
}

void handleButton(uint8_t number, bool longPress) {
  Serial.printf("[BUTTON] SW%u %s\n", number, longPress ? "LONG" : "SHORT");

  switch (number) {
    case 1:
      if (activeAlarmIndex >= 0) {
        dismissCurrentAlarm();
      } else {
        showAlarmList = !showAlarmList;
      }
      break;

    case 2:
      cycleDisplayStyle();
      break;

    case 3:
      if (activeAlarmIndex >= 0) {
        // acknowledge/dismiss the ringing alarm.
        dismissCurrentAlarm();
      } else {
        startSoundPattern(soundPattern, alarmSoundFrequency);
      }
      break;

    case 4:
      nightMode = !nightMode;
      setBacklight();
      saveSettings();
      break;
  }

  updateDisplay();
}

void serviceButtons() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < 4; ++i) {
    bool level = digitalRead(buttons[i].pin);
    if (level != buttons[i].lastLevel) {
      buttons[i].lastLevel = level;
      buttons[i].changedAt = now;
    }

    if (level == LOW && buttons[i].pressedAt == 0 && now - buttons[i].changedAt >= BUTTON_DEBOUNCE_MS) {
      buttons[i].pressedAt = now;
    }

    if (level == HIGH && buttons[i].pressedAt != 0 && now - buttons[i].changedAt >= BUTTON_DEBOUNCE_MS) {
      const uint32_t held = now - buttons[i].pressedAt;
      buttons[i].pressedAt = 0;
      handleButton(i + 1, held >= LONG_PRESS_MS);
    }
  }
}


// Web control
String jsonEscape(const String &input) {
  String out;
  out.reserve(input.length() + 4);
  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input[i];
    if (c == '"' || c == '\\') out += '\\';
    if (c == '\n') out += "\\n";
    else out += c;
  }
  return out;
}

String statusJson() {
  String json = "{";
  json += "\"device\":\"" + jsonEscape(deviceName) + "\",";
  json += "\"time\":\"" + currentTimeString() + "\",";
  json += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"style\":\"" + displayStyle + "\",";
  json += "\"font\":\"" + displayFont + "\",";
  json += "\"sound\":\"" + soundPattern + "\",";
  json += "\"frequency\":" + String(alarmSoundFrequency) + ",";
  json += "\"timezone_minutes\":" + String(tzMinutes) + ",";
  json += "\"brightness\":" + String(brightness) + ",";
  json += "\"night_mode\":" + String(nightMode ? "true" : "false") + ",";
  json += "\"active_alarm_index\":" + String(activeAlarmIndex) + ",";
  json += "\"alarms\":[";

  for (uint8_t i = 0; i < MAX_ALARMS; ++i) {
    if (i) json += ',';
    char tm[6];
    snprintf(tm, sizeof(tm), "%02u:%02u", alarms[i].hour, alarms[i].minute);
    json += "{\"id\":" + String(i);
    json += ",\"enabled\":" + String(alarms[i].enabled ? "true" : "false");
    json += ",\"time\":\"" + String(tm) + "\"";
    json += ",\"label\":\"" + jsonEscape(String(alarms[i].label)) + "\"}";
  }
  json += "]}";
  return json;
}

void apiStatus() {
  server.send(200, "application/json", statusJson());
}

bool validTime(const String &value) {
  if (value.length() != 5 || value[2] != ':') return false;
  int h = value.substring(0, 2).toInt();
  int m = value.substring(3, 5).toInt();
  return h >= 0 && h <= 23 && m >= 0 && m <= 59;
}

void apiAlarm() {
  int id = server.arg("id").toInt();
  if (id < 0 || id >= MAX_ALARMS) {
    server.send(400, "application/json", "{\"error\":\"bad id\"}");
    return;
  }

  const String timeArg = server.arg("time");
  if (timeArg.length() && !validTime(timeArg)) {
    server.send(400, "application/json", "{\"error\":\"bad time\"}");
    return;
  }

  if (server.hasArg("enabled")) alarms[id].enabled = server.arg("enabled") == "1" || server.arg("enabled") == "true";
  if (timeArg.length()) {
    alarms[id].hour = timeArg.substring(0, 2).toInt();
    alarms[id].minute = timeArg.substring(3, 5).toInt();
  }
  if (server.hasArg("label")) {
    String label = server.arg("label");
    label.replace("<", "");
    label.replace(">", "");
    strncpy(alarms[id].label, label.c_str(), sizeof(alarms[id].label) - 1);
    alarms[id].label[sizeof(alarms[id].label) - 1] = '\0';
  }

  saveSettings();
  server.send(200, "application/json", "{\"ok\":true}");
  updateDisplay();
}

void apiSettings() {
  if (server.hasArg("style")) {
    const String s = server.arg("style");
    if (s == "digital" || s == "minimal" || s == "info") displayStyle = s;
  }
  if (server.hasArg("font")) {
    const String f = server.arg("font");
    if (f == "small" || f == "large" || f == "xl") displayFont = f;
  }
  if (server.hasArg("tz")) tzMinutes = constrain(server.arg("tz").toInt(), -12 * 60, 14 * 60);
  if (server.hasArg("brightness")) brightness = constrain(server.arg("brightness").toInt(), 10, 255);
  if (server.hasArg("night")) nightMode = server.arg("night") == "1" || server.arg("night") == "true";

  setBacklight();
  saveSettings();
  configureClock();
  server.send(200, "application/json", "{\"ok\":true}");
  updateDisplay();
}

void apiSound() {
  const String pattern = server.hasArg("pattern") ? server.arg("pattern") : soundPattern;
  if (pattern == "beep" || pattern == "double" || pattern == "triple" || pattern == "pulse") soundPattern = pattern;
  if (server.hasArg("freq")) alarmSoundFrequency = constrain(server.arg("freq").toInt(), 200, 5000);
  saveSettings();
  startSoundPattern(soundPattern, alarmSoundFrequency);
  server.send(200, "application/json", "{\"ok\":true}");
}

void apiButton() {
  int id = server.arg("n").toInt();
  if (id < 1 || id > 4) {
    server.send(400, "application/json", "{\"error\":\"bad button\"}");
    return;
  }
  handleButton(id, false);
  server.send(200, "application/json", "{\"ok\":true}");
}

void apiWifi() {
  if (!server.hasArg("ssid") || !server.hasArg("password")) {
    server.send(400, "application/json", "{\"error\":\"ssid and password required\"}");
    return;
  }

  prefs.begin("wifi", false);
  prefs.putString("ssid", server.arg("ssid"));
  prefs.putString("pass", server.arg("password"));
  prefs.end();

  server.send(200, "application/json", "{\"ok\":true,\"restarting\":true}");
  delay(400);
  ESP.restart();
}

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Nova Clock</title>
<style>
body{font-family:system-ui,sans-serif;background:#0b0f14;color:#eef2f7;margin:0;padding:20px}
main{max-width:820px;margin:auto}.card{background:#121922;border:1px solid #2b3645;border-radius:16px;padding:16px;margin:12px 0}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:12px}
button,input,select{font:inherit;border-radius:9px;border:1px solid #38475a;background:#0b1118;color:#fff;padding:9px;margin:4px}
button{cursor:pointer}.clock{font-size:56px;font-weight:800;letter-spacing:2px}.status{color:#9daabd}.alarm{display:flex;gap:6px;align-items:center;flex-wrap:wrap}
</style>
</head>
<body><main>
<div class="card"><div class="status">NOVA CLOCK LOCAL CONTROL</div><h1 id="name">Nova Clock</h1><div id="clock" class="clock">--:--:--</div><div id="wifi" class="status">Connecting…</div></div>
<div class="card"><h2>Alarms</h2><div id="alarms"></div></div>
<div class="grid">
<div class="card"><h2>Display</h2><label>Style <select id="style"><option value="digital">Digital</option><option value="minimal">Minimal</option><option value="info">Info</option></select></label><br><label>Scale <select id="font"><option value="small">Small</option><option value="large">Large</option><option value="xl">XL</option></select></label><br><label>Brightness <input id="brightness" type="range" min="10" max="255"></label><br><button onclick="saveDisplay()">Save display</button></div>
<div class="card"><h2>Sound</h2><select id="sound"><option value="beep">Beep</option><option value="double">Double</option><option value="triple">Triple</option><option value="pulse">Pulse</option></select><input id="freq" type="number" min="200" max="5000"><button onclick="testSound()">Test</button></div>
</div>
<div class="card"><h2>Buttons</h2><button onclick="press(1)">SW1</button><button onclick="press(2)">SW2</button><button onclick="press(3)">SW3</button><button onclick="press(4)">SW4</button></div>
<div class="card"><h2>Network setup</h2><input id="ssid" placeholder="Wi-Fi SSID"><input id="pass" type="password" placeholder="Wi-Fi password"><button onclick="saveWiFi()">Save & restart</button></div>
</main>
<script>
const $=id=>document.getElementById(id);
async function api(path,opts={}){const r=await fetch(path,opts);return r.json()}
function esc(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
async function refresh(){
 const x=await api('/api/status');$('name').textContent=x.device;$('clock').textContent=x.time;
 $('wifi').textContent=x.wifi_connected?`Wi-Fi: ${x.ip} • http://${location.host}`:'Setup AP active • save Wi-Fi below';
 $('style').value=x.style;$('font').value=x.font;$('brightness').value=x.brightness;$('sound').value=x.sound;$('freq').value=x.frequency;
 let h='';x.alarms.forEach((a,i)=>{h+=`<div class="alarm"><input id="e${i}" type="checkbox" ${a.enabled?'checked':''}><input id="t${i}" type="time" value="${a.time}"><input id="l${i}" value="${esc(a.label)}"><button onclick="saveAlarm(${i})">Save</button></div>`});$('alarms').innerHTML=h;
}
async function saveAlarm(i){await api(`/api/alarm?id=${i}&enabled=${$('e'+i).checked?1:0}&time=${encodeURIComponent($('t'+i).value)}&label=${encodeURIComponent($('l'+i).value)}`,{method:'POST'});refresh()}
async function saveDisplay(){await api(`/api/settings?style=${$('style').value}&font=${$('font').value}&brightness=${$('brightness').value}`,{method:'POST'});refresh()}
async function testSound(){await api(`/api/sound?pattern=${$('sound').value}&freq=${$('freq').value}`,{method:'POST'})}
async function press(n){await api(`/api/button?n=${n}`,{method:'POST'});refresh()}
async function saveWiFi(){await api(`/api/wifi?ssid=${encodeURIComponent($('ssid').value)}&password=${encodeURIComponent($('pass').value)}`,{method:'POST'})}
refresh();setInterval(refresh,1000);
</script>
</body></html>
)HTML";

void setupWeb() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/status", HTTP_GET, apiStatus);
  server.on("/api/alarm", HTTP_POST, apiAlarm);
  server.on("/api/settings", HTTP_POST, apiSettings);
  server.on("/api/sound", HTTP_POST, apiSound);
  server.on("/api/button", HTTP_POST, apiButton);
  server.on("/api/wifi", HTTP_POST, apiWifi);
  server.begin();
}

// Wi-Fi
String savedSSID() {
  prefs.begin("wifi", true);
  String s = prefs.getString("ssid", "");
  prefs.end();
  return s;
}

String savedPassword() {
  prefs.begin("wifi", true);
  String p = prefs.getString("pass", "");
  prefs.end();
  return p;
}

void startSetupAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_NAME, AP_PASSWORD);
  Serial.printf("[WIFI] Setup AP: %s  IP: %s\n", AP_NAME, WiFi.softAPIP().toString().c_str());
}

bool connectToSavedWiFi() {
  const String ssid = savedSSID();
  const String pass = savedPassword();
  if (ssid.isEmpty()) return false;

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), pass.c_str());

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) return false;

  Serial.printf("[WIFI] Connected: %s\n", WiFi.localIP().toString().c_str());
  if (MDNS.begin(MDNS_NAME)) Serial.println("[mDNS] http://novaclock.local");
  return true;
}

void serviceWiFi() {
  if (WiFi.getMode() != WIFI_STA || savedSSID().isEmpty()) return;
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastWiFiRetry < 15000) return;
  lastWiFiRetry = millis();
  WiFi.reconnect();
}

// Setup / loop
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[NOVA] Nova Clock firmware V0.1 starting");

  loadSettings();

  pinMode(Pins::SW1, INPUT_PULLUP);
  pinMode(Pins::SW2, INPUT_PULLUP);
  pinMode(Pins::SW3, INPUT_PULLUP);
  pinMode(Pins::SW4, INPUT_PULLUP);

  ledcAttach(Pins::BUZZER, DEFAULT_BEEP_HZ, 8);
  ledcAttach(Pins::TFT_BL, 5000, 8);
  setBacklight();

  displayBegin();
  drawBootAnimation();

  startSoundPattern("beep", DEFAULT_BEEP_HZ);

  const bool connected = connectToSavedWiFi();
  if (!connected) startSetupAP();
  configureClock();
  setupWeb();
  updateDisplay();

  Serial.println("[BOOT] Ready");
  Serial.println("[WEB] Use http://novaclock.local when connected to Wi-Fi");
  Serial.println("[WEB] First setup AP: NovaClock-Setup / novaclock");
}

void loop() {
  server.handleClient();
  serviceButtons();
  serviceSound();
  serviceAlarms();
  serviceWiFi();

  if (millis() - lastDisplayDraw >= DISPLAY_REFRESH_MS) updateDisplay();
  delay(2);
}
