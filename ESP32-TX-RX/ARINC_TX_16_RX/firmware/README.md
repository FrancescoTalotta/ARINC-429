# Prebuilt ESP32-S3 Firmware

Firmware version `1.0.0` was built from this repository for an ESP32-S3 with:

- 2 MB flash
- DIO flash mode at 80 MHz
- Single-application partition table
- Secure Boot disabled
- Flash encryption disabled

`ARINC_TX_16_RX_merged.bin` is the recommended image. It contains the
bootloader, partition table, and application and is flashed at address `0x0`.

## Flash From Scratch

1. Install Python 3 and esptool:

   ```sh
   python3 -m pip install --upgrade esptool
   ```

   On Windows, use `python` instead of `python3` if necessary.

2. Connect the ESP32-S3 by USB and identify its serial port. Typical names are
   `/dev/cu.usbmodemXXXX` on macOS, `/dev/ttyACM0` on Linux, and `COM5` on
   Windows.

3. Put the board in download mode if it does not enter it automatically: hold
   **BOOT**, press and release **RESET**, then release **BOOT**.

4. Erase the existing flash, replacing `<PORT>` with the serial port:

   ```sh
   python3 -m esptool --chip esp32s3 --port <PORT> erase-flash
   ```

5. From the repository root, flash the merged image at address `0x0`:

   ```sh
   python3 -m esptool --chip esp32s3 --port <PORT> --baud 460800 \
       write-flash --flash-mode dio --flash-freq 80m --flash-size 2MB \
       0x0 firmware/ARINC_TX_16_RX_merged.bin
   ```

6. Press **RESET** if the board does not restart automatically.

If the connection is unreliable, repeat the command with `--baud 115200`.

## Separate Images

The merged image is equivalent to flashing the three files at their ESP-IDF
offsets:

```sh
python3 -m esptool --chip esp32s3 --port <PORT> --baud 460800 \
    write-flash --flash-mode dio --flash-freq 80m --flash-size 2MB \
    0x0 firmware/bootloader.bin \
    0x8000 firmware/partition-table.bin \
    0x10000 firmware/ARINC_TX_16_RX.bin
```

## Verify Downloads

From the repository root on macOS:

```sh
shasum -a 256 -c firmware/SHA256SUMS
```

On Linux:

```sh
sha256sum -c firmware/SHA256SUMS
```

The prebuilt firmware has not been flashed to hardware as part of the release
packaging process. Verify ARINC interface voltage levels and the documented GPIO
wiring before connecting it to an ARINC bus.
