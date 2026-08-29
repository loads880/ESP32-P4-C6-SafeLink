# Changelog

All notable changes to ESP32-P4-C6 SafeLink will be documented in this file.

## v0.1.0 - 2026-08-29

Initial hardware-validated SafeLink release.

### Added

* SafeLink reliability patch for ESP32-P4 ESP-Hosted host 2.11.6.
* Fixed, preallocated P4 receive packet pool.
* SDIO back-pressure when host receive capacity is exhausted.
* Bounded SDIO retry and recovery behaviour.
* ESP32-P4 ECO2 / prev3 reference configuration.
* ESP32-C6 ESP-Hosted 2.12.9 packet-mode reference configuration.
* Build, architecture, supported-version, and testing documentation.
* Apache License 2.0.

### Validated

* Arduino-ESP32 3.3.7.
* ESP32-P4 ECO2 / prev3.
* P4 ESP-Hosted host 2.11.6.
* ESP32-C6 ESP-Hosted slave 2.12.9 using stock source and packet-mode configuration.
* 4-bit SDIO.
* Host and slave packet-mode agreement.
* Physical P4/C6 hardware operation.
* Network runtime operation.
* Integration inside a working ESP32-P4 networked application.

### Compatibility

SafeLink v0.1.0 is version-specific.

Later Arduino-ESP32, ESP-Hosted, or ESP32-P4 revisions should be treated as new ports and independently validated before being described as supported.
