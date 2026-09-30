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
idf.py -p /dev/cu.usbmodem* flash               # first flash only
```

Partition table: two 1984 KB OTA slots, no factory app (`partitions.csv`).

Once the firmware runs, the USB PHY belongs to the host controller and the S3's
USB-Serial-JTAG port disappears. A later USB flash needs ROM download mode: hold BOOT
while plugging in the USB-C cable. The log console is UART0 only, 115200, GPIO43 (TX) and
GPIO44 (RX), read with a 3.3 V USB-UART adapter:

```
idf.py -p /dev/cu.usbserial-* monitor
```

## Network interface

Addresses below use the template's `kIpAddress`, 192.168.1.50. Nothing is authenticated:
anyone on the LAN can use the console, reset the printer and replace the firmware.

### OTA update

```
idf.py build
curl --data-binary @build/cr10-link.bin -H 'Expect:' http://192.168.1.50/ota
```

The image is written to the passive slot and the S3 reboots into it. The new image
marks itself valid once WiFi has its address (the console is already listening by
then). Until then it is pending: if the S3 resets before that, the bootloader boots the
previous image. The S3 is powered from the printer, so a printer power cycle is that
reset. An upload is refused (409) while the running image is still pending.

### Printer console

Raw TCP on port 2323, bytes passed through unchanged in both directions:

```
nc 192.168.1.50 2323
```

The terminal's line discipline supplies local echo and turns Marlin's bare LF into CRLF.
For picocom, bridge the socket to a pty:

```
socat pty,link=/tmp/cr10,raw,echo=0 tcp:192.168.1.50:2323 &
picocom --imap lfcrlf --echo --emap crcrlf /tmp/cr10
```

One client at a time: a second connection receives `cr10-link: console in use` and is
closed. A client that vanishes without closing is dropped by TCP keepalive after about
25 s. Printer output while no client is connected is discarded.

Connecting to the printer never asserts DTR, so it does not reset the printer.

### Printer reset

```
curl -X POST http://192.168.1.50/reset
```

Pulses DTR (100 ms), which resets the ATmega through the Melzi's 100 nF coupling
capacitor. Returns 503 while the printer's USB serial port is not connected.

## Host tests

Pure C++ logic lives in `components/` and is tested on ESP-IDF's linux target from
`test/host`, with Unity:

```
cd test/host
idf.py --preview set-target linux    # once
idf.py build && ./build/cr10_link_host_test.elf
```

The executable exits non-zero when a test fails.
