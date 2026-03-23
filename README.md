Smart Greenhouse Control System

Overview
This project implements a distributed smart greenhouse system using an Arduino Mega 2560 and a Raspberry Pi. 
The Arduino performs real-time sensor acquisition and actuator control, while the Raspberry Pi handles data 
processing, Node-RED dashboard visualization, MQTT messaging, and SQLite database logging.

The system monitors environmental conditions and automatically controls ventilation, airflow, and lighting 
while providing real-time fault detection.

System Architecture
Architectural drawings can be found in /Docs_Drawings
Arduino wiring can be found in /Docs_Drawings

Key Design Features
- Real-time control loop on Arduino
- Distributed architecture (embedded + edge processing)
- Node-RED dashboard for visualization and control
- MQTT messaging for data flow
- SQLite database for historical logging
- Fault detection system with prioritization logic

Hardware Components

Sensors
- 2 × DHT22 (Temperature / Humidity)
- BH1750 (Ambient Light Sensor, I2C)
- Capacitive Soil Moisture Sensor
- Voltage Divider (planned for system voltage monitoring, v7)

Actuators
- Linear Actuators (Louvre / Skirt control via BTS7960)
- Fans (PWM via MOSFET drivers)
- Grow Light (Relay controlled)

Control Hardware
- Arduino Mega 2560
- Raspberry Pi
- BTS7960 Motor Drivers
- IRLB8721 MOSFETs
- Relay Module

Software Components
- Arduino Firmware (v6)
- Node-RED (dashboard + logic)
- MQTT (data transport)
- SQLite (data storage)
- Bash Scripts (system installation and automation)

Fault Detection System

Sensor Faults
- Triggered when sensor readings are invalid (e.g., -1, NaN, or out-of-range)
- Averaging logic disabled when any sensor fails

Communication Faults
- Triggered when serial communication between Arduino and Raspberry Pi is lost
- Watchdog timer detects stale data
- Communication faults override sensor faults to prevent stale data display

Installation

1. Clone the Repository
git clone https://github.com/jujuwasmydog/gh-project-dashboard.git
cd gh-project-dashboard

2. Run Installer
cd scripts
chmod +x gh_installer_v5.sh
./gh_installer_v5.sh

3. Restart the Machine
sudo reboot now

4. Upload Arduino Firmware
cd ~/git_repo/gh-project-dashboard/scripts
chmod +x upload_arduino_v4.sh
./upload_arduino_v4.sh

Optional: Remote Access Setup (Tailscale)
cd ~/git_repo/gh-project-dashboard/scripts
chmod +x tailscale.sh
./tailscale.sh

Validation

Local Network:
http://192.168.1.xxx:1880/dashboard

Tailscale Network:
http://xxx.xxx.xxx.xxx:1880/dashboard - replace xxx with the IP address provided by Tailscale

Repository Structure

Arduino/
   Arduino_MEGA_2560_gh_v6.ino - for use on Arduino Mega 2560

ATMega32/
   Smart_Greenhouse_Sketch_Updated.ino - for use on cusom ATMega32A MC (not tested)

node-red/
  flows.json - dashboard configuration

scripts/
  gh_installer_v5.sh - install all dependencies required to run this program
  upload_arduino_v4.sh - displays all .inos in this repo
  tailscale.sh - download and install all required items for tailscale vpn
  data_analysis.py - view GH stats (last: 24 hours, day, week, month, all time)

database/
  gh_db_v3.sql - database schema 

archive/
  Previous firmware versions

docs/
  diagrams/
    GH_wiring_diagram_arduino.pdf - pinout for Arduino Mega 2560 Use

Notes
- Voltage sensing hardware is included but not yet implemented in firmware (planned for v7)
- System is designed for modular expansion and future enhancements

Author pff
Smart Greenhouse Project
BSEE Capstone Project
