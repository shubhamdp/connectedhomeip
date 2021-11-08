# CHIP ESP32 OTA Requestor Example

A prototype application that demonstrates OTA requestor capabilities.

---
## OTA Provider Example
Before moving ahead, make sure you [OTA Provider](../../ota-provider-app/esp32) is commissioned and running.


## Supported Devices

CHIP demo application is intended to work on three categories of ESP32 devices: [ESP32-DevKitC](https://www.espressif.com/en/products/hardware/esp32-devkitc/overview), [ESP32-WROVER-KIT_V4.1](https://www.espressif.com/en/products/hardware/esp-wrover-kit/overview), [M5Stack](http://m5stack.com), and [ESP32C3-DevKitM](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/hw-reference/esp32c3/user-guide-devkitm-1.html).

Note: M5Stack Core 2 display is not supported in the tft component, while other functionality can still work fine.

## Building the Example Application

Building the example application requires the use of the Espressif ESP32 IoT Development Framework
and the xtensa-esp32-elf toolchain for ESP32 modules or the riscv-esp32-elf toolchain for ESP32C3 modules.

The VSCode devcontainer has these components pre-installed, so you can skip this step. To install these components manually, follow these steps:

-   Clone the Espressif ESP-IDF and checkout
    [v4.3 tag](https://github.com/espressif/esp-idf/releases/v4.3)

        $ mkdir ${HOME}/tools
        $ cd ${HOME}/tools
        $ git clone https://github.com/espressif/esp-idf.git
        $ cd esp-idf
        $ git checkout v4.3
        $ git submodule update --init
        $ ./install.sh

-   Install ninja-build

        $ sudo apt-get install ninja-build

Currently building in VSCode _and_ deploying from native is not supported, so make sure the IDF_PATH has been exported(See the manual setup steps above).

-   Setting up the environment

        $ cd ${HOME}/tools/esp-idf
        $ ./install.sh
        $ . ./export.sh
        $ cd {path-to-connectedhomeip}
        
    If packages are already installed then simply activate them.

        $ source ./scripts/activate.sh

    To download and install packages.

        $ source ./scripts/bootstrap.sh
        $ source ./scripts/activate.sh

-   Target Set
    To set supported IDF target, run following command with appropriate target (esp32 or esp32c3)

        # For ESP32
        $ idf.py set-target esp32

        # For ESP32-C3
        $ idf.py set-target esp32c3

-   Configuration Options
    To choose from the different configuration options, run menuconfig.

    The device types that are currently supported include `ESP32-DevKitC` (default), `ESP32-WROVER-KIT_V4.1`, `M5Stack` and `ESP32C3-DevKitM`.

        $ idf.py menuconfig -> Demo -> Device Type

    Supported rendezvous modes are WiFi, BLE or Bypass.

        $ idf.py menuconfig -> Component config -> CHIP Device Layer -> WiFi Station Options

    If Rendezvous Mode is ByPass then set the credentials of the WiFi Network (i.e. SSID and Password from menuconfig).

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

          $ idf.py -p <RequestorSerialPort> flash monitor

    Note: Some users might have to install the
    [VCP driver](https://www.silabs.com/products/development-tools/software/usb-to-uart-bridge-vcp-drivers)
    before the device shows up on `/dev/tty`.

-   Quit the monitor by hitting `Ctrl+]`.

    Note: You can see a menu of various monitor commands by hitting
    `Ctrl+t Ctrl+h` while the monitor is running.

## Commissioning

Commission the OTA Provider using [chip-tool](../../chip-tool). Below, command commissions the device using BLE.

    $ ./out/debug/chip-tool pairing ble-wifi 12346 <ssid> <passphrase> 0 20202021 3840

## Query for an OTA Image
-   After commissioning is successful, query for OTA image. Head over to ESP32
    console and fire the following command. This command start the OTA image
    tranfer.

        esp32> QueryImage <OtaProviderIpAddress> <OtaProviderNodeId>

## Apply update request
-   Once transfer is complete OTA Requestor should take permission the OTA Provider for applying the OTA image.
        
        esp32> ApplyUpdateRequest <OtaProviderIpAddress> <OtaProviderNodeId>
After this step device should reboot and start running [hello world example](https://github.com/espressif/esp-idf/tree/master/examples/get-started/hello_world).

## ESP32 OTA Requestor with [Linux OTA Provider](../../ota-provider-app/linux)
-    Run the Linux OTA Provider with [hello world OTA image](http://shubhamdp.github.io/esp_ota/esp32/hello-world-flash-in-ota-provider-partition.bin)

         $ ./out/debug/chip-ota-provider-app -f hello-world-flash-in-ota-provider-partition.bin
            
- Provision the Linux OTA Provider using chip-tool

        $ ./out/debug/chip-tool pairing 12345 20202021
        
- Note down the OTA Provider IP address and Node Id and repeat the steps for ESP32 OTA Requestor app.
