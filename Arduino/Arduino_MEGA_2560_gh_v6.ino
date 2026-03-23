// Arduino_MEGA_2560_gh_v6.ino
// Smart Greenhouse - Mega conversion draft (v6)
// Adds explicit failed-read handling for both DHT-22 sensors so unplugged / failed
// sensors do NOT keep reporting stale last-good values.
//
// Serial commands supported (send with newline):
//   AUTO
//   OPEN
//   CLOSE
//   HALF
//   STOP
//   FAN_AUTO
//   FAN_50
//   FAN_100
//   FAN_OFF
//   LIGHT_AUTO
//   LIGHT_ON
//   LIGHT_OFF

#include <DHT.h>
#include <Wire.h>
#include <BH1750.h>
#include <math.h>

// --------------------------- Pin map (Arduino Mega) ---------------------------
#define exhaust     A3
#define intake      A11
#define DHTTYPE     DHT22

#define cap_sense   A1
#define relay4      28
#define lf          2
#define rf          4
#define dry_light   13

// IBT-2 #1 roof
#define roof_rpwm   5
#define roof_lpwm   6

// IBT-2 #2 side actuators
#define side_rpwm   9
#define side_lpwm   10

// --------------------------- Devices ---------------------------
DHT dhtExhaust(exhaust, DHTTYPE);
DHT dhtIn(intake, DHTTYPE);
BH1750 lightSense;

// --------------------------- Tunables ---------------------------
const uint32_t TELEMETRY_MS      = 2000;
const uint32_t DHT_MIN_MS        = 2200;

const float    FAN_LOW_ON_F      = 75.0;
const float    FAN_MED_ON_F      = 80.0;
const float    FAN_HIGH_ON_F     = 85.0;
const uint8_t  FAN_PWM_LOW       = 90;    // legacy ~35%
const uint8_t  FAN_PWM_MED       = 170;   // legacy ~67%
const uint8_t  FAN_PWM_HIGH      = 255;   // 100%

const uint8_t  FAN_PWM_50        = 128;   // manual 50%
const uint8_t  FAN_PWM_100       = 255;   // manual 100%

const uint8_t  ACT_PWM           = 255;
const uint32_t FULL_TRAVEL_MS    = 4000;  // full travel estimate
const uint32_t STARTUP_CLOSE_MS  = 4000;

const int      SOIL_DRY_RAW      = 212;
const int      SOIL_DRY_CAL      = 417;
const int      SOIL_WET_CAL      = 380;

const float    LIGHT_ON_LUX      = 2000.0;
const float    LIGHT_OFF_LUX     = 4000.0;

const float    TEMP_OPEN_F       = 85.0;
const float    TEMP_CLOSE_F      = 69.0;
const float    RH_OPEN_PCT       = 85.0;
const float    RH_CLOSE_PCT      = 83.0;

const float    LOUVRE_TARGET_TOL = 2.0f;   // % tolerance around target

// --------------------------- States ---------------------------
enum LouvreState { LS_STOPPED, LS_OPENING, LS_CLOSING };

float t1=NAN, t2=NAN, tavg=NAN;
float h1=NAN, h2=NAN, havg=NAN;
int   soil_pct = 0;
float lux = 0.0f;

uint8_t fan_pwm = 0;
bool lightOn = false;

LouvreState louvreState = LS_STOPPED;
float louvre_pct = 0.0f;       // 0..100 estimated position
uint32_t lastMoveUpdate = 0;

// Louvre override
bool manualOverride = false;
String manualCommand = "AUTO";
float manualTargetPct = -1.0f; // -1 means no target (STOP)

// Fan override
bool fanManualOverride = false;
String fanCommand = "FAN_AUTO";
uint8_t fanManualPwm = 0;

// Light override
bool lightManualOverride = false;
String lightCommand = "LIGHT_AUTO";

// --------------------------- Helpers ---------------------------
static int soilPercentFromRaw(int raw) {
  int pct = map(raw, SOIL_DRY_CAL, SOIL_WET_CAL, 0, 100);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

static void actuators_stop() {
  analogWrite(roof_rpwm, 0);
  analogWrite(roof_lpwm, 0);
  analogWrite(side_rpwm, 0);
  analogWrite(side_lpwm, 0);
  louvreState = LS_STOPPED;
}

static void actuators_open(uint8_t pwm) {
  analogWrite(roof_lpwm, 0);
  analogWrite(side_lpwm, 0);
  analogWrite(roof_rpwm, pwm);
  analogWrite(side_rpwm, pwm);
  louvreState = LS_OPENING;
}

static void actuators_close(uint8_t pwm) {
  analogWrite(roof_rpwm, 0);
  analogWrite(side_rpwm, 0);
  analogWrite(roof_lpwm, pwm);
  analogWrite(side_lpwm, pwm);
  louvreState = LS_CLOSING;
}

static void updateLouvrePositionEstimate() {
  uint32_t now = millis();
  if (lastMoveUpdate == 0) {
    lastMoveUpdate = now;
    return;
  }

  uint32_t dt = now - lastMoveUpdate;
  lastMoveUpdate = now;

  if (louvreState == LS_OPENING) {
    float delta = (100.0f * (float)dt) / (float)FULL_TRAVEL_MS;
    louvre_pct += delta;
    if (louvre_pct >= 100.0f) {
      louvre_pct = 100.0f;
      actuators_stop();
    }
  } else if (louvreState == LS_CLOSING) {
    float delta = (100.0f * (float)dt) / (float)FULL_TRAVEL_MS;
    louvre_pct -= delta;
    if (louvre_pct <= 0.0f) {
      louvre_pct = 0.0f;
      actuators_stop();
    }
  }
}

// --------------------------- Sensor Updates ---------------------------
static void updateDhts() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < DHT_MIN_MS) return;
  last = now;

  float nt1 = dhtExhaust.readTemperature(true);
  float nh1 = dhtExhaust.readHumidity();
  float nt2 = dhtIn.readTemperature(true);
  float nh2 = dhtIn.readHumidity();

  // IMPORTANT:
  // On any failed DHT read, force the corresponding values to NAN instead of
  // keeping stale last-good values. Telemetry will print NAN as -1 so Node-RED
  // can immediately declare a fault.
  if (isnan(nt1) || isnan(nh1)) {
    t1 = NAN;
    h1 = NAN;
  } else {
    t1 = nt1;
    h1 = nh1;
  }

  if (isnan(nt2) || isnan(nh2)) {
    t2 = NAN;
    h2 = NAN;
  } else {
    t2 = nt2;
    h2 = nh2;
  }

  // Averages are only valid when BOTH sensors are valid.
  if (!isnan(t1) && !isnan(t2)) tavg = 0.5f * (t1 + t2);
  else tavg = NAN;

  if (!isnan(h1) && !isnan(h2)) havg = 0.5f * (h1 + h2);
  else havg = NAN;
}

static void updateSoil() {
  int raw = analogRead(cap_sense);
  soil_pct = soilPercentFromRaw(raw);
}

static void updateLux() {
  float v = lightSense.readLightLevel();
  if (v >= 0.0f) lux = v;
}

// --------------------------- Control ---------------------------
static void updateFans() {
  if (fanManualOverride) {
    fan_pwm = fanManualPwm;
  } else {
    uint8_t target = 0;
    if (!isnan(tavg)) {
      if (tavg >= FAN_HIGH_ON_F) target = FAN_PWM_HIGH;
      else if (tavg >= FAN_MED_ON_F) target = FAN_PWM_MED;
      else if (tavg >= FAN_LOW_ON_F) target = FAN_PWM_LOW;
      else target = 0;
    }
    fan_pwm = target;
  }

  analogWrite(lf, fan_pwm);
  analogWrite(rf, fan_pwm);
}

static void updateLightControl() {
  if (lightManualOverride) {
    if (lightCommand == "LIGHT_ON") lightOn = true;
    else if (lightCommand == "LIGHT_OFF") lightOn = false;
  } else {
    if (lux <= LIGHT_ON_LUX) lightOn = true;
    if (lux >= LIGHT_OFF_LUX) lightOn = false;
  }
}

static void updateLouvreControlAuto() {
  // If DHT averages are invalid, stop automatic motion rather than continuing
  // to move on stale state.
  if (isnan(tavg) || isnan(havg)) {
    actuators_stop();
    return;
  }

  bool needOpen = (tavg >= TEMP_OPEN_F) || (havg >= RH_OPEN_PCT);
  bool needClose = (tavg <= TEMP_CLOSE_F) && (havg <= RH_CLOSE_PCT);

  if (needOpen) {
    if (louvre_pct < 100.0f - LOUVRE_TARGET_TOL) {
      actuators_open(ACT_PWM);
    } else {
      actuators_stop();
    }
  } else if (needClose) {
    if (louvre_pct > 0.0f + LOUVRE_TARGET_TOL) {
      actuators_close(ACT_PWM);
    } else {
      actuators_stop();
    }
  } else {
    actuators_stop();
  }
}

static void updateLouvreControlManual() {
  if (manualCommand == "STOP") {
    actuators_stop();
    return;
  }

  if (manualTargetPct < 0.0f) return;

  if (louvre_pct < manualTargetPct - LOUVRE_TARGET_TOL) {
    actuators_open(ACT_PWM);
  } else if (louvre_pct > manualTargetPct + LOUVRE_TARGET_TOL) {
    actuators_close(ACT_PWM);
  } else {
    actuators_stop();
  }
}

// --------------------------- Serial Commands ---------------------------
static void handleSerialCommands() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toUpperCase();

  // Louvre
  if (cmd == "AUTO") {
    manualOverride = false;
    manualCommand = "AUTO";
    manualTargetPct = -1.0f;
    actuators_stop();
  }
  else if (cmd == "OPEN") {
    manualOverride = true;
    manualCommand = "OPEN";
    manualTargetPct = 100.0f;
  }
  else if (cmd == "CLOSE") {
    manualOverride = true;
    manualCommand = "CLOSE";
    manualTargetPct = 0.0f;
  }
  else if (cmd == "HALF") {
    manualOverride = true;
    manualCommand = "HALF";
    manualTargetPct = 50.0f;
  }
  else if (cmd == "STOP") {
    manualOverride = true;
    manualCommand = "STOP";
    manualTargetPct = -1.0f;
    actuators_stop();
  }

  // Fan
  else if (cmd == "FAN_AUTO") {
    fanManualOverride = false;
    fanCommand = "FAN_AUTO";
    fanManualPwm = 0;
  }
  else if (cmd == "FAN_50") {
    fanManualOverride = true;
    fanCommand = "FAN_50";
    fanManualPwm = FAN_PWM_50;
  }
  else if (cmd == "FAN_100") {
    fanManualOverride = true;
    fanCommand = "FAN_100";
    fanManualPwm = FAN_PWM_100;
  }
  else if (cmd == "FAN_OFF") {
    fanManualOverride = true;
    fanCommand = "FAN_OFF";
    fanManualPwm = 0;
  }

  // Light
  else if (cmd == "LIGHT_AUTO") {
    lightManualOverride = false;
    lightCommand = "LIGHT_AUTO";
  }
  else if (cmd == "LIGHT_ON") {
    lightManualOverride = true;
    lightCommand = "LIGHT_ON";
    lightOn = true;
  }
  else if (cmd == "LIGHT_OFF") {
    lightManualOverride = true;
    lightCommand = "LIGHT_OFF";
    lightOn = false;
  }
}

// --------------------------- Telemetry ---------------------------
static void printTelemetry() {
  Serial.print("{");

  Serial.print("\"ts\":"); Serial.print(millis());

  Serial.print(",\"t1\":"); Serial.print(isnan(t1) ? -1 : t1);
  Serial.print(",\"t2\":"); Serial.print(isnan(t2) ? -1 : t2);
  Serial.print(",\"tavg\":"); Serial.print(isnan(tavg) ? -1 : tavg);

  Serial.print(",\"h1\":"); Serial.print(isnan(h1) ? -1 : h1);
  Serial.print(",\"h2\":"); Serial.print(isnan(h2) ? -1 : h2);
  Serial.print(",\"havg\":"); Serial.print(isnan(havg) ? -1 : havg);

  Serial.print(",\"soil\":"); Serial.print(soil_pct);
  Serial.print(",\"lux\":"); Serial.print(lux);

  Serial.print(",\"louvre_pct\":"); Serial.print(louvre_pct);

  Serial.print(",\"louvre_state\":\"");
  if (louvreState == LS_OPENING) Serial.print("opening");
  else if (louvreState == LS_CLOSING) Serial.print("closing");
  else Serial.print("stopped");
  Serial.print("\"");

  Serial.print(",\"fan_pct\":"); Serial.print((int)(fan_pwm * 100 / 255));

  Serial.print(",\"lightOn\":");
  Serial.print(lightOn ? "true" : "false");

  Serial.print(",\"manualOverride\":");
  Serial.print(manualOverride ? "true" : "false");

  Serial.print(",\"manualCommand\":\"");
  Serial.print(manualCommand);
  Serial.print("\"");

  Serial.print(",\"fanManualOverride\":");
  Serial.print(fanManualOverride ? "true" : "false");

  Serial.print(",\"fanCommand\":\"");
  Serial.print(fanCommand);
  Serial.print("\"");

  Serial.print(",\"lightManualOverride\":");
  Serial.print(lightManualOverride ? "true" : "false");

  Serial.print(",\"lightCommand\":\"");
  Serial.print(lightCommand);
  Serial.print("\"");

  Serial.println("}");
}

// --------------------------- Setup ---------------------------
void setup() {
  Serial.begin(9600);

  pinMode(lf, OUTPUT);
  pinMode(rf, OUTPUT);
  pinMode(relay4, OUTPUT);
  pinMode(dry_light, OUTPUT);

  pinMode(roof_rpwm, OUTPUT);
  pinMode(roof_lpwm, OUTPUT);
  pinMode(side_rpwm, OUTPUT);
  pinMode(side_lpwm, OUTPUT);

  dhtExhaust.begin();
  dhtIn.begin();

  Wire.begin();
  lightSense.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  actuators_close(ACT_PWM);
  delay(STARTUP_CLOSE_MS);
  actuators_stop();
  louvre_pct = 0.0f;
}

// --------------------------- Loop ---------------------------
uint32_t lastPrint = 0;

void loop() {
  handleSerialCommands();

  updateDhts();
  updateSoil();
  updateLux();

  updateFans();
  updateLightControl();

  updateLouvrePositionEstimate();

  if (manualOverride) {
    updateLouvreControlManual();
  } else {
    updateLouvreControlAuto();
  }

  digitalWrite(relay4, lightOn ? HIGH : LOW);
  digitalWrite(dry_light, lightOn ? HIGH : LOW);

  uint32_t now = millis();
  if (now - lastPrint >= TELEMETRY_MS) {
    lastPrint = now;
    printTelemetry();
  }

  delay(50);
}
