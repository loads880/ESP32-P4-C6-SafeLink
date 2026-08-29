# Test 6C result — repeated recovery endurance

**Result: PASS**

Test 6C exercised 25 consecutive interrupted HTTP-to-SD recovery cycles in one firmware run. The deterministic fault server rotated disconnect points at 64 KiB, 1 MiB, and 4 MiB of a declared 5 MiB transfer.

For each cycle the test required the interrupted transfer to be detected, partial artifacts to be absent, the transport to remain usable, a clean 5 MiB recovery download to complete, the SD copy to pass a second byte-for-byte verification, and cleanup to complete before the next cycle.

Final firmware summary:

```text
CYCLE 25/25 PASS: cutoff=64KiB cleaned and recovery verified
Memory endurance PASS: no material settled-memory drift
TEST 6C PASS: 25/25 interrupted transfers cleaned up and recovered byte-perfectly without reboot
```

Settled-memory summary reported by the firmware:

```text
first DMA=398563 largest=352256 internal=432535
final DMA=395819 largest=352256 internal=429791
minimum DMA=395807 largest=352256 internal=429779
```

The included serial capture `SafeLink-20260824-212914.txt` begins during cycle 3, but contains the continuing run through cycle 25 and the firmware's final 25/25 PASS and memory-endurance summary. It is included unchanged.

SHA-256 of the included serial capture:

```text
38db8b96e0fae3f57c33ea77d2575bb9b9d6173e199c0921a8ffa60067d0f99b  SafeLink-20260824-212914.txt
```

The original Test 6C kit files remain unchanged and continue to match `CHECKSUMS.txt`.
