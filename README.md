# cr10-link

Firmware for an ESP32-S3 SuperMini that bridges a Creality CR-10's USB serial port
(FT232R on the Melzi board) to WiFi. The S3 runs as a USB host to the printer and
serves a TCP console and an OctoPrint-compatible HTTP API, so Cura can upload to the
printer's SD card and start and control prints. Cura is pointed at the S3's fixed
address.

Board: ESP32-S3 SuperMini (Nologo), ESP32-S3FH4R2: 4 MB flash and 2 MB quad PSRAM in
package, native USB on GPIO19/20.

## Wiring

| Melzi ISP header | S3 SuperMini |
|---|---|
| pin 2 VCC | 5V |
| pin 6 GND | GND |

A USB-C to mini-B cable runs from the S3's USB-C port to the Melzi's mini-B port.

The S3 draws its power from the Melzi's 5 V rail, so jumper J5 on the Melzi must select
the regulator, not USB: with J5 on USB the rail's only source is USB VBUS, which on this
link is fed from that same rail or not at all.

The first flash is over USB, before the S3 is connected to the printer. After that the
USB port is in host mode and updates are OTA.

## Build and flash

ESP-IDF v6.

```
cp credentials.h.template main/credentials.h   # then fill it in
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor       # first flash only
```

Partition table: two 1984 KB OTA slots, no factory app (`partitions.csv`).

## Host tests

Pure C++ logic lives in `components/` and is tested on ESP-IDF's linux target from
`test/host`, with Unity:

```
cd test/host
idf.py --preview set-target linux    # once
idf.py build && ./build/cr10_link_host_test.elf
```

The executable exits non-zero when a test fails.
