# ESP32 SIP CallMonitor

A minimal SIP call monitor built for the **WT32-ETH01** (ESP32 + LAN8720 Ethernet board).  
Registers as a SIP extension and lights up an LED on every incoming call.

---

## Features

- Ethernet connection via LAN8720
- SIP registration with MD5 Digest Authentication (RFC 3261)
- Automatic re-registration before expiry
- Incoming call detection – LED on during call, off on BYE/CANCEL
- All activity logged via Serial (115200 baud)

---

## Hardware

This sketch is designed for the **Wireless-Tag WT32-ETH01** – an ESP32 module with onboard LAN8720 Ethernet. The default pin mapping in the code matches this board exactly, so no wiring changes are needed.

It will also work on any custom ESP32 board with a LAN8720, as long as the GPIOs match.

| Component | Details |
|-----------|---------|
| MCU | ESP32 (e.g. WT32-ETH01) |
| Ethernet PHY | LAN8720 (onboard on WT32-ETH01) |
| LED | Any LED + resistor on GPIO 2 |

### LAN8720 Pin Mapping (matches WT32-ETH01)

| Signal | ESP32 GPIO |
|--------|-----------|
| MDC | 23 |
| MDIO | 18 |
| POWER | 16 |
| CLK | 0 (input) |
| PHY ADDR | 1 |

> These values are already set as defaults in the sketch – no changes needed for the WT32-ETH01.

---

## Configuration

Edit the defines at the top of the sketch before flashing:

```cpp
char SIP_SERVER[64] = "192.168.1.1";  // IP or hostname of your SIP server
char SIP_USER[64]   = "100";           // SIP extension
char SIP_PASS[64]   = "secret";        // SIP password
const int SIP_PORT        = 5060;
const int SIP_REG_TIMEOUT = 600;       // Registration expiry in seconds
const int LED_PIN = 2;                 // GPIO for indicator LED
```

---

## Dependencies

All libraries are part of the **ESP32 Arduino Core** (≥ 3.x) – no additional installs required:

- `ETH.h`
- `WiFiUdp.h`
- `MD5Builder.h`

Install the ESP32 core via Arduino Board Manager:  
`https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`

---

## SIP Behaviour

| Event | Action |
|-------|--------|
| ETH connected + IP assigned | SIP REGISTER sent |
| `401 Unauthorized` | Re-registers with MD5 Digest credentials |
| `200 OK` (REGISTER) | Registration confirmed |
| `INVITE` | LED on, responds with `180 Ringing` |
| `BYE` / `CANCEL` | LED off, responds with `200 OK` |
| ETH disconnected | SIP stopped, LED off |
| Re-registration | Automatic at 50% of expiry interval |

> **Note:** The ESP answers incoming calls with `180 Ringing` only and never picks up (`200 OK` for INVITE). It acts as a pure call indicator, not a SIP endpoint.

### Tested with

- **AVM FRITZ!Box** – configure a new DECT/IP telephone in the Fritz!Box telephony settings and use the assigned credentials
- **Siemens Unify OpenScape** – register as a SIP endpoint via the OpenScape administration interface

---

## Serial Output Example

```
=== ESP32 SIP CallMonitor ===
ETH Started
ETH Connected
ETH Got IP: 192.168.1.50
=== Starting SIP ===
Register without auth

>>> INCOMING CALL
>>> LED ON (Incoming Call)
>>> BYE – call ended
>>> LED OFF
```

---

## Using WiFi instead of Ethernet

The sketch uses `ETH.h` and the `ETHevent` handler. To run it on a standard ESP32 with WiFi, three things need to change:

1. **Replace includes** – swap `ETH.h` for `WiFi.h`
2. **Connect to WiFi** – replace `ETH.begin(...)` in `setup()` with:
   ```cpp
   WiFi.begin("your-ssid", "your-password");
   while (WiFi.status() != WL_CONNECTED) delay(500);
   startSIP();
   ```
3. **Remove the ETH event handler** – delete `ETHevent()` and the `Network.onEvent()` call; handle reconnects manually if needed.

Everything else (SIP, LED, UDP) works unchanged.

---

## License

MIT – do whatever you like with it.

---

![Badge](https://hitscounter.dev/api/hit?url=https%3A%2F%2Fgithub.com%2Fpeff74%2FESP32_SIP_Callmonitor&label=Hits&icon=github&color=%23198754&message=&style=flat&tz=UTC)
