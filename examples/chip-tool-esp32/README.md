# Chip Tool ESP32

---

-   [Building the Example Application](#building-the-example-application)
-   [Flashing app using script](#flashing-app-using-script)
-   [Optimization](#optimization)

---

## Building the Example Application

Building the example application requires the use of the Espressif ESP32 IoT
Development Framework and the xtensa-esp32-elf toolchain.

The VSCode devcontainer has these components pre-installed, so you can skip this
step. To install these components manually, follow these steps:

-   Clone the Espressif ESP-IDF and checkout
    [v4.4-beta1 pre-release](https://github.com/espressif/esp-idf/releases/tag/v4.4-beta1)

          $ mkdir ${HOME}/tools
          $ cd ${HOME}/tools
          $ git clone https://github.com/espressif/esp-idf.git
          $ cd esp-idf
          $ git checkout v4.4-beta1
          $ git submodule update --init
          $ ./install.sh

-   Install ninja-build

          $ sudo apt-get install ninja-build

Currently building in VSCode _and_ deploying from native is not supported, so
make sure the IDF_PATH has been exported(See the manual setup steps above).

-   Setting up the environment

        $ cd ${HOME}/tools/esp-idf
        $ ./install.sh
        $ . ./export.sh
        $ cd {path-to-connectedhomeip}

    To download and install packages.

        $ source ./scripts/bootstrap.sh
        $ source ./scripts/activate.sh

    If packages are already installed then simply activate them.

        $ source ./scripts/activate.sh

-   Target Select

        $ idf.py set-target esp32(or esp32c3)

-   Configuration Options

    This application uses `ESP32-DevKitC` as a default device type. To use other
    ESP32 based device types, please refer
    [examples/all-clusters-app/esp32](https://github.com/project-chip/connectedhomeip/tree/master/examples/all-clusters-app/esp32)

-   To build the demo application.

          $ idf.py build

-   After building the application, to flash it outside of VSCode, connect your
    device via USB. Then run the following command to flash the demo application
    onto the device and then monitor its output. If necessary, replace
    `/dev/tty.SLAB_USBtoUART`(MacOS) with the correct USB device name for your
    system(like `/dev/ttyUSB0` on Linux). Note that sometimes you might have to
    press and hold the `boot` button on the device while it's trying to connect
    before flashing. For ESP32-DevKitC devices this is labeled in the
    [functional description diagram](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/hw-reference/esp32/get-started-devkitc.html#functional-description).

          $ idf.py -p /dev/tty.SLAB_USBtoUART flash monitor

    Note: Some users might have to install the
    [VCP driver](https://www.silabs.com/products/development-tools/software/usb-to-uart-bridge-vcp-drivers)
    before the device shows up on `/dev/tty`.

-   Quit the monitor by hitting `Ctrl+]`.

    Note: You can see a menu of various monitor commands by hitting
    `Ctrl+t Ctrl+h` while the monitor is running.

-   If desired, the monitor can be run again like so:

          $ idf.py -p /dev/tty.SLAB_USBtoUART monitor

## Flashing app using script

-   Follow these steps to use `${app_name}.flash.py`.

    -   First set IDF target, run set-target with one of the commands.

            $ idf.py set-target esp32
            $ idf.py set-target esp32c3

    -   Execute below sequence of commands

```
        $ export ESPPORT=/dev/tty.SLAB_USBtoUART
        $ idf.py build
        $ idf.py flashing_script
        $ python ${app_name}.flash.py
```

## Optimization

Optimization related to WiFi, BLuetooth, Asserts etc are the part of this
example by default. To enable this option set is_debug=false from command-line.

```
# Reconfigure the project for additional optimizations
rm -rf sdkconfig build/
idf.py -Dis_debug=false reconfigure

# Set additional configurations if required
idf.py menuconfig

# Build, flash, and monitor the device
idf.py -p /dev/tty.SLAB_USBtoUART build flash monitor
```
