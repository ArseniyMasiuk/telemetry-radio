| Supported Targets | ESP32-H4 | ESP32-P4 | ESP32-S2 | ESP32-S3 |
| ----------------- | -------- | -------- | -------- | -------- |

# USB CDC-ACM Host Driver as telemetry radio

This code utilizes USB-OTG functionality of boards listed above.
Its a tool for a telemetry that allows to plug FC to the board, read telemetry from it, and send it to the RX (or it can be TX configured as RX) flashed with ELRS and set up as telemetry radio.
This solution is mostly learing project, but also it solves an problem at mu job.

Tested on Matek H743 Wing V3 FC, Radiomaster Nomad TX as GS station radio and Dual Band Gemini RX.
Planning to make same RX module but Nomad flashed as RX for longer range.

## How to use

### Hardware Required

Two development boards with USB-OTG support. One will act as USB host and the other as USB device.

#### Pin Assignment

Follow instruction in [examples/usb/README.md](../../../README.md) for specific hardware setup.

### Build and Flash

1. Build and flash [tusb_serial_device example](../../../device/tusb_serial_device) to USB device board.
2. Build this project and flash it to the USB host board, then run monitor tool to view serial output:

```bash
idf.py -p PORT flash monitor
```

(Replace PORT with the name of the serial port to use.)

(To exit the serial monitor, type ``Ctrl-]``.)

See the Getting Started Guide for full steps to configure and use ESP-IDF to build projects.

