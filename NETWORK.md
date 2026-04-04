# Network Architecture

## Overview
The Smart Greenhouse system uses a layered network architecture designed for reliability, local-first operation, and optional remote access.

The system is built around a Raspberry Pi acting as the central node, with an Arduino Mega handling real-time control over a serial connection.

---

## Core Components

### 1. Arduino Mega 2560 (Controller Layer)
- Connected via USB serial (/dev/ttyACM0)
- Handles:
  - Sensor acquisition
  - Control loops
  - Actuator commands
- Outputs data as structured CSV over serial

---

### 2. Raspberry Pi (Edge Processing Layer)
- Runs:
  - Node-RED (flows + dashboard logic)
  - Mosquitto MQTT broker
  - SQLite database
  - Custom HTML dashboard
- Responsibilities:
  - Parse incoming serial data
  - Apply validation and fault detection
  - Log data to CSV and SQLite
  - Publish MQTT topics
  - Serve UI/dashboard

---

## Data Flow

Arduino -> Serial -> Node-RED Parser -> 
    -> Live Processing (fault detection, gh_data)
    -> MQTT Publish (greenhouse/data/live)
    -> SQLite Logging
    -> CSV Logging
    -> Dashboard UI
    -> WebSocket (/ws/greenhouse)

---

## MQTT Topics

- greenhouse/data/live
  - Raw + processed live data (includes faults)

- greenhouse/data/latest
  - Database-derived latest sample (may omit fault metadata)

NOTE:
Avoid subscribing to greenhouse/data/# without filtering, as "latest" can overwrite richer "live" payloads.

---

## Web Interfaces

### Node-RED Dashboard
- URL: http://<pi-ip>:1880
- Used for development and diagnostics

### Custom Dashboard
- Path: ~/greenhouse/dashboard/index.html
- Served via Node-RED httpStatic
- Uses WebSocket: /ws/greenhouse

---

## Remote Access (Optional)

### Tailscale
- Provides secure remote access via private network
- Access Node-RED and dashboard using Tailscale IP or MagicDNS
- Ports typically used:
  - 1880 (Node-RED)
  - 80 (dashboard if proxied)

---

## Ports Summary

- 1880 → Node-RED
- 1883 → MQTT (optional external access)
- 80 → Dashboard (if using reverse proxy)
- 22 → SSH

---

## Fault Handling Integration

Two fault domains exist:

### 1. Sensor Faults
- Generated in Node-RED
- Based on missing or invalid values

### 2. Communication Faults
- Based on serial watchdog timeout
- Stored in flow context (arduino_comm_fault)

Both are merged into a unified system status for UI and logging.

---

## Design Philosophy

- Local-first operation (no cloud dependency)
- Separation of control (Arduino) and logic (Node-RED)
- Fault-tolerant data handling (last known good values)
- Expandable architecture (MQTT + modular flows)
