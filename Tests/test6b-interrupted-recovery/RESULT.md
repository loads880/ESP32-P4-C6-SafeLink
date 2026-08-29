# Test 6B result — interrupted transfer recovery

**Status:** PASS  
**Test date:** 2026-08-24

## Purpose

Test 6B exercises recovery from a deterministic mid-transfer network failure while ESP32-P4 and ESP32-C6 remain connected through ESP-Hosted over SDIO.

The fault-injection server advertises a 5 MiB object and deliberately closes the first GET connection after exactly 1 MiB. The P4 test is required to detect the incomplete response without asserting or rebooting, remove the partial SD-card artifact, prove the transport is still usable, perform a fresh 5 MiB download on the same boot, verify the data again from SD, atomically commit the completed file, and clean up.

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

## Preserved fault-server evidence

The Test 6B server was version `1.1-test6b`. Its preserved console transcript records the deliberate fault and the subsequent recovery requests:

```text
GET /fault/drop/5MiB-at-1MiB.bin HTTP/1.1 200
TEST 6B forced disconnect after 1048576 of 5242880 bytes
HEAD /download/5MiB.bin HTTP/1.1 200
GET /download/5MiB.bin HTTP/1.1 200
```

This shows that the first transfer was cut at exactly 1 MiB and that the same P4 subsequently reached the server again for the clean recovery HEAD and GET sequence rather than losing the ESP-Hosted transport.

## Result

The development validation record for Test 6B is **PASS**: the deliberate 1 MiB disconnect was handled as an incomplete transfer, the partial artifact was cleaned up, the C6/SDIO transport remained usable, and the clean 5 MiB recovery path completed with the test's byte-verification and cleanup checks.

A later Test 6C extended this same recovery mechanism into a 25-cycle endurance test with multiple forced-disconnect positions.

## Evidence note

The original Test 6B kit is preserved here byte-for-byte. I did not find a standalone full P4 serial capture from the Test 6B run in the saved artifact set available here, so this public result summary intentionally uses only the preserved build transcript, fault-server transcript, original test contract, and recorded PASS result. It does not invent missing serial lines.

## Relationship to SafeLink v0.1.0

Test 6B belongs to the SafeLink laboratory validation sequence that preceded the final public v0.1.0 P4 port. It is published as development validation evidence, not as a claim that this exact Test 6B application binary was built from the final public ESP-Hosted 2.11.6 patch artifact.
