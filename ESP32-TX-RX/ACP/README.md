# ESP32 ARINC 429 TX/RX with example application ACP GUI

ESP32-S3 ESP-IDF firmware example for transmitting and receiving ARINC 429 words and
forwarding ACP data over UDP through a W5500 Ethernet module. The included
Tkinter GUI displays ACP state and sends ACP controls back to the firmware.

This project is intended for simulator and bench-test use. It is not certified
avionics equipment and must not be used in flight-critical systems.

## Features

- ARINC 429 transmit timing using ESP32-S3 RMT.
- ARINC 429 receive sampling from an external clock and data front-end.
- Odd-parity generation and checking.
- Change filtering by ARINC label and SDI.
- W5500 Ethernet with DHCP.
- UDP communication with the ACP GUI.
- Two-partition OTA support for development.

## Requirements

- ESP32-S3 with 16 MB flash.
- W5500 Ethernet module.
- Suitable ARINC 429 line drivers and receivers.
- ESP-IDF 6.0.
- Python 3.10 or newer with Tkinter for the GUI.

The ESP32 GPIO pins are not electrically compatible with an ARINC 429 bus.
Never connect them directly. Use suitable external ARINC line-driver and
line-receiver circuitry with the required protection and voltage levels.

## Pin Assignment

ARINC transmit:

- Clock: GPIO13
- Data: GPIO14

ARINC receive front-end:

- Clock: GPIO11
- Data: GPIO12

W5500 SPI:

- MISO: GPIO1
- MOSI: GPIO4
- SCK: GPIO38
- CS: GPIO39
- INT: GPIO40
- RST: GPIO2

The default ARINC half-period is 40 microseconds, producing a 12.5 kbit/s bit
rate. Pin and timing definitions are in `firmware/main/arinc.h`; W5500 pin
definitions are in `firmware/main/ethernet.c`.

## Network Configuration

The W5500 obtains its address through DHCP. The default UDP settings are:

- Port `5000`: commands received by the ESP32.
- Port `5001`: ARINC words sent from the ESP32 to the PC.
- Port `9001`: OTA control packets.
- Port `9002`: OTA data packets.

Set the PC destination address in `firmware/main/ethernet.h` using `UDP_PC_IP`.
Set the ESP32 destination address in `gui.py` using `UDP_HOST`. The current
defaults are examples for a private network and must match your own DHCP setup.

## Build and Flash

Install ESP-IDF 6.0 and source its environment, then run:

```sh
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

The component manager downloads the pinned W5500 dependency during
configuration. Build artifacts and downloaded managed components are ignored.

## ACP GUI

From the `ESP32-TX-RX/ACP` directory, run:

```sh
python3 gui.py
```

The GUI listens for network-order 32-bit ARINC words on UDP port `5001` and
sends ACP commands to UDP port `5000`. Edit `UDP_HOST` near the top of `gui.py`
before starting it.

## Source Layout

- `firmware/main/arinc.c`, `arinc.h`: ARINC TX/RX and filtering.
- `firmware/main/ethernet.c`, `ethernet.h`: W5500, UDP, and development OTA.
- `firmware/main/ARINC_TX_RX.c`: application startup and task creation.
- `gui.py`: Tkinter ACP monitor and control GUI.

## License

This project is licensed under the GNU General Public License v3.0. See
`LICENSE` for the complete terms.
