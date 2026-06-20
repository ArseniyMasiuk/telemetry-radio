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

Exactly **one** define must be active per build. A `#error` guard in `main.c` prevents enabling more than one at a time.

| Define | Role |
|--------|------|
| `UART_COMMUNICATION` | Air Module |
| `UDP_WIFI_COMMUNICATION` | Ground Module |
| `ELRS_TX_HOST_MODE` | TX Host Module |

Example (Air Module build):

```c
#define UART_COMMUNICATION
// #define UDP_WIFI_COMMUNICATION
// #define ELRS_TX_HOST_MODE
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
| FC → GS (downlink) | MAVLink from FC USB → outbound transport | `handle_data_from_USB` → `air_to_ground_task` |
| GS → FC (uplink) | MAVLink from inbound transport → FC USB | `ground_to_air_task` → `read_data_from_ground` → `cdc_acm_host_data_tx_blocking` |

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

1. Configure communications — UART (Air) or WiFi+UDP+ELRS-TX (Ground) via `setup_communications()`
2. Call `start_main_USB_task(handle_data_from_USB)` (implemented in `common/usb_host/usb_cdc_manager.c`):
   a. Create USB host message queue (`usb_host_queue`)
   b. Start USB host + CDC-ACM driver (`configure_USB`)
   c. Block on queue messages:
      - `USB_HOST_DEVICE_CONNECTED` — open CDC device, spawn `ground_to_air_task`
      - `USB_HOST_DEVICE_DISCONNECTED` — close device slot
      - `USB_HOST_QUIT` — tear down and exit

USB hotplug events are posted to the queue from `new_dev_cb` and `handle_event`. `start_main_USB_task` accepts a `cdc_acm_data_callback_t` so the data handler can be injected from `main.c` (data routing stays in `main.c`; USB lifecycle management is in `usb_cdc_manager`).

## Source Files

### Air Module / Ground Module

| File | Role |
|------|------|
| `main/main.c` | Entry point, role selection (`#define`s + `#error` guard), data routing tasks (`ground_to_air_task`, `air_to_ground_task`, `handle_data_from_USB`) |
| `common/usb_host/usb_cdc_manager.c` / `.h` | CDC device lifecycle: slot management, VCP open dispatch, event handling, `start_main_USB_task()` |
| `common/usb_host/telemetry_ubs_device.c` / `.h` | USB host + CDC-ACM install, device hotplug callback |
| `common/usb_host/usb_host_queue.c` / `.h` | FreeRTOS queue for USB host events (`usb_host_message_t`, connect/disconnect/quit) |
| `main/telemetry_uart.c` / `.h` | UART1 transport (GPIO17/18, 460800 baud) — **Air Module** |
| `main/telemetry_wifi.c` / `.h` | WiFi soft-AP — **Ground Module** |
| `main/telemetry_udp_support.c` / `.h` | UDP client registry and send — **Ground Module** |

### TX Host Module

| File | Role |
|------|------|
| `ground_unit/tx_module/crsf_protocol.c` / `.h` | CRSF frame build/parse, CRC8 (poly `0xD5`), all frame type/address constants |
| `ground_unit/tx_module/elrs_tx_uart.c` / `.h` | UART1 @ GPIO17/18, 400000 baud, inverted (`ELRS_TX_UART_INVERTED 1`), half-duplex S port |
| `ground_unit/tx_module/elrs_tx_host.c` / `.h` | RC emulation task (`crsf_tx_task`), RX parser task (`crsf_rx_task`), link stats logging, `elrs_tx_host_setup()` |
| `ground_unit/tx_module/elrs_tx_params.c` / `.h` | Parameter discovery + C API + boot log dump |

## Build System

- **Framework:** ESP-IDF (CMake, `MINIMAL_BUILD ON`)
- **Project name:** `telemetry-radio`
- **Component layout:**
  - `main/` — Air/Ground Module sources + entry point (all roles)
  - `common/usb_host/` — Shared USB host + queue IDF component; registered via `EXTRA_COMPONENT_DIRS` in root `CMakeLists.txt`
  - `ground_unit/tx_module/` — TX Host Module as a standalone IDF component; registered via `EXTRA_COMPONENT_DIRS` in root `CMakeLists.txt`
- **Component deps** (`main/idf_component.yml` — Air/Ground Module only):
  - `usb_host_cdc_acm` ^2.3
  - `usb_host_ch34x_vcp` ^2.2 (locked: 2.2.1)
  - `usb_host_cp210x_vcp` ^2.2
  - `usb_host_ftdi_vcp` ^2.1
- **IDF components required (`main`):** `esp_driver_uart`, `esp_wifi`, `nvs_flash`, `tx_module`, `usb_host`
- **IDF components required (`tx_module`):** `esp_driver_uart`
- **IDF components required (`usb_host`):** `espressif__usb_host_cdc_acm`, `espressif__usb_host_ch34x_vcp`, `espressif__usb_host_cp210x_vcp`, `espressif__usb_host_ftdi_vcp`

### Build & flash

```bash
idf.py -p PORT flash monitor
```

Flash **Air Module** and **Ground Module** builds separately with the appropriate define enabled.

### Dev environment

- `.devcontainer/` — ESP-IDF Docker devcontainer with Espressif VS Code extensions
- `.vscode/` — launch, C/C++ properties, settings

## TX Host Module (implemented — enable `ELRS_TX_HOST_MODE` in `main.c`)

Third build role: ESP32-S3 connected to the **S port** (CRSF serial pad) of a detached **Radiomaster Nomad** ELRS TX module running **ELRS 4.0**. TX and RX are already configured with the **same regulatory domain** and **same binding phrase** — no special bind-mode command flow is needed.

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
| TX module | Radiomaster Nomad (S port — JR bay serial signal pad) |
| ELRS version | **4.0** |
| ESP | ESP32-S3 |
| Protocol | CRSF handset side — half-duplex, inverted, 400000 baud |
| Prerequisite | TX and RX: same regulatory domain + binding phrase |

### Hardware wiring

#### Why a single pad (S port)?

The Radiomaster Nomad's ELRS firmware defines `"serial_rx": 4, "serial_tx": 4` — both the receive and transmit functions share **Nomad GPIO4**, which is the **S port** (the serial signal pin on the JR module bay edge connector). This is standard CRSF half-duplex: the handset and the TX module take turns on one wire. Both ESP32 TX and RX must therefore physically meet at that same pad.

#### Do NOT use the other internal pads

The Nomad PCB also exposes backpack bridge pads internally (Nomad GPIO18 = backpack RX, GPIO5 = backpack TX, 460800 baud) and passthrough/debug pads. These have no relation to the CRSF handset protocol — connecting to them will produce no response regardless of baud rate or inversion setting.

#### Connection table

| Nomad pad | ESP32-S3 pin | Notes |
|-----------|-------------|-------|
| S port (GPIO4) | GPIO17 (UART1 TX) | ESP32 drives the line outward |
| S port (GPIO4) | GPIO18 (UART1 RX) | ESP32 reads the Nomad's responses — same physical node as above |
| GND | GND | Required — shared ground |
| XT30 / power in | external supply | 6–16.8 V DC; do NOT power from ESP32 3.3 V rail |

#### Signal characteristics

- **Logic level:** 3.3 V — matches ESP32-S3 GPIO natively; no level shifter needed
- **Inverted:** yes — ELRS half-duplex mode always starts with inverted UART; configured via `ELRS_TX_UART_INVERTED 1`
- **Baud rate:** 400000 — the Nomad boots at 5250000 then autobauds to 400000 after ~1 s of no valid frames, then locks on
- **Loopback:** because both GPIO17 and GPIO18 share one wire, the ESP32 will receive echoes of its own TX frames; this is harmless — the frame types sent (RC channels `0x16`, heartbeat `0x0B`) are silently ignored by the RX parser

### Build flag

Enable in `main/main.c` — only one role active at a time:

```c
// #define UART_COMMUNICATION      // Air Module
// #define UDP_WIFI_COMMUNICATION  // Ground Module
#define ELRS_TX_HOST_MODE         // TX Host Module — CRSF handset for Nomad TX
```

A `#error` guard prevents enabling more than one role simultaneously.

### Architecture

```
┌─────────────────┐   UART1 (CRSF)   ┌──────────────────────┐   RF (ELRS 4.0)   ┌─────────────┐
│  ESP32-S3       │ ◄──────────────► │  Radiomaster Nomad   │ ────────────────► │  ELRS RX    │
│  TX Host Module │  emulate remote  │  TX module S port    │  search + link    │  (aircraft) │
└────────┬────────┘  GPIO17+18→S     └──────────────────────┘                   └─────────────┘
         │           400000 baud, inverted, half-duplex
         └── serial logs: link stats every 1s (RSSI, LQ, SNR)
         └── serial logs: all TX params on boot
         └── C API: elrs_tx_params_get() — display layer TBD
```

### Source files

All under `ground_unit/tx_module/` (IDF component `tx_module`):

| File | Status | Role |
|------|--------|------|
| `crsf_protocol.c/h` | **Done** | CRSF frame build/parse, CRC8, all frame types and addresses |
| `elrs_tx_uart.c/h` | **Done** | UART1 @ GPIO17/18, 400000 baud, inverted (ELRS_TX_UART_INVERTED 1), half-duplex S port |
| `elrs_tx_host.c/h` | **Done** | RC emulation task, RX parser task, link stats; arm/collect API; `elrs_tx_host_setup()` |
| `elrs_tx_params.c/h` | **Done** | Parameter fetch (multi-chunk reassembly) + C API + boot log dump |

### C API

```c
// Initialise, start handset emulation, and fetch+log params (call once at boot)
void elrs_tx_host_setup(void);

// Lower-level steps called internally by elrs_tx_host_setup()
esp_err_t elrs_tx_host_init(void);
esp_err_t elrs_tx_host_start(void);

// Blocking parameter fetch — call after start()
esp_err_t elrs_tx_params_fetch_all(void);

// Access cached parameters at any time after fetch
const elrs_tx_params_t *elrs_tx_params_get(void);

// Dump everything to ESP_LOGI — called once at boot
void elrs_tx_params_log_all(void);
```

### Boot sequence (implemented)

```
power on
  → elrs_tx_host_setup()    calls init → start → fetch params → log params (single entry point from main.c)
  → elrs_tx_host_init()     UART1 init (GPIO17/18, 400000 baud, inverted), parser init, mutex/semaphore create
  → elrs_tx_host_start()    spawn crsf_rx_task (pri 10) + crsf_tx_task (pri 9)
       crsf_tx_task          sends 0x16 RC_CHANNELS_PACKED every 10 ms (neutral sticks)
                             sends 0x0B HEARTBEAT (origin 0xEE) every 200 ms — keeps Nomad out of WiFi mode
       crsf_rx_task          reads UART, feeds parser, dispatches frame callbacks
  → 1500 ms settle (ELRS_TX_BOOT_SETTLE_MS — allows Nomad CRSF stack to initialise)
  → elrs_tx_params_fetch_all()
       send 0x28 DEVICE_PING
       wait for 0x29 DEVICE_INFO  (device name, HW/SW ver, param count)
       for each param index 0..N:
           send 0x2C PARAMETER_READ (chunk 0)
           wait for 0x2B PARAMETER_SETTINGS_ENTRY
           if chunks_remaining > 0: request and accumulate remaining chunks (multi-chunk reassembly implemented)
           parse into elrs_tx_param_entry_t (name, type, value as string)
  → elrs_tx_params_log_all()  single structured dump to serial
  → loop forever
       crsf_rx_task logs 0x14 LINK_STATISTICS every 1 s (RSSI1/2, LQ, SNR)
```

Display/UI (web, screen, etc.) will consume `elrs_tx_params_get()` later.

### Bring-up checklist

- [x] Solder ESP32 GPIO17 and GPIO18 both to Nomad S port; connect GND
- [x] Set `ELRS_TX_UART_INVERTED 1` in `ground_unit/tx_module/elrs_tx_uart.h`
- [ ] Flash with `ELRS_TX_HOST_MODE` enabled
- [ ] Confirm Nomad is in **CRSF** link mode, not MAVLink (ELRS Configurator 4.0)
- [ ] Check monitor for `ELRS-TX-HOST: Handset emulation started` — Nomad should stay in RF mode (no WiFi fallback)
- [ ] Check monitor for `DEVICE_INFO` response (appears ~1.5 s after boot)
- [ ] Confirm link stats appear once RX is powered (`RSSI1`, `LQ` in logs)

### Remaining / future work

| Item | Priority |
|------|----------|
| Parameter write (`0x2D`) to change TX config | Future |
| Expose param API over Ground Module WiFi/UDP | Future |
| Display layer (screen, web UI) | Future |

## Known Gaps / TODOs

1. **Ground Module uplink not implemented** — `read_data_from_GS` returns 0; Mission Planner commands cannot reach the FC over UDP yet.
2. **`udp_read_all`** — receive path stubbed; not wired into `ground_to_air_task`.
3. **Air Module** — basic version works; speed and bug audit deferred.
4. **Typo in filename** — `telemetry_ubs_device.c` (likely meant `usb`); keep name unless renaming intentionally. File now lives in `common/usb_host/`.
5. **README** references external ESP-IDF USB examples that may not exist in this standalone repo.

## Conventions & Notes

### Logging tags

| Tag | Module |
|-----|--------|
| `USB-CDC-MAIN` | `main.c` and `common/usb_host/usb_cdc_manager.c` |
| `USB-CDC` | `common/usb_host/telemetry_ubs_device.c` |
| `WIFI-MODULE` | `telemetry_wifi.c` |
| `UDP_SERVER` | `telemetry_udp_support.c` |
| `UART-MODULE` | `telemetry_uart.c` |
| `ELRS-TX-MAIN` | `ground_unit/tx_module/elrs_tx_host.c` (`elrs_tx_host_setup`) |
| `ELRS-TX-HOST` | `ground_unit/tx_module/elrs_tx_host.c` |
| `ELRS-TX-UART` | `ground_unit/tx_module/elrs_tx_uart.c` |
| `ELRS-TX-PARAMS` | `ground_unit/tx_module/elrs_tx_params.c` |

### FreeRTOS tasks

| Task name | Priority | Source |
|-----------|----------|--------|
| `usb_lib` | 20 | `common/usb_host/telemetry_ubs_device.c` |
| `ground to air task` | 5 | `common/usb_host/usb_cdc_manager.c` (spawned); task function in `main.c` |
| `elrs_crsf_rx` | 10 | `ground_unit/tx_module/elrs_tx_host.c` |
| `elrs_crsf_tx` | 9 | `ground_unit/tx_module/elrs_tx_host.c` |

### General
- CDC TX timeout: 1000 ms (`TX_TIMEOUT_MS`)
- WiFi credentials and UDP port are hardcoded — no NVS/config UI yet
- When editing, match existing C style: minimal comments, `ESP_ERROR_CHECK` for truly fatal init errors only (not for operations that can legitimately fail at runtime), `ESP_LOG*` for diagnostics

## Related Docs

- `README.md` — user-facing setup and hardware notes
- [ESP-IDF USB examples](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb) — upstream USB host/device reference
