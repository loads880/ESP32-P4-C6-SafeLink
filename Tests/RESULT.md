# SafeLink Test 4 — recorded result

This file summarizes the recorded hardware run of **SafeLink Test 4 — deterministic HTTP download**.

> Historical validation note: Test 4 was run during SafeLink development before the final Arduino-ESP32 3.3.7 / ESP-Hosted 2.11.6 P4 port published as SafeLink v0.1.0. The test is included as development validation evidence for the SafeLink packet-mode, bounded-buffer design. It should not be interpreted as a binary-level reproduction of the final v0.1.0 P4 patch.

## Test objective

Exercise sustained inbound HTTP traffic through the complete path:

```text
Windows HTTP server -> Wi-Fi -> ESP32-C6 -> 4-bit SDIO -> ESP32-P4 -> validate -> discard
```

Every received byte was compared against the server's deterministic `00..FF` pattern. The P4 application used a 1,024-byte HTTP receive buffer.

## Recorded environment

The captured serial log reports:

```text
SafeLink Test 4 - deterministic HTTP download
SafeLink fixed RX pool, backpressure and TX retry remain active
SDIO Host operating in PACKET MODE
SDIO mode: slave: packet, host: packet
C6 project=network_adapter version=2.12.9 IDF=v5.5.4
```

## Baseline transfers

The test first completed:

```text
1 MiB   PASS
5 MiB   PASS
25 MiB  PASS
50 MiB  PASS
```

The recorded 50 MiB baseline result was:

```text
HTTP=200
declared=52428800
received=52428800
elapsed=44926 ms
rate=1139 KiB/s
SafeLink download 4 PASS (50 MiB)
```

After this baseline, the application entered its repeated 50 MiB endurance loop.

## Endurance result

The captured log contains **415 completed 50 MiB transfers** (downloads 4 through 418 inclusive).

Including the earlier 1, 5 and 25 MiB transfers, the recorded run completed approximately:

```text
20,781 MiB
20.29 GiB
```

of deterministic receive traffic with byte-pattern validation.

The final completed transfer in the captured log was:

```text
Download 418 result: err=ESP_OK HTTP=200 declared=52428800 received=52428800
SafeLink download 418 PASS (50 MiB)
```

Logging stopped while download 419 was in progress; this is not recorded as a completed transfer.

## Failure scan

The captured run was checked for the failure indicators called out by the Test 4 README.

No recorded occurrence was found of:

```text
assert failed
core dump
transport restart / transport failure
SafeLink RX pool exhausted
SafeLink RX resumed after exhaustion
SafeLink TX retry/recovery failure
integrity failure
length failure
SDIO timeout
```

Therefore this run is evidence of sustained, integrity-checked network receive operation without the RX allocation/assert failure appearing during the captured endurance period.

It does **not** by itself prove the deliberate pool-exhaustion/back-pressure branch, because no pool-exhaustion event occurred in this run.

## Memory observations

During the repeated 50 MiB transfers, the log repeatedly reported a largest DMA-capable block of approximately:

```text
352256 bytes
```

while transfers continued successfully. Free DMA/internal memory moved within a bounded range during the test and recovered after completed downloads.

## Result

```text
SAFELINK TEST 4 — PASS
```

The recorded run demonstrates:

- packet mode on both host and slave;
- ESP32-C6 ESP-Hosted 2.12.9 operation;
- successful deterministic HTTP transfers from 1 MiB through 50 MiB;
- repeated 50 MiB endurance transfers;
- more than 20 GiB of completed validated receive traffic in the captured log;
- no recorded RX assert, core dump, transport restart, integrity failure, or length failure.

## Evidence identity

Original Test 4 kit:

```text
SafeLink-Test4-HTTP-Download-2026-08-22.zip
SHA-256: 34eb76e575792ac11ca9682efdc5de0b8096dea4749f22d30c775023656b4746
```

Recorded full serial log used for this summary:

```text
SafeLink-20260823-045045.txt
SHA-256: 274027089be09ba01a74e866e6aaf81cc2a28a14e085870adccf6549b1597fbd
```

The complete serial log is not included in this directory because the concise result above is intended to keep the repository lightweight. The hash records the exact source log used to prepare this summary.
