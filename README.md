SMART GREENHOUSE CONTROL SYSTEM
Customer Deployment & Technical Reference Guide
================================================

1. SYSTEM OVERVIEW
------------------

This repository provisions a Raspberry Pi–based Smart Greenhouse Control System designed for:

• Environmental data acquisition
• Real-time dashboard visualization
• Local data logging (SQLite)
• Arduino-based sensor interface
• Secure remote access (optional via Tailscale)
• Firewall-protected network exposure

The system is designed for fresh Raspberry Pi OS installations and provides a fully automated deployment process.

2. SYSTEM ARCHITECTURE
----------------------

Hardware Components:
• Raspberry Pi (Raspberry Pi OS)
• Arduino Uno (USB connected)
• Environmental sensors connected to Arduino
• Local network (Ethernet or Wi-Fi)

Software Stack:
• Node-RED (Application Layer)
• SQLite3 (Data Layer)
• Mosquitto (MQTT Broker)
• Arduino CLI (Embedded Interface)
• UFW Firewall (Network Security)
• Optional: Tailscale (Remote Access)

3. INSTALLATION PROCEDURE (Fresh Raspberry Pi OS)
-------------------------------------------------

Step 1 – Install Git

sudo apt update
sudo apt install -y git


Step 2 – Clone Repository

mkdir -p ~/git_repo
cd ~/git_repo
git clone https://github.com/jujuwasmydog/gh-project-dashboard.git
cd gh-project-dashboard


Step 3 – Execute Installer

sudo ./gh_installer_v5.sh

The installer performs the following:
• System package update
• Node.js 22 LTS installation
• Node-RED installation and configuration
• Systemd override to ensure non-root execution
• Installation of required Node-RED nodes
• SQLite installation
• Mosquitto installation
• Arduino CLI installation
• Firewall configuration (UFW)
• Deployment of flows and database schema
• Service enablement at boot


Step 4 – Reboot System

sudo reboot

This ensures:
• Group membership (dialout) becomes active
• Services initialize cleanly from boot


4. ACCESSING THE DASHBOARD
--------------------------

After installation:

http://<raspberry-pi-ip>:1880

If using Dashboard 2:

http://<raspberry-pi-ip>:1880/dashboard


5. ARDUINO INTEGRATION
----------------------

Requirements:
• Arduino Uno connected via USB
• Arduino recognized as /dev/ttyACM0 (typical)

Verify detection:

ls /dev/ttyACM*
arduino-cli board list


Uploading a Sketch:

./upload_arduino.sh

The utility will:
1. Detect available .ino files
2. Prompt for selection
3. Compile using arduino-cli
4. Detect connected Arduino port
5. Upload automatically


6. DATABASE LOGGING
-------------------

SQLite database schema is deployed to:

~/greenhouse/db/gh_db_v2.sql

Node-RED logs environmental data locally for academic project requirements.


7. SERVICE RESILIENCE
---------------------

The following services are enabled at boot:
• Node-RED
• Mosquitto
• Optional: Tailscale

Verify service status:

systemctl status nodered
systemctl status mosquitto

Verify boot enablement:

systemctl is-enabled nodered mosquitto


8. FIREWALL CONFIGURATION
-------------------------

UFW (Uncomplicated Firewall) is enabled.

Allowed ports:
• OpenSSH
• TCP 1880 (Node-RED)

Verify:

sudo ufw status verbose


9. OPTIONAL REMOTE ACCESS (TAILSCALE)
--------------------------------------

If remote access is desired:

sudo ./tailscale.sh

The script will:
• Install Tailscale
• Start tailscaled service
• Present a login URL / QR code
• Join device to secure tailnet


10. UPDATING THE SYSTEM
-----------------------

To synchronize with the latest repository version:

cd ~/git_repo/gh-project-dashboard
git fetch origin
git reset --hard origin/main
git clean -fd


11. OPERATIONAL NOTES
---------------------

• Node-RED currently does not enforce dashboard authentication.
• LAN access to port 1880 is permitted by design.
• SQLite logging writes to SD card (long-term deployments should consider external storage).
• Arduino must remain connected for serial data acquisition.


12. INTENDED USE
----------------

This system is intended for:
• Educational demonstration
• Controlled greenhouse monitoring
• Embedded systems coursework
• Prototype environmental control deployments


13. TROUBLESHOOTING
-------------------

Node-RED logs:

sudo journalctl -u nodered -n 150 --no-pager

Arduino detection:

arduino-cli board list

Firewall status:

sudo ufw status


14. LICENSE
-----------

See LICENSE file for distribution terms.
