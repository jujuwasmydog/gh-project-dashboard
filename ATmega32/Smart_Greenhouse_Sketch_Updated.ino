/*
  Smart Greenhouse for ATmega32A 

  CONFIGURED FOR: 1x DHT22 (we can scale later)
  - Add command set and original telemetry fields for Node-RED compatibility
  - Adds PV panel/controller PV input voltage measurement on A6: pv_v, pv_present
  - Adds ECO mode after 30s sustained LOW_BATT && pv_present=0: eco_mode + PV_MISSING fault bit
  - Adds time-based louvre position estimate 0–100%: louvre_pos_pct (calibrate FULL_TRAVEL_MS)

  AUTO Logic (PDR guide):
    - Open louvre if Temp > 85°F OR RH > 85%
    - Close louvre if Temp < (65°F - hyst) AND RH < (85% - hyst)

  IMPORTANT:
    - ATmega32A Arduino pin mapping assumed:
      D0..D7=PD0..PD7, D8..D15=PB0..PB7, D16..D23=PC0..PC7, A0..A7=PA0..PA7.
    - DHT22 minimum interval is 2 seconds.
*/

#include <Wire.h>
#include <BH1750.h>
#include <DHT.h>
#include <math.h>

// --------------------------- Pin Map (ATmega32A) ---------------------------
// UART: Serial uses D0/D1 automatically (PD0/PD1)

#define PIN_DHT         12      // PB4 (original)
#define PIN_SOIL_ADC    A0      // PA0/ADC0 (original)
#define PIN_BATT_ADC    A1      // PA1/ADC1 (original)
#define PIN_PV_ADC      A6      // PA6/ADC6 (new PV sense)

// I2C pins are core-defined; typical ATmega32 cores:
// SCL = PC0 (D16), SDA = PC1 (D17)

// BTS7960 (original)
#define PIN_RPWM        5       // PD5 PWM (OC1A)
#define PIN_LPWM        4       // PD4 PWM (may be OC1B in some cores; verify PWM support)
#define PIN_REN         8       // PB0
#define PIN_LEN         9       // PB1

// Limit switches (active LOW w/ pullups) (original)
#define PIN_LIMIT_OPEN  2       // PD2 / INT0
#define PIN_LIMIT_CLOSE 3       // PD3 / INT1

// Outputs (original)
#define PIN_VALVE       10      // PB2
#define PIN_LIGHT       11      // PB3 (used as on/off here)
#define PIN_FAN_PWM     7       // PD7 (PWM only if your core supports; else will behave as on/off)

// --------------------------- Control thresholds (PDR) ---------------------------
float TEMP_OPEN_F  = 85.0;
float TEMP_CLOSE_F = 65.0;
float RH_OPEN_PCT  = 85.0;

// Hysteresis
float TEMP_HYST_F = 2.0;
float RH_HYST_PCT = 3.0;

// Timing
const uint32_t TELEMETRY_MS      = 1000;     // 1 Hz
const uint32_t DHT_MIN_MS        = 2200;     // >=2s
const uint32_t FROZEN_WINDOW_MS  = 60000;    // 60s
const uint32_t LOUVRE_TIMEOUT_MS = 20000;    // 20s
const uint32_t UI_HEARTBEAT_MS   = 15000;    // 15s

// ECO / PV fault timing (requested 30s)
const uint32_t ECO_DELAY_MS      = 30000;    // 30s sustained low batt + no PV

// Actuator PWM
const uint8_t LOUVRE_PWM = 200;  // 0..255

// Louvre position estimate (time-based) — calibrate!
uint32_t FULL_TRAVEL_MS = 12000; // ms close->open

// Soil calibration (tune later)
int SOIL_DRY_RAW = 780;
int SOIL_WET_RAW = 380;

// ADC reference
float ADC_VREF = 5.0;

// Battery divider calibration (tune later)
float BATT_DIVIDER_RATIO = 4.0;
float LOW_BATT_V = 11.2;

// PV divider calibration (R1=150k, R2=22k => ~7.82)
float PV_DIVIDER_RATIO = 7.82;
float PV_PRESENT_V = 10.0;

// --------------------------- Devices ---------------------------
#define DHTTYPE DHT22
DHT dht(PIN_DHT, DHTTYPE);
BH1750 luxSensor;

// --------------------------- States ---------------------------
enum Mode : uint8_t { MODE_AUTO=0, MODE_MANUAL=1 };
Mode mode = MODE_AUTO;

enum LouvreState : uint8_t {
  L_UNKNOWN=0, L_OPEN=1, L_CLOSED=2, L_OPENING=3, L_CLOSING=4, L_FAULT=5
};
LouvreState louvreState = L_UNKNOWN;

enum FaultBits {
  F_SENSOR_FAIL    = 1 << 0,
  F_SOIL_OOR       = 1 << 1,
  F_LOUVRE_TIMEOUT = 1 << 2,
  F_LIMIT_CONFLICT = 1 << 3,
  F_LOW_BATT       = 1 << 4,
  F_FROZEN_SENSOR  = 1 << 5,
  F_COMM_LOSS      = 1 << 6,
  F_PV_MISSING     = 1 << 7   // NEW: eco condition persisted >=30s
};
uint16_t faultBits = 0;

// Measurements
float tempF = NAN, rhPct = NAN;
uint16_t lux = 0;
int soilRaw = 0, soilPct = 0;
float battV = 0.0;

// PV
float pvV = 0.0;
bool  pvPresent = false;

// Outputs
bool valveOn = false;
bool lightOn = true;     // placeholder policy; we can be scheduled later
uint8_t fanPwm = 0;

// Timers
uint32_t lastTeleMs = 0;
uint32_t lastDhtMs  = 0;
uint32_t louvreMoveStartMs = 0;
uint32_t lastUiHeartbeatMs = 0;

// Frozen detection
float lastTempF = NAN, lastRhPct = NAN;
uint32_t lastChangeMs = 0;

// Louvre position estimate tracking
float louvrePosPct = 0.0f;
int8_t louvreDir = 0;          // +1 opening, -1 closing, 0 stopped
uint32_t lastPosUpdateMs = 0;

// ECO timer
uint32_t ecoStartMs = 0;

// --------------------------- Helpers ---------------------------
static bool limitOpenActive()  { return digitalRead(PIN_LIMIT_OPEN)  == LOW; }
static bool limitCloseActive() { return digitalRead(PIN_LIMIT_CLOSE) == LOW; }

static int clampInt(int v, int lo, int hi) { return (v<lo)?lo : (v>hi)?hi : v; }
static float clampFloat(float v, float lo, float hi) { return (v<lo)?lo : (v>hi)?hi : v; }

static int mapSoilRawToPct(int raw) {
  if (SOIL_DRY_RAW == SOIL_WET_RAW) return 0;
  long pct;
  if (SOIL_DRY_RAW > SOIL_WET_RAW) {
    pct = (long)(SOIL_DRY_RAW - raw) * 100L / (long)(SOIL_DRY_RAW - SOIL_WET_RAW);
  } else {
    pct = (long)(raw - SOIL_DRY_RAW) * 100L / (long)(SOIL_WET_RAW - SOIL_DRY_RAW);
  }
  return clampInt((int)pct, 0, 100);
}

static int readAnalogAvg(uint8_t pin, uint8_t n=8) {
  long s=0;
  for (uint8_t i=0;i<n;i++) { s += analogRead(pin); delay(2); }
  return (int)(s/n);
}

static void btsStop() {
  analogWrite(PIN_RPWM, 0);
  analogWrite(PIN_LPWM, 0);
  digitalWrite(PIN_REN, LOW);
  digitalWrite(PIN_LEN, LOW);
  louvreDir = 0;
}

static void btsOpen(uint8_t pwm) {
  digitalWrite(PIN_REN, HIGH);
  digitalWrite(PIN_LEN, HIGH);
  analogWrite(PIN_LPWM, 0);
  analogWrite(PIN_RPWM, pwm);
  louvreDir = +1;
}

static void btsClose(uint8_t pwm) {
  digitalWrite(PIN_REN, HIGH);
  digitalWrite(PIN_LEN, HIGH);
  analogWrite(PIN_RPWM, 0);
  analogWrite(PIN_LPWM, pwm);
  louvreDir = -1;
}

static void updateCommsLoss() {
  uint32_t now = millis();
  if (now - lastUiHeartbeatMs > UI_HEARTBEAT_MS) faultBits |= F_COMM_LOSS;
  else faultBits &= ~F_COMM_LOSS;
}

// --------------------------- PV + ECO ---------------------------
static void updateSolarPanel() {
  int pvRaw = readAnalogAvg(PIN_PV_ADC, 8);
  pvV = (float)pvRaw * (ADC_VREF / 1023.0f) * PV_DIVIDER_RATIO;
  pvPresent = (pvV > PV_PRESENT_V);
}

static bool ecoConditionRaw() {
  return ((faultBits & F_LOW_BATT) && !pvPresent);
}

static bool ecoModeActive() {
  return (ecoStartMs != 0 && (millis() - ecoStartMs) >= ECO_DELAY_MS);
}

static void updateEcoTimersAndFault() {
  uint32_t now = millis();
  if (ecoConditionRaw()) {
    if (ecoStartMs == 0) ecoStartMs = now;
  } else {
    ecoStartMs = 0;
    faultBits &= ~F_PV_MISSING;
    return;
  }
  if (ecoModeActive()) faultBits |= F_PV_MISSING;
}

// --------------------------- Louvre position estimate ---------------------------
static void updateLouvrePositionEstimate() {
  uint32_t now = millis();
  if (lastPosUpdateMs == 0) lastPosUpdateMs = now;

  // Hard truth from limit switches
  if (limitCloseActive() && !limitOpenActive()) { louvrePosPct = 0.0f; return; }
  if (limitOpenActive() && !limitCloseActive()) { louvrePosPct = 100.0f; return; }

  if (louvreDir == 0) { lastPosUpdateMs = now; return; }

  uint32_t dt = now - lastPosUpdateMs;
  lastPosUpdateMs = now;

  if (FULL_TRAVEL_MS < 1000) FULL_TRAVEL_MS = 1000;

  float deltaPct = (100.0f * (float)dt) / (float)FULL_TRAVEL_MS;
  if (louvreDir > 0) louvrePosPct += deltaPct;
  else               louvrePosPct -= deltaPct;

  louvrePosPct = clampFloat(louvrePosPct, 0.0f, 100.0f);
}

// --------------------------- Measurements ---------------------------
static void updateLux() {
  lux = (uint16_t)luxSensor.readLightLevel();
}

static void updateSoilAndBattery() {
  soilRaw = readAnalogAvg(PIN_SOIL_ADC, 8);
  if (soilRaw < 0 || soilRaw > 1023) faultBits |= F_SOIL_OOR;
  soilPct = mapSoilRawToPct(soilRaw);

  int battRaw = readAnalogAvg(PIN_BATT_ADC, 8);
  battV = (float)battRaw * (ADC_VREF / 1023.0f) * BATT_DIVIDER_RATIO;
  if (battV > 0.5f && battV < LOW_BATT_V) faultBits |= F_LOW_BATT;
  else faultBits &= ~F_LOW_BATT;
}

static void updateDht() {
  uint32_t now = millis();
  if (now - lastDhtMs < DHT_MIN_MS) return;
  lastDhtMs = now;

  float h = dht.readHumidity();
  float tC = dht.readTemperature();
  if (isnan(h) || isnan(tC)) {
    faultBits |= F_SENSOR_FAIL;
    return;
  }
  faultBits &= ~F_SENSOR_FAIL;
  rhPct = h;
  tempF = tC * 9.0f/5.0f + 32.0f;
}

static void updateFrozenFault() {
  uint32_t now = millis();
  if (isnan(tempF) || isnan(rhPct)) return;

  if (isnan(lastTempF) || isnan(lastRhPct)) {
    lastTempF = tempF; lastRhPct = rhPct;
    lastChangeMs = now;
    return;
  }

  const float EPS_T = 0.05f;
  const float EPS_H = 0.05f;

  if (fabs(tempF - lastTempF) > EPS_T || fabs(rhPct - lastRhPct) > EPS_H) {
    lastTempF = tempF; lastRhPct = rhPct;
    lastChangeMs = now;
    faultBits &= ~F_FROZEN_SENSOR;
  } else if (now - lastChangeMs >= FROZEN_WINDOW_MS) {
    faultBits |= F_FROZEN_SENSOR;
  }
}

// --------------------------- Control ---------------------------
static void updateLouvreStateFromLimits() {
  if (limitOpenActive() && !limitCloseActive()) louvreState = L_OPEN;
  else if (limitCloseActive() && !limitOpenActive()) louvreState = L_CLOSED;
}

static void autoControlStep() {
  // Hard fault: limit conflict
  if (limitOpenActive() && limitCloseActive()) {
    faultBits |= F_LIMIT_CONFLICT;
    btsStop();
    louvreState = L_FAULT;
    return;
  } else {
    faultBits &= ~F_LIMIT_CONFLICT;
  }

  // Comms loss: hold position (stop motor)
  if (faultBits & F_COMM_LOSS) {
    btsStop();
    return;
  }

  // ECO mode: shed loads and block louvre unless emergency ventilation
  if (ecoModeActive()) {
    valveOn = false;
    lightOn = false;
    fanPwm  = 0;

    bool emergency = (!isnan(tempF) && tempF > (TEMP_OPEN_F + 5.0f)) || (!isnan(rhPct) && rhPct > (RH_OPEN_PCT + 5.0f));
    if (!emergency) {
      btsStop();
      return;
    }
    // emergency ventilation: allow OPEN only
    // (fall through to normal open logic, but never close)
  }

  bool wantOpen = false;
  bool wantClose = false;

  if (!isnan(tempF) && tempF > TEMP_OPEN_F) wantOpen = true;
  if (!isnan(rhPct) && rhPct > RH_OPEN_PCT) wantOpen = true;

  if (!ecoModeActive()) {
    if (!isnan(tempF) && !isnan(rhPct)) {
      if (tempF < (TEMP_CLOSE_F - TEMP_HYST_F) && rhPct < (RH_OPEN_PCT - RH_HYST_PCT)) {
        wantClose = true;
      }
    }
  }

  // Update known state if not moving
  if (louvreState != L_OPENING && louvreState != L_CLOSING) {
    updateLouvreStateFromLimits();
  }

  uint32_t now = millis();

  // Moving monitors
  if (louvreState == L_OPENING) {
    if (limitOpenActive()) { btsStop(); louvreState = L_OPEN; louvrePosPct = 100.0f; }
    else if (now - louvreMoveStartMs > LOUVRE_TIMEOUT_MS) {
      btsStop(); faultBits |= F_LOUVRE_TIMEOUT; louvreState = L_FAULT;
    }
    return;
  }

  if (louvreState == L_CLOSING) {
    if (limitCloseActive()) { btsStop(); louvreState = L_CLOSED; louvrePosPct = 0.0f; }
    else if (now - louvreMoveStartMs > LOUVRE_TIMEOUT_MS) {
      btsStop(); faultBits |= F_LOUVRE_TIMEOUT; louvreState = L_FAULT;
    }
    return;
  }

  // Command new motion
  if (wantOpen && louvreState != L_OPEN) {
    if (limitOpenActive()) { louvreState = L_OPEN; btsStop(); louvrePosPct = 100.0f; }
    else { louvreState = L_OPENING; louvreMoveStartMs = now; btsOpen(LOUVRE_PWM); }
  } else if (wantClose && louvreState != L_CLOSED) {
    if (limitCloseActive()) { louvreState = L_CLOSED; btsStop(); louvrePosPct = 0.0f; }
    else { louvreState = L_CLOSING; louvreMoveStartMs = now; btsClose(LOUVRE_PWM); }
  } else {
    btsStop();
  }

  // Optional fan behavior: ramp above close threshold
  if (!isnan(tempF) && tempF > TEMP_CLOSE_F) {
    fanPwm = (uint8_t)clampInt((int)((tempF - TEMP_CLOSE_F) * 12.0f), 0, 255);
  } else {
    fanPwm = 0;
  }
}

// --------------------------- Serial commands ---------------------------
/*
  Commands (newline terminated):
    HEARTBEAT
    MODE AUTO|MANUAL
    LOUVRE OPEN|CLOSE|STOP     (manual only)
    VALVE ON|OFF               (manual only)
    LIGHT ON|OFF               (manual only)
    FAN <0..255>               (manual only)
    SET TEMP_OPEN_F <val>
    SET TEMP_CLOSE_F <val>
    SET RH_OPEN_PCT <val>
    SET FULL_TRAVEL_MS <val>
    SET PV_PRESENT_V <val>
*/
static void processLine(String line) {
  line.trim();
  if (!line.length()) return;

  if (line.equalsIgnoreCase("HEARTBEAT")) { lastUiHeartbeatMs = millis(); return; }

  if (line.startsWith("MODE")) {
    if (line.indexOf("AUTO") > 0) mode = MODE_AUTO;
    if (line.indexOf("MANUAL") > 0) mode = MODE_MANUAL;
    return;
  }

  if (line.startsWith("SET")) {
    int sp1 = line.indexOf(' ');
    int sp2 = line.indexOf(' ', sp1 + 1);
    if (sp1 < 0 || sp2 < 0) return;
    String key = line.substring(sp1 + 1, sp2);
    float val = line.substring(sp2 + 1).toFloat();
    if (key.equalsIgnoreCase("TEMP_OPEN_F"))  TEMP_OPEN_F  = val;
    if (key.equalsIgnoreCase("TEMP_CLOSE_F")) TEMP_CLOSE_F = val;
    if (key.equalsIgnoreCase("RH_OPEN_PCT"))  RH_OPEN_PCT  = val;
    if (key.equalsIgnoreCase("FULL_TRAVEL_MS")) FULL_TRAVEL_MS = (uint32_t)val;
    if (key.equalsIgnoreCase("PV_PRESENT_V")) PV_PRESENT_V = val;
    return;
  }

  if (mode == MODE_MANUAL) {
    if (line.startsWith("LOUVRE")) {
      if (line.indexOf("OPEN") > 0)  { louvreState = L_OPENING; louvreMoveStartMs = millis(); btsOpen(LOUVRE_PWM); }
      if (line.indexOf("CLOSE") > 0) { louvreState = L_CLOSING; louvreMoveStartMs = millis(); btsClose(LOUVRE_PWM); }
      if (line.indexOf("STOP") > 0)  { btsStop(); }
      return;
    }
    if (line.startsWith("VALVE")) {
      if (line.indexOf("ON") > 0)  valveOn = true;
      if (line.indexOf("OFF") > 0) valveOn = false;
      return;
    }
    if (line.startsWith("LIGHT")) {
      if (line.indexOf("ON") > 0)  lightOn = true;
      if (line.indexOf("OFF") > 0) lightOn = false;
      return;
    }
    if (line.startsWith("FAN")) {
      int sp = line.indexOf(' ');
      if (sp > 0) fanPwm = (uint8_t)clampInt(line.substring(sp + 1).toInt(), 0, 255);
      return;
    }
  }
}

static void pollSerial() {
  static String buf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') { processLine(buf); buf = ""; }
    else if (c != '\r') buf += c;
  }
}

// --------------------------- Telemetry ---------------------------
static void printTelemetry() {
  uint32_t ts = millis();
  Serial.print("ts="); Serial.print(ts);
  Serial.print(",mode="); Serial.print((mode==MODE_AUTO) ? "AUTO" : "MANUAL");

  Serial.print(",temp_f=");
  if (isnan(tempF)) Serial.print("nan"); else Serial.print(tempF, 1);

  Serial.print(",rh_pct=");
  if (isnan(rhPct)) Serial.print("nan"); else Serial.print(rhPct, 1);

  Serial.print(",lux="); Serial.print(lux);
  Serial.print(",soil_raw="); Serial.print(soilRaw);
  Serial.print(",soil_pct="); Serial.print(soilPct);
  Serial.print(",batt_v="); Serial.print(battV, 2);

  // New solar fields
  Serial.print(",pv_v="); Serial.print(pvV, 2);
  Serial.print(",pv_present="); Serial.print(pvPresent ? 1 : 0);

  // Louvre state + position estimate
  Serial.print(",louvre="); Serial.print((uint8_t)louvreState);
  Serial.print(",louvre_pos_pct="); Serial.print(louvrePosPct, 1);
  Serial.print(",full_travel_ms="); Serial.print(FULL_TRAVEL_MS);

  // Outputs
  Serial.print(",valve="); Serial.print(valveOn ? 1 : 0);
  Serial.print(",light_on="); Serial.print(lightOn ? 1 : 0);
  Serial.print(",fan_pwm="); Serial.print(fanPwm);

  // ECO flag
  Serial.print(",eco_mode="); Serial.print(ecoModeActive() ? 1 : 0);

  // Tunables
  Serial.print(",temp_open_f="); Serial.print(TEMP_OPEN_F, 1);
  Serial.print(",temp_close_f="); Serial.print(TEMP_CLOSE_F, 1);
  Serial.print(",rh_open_pct="); Serial.print(RH_OPEN_PCT, 1);
  Serial.print(",pv_present_v="); Serial.print(PV_PRESENT_V, 1);

  Serial.print(",fault="); Serial.print(faultBits);
  Serial.println();
}

// --------------------------- Setup / Loop ---------------------------
void setup() {
  Serial.begin(115200);

  pinMode(PIN_REN, OUTPUT);
  pinMode(PIN_LEN, OUTPUT);
  pinMode(PIN_RPWM, OUTPUT);
  pinMode(PIN_LPWM, OUTPUT);

  pinMode(PIN_LIMIT_OPEN, INPUT_PULLUP);
  pinMode(PIN_LIMIT_CLOSE, INPUT_PULLUP);

  pinMode(PIN_VALVE, OUTPUT);
  pinMode(PIN_LIGHT, OUTPUT);
  pinMode(PIN_FAN_PWM, OUTPUT);

  btsStop();
  digitalWrite(PIN_VALVE, LOW);
  digitalWrite(PIN_LIGHT, HIGH);
  analogWrite(PIN_FAN_PWM, 0);

  dht.begin();

  Wire.begin(); // uses core-defined TWI pins (PC0/PC1 assumed)
  luxSensor.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  lastUiHeartbeatMs = millis();
  lastChangeMs = millis();
  lastPosUpdateMs = millis();

  // Seed louvre position from limits
  if (limitCloseActive() && !limitOpenActive()) louvrePosPct = 0.0f;
  else if (limitOpenActive() && !limitCloseActive()) louvrePosPct = 100.0f;
  else louvrePosPct = 0.0f;
}

void loop() {
  pollSerial();
  updateCommsLoss();

  // Clear soft faults each loop (keep hard faults latched)
  faultBits &= ~(uint16_t)F_SENSOR_FAIL;
  faultBits &= ~(uint16_t)F_SOIL_OOR;

  updateLux();
  updateSoilAndBattery();
  updateSolarPanel();
  updateEcoTimersAndFault();
  updateDht();
  updateFrozenFault();
  updateLouvrePositionEstimate();

  if (mode == MODE_AUTO) {
    autoControlStep();
  } else {
    // Manual mode safety: stop on limit conflict + timeout handling
    uint32_t now = millis();

    if (limitOpenActive() && limitCloseActive()) {
      faultBits |= F_LIMIT_CONFLICT;
      btsStop();
      louvreState = L_FAULT;
    }

    if (louvreState == L_OPENING) {
      if (limitOpenActive()) { btsStop(); louvreState = L_OPEN; louvrePosPct = 100.0f; }
      else if (now - louvreMoveStartMs > LOUVRE_TIMEOUT_MS) { btsStop(); faultBits |= F_LOUVRE_TIMEOUT; louvreState = L_FAULT; }
    } else if (louvreState == L_CLOSING) {
      if (limitCloseActive()) { btsStop(); louvreState = L_CLOSED; louvrePosPct = 0.0f; }
      else if (now - louvreMoveStartMs > LOUVRE_TIMEOUT_MS) { btsStop(); faultBits |= F_LOUVRE_TIMEOUT; louvreState = L_FAULT; }
    }
  }

  // ECO load shedding in BOTH modes (after 30s): force outputs off
  if (ecoModeActive()) {
    valveOn = false;
    lightOn = false;
    fanPwm  = 0;
  }

  // Apply outputs
  digitalWrite(PIN_VALVE, valveOn ? HIGH : LOW);
  digitalWrite(PIN_LIGHT, lightOn ? HIGH : LOW);
  analogWrite(PIN_FAN_PWM, fanPwm);

  // Telemetry
  uint32_t now = millis();
  if (now - lastTeleMs >= TELEMETRY_MS) {
    lastTeleMs = now;
    printTelemetry();
  }
}
