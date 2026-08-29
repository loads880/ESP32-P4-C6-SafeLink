# SafeLink Supported Versions

SafeLink v0.1 is deliberately tied to the software and hardware configuration on which it was developed and validated.

The purpose of this version lock is reliability and reproducibility.

SafeLink should not be assumed to work correctly with later or earlier versions of Arduino-ESP32, ESP-IDF, ESP-Hosted, or different ESP32-P4 silicon revisions without additional testing.

# Validated Configuration

| Component                          | Validated version / configuration |
| ---------------------------------- | --------------------------------- |
| Host processor                     | ESP32-P4                          |
| Host silicon                       | ECO2 / prev3                      |
| Communications processor           | ESP32-C6                          |
| Arduino-ESP32                      | 3.3.7                             |
| ESP-IDF baseline                   | release/v5.5                      |
| ESP-IDF commit                     | `87912cd291`                      |
| ESP32 Arduino lib-builder baseline | `8cabf2c`                         |
| P4 ESP-Hosted host component       | 2.11.6                            |
| `esp_wifi_remote`                  | 1.3.2                             |
| C6 ESP-Hosted slave firmware       | 2.12.9                            |
| Transport                          | 4-bit SDIO                        |
| ESP-Hosted operating mode          | Packet mode                       |
| SDIO RX optimisation               | Disabled                          |
| SafeLink host buffering            | Fixed preallocated packet pool    |

# P4 Host Baseline

The SafeLink v0.1 P4 host patch is based on the ESP-Hosted host component supplied by the Arduino-ESP32 3.3.7 ESP32-P4 build environment.

The validated host component is:

```text
esp_hosted 2.11.6
```

The Arduino-ESP32 baseline is:

```text
Arduino-ESP32 3.3.7
```

The ESP-IDF baseline associated with the validated Arduino build is:

```text
ESP-IDF release/v5.5
commit 87912cd291
```

The corresponding ESP32 Arduino library-builder provenance is:

```text
esp32-arduino-lib-builder
commit 8cabf2c
```

The validated remote Wi-Fi component is:

```text
esp_wifi_remote 1.3.2
```

# C6 Slave Baseline

The validated ESP32-C6 communications processor uses:

```text
ESP-Hosted slave firmware 2.12.9
```

with the SafeLink packet-mode changes required by this implementation.

The C6 side should therefore not be assumed to be an untouched stock ESP-Hosted 2.12.9 firmware image.
The SafeLink C6 changes will be provided separately from the P4 host changes so that the modifications on each processor can be reviewed independently.
note: when flashing the c6 you may want to park the p4 so it does not try to recover and reset the c6 

# ESP32-P4 Hardware

SafeLink v0.1 has been hardware validated on:

```text
ESP32-P4 ECO2
Arduino chip variant: prev3
```

Other ESP32-P4 revisions have not been validated as part of the SafeLink v0.1 release.

# SDIO Configuration

The validated SafeLink transport configuration is:

```text
ESP32-P4 host
       |
       |  4-bit SDIO
       |
ESP32-C6 communications processor
```

SafeLink v0.1 requires ESP-Hosted packet-mode operation.
SDIO RX optimisation is disabled in the validated configuration.
The SafeLink host receive path uses a fixed preallocated packet-buffer pool.
When the pool is exhausted, the P4 host stops consuming additional SDIO packets until receive capacity becomes available again. This allows transport back-pressure to occur rather than continuing host-side packet consumption beyond the available SafeLink buffer capacity.

# Unsupported Configurations

SafeLink v0.1 does not currently claim support for:

```text
Arduino-ESP32 versions other than 3.3.7
P4 ESP-Hosted host versions other than 2.11.6
C6 ESP-Hosted slave versions other than 2.12.9
ESP32-P4 silicon revisions other than the validated ECO2 / prev3 target
Alternative ESP-Hosted transport modes
SDIO RX optimisation enabled
```

These configurations may work after suitable porting, but they have not been validated by this project.

# Version Porting

Developers are welcome to port SafeLink to newer ESP-Hosted or Arduino-ESP32 releases.

A port should not be described as a validated SafeLink configuration until the relevant SafeLink invariants and runtime behaviour have been tested on physical hardware.

Future validated combinations can be documented separately so that the original v0.1 baseline remains reproducible.

# SafeLink v0.1 Compatibility Policy

The SafeLink v0.1 patches should be treated as version-specific source modifications rather than generic patches intended to apply automatically to arbitrary ESP-Hosted releases.

If a patch does not apply cleanly to another version, do not force it.

Instead, the SafeLink behaviour should be ported deliberately to the newer implementation and tested again.

# Validation Status

The configuration documented above has completed:

```text
Build validation                 PASS
Configuration validation         PASS
SafeLink binary verification     PASS
ESP32-P4 ECO2 hardware test      PASS
P4 <-> C6 SDIO runtime test      PASS
Application integration test     PASS
```

The application used for integration testing is private and is not part of the SafeLink repository.
The SafeLink repository contains only the transport modifications, supporting documentation, and material required to reproduce or review SafeLink itself.
