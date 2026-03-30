# Smart Greenhouse Control System

## Overview
This project implements a distributed smart greenhouse system using an Arduino Mega 2560 and a Raspberry Pi.
The Arduino performs real-time sensor acquisition and actuator control, while the Raspberry Pi handles data
processing, Node-RED visualization and control, MQTT messaging, SQLite database logging, and local dashboard hosting.

The system monitors environmental conditions and automatically controls ventilation, airflow, and lighting
while providing real-time status reporting and fault detection.

## System Architecture
Architectural drawings can be found in `Docs_Diagrams/`.
Arduino wiring can be found in `Docs_Diagrams/`.

## Key Design Features
- Real-time control loop on Arduino
- Distributed architecture (embedded control + edge processing)
- Node-RED for logic, messaging, and integration
- HTML dashboard support for greenhouse monitoring
- MQTT messaging for data flow
- SQLite database for historical logging
- Fault detection system with prioritization logic

## Hardware Components

### Sensors
- 2 × DHT22 (temperature / humidity)
- BH1750 (ambient light sensor, I2C)
- Capacitive soil moisture sensors
- Voltage divider (planned for system voltage monitoring, v7)

### Actuators
- Linear actuators (louvre / skirt control via BTS7960)
- Fans (PWM via MOSFET drivers)
- Grow light (relay controlled)

### Control Hardware
- Arduino Mega 2560
- Raspberry Pi
- BTS7960 motor drivers
- IRLB8721 MOSFETs
- Relay module

## Software Components
- Arduino firmware (v6 / v7 development variants in repo)
- Node-RED (flows + runtime settings)
- HTML dashboard (`html/index.html`)
- MQTT (data transport)
- SQLite (data storage)
- Bash scripts (installation and automation)

## Fault Detection System

### Sensor Faults
- Triggered when sensor readings are invalid (for example `-1`, `NaN`, or out-of-range)
- Averaging logic is disabled when required sensor inputs are invalid

### Communication Faults
- Triggered when serial communication between Arduino and Raspberry Pi is lost
- Watchdog logic detects stale or missing data
- Communication faults override sensor-only status so stale values are not presented as valid

## Installation

### 1. Clone the Repository
```bash
git clone https://github.com/jujuwasmydog/gh-project-dashboard.git
cd gh-project-dashboard
```

### 2. Run Installer
```bash
cd scripts
chmod +x gh_installer_v6.sh
sudo ./gh_installer_v6.sh
```

### 3. Restart the Machine
```bash
sudo reboot now
```

### 4. Upload Arduino Firmware
```bash
cd ~/git_repo/gh-project-dashboard/scripts
chmod +x upload_arduino_v5.sh
./upload_arduino_v5.sh
```

### Optional: Remote Access Setup (Tailscale)
```bash
cd ~/git_repo/gh-project-dashboard/scripts
chmod +x tailscale.sh
./tailscale.sh
```

## What the Installer Does
The installer:
- installs required OS packages and Node-RED dependencies
- installs the required Node-RED nodes
- creates:
  - `~/greenhouse/db/`
  - `~/greenhouse/logs/`
  - `~/greenhouse/dashboard/`
- places `gh_db_v3.sql` into `~/greenhouse/db/`
- places `flows.json` into `~/.node-red/flows.json`
- places `settings.js` into `~/.node-red/settings.js`
- places `index.html` into `~/greenhouse/dashboard/index.html`
- enables required services
- installs Arduino CLI and AVR core support

## Validation

### Node-RED
Local network:
```text
http://192.168.1.xxx:1880
```

### Dashboard
The HTML dashboard is installed to:
```text
~/greenhouse/dashboard/index.html
```

### Tailscale Network
Use the Tailscale IP assigned to the Raspberry Pi for remote access.

## Repository Structure
```text
.
├── Archive/
│   └── Arduino_MEGA_2560_gh_v5.ino
├── Arduino/
│   ├── Arduino_MEGA_2560_gh_v6.ino
│   └── Arduino_MEGA_2560_GH_v7.ino
├── ATmega32/
│   ├── Smart_Greenhouse_Sketch_Final_v5_rebuilt.ino
│   └── Smart_Greenhouse_Sketch_Updated.ino
├── Database/
│   └── gh_db_v3.sql
├── Docs_Diagrams/
│   └── GH_wiring_diagram_arduino.pdf
├── html/
│   └── index.html
├── LICENSE
├── node-red/
│   ├── flows.json
│   └── settings.js
├── README.md
└── scripts/
    ├── data_analysis.py
    ├── gh_installer_v5.sh
    ├── gh_installer_v6.sh
    ├── tailscale.sh
    ├── testing/
    │   └── gh_installer_v6.sh
    └── upload_arduino_v5.sh
```

## File Notes
- `Arduino/Arduino_MEGA_2560_gh_v6.ino` - Arduino Mega 2560 firmware
- `Arduino/Arduino_MEGA_2560_GH_v7.ino` - newer Mega development variant
- `ATmega32/Smart_Greenhouse_Sketch_Updated.ino` - ATmega32A variant
- `Database/gh_db_v3.sql` - database schema
- `Docs_Diagrams/GH_wiring_diagram_arduino.pdf` - Arduino Mega wiring / pinout reference
- `html/index.html` - greenhouse HTML dashboard
- `node-red/flows.json` - Node-RED flow configuration
- `node-red/settings.js` - Node-RED runtime settings used for dashboard support
- `scripts/gh_installer_v6.sh` - current installer script
- `scripts/upload_arduino_v5.sh` - Arduino upload helper
- `scripts/tailscale.sh` - Tailscale install helper
- `scripts/data_analysis.py` - greenhouse historical data analysis script

## Notes
- Voltage sensing hardware is included but not yet fully implemented in firmware
- The project is structured for modular expansion and future enhancements
- `gh_installer_v6.sh` should be used in place of `gh_installer_v5.sh` for the current repo layout

## Author
pff  
Smart Greenhouse Project  
BSEE Capstone Project
