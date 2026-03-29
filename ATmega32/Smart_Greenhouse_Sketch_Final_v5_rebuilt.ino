
/*
  Smart Greenhouse – FINALIZED Integrated Sketch (v4)

  Added in this revision:
  - Non-blocking timing structure using millis() task scheduling
  - 4 per-row soil moisture sensors for precision irrigation
  - Per-row soil hysteresis thresholds and per-row run timing
  - Serial CSV telemetry logging with CSV header output
  - Asynchronous DS18B20 tank temperature conversion flow (optional)
  - Pump automatically follows active irrigation zone

  Notes:
  - ATmega32A Arduino-style mapping assumed:
      D0..D7  = PD0..PD7
      D8..D15 = PB0..PB7
      D16..D23= PC0..PC7
      A0..A7  = PA0..PA7
  - I2C assumed on PC0/PC1 per board core.
  - DHT22 reads are scheduled by millis(); the DHT library call itself is brief but not user-blocking.
  - DS18B20 is run in non-blocking mode using request/collect phases.
*/

#include <Wire.h>
#include <BH1750.h>
#include <DHT.h>
#include <math.h>

// Set to 1 when OneWire and DallasTemperature are installed and a DS18B20 is wired.
// Leave at 0 for a compile-safe build that uses a simulated tank temperature value.
#define USE_DS18B20_TANK_SENSOR 0

#if USE_DS18B20_TANK_SENSOR
#include <OneWire.h>
#include <DallasTemperature.h>
#endif

// --------------------------- Pin Map ---------------------------
// Primary environment sensors
#define PIN_DHT              12      // PB4
#define PIN_BATT_ADC         A1      // PA1/ADC1
#define PIN_PV_ADC           A6      // PA6/ADC6

// Per-row soil sensors (new precision irrigation)
#define PIN_SOIL_ROW1        A2      // PA2/ADC2
#define PIN_SOIL_ROW2        A3      // PA3/ADC3
#define PIN_SOIL_ROW3        A4      // PA4/ADC4
#define PIN_SOIL_ROW4        A5      // PA5/ADC5

// BTS7960 louvre driver
#define PIN_RPWM             5       // PD5 PWM
#define PIN_LPWM             4       // PD4 PWM
#define PIN_REN              8       // PB0
#define PIN_LEN              9       // PB1

// Limit switches
#define PIN_LIMIT_OPEN       2       // PD2 / INT0
#define PIN_LIMIT_CLOSE      3       // PD3 / INT1

// Outputs
#define PIN_LIGHT            11      // PB3
#define PIN_FAN_PWM          7       // PD7
#define PIN_PUMP             6       // PD6  (added master irrigation pump output)

// 4 row valves
#define PIN_VALVE_ROW1       10      // PB2
#define PIN_VALVE_ROW2       13      // PB5
#define PIN_VALVE_ROW3       14      // PB6
#define PIN_VALVE_ROW4       15      // PB7

// Tank heating
#define PIN_TANK_TEMP        18      // PC2 (DS18B20 data)
#define PIN_TANK_HEATER      19      // PC3

// --------------------------- Constants ---------------------------
#define DHTTYPE DHT22
const uint8_t NUM_ROWS = 4;

// Scheduler periods
const uint32_t TELEMETRY_MS          = 1000UL;
const uint32_t DHT_INTERVAL_MS       = 2500UL;
const uint32_t SOIL_INTERVAL_MS      = 1500UL;
const uint32_t LUX_INTERVAL_MS       = 1000UL;
const uint32_t BATTERY_INTERVAL_MS   = 2000UL;
const uint32_t PV_INTERVAL_MS        = 2000UL;
const uint32_t TANK_REQUEST_MS       = 5000UL;
const uint32_t TANK_CONVERT_MS       = 900UL;     // 12-bit DS18B20 max conversion ~750ms
const uint32_t FROZEN_WINDOW_MS      = 60000UL;
const uint32_t LOUVRE_TIMEOUT_MS     = 20000UL;
const uint32_t UI_HEARTBEAT_MS       = 15000UL;
const uint32_t ECO_DELAY_MS          = 30000UL;

// Louvre / climate thresholds
float TEMP_OPEN_F  = 85.0f;
float TEMP_CLOSE_F = 65.0f;
float RH_OPEN_PCT  = 85.0f;
float TEMP_HYST_F  = 2.0f;
float RH_HYST_PCT  = 3.0f;
uint8_t LOUVRE_PWM = 200;
uint32_t FULL_TRAVEL_MS = 12000UL;

// Tank heater hysteresis
float TANK_HEAT_ON_F  = 40.0f;
float TANK_HEAT_OFF_F = 45.0f;

// Irrigation defaults
uint32_t rowDurationMs[NUM_ROWS] = {60000UL, 60000UL, 60000UL, 60000UL};
uint8_t rowStartPct[NUM_ROWS]    = {35, 35, 35, 35};
uint8_t rowStopPct[NUM_ROWS]     = {55, 55, 55, 55};
uint32_t rowMaxCycleMs[NUM_ROWS] = {600000UL, 600000UL, 600000UL, 600000UL};
bool rowEnabled[NUM_ROWS]        = {true, true, true, true};

// Soil calibration per row
int soilDryRaw[NUM_ROWS]         = {780, 780, 780, 780};
int soilWetRaw[NUM_ROWS]         = {380, 380, 380, 380};

// ADC calibration
float ADC_VREF = 5.0f;
float BATT_DIVIDER_RATIO = 4.0f;
float LOW_BATT_V = 11.2f;
float PV_DIVIDER_RATIO = 7.82f;
float PV_PRESENT_V = 10.0f;

// --------------------------- Devices ---------------------------
DHT dht(PIN_DHT, DHTTYPE);
BH1750 luxSensor;
#if USE_DS18B20_TANK_SENSOR
OneWire oneWire(PIN_TANK_TEMP);
DallasTemperature tankTempSensor(&oneWire);
#endif

// --------------------------- Enums / State ---------------------------
enum Mode : uint8_t { MODE_AUTO=0, MODE_MANUAL=1 };
enum IrrigationMode : uint8_t { IRR_MANUAL=0, IRR_SOIL=1, IRR_SCHEDULE=2 };
enum LouvreState : uint8_t { L_UNKNOWN=0, L_OPEN=1, L_CLOSED=2, L_OPENING=3, L_CLOSING=4, L_FAULT=5 };

enum FaultBits {
  F_SENSOR_FAIL    = 1 << 0,
  F_SOIL_OOR       = 1 << 1,
  F_LOUVRE_TIMEOUT = 1 << 2,
  F_LIMIT_CONFLICT = 1 << 3,
  F_LOW_BATT       = 1 << 4,
  F_FROZEN_SENSOR  = 1 << 5,
  F_COMM_LOSS      = 1 << 6,
  F_PV_MISSING     = 1 << 7,
  F_TANK_SENSOR    = 1 << 8
};

Mode mode = MODE_AUTO;
IrrigationMode irrigationMode = IRR_MANUAL;
LouvreState louvreState = L_UNKNOWN;
uint16_t faultBits = 0;

// Measurements
float tempF = NAN, rhPct = NAN;
float lastTempF = NAN, lastRhPct = NAN;
uint16_t lux = 0;
float battV = 0.0f;
float pvV = 0.0f;
bool pvPresent = false;
float tankTempF = NAN;
float tankTempSimF = 50.0f;   // used when USE_DS18B20_TANK_SENSOR = 0

int soilRaw[NUM_ROWS] = {0,0,0,0};
uint8_t soilPct[NUM_ROWS] = {0,0,0,0};

// Outputs / status
bool lightOn = true;
uint8_t fanPwm = 0;
bool tankHeaterOn = false;
bool rowValveOn[NUM_ROWS] = {false,false,false,false};
bool pumpOn = false;

// Irrigation runtime state
bool rowsRunActive = false;
bool rowsRunAuto = false;
int8_t activeRowIndex = -1;
uint32_t activeRowStartMs = 0;
uint32_t activeRowCycleStartMs = 0;
uint32_t nextScheduleStartMs = 0;
uint32_t SCHEDULE_INTERVAL_MS = 12UL * 60UL * 60UL * 1000UL;

// Timers
uint32_t lastTeleMs = 0;
uint32_t lastDhtMs = 0;
uint32_t lastSoilMs = 0;
uint32_t lastLuxMs = 0;
uint32_t lastBatteryMs = 0;
uint32_t lastPvMs = 0;
uint32_t lastUiHeartbeatMs = 0;
uint32_t lastChangeMs = 0;
uint32_t louvreMoveStartMs = 0;
uint32_t lastPosUpdateMs = 0;
uint32_t ecoStartMs = 0;

// Tank sensor async state
uint32_t lastTankRequestMs = 0;
bool tankConversionPending = false;

// Louvre estimate
float louvrePosPct = 0.0f;
int8_t louvreDir = 0;

// Telemetry
bool telemetryHeaderPrinted = false;

// --------------------------- Helpers ---------------------------
static bool limitOpenActive()  { return digitalRead(PIN_LIMIT_OPEN)  == LOW; }
static bool limitCloseActive() { return digitalRead(PIN_LIMIT_CLOSE) == LOW; }

static int clampInt(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi ? hi : v); }
static float clampFloat(float v, float lo, float hi) { return (v < lo) ? lo : (v > hi ? hi : v); }

static uint8_t rowValvePin(uint8_t idx) {
  const uint8_t pins[NUM_ROWS] = {PIN_VALVE_ROW1, PIN_VALVE_ROW2, PIN_VALVE_ROW3, PIN_VALVE_ROW4};
  return pins[idx];
}

static uint8_t rowSoilPin(uint8_t idx) {
  const uint8_t pins[NUM_ROWS] = {PIN_SOIL_ROW1, PIN_SOIL_ROW2, PIN_SOIL_ROW3, PIN_SOIL_ROW4};
  return pins[idx];
}

static int readAnalogAvgNoDelay(uint8_t pin, uint8_t n=4) {
  long sum = 0;
  for (uint8_t i = 0; i < n; i++) sum += analogRead(pin);
  return (int)(sum / n);
}

static uint8_t mapSoilRawToPctByRow(int raw, uint8_t row) {
  if (row >= NUM_ROWS) return 0;
  if (soilDryRaw[row] == soilWetRaw[row]) return 0;

  long pct;
  if (soilDryRaw[row] > soilWetRaw[row]) {
    pct = (long)(soilDryRaw[row] - raw) * 100L / (long)(soilDryRaw[row] - soilWetRaw[row]);
  } else {
    pct = (long)(raw - soilDryRaw[row]) * 100L / (long)(soilWetRaw[row] - soilDryRaw[row]);
  }
  return (uint8_t)clampInt((int)pct, 0, 100);
}

static void allRowsOff() {
  for (uint8_t i = 0; i < NUM_ROWS; i++) rowValveOn[i] = false;
}

static void setRowState(uint8_t idx, bool on) {
  if (idx >= NUM_ROWS) return;
  rowValveOn[idx] = on;
}

static void updatePumpDemand() {
  pumpOn = false;
  for (uint8_t i = 0; i < NUM_ROWS; i++) {
    if (rowValveOn[i]) { pumpOn = true; break; }
  }
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

static void stopRowRun() {
  rowsRunActive = false;
  rowsRunAuto = false;
  activeRowIndex = -1;
  activeRowStartMs = 0;
  activeRowCycleStartMs = 0;
  allRowsOff();
  updatePumpDemand();
}

static int8_t firstEnabledRow() {
  for (uint8_t i = 0; i < NUM_ROWS; i++) if (rowEnabled[i]) return (int8_t)i;
  return -1;
}

static int8_t nextEnabledRow(uint8_t startExclusive) {
  for (uint8_t i = startExclusive + 1; i < NUM_ROWS; i++) if (rowEnabled[i]) return (int8_t)i;
  return -1;
}

static bool ecoConditionRaw() {
  return ((faultBits & F_LOW_BATT) && !pvPresent);
}

static bool ecoModeActive() {
  return (ecoStartMs != 0 && (millis() - ecoStartMs) >= ECO_DELAY_MS);
}

// --------------------------- Sensor Updates ---------------------------
static void updateDhtTask() {
  uint32_t now = millis();
  if (now - lastDhtMs < DHT_INTERVAL_MS) return;
  lastDhtMs = now;

  float h = dht.readHumidity();
  float tC = dht.readTemperature();
  if (isnan(h) || isnan(tC)) {
    faultBits |= F_SENSOR_FAIL;
    return;
  }

  faultBits &= ~F_SENSOR_FAIL;
  rhPct = h;
  tempF = tC * 9.0f / 5.0f + 32.0f;
}

static void updateSoilTask() {
  uint32_t now = millis();
  if (now - lastSoilMs < SOIL_INTERVAL_MS) return;
  lastSoilMs = now;

  bool anyOor = false;
  for (uint8_t i = 0; i < NUM_ROWS; i++) {
    soilRaw[i] = readAnalogAvgNoDelay(rowSoilPin(i), 4);
    if (soilRaw[i] < 0 || soilRaw[i] > 1023) anyOor = true;
    soilPct[i] = mapSoilRawToPctByRow(soilRaw[i], i);
  }

  if (anyOor) faultBits |= F_SOIL_OOR;
  else faultBits &= ~F_SOIL_OOR;
}

static void updateLuxTask() {
  uint32_t now = millis();
  if (now - lastLuxMs < LUX_INTERVAL_MS) return;
  lastLuxMs = now;
  lux = (uint16_t)luxSensor.readLightLevel();
}

static void updateBatteryTask() {
  uint32_t now = millis();
  if (now - lastBatteryMs < BATTERY_INTERVAL_MS) return;
  lastBatteryMs = now;

  int battRaw = readAnalogAvgNoDelay(PIN_BATT_ADC, 4);
  battV = (float)battRaw * (ADC_VREF / 1023.0f) * BATT_DIVIDER_RATIO;
  if (battV > 0.5f && battV < LOW_BATT_V) faultBits |= F_LOW_BATT;
  else faultBits &= ~F_LOW_BATT;
}

static void updatePvTask() {
  uint32_t now = millis();
  if (now - lastPvMs < PV_INTERVAL_MS) return;
  lastPvMs = now;

  int pvRaw = readAnalogAvgNoDelay(PIN_PV_ADC, 4);
  pvV = (float)pvRaw * (ADC_VREF / 1023.0f) * PV_DIVIDER_RATIO;
  pvPresent = (pvV > PV_PRESENT_V);
}

static void updateEcoTask() {
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

static void updateTankTempTask() {
  uint32_t now = millis();

#if USE_DS18B20_TANK_SENSOR
  if (!tankConversionPending) {
    if (now - lastTankRequestMs >= TANK_REQUEST_MS) {
      tankTempSensor.requestTemperatures();
      lastTankRequestMs = now;
      tankConversionPending = true;
    }
    return;
  }

  if (now - lastTankRequestMs < TANK_CONVERT_MS) return;

  float tC = tankTempSensor.getTempCByIndex(0);
  tankConversionPending = false;

  if (tC == DEVICE_DISCONNECTED_C || tC < -55.0f || tC > 125.0f) {
    tankTempF = NAN;
    faultBits |= F_TANK_SENSOR;
    return;
  }

  faultBits &= ~F_TANK_SENSOR;
  tankTempF = tC * 9.0f / 5.0f + 32.0f;
#else
  // Compile-safe fallback when DS18B20 support is disabled.
  // You can adjust the simulated value with: SET TANK_TEMP_SIM_F <value>
  static uint32_t lastTankSimMs = 0;
  if (now - lastTankSimMs < TANK_REQUEST_MS) return;
  lastTankSimMs = now;
  tankTempF = tankTempSimF;
  faultBits &= ~F_TANK_SENSOR;
#endif
}

static void updateFrozenFaultTask() {
  uint32_t now = millis();
  if (isnan(tempF) || isnan(rhPct)) return;

  if (isnan(lastTempF) || isnan(lastRhPct)) {
    lastTempF = tempF;
    lastRhPct = rhPct;
    lastChangeMs = now;
    return;
  }

  const float EPS_T = 0.05f;
  const float EPS_H = 0.05f;

  if (fabs(tempF - lastTempF) > EPS_T || fabs(rhPct - lastRhPct) > EPS_H) {
    lastTempF = tempF;
    lastRhPct = rhPct;
    lastChangeMs = now;
    faultBits &= ~F_FROZEN_SENSOR;
  } else if (now - lastChangeMs >= FROZEN_WINDOW_MS) {
    faultBits |= F_FROZEN_SENSOR;
  }
}

static void updateCommsLossTask() {
  if (millis() - lastUiHeartbeatMs > UI_HEARTBEAT_MS) faultBits |= F_COMM_LOSS;
  else faultBits &= ~F_COMM_LOSS;
}

// --------------------------- Heater ---------------------------
static void controlTankHeater() {
  if (isnan(tankTempF) || ecoModeActive()) {
    tankHeaterOn = false;
    return;
  }

  if (tankTempF < TANK_HEAT_ON_F) tankHeaterOn = true;
  else if (tankTempF > TANK_HEAT_OFF_F) tankHeaterOn = false;
}

// --------------------------- Louvre ---------------------------
static void updateLouvreStateFromLimits() {
  if (limitOpenActive() && !limitCloseActive()) louvreState = L_OPEN;
  else if (limitCloseActive() && !limitOpenActive()) louvreState = L_CLOSED;
}

static void updateLouvrePositionEstimate() {
  uint32_t now = millis();
  if (lastPosUpdateMs == 0) lastPosUpdateMs = now;

  if (limitCloseActive() && !limitOpenActive()) { louvrePosPct = 0.0f; lastPosUpdateMs = now; return; }
  if (limitOpenActive() && !limitCloseActive()) { louvrePosPct = 100.0f; lastPosUpdateMs = now; return; }

  if (louvreDir == 0) { lastPosUpdateMs = now; return; }

  uint32_t dt = now - lastPosUpdateMs;
  lastPosUpdateMs = now;

  if (FULL_TRAVEL_MS < 1000UL) FULL_TRAVEL_MS = 1000UL;
  float deltaPct = (100.0f * (float)dt) / (float)FULL_TRAVEL_MS;
  if (louvreDir > 0) louvrePosPct += deltaPct;
  else louvrePosPct -= deltaPct;

  louvrePosPct = clampFloat(louvrePosPct, 0.0f, 100.0f);
}

static void autoControlStep() {
  if (limitOpenActive() && limitCloseActive()) {
    faultBits |= F_LIMIT_CONFLICT;
    btsStop();
    louvreState = L_FAULT;
    return;
  } else {
    faultBits &= ~F_LIMIT_CONFLICT;
  }

  if (faultBits & F_COMM_LOSS) {
    btsStop();
    return;
  }

  if (ecoModeActive()) {
    lightOn = false;
    fanPwm = 0;

    bool emergency = (!isnan(tempF) && tempF > (TEMP_OPEN_F + 5.0f)) ||
                     (!isnan(rhPct) && rhPct > (RH_OPEN_PCT + 5.0f));

    if (!emergency) {
      btsStop();
      return;
    }
  }

  bool wantOpen = false;
  bool wantClose = false;

  if (!isnan(tempF) && tempF > TEMP_OPEN_F) wantOpen = true;
  if (!isnan(rhPct) && rhPct > RH_OPEN_PCT) wantOpen = true;

  if (!ecoModeActive()) {
    if (!isnan(tempF) && !isnan(rhPct)) {
      if (tempF < (TEMP_CLOSE_F - TEMP_HYST_F) &&
          rhPct < (RH_OPEN_PCT - RH_HYST_PCT)) {
        wantClose = true;
      }
    }
  }

  if (louvreState != L_OPENING && louvreState != L_CLOSING) updateLouvreStateFromLimits();

  uint32_t now = millis();

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

  if (wantOpen && louvreState != L_OPEN) {
    if (limitOpenActive()) { louvreState = L_OPEN; btsStop(); louvrePosPct = 100.0f; }
    else { louvreState = L_OPENING; louvreMoveStartMs = now; btsOpen(LOUVRE_PWM); }
  } else if (wantClose && louvreState != L_CLOSED) {
    if (limitCloseActive()) { louvreState = L_CLOSED; btsStop(); louvrePosPct = 0.0f; }
    else { louvreState = L_CLOSING; louvreMoveStartMs = now; btsClose(LOUVRE_PWM); }
  } else {
    btsStop();
  }

  if (!isnan(tempF) && tempF > TEMP_CLOSE_F) {
    fanPwm = (uint8_t)clampInt((int)((tempF - TEMP_CLOSE_F) * 12.0f), 0, 255);
  } else {
    fanPwm = 0;
  }
}

// --------------------------- Irrigation ---------------------------
static void startRow(uint8_t row, bool autoRequest) {
  stopRowRun();
  if (row >= NUM_ROWS || !rowEnabled[row]) return;
  rowsRunActive = true;
  rowsRunAuto = autoRequest;
  activeRowIndex = (int8_t)row;
  activeRowStartMs = millis();
  activeRowCycleStartMs = activeRowStartMs;
  setRowState(row, true);
  updatePumpDemand();
}

static void startSequentialRowRun(bool autoRequest=false) {
  int8_t first = firstEnabledRow();
  if (first < 0) return;
  stopRowRun();
  rowsRunActive = true;
  rowsRunAuto = autoRequest;
  activeRowIndex = first;
  activeRowStartMs = millis();
  activeRowCycleStartMs = activeRowStartMs;
  setRowState((uint8_t)first, true);
  updatePumpDemand();
}

static bool rowNeedsWater(uint8_t row) {
  if (row >= NUM_ROWS || !rowEnabled[row]) return false;
  return (soilPct[row] <= rowStartPct[row]);
}

static void finishActiveRowAndAdvance() {
  if (activeRowIndex >= 0 && activeRowIndex < (int8_t)NUM_ROWS) {
    setRowState((uint8_t)activeRowIndex, false);
  }

  if (irrigationMode == IRR_SCHEDULE) {
    int8_t nextRow = nextEnabledRow((uint8_t)activeRowIndex);
    if (nextRow >= 0) {
      activeRowIndex = nextRow;
      activeRowStartMs = millis();
      activeRowCycleStartMs = activeRowStartMs;
      setRowState((uint8_t)activeRowIndex, true);
      updatePumpDemand();
      return;
    }
    stopRowRun();
    return;
  }

  if (irrigationMode == IRR_SOIL) {
    uint8_t startIdx = (activeRowIndex < 0) ? 0 : (uint8_t)activeRowIndex;
    for (uint8_t k = 1; k <= NUM_ROWS; k++) {
      uint8_t idx = (startIdx + k) % NUM_ROWS;
      if (rowNeedsWater(idx)) {
        activeRowIndex = idx;
        activeRowStartMs = millis();
        activeRowCycleStartMs = activeRowStartMs;
        setRowState(idx, true);
        updatePumpDemand();
        return;
      }
    }
    stopRowRun();
    return;
  }

  stopRowRun();
}

static void updateSequentialRowRun() {
  if (!rowsRunActive || activeRowIndex < 0 || activeRowIndex >= (int8_t)NUM_ROWS) return;

  uint32_t now = millis();
  uint8_t row = (uint8_t)activeRowIndex;

  bool stopThisRow = false;

  if (now - activeRowStartMs >= rowDurationMs[row]) stopThisRow = true;

  if (rowsRunAuto && irrigationMode == IRR_SOIL) {
    if (soilPct[row] >= rowStopPct[row]) stopThisRow = true;
    if (now - activeRowCycleStartMs >= rowMaxCycleMs[row]) stopThisRow = true;
  }

  if (stopThisRow) finishActiveRowAndAdvance();
}

static void updateIrrigationAutomation() {
  uint32_t now = millis();

  if (ecoModeActive() || (faultBits & F_COMM_LOSS)) {
    stopRowRun();
    return;
  }

  if (irrigationMode == IRR_MANUAL) {
    if (rowsRunActive) updateSequentialRowRun();
    return;
  }

  if (rowsRunActive) {
    updateSequentialRowRun();
    return;
  }

  if (irrigationMode == IRR_SCHEDULE) {
    if (nextScheduleStartMs == 0) nextScheduleStartMs = now + SCHEDULE_INTERVAL_MS;
    if ((int32_t)(now - nextScheduleStartMs) >= 0) {
      startSequentialRowRun(true);
      nextScheduleStartMs = now + SCHEDULE_INTERVAL_MS;
    }
    return;
  }

  if (irrigationMode == IRR_SOIL) {
    for (uint8_t i = 0; i < NUM_ROWS; i++) {
      if (rowNeedsWater(i)) {
        startRow(i, true);
        return;
      }
    }
  }
}

// --------------------------- Serial ---------------------------
static void printHelp() {
  Serial.println(F("# Commands:"));
  Serial.println(F("# HEARTBEAT"));
  Serial.println(F("# MODE AUTO|MANUAL"));
  Serial.println(F("# LOUVRE OPEN|CLOSE|STOP"));
  Serial.println(F("# LIGHT ON|OFF"));
  Serial.println(F("# FAN <0..255>"));
  Serial.println(F("# IRRIGATION MANUAL|SOIL|SCHEDULE"));
  Serial.println(F("# ROW_ENABLE <1..4> ON|OFF"));
  Serial.println(F("# ROW <1..4> ON|OFF"));
  Serial.println(F("# RUN_ROWS"));
  Serial.println(F("# STOP_ROWS"));
  Serial.println(F("# RUN_SCHEDULE_NOW"));
  Serial.println(F("# CSV_HEADER"));
  Serial.println(F("# SET ROW1_MS <value>"));
  Serial.println(F("# SET ROW1_START_PCT <value>"));
  Serial.println(F("# SET ROW1_STOP_PCT <value>"));
  Serial.println(F("# SET ROW1_MAX_MS <value>"));
  Serial.println(F("# SET ROW1_DRY_RAW <value>"));
  Serial.println(F("# SET ROW1_WET_RAW <value>"));
  Serial.println(F("# SET SCHEDULE_INTERVAL_MS <value>"));
  Serial.println(F("# SET TANK_HEAT_ON_F <value>"));
  Serial.println(F("# SET TANK_HEAT_OFF_F <value>"));
  Serial.println(F("# SET TANK_TEMP_SIM_F <value>   (used when USE_DS18B20_TANK_SENSOR=0)"));
}


static void printTelemetryHeader() {
  Serial.println(F("ts_ms,mode,irrigation_mode,temp_f,rh_pct,tank_temp_f,lux,"
                   "soil1_raw,soil1_pct,soil2_raw,soil2_pct,soil3_raw,soil3_pct,soil4_raw,soil4_pct,"
                   "batt_v,pv_v,pv_present,louvre_state,louvre_pos_pct,"
                   "row1,row2,row3,row4,row1_en,row2_en,row3_en,row4_en,"
                   "row1_ms,row2_ms,row3_ms,row4_ms,"
                   "row1_start_pct,row2_start_pct,row3_start_pct,row4_start_pct,"
                   "row1_stop_pct,row2_stop_pct,row3_stop_pct,row4_stop_pct,"
                   "pump_on,heater_on,light_on,fan_pwm,rows_run_active,rows_run_auto,active_row,"
                   "temp_open_f,temp_close_f,rh_open_pct,tank_heat_on_f,tank_heat_off_f,"
                   "schedule_interval_ms,eco_mode,fault"));
  telemetryHeaderPrinted = true;
}

static void processSetKey(String key, float val) {
  if (key.equalsIgnoreCase("TEMP_OPEN_F")) TEMP_OPEN_F = val;
  else if (key.equalsIgnoreCase("TEMP_CLOSE_F")) TEMP_CLOSE_F = val;
  else if (key.equalsIgnoreCase("RH_OPEN_PCT")) RH_OPEN_PCT = val;
  else if (key.equalsIgnoreCase("FULL_TRAVEL_MS")) FULL_TRAVEL_MS = (uint32_t)val;
  else if (key.equalsIgnoreCase("PV_PRESENT_V")) PV_PRESENT_V = val;
  else if (key.equalsIgnoreCase("TANK_HEAT_ON_F")) TANK_HEAT_ON_F = val;
  else if (key.equalsIgnoreCase("TANK_HEAT_OFF_F")) TANK_HEAT_OFF_F = val;
  else if (key.equalsIgnoreCase("TANK_TEMP_SIM_F")) tankTempSimF = val;
  else if (key.equalsIgnoreCase("SCHEDULE_INTERVAL_MS")) {
    SCHEDULE_INTERVAL_MS = (uint32_t)val;
    if (irrigationMode == IRR_SCHEDULE) nextScheduleStartMs = millis() + SCHEDULE_INTERVAL_MS;
  }
  else {
    for (uint8_t i = 0; i < NUM_ROWS; i++) {
      String base = "ROW" + String(i + 1);
      if (key.equalsIgnoreCase(base + "_MS")) rowDurationMs[i] = (uint32_t)val;
      else if (key.equalsIgnoreCase(base + "_START_PCT")) rowStartPct[i] = (uint8_t)clampInt((int)val, 0, 100);
      else if (key.equalsIgnoreCase(base + "_STOP_PCT")) rowStopPct[i] = (uint8_t)clampInt((int)val, 0, 100);
      else if (key.equalsIgnoreCase(base + "_MAX_MS")) rowMaxCycleMs[i] = (uint32_t)val;
      else if (key.equalsIgnoreCase(base + "_DRY_RAW")) soilDryRaw[i] = (int)val;
      else if (key.equalsIgnoreCase(base + "_WET_RAW")) soilWetRaw[i] = (int)val;
    }
  }
}

static void processLine(String line) {
  line.trim();
  if (!line.length()) return;

  if (line.equalsIgnoreCase("HELP")) { printHelp(); return; }
  if (line.equalsIgnoreCase("HEARTBEAT")) { lastUiHeartbeatMs = millis(); return; }
  if (line.equalsIgnoreCase("CSV_HEADER")) { printTelemetryHeader(); return; }

  if (line.startsWith("MODE")) {
    if (line.indexOf("AUTO") > 0) mode = MODE_AUTO;
    if (line.indexOf("MANUAL") > 0) mode = MODE_MANUAL;
    return;
  }

  if (line.startsWith("IRRIGATION")) {
    if (line.indexOf("MANUAL") > 0) { irrigationMode = IRR_MANUAL; stopRowRun(); }
    if (line.indexOf("SOIL") > 0)   { irrigationMode = IRR_SOIL;   stopRowRun(); }
    if (line.indexOf("SCHEDULE") > 0) {
      irrigationMode = IRR_SCHEDULE;
      stopRowRun();
      nextScheduleStartMs = millis() + SCHEDULE_INTERVAL_MS;
    }
    return;
  }

  if (line.startsWith("SET")) {
    int sp1 = line.indexOf(' ');
    int sp2 = line.indexOf(' ', sp1 + 1);
    if (sp1 < 0 || sp2 < 0) return;
    String key = line.substring(sp1 + 1, sp2);
    float val = line.substring(sp2 + 1).toFloat();
    processSetKey(key, val);
    return;
  }

  if (line.startsWith("ROW_ENABLE ")) {
    int sp1 = line.indexOf(' ');
    int sp2 = line.indexOf(' ', sp1 + 1);
    if (sp2 > 0) {
      int rowNum = line.substring(sp1 + 1, sp2).toInt();
      String state = line.substring(sp2 + 1);
      if (rowNum >= 1 && rowNum <= (int)NUM_ROWS) {
        rowEnabled[rowNum - 1] = state.equalsIgnoreCase("ON");
        if (!rowEnabled[rowNum - 1] && activeRowIndex == (rowNum - 1)) stopRowRun();
      }
    }
    return;
  }

  if (line.equalsIgnoreCase("RUN_SCHEDULE_NOW")) {
    startSequentialRowRun(true);
    if (irrigationMode == IRR_SCHEDULE) nextScheduleStartMs = millis() + SCHEDULE_INTERVAL_MS;
    return;
  }

  if (mode == MODE_MANUAL) {
    if (line.startsWith("LOUVRE")) {
      if (line.indexOf("OPEN") > 0)  { louvreState = L_OPENING; louvreMoveStartMs = millis(); btsOpen(LOUVRE_PWM); }
      if (line.indexOf("CLOSE") > 0) { louvreState = L_CLOSING; louvreMoveStartMs = millis(); btsClose(LOUVRE_PWM); }
      if (line.indexOf("STOP") > 0)  { btsStop(); }
      return;
    }

    if (line.startsWith("LIGHT")) {
      if (line.indexOf("ON") > 0) lightOn = true;
      if (line.indexOf("OFF") > 0) lightOn = false;
      return;
    }

    if (line.startsWith("FAN")) {
      int sp = line.indexOf(' ');
      if (sp > 0) fanPwm = (uint8_t)clampInt(line.substring(sp + 1).toInt(), 0, 255);
      return;
    }

    if (line.equalsIgnoreCase("RUN_ROWS")) { startSequentialRowRun(false); return; }
    if (line.equalsIgnoreCase("STOP_ROWS") || line.equalsIgnoreCase("ROWS OFF")) { stopRowRun(); return; }

    if (line.startsWith("ROW ")) {
      int sp1 = line.indexOf(' ');
      int sp2 = line.indexOf(' ', sp1 + 1);
      if (sp2 > 0) {
        int rowNum = line.substring(sp1 + 1, sp2).toInt();
        String state = line.substring(sp2 + 1);
        if (rowNum >= 1 && rowNum <= (int)NUM_ROWS) {
          stopRowRun();
          if (state.equalsIgnoreCase("ON")) setRowState((uint8_t)(rowNum - 1), true);
          if (state.equalsIgnoreCase("OFF")) setRowState((uint8_t)(rowNum - 1), false);
          updatePumpDemand();
        }
      }
      return;
    }
  }
}

static void pollSerial() {
  static String buf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      processLine(buf);
      buf = "";
    } else if (c != '\r') {
      buf += c;
    }
  }
}

// --------------------------- Telemetry ---------------------------
static void printTelemetry() {
  if (!telemetryHeaderPrinted) printTelemetryHeader();

  Serial.print(millis());
  Serial.print(',');
  Serial.print((mode == MODE_AUTO) ? F("AUTO") : F("MANUAL"));
  Serial.print(',');
  if (irrigationMode == IRR_MANUAL) Serial.print(F("MANUAL"));
  else if (irrigationMode == IRR_SOIL) Serial.print(F("SOIL"));
  else Serial.print(F("SCHEDULE"));
  Serial.print(',');

  if (isnan(tempF)) Serial.print(F("nan")); else Serial.print(tempF, 1);
  Serial.print(',');
  if (isnan(rhPct)) Serial.print(F("nan")); else Serial.print(rhPct, 1);
  Serial.print(',');
  if (isnan(tankTempF)) Serial.print(F("nan")); else Serial.print(tankTempF, 1);
  Serial.print(',');
  Serial.print(lux);
  Serial.print(',');

  for (uint8_t i = 0; i < NUM_ROWS; i++) {
    Serial.print(soilRaw[i]); Serial.print(',');
    Serial.print(soilPct[i]); Serial.print(',');
  }

  Serial.print(battV, 2); Serial.print(',');
  Serial.print(pvV, 2); Serial.print(',');
  Serial.print(pvPresent ? 1 : 0); Serial.print(',');
  Serial.print((uint8_t)louvreState); Serial.print(',');
  Serial.print(louvrePosPct, 1); Serial.print(',');

  for (uint8_t i = 0; i < NUM_ROWS; i++) { Serial.print(rowValveOn[i] ? 1 : 0); Serial.print(','); }
  for (uint8_t i = 0; i < NUM_ROWS; i++) { Serial.print(rowEnabled[i] ? 1 : 0); Serial.print(','); }
  for (uint8_t i = 0; i < NUM_ROWS; i++) { Serial.print(rowDurationMs[i]); Serial.print(','); }
  for (uint8_t i = 0; i < NUM_ROWS; i++) { Serial.print(rowStartPct[i]); Serial.print(','); }
  for (uint8_t i = 0; i < NUM_ROWS; i++) { Serial.print(rowStopPct[i]); Serial.print(','); }

  Serial.print(pumpOn ? 1 : 0); Serial.print(',');
  Serial.print(tankHeaterOn ? 1 : 0); Serial.print(',');
  Serial.print(lightOn ? 1 : 0); Serial.print(',');
  Serial.print(fanPwm); Serial.print(',');
  Serial.print(rowsRunActive ? 1 : 0); Serial.print(',');
  Serial.print(rowsRunAuto ? 1 : 0); Serial.print(',');
  Serial.print(activeRowIndex + 1); Serial.print(',');

  Serial.print(TEMP_OPEN_F, 1); Serial.print(',');
  Serial.print(TEMP_CLOSE_F, 1); Serial.print(',');
  Serial.print(RH_OPEN_PCT, 1); Serial.print(',');
  Serial.print(TANK_HEAT_ON_F, 1); Serial.print(',');
  Serial.print(TANK_HEAT_OFF_F, 1); Serial.print(',');
  Serial.print(SCHEDULE_INTERVAL_MS); Serial.print(',');
  Serial.print(ecoModeActive() ? 1 : 0); Serial.print(',');
  Serial.println(faultBits);
}

// --------------------------- Output Apply ---------------------------
static void applyOutputs() {
  for (uint8_t i = 0; i < NUM_ROWS; i++) digitalWrite(rowValvePin(i), rowValveOn[i] ? HIGH : LOW);
  digitalWrite(PIN_PUMP, pumpOn ? HIGH : LOW);
  digitalWrite(PIN_LIGHT, lightOn ? HIGH : LOW);
  analogWrite(PIN_FAN_PWM, fanPwm);
  digitalWrite(PIN_TANK_HEATER, tankHeaterOn ? HIGH : LOW);
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

  pinMode(PIN_LIGHT, OUTPUT);
  pinMode(PIN_FAN_PWM, OUTPUT);
  pinMode(PIN_PUMP, OUTPUT);
  pinMode(PIN_VALVE_ROW1, OUTPUT);
  pinMode(PIN_VALVE_ROW2, OUTPUT);
  pinMode(PIN_VALVE_ROW3, OUTPUT);
  pinMode(PIN_VALVE_ROW4, OUTPUT);
  pinMode(PIN_TANK_HEATER, OUTPUT);

  btsStop();
  allRowsOff();
  updatePumpDemand();
  digitalWrite(PIN_LIGHT, HIGH);
  analogWrite(PIN_FAN_PWM, 0);
  digitalWrite(PIN_TANK_HEATER, LOW);

  dht.begin();
#if USE_DS18B20_TANK_SENSOR
  tankTempSensor.begin();
  tankTempSensor.setWaitForConversion(false);
#else
  tankTempF = tankTempSimF;
#endif

  Wire.begin();
  luxSensor.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  lastUiHeartbeatMs = millis();
  lastChangeMs = millis();
  lastPosUpdateMs = millis();
  nextScheduleStartMs = millis() + SCHEDULE_INTERVAL_MS;

  if (limitCloseActive() && !limitOpenActive()) louvrePosPct = 0.0f;
  else if (limitOpenActive() && !limitCloseActive()) louvrePosPct = 100.0f;
  else louvrePosPct = 0.0f;

  printTelemetryHeader();
}

void loop() {
  pollSerial();
  updateCommsLossTask();

  // Clear soft faults that are re-evaluated by their tasks
  faultBits &= ~(uint16_t)F_SENSOR_FAIL;
  faultBits &= ~(uint16_t)F_SOIL_OOR;
  faultBits &= ~(uint16_t)F_TANK_SENSOR;

  updateDhtTask();
  updateSoilTask();
  updateLuxTask();
  updateBatteryTask();
  updatePvTask();
  updateEcoTask();
  updateTankTempTask();
  updateFrozenFaultTask();
  updateLouvrePositionEstimate();
  controlTankHeater();
  updateIrrigationAutomation();

  if (mode == MODE_AUTO) {
    autoControlStep();
  } else {
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

  if (ecoModeActive()) {
    stopRowRun();
    lightOn = false;
    fanPwm = 0;
    tankHeaterOn = false;
  }

  updatePumpDemand();
  applyOutputs();

  uint32_t now = millis();
  if (now - lastTeleMs >= TELEMETRY_MS) {
    lastTeleMs = now;
    printTelemetry();
  }
}
