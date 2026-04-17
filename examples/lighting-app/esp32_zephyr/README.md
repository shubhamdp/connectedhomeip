# Matter ESP32 Zephyr Lighting Example Application

This example demonstrates a Matter-enabled lighting device running on ESP32
chips using Zephyr RTOS with Thread networking.

## Supported Hardware

| Board | Build target | Thread | WiFi |
|-------|-------------|--------|------|
| ESP32-C6 DevKitC | `esp32c6_devkitc` | Yes | No (Phase 2) |
| ESP32-H2 DevKitM | `esp32h2_devkitm` | Yes | No |

## Prerequisites

1. Install [Zephyr SDK](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)
2. Set up a west workspace with ESP32 support:

```bash
west init -m https://github.com/zephyrproject-rtos/zephyr --mr main ~/zephyrproject
cd ~/zephyrproject
west update
west blobs fetch hal_espressif
```

3. Checkout Matter submodules:

```bash
cd path/to/connectedhomeip
python3 scripts/checkout_submodules.py --shallow --platform esp32_zephyr
```

## Building

```bash
cd examples/lighting-app/esp32_zephyr
west build -b esp32c6_devkitc
```

For ESP32-H2:

```bash
west build -b esp32h2_devkitm
```

## Flashing

```bash
west flash
```

## Commissioning

The device starts BLE advertising automatically. Use a Matter controller
(e.g., chip-tool) to commission it into a Thread network.

## Device UI

- **LED0** (GPIO8): Status LED — blinks during commissioning, solid when provisioned
- **LED1** (GPIO7): Light state — on/off controlled by Matter
- **BOOT button** (GPIO9): Long press (>3s) triggers factory reset
