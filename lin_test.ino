// include libraries
#include <DHT.h> // library for dht22
#include <Wire.h> // library for i2c comms
#include <BH1750.h> // library for light sensor

// define pins
#define exhaust A3 // exhaust temp sensor
#define intake A11 // door temp sensor
#define DHTTYPE DHT22

#define cap_sense A1 // capacitive moisture sensor (scale as more are installed)
#define relay4 28 // input to relay 4 (controls grow lights)
#define lf 2 // left fan pwm
#define rf 4 // right fan pwm
#define dry_light 13 // indicate when soil is too dry

// === IBT-2 #1 roof actuator ===
#define roof_rpwm 5
#define roof_lpwm 6

// === IBT-2 #2 side actuators ===
#define side_rpwm 9
#define side_lpwm 10

// define objects
DHT dhtExhaust(exhaust, DHTTYPE);
DHT dhtIn(intake, DHTTYPE);
BH1750 lightSense; 

// define grow light state
bool lightOn = false;

// actuator state machine
bool actuatorsOpening = true;
unsigned long lastActuatorToggle = 0;
const unsigned long actuatorInterval = 2000; // 2 seconds
const uint8_t actuatorPWM = 255; // full speed for testing

// use F instead of C
static inline float cToF(float c) { return c * 1.8f + 32.0f; }

// set fan state 
void setFans(uint8_t pwm) {
  analogWrite(lf, pwm);
  analogWrite(rf, pwm);
}

// roof actuator control
void roof_extend(uint8_t pwm) {
  analogWrite(roof_rpwm, pwm);
  analogWrite(roof_lpwm, 0);
}

void roof_retract(uint8_t pwm) {
  analogWrite(roof_rpwm, 0);
  analogWrite(roof_lpwm, pwm);
}

void roof_stop() {
  analogWrite(roof_rpwm, 0);
  analogWrite(roof_lpwm, 0);
}

// side actuator control
void side_extend(uint8_t pwm) {
  analogWrite(side_rpwm, pwm);
  analogWrite(side_lpwm, 0);
}

void side_retract(uint8_t pwm) {
  analogWrite(side_rpwm, 0);
  analogWrite(side_lpwm, pwm);
}

void side_stop() {
  analogWrite(side_rpwm, 0);
  analogWrite(side_lpwm, 0);
}

// control both actuator groups together
void actuators_open(uint8_t pwm) {
  roof_extend(pwm);
  side_extend(pwm);
}

void actuators_close(uint8_t pwm) {
  roof_retract(pwm);
  side_retract(pwm);
}

void actuators_stop() {
  roof_stop();
  side_stop();
}

void setup() {
  Serial.begin(9600);

  pinMode(relay4, OUTPUT);
  digitalWrite(relay4, LOW); // start OFF

  pinMode(lf, OUTPUT);
  pinMode(rf, OUTPUT);
  setFans(255); // force fans to 100% for now

  pinMode(dry_light, OUTPUT);
  digitalWrite(dry_light, LOW);

  // actuator pwm pins
  pinMode(roof_rpwm, OUTPUT);
  pinMode(roof_lpwm, OUTPUT);
  pinMode(side_rpwm, OUTPUT);
  pinMode(side_lpwm, OUTPUT);

  actuators_stop();

  dhtExhaust.begin();
  dhtIn.begin();

  // collect data from light sensor (sda= 20; scl = 21)
  Wire.begin(); 
  if (!lightSense.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    lightSense.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x5C);
  }

  // start by opening
  actuatorsOpening = true;
  lastActuatorToggle = millis();
  actuators_open(actuatorPWM);
}

void loop() {
  // keep fans at 100% for now
  uint8_t fanPWM = 255;
  setFans(fanPWM);

  // actuator test cycle: open 2 sec, close 2 sec, repeat
  unsigned long now = millis();
  if (now - lastActuatorToggle >= actuatorInterval) {
    lastActuatorToggle = now;
    actuatorsOpening = !actuatorsOpening;

    if (actuatorsOpening) {
      actuators_open(actuatorPWM);
    } else {
      actuators_close(actuatorPWM);
    }
  }

  // read DHT22 sensors
  float exhaustC  = dhtExhaust.readTemperature();
  float intakeC = dhtIn.readTemperature();

  // create average temperature
  float sumC = 0.0f; int nC = 0;
  if (!isnan(exhaustC))  { sumC += exhaustC;  nC++; }
  if (!isnan(intakeC)) { sumC += intakeC; nC++; }
  float tempF = NAN;
  if (nC > 0) tempF = cToF(sumC / nC); // set to F

  // moisture
  int msRaw = analogRead(cap_sense);
  bool isDry = (msRaw >= 300);
  digitalWrite(dry_light, isDry ? HIGH : LOW);

  // determine how bright it is:
  float brightness = lightSense.readLightLevel();
  if (!isnan(brightness)) {
    if (brightness < 100.0f) {
      lightOn = true;
    } else if (brightness >= 240.0f) {
      lightOn = false;
    }
  }
  digitalWrite(relay4, lightOn ? HIGH : LOW);

  // log data:
  Serial.print(F("Exhaust Temp: "));
  if (isnan(exhaustC)) Serial.print(F("DHT error"));
  else { Serial.print(cToF(exhaustC), 1); Serial.print(F(" F")); }

  Serial.print(F(" | Intake Temp: "));
  if (isnan(intakeC)) Serial.print(F("DHT error"));
  else { Serial.print(cToF(intakeC), 1); Serial.print(F(" F")); }

  Serial.print(F(" | Avg Temp: "));
  if (isnan(tempF)) Serial.print(F("NaN"));
  else              Serial.print(tempF, 1);

  Serial.print(F(" | Fan Speed: "));
  Serial.print((fanPWM / 255.0) * 100.0); Serial.print(F("%"));

  Serial.print(F(" | Roof Actuator: "));
  Serial.print(actuatorsOpening ? F("OPENING") : F("CLOSING"));

  Serial.print(F(" | Side Actuators: "));
  Serial.print(actuatorsOpening ? F("OPENING") : F("CLOSING"));

  Serial.print(F(" | Moisture: "));
  Serial.print(msRaw);
  Serial.print(F(" | Dry LED: "));
  Serial.print(isDry ? F("ON") : F("OFF"));

  Serial.print(F(" | brightness: "));
  if (isnan(brightness)) Serial.print(F("NaN"));
  else Serial.print(brightness, 1);

  Serial.print(F(" | Light: "));
  Serial.print(lightOn ? F("ON") : F("OFF"));

  Serial.println();

  delay(250);
}
