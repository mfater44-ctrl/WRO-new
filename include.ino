#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>
// =====================================================
// WIFI
// =====================================================
const char* WIFI_SSID = "Heritage_Guard";
const char* WIFI_PASSWORD = "00000000";
// =====================================================
// PINS
// =====================================================
// Capacitive Moisture Sensor
#define MOISTURE_PIN 34
// BME280 - I2C
#define SDA_PIN 21
#define SCL_PIN 22
// =====================================================
// OPTIONAL BATTERY / UPS MONITOR
===================================================== //
// Keep false if the ESP32 is powered by USB / adapter only.
// To enable real battery voltage sensing later:
// 1) Use an external voltage divider.
// 2) Connect divider output to GPIO35.
// 3) Change BATTERY_MONITOR_ENABLED to true.
const bool BATTERY_MONITOR_ENABLED = false;
#define BATTERY_PIN 35
// Defaults for a single-cell Li-ion/LiPo battery.
// Adjust these values if your UPS/battery chemistry is different.
const float BATTERY_FULL_VOLTAGE = 4.20;
const float BATTERY_EMPTY_VOLTAGE = 3.20;
// Example 1:1 resistor divider => battery voltage is ADC voltage x 2.
const float BATTERY_DIVIDER_RATIO = 2.0;
===================================================== //
// WALL MOISTURE CALIBRATION
===================================================== //
- قيم مؤقتة //
لاحقا Calibration لازم نسوي
int DRY_VALUE = 2511;
int WET_VALUE = 1465;
===================================================== //
// WALL MOISTURE LIMITS
===================================================== //
const float WALL_WARNING = 40.0;
const float WALL_DANGER = 60.0;
// =====================================================
// AIR HUMIDITY LIMITS
// =====================================================
const float AIR_SAFE_MIN = 40.0;
const float AIR_SAFE_MAX = 60.0;
const float AIR_HIGH = 70.0;
const float AIR_DANGER = 75.0;
// =====================================================
// OBJECTS
// =====================================================
Adafruit_BME280 bme;
WebServer server(80);
// LCD 16x2 I2C - address confirmed by scanner
LiquidCrystal_I2C lcd(0x27, 16, 2);
Preferences prefs;
bool bmeFound = false;
// =====================================================
// SENSOR VALUES
===================================================== //
int moistureRaw = 0;
float wallMoisture = 0.0;
float temperature = 0.0;
float humidity = 0.0;
float pressure = 0.0;
float batteryVoltage = 0.0;
float batteryPercent = 0.0;
===================================================== //
// CHANGE MONITORING
===================================================== //
float oldTemperature = 0.0;
float oldPressure = 0.0;
float temperatureChange = 0.0;
float pressureChange = 0.0;
unsigned long lastEnvironmentReference = 0;
دقائق
5 نقارن كل //
const unsigned long ENVIRONMENT_WINDOW = 300000;
===================================================== //
// SENSOR READ TIMER
// =====================================================
unsigned long previousRead = 0;
const unsigned long READ_INTERVAL = 2000;
// =====================================================
// INCIDENT HISTORY / REAL-TIME CLOCK
// =====================================================
enum WallStateCode
{
STATE_SAFE = 0,
STATE_WARNING = 1,
STATE_DANGER = 2
};
struct WallEvent
{
bool valid = false;
uint8_t state = STATE_SAFE;
uint32_t startEpoch = 0;
uint32_t endEpoch = 0;
float startMoisture = 0;
float peakMoisture = 0;
float endMoisture = 0;
int startRaw = 0;
int peakRaw = 0;
int endRaw = 0;
float startTemperature = 0;
float startHumidity = 0;
float startPressure = 0;
float endTemperature = 0;
float endHumidity = 0;
float endPressure = 0;
uint8_t previousState = STATE_SAFE;
uint32_t previousDuration = 0;
};
const int MAX_RECENT_EVENTS = 8;
WallEvent recentEvents[MAX_RECENT_EVENTS];
int recentEventCount = 0;
int currentWallState = -1;
uint32_t currentStateStartEpoch = 0;
float currentStateStartMoisture = 0;
int currentStateStartRaw = 0;
float currentStatePeakMoisture = 0;
int currentStatePeakRaw = 0;
float currentStateStartTemperature = 0;
float currentStateStartHumidity = 0;
float currentStateStartPressure = 0;
uint8_t currentPreviousState = STATE_SAFE;
uint32_t currentPreviousDuration = 0;
bool timeReady = false;
unsigned long lastTimeSyncAttempt = 0;
String dailyDateKey = "";
uint16_t dailyDangerCount = 0;
uint16_t dailyWarningCount = 0;
uint32_t dailyDangerSeconds = 0;
uint32_t dailyWarningSeconds = 0;
uint32_t dailyLongestDanger = 0;
uint32_t dailyLongestWarning = 0;
float dailyHighestMoisture = 0;
// =====================================================
// AIR HUMIDITY INCIDENT HISTORY
// =====================================================
enum AirIncidentState
{
AIR_EVENT_NORMAL = 0,
AIR_EVENT_WARNING = 1, // 70% to <75%
AIR_EVENT_DANGER = 2 // >=75%
};
struct AirHumidityEvent
{
bool valid = false;
uint8_t state = AIR_EVENT_NORMAL;
uint32_t startEpoch = 0;
uint32_t endEpoch = 0;
float startHumidity = 0;
float peakHumidity = 0;
float endHumidity = 0;
float startTemperature = 0;
float startPressure = 0;
float endTemperature = 0;
float endPressure = 0;
uint8_t previousState = AIR_EVENT_NORMAL;
uint32_t previousDuration = 0;
};
AirHumidityEvent lastAirWarningEvent;
AirHumidityEvent lastAirDangerEvent;
int currentAirIncidentState = -1;
uint32_t currentAirStateStartEpoch = 0;
float currentAirStartHumidity = 0;
float currentAirPeakHumidity = 0;
float currentAirStartTemperature = 0;
float currentAirStartPressure = 0;
uint8_t currentAirPreviousState = AIR_EVENT_NORMAL;
uint32_t currentAirPreviousDuration = 0;
// =====================================================
// TREND HISTORY / PREDICTION
// =====================================================
struct TrendSample
{
uint32_t epoch = 0;
float wall = 0;
float temperature = 0;
float humidity = 0;
float pressure = 0;
};
const int MAX_TREND_SAMPLES = 432; // 72 hours at 10-minute intervals
const unsigned long TREND_INTERVAL = 600000; // 10 minutes
TrendSample trendSamples[MAX_TREND_SAMPLES];
int trendSampleCount = 0;
int trendWriteIndex = 0;
unsigned long lastTrendSampleMillis = 0;
// =====================================================
// WALL STATUS
// =====================================================
String getWallStatus(float value)
{
if (value < WALL_WARNING)
return "SAFE";
if (value < WALL_DANGER)
return "EARLY WARNING";
return "DANGER";
}
// =====================================================
// AIR HUMIDITY STATUS
// =====================================================
String getAirStatus(float value)
{
if (value >= AIR_SAFE_MIN && value <= AIR_SAFE_MAX)
return "SAFE";
if (value < AIR_SAFE_MIN)
return "DRY AIR";
if (value < AIR_HIGH)
return "MONITOR";
if (value < AIR_DANGER)
return "HIGH HUMIDITY";
return "DANGER";
}
// =====================================================
// TEMPERATURE STATUS
// =====================================================
String getTemperatureStatus()
{
float changeValue = abs(temperatureChange);
if (changeValue < 2.0)
return "STABLE";
if (changeValue < 4.0)
return "MONITOR";
return "RAPID CHANGE";
}
// =====================================================
// PRESSURE STATUS
// =====================================================
String getPressureStatus()
{
float changeValue = abs(pressureChange);
if (changeValue < 2.0)
return "STABLE";
if (changeValue < 5.0)
return "CHANGING";
return "LARGE CHANGE";
}
// =====================================================
// INCIDENT HISTORY FUNCTIONS
// =====================================================
String wallStateName(int state)
{
if (state == STATE_WARNING) return "EARLY WARNING";
if (state == STATE_DANGER) return "DANGER";
return "SAFE";
}
int wallStateFromMoisture(float value)
{
if (value < WALL_WARNING) return STATE_SAFE;
if (value < WALL_DANGER) return STATE_WARNING;
return STATE_DANGER;
}
uint32_t getEpochNow()
{
time_t now;
time(&now);
if (now < 1700000000)
return 0;
return (uint32_t)now;
}
String dateKeyFromEpoch(uint32_t epoch)
{
if (epoch == 0) return "";
time_t value = (time_t)epoch;
struct tm info;
localtime_r(&value, &info);
char buffer[16];
strftime(buffer, sizeof(buffer), "%Y-%m-%d", &info);
return String(buffer);
}
String eventToStorage(const WallEvent &e)
{
String s;
s.reserve(220);
s += String(e.valid ? 1 : 0) + "|";
s += String(e.state) + "|";
s += String(e.startEpoch) + "|";
s += String(e.endEpoch) + "|";
s += String(e.startMoisture, 2) + "|";
s += String(e.peakMoisture, 2) + "|";
s += String(e.endMoisture, 2) + "|";
s += String(e.startRaw) + "|";
s += String(e.peakRaw) + "|";
s += String(e.endRaw) + "|";
s += String(e.startTemperature, 2) + "|";
s += String(e.startHumidity, 2) + "|";
s += String(e.startPressure, 2) + "|";
s += String(e.endTemperature, 2) + "|";
s += String(e.endHumidity, 2) + "|";
s += String(e.endPressure, 2) + "|";
s += String(e.previousState) + "|";
s += String(e.previousDuration);
return s;
}
String storagePart(const String &source, int wanted)
{
int start = 0;
int part = 0;
for (int i = 0; i <= source.length(); i++)
{
if (i == source.length() || source.charAt(i) == '|')
{
if (part == wanted)
return source.substring(start, i);
part++;
start = i + 1;
}
}
return "";
}
WallEvent eventFromStorage(const String &s)
{
WallEvent e;
if (s.length() == 0)
return e;
e.valid = storagePart(s, 0).toInt() == 1;
e.state = storagePart(s, 1).toInt();
e.startEpoch = (uint32_t)strtoul(storagePart(s, 2).c_str(), nullptr, 10);
e.endEpoch = (uint32_t)strtoul(storagePart(s, 3).c_str(), nullptr, 10);
e.startMoisture = storagePart(s, 4).toFloat();
e.peakMoisture = storagePart(s, 5).toFloat();
e.endMoisture = storagePart(s, 6).toFloat();
e.startRaw = storagePart(s, 7).toInt();
e.peakRaw = storagePart(s, 8).toInt();
e.endRaw = storagePart(s, 9).toInt();
e.startTemperature = storagePart(s, 10).toFloat();
e.startHumidity = storagePart(s, 11).toFloat();
e.startPressure = storagePart(s, 12).toFloat();
e.endTemperature = storagePart(s, 13).toFloat();
e.endHumidity = storagePart(s, 14).toFloat();
e.endPressure = storagePart(s, 15).toFloat();
e.previousState = storagePart(s, 16).toInt();
e.previousDuration = (uint32_t)strtoul(storagePart(s, 17).c_str(), nullptr, 10);
return e;
}
void saveRecentEvents()
{
prefs.putInt("eventCount", recentEventCount);
for (int i = 0; i < MAX_RECENT_EVENTS; i++)
{
String key = "evt" + String(i);
if (i < recentEventCount)
prefs.putString(key.c_str(), eventToStorage(recentEvents[i]));
else
prefs.remove(key.c_str());
}
}
void loadRecentEvents()
{
recentEventCount = prefs.getInt("eventCount", 0);
if (recentEventCount < 0) recentEventCount = 0;
if (recentEventCount > MAX_RECENT_EVENTS)
recentEventCount = MAX_RECENT_EVENTS;
for (int i = 0; i < recentEventCount; i++)
{
String key = "evt" + String(i);
recentEvents[i] = eventFromStorage(
prefs.getString(key.c_str(), "")
);
}
}
void saveDailySummary()
{
prefs.putString("dayKey", dailyDateKey);
prefs.putUInt("dDanger", dailyDangerCount);
prefs.putUInt("dWarn", dailyWarningCount);
prefs.putULong("dDangerSec", dailyDangerSeconds);
prefs.putULong("dWarnSec", dailyWarningSeconds);
prefs.putULong("dLongDanger", dailyLongestDanger);
prefs.putULong("dLongWarn", dailyLongestWarning);
prefs.putFloat("dHigh", dailyHighestMoisture);
}
void loadDailySummary()
{
dailyDateKey = prefs.getString("dayKey", "");
dailyDangerCount = prefs.getUInt("dDanger", 0);
dailyWarningCount = prefs.getUInt("dWarn", 0);
dailyDangerSeconds = prefs.getULong("dDangerSec", 0);
dailyWarningSeconds = prefs.getULong("dWarnSec", 0);
dailyLongestDanger = prefs.getULong("dLongDanger", 0);
dailyLongestWarning = prefs.getULong("dLongWarn", 0);
dailyHighestMoisture = prefs.getFloat("dHigh", 0);
}
void ensureDailySummary(uint32_t now)
{
if (now == 0) return;
String today = dateKeyFromEpoch(now);
if (dailyDateKey != today)
{
dailyDateKey = today;
dailyDangerCount = 0;
dailyWarningCount = 0;
dailyDangerSeconds = 0;
dailyWarningSeconds = 0;
dailyLongestDanger = 0;
dailyLongestWarning = 0;
dailyHighestMoisture = 0;
// If a warning/danger event is already active after midnight,
// count it once for the new day.
if (currentWallState == STATE_WARNING)
dailyWarningCount = 1;
else if (currentWallState == STATE_DANGER)
dailyDangerCount = 1;
saveDailySummary();
}
}
void addRecentEvent(const WallEvent &event)
{
if (!event.valid)
return;
for (int i = min(recentEventCount, MAX_RECENT_EVENTS - 1); i > 0; i--)
recentEvents[i] = recentEvents[i - 1];
recentEvents[0] = event;
if (recentEventCount < MAX_RECENT_EVENTS)
recentEventCount++;
saveRecentEvents();
}
void startStateTracking(int state, uint32_t now, int previousState, uint32_t previousDuration)
{
currentWallState = state;
currentStateStartEpoch = now;
currentStateStartMoisture = wallMoisture;
currentStateStartRaw = moistureRaw;
currentStatePeakMoisture = wallMoisture;
currentStatePeakRaw = moistureRaw;
currentStateStartTemperature = temperature;
currentStateStartHumidity = humidity;
currentStateStartPressure = pressure;
currentPreviousState = previousState;
currentPreviousDuration = previousDuration;
if (state == STATE_WARNING)
dailyWarningCount++;
else if (state == STATE_DANGER)
dailyDangerCount++;
saveDailySummary();
}
void finalizeCurrentEvent(uint32_t now)
{
if (currentWallState != STATE_WARNING &&
currentWallState != STATE_DANGER)
return;
WallEvent event;
event.valid = true;
event.state = currentWallState;
event.startEpoch = currentStateStartEpoch;
event.endEpoch = now;
event.startMoisture = currentStateStartMoisture;
event.peakMoisture = currentStatePeakMoisture;
event.endMoisture = wallMoisture;
event.startRaw = currentStateStartRaw;
event.peakRaw = currentStatePeakRaw;
event.endRaw = moistureRaw;
event.startTemperature = currentStateStartTemperature;
event.startHumidity = currentStateStartHumidity;
event.startPressure = currentStateStartPressure;
event.endTemperature = temperature;
event.endHumidity = humidity;
event.endPressure = pressure;
event.previousState = currentPreviousState;
event.previousDuration = currentPreviousDuration;
uint32_t duration = 0;
if (now >= event.startEpoch)
duration = now - event.startEpoch;
if (event.state == STATE_DANGER)
{
dailyDangerSeconds += duration;
if (duration > dailyLongestDanger)
dailyLongestDanger = duration;
}
else
{
dailyWarningSeconds += duration;
if (duration > dailyLongestWarning)
dailyLongestWarning = duration;
}
addRecentEvent(event);
saveDailySummary();
}
void processWallState()
{
uint32_t now = getEpochNow();
if (now == 0)
return;
ensureDailySummary(now);
if (wallMoisture > dailyHighestMoisture)
{
dailyHighestMoisture = wallMoisture;
saveDailySummary();
}
int newState = wallStateFromMoisture(wallMoisture);
if (currentWallState < 0)
{
// First state after boot. Do not count SAFE as an event.
currentWallState = newState;
currentStateStartEpoch = now;
currentStateStartMoisture = wallMoisture;
currentStateStartRaw = moistureRaw;
currentStatePeakMoisture = wallMoisture;
currentStatePeakRaw = moistureRaw;
currentStateStartTemperature = temperature;
currentStateStartHumidity = humidity;
currentStateStartPressure = pressure;
currentPreviousState = STATE_SAFE;
currentPreviousDuration = 0;
if (newState == STATE_WARNING)
dailyWarningCount++;
else if (newState == STATE_DANGER)
dailyDangerCount++;
saveDailySummary();
return;
}
if (newState == currentWallState)
{
if (wallMoisture > currentStatePeakMoisture)
{
currentStatePeakMoisture = wallMoisture;
currentStatePeakRaw = moistureRaw;
}
return;
}
uint32_t previousDuration = 0;
if (now >= currentStateStartEpoch)
previousDuration = now - currentStateStartEpoch;
int previousState = currentWallState;
finalizeCurrentEvent(now);
startStateTracking(
newState,
now,
previousState,
previousDuration
);
}
void initClock()
{
// Bahrain timezone: UTC+3, no DST
setenv("TZ", "AST-3", 1);
tzset();
configTime(
0,
0,
"pool.ntp.org",
"time.nist.gov"
);
struct tm timeInfo;
for (int i = 0; i < 12; i++)
{
if (getLocalTime(&timeInfo, 500))
{
timeReady = true;
Serial.println("Clock synchronized");
return;
}
delay(250);
}
Serial.println("NTP time not available yet");
}
void keepClockUpdated()
{
if (getEpochNow() != 0)
{
timeReady = true;
return;
}
if (WiFi.status() == WL_CONNECTED &&
millis() - lastTimeSyncAttempt > 30000)
{
lastTimeSyncAttempt = millis();
initClock();
}
}
void handleSetTime()
{
if (!server.hasArg("epoch"))
{
server.send(400, "text/plain", "Missing epoch");
return;
}
uint32_t epoch = (uint32_t)strtoul(
server.arg("epoch").c_str(),
nullptr,
10
);
if (epoch < 1700000000)
{
server.send(400, "text/plain", "Invalid epoch");
return;
}
struct timeval tv;
tv.tv_sec = epoch;
tv.tv_usec = 0;
settimeofday(&tv, nullptr);
timeReady = true;
server.send(200, "text/plain", "Time updated");
}
String eventJson(const WallEvent &e)
{
String json = "{";
json += "\"valid\":";
json += e.valid ? "true" : "false";
if (e.valid)
{
json += ",\"state\":\"" + wallStateName(e.state) + "\"";
json += ",\"start\":" + String(e.startEpoch);
json += ",\"end\":" + String(e.endEpoch);
json += ",\"duration\":" + String(e.endEpoch >= e.startEpoch ? e.endEpoch - e.startEpoch : 0);
json += ",\"startMoisture\":" + String(e.startMoisture, 1);
json += ",\"peakMoisture\":" + String(e.peakMoisture, 1);
json += ",\"endMoisture\":" + String(e.endMoisture, 1);
json += ",\"startRaw\":" + String(e.startRaw);
json += ",\"peakRaw\":" + String(e.peakRaw);
json += ",\"endRaw\":" + String(e.endRaw);
json += ",\"startTemperature\":" + String(e.startTemperature, 1);
json += ",\"startHumidity\":" + String(e.startHumidity, 1);
json += ",\"startPressure\":" + String(e.startPressure, 1);
json += ",\"endTemperature\":" + String(e.endTemperature, 1);
json += ",\"endHumidity\":" + String(e.endHumidity, 1);
json += ",\"endPressure\":" + String(e.endPressure, 1);
json += ",\"previousState\":\"" + wallStateName(e.previousState) + "\"";
json += ",\"previousDuration\":" + String(e.previousDuration);
}
json += "}";
return json;
}
WallEvent getLastEventByState(int state)
{
for (int i = 0; i < recentEventCount; i++)
{
if (recentEvents[i].valid &&
recentEvents[i].state == state)
return recentEvents[i];
}
WallEvent emptyEvent;
return emptyEvent;
}
void handleHistory()
{
uint32_t now = getEpochNow();
WallEvent lastDanger = getLastEventByState(STATE_DANGER);
WallEvent lastWarning = getLastEventByState(STATE_WARNING);
uint32_t activeDuration = 0;
if (now > 0 &&
currentStateStartEpoch > 0 &&
now >= currentStateStartEpoch)
activeDuration = now - currentStateStartEpoch;
String json;
json.reserve(5000);
json = "{";
json += "\"timeReady\":";
json += timeReady ? "true" : "false";
json += ",\"now\":" + String(now);
json += ",\"current\":{";
json += "\"state\":\"" + wallStateName(currentWallState < 0 ? STATE_SAFE : currentWallState) + "\"";
json += ",\"start\":" + String(currentStateStartEpoch);
json += ",\"duration\":" + String(activeDuration);
json += ",\"startMoisture\":" + String(currentStateStartMoisture, 1);
json += ",\"peakMoisture\":" + String(currentStatePeakMoisture, 1);
json += ",\"currentMoisture\":" + String(wallMoisture, 1);
json += ",\"previousState\":\"" + wallStateName(currentPreviousState) + "\"";
json += ",\"previousDuration\":" + String(currentPreviousDuration);
json += "}";
json += ",\"lastDanger\":";
json += eventJson(lastDanger);
json += ",\"lastWarning\":";
json += eventJson(lastWarning);
json += ",\"summary\":{";
json += "\"date\":\"" + dailyDateKey + "\"";
json += ",\"dangerCount\":" + String(dailyDangerCount);
json += ",\"warningCount\":" + String(dailyWarningCount);
json += ",\"dangerSeconds\":" + String(dailyDangerSeconds);
json += ",\"warningSeconds\":" + String(dailyWarningSeconds);
json += ",\"longestDanger\":" + String(dailyLongestDanger);
json += ",\"longestWarning\":" + String(dailyLongestWarning);
json += ",\"highestMoisture\":" + String(dailyHighestMoisture, 1);
json += "}";
uint32_t airActiveDuration = 0;
if (now > 0 &&
currentAirStateStartEpoch > 0 &&
now >= currentAirStateStartEpoch)
airActiveDuration = now - currentAirStateStartEpoch;
json += ",\"airCurrent\":{";
json += "\"state\":\"" +
airIncidentStateName(
currentAirIncidentState < 0 ?
AIR_EVENT_NORMAL :
currentAirIncidentState
) + "\"";
json += ",\"start\":" +
String(currentAirStateStartEpoch);
json += ",\"duration\":" +
String(airActiveDuration);
json += ",\"startHumidity\":" +
String(currentAirStartHumidity, 1);
json += ",\"peakHumidity\":" +
String(currentAirPeakHumidity, 1);
json += ",\"currentHumidity\":" +
String(humidity, 1);
json += ",\"previousState\":\"" +
airIncidentStateName(currentAirPreviousState) + "\"";
json += ",\"previousDuration\":" +
String(currentAirPreviousDuration);
json += "}";
json += ",\"lastAirDanger\":";
json += airEventJson(lastAirDangerEvent);
json += ",\"lastAirWarning\":";
json += airEventJson(lastAirWarningEvent);
json += ",\"recent\":[";
for (int i = 0; i < recentEventCount; i++)
{
if (i > 0) json += ",";
json += eventJson(recentEvents[i]);
}
json += "]";
json += "}";
server.send(200, "application/json", json);
}
// =====================================================
// BATTERY / POWER FUNCTIONS
// =====================================================
void readBatteryStatus()
{
if (!BATTERY_MONITOR_ENABLED)
{
batteryVoltage = 0;
batteryPercent = 0;
return;
}
long total = 0;
for (int i = 0; i < 20; i++)
{
total += analogRead(BATTERY_PIN);
delay(2);
}
float raw = total / 20.0;
// ESP32 ADC is approximately 0-3.3V over 12-bit range.
float adcVoltage = (raw / 4095.0) * 3.3;
batteryVoltage = adcVoltage * BATTERY_DIVIDER_RATIO;
batteryPercent =
((batteryVoltage - BATTERY_EMPTY_VOLTAGE) /
(BATTERY_FULL_VOLTAGE - BATTERY_EMPTY_VOLTAGE)) * 100.0;
if (batteryPercent < 0) batteryPercent = 0;
if (batteryPercent > 100) batteryPercent = 100;
}
String powerSourceText()
{
if (!BATTERY_MONITOR_ENABLED)
return "USB / EXTERNAL POWER";
if (batteryPercent <= 15)
return "BATTERY LOW";
return "BATTERY / UPS";
}
// =====================================================
// AIR HUMIDITY INCIDENT FUNCTIONS
// =====================================================
int airIncidentStateFromHumidity(float value)
{
if (value >= AIR_DANGER)
return AIR_EVENT_DANGER;
if (value >= AIR_HIGH)
return AIR_EVENT_WARNING;
return AIR_EVENT_NORMAL;
}
String airIncidentStateName(int state)
{
if (state == AIR_EVENT_DANGER)
return "DANGER";
if (state == AIR_EVENT_WARNING)
return "HIGH HUMIDITY";
return "NORMAL";
}
String airEventToStorage(const AirHumidityEvent &e)
{
String s;
s.reserve(180);
s += String(e.valid ? 1 : 0) + "|";
s += String(e.state) + "|";
s += String(e.startEpoch) + "|";
s += String(e.endEpoch) + "|";
s += String(e.startHumidity, 2) + "|";
s += String(e.peakHumidity, 2) + "|";
s += String(e.endHumidity, 2) + "|";
s += String(e.startTemperature, 2) + "|";
s += String(e.startPressure, 2) + "|";
s += String(e.endTemperature, 2) + "|";
s += String(e.endPressure, 2) + "|";
s += String(e.previousState) + "|";
s += String(e.previousDuration);
return s;
}
AirHumidityEvent airEventFromStorage(const String &s)
{
AirHumidityEvent e;
if (s.length() == 0)
return e;
e.valid = storagePart(s, 0).toInt() == 1;
e.state = storagePart(s, 1).toInt();
e.startEpoch = (uint32_t)strtoul(storagePart(s, 2).c_str(), nullptr, 10);
e.endEpoch = (uint32_t)strtoul(storagePart(s, 3).c_str(), nullptr, 10);
e.startHumidity = storagePart(s, 4).toFloat();
e.peakHumidity = storagePart(s, 5).toFloat();
e.endHumidity = storagePart(s, 6).toFloat();
e.startTemperature = storagePart(s, 7).toFloat();
e.startPressure = storagePart(s, 8).toFloat();
e.endTemperature = storagePart(s, 9).toFloat();
e.endPressure = storagePart(s, 10).toFloat();
e.previousState = storagePart(s, 11).toInt();
e.previousDuration = (uint32_t)strtoul(storagePart(s, 12).c_str(), nullptr, 10);
return e;
}
void saveAirEvents()
{
prefs.putString("airWarnEvt", airEventToStorage(lastAirWarningEvent));
prefs.putString("airDangerEvt", airEventToStorage(lastAirDangerEvent));
}
void loadAirEvents()
{
lastAirWarningEvent =
airEventFromStorage(prefs.getString("airWarnEvt", ""));
lastAirDangerEvent =
airEventFromStorage(prefs.getString("airDangerEvt", ""));
}
void finalizeAirEvent(uint32_t now)
{
if (currentAirIncidentState != AIR_EVENT_WARNING &&
currentAirIncidentState != AIR_EVENT_DANGER)
return;
AirHumidityEvent e;
e.valid = true;
e.state = currentAirIncidentState;
e.startEpoch = currentAirStateStartEpoch;
e.endEpoch = now;
e.startHumidity = currentAirStartHumidity;
e.peakHumidity = currentAirPeakHumidity;
e.endHumidity = humidity;
e.startTemperature = currentAirStartTemperature;
e.startPressure = currentAirStartPressure;
e.endTemperature = temperature;
e.endPressure = pressure;
e.previousState = currentAirPreviousState;
e.previousDuration = currentAirPreviousDuration;
if (e.state == AIR_EVENT_DANGER)
lastAirDangerEvent = e;
else
lastAirWarningEvent = e;
saveAirEvents();
}
void startAirStateTracking(
int state,
uint32_t now,
int previousState,
uint32_t previousDuration
)
{
currentAirIncidentState = state;
currentAirStateStartEpoch = now;
currentAirStartHumidity = humidity;
currentAirPeakHumidity = humidity;
currentAirStartTemperature = temperature;
currentAirStartPressure = pressure;
currentAirPreviousState = previousState;
currentAirPreviousDuration = previousDuration;
}
void processAirHumidityState()
{
if (!bmeFound)
return;
uint32_t now = getEpochNow();
if (now == 0)
return;
int newState = airIncidentStateFromHumidity(humidity);
if (currentAirIncidentState < 0)
{
startAirStateTracking(
newState,
now,
AIR_EVENT_NORMAL,
0
);
return;
}
if (newState == currentAirIncidentState)
{
if (humidity > currentAirPeakHumidity)
currentAirPeakHumidity = humidity;
return;
}
uint32_t previousDuration = 0;
if (now >= currentAirStateStartEpoch)
previousDuration = now - currentAirStateStartEpoch;
int previousState = currentAirIncidentState;
finalizeAirEvent(now);
startAirStateTracking(
newState,
now,
previousState,
previousDuration
);
}
String airEventJson(const AirHumidityEvent &e)
{
String json = "{";
json += "\"valid\":";
json += e.valid ? "true" : "false";
if (e.valid)
{
json += ",\"state\":\"" + airIncidentStateName(e.state) + "\"";
json += ",\"start\":" + String(e.startEpoch);
json += ",\"end\":" + String(e.endEpoch);
json += ",\"duration\":" +
String(e.endEpoch >= e.startEpoch ?
e.endEpoch - e.startEpoch : 0);
json += ",\"startHumidity\":" + String(e.startHumidity, 1);
json += ",\"peakHumidity\":" + String(e.peakHumidity, 1);
json += ",\"endHumidity\":" + String(e.endHumidity, 1);
json += ",\"startTemperature\":" + String(e.startTemperature, 1);
json += ",\"startPressure\":" + String(e.startPressure, 1);
json += ",\"endTemperature\":" + String(e.endTemperature, 1);
json += ",\"endPressure\":" + String(e.endPressure, 1);
json += ",\"previousState\":\"" +
airIncidentStateName(e.previousState) + "\"";
json += ",\"previousDuration\":" +
String(e.previousDuration);
}
json += "}";
return json;
}
// =====================================================
// TREND HISTORY FUNCTIONS
// =====================================================
void saveTrendHistory()
{
prefs.putInt("trendCount", trendSampleCount);
prefs.putInt("trendIndex", trendWriteIndex);
prefs.putBytes("trendData", trendSamples, sizeof(trendSamples));
}
void loadTrendHistory()
{
trendSampleCount = prefs.getInt("trendCount", 0);
trendWriteIndex = prefs.getInt("trendIndex", 0);
if (trendSampleCount < 0 || trendSampleCount > MAX_TREND_SAMPLES)
trendSampleCount = 0;
if (trendWriteIndex < 0 || trendWriteIndex >= MAX_TREND_SAMPLES)
trendWriteIndex = 0;
size_t stored = prefs.getBytesLength("trendData");
if (stored == sizeof(trendSamples))
prefs.getBytes("trendData", trendSamples, sizeof(trendSamples));
}
void captureTrendSample()
{
uint32_t now = getEpochNow();
if (now == 0)
return;
TrendSample sample;
sample.epoch = now;
sample.wall = wallMoisture;
sample.temperature = temperature;
sample.humidity = humidity;
sample.pressure = pressure;
trendSamples[trendWriteIndex] = sample;
trendWriteIndex++;
if (trendWriteIndex >= MAX_TREND_SAMPLES)
trendWriteIndex = 0;
if (trendSampleCount < MAX_TREND_SAMPLES)
trendSampleCount++;
lastTrendSampleMillis = millis();
// Persist at the same 10-minute interval.
saveTrendHistory();
}
void maybeCaptureTrendSample()
{
if (lastTrendSampleMillis == 0 ||
millis() - lastTrendSampleMillis >= TREND_INTERVAL)
{
captureTrendSample();
}
}
int trendPhysicalIndex(int logicalIndex)
{
if (trendSampleCount < MAX_TREND_SAMPLES)
return logicalIndex;
int oldest = trendWriteIndex;
return (oldest + logicalIndex) % MAX_TREND_SAMPLES;
}
float recentWallSlopePerHour()
{
if (trendSampleCount < 3)
return 0.0;
int points = min(trendSampleCount, 12); // last ~2 hours at 10-minute interval
int startLogical = trendSampleCount - points;
int firstIndex = trendPhysicalIndex(startLogical);
int lastIndex = trendPhysicalIndex(trendSampleCount - 1);
TrendSample first = trendSamples[firstIndex];
TrendSample last = trendSamples[lastIndex];
if (first.epoch == 0 || last.epoch <= first.epoch)
return 0.0;
float hours = (last.epoch - first.epoch) / 3600.0;
if (hours <= 0.0)
return 0.0;
return (last.wall - first.wall) / hours;
}
String predictionText()
{
float slope = recentWallSlopePerHour();
if (trendSampleCount < 3)
return "Collecting historical samples before a trend estimate can be calculated.";
if (wallMoisture >= WALL_DANGER)
return "The wall is already in the DANGER range. Prediction is not required; immediate inspection is
recommended.";
if (fabs(slope) < 0.20)
return "Moisture is currently stable. No meaningful rising trend is detected.";
if (slope < 0)
return "Moisture is trending downward. No rising-risk estimate is currently indicated.";
float target = wallMoisture < WALL_WARNING ? WALL_WARNING : WALL_DANGER;
float hours = (target - wallMoisture) / slope;
if (hours <= 0)
return "The threshold has effectively been reached.";
if (hours > 72)
return "Moisture is rising slowly, but the next threshold is not projected within the next 72 hours.";
String label = target >= WALL_DANGER ? "DANGER (60%)" : "EARLY WARNING (40%)";
return "Trend estimate: if the recent rate continues, the wall may reach " +
label + " in approximately " + String(hours, 1) +
" hours. This is a simple trend estimate, not a scientific forecast.";
}
String overallStatus()
{
if (wallMoisture >= WALL_DANGER ||
humidity >= AIR_DANGER ||
getTemperatureStatus() == "RAPID CHANGE" ||
getPressureStatus() == "LARGE CHANGE")
return "DANGER";
if (wallMoisture >= WALL_WARNING ||
humidity >= AIR_HIGH ||
getTemperatureStatus() == "MONITOR" ||
getPressureStatus() == "CHANGING")
return "WARNING";
return "SAFE";
}
void handleTrend()
{
String json;
json.reserve(16000);
json = "{";
json += "\"count\":" + String(trendSampleCount);
json += ",\"slope\":" + String(recentWallSlopePerHour(), 3);
json += ",\"prediction\":\"" + predictionText() + "\"";
json += ",\"samples\":[";
for (int i = 0; i < trendSampleCount; i++)
{
int idx = trendPhysicalIndex(i);
TrendSample &s = trendSamples[idx];
if (i > 0) json += ",";
json += "{";
json += "\"t\":" + String(s.epoch);
json += ",\"wall\":" + String(s.wall, 1);
json += ",\"temperature\":" + String(s.temperature, 1);
json += ",\"humidity\":" + String(s.humidity, 1);
json += ",\"pressure\":" + String(s.pressure, 1);
json += "}";
}
json += "]}";
server.send(200, "application/json", json);
}
void handleRefresh()
{
readMoisture();
readBME();
readBatteryStatus();
processWallState();
processAirHumidityState();
captureTrendSample();
String json = "{";
json += "\"ok\":true";
json += ",\"wall\":" + String(wallMoisture, 1);
json += ",\"temperature\":" + String(temperature, 1);
json += ",\"humidity\":" + String(humidity, 1);
json += ",\"pressure\":" + String(pressure, 1);
json += "}";
server.send(200, "application/json", json);
}
// =====================================================
// READ WALL MOISTURE
// =====================================================
void readMoisture()
{
long total = 0;
// Average 15 readings
for (int i = 0; i < 15; i++)
{
total += analogRead(MOISTURE_PIN);
delay(5);
}
moistureRaw = total / 15;
wallMoisture =
((float)(DRY_VALUE - moistureRaw) /
(DRY_VALUE - WET_VALUE)) * 100.0;
if (wallMoisture < 0)
wallMoisture = 0;
if (wallMoisture > 100)
wallMoisture = 100;
}
// =====================================================
// READ BME280
// =====================================================
void readBME()
{
if (!bmeFound)
return;
temperature = bme.readTemperature();
humidity = bme.readHumidity();
pressure = bme.readPressure() / 100.0F;
}
// =====================================================
// MONITOR ENVIRONMENT CHANGES
// =====================================================
void monitorEnvironmentalChanges()
{
if (!bmeFound)
return;
if (lastEnvironmentReference == 0)
{
oldTemperature = temperature;
oldPressure = pressure;
lastEnvironmentReference = millis();
return;
}
if (millis() - lastEnvironmentReference >= ENVIRONMENT_WINDOW)
{
temperatureChange = temperature - oldTemperature;
pressureChange = pressure - oldPressure;
oldTemperature = temperature;
oldPressure = pressure;
lastEnvironmentReference = millis();
}
}
// =====================================================
// SERIAL MONITOR
// =====================================================
void printReadings()
{
Serial.println();
Serial.println("=================================================");
Serial.println(" HERITAGE WALL MONITORING SYSTEM");
Serial.println("=================================================");
Serial.print("Wall Moisture RAW : ");
Serial.println(moistureRaw);
Serial.print("Wall Moisture : ");
Serial.print(wallMoisture, 1);
Serial.println(" %");
Serial.print("Wall Status : ");
Serial.println(getWallStatus(wallMoisture));
Serial.println("-------------------------------------------------");
if (bmeFound)
{
Serial.print("Temperature : ");
Serial.print(temperature, 1);
Serial.println(" C");
Serial.print("Temperature State : ");
Serial.println(getTemperatureStatus());
Serial.print("Air Humidity : ");
Serial.print(humidity, 1);
Serial.println(" %");
Serial.print("Air Status : ");
Serial.println(getAirStatus(humidity));
Serial.print("Pressure : ");
Serial.print(pressure, 1);
Serial.println(" hPa");
Serial.print("Pressure State : ");
Serial.println(getPressureStatus());
}
else
{
Serial.println("BME280 SENSOR OFFLINE");
}
Serial.println("=================================================");
}
// =====================================================
// LCD 16x2 DISPLAY
// =====================================================
String fitLCD(String text)
{
if (text.length() > 16)
return text.substring(0, 16);
while (text.length() < 16)
text += " ";
return text;
}
void updateLCD()
{
static bool secondPage = false;
static unsigned long lastPageChange = 0;
if (millis() - lastPageChange >= 4000)
{
secondPage = !secondPage;
lastPageChange = millis();
}
String line1;
String line2;
if (!secondPage)
{
String shortStatus;
if (wallMoisture < WALL_WARNING)
shortStatus = "SAFE";
else if (wallMoisture < WALL_DANGER)
shortStatus = "WARN";
else
shortStatus = "DANGER";
line1 = "Wall:" + String(wallMoisture, 1) + "%";
line2 = shortStatus + " RAW:" + String(moistureRaw);
}
else
{
if (bmeFound)
{
line1 = "T:" + String(temperature, 1) + "C H:" + String(humidity, 0) + "%";
line2 = "P:" + String(pressure, 1) + "hPa";
}
else
{
line1 = "BME280 OFFLINE";
line2 = "Check sensor";
}
}
lcd.setCursor(0, 0);
lcd.print(fitLCD(line1));
lcd.setCursor(0, 1);
lcd.print(fitLCD(line2));
}
// =====================================================
// JSON DATA FOR LIVE DASHBOARD
// =====================================================
void handleData()
{
String json = "{";
json += "\"wall\":";
json += String(wallMoisture, 1);
json += ",\"raw\":";
json += String(moistureRaw);
json += ",\"wallStatus\":\"";
json += getWallStatus(wallMoisture);
json += "\"";
json += ",\"bme\":";
json += bmeFound ? "true" : "false";
json += ",\"temperature\":";
json += String(temperature, 1);
json += ",\"humidity\":";
json += String(humidity, 1);
json += ",\"pressure\":";
json += String(pressure, 1);
json += ",\"airStatus\":\"";
json += getAirStatus(humidity);
json += "\"";
json += ",\"temperatureStatus\":\"";
json += getTemperatureStatus();
json += "\"";
json += ",\"pressureStatus\":\"";
json += getPressureStatus();
json += "\"";
json += ",\"temperatureChange\":";
json += String(temperatureChange, 1);
json += ",\"pressureChange\":";
json += String(pressureChange, 1);
json += ",\"rssi\":";
json += String(WiFi.RSSI());
json += ",\"uptime\":";
json += String(millis() / 1000);
json += ",\"batteryEnabled\":";
json += BATTERY_MONITOR_ENABLED ? "true" : "false";
json += ",\"batteryVoltage\":";
json += String(batteryVoltage, 2);
json += ",\"batteryPercent\":";
json += String(batteryPercent, 0);
json += ",\"powerSource\":\"";
json += powerSourceText();
json += "\"";
json += "}";
server.send(200, "application/json", json);
}
// =====================================================
// PROFESSIONAL WEB PAGE
// =====================================================
const char* createWebPage()
{
static const char html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport"
content="width=device-width, initial-scale=1.0">
<title>Heritage Wall Monitoring System</title>
<style>
*{
box-sizing:border-box;
}
body{
margin:0;
font-family:
Inter,
Segoe UI,
Arial,
sans-serif;
background:#07111f;
color:#eef5ff;
}
.header{
background:
linear-gradient(
135deg,
#0c1b2d,
#102b42
);
padding:32px 20px;
border-bottom:
1px solid rgba(255,255,255,.08);
}
.header-inner{
max-width:1200px;
margin:auto;
display:flex;
align-items:center;
justify-content:space-between;
gap:20px;
}
.title h1{
margin:0;
font-size:29px;
}
.title p{
color:#91a9bd;
margin:8px 0 0;
}
.live{
display:flex;
align-items:center;
gap:8px;
background:
rgba(34,197,94,.12);
padding:
8px 14px;
border-radius:100px;
color:#66e191;
font-weight:600;
}
.live-dot{
width:9px;
height:9px;
border-radius:50%;
background:#22c55e;
box-shadow:
0 0 12px #22c55e;
}
.container{
max-width:1200px;
margin:auto;
padding:28px 20px;
}
.cards{
display:grid;
grid-template-columns:
repeat(4,1fr);
gap:18px;
}
.card{
background:#0d1b2a;
border:
1px solid
rgba(255,255,255,.07);
border-radius:18px;
padding:22px;
box-shadow:
0 15px 35px
rgba(0,0,0,.22);
}
.card-title{
color:#91a9bd;
font-size:14px;
font-weight:600;
text-transform:uppercase;
letter-spacing:.6px;
}
.value{
font-size:38px;
font-weight:700;
margin:
12px 0 10px;
}
.unit{
font-size:18px;
color:#8194a7;
}
.badge{
display:inline-block;
padding:
7px 11px;
border-radius:8px;
font-size:13px;
font-weight:700;
}
.safe{
background:
rgba(34,197,94,.15);
color:#67e294;
}
.warning{
background:
rgba(245,158,11,.15);
color:#fbbf24;
}
.danger{
background:
rgba(239,68,68,.15);
color:#ff6b6b;
}
.neutral{
background:
rgba(59,130,246,.15);
color:#72a8ff;
}
.section{
background:#0d1b2a;
border:
1px solid
rgba(255,255,255,.07);
border-radius:18px;
margin-top:20px;
padding:25px;
}
.section h2{
margin-top:0;
font-size:20px;
}
.moisture-number{
font-size:45px;
font-weight:700;
margin:
18px 0 10px;
}
.progress{
width:100%;
height:17px;
border-radius:100px;
overflow:hidden;
background:#182b3d;
}
.progress-bar{
height:100%;
width:0%;
transition:
width .5s,
background .5s;
border-radius:100px;
}
.scale{
display:flex;
justify-content:space-between;
color:#71869a;
font-size:12px;
margin-top:8px;
}
.alert-box{
margin-top:20px;
padding:18px;
border-radius:13px;
line-height:1.6;
}
.alert-safe{
background:
rgba(34,197,94,.08);
border:
1px solid
rgba(34,197,94,.3);
}
.alert-warning{
background:
rgba(245,158,11,.08);
border:
1px solid
rgba(245,158,11,.3);
}
.alert-danger{
background:
rgba(239,68,68,.08);
border:
1px solid
rgba(239,68,68,.3);
}
.assessment{
display:grid;
grid-template-columns:
1fr 1fr;
gap:16px;
}
.info{
background:#102235;
padding:18px;
border-radius:13px;
}
.info-title{
font-weight:700;
margin-bottom:6px;
}
.info-text{
color:#91a9bd;
line-height:1.6;
font-size:14px;
}
.system{
display:flex;
gap:20px;
flex-wrap:wrap;
color:#8ca1b5;
font-size:13px;
margin-top:18px;
}
.footer{
color:#60778c;
text-align:center;
padding:
10px 20px 30px;
}
@media(max-width:900px){
.cards{
grid-template-columns:
1fr 1fr;
}
}
@media(max-width:600px){
.cards{
grid-template-columns:
1fr;
}
.assessment{
grid-template-columns:
1fr;
}
.header-inner{
flex-direction:column;
align-items:flex-start;
}
}
.history-grid{
display:grid;
grid-template-columns:1fr 1fr;
gap:18px;
}
.incident-card{
background:#102235;
border:1px solid rgba(255,255,255,.07);
border-radius:15px;
padding:20px;
}
.incident-card.danger-event{
border-color:rgba(239,68,68,.35);
}
.incident-card.warning-event{
border-color:rgba(245,158,11,.35);
}
.incident-head{
display:flex;
justify-content:space-between;
align-items:center;
gap:10px;
margin-bottom:15px;
}
.incident-title{
font-size:16px;
font-weight:800;
}
.incident-time{
color:#91a9bd;
font-size:13px;
}
.detail-grid{
display:grid;
grid-template-columns:repeat(2,1fr);
gap:10px;
margin-top:14px;
}
.detail-item{
background:#0b1a29;
border-radius:10px;
padding:11px 12px;
}
.detail-label{
color:#70879c;
font-size:11px;
text-transform:uppercase;
letter-spacing:.5px;
}
.detail-value{
margin-top:5px;
font-weight:700;
font-size:14px;
}
.report-box{
margin-top:14px;
background:#0b1a29;
padding:14px;
border-radius:11px;
color:#a9bac9;
line-height:1.65;
font-size:13px;
}
.summary-grid{
display:grid;
grid-template-columns:repeat(4,1fr);
gap:12px;
margin-top:16px;
}
.summary-item{
background:#102235;
border-radius:13px;
padding:16px;
}
.summary-label{
color:#758ca1;
font-size:12px;
}
.summary-value{
margin-top:7px;
font-size:22px;
font-weight:800;
}
.active-critical{
display:none;
margin-top:20px;
border:1px solid rgba(239,68,68,.5);
background:linear-gradient(135deg,rgba(239,68,68,.14),rgba(127,29,29,.10));
border-radius:16px;
padding:20px;
}
.active-warning{
border-color:rgba(245,158,11,.5);
background:linear-gradient(135deg,rgba(245,158,11,.14),rgba(120,53,15,.10));
}
.active-row{
display:flex;
justify-content:space-between;
align-items:flex-start;
gap:15px;
flex-wrap:wrap;
}
.active-title{
font-size:18px;
font-weight:800;
}
.timeline{
margin-top:15px;
display:flex;
flex-direction:column;
gap:10px;
}
.timeline-event{
display:grid;
grid-template-columns:120px 1fr auto;
gap:12px;
align-items:center;
background:#102235;
padding:13px;
border-radius:11px;
}
.timeline-state{
font-weight:800;
}
.timeline-period{
color:#91a9bd;
font-size:13px;
}
.timeline-duration{
font-weight:700;
font-size:13px;
}
.no-event{
color:#71869a;
padding:14px 0;
}
@media(max-width:900px){
.history-grid{
grid-template-columns:1fr;
}
.summary-grid{
grid-template-columns:1fr 1fr;
}
}
@media(max-width:600px){
.detail-grid,
.summary-grid{
grid-template-columns:1fr;
}
.timeline-event{
grid-template-columns:1fr;
}
}
.overall-banner{
margin-top:20px;
border-radius:18px;
padding:20px 22px;
display:flex;
justify-content:space-between;
align-items:center;
gap:18px;
flex-wrap:wrap;
border:1px solid rgba(255,255,255,.10);
}
.overall-safe{
background:linear-gradient(135deg,rgba(34,197,94,.18),rgba(22,101,52,.08));
border-color:rgba(34,197,94,.35);
}
.overall-warning{
background:linear-gradient(135deg,rgba(245,158,11,.18),rgba(120,53,15,.08));
border-color:rgba(245,158,11,.35);
}
.overall-danger{
background:linear-gradient(135deg,rgba(239,68,68,.20),rgba(127,29,29,.10));
border-color:rgba(239,68,68,.45);
}
.overall-title{
font-size:14px;
color:#9db0c2;
text-transform:uppercase;
letter-spacing:.7px;
}
.overall-state{
font-size:31px;
font-weight:900;
margin-top:4px;
}
.action-row{
display:flex;
gap:10px;
flex-wrap:wrap;
}
.action-btn{
border:1px solid rgba(255,255,255,.12);
background:#13283b;
color:#eef5ff;
padding:10px 15px;
border-radius:10px;
cursor:pointer;
font-weight:700;
}
.action-btn:hover{
background:#183249;
}
.wifi-meter{
display:inline-flex;
align-items:flex-end;
gap:3px;
height:18px;
vertical-align:middle;
margin-right:7px;
}
.wifi-bar{
width:4px;
background:#475d70;
border-radius:2px;
}
.wifi-bar.on{
background:#67e294;
}
.wifi-bar:nth-child(1){height:5px;}
.wifi-bar:nth-child(2){height:9px;}
.wifi-bar:nth-child(3){height:13px;}
.wifi-bar:nth-child(4){height:17px;}
.chart-grid{
display:grid;
grid-template-columns:1fr 1fr;
gap:18px;
margin-top:18px;
}
.chart-card{
background:#102235;
border-radius:14px;
padding:16px;
border:1px solid rgba(255,255,255,.06);
}
.chart-title{
font-weight:800;
margin-bottom:10px;
}
.chart-wrap{
width:100%;
height:230px;
}
.chart-wrap canvas{
width:100%;
height:100%;
display:block;
}
.compare-grid{
display:grid;
grid-template-columns:repeat(3,1fr);
gap:12px;
margin-top:16px;
}
.compare-card{
background:#102235;
border-radius:13px;
padding:16px;
}
.compare-title{
color:#7f95a9;
font-size:12px;
}
.compare-value{
font-size:18px;
font-weight:800;
margin-top:7px;
}
.section-tabs{
display:grid;
grid-template-columns:repeat(3,1fr);
gap:12px;
margin-top:15px;
}
.section-tab{
background:#102235;
border-radius:13px;
padding:16px;
}
.section-tab h3{
margin:0 0 8px;
font-size:16px;
}
.small-muted{
color:#91a9bd;
font-size:13px;
line-height:1.6;
}
@media(max-width:900px){
.chart-grid,
.compare-grid,
.section-tabs{
grid-template-columns:1fr;
}
}
.power-indicator{
display:flex;
align-items:center;
gap:10px;
}
.battery-shell{
width:48px;
height:22px;
border:2px solid #91a9bd;
border-radius:5px;
padding:2px;
position:relative;
}
.battery-shell:after{
content:"";
width:4px;
height:10px;
background:#91a9bd;
position:absolute;
right:-6px;
top:4px;
border-radius:0 2px 2px 0;
}
.battery-fill{
height:100%;
width:0%;
border-radius:2px;
background:#67e294;
transition:width .4s, background .4s;
}
.wall-map{
background:#102235;
border-radius:16px;
padding:18px;
margin-top:16px;
}
.wall-visual{
position:relative;
height:260px;
border-radius:14px;
overflow:hidden;
border:1px solid rgba(255,255,255,.12);
background:
linear-gradient(rgba(255,255,255,.03),rgba(255,255,255,.03)),
repeating-linear-gradient(
0deg,
#8d795f 0px,
#8d795f 35px,
#705f4a 36px,
#705f4a 39px
);
}
.wall-zone{
position:absolute;
left:18%;
top:18%;
width:64%;
height:64%;
border:3px solid rgba(255,255,255,.9);
border-radius:18px;
display:flex;
align-items:center;
justify-content:center;
text-align:center;
font-weight:900;
font-size:18px;
transition:background .5s, box-shadow .5s;
}
.wall-zone.safe-zone{
background:rgba(34,197,94,.35);
box-shadow:inset 0 0 50px rgba(34,197,94,.25);
}
.wall-zone.warning-zone{
background:rgba(245,158,11,.38);
box-shadow:inset 0 0 50px rgba(245,158,11,.25);
}
.wall-zone.danger-zone{
background:rgba(239,68,68,.42);
box-shadow:inset 0 0 60px rgba(239,68,68,.30);
}
.wall-map-legend{
display:flex;
gap:15px;
flex-wrap:wrap;
margin-top:12px;
color:#91a9bd;
font-size:12px;
}
.legend-dot{
width:9px;
height:9px;
border-radius:50%;
display:inline-block;
margin-right:5px;
}
.air-history-grid{
display:grid;
grid-template-columns:1fr 1fr;
gap:18px;
margin-top:18px;
}
.air-history-card{
background:#102235;
border-radius:15px;
padding:20px;
border:1px solid rgba(255,255,255,.07);
}
@media(max-width:900px){
.air-history-grid{
grid-template-columns:1fr;
}
}
.lang-toggle{
border:1px solid rgba(255,255,255,.16);
background:#13283b;
color:#eef5ff;
padding:10px 14px;
border-radius:10px;
cursor:pointer;
font-weight:800;
}
.lang-toggle:hover{
background:#183249;
}
body.rtl{
direction:rtl;
text-align:right;
}
body.rtl .header-inner,
body.rtl .overall-banner,
body.rtl .active-row,
body.rtl .incident-head,
body.rtl .system{
direction:rtl;
}
body.rtl .cards,
body.rtl .assessment,
body.rtl .history-grid,
body.rtl .summary-grid,
body.rtl .compare-grid,
body.rtl .section-tabs,
body.rtl .chart-grid,
body.rtl .air-history-grid{
direction:rtl;
}
body.rtl .detail-label,
body.rtl .summary-label,
body.rtl .compare-title,
body.rtl .overall-title{
letter-spacing:0;
}
@media print{
body{
background:#ffffff !important;
color:#000000 !important;
font-family:Arial,"Segoe UI",sans-serif;
}
.header{
background:#ffffff !important;
color:#000000 !important;
border-bottom:2px solid #000000;
}
.container{
max-width:none !important;
width:100% !important;
padding:10px !important;
}
.action-row,
.lang-toggle,
.live,
#historyClockWarning{
display:none !important;
}
#printReportHeader{
display:block !important;
}
.section,
.card,
.info,
.incident-card,
.summary-item,
.compare-card,
.section-tab,
.chart-card,
.air-history-card,
.wall-map,
.overall-banner{
background:#ffffff !important;
color:#000000 !important;
border:1px solid #c9c9c9 !important;
box-shadow:none !important;
break-inside:avoid;
}
.info-text,
.small-muted,
.incident-time,
.detail-label,
.summary-label,
.compare-title,
.system,
.footer{
color:#444444 !important;
}
.cards,
.assessment,
.history-grid,
.chart-grid,
.compare-grid,
.summary-grid,
.section-tabs,
.air-history-grid{
grid-template-columns:repeat(2,1fr) !important;
}
canvas{
background:#ffffff !important;
}
.footer{
padding-top:20px;
}
@page{
size:A4;
margin:12mm;
}
}
</style>
</head>
<body>
<div class="header">
<div class="header-inner">
<div class="title">
<h1>
Heritage Wall Monitoring System
</h1>
<p>
ESP32 Live Environmental Monitoring
</p>
</div>
<div style="display:flex;gap:10px;align-items:center;flex-wrap:wrap;">
<button
id="languageToggle"
class="lang-toggle"
onclick="toggleLanguage()">
العربية
</button>
<div class="live">
<div class="live-dot"></div>
<span id="liveMonitoringText">LIVE MONITORING</span>
</div>
</div>
</div>
</div>
<div class="container">
<div class="overall-banner overall-safe" id="overallBanner">
<div>
<div class="overall-title">Overall System Assessment</div>
<div class="overall-state" id="overallState">SAFE</div>
<div class="small-muted" id="overallReason">
Evaluating wall and environmental conditions...
</div>
</div>
<div class="action-row">
<button class="action-btn" onclick="manualRefresh(this)">Refresh Sensors</button>
<button class="action-btn" onclick="requestNotifications()" id="enableAlertsBtn">Enable
Alerts</button>
<button class="action-btn" onclick="printReport()" id="printReportBtn">Print Report</button>
</div>
</div>
<div class="cards">
<div class="card">
<div class="card-title">
Wall Moisture
</div>
<div class="value">
<span id="wall">
--
</span>
<span class="unit">%</span>
</div>
<span
id="wallBadge"
class="badge neutral">
Loading
</span>
</div>
<div class="card">
<div class="card-title">
Air Humidity
</div>
<div class="value">
<span id="humidity">
--
</span>
<span class="unit">%</span>
</div>
<span
id="airBadge"
class="badge neutral">
Loading
</span>
</div>
<div class="card">
<div class="card-title">
Temperature
</div>
<div class="value">
<span id="temperature">
--
</span>
<span class="unit">&deg;C</span>
</div>
<span
id="temperatureBadge"
class="badge neutral">
Loading
</span>
</div>
<div class="card">
<div class="card-title">
Atmospheric Pressure
</div>
<div class="value">
<span id="pressure">
--
</span>
<span class="unit">hPa</span>
</div>
<span
id="pressureBadge"
class="badge neutral">
Loading
</span>
</div>
</div>
<div class="section">
<h2>
Wall Condition
</h2>
<div class="moisture-number">
<span id="wallLarge">
--
</span>%
</div>
<div class="progress">
<div
id="wallBar"
class="progress-bar">
</div>
</div>
<div class="scale">
<span>0% Safe</span>
<span>40% Warning</span>
<span>60% Danger</span>
<span>100%</span>
</div>
<div
id="wallAlert"
class="alert-box">
Loading wall condition...
</div>
</div>
<div class="section">
<h2>
System Assessment
</h2>
<div class="assessment">
<div class="info">
<div class="info-title">
Wall Moisture Assessment
</div>
<div
id="wallAssessment"
class="info-text">
Waiting for sensor data...
</div>
</div>
<div class="info">
<div class="info-title">
Environmental Assessment
</div>
<div
id="environmentAssessment"
class="info-text">
Waiting for BME280 data...
</div>
</div>
</div>
<div class="system">
<span>
Sensor RAW:
<b id="raw">--</b>
</span>
<span>
BME280:
<b id="sensorStatus">--</b>
</span>
<span>
Wi-Fi:
<span class="wifi-meter" id="wifiMeter">
<span class="wifi-bar"></span>
<span class="wifi-bar"></span>
<span class="wifi-bar"></span>
<span class="wifi-bar"></span>
</span>
<b id="rssi">--</b>
</span>
<span>
Last Update:
<b id="time">--</b>
</span>
</div>
</div>
<div class="section">
<h2>Monitoring Overview</h2>
<div class="section-tabs">
<div class="section-tab">
<h3>Wall</h3>
<div class="small-muted">
Wall moisture condition, threshold status and recent moisture trend.
</div>
</div>
<div class="section-tab">
<h3>Environment</h3>
<div class="small-muted">
Temperature, ambient humidity and atmospheric pressure from the BME280.
</div>
</div>
<div class="section-tab">
<h3>Technical</h3>
<div class="small-muted">
Wi-Fi signal, sensor availability, RAW value, clock and update status.
</div>
</div>
</div>
</div>
<div class="section">
<h2>Power & Technical Status</h2>
<div class="assessment">
<div class="info">
<div class="info-title">Power Source</div>
<div class="power-indicator" style="margin-top:12px;">
<div class="battery-shell">
<div id="batteryFill" class="battery-fill"></div>
</div>
<div>
<div id="powerStatusText" style="font-weight:800;">--</div>
<div id="batteryDetails" class="info-text">Checking power status...</div>
</div>
</div>
</div>
<div class="info">
<div class="info-title">System Connectivity</div>
<div class="info-text" style="margin-top:10px;">
Wi-Fi quality is shown graphically in the live status area.
Sensor and web-server status update automatically every 2 seconds.
</div>
</div>
</div>
</div>
<div class="section">
<h2>Comparison With Preferred Ranges</h2>
<div class="compare-grid">
<div class="compare-card">
<div class="compare-title">Wall Moisture vs Warning Limit</div>
<div class="compare-value" id="wallCompare">--</div>
</div>
<div class="compare-card">
<div class="compare-title">Air Humidity vs Preferred 40-60%</div>
<div class="compare-value" id="humidityCompare">--</div>
</div>
<div class="compare-card">
<div class="compare-title">Pressure Trend</div>
<div class="compare-value" id="pressureCompare">--</div>
</div>
</div>
</div>
<div class="section">
<h2>Environmental Trends - Last 72 Hours</h2>
<div class="info-text">
A new historical sample is stored every 10 minutes. Charts build automatically
as the ESP32 collects data.
</div>
<div class="chart-grid">
<div class="chart-card">
<div class="chart-title">Wall Moisture (%)</div>
<div class="chart-wrap"><canvas id="wallChart"></canvas></div>
</div>
<div class="chart-card">
<div class="chart-title">Temperature (&deg;C)</div>
<div class="chart-wrap"><canvas id="tempChart"></canvas></div>
</div>
<div class="chart-card">
<div class="chart-title">Air Humidity (%)</div>
<div class="chart-wrap"><canvas id="humidityChart"></canvas></div>
</div>
<div class="chart-card">
<div class="chart-title">Atmospheric Pressure (hPa)</div>
<div class="chart-wrap"><canvas id="pressureChart"></canvas></div>
</div>
</div>
</div>
<div class="section">
<h2>Risk Trend Estimate</h2>
<div class="report-box" id="predictionBox">
Collecting enough historical data to estimate the recent moisture trend...
</div>
</div>
<div class="section">
<h2>Wall Moisture Map</h2>
<div class="info-text">
The current project uses one wall-moisture sensor, so the highlighted area
represents the monitored sensor zone. More zones can be added later by using
additional sensors.
</div>
<div class="wall-map">
<div class="wall-visual">
<div id="wallZone" class="wall-zone safe-zone">
<div>
ZONE A<br>
<span id="wallZoneValue">--%</span><br>
<span id="wallZoneState" style="font-size:13px;">SAFE</span>
</div>
</div>
</div>
<div class="wall-map-legend">
<span><span class="legend-dot" style="background:#22c55e;"></span>Safe &lt;40%</span>
<span><span class="legend-dot" style="background:#f59e0b;"></span>Warning 40-60%</span>
<span><span class="legend-dot" style="background:#ef4444;"></span>Danger 60%+</span>
</div>
</div>
</div>
<div
id="activeIncident"
class="active-critical">
<div class="active-row">
<div>
<div
id="activeIncidentTitle"
class="active-title">
ACTIVE INCIDENT
</div>
<div
id="activeIncidentText"
class="info-text"
style="margin-top:7px;">
Waiting for event data...
</div>
</div>
<span
id="activeIncidentBadge"
class="badge danger">
ACTIVE
</span>
</div>
</div>
<div class="section">
<h2>
Incident History & Risk Analysis
</h2>
<div class="info-text">
Detailed history of the latest danger and early-warning events,
including date, time, duration, previous condition and environmental readings.
</div>
<div
id="historyClockWarning"
class="alert-box alert-warning"
style="display:none;">
Device clock is not synchronized yet. Open this dashboard while connected
to the ESP32 network to synchronize the date and time.
</div>
<div class="summary-grid">
<div class="summary-item">
<div class="summary-label">Danger Events Today</div>
<div id="sumDangerCount" class="summary-value">--</div>
</div>
<div class="summary-item">
<div class="summary-label">Early Warnings Today</div>
<div id="sumWarningCount" class="summary-value">--</div>
</div>
<div class="summary-item">
<div class="summary-label">Total Danger Time</div>
<div id="sumDangerTime" class="summary-value">--</div>
</div>
<div class="summary-item">
<div class="summary-label">Highest Moisture Today</div>
<div id="sumHighest" class="summary-value">--</div>
</div>
</div>
<div class="history-grid" style="margin-top:18px;">
<div class="incident-card danger-event">
<div class="incident-head">
<div class="incident-title">Last Danger Event</div>
<span class="badge danger">DANGER</span>
</div>
<div id="lastDangerContent" class="no-event">
No completed danger event has been recorded yet.
</div>
</div>
<div class="incident-card warning-event">
<div class="incident-head">
<div class="incident-title">Last Early Warning</div>
<span class="badge warning">EARLY WARNING</span>
</div>
<div id="lastWarningContent" class="no-event">
No completed early-warning event has been recorded yet.
</div>
</div>
</div>
<h2 style="margin-top:26px;">
Recent Event Timeline
</h2>
<div id="eventTimeline" class="timeline">
<div class="no-event">No incident history recorded yet.</div>
</div>
</div>
<div class="section">
<h2>Air Humidity Incident History</h2>
<div class="info-text">
Tracks high ambient humidity separately from wall moisture.
HIGH HUMIDITY begins at 70%, while DANGER begins at 75%.
</div>
<div
id="activeAirIncident"
class="active-critical active-warning"
style="display:none;">
<div class="active-row">
<div>
<div id="activeAirTitle" class="active-title">ACTIVE AIR HUMIDITY EVENT</div>
<div id="activeAirText" class="info-text" style="margin-top:7px;">--</div>
</div>
<span id="activeAirBadge" class="badge warning">ACTIVE</span>
</div>
</div>
<div class="air-history-grid">
<div class="air-history-card">
<div class="incident-head">
<div class="incident-title">Last Air Humidity Danger</div>
<span class="badge danger">75%+</span>
</div>
<div id="lastAirDangerContent" class="no-event">
No completed air-humidity danger event has been recorded yet.
</div>
</div>
<div class="air-history-card">
<div class="incident-head">
<div class="incident-title">Last High Humidity Event</div>
<span class="badge warning">70-75%</span>
</div>
<div id="lastAirWarningContent" class="no-event">
No completed high-humidity event has been recorded yet.
</div>
</div>
</div>
</div>
<div class="section">
<h2>
Why These Measurements Matter
</h2>
<div class="assessment">
<div class="info">
<div class="info-title">
Moisture Inside the Wall
</div>
<div class="info-text">
High moisture may contribute to
deterioration of binding materials,
salt crystallization,
surface flaking,
mold growth and fungal activity.
</div>
</div>
<div class="info">
<div class="info-title">
Ambient Environment
</div>
<div class="info-text">
Temperature,
relative humidity and atmospheric
pressure help identify environmental
changes that may affect historic
building materials.
</div>
</div>
</div>
</div>
</div>
<div class="footer">
Heritage Conservation Monitoring System
* ESP32
</div>
<script>
function badgeClass(status){
if(
status === "SAFE" ||
status === "STABLE"
)
return "badge safe";
if(
status === "EARLY WARNING" ||
status === "MONITOR" ||
status === "HIGH HUMIDITY" ||
status === "CHANGING"
)
return "badge warning";
if(
status === "DANGER" ||
status === "RAPID CHANGE" ||
status === "LARGE CHANGE"
)
return "badge danger";
return "badge neutral";
}
function formatDuration(seconds){
seconds = Math.max(0, Number(seconds || 0));
const days = Math.floor(seconds / 86400);
seconds %= 86400;
const hours = Math.floor(seconds / 3600);
seconds %= 3600;
const minutes = Math.floor(seconds / 60);
const secs = Math.floor(seconds % 60);
let parts = [];
if(days) parts.push(days + "d");
if(hours) parts.push(hours + "h");
if(minutes) parts.push(minutes + "m");
if(parts.length === 0 || secs)
parts.push(secs + "s");
return parts.join(" ");
}
function formatDateTime(epoch){
if(!epoch) return "--";
const d = new Date(epoch * 1000);
return d.toLocaleString(
undefined,
{
year:"numeric",
month:"2-digit",
day:"2-digit",
hour:"2-digit",
minute:"2-digit",
second:"2-digit"
}
);
}
function incidentReport(event){
if(!event || !event.valid)
return "";
let trend =
event.endMoisture > event.startMoisture ?
"Moisture increased during the event." :
event.endMoisture < event.startMoisture ?
"Moisture decreased before the event ended." :
"Moisture remained approximately stable during the event.";
let previous =
"Before this event, the system remained in " +
event.previousState +
" for " +
formatDuration(event.previousDuration) +
;"."
let action =
event.state === "DANGER" ?
"Inspection of the monitored wall area is recommended. Check for moisture penetration, salt
crystallization, surface flaking, deterioration or mold activity." :
"Continue close monitoring. If the moisture trend continues upward and reaches 60%, the system will
classify the condition as DANGER.";
return previous + " " + trend + " " + action;
}
function eventDetailsHTML(event){
if(!event || !event.valid)
return currentLanguage === "ar" ?
'<div class="no-event">لم يتم تسجيل حدث مكتمل حتى الآن.</div>' :
'<div class="no-event">No completed event has been recorded yet.</div>';
return `
<div class="incident-time">
${formatDateTime(event.start)} &rarr; ${formatDateTime(event.end)}
</div>
<div class="detail-grid">
<div class="detail-item">
<div class="detail-label">${currentLanguage === "ar" ? "المدة" : "Duration"}</div>
<div class="detail-value">${formatDuration(event.duration)}</div>
</div>
<div class="detail-item">
<div class="detail-label">${currentLanguage === "ar" ? "أعلى رطوبة" : "Peak Moisture"}</div>
<div class="detail-value">${event.peakMoisture.toFixed(1)}%</div>
</div>
<div class="detail-item">
البداية " ? "div class="detail-label">${currentLanguage =>Strt / En"}</divالنهاية
<div class="detail-value">${event.startMoisture.toFixed(1)}% / ${event.endMoisture.toFixed(1)}%</div>
</div>
<div class="detail-item">
<div class="detail-label">${currentLanguage === "ar" ? "الحالة السابقة" : "Previous Condition"}</div>
<div class="detail-value">${event.previousState} - ${formatDuration(event.previousDuration)}</div>
</div>
<div class="detail-item">
<div class="detail-label">${currentLanguage === "ar" ? "RAW البداية / الأعلى / النهاية" : "RAW Start / Peak /
End"}</div>
<div class="detail-value">${event.startRaw} / ${event.peakRaw} / ${event.endRaw}</div>
</div>
<div class="detail-item">
<div class="detail-label">${currentLanguage === "ar" ? "درجة الحرارة" : "Temperature"}</div>
<div class="detail-value">${event.startTemperature.toFixed(1)}&deg;C &rarr;
${event.endTemperature.toFixed(1)}&deg;C</div>
</div>
<div class="detail-item">
<div class="detail-label">${currentLanguage === "ar" ? "رطوبة الهواء" : "Air Humidity"}</div>
<div class="detail-value">${event.startHumidity.toFixed(1)}% &rarr;
${event.endHumidity.toFixed(1)}%</div>
</div>
<div class="detail-item">
<div class="detail-label">${currentLanguage === "ar" ? "الضغط" : "Pressure"}</div>
<div class="detail-value">${event.startPressure.toFixed(1)} &rarr; ${event.endPressure.toFixed(1)}
hPa</div>
</div>
</div>
<div class="report-box">
<b>${currentLanguage === "ar" ? "التقييم التلقائي" : "Automatic Assessment"}</b><br>
${incidentReport(event)}
</div>
;`
}
function updateHistory(data){
updateAirHistory(data);
const clockWarning =
document.getElementById("historyClockWarning");
clockWarning.style.display =
data.timeReady ? "none" : "block";
document.getElementById("sumDangerCount").innerText =
data.summary.dangerCount;
document.getElementById("sumWarningCount").innerText =
data.summary.warningCount;
let totalDanger =
data.summary.dangerSeconds;
if(data.current.state === "DANGER")
totalDanger += data.current.duration;
document.getElementById("sumDangerTime").innerText =
formatDuration(totalDanger);
document.getElementById("sumHighest").innerText =
Math.max(
data.summary.highestMoisture || 0,
data.current.currentMoisture || 0
).toFixed(1) + "%";
document.getElementById("lastDangerContent").innerHTML =
eventDetailsHTML(data.lastDanger);
document.getElementById("lastWarningContent").innerHTML =
eventDetailsHTML(data.lastWarning);
const active =
document.getElementById("activeIncident");
if(
data.current.state === "DANGER" ||
data.current.state === "EARLY WARNING"
){
active.style.display = "block";
const isDanger =
data.current.state === "DANGER";
active.className =
isDanger ?
"active-critical" :
"active-critical active-warning";
document.getElementById("activeIncidentBadge").className =
isDanger ?
"badge danger" :
"badge warning";
document.getElementById("activeIncidentBadge").innerText =
translateStatus(
isDanger ?
"ACTIVE DANGER" :
"ACTIVE WARNING"
;)
document.getElementById("activeIncidentTitle").innerText =
currentLanguage === "ar" ?
(
isDanger ?
: "حالة خطر نشطة"
"إنذار مبكر نشط"
: )
(
isDanger ?
"ACTIVE CRITICAL EVENT" :
"ACTIVE EARLY WARNING"
;)
document.getElementById("activeIncidentText").innerHTML =
"Current wall moisture: <b>" +
data.current.currentMoisture.toFixed(1) +
"%</b> &nbsp; | &nbsp; Peak: <b>" +
data.current.peakMoisture.toFixed(1) +
"%</b><br>" +
"Started: <b>" +
formatDateTime(data.current.start) +
"</b> &nbsp; | &nbsp; Active for: <b>" +
formatDuration(data.current.duration) +
"</b><br>" +
"Previous condition: <b>" +
data.current.previousState +
"</b> for <b>" +
formatDuration(data.current.previousDuration) +
"</b>";
}
else{
active.style.display = "none";
}
const timeline =
document.getElementById("eventTimeline");
if(!data.recent || data.recent.length === 0){
timeline.innerHTML =
'<div class="no-event">No completed incident history recorded yet.</div>';
}
else{
timeline.innerHTML =
data.recent.slice(0,6).map(event => {
const stateClass =
event.state === "DANGER" ?
"danger" :
"warning";
return `
<div class="timeline-event">
<div>
<span class="badge ${stateClass}">
${event.state}
</span>
</div>
<div class="timeline-period">
${formatDateTime(event.start)}
<br>
to ${formatDateTime(event.end)}
</div>
<div class="timeline-duration">
${formatDuration(event.duration)}
<br>
Peak ${event.peakMoisture.toFixed(1)}%
</div>
</div>
`;
}).join("");
}
}
async function syncDeviceClock(){
try{
await fetch(
"/settime?epoch=" +
Math.floor(Date.now()/1000),
{cache:"no-store"}
);
}
catch(error){
console.log("Clock sync failed", error);
}
}
async function loadHistory(){
try{
const response =
await fetch(
"/history?t=" +
Date.now(),
{cache:"no-store"}
);
const data =
await response.json();
updateHistory(data);
translatePage();
}
catch(error){
console.log("History load error", error);
}
}
let lastAlertState = "SAFE";
let notificationEnabled = false;
let lastTrendData = null;
function printReport(){
const now = new Date();
let stamp =
now.toLocaleString(
currentLanguage === "ar" ? "ar-BH" : undefined,
{
year:"numeric",
month:"2-digit",
day:"2-digit",
hour:"2-digit",
minute:"2-digit"
}
);
let existing =
document.getElementById("printReportHeader");
if(existing)
existing.remove();
const reportHeader =
document.createElement("div");
reportHeader.id =
"printReportHeader";
reportHeader.style.cssText =
"display:none;";
reportHeader.innerHTML =
`
<div style="text-align:center;margin-bottom:18px;border-bottom:2px solid #000;padding-
bottom:10px;">
<h1 style="margin:0 0 5px;font-size:22px;">
${currentLanguage === "ar" ? "تقرير نظام مراقبة الجدار التراثي" : "Heritage Wall Monitoring System Report"}
</h1>
<div style="font-size:12px;">
${currentLanguage === "ar" ? "تاريخ ووقت التقرير: " : "Report Date & Time: "}
${stamp}
</div>
</div>
;`
document.body.insertBefore(
reportHeader,
document.body.firstChild
;)
const oldDisplay =
reportHeader.style.display;
window.addEventListener(
"beforeprint",
{>=)(
reportHeader.style.display = "block";
},
{once:true}
);
window.addEventListener(
"afterprint",
()=>{
reportHeader.remove();
},
{once:true}
);
window.print();
}
function requestNotifications(){
if(!("Notification" in window)){
alert("Browser notifications are not supported on this device.");
return;
}
Notification.requestPermission().then(permission=>{
notificationEnabled = permission === "granted";
if(notificationEnabled)
alert("Automatic browser alerts are enabled while this dashboard is open.");
});
}
function maybeNotify(state, wall){
if(!notificationEnabled ||
Notification.permission !== "granted")
return;
if(
(state === "EARLY WARNING" || state === "DANGER") &&
state !== lastAlertState
){
new Notification(
state === "DANGER" ?
"Heritage Wall - DANGER" :
"Heritage Wall - Early Warning",
{
body:
"Wall moisture is " +
wall.toFixed(1) +
"%."
}
);
}
lastAlertState = state;
}
async function manualRefresh(button=null){
if(button){
button.disabled = true;
button.innerText = "Refreshing...";
}
try{
await fetch(
"/refresh?t=" + Date.now(),
{cache:"no-store"}
);
await loadData();
await loadHistory();
await loadTrend();
}
catch(error){
console.log("Manual refresh failed", error);
}
if(button){
button.disabled = false;
button.innerText = "Refresh Sensors";
}
}
function wifiBarsFromRssi(rssi){
if(rssi >= -55) return 4;
if(rssi >= -67) return 3;
if(rssi >= -75) return 2;
if(rssi >= -85) return 1;
return 0;
}
function updateWifiMeter(rssi){
const bars =
document.querySelectorAll("#wifiMeter .wifi-bar");
const active =
wifiBarsFromRssi(rssi);
bars.forEach((bar,index)=>{
bar.classList.toggle("on", index < active);
;)}
}
function updateComparisons(data){
let wallText;
if(data.wall < 40){
wallText =
(40 - data.wall).toFixed(1) +
(currentLanguage === "ar" ?
: "% 40 أقل من حد التحذير %"
"% below the 40% warning threshold");
}
else if(data.wall < 60){
wallText =
(data.wall - 40).toFixed(1) +
(currentLanguage === "ar" ?
: "أعلى من حد التحذير %"
"% above the warning threshold");
}
else{
wallText =
(data.wall - 60).toFixed(1) +
(currentLanguage === "ar" ?
: "أعلى من حد الخطر %"
"% above the danger threshold");
}
document.getElementById("wallCompare").innerText =
wallText;
let humidityText;
if(data.humidity < 40){
humidityText =
(40 - data.humidity).toFixed(1) +
(currentLanguage === "ar" ?
: "أقل من الحد الأدنى المفضل %"
"% below preferred minimum");
}
else if(data.humidity <= 60){
humidityText =
currentLanguage === "ar" ?
: "ضمن النطاق المفضل"
"Within preferred range";
}
else{
humidityText =
(data.humidity - 60).toFixed(1) +
(currentLanguage === "ar" ?
: "أعلى من الحد الأعلى المفضل %"
"% above preferred maximum");
}
document.getElementById("humidityCompare").innerText =
humidityText;
document.getElementById("pressureCompare").innerText =
translateStatus(data.pressureStatus) +
" (" +
data.pressure.toFixed(1) +
" hPa)";
}
function updateOverallStatus(data){
const banner =
document.getElementById("overallBanner");
const state =
data.overallStatus || data.wallStatus;
document.getElementById("overallState").innerText =
translateStatus(state);
banner.className =
"overall-banner " +
(
state === "DANGER" ?
"overall-danger" :
state === "WARNING" ?
"overall-warning" :
"overall-safe"
;)
let reason;
if(state === "DANGER"){
reason =
bilingual(
"One or more critical thresholds are currently exceeded. Review wall and environmental readings
immediately.",
. راجع قراءات الجدار والبيئة فورا "
".تم تجاوز حد أو أكثر من الحدود الحرجة حاليا
;)
}
else if(state === "WARNING"){
reason =
bilingual(
"At least one reading requires closer monitoring, but the overall condition is not yet critical.",
".هناك قراءة واحدة على الأقل تحتاج إلى متابعة أقرب، لكن الحالة العامة لم تصل إلى مستوى الخطر"
;)
}
else{
reason =
bilingual(
"Current readings are within the configured safe monitoring conditions.",
".القراءات الحالية ضمن حدود المراقبة الآمنة المحددة"
;)
}
document.getElementById("overallReason").innerText =
reason;
}
function drawBarChart(canvasId, samples, field, unit, minFixed, maxFixed){
const canvas =
document.getElementById(canvasId);
if(!canvas) return;
const parent =
canvas.parentElement;
const width =
Math.max(320, parent.clientWidth);
const height =
Math.max(200, parent.clientHeight);
const ratio =
window.devicePixelRatio || 1;
canvas.width = width * ratio;
canvas.height = height * ratio;
const ctx =
canvas.getContext("2d");
ctx.setTransform(ratio,0,0,ratio,0,0);
ctx.clearRect(0,0,width,height);
const padLeft = 46;
const padRight = 18;
const padTop = 18;
const padBottom = 42;
const chartW = width - padLeft - padRight;
const chartH = height - padTop - padBottom;
if(!samples || samples.length === 0){
ctx.fillStyle = "#91a9bd";
ctx.font = "13px Segoe UI";
ctx.fillText(
currentLanguage === "ar" ? "جار ٍ جمع البيانات التاريخية..." : "Collecting historical samples...",
padLeft,
height/2
;)
return;
}
// Keep charts readable by showing at most 24 representative bars.
let displaySamples = samples;
if(samples.length > 24){
const step = Math.ceil(samples.length / 24);
displaySamples = [];
for(let i=0; i<samples.length; i += step){
const group = samples.slice(i, Math.min(i + step, samples.length));
const avg = {
t: group[Math.floor(group.length/2)].t
};
avg[field] =
group.reduce((sum,s)=>sum + Number(s[field]),0) / group.length;
displaySamples.push(avg);
}
}
const values =
displaySamples.map(s=>Number(s[field]));
let min =
minFixed !== null ?
minFixed :
Math.min(...values);
let max =
maxFixed !== null ?
maxFixed :
Math.max(...values);
if(max - min < 0.1){
max += 1;
min -= 1;
}
if(minFixed === null){
const margin = (max-min) * 0.12 || 1;
min -= margin;
max += margin;
}
const yFor =
v =>
padTop +
(max-v)/(max-min) * chartH;
// Grid + Y labels
ctx.font = "11px Segoe UI";
ctx.textAlign = "right";
ctx.textBaseline = "middle";
for(let i=0;i<5;i++){
const value =
max - (max-min)*(i/4);
const y =
padTop + chartH*(i/4);
ctx.strokeStyle = "rgba(255,255,255,.11)";
ctx.lineWidth = 1;
ctx.beginPath();
ctx.moveTo(padLeft,y);
ctx.lineTo(width-padRight,y);
ctx.stroke();
ctx.fillStyle = "#91a9bd";
ctx.fillText(
value.toFixed(field === "pressure" ? 0 : 1),
padLeft-7,
y
);
}
// Axes
ctx.strokeStyle = "rgba(255,255,255,.35)";
ctx.beginPath();
ctx.moveTo(padLeft,padTop);
ctx.lineTo(padLeft,padTop+chartH);
ctx.lineTo(width-padRight,padTop+chartH);
ctx.stroke();
const count = displaySamples.length;
const gap = Math.max(3, chartW * 0.015);
const availablePerBar = chartW / Math.max(1,count);
const barWidth = Math.max(5, Math.min(34, availablePerBar - gap));
// Draw bars
displaySamples.forEach((s,i)=>{
const v = Number(s[field]);
const centerX =
padLeft + availablePerBar*(i+0.5);
const x =
centerX - barWidth/2;
const y =
yFor(v);
const zeroY =
yFor(Math.max(min, Math.min(max, 0)));
const baseY =
(min <= 0 && max >= 0) ? zeroY : padTop + chartH;
const top = Math.min(y,baseY);
const h = Math.max(2,Math.abs(baseY-y));
let fill;
if(field === "wall"){
fill =
v >= 60 ? "#ef4444" :
v >= 40 ? "#f59e0b" :
"#22c55e";
}
else if(field === "humidity"){
fill =
v >= 75 ? "#ef4444" :
v >= 70 ? "#f59e0b" :
"#4f93e8";
}
else{
fill = "#4f93e8";
}
ctx.fillStyle = fill;
ctx.fillRect(x,top,barWidth,h);
});
// X labels: first, middle, last only
ctx.fillStyle = "#91a9bd";
ctx.font = "11px Segoe UI";
ctx.textBaseline = "top";
const labelIndices = [...new Set([0, Math.floor((count-1)/2), count-1])];
labelIndices.forEach((idx,pos)=>{
if(idx < 0 || !displaySamples[idx]) return;
const d = new Date(displaySamples[idx].t*1000);
const label =
d.toLocaleString(
currentLanguage === "ar" ? "ar-BH" : undefined,
{month:"short",day:"numeric",hour:"2-digit",minute:"2-digit"}
);
const x =
padLeft + availablePerBar*(idx+0.5);
ctx.textAlign =
pos===0 ? "left" :
pos===labelIndices.length-1 ? "right" :
"center";
ctx.fillText(label,x,height-padBottom+10);
});
}
function translatePredictionText(text){
if(currentLanguage !== "ar")
return text;
if(!text)
return "";
const exact = {
"Collecting historical samples before a trend estimate can be calculated.":
,".جار ٍ جمع بيانات تاريخية قبل حساب تقدير الاتجاه"
"The wall is already in the DANGER range. Prediction is not required; immediate inspection is
recommended.":
,".الجدار موجود حاليا ضمن نطاق الخطر. لا حاجة للتوقع، ويُنصح بالفحص الفوري"
"Moisture is currently stable. No meaningful rising trend is detected.":
,".الرطوبة مستقرة حاليا ، ولا يوجد اتجاه ارتفاع ملحوظ"
"Moisture is trending downward. No rising-risk estimate is currently indicated.":
,".الرطوبة تتجه للانخفاض، ولا يوجد حاليا تقدير لخطر صاعد"
"The threshold has effectively been reached.":
,".تم الوصول عمليا إلى الحد المحدد"
"Moisture is rising slowly, but the next threshold is not projected within the next 72 hours.":
". ساعة
72
الرطوبة ترتفع ببطء، لكن لا يُتوقع الوصول إلى الحد التالي خلال "
;}
if(exact[text])
return exact[text];
if(text.startsWith("Trend estimate: if the recent rate continues")){
return text
تقدير الاتجاه: إذا استمر المعدل "," replace("Trend estimate: if the recent rate continues, the wall may reach.
)" الحالي فقد يصل الجدار إلى
)" خلال حوالي "," replace(" in approximately.
ساعة. هذا تقدير اتجاه مبسط وليس ",".replace(" hours. This is a simple trend estimate, not a scientific forecast.
;)".توقعا علميا
}
return text;
}
function renderCharts(data){
lastTrendData = data;
const samples =
data.samples || [];
drawBarChart(
"wallChart",
samples,
"wall",
,"%"
,0
100
);
drawBarChart(
"tempChart",
samples,
"temperature",
"C",
null,
null
);
drawBarChart(
"humidityChart",
samples,
"humidity",
"%",
0,
100
);
drawBarChart(
"pressureChart",
samples,
"pressure",
"hPa",
null,
null
);
document.getElementById("predictionBox").innerText =
translatePredictionText(
data.prediction ||
currentLanguage === "ar" ?
: ".لا يتوفر تقدير للاتجاه حتى الآن"
"No trend estimate is available yet."
(
)
;)
}
async function loadTrend(){
try{
const response =
await fetch(
"/trend?t=" +
Date.now(),
{cache:"no-store"}
;)
const data =
await response.json();
renderCharts(data);
translatePage();
}
catch(error){
console.log("Trend load error", error);
}
}
window.addEventListener("resize",()=>{
if(lastTrendData)
renderCharts(lastTrendData);
});
function updatePowerStatus(data){
const fill =
document.getElementById("batteryFill");
const status =
document.getElementById("powerStatusText");
const details =
document.getElementById("batteryDetails");
if(!fill || !status || !details)
return;
if(!data.batteryEnabled){
fill.style.width = "100%";
fill.style.background = "#72a8ff";
status.innerText =
currentLanguage === "ar" ?
: "طاقة خارجية / USB"
"USB / External Power";
details.innerText =
"Battery voltage sensing is not configured. Enable it in the code only after wiring a safe voltage divider
to GPIO35.";
return;
}
const pct =
Math.max(0, Math.min(100, Number(data.batteryPercent || 0)));
fill.style.width =
pct + "%";
fill.style.background =
pct <= 15 ?
"#ef4444" :
pct <= 35 ?
"#f59e0b" :
"#22c55e";
status.innerText =
pct.toFixed(0) + "% - " +
translateStatus(data.powerSource);
details.innerText =
Number(data.batteryVoltage || 0).toFixed(2) +
" V";
}
function updateWallMap(data){
const zone =
document.getElementById("wallZone");
if(!zone)
return;
document.getElementById("wallZoneValue").innerText =
data.wall.toFixed(1) + "%";
document.getElementById("wallZoneState").innerText =
translateStatus(data.wallStatus);
if(data.wallStatus === "DANGER")
zone.className = "wall-zone danger-zone";
else if(data.wallStatus === "EARLY WARNING")
zone.className = "wall-zone warning-zone";
else
zone.className = "wall-zone safe-zone";
}
function airEventReport(event){
if(!event || !event.valid)
return "";
let action =
event.state === "DANGER" ?
"Ambient humidity remained at or above the configured 75% danger threshold. Inspect for
condensation risk and continue close environmental monitoring." :
"Ambient humidity entered the 70-75% high-humidity range. Continue monitoring to detect whether it
progresses toward the 75% danger threshold.";
return (
"Before this event, the air condition remained in " +
event.previousState +
" for " +
formatDuration(event.previousDuration) +
+ " ."
action
;)
}
function airEventDetailsHTML(event){
if(!event || !event.valid)
return currentLanguage === "ar" ?
'<div class="no-event">لم يتم تسجيل حدث مكتمل حتى الآن.</div>' :
'<div class="no-event">No completed event has been recorded yet.</div>';
return `
<div class="incident-time">
${formatDateTime(event.start)} &rarr; ${formatDateTime(event.end)}
</div>
<div class="detail-grid">
<div class="detail-item">
<div class="detail-label">${currentLanguage === "ar" ? "المدة" : "Duration"}</div>
<div class="detail-value">${formatDuration(event.duration)}</div>
</div>
<div class="detail-item">
<div class="detail-label">${currentLanguage === "ar" ? "أعلى رطوبة هواء" : "Peak Air Humidity"}</div>
<div class="detail-value">${event.peakHumidity.toFixed(1)}%</div>
</div>
<div class="detail-item">
<div class="detail-label">${currentLanguage === "ar" ? "رطوبة البداية / النهاية" : "Start / End
Humidity"}</div>
<div class="detail-value">${event.startHumidity.toFixed(1)}% / ${event.endHumidity.toFixed(1)}%</div>
</div>
<div class="detail-item">
<div class="detail-label">${currentLanguage === "ar" ? "الحالة السابقة" : "Previous Condition"}</div>
<div class="detail-value">${event.previousState} - ${formatDuration(event.previousDuration)}</div>
</div>
<div class="detail-item">
<div class="detail-label">${currentLanguage === "ar" ? "درجة الحرارة" : "Temperature"}</div>
<div class="detail-value">${event.startTemperature.toFixed(1)}&deg;C &rarr;
${event.endTemperature.toFixed(1)}&deg;C</div>
</div>
<div class="detail-item">
<div class="detail-label">${currentLanguage === "ar" ? "الضغط" : "Pressure"}</div>
<div class="detail-value">${event.startPressure.toFixed(1)} &rarr; ${event.endPressure.toFixed(1)}
hPa</div>
</div>
</div>
<div class="report-box">
<b>${currentLanguage === "ar" ? "التقييم التلقائي لرطوبة الهواء" : "Automatic Air-Humidity
Assessment"}</b><br>
${airEventReport(event)}
</div>
`;
}
function updateAirHistory(data){
const dangerBox =
document.getElementById("lastAirDangerContent");
const warningBox =
document.getElementById("lastAirWarningContent");
if(dangerBox)
dangerBox.innerHTML =
airEventDetailsHTML(data.lastAirDanger);
if(warningBox)
warningBox.innerHTML =
airEventDetailsHTML(data.lastAirWarning);
const active =
document.getElementById("activeAirIncident");
if(!active || !data.airCurrent)
return;
if(
data.airCurrent.state === "DANGER" ||
data.airCurrent.state === "HIGH HUMIDITY"
){
active.style.display = "block";
const danger =
data.airCurrent.state === "DANGER";
active.className =
danger ?
"active-critical" :
"active-critical active-warning";
document.getElementById("activeAirBadge").className =
danger ?
"badge danger" :
"badge warning";
document.getElementById("activeAirBadge").innerText =
translateStatus(
danger ?
"ACTIVE DANGER" :
"ACTIVE HIGH HUMIDITY"
);
document.getElementById("activeAirTitle").innerText =
currentLanguage === "ar" ?
(
danger ?
: "خطر رطوبة هواء نشط"
"رطوبة هواء مرتفعة نشطة"
: )
(
danger ?
"ACTIVE AIR HUMIDITY DANGER" :
"ACTIVE HIGH HUMIDITY"
;)
document.getElementById("activeAirText").innerHTML =
"Current air humidity: <b>" +
data.airCurrent.currentHumidity.toFixed(1) +
"%</b> &nbsp; | &nbsp; Peak: <b>" +
data.airCurrent.peakHumidity.toFixed(1) +
"%</b><br>" +
"Started: <b>" +
formatDateTime(data.airCurrent.start) +
"</b> &nbsp; | &nbsp; Active for: <b>" +
formatDuration(data.airCurrent.duration) +
"</b><br>" +
"Previous air condition: <b>" +
data.airCurrent.previousState +
"</b> for <b>" +
formatDuration(data.airCurrent.previousDuration) +
"</b>";
}
else{
active.style.display = "none";
}
}
const AR_TRANSLATIONS = {
,"نظام مراقبة الجدار التراثي":"Heritage Wall Monitoring System"
مراقبة بيئية مباشر ":"ESP32 Live Environmental Monitoring"
,"ESP32 ة باستخدام
,"مراقبة مباشرة":"LIVE MONITORING"
,"التقييم العام للنظام":"Overall System Assessment"
,"...جار ٍ تقييم حالة الجدار والبيئة":"...Evaluating wall and environmental conditions"
,"تحديث الحساسات":"Refresh Sensors"
," بيهات تفعيل التن ":"Enable Alerts"
,"رطوبة الجدار":"Wall Moisture"
,"رطوبة الهواء":"Air Humidity"
,"درجة الحرارة":"Temperature"
,"الضغط الجوي":"Atmospheric Pressure"
,"حالة الجدار":"Wall Condition"
,"تقييم النظام":"System Assessment"
,"تقييم رطوبة الجدار":"Wall Moisture Assessment"
,"تقييم البيئة":"Environmental Assessment"
,"نظرة عامة على المراقبة":"Monitoring Overview"
,"الجدار":"Wall"
,"البيئة":"Environment"
,"تقني":"Technical"
,"الطاقة والحالة التقنية":"Power & Technical Status"
,"مصدر الطاقة":"Power Source"
,"اتصال النظام":"System Connectivity"
,"المقارنة مع القيم المفضلة":"Comparison With Preferred Ranges"
,"رطوبة الجدار مقارنة بحد التحذير":"Wall Moisture vs Warning Limit"
رطوبة الهواء مقارنة بالطاق المفضل ":Air Humidity vs Prefer"
," غط اتجاه الض ":"Pressure Trend"
لتجاهات لبيئية ":"Environmental Trends,"تقدير اتجاه الخطر":"Risk Trend Estimate"
,"خريطة رطوبة الجدار":"Wall Moisture Map"
,"سجل الأحداث وتحليل المخاطر":"Incident History & Risk Analysis"
حالا ":"Danger Events Today"
," ت الخطر اليوم
,"الإنذارات المبكرة اليوم":"Early Warnings Today"
,"إجمالي مدة الخطر":"Total Danger Time"
,"أعلى رطوبة اليوم":"Highest Moisture Today"
,"آخر حالة خطر":"Last Danger Event"
,"آخر إنذار مبكر":"Last Early Warning"
الخط ":"Recent Event Timeline"
," الزمني للأحداث الأخيرة
,"سجل حالات رطوبة الهواء":"Air Humidity Incident History"
,"آخر حالة خطر لرطوبة الهواء":"Last Air Humidity Danger"
,"آخر حالة رطوبة هواء مرتفعة":"Last High Humidity Event"
,"لماذا هذه القياسات مهمة":"Why These Measurements Matter"
,"الرطوبة داخل الجدار":"Moisture Inside the Wall"
,"البيئة المحيطة":"Ambient Environment"
"Sensor RAW:":"قيمة الحساس RAW:",
"BME280:":"BME280:",
,":الواي فاي":":Wi-Fi"
,":آخر تحديث":":Last Update"
حالة رطوبة الجدار وحدود التحذير ":".Wall moisture condition, threshold status and recent moisture trend"
,".والاتجاه الأخير للرطوبة
درجة الحرارة ورطوبة ":".Temperature, ambient humidity and atmospheric pressure from the BME280"
,".BME280 الهواء والضغط الجوي من حساس
قوة الواي فاي وحالة الحساس وقيمة":".Wi-Fi signal, sensor availability, RAW value, clock and update status"
,".والساعة وحالة التحديث RAW
"Battery voltage sensing is not configured. Enable it in the code only after wiring a safe voltage divider
فعّله فقط بعد ,".GPIO35 وصيل م.
قياس جهد البطارية غير مفعّل حاليا
"Wi-Fi quality is shown graphically in the live status area. Sensor and web-server status update
يتم عرض جودة الواي فاي بشكل رسومي، ويتم تحديث حالة الحساسات والسيرفر تلقائيا كل ":".automatically every 2 seconds
,".ثانيتين
"A new historical sample is stored every 10 minutes. Charts build automatically as the ESP32 collects
10 يتم حف,". دقائق، وتُبنى الرسوم البيايا مع جمع البياظ قراءة تاريخية"The current project uses one wall-moisture sensor, so the highlighted area represents the monitored
المشروع الحالي يستخدم حساس ":".sensor zone. More zones can be added later by using additional sensors
,". ية
. يمكن إضافة مناطق أخرى لاحقا باستخدام حساسات إضاف
رطوبة واحد للجدار، لذلك تمثل المنطقة المظللة منطقة الحساس الحالية
"Detailed history of the latest danger and early-warning events, including date, time, duration, previous
سجل تفصيلي لآخر حالات الخطر والإنذار المبكر، ويشمل التاريخ والوقت والمدة ":".condition and environmental readings
,".والحالة السابقة وقراءات البيئة
"Tracks high ambient humidity separately from wall moisture. HIGH HUMIDITY begins at 70%, while
70 تتم متابعة رطوبة الهوا%، وتبدأ
تقل عن رطوبة الجدار. تبدأ الرطوبة المرتفعة عندDANGER begins at
حالة الخطر عند
,".% 75
,".لم يتم تسجيل حالة خطر مكتملة حتى الآن":".No completed danger event has been recorded yet"
,".لم يتم تسجيل إنذار مبكر مكتمل حتى الآن":".No completed early-warning event has been recorded yet"
,".لا يوجد سجل أحداث مكتمل حتى الآن":".No completed incident history recorded yet"
لم يتم تسجيل حالة خطر مكتملة لرطوبة الهواء ":".No completed air-humidity danger event has been recorded yet"
,".حتى الآن
لم يتم تسجيل حالة رطوبة هواء مرتفعة مكتملة حتى ":".No completed high-humidity event has been recorded yet"
,".الآن
جار ٍ جمع بيانات تاريخية كافية ":"...Collecting enough historical data to estimate the recent moisture trend"
,"...لتقدير اتجاه الرطوبة الأخير
,"آمن":"SAFE"
,"تحذير":"WARNING"
,"خطر":"DANGER"
,"إنذار مبكر":"EARLY WARNING"
,"مراقبة":"MONITOR"
,"رطوبة مرتفعة":"HIGH HUMIDITY"
,"هواء جاف":"DRY AIR"
,"مستقر":"STABLE"
,"تغير سريع":"RAPID CHANGE"
,"متغير":"CHANGING"
,"تغير كبير":"LARGE CHANGE"
,"طبيعي":"NORMAL"
,"متصل":"ONLINE"
,"غير متصل":"OFFLINE"
,"الحساس غير متصل":"SENSOR OFFLINE"
,"جار ٍ التحميل":"Loading"
,"...جار ٍ تحميل حالة الجدار":"...Loading wall condition"
,"...بانتظار بيانات الحساس":"...Waiting for sensor data"
"Waiting for BME280 data...":"بانتظار بيانات BME280...",
,"...جار ٍ فحص حالة الطاقة":"...Checking power status"
,"طاقة خارجية / USB / External Power":"USB"
,")%( رطوبة الجدار":")%( Wall Moisture"
,")%( رطوبة الهواء":")%( Air Humidity"
"Temperature (°C)":"درجة الحرارة (°C)",
"Atmospheric Pressure (hPa)":"الضغط الجوي (hPa)",
آمن < ":"%40< Safe"
,"% 40
تحذير ":"%60-40 Warning"
,"% 60 -40
,"+% 60 خطر ":"+%60 Danger"
,"طباعة التقرير":"Print Report"
"ZONE A":"المنطقة A",
"Device clock is not synchronized yet. Open this dashboard while connected to the ESP32 network to
لمزامنة ESP32 ساعة الجهاز غير متزامنة بعد. افتح لوحة التحكم أثناء الاتصال بشبكة":".synchronize the date and time
,". تاريخ والوقت ال
,"حدث نشط":"ACTIVE INCIDENT"
,"حالة خطر نشطة":"ACTIVE CRITICAL EVENT"
,"إنذار مبكر نشط":"ACTIVE EARLY WARNING"
,"حالة رطوبة هواء نشطة":"ACTIVE AIR HUMIDITY EVENT"
,"خطر رطوبة هواء نشط":"ACTIVE AIR HUMIDITY DANGER"
,"رطوبة هواء مرتفعة نشطة":"ACTIVE HIGH HUMIDITY"
,"المدة":"Duration"
,"أعلى رطوبة":"Peak Moisture"
,"البداية / النهاية":"Start / End"
,"الحالة السابقة":"Previous Condition"
,"البداية / الأعلى / النهاية RAW Start / Peak / End":"RAW"
,"الضغط":"Pressure"
,"التقييم التلقائي":"Automatic Assessment"
,"أعلى رطوبة هواء":"Peak Air Humidity"
,"رطوبة البداية / النهاية":"Start / End Humidity"
,"التقييم التلقائي لرطوبة الهواء":"Automatic Air-Humidity Assessment"
لم يتم تسجيل ":".No completed event has been r,". حدث مك,"...جار ٍ جمع البيانات التاريخية":"...Collecting historical samples"
,"نظام مراقبة وحماية المباني التراثية":"Heritage Conservation Monitoring System"
رطوبة الجدار ":".Wall moisture is below 40%. No immediate moisture risk %أقل من
,".خطر رطوبة فوري
"Wall moisture is between 40% and 60%. This is an early-warning zone and should be
,". %. هذه منطقة إنذار مبكر ويجب متابعتها40 رطوبة الجدار بين ":".itored
% و
رطوبة الجدار ":".Wall moisture is 60% or higher. Inspection of the affected wall area is recommended"
". % أو أعلى. يُنصح بفحص منطقة الجدار المتأ,".غير متصل حاليا BME280 حساس البيئة":".BME280 environmental sensor is currently offline"
"Air humidity is extremely high. Long-term exposure may encourage condensation and material
التعرض الطويل قد يسبب التكثف ويساهم في تدهور المواد":".deterioration
.
,".رطوبة الهواء مرتفعة جدا
تم اكتشاف رطوبة هواء مرتفعة. ":".High ambient humidity detected. Continued monitoring is recommended"
,".يُنصح بمواصلة المراقبة
الرطوبة النسبية للهواء ضمن نطاق ":".Ambient relative humidity is within the preferred monitoring range"
,".المراقبة المفضل
رطوبة الهواء خارج انطاق الفضل ":".Ambient humidity is outside the 40–60% pre"High moisture may contribute to deterioration of binding materials, salt crystallization, surface flaking,
قد تساهم الرطوبة المرتفعة في تدهور المواد الرابطة وتبلور الأملاح وتقشر السطح ونمو ":".mold growth and fungal activity
,".العفن والفطريات
"Temperature, relative humidity and atmospheric pressure help identify environmental changes that
تساعد درجة الحرارة والرطوبة النسبية والضغط الجوي في تحديد التغيرات البيئية ":".may affect historic building materials
".التي قد تؤثر في مواد المباني التراثية
;}
let currentLanguage = localStorage.getItem("heritageLang") || "en";
function translateStatus(status){
if(currentLanguage !== "ar")
return status;
const map = {
,"آمن":"SAFE"
,"تحذير":"WARNING"
,"خطر":"DANGER"
,"إنذار مبكر":"EARLY WARNING"
مرا ":"MONITOR"
," قبة
,"رطوبة مرتفعة":"HIGH HUMIDITY"
,"هواء جاف":"DRY AIR"
,"مستقر":"STABLE"
,"تغير سريع":"RAPID CHANGE"
,"متغير":"CHANGING"
,"تغير كبير":"LARGE CHANGE"
,"طبيعي":"NORMAL"
,"الحساس غير متصل":"SENSOR OFFLINE"
,"متصل":"ONLINE"
,"غير متصل":"OFFLINE"
,"نشط":"ACTIVE"
,"خطر نشط":"ACTIVE DANGER"
,"تحذير نشط":"ACTIVE WARNING"
,"رطوبة مرتفعة نشطة":"ACTIVE HIGH HUMIDITY"
,"طاقة خارجية / USB / EXTERNAL POWER":"USB"
,"البطارية منخفضة":"BATTERY LOW"
"BATTERY / UPS":"بطارية / UPS"
;}
return map[status] || status;
}
function translateTextNode(node){
if(!node || node.nodeType !== Node.TEXT_NODE) return;
if(!node.parentElement) return;
const tag = node.parentElement.tagName;
if(tag === "SCRIPT" || tag === "STYLE") return;
if(node.__originalText === undefined)
node.__originalText = node.nodeValue;
if(currentLanguage === "en"){
node.nodeValue = node.__originalText;
return;
}
const original = node.__originalText;
const trimmed = original.trim();
if(!trimmed) return;
if(AR_TRANSLATIONS[trimmed]){
const leading = original.match(/^\s*/)[0];
const trailing = original.match(/\s*$/)[0];
node.nodeValue = leading + AR_TRANSLATIONS[trimmed] + trailing;
}
}
function translatePage(){
const walker = document.createTreeWalker(
document.body,
NodeFilter.SHOW_TEXT
);
const nodes = [];
while(walker.nextNode()) nodes.push(walker.currentNode);
nodes.forEach(translateTextNode);
document.documentElement.lang = currentLanguage;
document.body.classList.toggle("rtl", currentLanguage === "ar");
const btn = document.getElementById("languageToggle");
;"العربية" : "if(btn) btn.innerText = currentLanguage === "ar" ? "English
const printBtn = document.getElementById("printReportBtn");
if(printBtn)
printBtn.innerText = currentLanguage === "ar" ? "طباعة التقرير" : "Print Report";
const alertsBtn = document.getElementById("enableAlertsBtn");
if(alertsBtn)
alertsBtn.innerText = currentLanguage === "ar" ? "تفعيل التنبيهات" : "Enable Alerts";
}
function toggleLanguage(){
currentLanguage =
currentLanguage === "en" ? "ar" : "en";
localStorage.setItem(
"heritageLang",
currentLanguage
;)
// Reload the original dashboard HTML.
// This prevents Arabic text replacements from remaining when switching back to English.
window.location.reload();
}
function bilingual(en, ar){
return currentLanguage === "ar" ? ar : en;
}
function updateDashboard(data){
document.getElementById("wall").innerText =
data.wall.toFixed(1);
document.getElementById("wallLarge").innerText =
data.wall.toFixed(1);
document.getElementById("raw").innerText =
data.raw;
updateOverallStatus(data);
updateComparisons(data);
maybeNotify(data.wallStatus, data.wall);
updatePowerStatus(data);
updateWallMap(data);
document.getElementById("humidity").innerText =
data.bme ?
data.humidity.toFixed(1) :
"
--";
document.getElementById("temperature").innerText =
data.bme ?
data.temperature.toFixed(1) :
"
--";
document.getElementById("pressure").innerText =
data.bme ?
data.pressure.toFixed(1) :
"
--";
let wallBadge =
document.getElementById("wallBadge");
wallBadge.innerText =
translateStatus(data.wallStatus);
wallBadge.className =
badgeClass(data.wallStatus);
let airBadge =
document.getElementById("airBadge");
airBadge.innerText =
data.bme ?
translateStatus(data.airStatus) :
translateStatus("SENSOR OFFLINE");
airBadge.className =
badgeClass(data.airStatus);
let temperatureBadge =
document.getElementById(
"temperatureBadge"
);
temperatureBadge.innerText =
data.bme ?
translateStatus(data.temperatureStatus) :
translateStatus("SENSOR OFFLINE");
temperatureBadge.className =
badgeClass(
data.temperatureStatus
);
let pressureBadge =
document.getElementById(
"pressureBadge"
);
pressureBadge.innerText =
data.bme ?
translateStatus(data.pressureStatus) :
translateStatus("SENSOR OFFLINE");
pressureBadge.className =
badgeClass(
data.pressureStatus
);
let wallBar =
document.getElementById("wallBar");
wallBar.style.width =
data.wall + "%";
if(data.wall < 40){
wallBar.style.background =
"#22c55e";
}
else if(data.wall < 60){
wallBar.style.background =
"#f59e0b";
}
else{
wallBar.style.background =
"#ef4444";
}
let alert =
document.getElementById(
"wallAlert"
;)
if(data.wall < 40){
alert.className =
"alert-box alert-safe";
alert.innerHTML =
bilingual(
"<b>SAFE CONDITION</b><br>Wall moisture is currently within the safe range.",
".رطوبة الجدار حاليا ضمن النطاق الآمن>b><br/<حالة آمنة>b<"
;)
}
else if(data.wall < 60){
alert.className =
"alert-box alert-warning";
alert.innerHTML =
bilingual(
"<b>EARLY WARNING</b><br>Moisture has entered the monitoring range. Closer observation is
recommended.",
دخلت رطوبة الجدار نطاق ". قبة عن قرب
صح بالمرا مبكر>b<"
;)
}
else{
alert.className =
"alert-box alert-danger";
alert.innerHTML =
bilingual(
"<b>DANGER — HIGH WALL MOISTURE</b><br>High moisture may increase the risk of material
deterioration, salt crystallization, surface flaking, mold and fungal growth.",
خطر >b<"
قد تزي الرطوبة المر—
عة من خ".العفن والفطريات
ونمو
طر تدهور المواد ;)
}
let wallAssessment =
document.getElementById(
"wallAssessment"
);
if(data.wall < 40){
wallAssessment.innerHTML =
"Wall moisture is below 40%. " +
"No immediate moisture risk is detected.";
}
else if(data.wall < 60){
wallAssessment.innerHTML =
"Wall moisture is between 40% and 60%. " +
"This is an early-warning zone and should be monitored.";
}
else{
wallAssessment.innerHTML =
"Wall moisture is 60% or higher. " +
"Inspection of the affected wall area is recommended.";
}
let env =
document.getElementById(
"environmentAssessment"
);
if(!data.bme){
env.innerHTML =
"BME280 environmental sensor is currently offline.";
}
else if(data.humidity >= 75){
env.innerHTML =
"Air humidity is extremely high. " +
"Long-term exposure may encourage condensation and material deterioration.";
}
else if(data.humidity >= 70){
env.innerHTML =
"High ambient humidity detected. " +
"Continued monitoring is recommended.";
}
else if(
data.humidity >= 40 &&
data.humidity <= 60
){
env.innerHTML =
"Ambient relative humidity is within the preferred monitoring range.";
}
else{
env.innerHTML =
"Ambient humidity is outside the 40–60% preferred range.";
}
document.getElementById(
"sensorStatus"
).innerText =
data.bme ?
"ONLINE" :
"OFFLINE";
document.getElementById(
"rssi"
).innerText =
data.rssi + " dBm";
updateWifiMeter(data.rssi);
document.getElementById(
"time"
).innerText =
new Date().toLocaleTimeString();
}
async function loadData(){
try{
const response =
await fetch(
"/data?t=" +
Date.now()
);
const data =
await response.json();
updateDashboard(data);
translatePage();
}
catch(error){
console.log(error);
}
}
translatePage();
syncDeviceClock();
loadData();
loadHistory();
loadTrend();
setInterval(
loadData,
2000
);
setInterval(
loadHistory,
3000
);
setInterval(
loadTrend,
60000
);
</script>
</body>
</html>
)rawliteral";
return html;
}
// =====================================================
// ROOT WEB PAGE
// =====================================================
void handleRoot()
{
server.sendHeader(
"Cache-Control",
"no-store, no-cache, must-revalidate, max-age=0"
);
server.sendHeader(
"Pragma",
"no-cache"
);
server.send_P(
200,
"text/html; charset=UTF-8",
createWebPage()
);
}
// =====================================================
// SETUP
// =====================================================
void setup()
{
Serial.begin(115200);
prefs.begin("heritage", false);
loadRecentEvents();
loadDailySummary();
loadTrendHistory();
loadAirEvents();
delay(1000);
Serial.println();
Serial.println(
"Starting Heritage Wall Monitoring System..."
);
// Moisture Sensor
analogReadResolution(12);
pinMode(MOISTURE_PIN, INPUT);
if (BATTERY_MONITOR_ENABLED)
pinMode(BATTERY_PIN, INPUT);
// I2C
Wire.begin(SDA_PIN, SCL_PIN);
// LCD 16x2 I2C
lcd.init();
lcd.backlight();
lcd.clear();
lcd.setCursor(0, 0);
lcd.print("Heritage Monitor");
lcd.setCursor(0, 1);
lcd.print("Starting...");
delay(1200);
// BME280 - Address 0x76
if (bme.begin(0x76))
{
bmeFound = true;
Serial.println(
"BME280 detected at 0x76"
);
}
// BME280 - Address 0x77
else if (bme.begin(0x77))
{
bmeFound = true;
Serial.println(
"BME280 detected at 0x77"
);
}
else
{
Serial.println(
"BME280 NOT FOUND"
);
}
// WiFi
Serial.println();
Serial.print(
"Connecting to WiFi: "
);
Serial.println(WIFI_SSID);
WiFi.begin(
WIFI_SSID,
WIFI_PASSWORD
);
int attempts = 0;
while(
WiFi.status() != WL_CONNECTED &&
attempts < 40
)
{
delay(500);
Serial.print(".");
attempts++;
}
Serial.println();
if(
WiFi.status() ==
WL_CONNECTED
)
{
Serial.println(
"WiFi Connected!"
);
Serial.print(
"IP Address: "
);
Serial.println(
WiFi.localIP()
);
server.on(
"/",
handleRoot
);
server.on(
"/data",
handleData
);
server.on(
"/history",
handleHistory
);
server.on(
"/settime",
handleSetTime
);
server.on(
"/trend",
handleTrend
);
server.on(
"/refresh",
handleRefresh
);
server.on(
"/health",
[]()
{
server.send(
200,
"text/plain; charset=UTF-8",
"Heritage Wall Monitor OK"
);
}
);
initClock();
server.begin();
Serial.println(
"Web Server Started"
);
Serial.print(
"Dashboard: http://"
);
Serial.println(
WiFi.localIP()
);
Serial.print(
"Health test: http://"
);
Serial.print(
WiFi.localIP()
);
Serial.println(
"/health"
);
}
else
{
Serial.println(
"WiFi connection failed"
);
}
Serial.println(
"System Ready"
);
}
// =====================================================
// LOOP
// =====================================================
void loop()
{
server.handleClient();
if(
millis() - previousRead >=
READ_INTERVAL
)
{
previousRead = millis();
readMoisture();
readBME();
readBatteryStatus();
keepClockUpdated();
processWallState();
processAirHumidityState();
maybeCaptureTrendSample();
updateLCD();
monitorEnvironmentalChanges();
printReadings();
}
}
