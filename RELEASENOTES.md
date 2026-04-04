# Release Notes

## v1.0 - Initial Public Release

### Features
- Arduino Mega-based greenhouse controller
- Sensor support:
  - DHT22 (temperature/humidity)
  - BH1750 (light)
  - Soil moisture sensors
- Actuator control:
  - Louvre (linear actuator via BTS7960)
  - Fans (PWM via MOSFET)
  - Grow light (relay)
- Node-RED integration:
  - Serial parsing
  - Dashboard visualization
  - MQTT messaging
- SQLite database logging
- CSV logging
- Fault detection system (sensor + communication)

---

## v1.1 - Dashboard & Data Improvements

### Improvements
- Enhanced Node-RED flow structure
- Added historical chart support
- Improved CSV formatting and validation
- Introduced gh_data function for:
  - Last known good value handling
  - Filtering invalid sensor data
- Improved fault messaging and UI indicators

---

## v1.2 - Smart Greenhouse (Mega Port)

### Major Changes
- Migrated to expanded CSV protocol from Arduino
- Added:
  - Multi-zone irrigation data
  - System mode (AUTO/MANUAL)
  - Fan PWM percentage
  - Louvre position feedback
  - Light state integration
- Extended SQLite schema to support new fields

### Fixes
- Resolved intermittent zero-value drops
- Improved handling of invalid lux readings
- Stabilized soil moisture readings

---

## v1.3 - Installer & Deployment Updates

### Additions
- gh_installer_v6.sh:
  - Automated Node-RED setup
  - Directory creation:
    - ~/greenhouse/db
    - ~/greenhouse/logs
    - ~/greenhouse/dashboard
  - Deployment of:
    - flows.json
    - settings.js
    - index.html

### Improvements
- Streamlined setup process
- Reduced manual configuration steps

---

## Known Limitations

- Voltage sensing not fully implemented in firmware
- Dashboard button state synchronization (AUTO vs MANUAL) limited by UI framework
- MQTT "latest" topic may omit fault metadata

---

## Future Work

- Improved UI state synchronization
- Enhanced fault logging and history
- Full voltage/power monitoring integration
- Optional secure web access (reverse proxy / TLS)
