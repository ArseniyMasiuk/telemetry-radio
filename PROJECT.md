# telemetry-radio — Project Reference

Reference document for developers and AI assistants working on this codebase.

## Core Feature

The firmware runs on an **ESP32-S3** (or other USB-OTG-capable ESP) and acts as a **USB host** on the OTG port. A flight controller (FC) is plugged in via USB; the ESP reads and writes **MAVLink** traffic in both directions:

- **In (FC → outbound link):** MAVLink from the FC over USB CDC-ACM
- **Out (inbound link → FC):** MAVLink received from the ground side, written back to the FC over USB

Everything else in the project is transport on top of this USB MAVLink bridge.

## Module Roles

The same codebase is flashed with different compile-time settings to act as either an **Air Module** or a **Ground Module**.

### Air Module (on the aircraft)

- Installed on the plane, FC connected to the ESP32-S3 USB-OTG port
- Reads MAVLink from the FC and forwards it toward the ground station
- **Transport:** UART to an **ELRS RX module**, which sends telemetry over the radio link to the GS
- **Configuration:** enable `UART_COMMUNICATION` in `main/main.c` (disable `UDP_WIFI_COMMUNICATION`)

### Ground Module (on the ground)

- Sits on the ground with WiFi enabled
- Receives MAVLink from the **Air Module** and relays it to devices on its network — typically a **PC running Mission Planner**
- **Transport:** WiFi soft-AP + UDP (port 14550, standard MAVLink)
- Connected clients (Mission Planner, etc.) join the ESP's WiFi and receive MAVLink over UDP
- **Configuration:** enable `UDP_WIFI_COMMUNICATION` in `main/main.c` (disable `UART_COMMUNICATION`)

### Role selection (`main/main.c`)

Only **one** transport define should be active per build:

```c
#define UART_COMMUNICATION        // Air Module — ELRS RX via UART
// #define UDP_WIFI_COMMUNICATION  // Ground Module — WiFi AP + UDP to Mission Planner
```

Or:

```c
// #define UART_COMMUNICATION
#define UDP_WIFI_COMMUNICATION    // Ground Module — WiFi AP + UDP to Mission Planner
```

## System Overview

```
AIR MODULE (UART_COMMUNICATION)                GROUND MODULE (UDP_WIFI_COMMUNICATION)
┌─────────────┐   USB OTG    ┌──────────────┐                    ┌──────────────┐   WiFi AP    ┌─────────────────┐
│ Flight      │ ────────────►│  ESP32-S3    │   MAVLink over     │  ESP32-S3    │ ───────────► │ Mission Planner │
│ Controller  │ ◄────────────│  Air Module  │   radio (ELRS)     │  Ground Mod. │ ◄─────────── │ (PC on network) │
└─────────────┘              └──────┬───────┘                    └──────────────┘   UDP :14550  └─────────────────┘
                                    │
                                    │ UART
                                    ▼
                             ┌──────────────┐
                             │  ELRS RX     │
                             │  module      │
                             └──────────────┘
```

### Data flow (code paths)

| Direction | Description | Task / function |
|-----------|-------------|-----------------|
| FC → GS (downlink) | MAVLink from FC USB → outbound transport | `handle_data_from_USB` → `FC_TO_GS_task` |
| GS → FC (uplink) | MAVLink from inbound transport → FC USB | `GS_to_FC_task` → `read_data_from_GS` → `cdc_acm_host_data_tx_blocking` |

| Module | Downlink transport | Uplink transport |
|--------|-------------------|------------------|
| Air Module | `write_data_to_uart` (UART → ELRS RX) | `read_data_from_uart` |
| Ground Module | `udp_send_all` (WiFi → Mission Planner) | **TODO** — `read_data_from_GS` returns 0 |

## Purpose & Context

Bridges MAVLink between an FC (USB) and a ground station. Tested with **Matek H743 Wing V3** FC, **Radiomaster Nomad TX** as GS radio, and **Dual Band Gemini RX**. Planned: Nomad flashed as RX for longer range.

Primarily a learning project that also solves a real problem at the author's job.

## Supported Hardware

| Target chips | ESP32-H4, ESP32-P4, ESP32-S2, ESP32-S3 |
|--------------|------------------------------------------|

Primary target: **ESP32-S3** with USB-OTG host. Up to **5 concurrent CDC devices** (`MAX_CDC_DEVICES`).

### USB CDC device support

- FTDI (`FTDI_VID`)
- CH34x (`NANJING_QINHENG_MICROE_VID`)
- CP210x (`SILICON_LABS_VID`)
- Generic CDC-ACM / Espressif (`ESPRESSIF_VID` = `0x303A`)

## Transport Details

### Air Module — UART (`UART_COMMUNICATION`)

- UART1, TX=GPIO17, RX=GPIO18, 460800 baud
- Buffer size: 1024 bytes
- Module: `telemetry_uart.c`

### Ground Module — WiFi + UDP (`UDP_WIFI_COMMUNICATION`)

- ESP runs as WiFi soft-AP
- SSID: `Telemetry-For-Everyone`, password: `iwanttelemetry`, channel 6
- Max STA connections: 5
- UDP port: **14550** (Mission Planner default)
- On client connect (DHCP), client IP + MAC registered in UDP client table
- Downlink: `udp_send_all` sends MAVLink to all connected clients
- Uplink: not yet implemented — `read_data_from_GS` returns 0; `udp_read_all` is stubbed
- Buffer size: 128 bytes
- Modules: `telemetry_wifi.c`, `telemetry_udp_support.c`

## Main Loop (`app_main`)

1. Create app message queue (`device_queue`)
2. Start USB host + CDC-ACM driver (`telemetry_ubs_device`)
3. Configure communications — UART (Air) or WiFi+UDP (Ground) via `setup_communications()`
4. Block on queue messages:
   - `APP_DEVICE_CONNECTED` — open CDC device, spawn `GS_to_FC_task`
   - `APP_DEVICE_DISCONNECTED` — close device slot
   - `APP_QUIT` — tear down and exit

USB hotplug events are posted to the queue from `new_dev_cb` and `handle_event`.

## Source Files

| File | Role |
|------|------|
| `main/main.c` | Entry point, role selection (defines), CDC management, MAVLink bridge tasks |
| `main/telemetry_ubs_device.c` | USB host + CDC-ACM install, device hotplug callback |
| `main/device_queue.c` / `.h` | FreeRTOS queue for app events (connect/disconnect/quit) |
| `main/telemetry_uart.c` / `.h` | UART transport — **Air Module** |
| `main/telemetry_wifi.c` / `.h` | WiFi soft-AP — **Ground Module** |
| `main/telemetry_udp_support.c` / `.h` | UDP client registry and send — **Ground Module** |

## Build System

- **Framework:** ESP-IDF (CMake, `MINIMAL_BUILD ON`)
- **Project name:** `telemetry-radio`
- **Component deps** (`main/idf_component.yml`):
  - `usb_host_cdc_acm` ^2.3
  - `usb_host_ch34x_vcp` ^2.2
  - `usb_host_cp210x_vcp` ^2.2
  - `usb_host_ftdi_vcp` ^2.1
- **IDF components required:** `esp_driver_uart`, `esp_wifi`, `nvs_flash`

### Build & flash

```bash
idf.py -p PORT flash monitor
```

Flash **Air Module** and **Ground Module** builds separately with the appropriate define enabled.

### Dev environment

- `.devcontainer/` — ESP-IDF Docker devcontainer with Espressif VS Code extensions
- `.vscode/` — launch, C/C++ properties, settings

## TX Host Module (planned — in progress)

Third build role: ESP32-S3 connected to the **internal TX/RX pads** of a detached **Radiomaster Nomad** ELRS TX module running **ELRS 4.0**. TX and RX are already configured with the **same regulatory domain** and **same binding phrase** — no special bind-mode command flow is needed.

### What it does

1. **Handset emulation (core):** Send continuous valid CRSF RC frames so the Nomad TX behaves as if a remote is connected — it stays active and **starts searching for / links to the RX** over the air. Without this stream the module idles ~60s then drops into WiFi update mode.
2. **Parameter API:** After the link is up, read all TX configuration parameters via the CRSF config protocol (same data EdgeTX Lua scripts see). Expose a **C API** for programmatic access; UI/display is deferred.
3. **Boot-time logging:** On power-on, once UART + handset emulation + parameter discovery complete, **dump all TX parameters to serial logs** (`idf.py monitor`).

### What it is NOT

- Not a manual "binding mode" trigger — link establishment is automatic when ESP feeds RC data and TX/RX share domain + phrase.
- Not MAVLink — uses **CRSF** on the Nomad pads (separate from Air Module's 460800 MAVLink UART path).
- Air Module performance tuning and bug fixes remain a separate future task.

### Target hardware

| Item | Value |
|------|-------|
| TX module | Radiomaster Nomad (internal UART pads) |
| ELRS version | **4.0** |
| ESP | ESP32-S3 |
| Protocol | CRSF (handset side) |
| Prerequisite | TX and RX: same regulatory domain + binding phrase |

### Build flag (planned)

```c
// #define UART_COMMUNICATION      // Air Module
// #define UDP_WIFI_COMMUNICATION  // Ground Module
#define ELRS_TX_HOST_MODE         // TX Host Module — CRSF handset for Nomad TX
```

### Architecture

```
┌─────────────────┐   UART (CRSF)    ┌──────────────────────┐   RF (ELRS 4.0)   ┌─────────────┐
│  ESP32-S3       │ ◄──────────────► │  Radiomaster Nomad   │ ────────────────► │  ELRS RX    │
│  TX Host Module │  emulate remote  │  TX module (pads)    │  search + link    │  (aircraft) │
└────────┬────────┘                  └──────────────────────┘                   └─────────────┘
         │
         └── serial logs: all TX params on boot
         └── C API: elrs_tx_get_params() etc. (display layer TBD)
```

### Implementation plan

#### Phase 0 — Hardware validation (before coding)

Sniff Nomad pads with handset connected (or logic analyzer) to confirm:

- Baud rate (start with **420000** / **400000**)
- Line inversion (Nomad module bay pads are often inverted CRSF)
- Separate TX/RX vs half-duplex single wire
- Power-on frame sequence from EdgeTX

**Deliverable:** `docs/elrs-nomad-sniff-notes.md` with sample frames.

#### Phase 1 — CRSF transport (`crsf_protocol.c/h`, `elrs_tx_uart.c/h`)

- Dedicated UART (do not reuse Air Module MAVLink pins/baud)
- Frame parser: sync `0xC8`, len, type, CRC8 (poly `0xD5`), extended frames for types ≥ `0x28`
- Frame builders: `0x16` RC_CHANNELS_PACKED, `0x28` DEVICE_PING, `0x2C` PARAMETER_READ
- RX ring buffer + parser task

#### Phase 2 — Handset emulation → TX searches for RX

- **CRSF TX task:** send `0x16` RC_CHANNELS_PACKED at ~50–250 Hz (neutral sticks)
- **CRSF RX task:** parse `0x14` LINK_STATISTICS — log LQ/RSSI to confirm OTA link
- Success: Nomad does not enter WiFi timeout; serial shows link stats when RX is powered

No bind command packets — matching domain + phrase + continuous RC is sufficient for ELRS 4.0.

#### Phase 3 — Parameter API + boot log dump

Mirror EdgeTX Lua discovery flow (read-only for now):

1. `0x28` DEVICE_PING → `0x29` DEVICE_INFO (name, HW/SW version, param count)
2. Read folder index `0x00`, enumerate children `0x01…N` via `0x2C` / `0x2B`
3. Parse all parameter types (UINT, TEXT_SELECTION, COMMAND, FOLDER, INFO, etc.)
4. Store in structured in-memory table

**C API (planned):**

```c
esp_err_t elrs_tx_host_init(void);
esp_err_t elrs_tx_host_start(void);          // starts RC emulation + RX parser
esp_err_t elrs_tx_params_fetch_all(void);    // blocking query of all params
const elrs_tx_params_t *elrs_tx_params_get(void);
void elrs_tx_params_log_all(void);           // called on boot after fetch — dumps to ESP_LOGI
```

**Boot sequence:**

```
power on → UART init → start RC stream → wait for DEVICE_INFO →
fetch all parameters → elrs_tx_params_log_all() → continue running (RC + link stats)
```

Display/UI (web, screen, etc.) will consume `elrs_tx_params_get()` later — not in initial scope.

#### Phase 4 — Integration

- Wire into `app_main` behind `ELRS_TX_HOST_MODE` (no USB host needed for this build)
- Add new sources to `main/CMakeLists.txt`
- Optional later: expose API over Ground Module WiFi

### New source files (planned)

| File | Role |
|------|------|
| `crsf_protocol.c/h` | CRSF frame build, parse, CRC, addresses |
| `elrs_tx_uart.c/h` | Nomad-specific UART config (baud, inversion, pins) |
| `elrs_tx_host.c/h` | Handset emulation state machine, RC TX task |
| `elrs_tx_params.c/h` | Parameter discovery, storage, API, boot log dump |

### Risks

| Risk | Mitigation |
|------|------------|
| Wrong baud/inversion on Nomad pads | Phase 0 sniff; baud fallback list |
| ELRS 4.0 param layout differs from 3.x | Discover via folder `0x00`, never hardcode indices |
| TX in MAVLink link mode instead of CRSF | Confirm Nomad config in ELRS Configurator 4.0 |
| Parameter fetch slow at boot | Log progress; run fetch once, cache in RAM |

### Sprint order

| Sprint | Output |
|--------|--------|
| 0 | Nomad UART sniff notes |
| 1 | CRSF parser + UART driver |
| 2 | RC emulation, TX links to RX, link stats in log |
| 3 | Parameter API + `elrs_tx_params_log_all()` on boot |
| 4 | Build flag, CMake, docs |

## Known Gaps / TODOs

1. **TX Host Module** — not yet implemented (see plan above).
2. **Ground Module uplink not implemented** — `read_data_from_GS` returns 0; Mission Planner commands cannot reach the FC over UDP yet.
3. **`udp_read_all`** — receive path stubbed; not wired into `GS_to_FC_task`.
4. **Air Module** — basic version works; speed and bug audit deferred.
5. **Typo in filename** — `telemetry_ubs_device.c` (likely meant `usb`); keep name unless renaming intentionally.
6. **README** references external ESP-IDF USB examples that may not exist in this standalone repo.

## Conventions & Notes

- Logging tags: `USB-CDC-MAIN`, `USB-CDC`, `WIFI-MODULE`, `UDP_SERVER`, `UART-MODULE`
- FreeRTOS tasks: `usb_lib` (priority 20), `GS to FC task` (priority 5)
- CDC TX timeout: 1000 ms (`TX_TIMEOUT_MS`)
- WiFi credentials and UDP port are hardcoded — no NVS/config UI yet
- When editing, match existing C style: minimal comments, `ESP_ERROR_CHECK` for fatal init errors, `ESP_LOG*` for diagnostics

## Related Docs

- `README.md` — user-facing setup and hardware notes
- [ESP-IDF USB examples](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb) — upstream USB host/device reference
