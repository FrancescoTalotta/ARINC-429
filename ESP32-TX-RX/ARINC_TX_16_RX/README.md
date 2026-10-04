# ARINC_TX_16_RX

ESP32-S3 ESP-IDF firmware for 16-slot ARINC 429 transmission and independent passive ARINC reception, controlled from a Tkinter GUI over W5500 Ethernet.

This project is intended for simulator and bench-test use. It is not certified avionics equipment and must not be used in flight-critical systems.

## Features

- All TX slot, one-shot, scheduler, label scan, BCD, and BNR controls from `ARINC_TX_16`.
- Passive single-channel RX; received words never trigger an ARINC transmission.
- ACP-style RX filtering: report the first word for each label/SDI, then only Data19 or SSM changes.
- Independently selectable 12.5 or 100 kbit/s RX timing.
- RX display for label, SDI, Data19 decimal/hex/binary, SSM, parity, raw word, update count, and local last-seen time.
- Exact 19-bit Data19 display with indicators numbered from ARINC bit 29 to bit 11.
- Bounded firmware and GUI queues with separate overflow diagnostics.

## Hardware

Target: ESP32-S3 using the same external ARINC interfaces as the existing TX and ACP projects.

ARINC pins:

- RX clock: GPIO11
- RX data: GPIO12
- TX clock: GPIO13
- TX data: GPIO14

W5500 SPI pins:

- MISO: GPIO1
- MOSI: GPIO4
- SCK: GPIO38
- CS: GPIO39
- INT: GPIO40
- RST: GPIO2

The ESP32 GPIOs are logic-level signals. Do not connect an ARINC differential bus directly to them. Use the working external ARINC receiver and line-driver hardware; its outputs connected to GPIO11/12 must be ESP32-compatible logic levels.

## Network

The W5500 obtains an address with DHCP.

- UDP `5002`: TX and RX control commands received by the ESP32.
- UDP `5003`: existing TX scan status destination.
- UDP `5004`: default PC listener for received ARINC telemetry.

The RX destination is not hard-coded. Clicking **Subscribe** in the RX tab sends two commands to UDP 5002:

```text
rx_speed=low
rx_port=5004
```

The ESP32 sends RX telemetry to the command sender's IPv4 address and selected port. `rx_port=0` disables telemetry but does not stop capture. Only one RX telemetry subscriber is retained at a time.

The telemetry `count` is the sequence number of emitted GUI updates, not the number of words seen on the ARINC bus. Repeated unchanged words are intentionally suppressed. The `dropped` value reports firmware capture-queue overflows.

## Build And Flash

Install ESP-IDF 6.0 or newer and source its environment. The dependency lock
was generated with ESP-IDF 6.0.0. Then use the included wrapper:

```sh
bash build.sh build
bash build.sh flash monitor
```

To specify a serial port:

```sh
bash build.sh -p /dev/cu.usbmodemXXXX flash monitor
```

If `idf.py` is not already available, the wrapper can load ESP-IDF from an
explicit checkout:

```sh
IDF_PATH=/path/to/esp-idf bash build.sh build
```

The preserved baseline `sdkconfig` uses a 2 MB flash image setting and the single-app partition table. Change the flash and partition configuration only after confirming the fitted module and deployment requirements.

## Prebuilt Firmware

The [`firmware`](firmware) directory contains a complete merged image for an
ESP32-S3 with 2 MB flash. To program a board without installing ESP-IDF, follow
the [from-scratch flashing procedure](firmware/README.md).

Do not flash this image on another ESP32 variant. The image uses DIO flash mode
at 80 MHz and does not enable Secure Boot or flash encryption.

## GUI

Run:

```sh
python3 arinc_control_gui.py
```

The TX tab preserves the existing transmitter interface. In the RX tab, set the ESP32 IP in the shared network controls, choose the RX speed and local UDP port, then click **Subscribe**. Pause freezes table rendering while capture and counters continue; Clear removes displayed rows without resetting transport diagnostics.

Data19 is decoded to the same numeric convention accepted by the TX controls. The raw 32-bit captured word remains visible for hardware and bit-order verification.

RX filtering follows ACP: `(label, SDI)` identifies a message stream. Its first received word is displayed, and subsequent words are sent to the GUI only when Data19 or SSM changes. Parity-only changes do not trigger an update. Subscribing again resets this filter so each stream can publish its current value again.

## Limitations

- UDP does not guarantee delivery or replay.
- RX telemetry collected before a subscriber exists is drained and discarded.
- High-speed RX uses one GPIO interrupt per bit and must be verified on the target hardware under simultaneous TX and Ethernet load.
- Framing starts only after a speed-dependent ARINC interword gap. Changing RX speed abandons a partial word.
- Engineering-unit interpretation of Data19 remains label/LRU-specific; the RX tab shows the decoded 19-bit field without assuming BNR or BCD semantics.

## License

Copyright (C) 2026 Francesco Talotta.

This project is licensed under the GNU General Public License version 3. See
[`LICENSE`](LICENSE) for the complete terms.
