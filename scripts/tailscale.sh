#!/usr/bin/env bash
set -euo pipefail

log() { echo -e "\n[tailscale] $*\n"; }

if [[ "${EUID}" -ne 0 ]]; then
  echo "Run as root: sudo $0"
  exit 1
fi

log "Installing Tailscale"
curl -fsSL https://tailscale.com/install.sh | sh
# Installs the package and sets up the repository for your distro. :contentReference[oaicite:2]{index=2}

log "Enabling and starting tailscaled"
systemctl enable --now tailscaled

log "Starting login (this will print a login URL; optionally a QR code)"
# --qr prints a QR code for the login URL in the terminal. :contentReference[oaicite:3]{index=3}
tailscale up --qr

log "Done. Verify status:"
tailscale status
tailscale ip -4