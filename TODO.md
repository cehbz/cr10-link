# cr10-link: remaining phases

Printer background: `~/.claude/knowledge/projects/cr10.md`.

## 1. Printer link, TCP console, OTA: hardware bring-up

Written and building; nothing verified on hardware. Commands are in the README.

- Fill in `main/credentials.h` (gitignored; placeholders from the template): WiFi SSID
  and password, a fixed IP outside the DHCP pool.
- Build and first flash over USB with the S3 on its own, not on the printer.
- Wire ISP header pin 2 → S3 5V, pin 6 → S3 GND; C-to-mini-B cable into the Melzi.
- Melzi J5 on the regulator position; the S3 runs from the Melzi 5 V rail.
- FT232R enumerates: UART0 log shows `ftdi_link: printer connected`.
- Console round trip: `M115` answered.
- `POST /reset` resets the printer: Marlin's `start` banner on the console, boot screen
  on the LCD.
- Connecting does not reset the printer: USB cable unplug and replug with a console
  client attached reconnects with no `start` banner.
- S3 reboot (OTA, EN button) with the printer running: no boot screen on the LCD.
- Printer power cycle: S3 and link come back.
- OTA: an update boots and logs `image marked valid`; an image that never gets WiFi rolls
  back after a power cycle.
- Capture printer output for phase 2's tests: `M115`, `M105`, `M155 S2`, `M27`, `M20`,
  and a line with a bad checksum (Error/Resend).

## 2. Marlin line protocol

`components/marlin` exists with host tests on lines built from Marlin's source formats.

- Route the console through it. USB receive runs in the driver task and must not block,
  so bytes are handed to the task that owns the protocol; call `Reset()` on reconnect,
  printer reset and write failure.
- Replace the test inputs with the output captured during bring-up.

## 3. OctoPrint status API

- `GET /api/settings`, `GET /api/printer`, `GET /api/job`.
- Temperatures from `M155 S2` auto-report; SD progress from polled `M27`.
- 409 while the printer is disconnected.
- Unknown routes return 404, not 405.

## 4. Streaming upload to SD

- Multipart parser: quoted boundary; `select` and `print` fields that arrive after the file.
- 8.3 filename derived from the job name; `M28` overwrites an existing file.
- Stream with line numbers and checksums, handling resends.
- `M29`, then 201 with a `Location` containing `api/`.
- Upload runs in its own task, not the httpd task.
- During an upload, status is served from cached state and commands are held.
- Measure throughput.

## 5. Print controls

- `POST /api/job`: pause, resume, cancel.
- `POST /api/printer/command`.
- `POST /api/files/local/<name>`: select, print.
- Determine how to cancel an SD print on Marlin 2.1.2.8, from source. Lead: the emergency
  parser handles `M524` (`feature/e_parser.h`, `EP_M524`).

## 6. Marlin reflash over the link

- DTR pulse into optiboot, then STK500 to write the firmware.
