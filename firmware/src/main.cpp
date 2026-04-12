#include <Config.h>
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <AutoConnect.h>
#include <WebServer.h>
#include <NTPClient.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>

#include <Display.h>
#include <OpenTransportDataSwiss.h>

// --- DST HELPERS (Central Europe: CET=UTC+1, CEST=UTC+2) ---

static int _dow(int y, int m, int d) {
    static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (m < 3) y--;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

static int _lastSun(int y, int m) {
    int days = (m==1||m==3||m==5||m==7||m==8||m==10||m==12) ? 31 :
               (m==4||m==6||m==9||m==11) ? 30 :
               (y%4==0 && (y%100!=0 || y%400==0)) ? 29 : 28;
    return days - _dow(y, m, days);
}

static bool _isCEST(int y, int m, int d, int h) {
    if (m < 3 || m > 10) return false;
    if (m > 3 && m < 10) return true;
    int b = _lastSun(y, m);
    if (m == 3) return (d > b || (d == b && h >= 1));
    return (d < b || (d == b && h < 1));
}

// --- GLOBALS ---

int timeOffset = 3600;
const char* timeServer = TIME_SERVER;
const int BUTTON_PIN = 0;

// Runtime config (loaded from NVS on boot)
Preferences prefs;
String station1Id    = "8591324";
String station1Name  = "Roeslistrasse";
String station2Id    = "8591237";
String station2Name  = "Kronenstrasse";
int    nightStart    = 22;
int    nightEnd      = 6;
int    cfgBrightness = -1;  // -1 = auto/sensor
String cfgDirection  = "A"; // A=both, H=outbound, R=inbound

bool showRoesli    = true;
bool nightMode     = false;
bool displayOff    = false;
bool clockMode     = false;
bool manualOverride = false;
bool clkShowDate   = true;
bool clkShowWx     = true;
bool clkShowUv     = true;
bool clkShowRain   = true;
int  lastAutoHour   = -1;
int  sensorValue;
unsigned long lastTick = 0;

// Weather (written by core 0 task, read by core 1 loop)
volatile float wxTemp      = 0.0f;
volatile bool  wxValid     = false;
volatile char  wxDescBuf[16] = "";
volatile bool  wxRainToday = false;
volatile int   wxRainProb  = 0;
volatile float wxUvMax     = -1.0f;
volatile float wxHourlyRain[24] = {};

// Departure fetch task
SemaphoreHandle_t deptMutex  = NULL;
TaskHandle_t      deptTaskHandle = NULL;
volatile bool     newDeptData   = false;
volatile int      deptLastCode  = 0;
char              deptErrBuf[64] = "";

// Button state machine
enum BtnState { BTN_IDLE, BTN_PRESSED, BTN_WAIT_DOUBLE };
BtnState btnState = BTN_IDLE;
unsigned long btnPressStart  = 0;
unsigned long btnReleaseTime = 0;
bool btnPrev = false;

const unsigned long LONG_PRESS_MS   = 800;
const unsigned long DOUBLE_PRESS_MS = 400;

OpenTransportDataSwiss* api = nullptr;

Display display;
WebServer Server;
AutoConnect Portal(Server);
AutoConnectConfig Config;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, timeServer, timeOffset, 60000);

// --- DST: compute correct UTC offset for current NTP time ---

int computeTimeOffset() {
    unsigned long utc = (unsigned long)timeClient.getEpochTime() - (unsigned long)timeOffset;
    int h    = (utc / 3600) % 24;
    long days = utc / 86400;
    int y = 1970;
    while (true) {
        int diy = (y%4==0 && (y%100!=0 || y%400==0)) ? 366 : 365;
        if (days < diy) break;
        days -= diy; y++;
    }
    static const int md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap = (y%4==0 && (y%100!=0 || y%400==0));
    int m = 0;
    for (; m < 12; m++) {
        int dim = md[m] + (m==1 && leap ? 1 : 0);
        if (days < dim) break;
        days -= dim;
    }
    return _isCEST(y, m+1, (int)days+1, h) ? 7200 : 3600;
}

// --- NVS CONFIG ---

void loadConfig() {
    prefs.begin("vbz", true);
    station1Id    = prefs.getString("s1id",    "8591324");
    station1Name  = prefs.getString("s1name",  "Roeslistrasse");
    station2Id    = prefs.getString("s2id",    "8591237");
    station2Name  = prefs.getString("s2name",  "Kronenstrasse");
    nightStart    = prefs.getInt("ns",    22);
    nightEnd      = prefs.getInt("ne",     6);
    cfgBrightness = prefs.getInt("bright", -1);
    cfgDirection  = prefs.getString("dir", "A");
    clkShowDate   = prefs.getBool("clkdate", true);
    clkShowWx     = prefs.getBool("clkwx",   true);
    clkShowUv     = prefs.getBool("clkuv",   true);
    clkShowRain   = prefs.getBool("clkrain", true);
    prefs.end();
}

void saveConfig() {
    prefs.begin("vbz", false);
    prefs.putString("s1id",    station1Id);
    prefs.putString("s1name",  station1Name);
    prefs.putString("s2id",    station2Id);
    prefs.putString("s2name",  station2Name);
    prefs.putInt("ns",    nightStart);
    prefs.putInt("ne",    nightEnd);
    prefs.putInt("bright", cfgBrightness);
    prefs.putString("dir", cfgDirection);
    prefs.putBool("clkdate", clkShowDate);
    prefs.putBool("clkwx",   clkShowWx);
    prefs.putBool("clkuv",   clkShowUv);
    prefs.putBool("clkrain", clkShowRain);
    prefs.end();
}

// --- WEATHER ---

static String wxCodeStr(int code) {
    if (code == 0)   return "Clear";
    if (code <= 3)   return "Cloudy";
    if (code <= 48)  return "Fog";
    if (code <= 67)  return "Rain";
    if (code <= 77)  return "Snow";
    if (code <= 82)  return "Showers";
    return                  "Storm";
}

// Single HTTP task — weather + departures run sequentially, no mutex needed for HTTP
void httpTask(void*) {
    unsigned long lastWeatherMs = 0;
    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {

            // --- Weather (every 10 min) ---
            unsigned long now = millis();
            if (!wxValid || (now - lastWeatherMs >= 10UL * 60 * 1000)) {
                WiFiClientSecure *wc = new WiFiClientSecure;
                if (wc) {
                    wc->setInsecure();
                    HTTPClient wh;
                    wh.setTimeout(8000);
                    // --- Single call: current conditions + daily summary + hourly precipitation ---
                    String wurl = String("https://api.open-meteo.com/v1/forecast"
                        "?latitude=" WEATHER_LAT "&longitude=" WEATHER_LON
                        "&current=temperature_2m,weather_code"
                        "&hourly=precipitation"
                        "&daily=precipitation_probability_max,uv_index_max"
                        "&forecast_days=1"
                        "&timezone=auto");
                    bool gotWeather = false;
                    if (wh.begin(*wc, wurl) && wh.GET() == 200) {
                        StaticJsonDocument<384> wfilter;
                        wfilter["current"]["temperature_2m"] = true;
                        wfilter["current"]["weather_code"]   = true;
                        wfilter["hourly"]["precipitation"][0] = true;
                        wfilter["daily"]["precipitation_probability_max"][0] = true;
                        wfilter["daily"]["uv_index_max"][0]  = true;
                        StaticJsonDocument<1024> wdoc;
                        String body = wh.getString();
                        if (deserializeJson(wdoc, body,
                                DeserializationOption::Filter(wfilter)) == DeserializationError::Ok) {
                            wxTemp = wdoc["current"]["temperature_2m"].as<float>();
                            String d = wxCodeStr(wdoc["current"]["weather_code"].as<int>());
                            strncpy((char*)wxDescBuf, d.c_str(), sizeof(wxDescBuf) - 1);
                            ((char*)wxDescBuf)[sizeof(wxDescBuf) - 1] = '\0';
                            int rainProb = wdoc["daily"]["precipitation_probability_max"][0] | 0;
                            wxRainToday = (rainProb >= 40);
                            wxRainProb  = rainProb;
                            wxUvMax     = wdoc["daily"]["uv_index_max"][0] | -1.0f;
                            JsonArray hp = wdoc["hourly"]["precipitation"];
                            for (int i = 0; i < 24 && i < (int)hp.size(); i++)
                                wxHourlyRain[i] = hp[i].as<float>();
                            gotWeather = true;
                            Serial.printf("Weather: %.1fC %s rain=%d%% uv=%.1f hp[0]=%.2f hp[12]=%.2f\n",
                                wxTemp, d.c_str(), rainProb, (float)wxUvMax,
                                (float)wxHourlyRain[0], (float)wxHourlyRain[12]);
                        } else Serial.println("Weather: parse error");
                    } else Serial.printf("Weather: request failed, heap=%d\n", ESP.getMaxAllocHeap());
                    wh.end();

                    if (gotWeather) wxValid = true;
                    delete wc;
                } else Serial.printf("Weather: alloc failed, heap=%d\n", ESP.getMaxAllocHeap());
                lastWeatherMs = millis();
            }

            // --- Departures ---
            // Free old api FIRST to maximise contiguous DRAM available for TLS
            xSemaphoreTake(deptMutex, portMAX_DELAY);
            if (api != nullptr) { api->~OpenTransportDataSwiss(); free(api); api = nullptr; }
            xSemaphoreGive(deptMutex);

            // Allocate api object in PSRAM so the 4KB JSON doc doesn't eat DRAM needed by TLS
            String stationId = showRoesli ? station1Id : station2Id;
            void* apiBuf = heap_caps_malloc(sizeof(OpenTransportDataSwiss), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!apiBuf) apiBuf = malloc(sizeof(OpenTransportDataSwiss));
            Serial.printf("API buf: %p  dram_free=%d\n", apiBuf, (int)ESP.getMaxAllocHeap());
            if (!apiBuf) { ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(32000)); continue; }
            OpenTransportDataSwiss* newApi = new(apiBuf) OpenTransportDataSwiss(
                stationId, cfgDirection, OPEN_DATA_URL, OPEN_DATA_API_KEY, OPEN_DATA_RESULTS);
            int code = newApi->getWebData(timeClient);

            char errBuf[64] = "";
            if (code != 0) strncpy(errBuf, newApi->httpLastError.c_str(), sizeof(errBuf) - 1);

            xSemaphoreTake(deptMutex, portMAX_DELAY);
            api         = newApi;
            deptLastCode = code;
            strncpy(deptErrBuf, errBuf, sizeof(deptErrBuf) - 1);
            newDeptData = true;
            xSemaphoreGive(deptMutex);

            if (code == 0) Serial.println("Dept: OK");
            else Serial.printf("Dept: error %d %s\n", code, errBuf);
        }
        // Wait 32s, or wake immediately on station/direction/config change
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(32000));
    }
}

// --- CLOCK HELPERS ---

String clockDate() {
    String fd = timeClient.getFormattedDate(); // "YYYY-MM-DDTHH:MM:SSZ"
    return fd.substring(8, 10) + "." + fd.substring(5, 7) + "." + fd.substring(0, 4);
}

void showClockNow() {
    String wx = "";
    if (!wxValid)
        wx = "heap:" + String(ESP.getMaxAllocHeap() / 1024) + "K";
    else if (clkShowWx)
        wx = String((int)round(wxTemp)) + "C  " + String((const char*)wxDescBuf);

    float uvToShow = (clkShowWx && clkShowUv) ? (float)wxUvMax : -1.0f;

    float hourlyRainCopy[24];
    const float* rainPtr = nullptr;
    if (wxValid && clkShowRain) {
        for (int i = 0; i < 24; i++) hourlyRainCopy[i] = wxHourlyRain[i];
        rainPtr = hourlyRainCopy;
    }

    display.showClock(timeClient.getHours(), timeClient.getMinutes(),
                      timeClient.getSeconds(), clkShowDate ? clockDate() : "", wx,
                      wxRainToday, uvToShow, rainPtr);
}

// --- RENDER HELPER ---

void reRender() {
    if (displayOff) {
        display.turnOff();
    } else if (clockMode) {
        showClockNow();
    } else if (xSemaphoreTake(deptMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        if (api != nullptr) {
            if (deptLastCode == 0) display.printLines(api->doc.as<JsonArray>());
            else display.printError("API Error: " + String(deptLastCode) + "\n" + String(deptErrBuf) + "\nheap:" + String(ESP.getMaxAllocHeap() / 1024) + "K");
        }
        xSemaphoreGive(deptMutex);
    }
}

// --- XML HELPER ---

static String _xv(const String& doc, const char* open, const char* close) {
    int s = doc.indexOf(open);
    if (s < 0) return "";
    s += strlen(open);
    int e = doc.indexOf(close, s);
    if (e < 0) return "";
    return doc.substring(s, e);
}

// --- STATUS ENDPOINT ---

void handleStatus() {
    String s = "wxValid: " + String(wxValid ? "true" : "false") + "\n";
    s += "temp:    " + String(wxTemp) + " C\n";
    s += "desc:    " + String((const char*)wxDescBuf) + "\n";
    s += "uvMax:   " + String(wxUvMax) + "\n";
    s += "rainToday: " + String(wxRainToday ? "true" : "false") + " (" + String(wxRainProb) + "%)\n";
    s += "\nhourly precipitation (mm/h):\n";
    for (int i = 0; i < 24; i++) {
        char line[32];
        snprintf(line, sizeof(line), "  %02d:00  %.2f\n", i, (float)wxHourlyRain[i]);
        s += line;
    }
    Server.send(200, "text/plain", s);
}

// --- DEPARTURES ENDPOINT ---

void handleDepartures() {
    if (xSemaphoreTake(deptMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        Server.send(200, "application/json", "[]"); return;
    }
    if (api == nullptr) { xSemaphoreGive(deptMutex); Server.send(200, "application/json", "[]"); return; }
    String json;
    serializeJson(api->doc, json);
    xSemaphoreGive(deptMutex);
    Server.send(200, "application/json", json);
}

// --- STATION SEARCH ENDPOINT ---

void handleSearch() {
    String query = Server.arg("q");
    if (!query.length()) { Server.send(200, "application/json", "[]"); return; }

    WiFiClientSecure* client = new WiFiClientSecure;
    if (!client) { Server.send(200, "application/json", "[]"); return; }
    client->setInsecure();
    HTTPClient https;
    https.setTimeout(8000);

    String auth = String(OPEN_DATA_API_KEY);
    if (!auth.startsWith("Bearer ")) auth = "Bearer " + auth;

    String ts = timeClient.getFormattedDate();
    String body =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<OJP xmlns=\"http://www.vdv.de/ojp\" xmlns:siri=\"http://www.siri.org.uk/siri\" version=\"2.0\">"
        "<OJPRequest><siri:ServiceRequest>"
        "<siri:ServiceRequestContext><siri:Language>de</siri:Language></siri:ServiceRequestContext>"
        "<siri:RequestTimestamp>" + ts + "</siri:RequestTimestamp>"
        "<siri:RequestorRef>tramdisplay</siri:RequestorRef>"
        "<OJPLocationInformationRequest>"
        "<siri:RequestTimestamp>" + ts + "</siri:RequestTimestamp>"
        "<siri:MessageIdentifier>LIR-1</siri:MessageIdentifier>"
        "<InitialInput><LocationName>" + query + "</LocationName></InitialInput>"
        "<Restrictions><Type>stop</Type><NumberOfResults>6</NumberOfResults></Restrictions>"
        "</OJPLocationInformationRequest>"
        "</siri:ServiceRequest></OJPRequest></OJP>";

    String json = "[]";
    if (https.begin(*client, OPEN_DATA_URL)) {
        https.addHeader("Authorization", auth);
        https.addHeader("Content-Type", "application/xml");
        if (https.POST(body) == 200) {
            String xml = https.getString();
            json = "[";
            bool first = true;
            while (true) {
                int end = xml.indexOf("</ojp:Location>");
                if (end < 0) end = xml.indexOf("</Location>");
                if (end < 0) break;
                String block = xml.substring(0, end);
                xml = xml.substring(end + 15);

                String ref = _xv(block, "<siri:StopPointRef>", "</siri:StopPointRef>");
                if (!ref.length()) ref = _xv(block, "<StopPointRef>", "</StopPointRef>");
                String name = _xv(block, "<ojp:Text>", "</ojp:Text>");
                if (!name.length()) name = _xv(block, "<Text>", "</Text>");
                name.replace("\"", "\\\"");

                if (ref.length() && name.length()) {
                    if (!first) json += ",";
                    json += "{\"id\":\"" + ref + "\",\"name\":\"" + name + "\"}";
                    first = false;
                }
            }
            json += "]";
        }
        https.end();
    }
    delete client;
    Server.send(200, "application/json", json);
}

// --- WEB CONFIG HANDLERS ---

void handleCmd() {
    String action = Server.arg("action");
    if (action == "station") {
        showRoesli = !showRoesli;
        display.connectingMsg();
        if (deptTaskHandle) xTaskNotifyGive(deptTaskHandle);
        Serial.println("Web: Switch -> " + (showRoesli ? station1Name : station2Name));
    } else if (action == "night") {
        manualOverride = true;
        nightMode = !nightMode;
        display.setNightMode(nightMode);
        reRender();
        Serial.println(nightMode ? "Web: Night ON (manual)" : "Web: Night OFF (manual)");
    } else if (action == "display") {
        displayOff = !displayOff;
        reRender();
        Serial.println(displayOff ? "Web: Display OFF" : "Web: Display ON");
    } else if (action == "clock") {
        clockMode = !clockMode;
        reRender();
        Serial.println(clockMode ? "Web: Clock ON" : "Web: Clock OFF");
    } else if (action == "direction") {
        if      (cfgDirection == "A") cfgDirection = "H";
        else if (cfgDirection == "H") cfgDirection = "R";
        else                          cfgDirection = "A";
        // Persist and force immediate re-fetch
        prefs.begin("vbz", false);
        prefs.putString("dir", cfgDirection);
        prefs.end();
        if (deptTaskHandle) xTaskNotifyGive(deptTaskHandle);
        Serial.println("Web: Direction -> " + cfgDirection);
    } else if (action == "test") {
        display.testPattern();
        reRender();
        Serial.println("Web: Test pattern done");
    } else if (action == "clktest") {
        for (int uv = 10; uv >= 0; uv--) {
            display.showClock(timeClient.getHours(), timeClient.getMinutes(),
                              timeClient.getSeconds(), clockDate(),
                              "25C  Clear", false, (float)uv, nullptr);
            delay(600);
        }
        reRender();
        Serial.println("Web: Clock UV test done");
    } else if (action == "clkraintest") {
        float rainBuf[24];
        // Gaussian peak sweeps left to right across the 24 bars
        for (int f = -4; f <= 27; f++) {
            for (int i = 0; i < 24; i++) {
                float d = (float)(i - f);
                rainBuf[i] = 4.8f * expf(-0.5f * d * d / 5.0f);
                if (rainBuf[i] < 0.0f) rainBuf[i] = 0.0f;
            }
            display.showClock(timeClient.getHours(), timeClient.getMinutes(),
                              timeClient.getSeconds(), clockDate(),
                              "25C  Clear", false, 5.0f, rainBuf);
            delay(100);
        }
        reRender();
        Serial.println("Web: Clock rain test done");
    } else if (action == "clkdate") {
        clkShowDate = !clkShowDate;
        prefs.begin("vbz", false); prefs.putBool("clkdate", clkShowDate); prefs.end();
        reRender();
    } else if (action == "clkwx") {
        clkShowWx = !clkShowWx;
        prefs.begin("vbz", false); prefs.putBool("clkwx", clkShowWx); prefs.end();
        reRender();
    } else if (action == "clkuv") {
        clkShowUv = !clkShowUv;
        prefs.begin("vbz", false); prefs.putBool("clkuv", clkShowUv); prefs.end();
        reRender();
    } else if (action == "clkrain") {
        clkShowRain = !clkShowRain;
        prefs.begin("vbz", false); prefs.putBool("clkrain", clkShowRain); prefs.end();
        reRender();
    }
    String json = String("{\"station\":") + (showRoesli ? "1" : "2")
                + ",\"night\":"   + (nightMode  ? "true" : "false")
                + ",\"off\":"     + (displayOff ? "true" : "false")
                + ",\"clock\":"   + (clockMode  ? "true" : "false")
                + ",\"dir\":\""   + cfgDirection + "\""
                + ",\"s1name\":\"" + station1Name + "\""
                + ",\"s2name\":\"" + station2Name + "\""
                + ",\"clkdate\":" + (clkShowDate ? "true" : "false")
                + ",\"clkwx\":"   + (clkShowWx   ? "true" : "false")
                + ",\"clkuv\":"   + (clkShowUv   ? "true" : "false")
                + ",\"clkrain\":" + (clkShowRain  ? "true" : "false")
                + "}";
    Server.send(200, "application/json", json);
}

void handleConfigGet() {
    bool saved = Server.hasArg("saved");

    String activeName = showRoesli ? station1Name : station2Name;
    String nightCls   = nightMode  ? " act-b" : "";
    String clockCls   = clockMode  ? " act-b" : "";
    String offCls     = displayOff ? " act-r" : "";
    String nightVal   = nightMode  ? "Night"  : "Day";
    String clockVal   = clockMode  ? "On"     : "Off";
    String dispVal    = displayOff ? "Off"    : "On";
    String cdCls      = clkShowDate ? " act-b" : "";
    String cwCls      = clkShowWx   ? " act-b" : "";
    String cuCls      = clkShowUv   ? " act-b" : "";
    String crCls      = clkShowRain ? " act-b" : "";
    String cdVal      = clkShowDate ? "On" : "Off";
    String cwVal      = clkShowWx   ? "On" : "Off";
    String cuVal      = clkShowUv   ? "On" : "Off";
    String crVal      = clkShowRain ? "On" : "Off";
    String dirVal     = (cfgDirection == "H") ? "Outbound" :
                        (cfgDirection == "R") ? "Inbound"  : "Both";

    String html =
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>VBZ Display</title><style>"
        ":root{--bg:#111;--sf:#1e1e1e;--bd:#2a2a2a;--ac:#ffc800;--tx:#eee;--tx2:#888;--r:12px}"
        "*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
        "body{margin:0;font-family:sans-serif;background:var(--bg);color:var(--tx)}"
        ".bar{position:sticky;top:0;z-index:10;background:#161616;"
        "border-bottom:1px solid var(--bd);display:flex;align-items:center;gap:8px;padding:13px 16px}"
        ".bar h1{margin:0;font-size:1.05em;font-weight:700;color:var(--ac);flex:1;letter-spacing:.02em}"
        ".dot{width:7px;height:7px;border-radius:50%;background:#4caf50;flex-shrink:0}"
        ".wrap{padding:12px 14px;max-width:520px;margin:0 auto}"
        ".card{background:var(--sf);border:1px solid var(--bd);border-radius:var(--r);"
        "padding:14px;margin-bottom:12px}"
        ".ctit{font-size:.65em;text-transform:uppercase;letter-spacing:.1em;"
        "color:var(--tx2);margin:0 0 12px;font-weight:600}"
        ".grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}"
        ".btn{min-height:72px;border:none;border-radius:10px;cursor:pointer;"
        "background:#242424;color:var(--tx);width:100%;text-align:left;"
        "padding:12px 14px;display:flex;flex-direction:column;justify-content:space-between;"
        "font-family:inherit;transition:transform .1s,opacity .1s;-webkit-user-select:none}"
        ".btn:active{transform:scale(.96);opacity:.85}"
        ".blbl{font-size:.6em;text-transform:uppercase;letter-spacing:.09em;color:var(--tx2);font-weight:600}"
        ".bval{font-size:1.05em;font-weight:700;color:var(--tx);"
        "white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
        ".act-b{background:#1c3f7a;box-shadow:inset 0 0 0 1.5px #4a7fc4}"
        ".act-b .blbl{color:#7aaee8}"
        ".act-b .bval{color:#c8ddfa}"
        ".act-r{background:#4a1414;box-shadow:inset 0 0 0 1.5px #b03030}"
        ".act-r .blbl{color:#cc7070}"
        ".act-r .bval{color:#f5a0a0}"
        ".wide{grid-column:span 2}"
        ".tbtn{width:100%;margin-top:8px;padding:14px;background:transparent;"
        "color:var(--tx2);border:1px solid var(--bd);border-radius:10px;"
        "font-family:inherit;font-size:.9em;cursor:pointer;transition:background .15s,color .15s}"
        ".tbtn:active{background:#2a2a2a;color:var(--tx)}"
        ".sbtn{width:100%;padding:14px;background:var(--ac);color:#000;border:none;"
        "border-radius:10px;font-weight:700;font-size:1em;cursor:pointer;margin-top:12px}"
        ".sbtn:active{background:#e6b400}"
        ".srch{padding:10px 14px;background:#2a2a2a;color:var(--tx);border:1px solid var(--bd);"
        "border-radius:8px;cursor:pointer;white-space:nowrap;font-family:inherit;font-size:.9em}"
        "input{width:100%;padding:10px;background:#161616;border:1px solid var(--bd);"
        "color:var(--tx);border-radius:8px;font-size:.95em;margin-top:4px}"
        "label{font-size:.78em;color:var(--tx2);margin-top:12px;display:block}"
        "details summary{cursor:pointer;font-size:.85em;color:var(--tx2);"
        "user-select:none;padding:2px 0;list-style:none;display:flex;align-items:center;gap:6px}"
        "details summary::-webkit-details-marker{display:none}"
        "details summary::before{content:'\\25B6';font-size:.6em;transition:.2s}"
        "details[open] summary::before{content:'\\25BC'}"
        ".ok{background:#1a3a1a;border:1px solid #4caf50;color:#4caf50;"
        "padding:10px;border-radius:8px;margin-bottom:12px;text-align:center;font-size:.9em}"
        ".ngrid{display:grid;grid-template-columns:1fr 1fr;gap:8px}"
        "ul{list-style:none;padding:0;margin:6px 0 0}"
        "ul li{padding:9px 11px;background:#242424;margin:3px 0;"
        "border-radius:8px;cursor:pointer;font-size:.9em}"
        "ul li:active{background:#333}"
        "</style></head><body>"
        "<div class='bar'><h1>VBZ Display</h1><div class='dot'></div></div>"
        "<div class='wrap'>";

    if (saved) html += "<div class='ok'>Settings saved and applied.</div>";

    // Controls card
    html += "<div class='card'><p class='ctit'>Controls</p><div class='grid'>";
    html += "<button class='btn' id='b-st' onclick='cmd(\"station\")'>"
            "<span class='blbl'>Station</span><span class='bval'>" + activeName + "</span></button>";
    html += "<button class='btn" + nightCls + "' id='b-nt' onclick='cmd(\"night\")'>"
            "<span class='blbl'>Night Mode</span><span class='bval'>" + nightVal + "</span></button>";
    html += "<button class='btn" + clockCls + "' id='b-ck' onclick='cmd(\"clock\")'>"
            "<span class='blbl'>Clock</span><span class='bval'>" + clockVal + "</span></button>";
    html += "<button class='btn' id='b-di' onclick='cmd(\"direction\")'>"
            "<span class='blbl'>Direction</span><span class='bval'>" + dirVal + "</span></button>";
    html += "<button class='btn wide" + offCls + "' id='b-dp' onclick='cmd(\"display\")'>"
            "<span class='blbl'>Display</span><span class='bval'>" + dispVal + "</span></button>";
    html +=
        "</div>"
        "<button class='tbtn' id='b-ts' onclick='cmdTest()'>Run Test Pattern</button>"
        "</div>";

    // Clock display card
    html += "<div class='card'><p class='ctit'>Clock display</p><div class='grid'>";
    html += "<button class='btn" + cdCls + "' id='b-cd' onclick='cmd(\"clkdate\")'>"
            "<span class='blbl'>Date</span><span class='bval'>" + cdVal + "</span></button>";
    html += "<button class='btn" + cwCls + "' id='b-cw' onclick='cmd(\"clkwx\")'>"
            "<span class='blbl'>Weather</span><span class='bval'>" + cwVal + "</span></button>";
    html += "<button class='btn" + cuCls + "' id='b-cu' onclick='cmd(\"clkuv\")'"
            + (clkShowWx ? "" : " style='opacity:0.35;pointer-events:none'") + ">"
            "<span class='blbl'>UV Index</span><span class='bval'>" + cuVal + "</span></button>";
    html += "<button class='btn" + crCls + "' id='b-cr' onclick='cmd(\"clkrain\")'>"
            "<span class='blbl'>Rain chart</span><span class='bval'>" + crVal + "</span></button>";
    html += "</div>"
            "<button class='tbtn' id='b-ct' onclick='cmdClkTest()'>Test Weather &amp; UV</button>"
            "<button class='tbtn' id='b-crt' onclick='cmdRainTest()' style='margin-top:6px'>Test Rain wave</button>"
            "</div>";

    // Settings card (collapsible)
    html +=
        "<div class='card'><details><summary>Settings</summary>"
        "<form method='POST' action='/config' style='margin-top:14px'>"
        "<label>Station 1 ID</label>";
    html += "<input id='s1id' name='s1id' value='" + station1Id + "' required>";
    html +=
        "<label>Station 1 Name</label>"
        "<div style='display:flex;gap:8px;margin-top:4px'>";
    html += "<input id='s1name' name='s1name' value='" + station1Name + "' style='margin:0;flex:1'>";
    html +=
        "<button type='button' class='srch' onclick='search(1)'>Search</button>"
        "</div><div id='s1results'></div>"
        "<label style='margin-top:16px'>Station 2 ID</label>";
    html += "<input id='s2id' name='s2id' value='" + station2Id + "' required>";
    html +=
        "<label>Station 2 Name</label>"
        "<div style='display:flex;gap:8px;margin-top:4px'>";
    html += "<input id='s2name' name='s2name' value='" + station2Name + "' style='margin:0;flex:1'>";
    html +=
        "<button type='button' class='srch' onclick='search(2)'>Search</button>"
        "</div><div id='s2results'></div>"
        "<label style='margin-top:16px'>Night hours</label>"
        "<div class='ngrid'>"
        "<div><label>Start (0-23)</label>";
    html += "<input name='ns' type='number' min='0' max='23' value='" + String(nightStart) + "'>";
    html += "</div><div><label>End (0-23)</label>";
    html += "<input name='ne' type='number' min='0' max='23' value='" + String(nightEnd) + "'>";
    html += "</div></div><label style='margin-top:16px'>Brightness (0-255, -1=auto)</label>";
    html += "<input name='bright' type='number' min='-1' max='255' value='" + String(cfgBrightness) + "'>";
    html +=
        "<button type='submit' class='sbtn'>Save &amp; Apply</button>"
        "</form></details></div></div>";

    // JavaScript
    html +=
        "<script>"
        "let _sr=[];"
        "function sb(id,cls,val){"
          "const b=document.getElementById(id);"
          "const w=b.classList.contains('wide');"
          "b.className='btn'+(w?' wide':'')+(cls?' '+cls:'');"
          "b.querySelector('.bval').textContent=val;"
        "}"
        "async function cmd(a){"
          "const r=await fetch('/cmd?action='+a);"
          "const s=await r.json();"
          "sb('b-st','',s.station===1?s.s1name:s.s2name);"
          "sb('b-nt',s.night?'act-b':'',s.night?'Night':'Day');"
          "sb('b-ck',s.clock?'act-b':'',s.clock?'On':'Off');"
          "sb('b-dp',s.off?'act-r':'',s.off?'Off':'On');"
          "sb('b-di','',s.dir==='H'?'Outbound':s.dir==='R'?'Inbound':'Both');"
          "sb('b-cd',s.clkdate?'act-b':'',s.clkdate?'On':'Off');"
          "sb('b-cw',s.clkwx?'act-b':'',s.clkwx?'On':'Off');"
          "sb('b-cu',s.clkuv?'act-b':'',s.clkuv?'On':'Off');"
          "sb('b-cr',s.clkrain?'act-b':'',s.clkrain?'On':'Off');"
          "const cu=document.getElementById('b-cu');"
          "cu.style.opacity=s.clkwx?'1':'0.35';"
          "cu.style.pointerEvents=s.clkwx?'':'none';"
        "}"
        "async function cmdTest(){"
          "const b=document.getElementById('b-ts');"
          "b.textContent='Testing...';"
          "b.disabled=true;"
          "await fetch('/cmd?action=test');"
          "b.textContent='Run Test Pattern';"
          "b.disabled=false;"
        "}"
        "async function cmdClkTest(){"
          "const b=document.getElementById('b-ct');"
          "b.textContent='Running...';"
          "b.disabled=true;"
          "await fetch('/cmd?action=clktest');"
          "b.textContent='Test Weather & UV';"
          "b.disabled=false;"
        "}"
        "async function cmdRainTest(){"
          "const b=document.getElementById('b-crt');"
          "b.textContent='Running...';"
          "b.disabled=true;"
          "await fetch('/cmd?action=clkraintest');"
          "b.textContent='Test Rain wave';"
          "b.disabled=false;"
        "}"
        "async function search(n){"
          "const q=document.getElementById('s'+n+'name').value.trim();"
          "if(!q)return;"
          "try{"
            "const r=await fetch('/search?q='+encodeURIComponent(q));"
            "_sr=await r.json();"
            "const el=document.getElementById('s'+n+'results');"
            "if(!_sr.length){el.innerHTML='<p style=\"color:#555\">No results.</p>';return;}"
            "let h='<ul>';"
            "for(let i=0;i<_sr.length;i++){"
              "h+='<li onclick=\"pick('+n+','+i+')\">'+ _sr[i].name+'</li>';"
            "}"
            "h+='</ul>';"
            "el.innerHTML=h;"
          "}catch(e){}"
        "}"
        "function pick(n,i){"
          "document.getElementById('s'+n+'id').value=_sr[i].id;"
          "document.getElementById('s'+n+'name').value=_sr[i].name;"
          "document.getElementById('s'+n+'results').innerHTML='';"
        "}"
        "</script></body></html>";

    Server.send(200, "text/html", html);
}

void handleConfigPost() {
    if (Server.hasArg("s1id"))    station1Id    = Server.arg("s1id");
    if (Server.hasArg("s1name"))  station1Name  = Server.arg("s1name");
    if (Server.hasArg("s2id"))    station2Id    = Server.arg("s2id");
    if (Server.hasArg("s2name"))  station2Name  = Server.arg("s2name");
    if (Server.hasArg("ns"))      nightStart    = Server.arg("ns").toInt();
    if (Server.hasArg("ne"))      nightEnd      = Server.arg("ne").toInt();
    if (Server.hasArg("bright"))  cfgBrightness = Server.arg("bright").toInt();

    saveConfig();

    // Force immediate API refresh with new station if changed
    if (deptTaskHandle) xTaskNotifyGive(deptTaskHandle);

    Server.sendHeader("Location", "/config?saved=1");
    Server.send(303);
}

// --- AUTOCONNECT CALLBACK ---

bool startCP(IPAddress ip) {
    display.connectionMsg(AP_NAME, AP_PASSWORD);
    delay(5000);
    return true;
}

// --- SETUP ---

void setup() {
    delay(1000);
    Serial.begin(MONITOR_SPEED);
    Serial.setDebugOutput(true);
    delay(500);
    Serial.println("BOOT OK");

    loadConfig();

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    display.begin(
        R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN,
        A_PIN, B_PIN, C_PIN, D_PIN, E_PIN,
        LAT_PIN, OE_PIN, CLK_PIN,
        PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);

    display.showSplash();
    delay(3000);
    display.connectingMsg();

    Config.title       = "VBZ-Anzeige";
    Config.apid        = AP_NAME;
    Config.psk         = AP_PASSWORD;
    Config.autoReconnect = true;

    Server.on("/config",     HTTP_GET,  handleConfigGet);
    Server.on("/config",     HTTP_POST, handleConfigPost);
    Server.on("/cmd",        HTTP_GET,  handleCmd);
    Server.on("/status",     HTTP_GET,  handleStatus);
    Server.on("/departures", HTTP_GET,  handleDepartures);
    Server.on("/search",     HTTP_GET,  handleSearch);

    Portal.config(Config);
    Portal.onDetect(startCP);

    if (Portal.begin()) {
        Serial.println("WiFi connected");
        WiFi.setSleep(false);

        String configUrl = "http://" + WiFi.localIP().toString() + "/config";
        Serial.println("Config: " + configUrl);
        display.showIpAddress((WiFi.localIP().toString() + "/config").c_str());
        delay(2000);

        display.connectingMsg();

        timeClient.begin();
        timeClient.setTimeOffset(timeOffset);

        Serial.print("Waiting for NTP...");
        int retry = 0;
        while (!timeClient.update() && retry < 20) {
            timeClient.forceUpdate();
            delay(500);
            Serial.print(".");
            retry++;
        }
        Serial.println(" OK");

        timeOffset = computeTimeOffset();
        timeClient.setTimeOffset(timeOffset);
        Serial.printf("Time offset: %d (%s)\n", timeOffset, timeOffset == 7200 ? "CEST" : "CET");

        deptMutex = xSemaphoreCreateMutex();

        ArduinoOTA.setHostname("vbz-display");
        ArduinoOTA.setPassword("vbz1234");
        ArduinoOTA.onStart([]() {
            display.connectingMsg();
            Serial.println("OTA start");
        });
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            Serial.printf("OTA: %u%%\n", progress * 100 / total);
        });
        ArduinoOTA.onEnd([]() {
            Serial.println("OTA done");
        });
        ArduinoOTA.onError([](ota_error_t e) {
            Serial.printf("OTA error %u\n", e);
        });
        ArduinoOTA.begin();
        // mDNS is started by ArduinoOTA.begin() but is not needed — OTA uses a fixed IP.
        // Freeing it recovers ~10KB of DRAM for TLS.
        MDNS.end();
        Serial.printf("OTA ready, heap after MDNS.end=%d\n", ESP.getMaxAllocHeap());

        // Start httpTask AFTER all setup allocations are done so TLS sees a stable heap
        xTaskCreatePinnedToCore(httpTask, "http", 12288, NULL, 1, &deptTaskHandle, 0);
    }
}

// --- LOOP ---

void loop() {
    ArduinoOTA.handle();
    Portal.handleClient();
    timeClient.update();

    // --- 1. KNOPF-ERKENNUNG ---
    bool btnCurrent  = (digitalRead(BUTTON_PIN) == LOW);
    bool btnPressed  = (btnCurrent && !btnPrev);
    bool btnReleased = (!btnCurrent && btnPrev);
    btnPrev = btnCurrent;

    switch (btnState) {
        case BTN_IDLE:
            if (btnPressed) {
                btnPressStart = millis();
                btnState = BTN_PRESSED;
            }
            break;

        case BTN_PRESSED:
            if (btnReleased) {
                if (millis() - btnPressStart >= LONG_PRESS_MS) {
                    // Long press -> toggle display off/on
                    displayOff = !displayOff;
                    reRender();
                    Serial.println(displayOff ? "Display OFF" : "Display ON");
                    btnState = BTN_IDLE;
                } else {
                    btnReleaseTime = millis();
                    btnState = BTN_WAIT_DOUBLE;
                }
            }
            break;

        case BTN_WAIT_DOUBLE:
            if (btnPressed) {
                // Double press -> toggle night mode (manual override until next boundary)
                manualOverride = true;
                nightMode = !nightMode;
                display.setNightMode(nightMode);
                reRender();
                Serial.println(nightMode ? "Night mode ON (manual)" : "Night mode OFF (manual)");
                btnState = BTN_IDLE;
            } else if (millis() - btnReleaseTime >= DOUBLE_PRESS_MS) {
                // Single press -> switch station
                showRoesli = !showRoesli;
                display.connectingMsg();
                if (deptTaskHandle) xTaskNotifyGive(deptTaskHandle);
                Serial.println("Switch -> " + (showRoesli ? station1Name : station2Name));
                btnState = BTN_IDLE;
            }
            break;
    }

    // --- 2. DIE SEKUNDEN-SCHLEIFE ---
    if (millis() - lastTick >= 1000 || lastTick == 0) {
        lastTick = millis();

        // --- AUTO NIGHT MODE ---
        int currentHour = timeClient.getHours();
        if (currentHour != lastAutoHour) {
            lastAutoHour = currentHour;

            // Update DST offset on every hour change
            int newOffset = computeTimeOffset();
            if (newOffset != timeOffset) {
                timeOffset = newOffset;
                timeClient.setTimeOffset(timeOffset);
                Serial.printf("DST changed: %d (%s)\n", timeOffset, timeOffset == 7200 ? "CEST" : "CET");
            }

            // At night mode boundaries, clear manual override
            if (currentHour == nightStart || currentHour == nightEnd) {
                manualOverride = false;
                Serial.println("Auto night boundary, manual override cleared");
            }
        }

        if (!manualOverride) {
            bool schedNight = (nightStart != nightEnd) && (
                nightStart > nightEnd
                    ? (currentHour >= nightStart || currentHour < nightEnd)
                    : (currentHour >= nightStart && currentHour < nightEnd)
            );
            if (schedNight != nightMode) {
                nightMode = schedNight;
                display.setNightMode(nightMode);
                reRender();
                Serial.println(nightMode ? "Auto: Night mode ON" : "Auto: Night mode OFF");
            }
        }

        // --- CLOCK UPDATE ---
        if (clockMode && !displayOff) {
            showClockNow();
        }

        // Helligkeit
        if (displayOff) {
            display.displaySetBrightness(0);
        } else if (nightMode) {
            display.displaySetBrightness(25);
        } else if (cfgBrightness >= 0) {
            display.displaySetBrightness(cfgBrightness);
        } else {
            #if BRIGHTNESS_FIXED >= 0
                display.displaySetBrightness(BRIGHTNESS_FIXED);
            #else
                sensorValue = analogRead(A0);
                sensorValue = map(sensorValue, 0, 4095, 12, 255);
                display.displaySetBrightness(sensorValue);
            #endif
        }

        // --- 3. DEPARTURE DISPLAY ---
        if (newDeptData && !displayOff && !clockMode) {
            newDeptData = false;
            if (xSemaphoreTake(deptMutex, 0) == pdTRUE) {
                if (deptLastCode == 0) display.printLines(api->doc.as<JsonArray>());
                else display.printError("API Error: " + String(deptLastCode) + "\n" + String(deptErrBuf) + "\nheap:" + String(ESP.getMaxAllocHeap() / 1024) + "K");
                xSemaphoreGive(deptMutex);
            }
        }
    }
}
