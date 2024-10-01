| Supported Targets | ESP32-C6 | ESP32-H2 |
| ----------------- | -------- | -------- |

# Smarthome Heater

This project is the implementation of a heater, that controls a simple relay to turn on and off.

It supports schedules, local and remote temp and also temperature target overrides.

The Converter for zigbee2mqtt is also present, will have to find out, if they accept such a diy device with a custom manufacturer name.


## Hardware Required

* One development board with ESP32-C6 SoC acting as Zigbee end-device heater
* A USB cable for power supply and programming

## Configure the project

Before project configuration and build, make sure to set the correct chip target using `idf.py --preview set-target TARGET` command.

## Erase the NVRAM

Before flash it to the board, it is recommended to erase NVRAM if user doesn't want to keep the previous examples or other projects stored info using `idf.py -p PORT erase-flash`

## Build and Flash

Build the project, flash it to the board, and start the monitor tool to view the serial output by running `idf.py -p PORT flash monitor`.

(To exit the serial monitor, type ``Ctrl-]``.)
