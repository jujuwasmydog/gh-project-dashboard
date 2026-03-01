unsigned long lastPrint = 0;
unsigned long lastMoveUpdate = 0;
unsigned long lastLouvreToggle = 0;

// ---------------- GREEN bands ----------------
// Humidity GREEN: 55–70 %RH
const float H_MIN = 55.0;
const float H_MAX = 70.0;

// Lux GREEN: 10,000–40,000 lux
const long  LUX_MIN = 10000L;
const long  LUX_MAX = 40000L;

// Soil GREEN: 35–65 %
const int   SOIL_MIN = 35;
const int   SOIL_MAX = 65;

// Temperature (pick a GREEN band you want)
// Right now: mild greenhouse-ish (C): 24.0–27.0
// If you want °F, tell me and I’ll adjust.
const float T_MIN = 65.0;
const float T_MAX = 85.0;

// ---------------- initial values ----------------
float t1 = 25.0;
float t2 = 25.3;
float h1 = 62.0;
float h2 = 61.5;
int   soil = 55;        // percent
long  lux  = 22000;     // lux (MUST be long on Uno)

// ---------------- louvre behavior ----------------
const unsigned long FULL_TRAVEL_MS   = 10000;  // time to go 0->100 (sim)
const unsigned long LOUVRE_TOGGLE_MS = 30000;  // toggle target every 30s

float  louvre_pct   = 0.0;     // 0–100 percent open
int    louvre_target = 0;      // 0 or 100
int    louvre_dir    = 0;      // -1 closing, +1 opening, 0 stopped
String louvre_state  = "closed";

// ---------------- helpers ----------------
float clampf(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

int clampi(int x, int lo, int hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

long clampl(long x, long lo, long hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

// smooth drift helper (bounded random walk)
float driftf(float x, float step, float lo, float hi) {
  float n = (random(-100, 101) / 100.0) * step;   // [-step, +step]
  x += n;
  return clampf(x, lo, hi);
}

int drifti(int x, int step, int lo, int hi) {
  int n = random(-step, step + 1);
  x += n;
  return clampi(x, lo, hi);
}

long driftl(long x, long step, long lo, long hi) {
  long n = random(-((int)step), ((int)step) + 1);
  x += n;
  return clampl(x, lo, hi);
}

void setup() {
  Serial.begin(9600);

  randomSeed(analogRead(A0));

  lastMoveUpdate  = millis();
  lastLouvreToggle = millis();
}

void loop() {
  unsigned long now = millis();

  // ---------- louvre: toggle target every 30 seconds ----------
  if (now - lastLouvreToggle >= LOUVRE_TOGGLE_MS) {
    lastLouvreToggle = now;
    louvre_target = (louvre_target == 0) ? 100 : 0;
  }

  // ---------- simulate louvre movement (time-based) ----------
  unsigned long dt = now - lastMoveUpdate;
  lastMoveUpdate = now;

  if (louvre_pct < (float)louvre_target - 0.5) louvre_dir = +1;
  else if (louvre_pct > (float)louvre_target + 0.5) louvre_dir = -1;
  else louvre_dir = 0;

  if (louvre_dir != 0) {
    float delta_pct = (dt / (float)FULL_TRAVEL_MS) * 100.0 * louvre_dir;
    louvre_pct += delta_pct;
    louvre_pct = clampf(louvre_pct, 0.0, 100.0);
    louvre_state = (louvre_dir > 0) ? "opening" : "closing";
  } else {
    louvre_state = (louvre_target == 0) ? "closed" : "open";
  }

  // ---------- publish every 2 seconds ----------
  if (now - lastPrint >= 2000) {
    lastPrint = now;

    // ---------- GREEN-ONLY sensor simulation ----------
    // Temperature: small smooth drift, always inside green band
    t1 = driftf(t1, 0.15, T_MIN, T_MAX);
    t2 = driftf(t2, 0.15, T_MIN, T_MAX);

    // Humidity: small smooth drift, always inside green band
    h1 = driftf(h1, 0.40, H_MIN, H_MAX);
    h2 = driftf(h2, 0.40, H_MIN, H_MAX);

    // Soil: very slow drift, always inside green band
    soil = drifti(soil, 1, SOIL_MIN, SOIL_MAX);

    // Lux: smooth drift, always inside green band (and never negative)
    lux = driftl(lux, 500L, LUX_MIN, LUX_MAX);

    // Final hard clamps (belt + suspenders)
    t1 = clampf(t1, T_MIN, T_MAX);
    t2 = clampf(t2, T_MIN, T_MAX);
    h1 = clampf(h1, H_MIN, H_MAX);
    h2 = clampf(h2, H_MIN, H_MAX);
    soil = clampi(soil, SOIL_MIN, SOIL_MAX);
    lux = clampl(lux, LUX_MIN, LUX_MAX);

    // ---------- JSON output (keep formatting/fields) ----------
    Serial.print("{\"ts\":");
    Serial.print(now);

    Serial.print(",\"t1\":");
    Serial.print(t1, 1);

    Serial.print(",\"t2\":");
    Serial.print(t2, 1);

    Serial.print(",\"tavg\":");
    Serial.print((t1 + t2) / 2.0, 2);

    Serial.print(",\"h1\":");
    Serial.print(h1, 1);

    Serial.print(",\"h2\":");
    Serial.print(h2, 1);

    Serial.print(",\"havg\":");
    Serial.print((h1 + h2) / 2.0, 2);

    Serial.print(",\"soil\":");
    Serial.print(soil);

    Serial.print(",\"lux\":");
    Serial.print(lux);

    Serial.print(",\"louvre_pct\":");
    Serial.print(louvre_pct, 1);

    Serial.print(",\"louvre_state\":\"");
    Serial.print(louvre_state);
    Serial.println("\"}");
  }
}
