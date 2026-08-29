# SafeLink Test 6B — interrupted transfer recovery

Test 6B begins from the frozen Test 6A baseline and adds one deterministic
failure. The Test 6B server advertises a 5 MiB object but closes the first GET
connection after exactly 1 MiB.

The P4 must:

1. detect the incomplete HTTP response without asserting or rebooting;
2. close and delete `/sdcard/SL6B.TMP`;
3. prove that neither the temporary nor final file remains;
4. preserve the C6 and ESP-Hosted SDIO transport;
5. perform a new 5 MiB download on the same boot;
6. verify every received byte and every byte read back from SD;
7. atomically rename, validate and remove the completed file;
8. release only SDMMC slot 0 and LDO channel 4.

## Start the Test 6B server

Stop the older server with `Ctrl+C`, then run from the laboratory root:

```powershell
python ".\SafeLink-Test6B-Interrupted-Recovery\safelink_download_server_test6b.py"
```

Verify that health reports version `1.1-test6b`:

```powershell
Invoke-RestMethod "http://192.168.0.189:8000/health"
```

## Apply and build

```powershell
python ".\SafeLink-Test6B-Interrupted-Recovery\apply_safelink_test6b.py" --project ".\p4-safelink-test1"
Set-Location ".\p4-safelink-test1"
python "$env:IDF_PATH\tools\idf.py" -B build-safelink-test6b -D SDKCONFIG=sdkconfig.safelink-eco2-test4 build
```

## Required binary markers

Before flashing, confirm that the binary contains all of these strings:

- `SafeLink Test 6B`
- `PHASE 1: requesting deterministic disconnect after 1 MiB`
- `Expected interruption detected safely`
- `Partial artifact absent`
- `PHASE 1 PASS: partial file removed; C6 transport remains active`
- `PHASE 2: starting clean 5 MiB recovery download`
- `Second-pass SD verification PASS`
- `Atomic commit PASS`
- `Final cleanup PASS`
- `TEST 6B PASS`

Record the SHA-256 of the newly built application:

```powershell
Get-FileHash ".\build-safelink-test6b\transport_config.bin" -Algorithm SHA256
```

## Flash

With the P4 on COM9:

```powershell
python "$env:IDF_PATH\tools\idf.py" -B build-safelink-test6b -D SDKCONFIG=sdkconfig.safelink-eco2-test4 -p COM9 flash
```

## Required serial result

The expected order is:

```text
PHASE 1: requesting deterministic disconnect after 1 MiB
Expected interruption detected safely
Partial artifact absent: /sdcard/SL6B.TMP
Partial artifact absent: /sdcard/SL6B.BIN
PHASE 1 PASS: partial file removed; C6 transport remains active
HEAD preflight: err=ESP_OK HTTP=200 Content-Length=5242880
PHASE 2: starting clean 5 MiB recovery download
Second-pass SD verification PASS: 5242880 bytes
Atomic commit PASS: /sdcard/SL6B.BIN
Final cleanup PASS
TEST 6B PASS
```

Any assertion, P4 reset, C6 reset after phase 1, remaining partial file, failed
recovery HEAD, incorrect length or byte mismatch is a test failure.

