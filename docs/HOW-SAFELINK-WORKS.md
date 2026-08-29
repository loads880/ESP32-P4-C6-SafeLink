# How SafeLink Works

SafeLink is a reliability modification for ESP-Hosted communication between an ESP32-P4 host and an ESP32-C6 communications processor using SDIO.

Its purpose is to make host-side packet handling bounded and predictable when incoming network traffic temporarily arrives faster than the host application can process it.

## The Problem

In a networked ESP32-P4 / ESP32-C6 system, packets received by the ESP32-C6 are transferred to the ESP32-P4 through ESP-Hosted over SDIO.

Under normal conditions the P4 consumes those packets quickly enough that no significant queue develops.

Problems can occur when the host temporarily cannot keep up with incoming traffic.

Examples include:

* short processing stalls
* temporary application load
* bursts of network packets
* scheduling delays
* memory pressure
* other work temporarily preventing packets from being processed

SafeLink is designed around the principle that the P4 should never accept more packet data than it has explicitly reserved capacity to hold.

## SafeLink Architecture

The validated SafeLink configuration is:

```text
                     Network
                        |
                        v
                  +-----------+
                  | ESP32-C6  |
                  |           |
                  | ESP-Hosted|
                  |   slave   |
                  +-----------+
                        |
                        |
                   4-bit SDIO
                        |
                        v
                  +-----------+
                  | ESP32-P4  |
                  |           |
                  | ESP-Hosted|
                  |    host   |
                  +-----------+
                        |
                        v
                  +-----------+
                  | SafeLink  |
                  | packet    |
                  | pool      |
                  +-----------+
                        |
                        v
                 Host application
```

SafeLink operates on the ESP32-P4 host receive path.

## Fixed Packet Pool

SafeLink uses a fixed pool of preallocated packet buffers.

The pool is created before normal packet handling begins.

This means SafeLink knows in advance exactly how many packets it can safely accept.

The design does not depend on continuously allocating additional memory as traffic increases.

Conceptually:

```text
SafeLink packet pool

+-----------+
| Buffer 1  |
+-----------+

+-----------+
| Buffer 2  |
+-----------+

+-----------+
| Buffer 3  |
+-----------+

     ...

+-----------+
| Buffer N  |
+-----------+
```

A received packet must have an available SafeLink buffer before the host continues consuming it from the transport.

## Normal Operation

During normal operation:

```text
C6 has packet
      |
      v
P4 checks SafeLink pool
      |
      v
Buffer available?
      |
     YES
      |
      v
Packet accepted
      |
      v
Application processes packet
      |
      v
Buffer returned to pool
```

The same fixed buffers are reused repeatedly.

## Pool Exhaustion

The important SafeLink behaviour occurs when every receive buffer is already in use.

Without a defined limit, a host implementation can continue trying to consume incoming traffic even though the application has temporarily fallen behind.

SafeLink instead treats the absence of a free packet buffer as a transport-control condition.

```text
C6 has another packet
      |
      v
P4 checks SafeLink pool
      |
      v
Buffer available?
      |
      NO
      |
      v
STOP consuming additional
SDIO packet data
```

The P4 waits until receive capacity becomes available again.

## Back-Pressure

Stopping consumption is deliberate.

SafeLink does not attempt to solve overload by continually creating more buffering on the P4.

Instead, when the P4 has reached its defined receive capacity, it allows the existing ESP-Hosted transport relationship between the P4 and C6 to provide back-pressure.

Conceptually:

```text
Network traffic
      |
      v
+-------------+
| ESP32-C6    |
+-------------+
      |
      | SDIO packets
      v
+-------------+
| ESP32-P4    |
|             |
| SafeLink    |
| pool FULL   |
+-------------+
      X
      X  P4 temporarily stops
      X  consuming packets
      X
```

Once a SafeLink buffer is released:

```text
Application finishes with packet
             |
             v
SafeLink buffer becomes free
             |
             v
SDIO consumption can continue
```

The result is a bounded host receive system.

## Why Preallocation Matters

Dynamic memory allocation can make behaviour under heavy traffic harder to predict.

SafeLink instead reserves its packet capacity in advance.

This provides several useful properties:

* known maximum host-side receive capacity
* predictable memory consumption
* no requirement for unbounded receive queue growth
* no dependence on successfully allocating additional packet buffers during traffic bursts
* a clear point at which transport consumption must stop

The objective is not to create an extremely large queue.

The objective is to create a queue whose maximum size is known.

## Packet Mode

SafeLink v0.1 operates with ESP-Hosted packet mode.

Packet mode is a required invariant of the initial SafeLink implementation.

The validated configuration must therefore not be changed to another ESP-Hosted operating mode without reviewing and retesting the SafeLink transport behaviour.

## SDIO RX Optimisation

The validated SafeLink configuration has SDIO RX optimisation disabled.

This is intentional.

SafeLink depends on maintaining explicit control over when the P4 host consumes incoming packet data.

Enabling a receive optimisation that changes how packets are consumed could alter the back-pressure behaviour SafeLink relies upon.

For this reason:

```text
SDIO RX optimisation = disabled
```

is part of the SafeLink v0.1 validated configuration.

## 4-bit SDIO

The initial SafeLink implementation has been validated using 4-bit SDIO communication between the ESP32-P4 and ESP32-C6.

```text
ESP32-P4
   ||
   ||  4-bit SDIO
   ||
ESP32-C6
```

Other transport configurations should be considered unvalidated until tested independently.

## SafeLink Invariants

The initial implementation is based on several rules that should remain true when SafeLink is ported.

### Invariant 1 — Packet mode

ESP-Hosted operates in packet mode.

### Invariant 2 — Bounded receive capacity

The P4 has a fixed, preallocated packet-buffer pool.

### Invariant 3 — No packet without capacity

The host must not continue accepting packet data when no SafeLink packet buffer is available.

### Invariant 4 — Back-pressure

When the SafeLink pool is full, the P4 stops consuming additional SDIO packets.

### Invariant 5 — Resume when capacity returns

When buffers are returned to the pool, packet consumption may resume.

### Invariant 6 — Controlled receive behaviour

SDIO RX optimisation remains disabled for the validated SafeLink v0.1 configuration.

## What SafeLink Does Not Do

SafeLink is not:

* a replacement for ESP-Hosted
* a replacement network stack
* a replacement Wi-Fi driver
* a new SDIO protocol
* an application-level retry mechanism
* an unlimited packet queue

SafeLink modifies how the P4 host manages its available receive capacity while continuing to use ESP-Hosted and SDIO.

## Relationship to ESP-Hosted

SafeLink should be viewed as a reliability layer associated with the ESP32-P4 host-side ESP-Hosted transport path.

Conceptually:

```text
Application
     |
     v
Network APIs
     |
     v
SafeLink receive policy
     |
     v
ESP-Hosted host
     |
     v
SDIO
     |
     v
ESP-Hosted slave
     |
     v
ESP32-C6
```

SafeLink deliberately leaves the underlying ESP-Hosted transport responsible for communication between the two processors.

Its role is to prevent the P4 host from consuming packet data beyond the capacity that has been reserved for it.

## Design Philosophy

The central SafeLink rule is simple:

> Do not consume a packet unless there is already somewhere safe to put it.

When capacity has been exhausted:

> Stop consuming and allow back-pressure to occur.

This provides a deterministic upper boundary for host receive buffering and makes overload behaviour explicit rather than dependent on continued allocation or uncontrolled queue growth.

## Porting SafeLink

When SafeLink is ported to another ESP-Hosted version, copying individual source lines is not sufficient.

The porter should verify that the same behavioural invariants remain true:

```text
Packet mode
        +
Fixed packet pool
        +
Capacity checked before consumption
        +
Stop SDIO consumption when full
        +
Transport back-pressure
        +
Resume when capacity returns
```

Changes in newer ESP-Hosted versions may require the SafeLink logic to be adapted rather than applying the original patch directly.

Any new port should be hardware tested before being described as a validated SafeLink configuration.
