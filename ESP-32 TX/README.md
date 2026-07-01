# ARINC_TX_16

ESP32-S3 ESP-IDF firmware for transmitting ARINC 429 words over GPIO/RMT, controlled from a PC over UDP through a W5500 Ethernet module.

This project is intended for simulator and bench-test use. It is not certified avionics equipment and must not be used in flight-critical systems.

## Features

- 16 configurable ARINC TX slots.
- UDP control interface for label, SDI, data19, SSM, period, offset, enable, one-shot send, and global speed.
- Optional persistent scan channel for walking ARINC labels and sending status back to the PC.
- Tkinter GUI for configuring slots and sending commands.
- Local ARINC label database helper for common labels and encoding defaults.
- W5500 Ethernet with DHCP.

## Hardware

Target: ESP32-S3.

ARINC TX pins:

- Clock: GPIO13
- Data: GPIO14

W5500 SPI pins:

- MISO: GPIO1
- MOSI: GPIO4
- SCK: GPIO38
- CS: GPIO39
- INT: GPIO40
- RST: GPIO2

The ARINC electrical interface is not provided by the ESP32 directly. Use low cost CD4052 analog switch between the ESP32 GPIO/RMT outputs and the ARINC bus.

## Network

The ESP32 uses DHCP on the W5500 Ethernet interface.

UDP ports:

- `5002`: ARINC TX control commands received by the ESP32.
- `5003`: scan status packets sent from the ESP32 to the PC.

The firmware contains a PC destination IP for scan status in `main/ethernet.h`. The GUI has its own default ESP32 destination IP in `arinc_control_gui.py`. Adjust these for your local network before use.

## Build And Flash

Install ESP-IDF and source its environment first.

```sh
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

If component dependencies need to be refreshed:

```sh
idf.py reconfigure
```

## GUI

Run the control GUI from the project directory:

```sh
python3 arinc_control_gui.py
```

The GUI sends ASCII UDP commands to the ESP32 control port. It can configure each slot, start/stop the scheduler, send one-shot words, edit data bits, and apply BCD/BNR helper conversions.

## Source Layout

- `main/ARINC_TX_16.c`: application startup and task creation.
- `main/arinc.c`, `main/arinc.h`: ARINC 429 TX encoding, RMT output, scheduler, and scan logic.
- `main/ethernet.c`, `main/ethernet.h`: W5500 Ethernet setup and UDP command/status tasks.
- `arinc_control_gui.py`: Tkinter PC control GUI.
- `arinc_label_database.py`: label lookup data used by the GUI.
