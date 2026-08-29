# SafeLink Test 4 — deterministic HTTP download

This kit changes only the P4 laboratory application. It verifies the exact
SafeLink Test 3 application and SDIO-driver hashes before making changes. The
proven SDIO driver and C6 `network_adapter.bin` remain unchanged.

## Test path

```text
Windows server -> Wi-Fi -> ESP32-C6 -> SDIO -> ESP32-P4 -> validate -> discard
```

The application downloads 1, 5, 25 and 50 MiB files, then repeats 50 MiB
downloads indefinitely. Every received byte is compared with the server's
deterministic `00..FF` pattern. Only a 1,024-byte HTTP receive buffer is used.
Local NVS is deliberately not initialized: this ECO2 configuration places the
main-task stack in TCM, where a local flash/cache-disable operation asserts.
The remote Wi-Fi configuration is explicitly RAM-only for this laboratory test.
Wi-Fi disconnects are logged with their ESP-IDF reason code so authentication,
missing-AP, and transport failures can be distinguished without guesswork.

## Apply from the lab root

```powershell
python .\SafeLink-Test4\apply_safelink_test4.py --project .\p4-safelink-test1
```

The script creates one-time Test 3 backups beside changed files and creates:

```text
p4-safelink-test1\sdkconfig.safelink-eco2-test4
```

## Configure locally

Activate the ECO2 ESP-IDF checkout in PowerShell 7. From the P4 project:

```powershell
python "$env:IDF_PATH\tools\idf.py" `
    -B build-safelink-test4 `
    -D SDKCONFIG=sdkconfig.safelink-eco2-test4 `
    menuconfig
```

Open **SafeLink Test 4** and enter:

- Wi-Fi SSID
- Wi-Fi password
- Server IPv4 address (initially `192.168.0.189`)
- Server port (`8000`)

Save and exit. The configured sdkconfig contains the Wi-Fi password; do not
publish it.

## Build

```powershell
python "$env:IDF_PATH\tools\idf.py" `
    -B build-safelink-test4 `
    -D SDKCONFIG=sdkconfig.safelink-eco2-test4 `
    build
```

Before flashing, start `Start-SafeLink-Server.cmd` and confirm its displayed
IPv4 address still matches menuconfig.

## Flash only the application first

With the P4 on COM9:

```powershell
python -m esptool --chip esp32p4 --port COM9 --baud 460800 `
    --before default_reset --after no_reset write_flash `
    --flash_mode dio --flash_size 16MB --flash_freq 80m `
    0x10000 build-safelink-test4\transport_config.bin
```

Power-cycle after flashing. Do not flash the bootloader, partition table, NVS,
FFat, or C6 for this test.

## Expected result

```text
SafeLink download 1 PASS (1 MiB)
SafeLink download 2 PASS (5 MiB)
SafeLink download 3 PASS (25 MiB)
SafeLink download 4 PASS (50 MiB)
Baseline PASS; beginning repeated 50 MiB endurance test
```

Stop and retain the complete serial log if any of these appears:

- `SafeLink: RX pool exhausted; applying backpressure`
- `SafeLink: RX resumed`
- `SafeLink TX retry`
- integrity or length failure
- SDIO timeout or transport restart
- core dump or reset

## Restore the Test 3 application

Copy the `.safelink-test3-backup` files back to their original names and use
the Test 3 sdkconfig/build directory. The Test 3 checkpoint ZIP remains the
authoritative recovery artifact.
