# SafeLink Configuration

This directory contains the reference configuration used for the validated SafeLink v0.1 ESP32-P4 ECO2 build.

## Reference fragment

`safelink-esp32p4.fragment`

This fragment records the SafeLink transport settings used with:

* Arduino-ESP32 3.3.7
* ESP32-P4 ECO2 / prev3
* ESP-Hosted host 2.11.6
* ESP32-C6 communications processor
* 4-bit SDIO
* ESP-Hosted packet mode
* SafeLink fixed receive-buffer pool
* SDIO RX optimisation disabled

The GPIO assignments and SDIO clock settings in this fragment are the values used on the hardware on which SafeLink was validated.

Developers using different ESP32-P4 / ESP32-C6 hardware may need different physical SDIO pin assignments. Such configurations should be treated as new hardware ports and tested accordingly.

The SafeLink behavioural invariants — particularly packet mode, bounded receive buffering, and disabling SDIO RX optimisation — should be preserved.
