# cr10-link: remaining phases

Printer background: `~/.claude/knowledge/projects/cr10.md`.

## 1. Printer link, TCP console, OTA

- USB host on the S3 with `usb_host_ftdi_vcp` for the Melzi's FT232R.
- Open at 115200 8N1 without asserting DTR: DTR resets the ATmega1284P.
- Reset command that pulses DTR.
- Reconnect on detach and on printer power cycle.
- WiFi STA with the fixed address from `credentials.h`; TCP console bridged to the printer.
- OTA update over WiFi. Rollback is enabled in the bootloader: mark the image valid
  once WiFi and the console are up.
- Hardware checks: Melzi J5 on the regulator position; FT232R enumerates; connecting
  does not reset the printer; the reset command does; an OTA update boots.

## 2. Marlin line protocol

- Single owner of the serial link; the console and later the HTTP API go through it.
- Classify responses: `ok`, temperature reports, `echo:`, `Error:`, `Resend:`.
- Line numbers and checksums (`N… *cs`), with resend handling.
- Host tests against recorded printer output.

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
- Determine how to cancel an SD print on Marlin 2.1.2.8, from source.

## 6. Marlin reflash over the link

- DTR pulse into optiboot, then STK500 to write the firmware.
