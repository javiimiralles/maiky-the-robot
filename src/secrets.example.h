// Copy this file to src/secrets.h (git-ignored) and fill in your values.
#pragma once

#define WIFI_SSID "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"

// LAN IP of the PC running the relay (`wrangler dev`) and its port.
#define RELAY_HOST "192.168.1.50"
#define RELAY_PORT 8787
// Same value as RELAY_TOKEN in relay/.dev.vars.
#define RELAY_TOKEN "change-me"
