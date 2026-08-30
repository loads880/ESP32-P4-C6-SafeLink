# ESP32-P4-C6-SafeLink
Reliability patches for ESP32-P4 ↔ ESP32-C6 ESP-Hosted networking over SDIO.
# ESP32-P4-C6 SafeLink

SafeLink is a reliability modification for ESP32-P4 to ESP32-C6 networking using ESP-Hosted over SDIO.

The project provides only the patches and documentation required to reproduce the SafeLink transport changes. It does not contain the application in which SafeLink was originally developed and tested.

## Purpose

SafeLink is intended to improve reliability when an ESP32-P4 host communicates with an ESP32-C6 communications coprocessor using ESP-Hosted over SDIO.
The SafeLink receive path uses a bounded, preallocated packet-buffer pool.
When no receive buffers remain available, the ESP32-P4 stops consuming additional SDIO packets. This allows the existing ESP-Hosted transport to apply back-pressure rather than allowing host-side packet consumption to continue beyond available capacity.

## Design principles

SafeLink currently uses the following transport policy:

* ESP-Hosted packet mode
* 4-bit SDIO transport
* SDIO RX optimisation disabled
* Fixed preallocated receive packet pool
* No unbounded receive-buffer growth
* Stop consuming SDIO packets when the receive pool is exhausted
* Resume packet consumption when receive capacity becomes available

## Supported configuration

SafeLink v0.1 is deliberately version-locked to the configuration on which it has been developed and validated.

### ESP32-P4 host

* ESP32-P4 ECO2 / prev3
* Arduino-ESP32 3.3.7
* ESP-Hosted host component 2.11.6
* 4-bit SDIO

### ESP32-C6 coprocessor

* ESP32-C6
* ESP-Hosted slave firmware 2.12.9
* SafeLink packet-mode changes applied

Other Arduino-ESP32, ESP-IDF and ESP-Hosted versions have not yet been validated and are not currently supported.

Do not apply these patches blindly to later ESP-Hosted releases.

## Validation

SafeLink has completed:

* build-time validation
* configuration validation
* binary verification
* ESP32-P4 ECO2 hardware testing
* ESP32-P4 ↔ ESP32-C6 SDIO runtime testing
* integration testing inside a working ESP32-P4 networked application

The application used for validation is not part of this repository.

## Repository contents

Tests/

this folder contains some details of the testing that has been used 
the actual test files are not part of this release but 3 tests are shown as examples here to illustrate the integrity of safelink.
multiple downloads of 50gb files were soak tested over many hours and  disrupted and partial files with safelink recovery were
tested  using safelink including downloading to the SD card without disrupting the host.  I have added the serial logs of each test.

`patches/p4/`
Contains the SafeLink host-side changes for the ESP32-P4 ESP-Hosted transport.

`patches/c6/`
There are no patches for the C6 only a single configuration  change to select packet mode The C6 is stock 11.2.9

`docs/`
Contains the supported-version matrix, design description, build instructions and validation notes.

`checksums/`
Contains hashes for published reference artifacts where applicable.

## Project status
SafeLink v0.1 represents the first hardware-validated implementation.
The initial goal of this repository is to make the known-good implementation reproducible and reviewable rather than to claim compatibility with every ESP-Hosted release.
Future ports can be added as separately validated versions or in the future safelink will be superceded by espressif transport improvements

## License
SafeLink modifications are intended to be released under the Apache License 2.0.
Existing Espressif source copyright and SPDX notices must be preserved where applicable.

## Disclaimer
SafeLink is an independent experimental reliability modification and is not an official Espressif project.
ESP32, ESP-IDF and ESP-Hosted are technologies and projects developed by Espressif Systems.
