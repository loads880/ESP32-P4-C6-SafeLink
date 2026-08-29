# SafeLink Test 6C — repeated recovery endurance

Test 6C extends the proven Test 6B recovery into 25 consecutive same-boot
cycles. The fault server rotates deterministic disconnects after 64 KiB,
1 MiB and 4 MiB of a declared 5 MiB response.

Every cycle mounts the removable FAT32 card on P4 SDMMC slot 0, requests one
truncated response, proves that both 8.3 artifacts are absent, proves the C6
transport is still usable with another HEAD request, completes a clean 5 MiB
download, performs a second byte-for-byte SD read, atomically renames and
removes the file, then releases only slot 0 and LDO channel 4.

The test records settled memory after every cycle. It fails if final DMA or
internal memory falls by more than 16 KiB from cycle 1, or if the minimum
settled largest DMA block falls by more than 4 KiB.

Expected runtime is approximately 8–12 minutes. There must be only one P4 boot
and one C6 initialization sequence in the complete serial log.

## Start the Test 6C server

Stop the Test 6B server with `Ctrl+C`. From the laboratory root run:

```powershell
python ".\SafeLink-Test6C-Recovery-Endurance\safelink_download_server_test6c.py"
```

In another window verify:

```powershell
Invoke-RestMethod "http://192.168.0.189:8000/health"
```

The version must be `1.2-test6c`.

## Apply and build

Open the SafeLink ESP-IDF launcher, then from the project directory run:

```powershell
Set-Location "C:\Users\John\Documents\Arduino\SafeLink-SDIO-Lab"
python ".\SafeLink-Test6C-Recovery-Endurance\apply_safelink_test6c.py" --project ".\p4-safelink-test1"
Set-Location ".\p4-safelink-test1"
idf -B build-safelink-test6c -D SDKCONFIG=sdkconfig.safelink-eco2-test4 build
```

Record the binary hash:

```powershell
$bin = (Resolve-Path ".\build-safelink-test6c\transport_config.bin").Path
Get-FileHash $bin -Algorithm SHA256
```

## Required binary markers

- `SafeLink Test 6C`
- `25-cycle interrupted recovery endurance`
- `Cycle %u fault phase`
- `Cycle %u expected interruption`
- `Partial artifact absent`
- `Cycle %u recovery phase`
- `Second-pass SD verification PASS`
- `CYCLE %u/%u PASS`
- `Memory endurance PASS`
- `TEST 6C PASS`

Do not flash unless every marker is present.

## Flash

```powershell
idf -B build-safelink-test6c -D SDKCONFIG=sdkconfig.safelink-eco2-test4 -p COM9 app-flash
```

Start the serial logger immediately. Do not reset either device or stop the
server while the 25 cycles run.

## Pass conditions

The final log must show:

```text
CYCLE 25/25 PASS
Memory endurance PASS: no material settled-memory drift
TEST 6C PASS: 25/25 interrupted transfers cleaned up and recovered byte-perfectly without reboot
```

Any assertion, panic, reboot, remaining partial file, failed recovery HEAD,
byte mismatch, slot-release failure, filesystem failure, or memory-drift guard
failure makes the test a failure.

