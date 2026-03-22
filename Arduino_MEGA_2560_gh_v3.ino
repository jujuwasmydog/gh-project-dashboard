// Smart Greenhouse - Mega conversion draft
// Output format updated to match new_test_sketch.ino JSON exactly.
// Pin map matches the currently installed Mega hardware.

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
const uint32_t TELEMETRY_MS     = 2000;
const uint32_t DHT_MIN_MS       = 2200;
const float    FAN_LOW_ON_F     = 75.0;
const float    FAN_MED_ON_F     = 80.0;
const float    FAN_HIGH_ON_F    = 85.0;
const uint8_t  FAN_PWM_LOW      = 90;
const uint8_t  FAN_PWM_MED      = 170;
const uint8_t  FAN_PWM_HIGH     = 255;
const uint8_t  ACT_PWM          = 255;     // actuators full speed for now
const uint32_t FULL_TRAVEL_MS   = 2000;    // 0-100% estimate, tune later
const uint32_t STARTUP_CLOSE_MS  = 4000;    // force louvres closed at boot to establish known state
const int      SOIL_DRY_RAW     = 212;     // preserve dry LED behavior on pin 13
const int      SOIL_DRY_CAL     = 417;     // raw value at dry end for soil % estimate
const int      SOIL_WET_CAL     = 380;     // raw value at wet end for soil % estimate
const float    LIGHT_ON_LUX     = 2000.0;  // tune with real outdoor light
const float    LIGHT_OFF_LUX    = 4000.0;  // tune with real outdoor light
const float    TEMP_OPEN_F      = 85.0;    // open above this average temperature
const float    TEMP_CLOSE_F     = 69.0;    // close below this average temperature
const float    RH_OPEN_PCT      = 85.0;    // open above this average humidity
const float    RH_CLOSE_PCT     = 83.0;    // close below this average humidity

// --------------------------- States ---------------------------
enum LouvreState : uint8_t {
  L_OPEN = 0,
  L_CLOSED = 1,
  L_OPENING = 2,
  L_CLOSING = 3
};

LouvreState louvreState = L_CLOSED;

float t1 = NAN;
float t2 = NAN;
float h1 = NAN;
float h2 = NAN;
float tavg = NAN;
float havg = NAN;
int   soil = 0;
int   soilRaw = 0;
long  lux = 0;
float louvre_pct = 0.0f;
int8_t louvre_dir = 0; // +1 opening, -1 closing, 0 stopped
bool lightOn = false;
bool isDry = false;
uint8_t fanPWM = 0;

uint32_t lastPrint = 0;
uint32_t lastDhtMs = 0;
uint32_t lastMoveUpdate = 0;

// --------------------------- Helpers ---------------------------
static inline float cToF(float c) {
  return c * 1.8f + 32.0f;
}

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static int mapSoilRawToPct(int raw, int dryRaw, int wetRaw) {
  if (dryRaw == wetRaw) return 0;
  long pct = (long)(dryRaw - raw) * 100L / (long)(dryRaw - wetRaw);
  return clampi((int)pct, 0, 100);
}

static void setFans(uint8_t pwm) {
  analogWrite(lf, pwm);
  analogWrite(rf, pwm);
}

static void roof_extend(uint8_t pwm) {
  analogWrite(roof_rpwm, pwm);
  analogWrite(roof_lpwm, 0);
}

static void roof_retract(uint8_t pwm) {
  analogWrite(roof_rpwm, 0);
  analogWrite(roof_lpwm, pwm);
}

static void roof_stop() {
  analogWrite(roof_rpwm, 0);
  analogWrite(roof_lpwm, 0);
}

static void side_extend(uint8_t pwm) {
  analogWrite(side_rpwm, pwm);
  analogWrite(side_lpwm, 0);
}

static void side_retract(uint8_t pwm) {
  analogWrite(side_rpwm, 0);
  analogWrite(side_lpwm, pwm);
}

static void side_stop() {
  analogWrite(side_rpwm, 0);
  analogWrite(side_lpwm, 0);
}

static void actuators_open(uint8_t pwm) {
  roof_extend(pwm);
  side_extend(pwm);
  louvre_dir = +1;
  louvreState = L_OPENING;
}

static void actuators_close(uint8_t pwm) {
  roof_retract(pwm);
  side_retract(pwm);
  louvre_dir = -1;
  louvreState = L_CLOSING;
}

static void actuators_stop() {
  roof_stop();
  side_stop();
  louvre_dir = 0;
}

static const char* louvreStateText() {
  switch (louvreState) {
    case L_OPEN:    return "open";
    case L_CLOSED:  return "closed";
    case L_OPENING: return "opening";
    case L_CLOSING: return "closing";
    default:        return "closed";
  }
}

static void updateLouvrePositionEstimate() {
  uint32_t now = millis();
  if (lastMoveUpdate == 0) {
    lastMoveUpdate = now;
    return;
  }

  uint32_t dt = now - lastMoveUpdate;
  lastMoveUpdate = now;

  if (louvre_dir == 0) return;

  float delta_pct = (100.0f * (float)dt) / (float)FULL_TRAVEL_MS;

  if (louvre_dir > 0) louvre_pct += delta_pct;
  else                louvre_pct -= delta_pct;

  louvre_pct = clampf(louvre_pct, 0.0f, 100.0f);

  if (louvre_pct >= 100.0f && louvre_dir > 0) {
    actuators_stop();
    louvreState = L_OPEN;
  } else if (louvre_pct <= 0.0f && louvre_dir < 0) {
    actuators_stop();
    louvreState = L_CLOSED;
  }
}

static void updateLouvreControl() {
  bool wantOpen = false;
  bool wantClose = false;

  // Always force the louvres closed at or below the configured close temperature.
  // This keeps reported state aligned with the physical actuators.
  if (!isnan(tavg) && tavg <= TEMP_CLOSE_F) {
    wantClose = true;
  } else {
    // Keep the original spirit of the logic for opening.
    if (!isnan(tavg) && tavg >= TEMP_OPEN_F) wantOpen = true;
    if (!isnan(havg) && havg >= RH_OPEN_PCT) wantOpen = true;

    // Preserve the original humidity-assisted close behavior when temperature
    // is above the forced-close threshold.
    if (!wantOpen) {
      if (!isnan(tavg) && !isnan(havg)) {
        if (tavg <= TEMP_CLOSE_F && havg <= RH_CLOSE_PCT) {
          wantClose = true;
        }
      }
    }
  }

  if (wantClose) {
    if (louvreState != L_CLOSED && louvreState != L_CLOSING) {
      actuators_close(ACT_PWM);
    }
  } else if (wantOpen) {
    if (louvreState != L_OPEN && louvreState != L_OPENING) {
      actuators_open(ACT_PWM);
    }
  }
}


static void updateFansFromTemp() {
  if (isnan(tavg)) {
    fanPWM = 0;
  } else if (tavg >= FAN_HIGH_ON_F) {
    fanPWM = FAN_PWM_HIGH;
  } else if (tavg >= FAN_MED_ON_F) {
    fanPWM = FAN_PWM_MED;
  } else if (tavg >= FAN_LOW_ON_F) {
    fanPWM = FAN_PWM_LOW;
  } else {
    fanPWM = 0;
  }

  setFans(fanPWM);
}

static void updateDhts() {
  uint32_t now = millis();
  if (now - lastDhtMs < DHT_MIN_MS) return;
  lastDhtMs = now;

  float exH = dhtExhaust.readHumidity();
  float exC = dhtExhaust.readTemperature();
  float inH = dhtIn.readHumidity();
  float inC = dhtIn.readTemperature();

  t1 = isnan(exC) ? NAN : cToF(exC);
  t2 = isnan(inC) ? NAN : cToF(inC);
  h1 = exH;
  h2 = inH;

  float sumT = 0.0f;
  int nT = 0;
  if (!isnan(t1)) { sumT += t1; nT++; }
  if (!isnan(t2)) { sumT += t2; nT++; }
  tavg = (nT > 0) ? (sumT / nT) : NAN;

  float sumH = 0.0f;
  int nH = 0;
  if (!isnan(h1)) { sumH += h1; nH++; }
  if (!isnan(h2)) { sumH += h2; nH++; }
  havg = (nH > 0) ? (sumH / nH) : NAN;
}

static void updateSoil() {
  soilRaw = analogRead(cap_sense);
  soil = mapSoilRawToPct(soilRaw, SOIL_DRY_CAL, SOIL_WET_CAL);
  isDry = (soilRaw >= SOIL_DRY_RAW);
  digitalWrite(dry_light, isDry ? HIGH : LOW);
}

static void updateLux() {
  float brightness = lightSense.readLightLevel();
  if (!isnan(brightness)) {
    lux = (long)brightness;
    if (brightness < LIGHT_ON_LUX) lightOn = true;
    else if (brightness >= LIGHT_OFF_LUX) lightOn = false;
  }
}

static void printTelemetry() {
  unsigned long now = millis();

  Serial.print("{\"ts\":");
  Serial.print(now);

  Serial.print(",\"t1\":");
  if (isnan(t1)) Serial.print("null");
  else Serial.print(t1, 1);

  Serial.print(",\"t2\":");
  if (isnan(t2)) Serial.print("null");
  else Serial.print(t2, 1);

  Serial.print(",\"tavg\":");
  if (isnan(tavg)) Serial.print("null");
  else Serial.print(tavg, 2);

  Serial.print(",\"h1\":");
  if (isnan(h1)) Serial.print("null");
  else Serial.print(h1, 1);

  Serial.print(",\"h2\":");
  if (isnan(h2)) Serial.print("null");
  else Serial.print(h2, 1);

  Serial.print(",\"havg\":");
  if (isnan(havg)) Serial.print("null");
  else Serial.print(havg, 2);

  Serial.print(",\"soil\":");
  Serial.print(soil);

  Serial.print(",\"soilRaw\":");
  Serial.print(soilRaw);

  Serial.print(",\"lux\":");
  Serial.print(lux);

  Serial.print(",\"louvre_pct\":");
  Serial.print(louvre_pct, 1);

  Serial.print(",\"louvre_state\":\"");
  Serial.print(louvreStateText());
  Serial.print("\"");

  Serial.print(",\"fan_pct\":");
  Serial.print((fanPWM / 255.0) * 100.0, 1);

  // add light status
  Serial.print(",\"lightOn\":");
  Serial.print(lightOn ? "true" : "false");

  Serial.println("}");
}

void setup() {
  Serial.begin(9600);

  pinMode(relay4, OUTPUT);
  digitalWrite(relay4, LOW);

  pinMode(lf, OUTPUT);
  pinMode(rf, OUTPUT);
  setFans(0);

  pinMode(dry_light, OUTPUT);
  digitalWrite(dry_light, LOW);

  pinMode(roof_rpwm, OUTPUT);
  pinMode(roof_lpwm, OUTPUT);
  pinMode(side_rpwm, OUTPUT);
  pinMode(side_lpwm, OUTPUT);

  // Force the louvres fully closed before any sensor-based control starts.
  actuators_close(ACT_PWM);
  delay(STARTUP_CLOSE_MS);
  actuators_stop();
  louvreState = L_CLOSED;
  louvre_pct = 0.0f;

  dhtExhaust.begin();
  dhtIn.begin();

  Wire.begin();
  if (!lightSense.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    lightSense.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x5C);
  }

  lastPrint = 0;
  lastDhtMs = 0;
  lastMoveUpdate = millis();
}

void loop() {
  updateDhts();
  updateFansFromTemp();
  updateSoil();
  updateLux();
  updateLouvrePositionEstimate();
  updateLouvreControl();

  digitalWrite(relay4, lightOn ? HIGH : LOW);

  unsigned long now = millis();
  if (now - lastPrint >= TELEMETRY_MS) {
    lastPrint = now;
    printTelemetry();
  }

  delay(50);
}
