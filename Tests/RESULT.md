# Test 6B result — interrupted transfer recovery

**Status:** PASS  
**Test date:** 2026-08-24

## Purpose

Test 6B exercises recovery from a deterministic mid-transfer network failure while ESP32-P4 and ESP32-C6 remain connected through ESP-Hosted over SDIO.

The fault-injection server advertises a 5 MiB object and deliberately closes the first GET connection after exactly 1 MiB. The P4 test is required to detect the incomplete response without asserting or rebooting, remove the partial SD-card artifact, prove the transport is still usable, perform a fresh 5 MiB download on the same boot, verify the data again from SD, atomically commit the completed file, and clean up.

## Preserved runtime evidence

The original Test 6B serial capture is included unchanged as:

```text
SafeLink-20260824-182835.txt
```

SHA-256:

```text
2065a3af95159f9cc6dceb815d814d721f46e577819cd488d8483480e26174ea
```

The capture identifies the ESP32-C6 runtime as `network_adapter`, ESP-Hosted `2.12.9`, ESP-IDF `v5.5.4`, and shows slot 0 FAT32 mounted while the SafeLink SDIO transport on slot 1 remains active.

### Phase 1 — deterministic interruption

The P4 begins the fault-injection request and reaches exactly 1 MiB before the HTTP connection is deliberately cut:

```text
PHASE 1: requesting deterministic disconnect after 1 MiB
HTTP-to-SD progress: 1048576 / 5242880 bytes
HTTP_CLIENT: Incomlete data received, ret=-1, 1048576/5242880 bytes
Freight result: err=ESP_ERR_HTTP_INCOMPLETE_DATA ... received=1048576
```

The partial artifact is then removed and both temporary and final paths are confirmed absent:

```text
Removed artifact: /sdcard/SL6B.TMP
Expected interruption detected safely: ESP_ERR_HTTP_INCOMPLETE_DATA
Partial artifact absent: /sdcard/SL6B.TMP
Partial artifact absent: /sdcard/SL6B.BIN
PHASE 1 PASS: partial file removed; C6 transport remains active
```

### Phase 2 — recovery on the same boot

The same P4/C6 transport immediately performs a new HEAD preflight and clean 5 MiB download:

```text
HEAD preflight: err=ESP_OK HTTP=200 Content-Length=5242880
PHASE 2: starting clean 5 MiB recovery download
HTTP-to-SD progress: 5242880 / 5242880 bytes
Freight result: err=ESP_OK HTTP=200 ... received=5242880
```

The completed file is then read back from SD and verified byte-for-byte before the atomic commit and cleanup:

```text
Second-pass SD verification PASS: 5242880 bytes
Atomic commit PASS: /sdcard/SL6B.BIN
Final cleanup PASS; card returned to original contents
TEST 6B PASS: interrupted transfer cleaned up and 5 MiB recovery completed byte-perfectly
```

The final memory report is also healthy and the application returns normally:

```text
test complete: DMA free=398551 largest=352256 internal=432523
main_task: Returned from app_main()
```

## Preserved build evidence

The Test 6B application binary used during validation had SHA-256:

```text
44FC120C7F31BA073BD3507B7FAB839CCE6A033D2F83A689495D2A212338E9BA
```

The preserved build transcript verified all 10 required Test 6B markers were present in that binary, including:

```text
SafeLink Test 6B
PHASE 1: requesting deterministic disconnect after 1 MiB
Expected interruption detected safely
Partial artifact absent
PHASE 1 PASS: partial file removed; C6 transport remains active
PHASE 2: starting clean 5 MiB recovery download
Second-pass SD verification PASS
Atomic commit PASS
Final cleanup PASS
TEST 6B PASS
```

## Result

**PASS.** The deliberate 1 MiB interruption was detected without a P4 reboot or transport loss; the incomplete artifact was removed; the C6/SDIO path remained active; a new 5 MiB transfer completed on the same boot; the file passed second-pass SD verification; atomic commit and cleanup completed; and the application returned normally.

A later Test 6C extended this same recovery mechanism into a 25-cycle endurance test with multiple forced-disconnect positions.

## Evidence note

`CHECKSUMS.txt` is the checksum manifest from the original Test 6B kit and is intentionally left unchanged. The added serial capture and this `RESULT.md` are public evidence files layered on top of that preserved kit.

## Relationship to SafeLink v0.1.0

Test 6B belongs to the SafeLink laboratory validation sequence that preceded the final public v0.1.0 P4 port. It is published as development validation evidence, not as a claim that this exact Test 6B application binary was built from the final public ESP-Hosted 2.11.6 patch artifact.
