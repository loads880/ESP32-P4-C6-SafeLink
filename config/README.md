# ESP32-P4-C6 SafeLink

SafeLink is a reliability modification for ESP32-P4 to ESP32-C6 networking using ESP-Hosted over SDIO.

The project provides only the source patch, configuration fragments, and documentation required to reproduce the SafeLink transport changes.

The private application in which SafeLink was originally integrated and validated is not included.

## Purpose

SafeLink is intended to improve reliability when an ESP32-P4 host communicates with an ESP32-C6 communications coprocessor using ESP-Hosted over SDIO.

The SafeLink host receive path uses bounded, preallocated packet buffering.

When no receive capacity remains available, the ESP32-P4 stops consuming additional SDIO packet data and allows the ESP-Hosted transport relationship with the ESP32-C6 to provide back-pressure.

## Architecture

SafeLink v0.1 uses:

```text
ESP32-P4
    ESP-Hosted host 2.11.6
    + SafeLink source patch
    + SafeLink P4 configuration
              |
              | 4-bit SDIO
              | packet mode
              |
ESP32-C6
    ESP-Hosted slave 2.12.9
    + packet-mode configuration
    + no SafeLink source-code patch
```

The SafeLink source modifications are on the ESP32-P4 host.

The ESP32-C6 side uses ESP-Hosted 2.12.9 with the configuration required for packet-mode operation.

## Design Principles

SafeLink v0.1 uses the following transport policy:

* ESP-Hosted packet mode
* 4-bit SDIO transport
* SDIO RX optimisation disabled on the P4
* Fixed preallocated P4 receive packet pool
* No unbounded receive-buffer growth
* Stop consuming SDIO packets when P4 receive capacity is exhausted
* Resume packet consumption when receive capacity becomes available
* Bounded SDIO recovery and retry behaviour

## Supported Configuration

SafeLink v0.1 is deliberately version-locked to the configuration on which it was developed and validated.

### ESP32-P4 Host

* ESP32-P4 ECO2 / prev3
* Arduino-ESP32 3.3.7
* ESP-Hosted host component 2.11.6
* SafeLink P4 source patch
* 4-bit SDIO
* Packet mode
* SDIO RX optimisation disabled

### ESP32-C6 Coprocessor

* ESP32-C6
* ESP-Hosted slave firmware 2.12.9
* Stock ESP-Hosted source
* Packet-mode configuration
* 4-bit SDIO

Other Arduino-ESP32, ESP-IDF, ESP-Hosted, or ESP32-P4 silicon versions have not yet been validated and are not currently supported.

Do not apply the P4 patch blindly to later ESP-Hosted releases.

## Repository Contents

### `patches/p4/`

Contains the SafeLink host-side source patch for ESP-Hosted 2.11.6.

The current validated patch is:

```text
safelink-p4-esp-hosted-2.11.6.patch
```

### `config/`

Contains the validated reference configuration fragments:

```text
safelink-esp32p4.fragment
safelink-esp32c6.fragment
```

The P4 fragment defines the SafeLink host transport requirements.

The C6 fragment records the packet-mode configuration used with ESP-Hosted slave 2.12.9.

### `docs/`

Contains supported-version information, architecture documentation, build instructions, and testing notes.

## Validation

SafeLink has completed:

* stock baseline reproduction
* build-time validation
* configuration validation
* patched-source selection verification
* binary verification
* ESP32-P4 ECO2 physical hardware testing
* ESP32-P4 to ESP32-C6 SDIO runtime testing
* integration testing inside a working ESP32-P4 networked application

The application used for integration testing is private and is not required to use SafeLink.

## Important C6 Note

SafeLink v0.1 does not modify the ESP32-C6 ESP-Hosted source code.

The validated C6 side uses ESP-Hosted 2.12.9 configured for packet-mode SDIO operation.

Therefore there is no `patches/c6/` source patch in this repository.

## Project Status

SafeLink v0.1 represents the first hardware-validated implementation.

The goal of this repository is to make that known-good implementation reproducible and reviewable rather than to claim compatibility with every ESP-Hosted release.

Future ports can be added as separately validated versions.

## License

SafeLink modifications are intended to be released under the Apache License 2.0.

Existing Espressif copyright and SPDX notices must be preserved where applicable.

## Disclaimer

SafeLink is an independent reliability modification and is not an official Espressif project.

ESP32, ESP-IDF, and ESP-Hosted are technologies and projects developed by Espressif Systems.
