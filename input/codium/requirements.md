# opener — DECT-2020 NR Implementation Requirements

**Document version**: 0.2  
**Status**: Draft  
**Standard references**: ETSI TS 103 636-1 V1.6.1, ETSI TS 103 636-4 V1.6.1, ETSI TS 103 636-5 V2.2.1  
**Related documents**:

- [use-cases.md](use-cases.md) — application-level use-case requirements that drive the design choices in this document
- [spi-rpc-design.md](spi-rpc-design.md) — SPI RPC architecture specification
- [spi-rpc-architecture.svg](spi-rpc-architecture.svg) — SPI RPC architecture diagram

---

## Overview

This document specifies the implementation requirements for a DECT-2020 NR (New Radio) protocol stack targeting embedded and host-controlled deployments. The following sections summarise the principal architectural choices and the rationale behind each.

### Dual-role FT+PT support

DECT-2020 NR defines two operational modes for a Radio Device: Fixed Termination point (FT), which coordinates local radio resources and drives beaconing, and Portable Termination point (PT), which associates with an FT for data exchange. A device may operate as FT only, PT only, or both simultaneously (FT+PT), the last being the building block of the self-organising mesh topology defined in ETSI TS 103 636-1.

The stack is designed to support all three configurations. FT and PT code paths are independently gated by Kconfig symbols (`CONFIG_OPENER_FT`, `CONFIG_OPENER_PT`), so a constrained device may be compiled with only the subset it needs, with no dead code in flash. This is particularly important on nRF91x1 targets where flash and RAM budgets are tight. The modular structure ensures that FT-specific logic (beacon generation, association acceptance, resource coordination) and PT-specific logic (beacon scanning, association initiation, mobility) never pollute each other's build.

### Device-only and device+host deployment models

Two fundamentally different deployment configurations are anticipated.

In **device-only** deployment, the complete stack — from PHY to the Convergence layer — runs as a Zephyr application on the radio MCU (nRF91x1 or a future equivalent). The application logic is co-located on the same device. This model is suited to autonomous sensor nodes, simple cable-replacement products, and any application where adding a separate host processor is undesirable. The SPI RPC transport is entirely absent from the build.

In **device+host** deployment, a host processor (running Linux, Windows, or any other OS) is connected to the radio MCU over SPI. The host can implement any subset of the protocol stack modules — for example, the Association Manager, Beacon Manager, DLC Routing Service, or the entire DLC and CVG layers — while the radio MCU retains the latency-sensitive modules (Radio Scheduler, HARQ Engine, Spectrum Manager). This model is suited to industrial gateways, access points managing hundreds of simultaneous associations, or products where a general-purpose OS is already present and it is natural to run higher-level protocol logic there.

The key design principle enabling this split is that every module in the stack is expressed as a C interface (a struct of function pointers). A host-side module and a device-side module are indistinguishable from the perspective of their callers: the RPC transport provides a transparent proxy. This means the same application code works regardless of where each module physically executes.

### SPI-based RPC transport

The SPI bus was chosen as the physical transport between device and host for several reasons: it is universally available on embedded MCUs and Linux-capable SoCs, it supports full-duplex operation, and it offers deterministic latency without the protocol overhead of USB or UART framing. The bus operates at 8 MHz, sized to carry DECT-2020 NR MAC PDU payloads with adequate headroom.

The RPC protocol is binary, with a fixed header carrying module ID, procedure ID, sequence number, and payload length, followed by a serialized payload and a CRC-16. Asynchronous events (radio receptions, link state changes, RSSI notifications) are pushed from device to host using a dedicated IRQ GPIO line that the host samples to initiate a transfer, avoiding polling. The host-side RPC library is a plain C library with no OS dependency, so it can be linked into any host environment.

A critical safety property of the RPC layer is that it refuses to let the stack initialise if any module has been disabled on the device without a corresponding host implementation registered. This prevents silent misconfiguration where a required protocol function is simply absent.

### Pluggable radio scheduler with policy system

The Radio Scheduler is the sole arbiter of radio resources. Rather than hard-coding a single scheduling strategy, it is exposed as a pluggable interface: two reference implementations are provided (a static round-robin scheduler for deterministic URLLC use cases, and a dynamic priority scheduler for mMTC deployments), and integrators may supply their own. On top of the scheduler, a chained policy system allows runtime injection of scheduling hints — for example, a power-saving policy that biases the scheduler toward fewer, larger transmissions, or a QoS policy that enforces per-flow deadlines. Policies can be installed and removed at runtime without interrupting ongoing connections.

### Modular architecture with well-defined interfaces

Every named function in the stack — Association Manager, Beacon Manager, Spectrum Manager, HARQ Engine, MAC Security Engine, DLC Routing Service, CVG transmission service, and others — is a distinct module behind a versioned C interface. Modules register themselves with a per-layer registry at startup. Any module can be replaced at runtime by deregistering the current implementation and registering a new one. This design supports: A/B testing of module implementations, remote module execution via RPC, test doubles for unit testing without hardware, and incremental delivery where unimplemented modules are stubbed until ready.

### Coding standards

All code shall comply with the Zephyr coding style and MISRA C:2012. MISRA C:2012 takes precedence on conflicts. This combination ensures that the codebase is readable by engineers familiar with the Zephyr ecosystem while meeting the safety and portability guarantees required for a wireless protocol stack deployed in industrial and critical IoT applications. Intentional deviations are tracked in per-module deviation records.

---

## Requirement notation

- **SHALL**: mandatory requirement.
- **SHOULD**: recommended; deviation requires documented justification.
- **MAY**: optional feature.
- **REQUIRED ON HOST**: when a module is disabled on the device at build time in a device+host deployment, the host MUST provide an equivalent implementation reachable over the RPC transport. The build system SHALL enforce this.

Identifiers follow the scheme `[DOMAIN]-[MODULE]-[NNN]`.

---

## 1. System-Level Requirements (SYS)

### 1.1 General

**SYS-001** — The stack SHALL implement the DECT-2020 NR protocol as defined in ETSI TS 103 636 series, Release 1 for the MAC layer and Release 2 for the DLC and Convergence layers.

**SYS-002** — The stack SHALL be written in C (C11 minimum).

**SYS-002a** — All source code SHALL conform to the Zephyr coding style and to MISRA C:2012. Where the two guidelines conflict, MISRA C:2012 takes precedence. Every intentional deviation from a MISRA C:2012 mandatory or required rule SHALL be documented in a deviation record, stating the rule number, the location, and the technical justification.

**SYS-003** — The stack SHALL target the Zephyr RTOS and SHALL use only Zephyr kernel primitives (threads, semaphores, mutexes, message queues, work queues, timers) for synchronization and scheduling.

**SYS-004** — The stack SHALL be buildable with the nRF Connect SDK (NCS) targeting nRF91x1 series SoCs (nRF9161, nRF9151) as first-generation hardware.

**SYS-005** — The stack architecture SHALL be designed to support future chipsets (e.g., LMS 2027 target) with minimal porting effort, limited to hardware abstraction layer (HAL) adaptations.

**SYS-006** — All modules SHALL expose their configuration through Zephyr Kconfig symbols, allowing compile-time feature selection without source modification.

**SYS-007** — All modules SHALL use the Zephyr logging subsystem (`LOG_MODULE_REGISTER`) with per-module log levels configurable at compile time and runtime.

**SYS-008** — The stack SHALL support simultaneous FT mode, PT mode, and combined FT+PT mode of operation as defined in ETSI TS 103 636-1 clause 3.1.

**SYS-008a** — FT mode support SHALL be independently activatable at build time via `CONFIG_OPENER_FT`. When `CONFIG_OPENER_FT` is disabled, all FT-only code paths (beacon generation, association response, resource coordination) SHALL be excluded from the build and SHALL NOT occupy flash or RAM.

**SYS-008b** — PT mode support SHALL be independently activatable at build time via `CONFIG_OPENER_PT`. When `CONFIG_OPENER_PT` is disabled, all PT-only code paths (beacon scanning, association request, uplink scheduling) SHALL be excluded from the build and SHALL NOT occupy flash or RAM.

**SYS-008c** — At least one of `CONFIG_OPENER_FT` or `CONFIG_OPENER_PT` SHALL be enabled. The build system SHALL emit a compile-time error if both are disabled.

### 1.4 Deployment Models

**SYS-014** — The stack SHALL support two deployment models, selectable at build time via `CONFIG_OPENER_DEPLOYMENT`:

- **Device-only** (`CONFIG_OPENER_DEPLOYMENT_STANDALONE`): The entire stack and the application run as a Zephyr application on the same MCU (e.g., nRF9161). No SPI host interface is present. All modules SHALL execute on-device.

- **Device+host** (`CONFIG_OPENER_DEPLOYMENT_HOST_CONTROLLED`): A host processor (running Linux, Windows, or any OS) communicates with the Zephyr device via the SPI RPC transport. Specific modules MAY be disabled on the device and REQUIRED ON HOST.

**SYS-015** — In device-only deployment, the RPC transport layer SHALL be excluded entirely from the build. No SPI driver, framing, or serialization code SHALL be compiled or linked.

**SYS-016** — In device+host deployment, the build system SHALL verify at link time that every module not compiled into the device firmware has a corresponding RPC proxy stub registered, ensuring no module is silently absent from the system.

**SYS-017** — Modules marked as REQUIRED ON HOST in device+host deployment SHALL declare this requirement via a Kconfig dependency. When the device-side implementation is disabled (`CONFIG_OPENER_MODULE_<NAME>=n`), a Kconfig warning SHALL be emitted reminding the integrator that the host implementation is mandatory.

### 1.2 Memory and Performance

**SYS-009** — In device-only deployment, the stack SHALL operate within the memory constraints of the nRF91x1 (1 MB flash, 256 KB RAM) when compiled with a representative feature subset (PT-only mode, single association, DLC service type 2, CVG service type 2). In device+host deployment, association state, routing tables, and CVG session state MAY be offloaded to host memory, relaxing the device-side RAM constraint accordingly.

**SYS-010** — All memory allocations in time-critical paths (MAC scheduler, HARQ processing) SHALL use statically pre-allocated pools. Dynamic allocation (`k_malloc`) is prohibited in interrupt context and in scheduler critical paths.

**SYS-011** — The MAC layer SHALL process a received PHY packet and enqueue the next transmission decision within a budget compatible with the Guard Interval (GI) of the selected numerology, as specified in ETSI TS 103 636-3.

### 1.3 Compliance and Testing

**SYS-012** — Each module SHALL provide a unit-testable interface usable with the Zephyr twister test framework (native_posix target).

**SYS-013** — Each module SHALL expose diagnostic counters (Tx/Rx packet counts, error counts, HARQ statistics, etc.) accessible via a structured API and optionally via the Zephyr shell subsystem.

---

## 2. Platform and HAL Requirements (PLT)

**PLT-001** — The stack SHALL define a hardware abstraction layer (HAL) interface that isolates all chipset-specific operations. By default, upper layers (MAC and above) SHALL NOT directly call any chipset-specific API, and all PHY interactions SHALL go through `struct opener_hal_ops`.

**PLT-001a** — As an exception to PLT-001, direct chipset PHY API access MAY be enabled per chipset via a dedicated Kconfig guard (`CONFIG_OPENER_DIRECT_PHY_<CHIPSET>`, e.g., `CONFIG_OPENER_DIRECT_PHY_NRF91X1`). When enabled, the Radio Scheduler and HARQ Engine MAY bypass `struct opener_hal_ops` and call the chipset PHY API directly for latency-critical operations. All direct-access call sites SHALL be enclosed in `#if defined(CONFIG_OPENER_DIRECT_PHY_<CHIPSET>)` guards to ensure portability is never silently broken.

**PLT-001b** — The HAL ops struct SHALL remain fully implementable even when direct PHY access is enabled, so that test environments and future chipsets can always use the standard path without code changes.

**PLT-002** — The HAL SHALL expose the following capability groups:
- PHY packet transmission (schedule and send a physical layer packet on a given subslot/channel)
- PHY packet reception (register a receive callback per subslot/channel)
- RSSI measurement (RSSI-1 and RSSI-2 as defined in ETSI TS 103 636-2)
- Frame timing (current System Frame Number, subslot counter)
- Radio power control (set TX power level)
- Radio on/off and sleep/wake

**PLT-003** — The HAL interface SHALL be defined as a C struct of function pointers (`struct opener_hal_ops`) allowing compile-time or runtime binding to a chipset implementation.

**PLT-004** — The HAL SHALL deliver PHY events (packet received, transmission complete, RSSI measurement result) to the MAC layer through a Zephyr message queue or work queue callback, never by direct function call from interrupt context into MAC logic.

**PLT-005** — The nRF91x1 HAL implementation SHALL use the nRF modem library (`nrf_modem_dect_phy`) API provided by NCS. When `CONFIG_OPENER_DIRECT_PHY_NRF91X1` is enabled, the Radio Scheduler and HARQ Engine SHALL use `nrf_modem_dect_phy` calls directly at the guarded call sites.

---

## 3. SPI-based RPC Transport Requirements (RPC)

### 3.1 Purpose and Scope

**RPC-001** — Each layer (MAC, DLC, CVG) and each named module within a layer SHALL expose its public API as an RPC-callable interface over SPI, allowing a host processor (running Linux, Windows, or any other OS) to invoke stack operations remotely. The RPC layer SHALL be OS-agnostic on the host side: it SHALL expose a plain C API with no dependency on any specific OS or framework.

**RPC-002** — The RPC transport SHALL operate in a master/slave model. The Zephyr device (nRF91x1) is the SPI slave. The host is the SPI master.

**RPC-003** — The RPC layer SHALL allow the host to selectively own specific modules (e.g., FT association manager, beacon manager) while leaving other modules (e.g., radio scheduler, HARQ engine) executing on the Zephyr device.

**RPC-004** — A module running on the host SHALL appear to the on-device stack as a local module implementation via the same module interface (struct of function pointers), with the RPC transport providing transparent proxying.

### 3.2 Protocol

**RPC-005** — The SPI bus SHALL operate at 8 MHz clock speed. The framing format SHALL be designed to minimize SPI transaction overhead, preferring fewer large transfers over many small ones to maximize effective bandwidth for NR+ packet payloads.

**RPC-006** — The RPC protocol SHALL use a binary framing format with:
- A fixed-size header: magic byte, protocol version, module ID, procedure ID, sequence number, payload length
- A variable-length payload (serialized procedure arguments or return values)
- A trailing CRC-16

**RPC-007** — The RPC layer SHALL support both synchronous calls (host waits for response) and asynchronous notifications (device pushes events to host without a prior request).

**RPC-008** — The device SHALL assert a dedicated interrupt line (IRQ GPIO) to signal the host that an asynchronous notification is pending.

**RPC-009** — The RPC layer SHALL implement a sequence number mechanism to detect dropped or out-of-order frames and SHALL surface such errors to the caller.

**RPC-010** — Module IDs and procedure IDs SHALL be defined in a shared header, versioned alongside the stack, to guarantee binary compatibility between host and device firmware.

**RPC-011** — The RPC layer SHALL support a discovery procedure by which the host can query which modules are currently active on the device and whether each is locally executing or proxied.

**RPC-012** — When a module is disabled on the device (`CONFIG_OPENER_MODULE_<NAME>=n`) in a device+host deployment, the RPC layer on the device SHALL register a mandatory-proxy stub for that module. If the host has not registered a remote implementation for that module before the stack is started, the stack SHALL refuse to initialize and SHALL return an explicit error (`-ENODEV`) identifying the missing module by name.

**RPC-013** — The RPC host-side library SHALL be compilable as a standalone C library with no Zephyr dependency, suitable for linking into Linux userspace applications, Windows applications, or any bare-metal host environment.

### 3.3 Serialization

**RPC-014** — Serialization of procedure arguments and return values SHALL use a compact, schema-defined binary format. A code generator SHOULD be used to produce serializer/deserializer stubs from a declarative interface description.

**RPC-015** — The serialization format SHALL be deterministic (no padding variation across compilers or platforms) and SHALL be explicitly documented per procedure.

---

## 4. MAC Layer Requirements (MAC)

### 4.1 General Architecture

**MAC-001** — The MAC layer SHALL be implemented as a set of loosely coupled modules. Each module SHALL be independently compilable, testable, and replaceable.

**MAC-002** — Inter-module communication within the MAC layer SHALL use well-defined C interfaces (structs of function pointers). Direct function calls between modules SHALL NOT bypass these interfaces.

**MAC-003** — The MAC layer SHALL expose a single unified API to the DLC layer above (`struct opener_mac_ops`) and SHALL consume a single unified API from the PHY/HAL below (`struct opener_hal_ops`).

**MAC-004** — The MAC layer SHALL support concurrent operation of the following modules, each running in its own Zephyr thread or work queue context:
- Radio Scheduler
- Spectrum Manager
- Association Manager
- Beacon and Broadcast Manager
- Random Access Controller
- HARQ Engine
- Connection Configuration Controller
- Paging Controller
- Mobility Manager
- MAC Security Engine
- MAC Multiplexer/Demultiplexer

**MAC-005** — Each MAC module SHALL register itself with a MAC module registry at initialization, exposing: module ID, module name, version, pointer to its ops struct, and pointer to its diagnostics struct.

**MAC-006** — Any MAC module SHALL be replaceable at runtime by deregistering the current implementation and registering a new one, provided the replacement satisfies the same module interface. The transition SHALL be atomic with respect to ongoing radio operations.

---

### 4.2 Radio Scheduler (MAC-SCH)

**MAC-SCH-001** — The Radio Scheduler SHALL be the sole authority over the assignment of transmission and reception resources (subslots) on the operating channel(s). All other modules SHALL request resources from the scheduler and SHALL NOT directly issue PHY commands, except where PLT-001a applies.

**MAC-SCH-001a** — The Radio Scheduler SHALL be compiled and present in all build configurations (device-only and device+host). It SHALL NOT be disableable. In device+host deployment it MAY be REQUIRED ON HOST if `CONFIG_OPENER_MODULE_SCHEDULER=n`, though this is not recommended given its timing sensitivity.

**MAC-SCH-002** — The Radio Scheduler SHALL be implemented behind a pluggable interface (`struct opener_scheduler_ops`) with the following mandatory operations:
- `schedule_tx`: request a TX resource for a given MAC PDU, priority, and earliest/latest subslot window
- `schedule_rx`: request an RX resource for a given subslot range
- `cancel`: cancel a pending resource request by handle
- `reconfigure`: update scheduler-wide policy parameters at runtime
- `get_stats`: retrieve scheduler performance counters

**MAC-SCH-003** — The stack SHALL include at least two scheduler implementations selectable at compile time and switchable at runtime:
- **Static Round-Robin Scheduler**: allocates subslots in a fixed repeating pattern, suitable for deterministic URLLC use cases.
- **Dynamic Priority Scheduler**: allocates subslots based on per-flow priorities, deadlines, and current channel occupancy, suitable for general mMTC use cases.

**MAC-SCH-004** — The Radio Scheduler SHALL support a **policy plug-in system**. A policy is a module (`struct opener_scheduler_policy_ops`) that the scheduler consults at each scheduling decision point. Policy operations SHALL include:
- `on_tx_request`: may modify or annotate a TX request (priority, MCS hint, retransmission delay)
- `on_rx_result`: called after each reception with RSSI and BLER feedback
- `on_channel_status`: called when spectrum manager updates channel occupancy
- `on_load_threshold`: called when channel load crosses a configured threshold

**MAC-SCH-005** — Multiple scheduler policies MAY be active simultaneously and SHALL be chained in priority order. The scheduler SHALL apply policies in order, with each policy able to pass, modify, or veto a scheduling decision.

**MAC-SCH-006** — The scheduler policy interface SHALL allow runtime installation and removal of policies without restarting the scheduler or dropping ongoing connections.

**MAC-SCH-007** — The Radio Scheduler SHALL implement the Listen Before Talk (LBT) procedure as defined in ETSI TS 103 636-4 clause 5.3.1, with configurable `CW_MIN`, `CW_MAX`, `MINIMUM_LBT_PERIOD`, and exponential backoff timer `rachBackOff`.

**MAC-SCH-008** — The Radio Scheduler SHALL track and expose the current System Frame Number (SFN) and SHALL maintain subslot-level timing synchronization with the operating channel frame structure.

**MAC-SCH-009** — The Radio Scheduler SHALL support preemption: a higher-priority request arriving after a lower-priority transmission has been queued but not yet started SHALL be able to displace the lower-priority transmission.

---

### 4.3 Spectrum Manager (MAC-SPM)

**MAC-SPM-001** — The Spectrum Manager SHALL implement the background scan procedure defined in ETSI TS 103 636-4 clause 5.1.2, classifying each subslot per channel as "free", "possible", or "busy" based on RSSI-1 measurements against configurable thresholds `RSSI_THRESHOLD_MIN` and `RSSI_THRESHOLD_MAX`.

**MAC-SPM-002** — The Spectrum Manager SHALL implement the operating channel selection logic defined in ETSI TS 103 636-4 clause 5.1.2: prefer all-free channels; otherwise select the channel with the minimum number of "busy" subslots.

**MAC-SPM-003** — The Spectrum Manager SHALL implement the Last Minute Scan procedure defined in ETSI TS 103 636-4 clause 5.1.3 when `OPENER_PROTECTED` is TRUE.

**MAC-SPM-004** — The Spectrum Manager SHALL maintain a scan validity timer (`scanStatusValid`) and SHALL notify the Radio Scheduler and Beacon Manager when channel status has expired or been refreshed.

**MAC-SPM-005** — The Spectrum Manager SHALL expose a callback interface (`struct opener_spm_event_ops`) through which it notifies other modules of channel status changes (channel selected, channel loaded, channel lost).

**MAC-SPM-006** — The Spectrum Manager SHALL support multi-channel operation (multiple simultaneous operating channels) as permitted by the standard.

**MAC-SPM-007** — The Spectrum Manager SHALL expose a **raw RSSI series API** (`struct opener_spm_rssi_ops`) delivering timestamped RSSI-1 samples per subslot per channel to registered consumers. This API SHALL be independently activatable via `CONFIG_OPENER_SPM_RAW_RSSI`. When disabled, the corresponding measurement callbacks and ring buffers SHALL be excluded from the build. In device+host deployments, raw RSSI series MAY be streamed to the host via RPC for off-device spectrum analysis, machine learning, or regulatory logging.

**MAC-SPM-008** — The Spectrum Manager SHALL expose a **processed channel status API** (`struct opener_spm_status_ops`) delivering per-channel, per-subslot classification ("free" / "possible" / "busy"), the current `scanStatusValid` timer state, and the selected operating channel(s). This API SHALL be independently activatable via `CONFIG_OPENER_SPM_CHANNEL_STATUS`. When disabled, the classification logic SHALL be excluded but SHALL be REQUIRED ON HOST in device+host deployments where the host performs channel selection.

**MAC-SPM-009** — At least one of `CONFIG_OPENER_SPM_RAW_RSSI` or `CONFIG_OPENER_SPM_CHANNEL_STATUS` SHALL be enabled in any build. The build system SHALL emit a compile-time error if both are disabled.

---

### 4.4 Association Manager (MAC-ASC)

**MAC-ASC-001** — The Association Manager SHALL maintain the full state of all associations. For each association it SHALL store: Long RD ID (peer), Short RD ID (peer), Short RD ID (self), association state (associating / associated / releasing), negotiated capabilities, current MCS, active HARQ configuration, and current security context handle.

**MAC-ASC-002** — The Association Manager SHALL implement the complete association procedure defined in ETSI TS 103 636-4 clause 5.8:
- FT side: send beacon, receive and validate Association Request, send Association Response with Short RD ID assignment
- PT side: scan beacons, select FT (RSSI-2 quality + route cost), send Association Request, process Association Response

**MAC-ASC-003** — The Association Manager SHALL implement the Association Release procedure (clause 5.8.6) and SHALL notify upper layers and the Radio Scheduler upon link teardown.

**MAC-ASC-004** — The Association Manager SHALL support simultaneous association with multiple peers (FT tracking multiple PTs, PT operating in FT+PT mode tracking both its upstream FT and downstream PTs).

**MAC-ASC-004a** — In device-only deployment, the maximum number of simultaneous associations SHALL be bounded by `CONFIG_OPENER_MAX_ASSOCIATIONS` (Kconfig integer), with each association context allocated from a statically sized pool. The default value SHALL be chosen to fit within nRF91x1 RAM constraints; the build system SHALL assert a static size check.

**MAC-ASC-004b** — In device+host deployment, the Association Manager MAY be REQUIRED ON HOST (`CONFIG_OPENER_MODULE_ASSOCIATION_MANAGER=n`), in which case association state for potentially hundreds of simultaneous associations is managed by the host. The on-device MAC layer SHALL then operate with a lightweight association context cache limited to the RDs actively scheduled in the current radio frame, sized by `CONFIG_OPENER_ASSOC_CACHE_ENTRIES`.

**MAC-ASC-005** — The Association Manager SHALL expose an event interface (`struct opener_asc_event_ops`) for: association established, association released, association failed, peer capability updated.

**MAC-ASC-006** — The Association Manager SHALL be the sole module allowed to allocate and deallocate Short RD IDs. Short RD ID allocation SHALL use a random draw from the range 0x0001–0xFFFE with collision detection as defined in clause 4.2.3.3.

**MAC-ASC-007** — The Association Manager SHALL persist association context to Zephyr settings storage (NVS or equivalent) so that associations can survive a device reboot without a full re-association procedure.

**MAC-ASC-008** — The FT-side association logic (accepting Association Requests, assigning Short RD IDs to PTs) SHALL be conditionally compiled under `CONFIG_OPENER_FT`. The PT-side association logic (sending Association Requests, processing Association Responses) SHALL be conditionally compiled under `CONFIG_OPENER_PT`. When operating in FT+PT mode, both code paths SHALL be active.

**MAC-ASC-009** — In device+host deployment, the Association Manager is a candidate for host execution (REQUIRED ON HOST when `CONFIG_OPENER_MODULE_ASSOCIATION_MANAGER=n`). The RPC interface for the Association Manager SHALL expose all events defined in MAC-ASC-005 as asynchronous RPC notifications to the host, and SHALL allow the host to inject association decisions (accept/reject) via synchronous RPC calls.

---

### 4.5 Beacon and Broadcast Manager (MAC-BCN)

**MAC-BCN-001** — The Beacon and Broadcast Manager SHALL implement the broadcast procedure defined in ETSI TS 103 636-4 clause 5.2.

**MAC-BCN-002** — The Beacon Manager SHALL generate and transmit two beacon types when operating in FT mode:
- **Network Beacon** (clause 6.4.2.2): carries Network ID, operating channel, timing of next Cluster Beacon, cluster max TX power. Transmission period independently configurable.
- **Cluster Beacon** (clause 6.4.2.3): carries frame/slot timing, RACH resources, Route Info IE, Load Info IE, and optional IEs. Transmission period independently configurable.

**MAC-BCN-003** — The Beacon Manager SHALL allow the host (via RPC) or an upper-layer module to inject additional optional IEs into the next scheduled Cluster Beacon without triggering a full beacon rebuild.

**MAC-BCN-004** — The Beacon Manager SHALL implement beacon reception and parsing when operating in PT mode, extracting: Network ID, operating channel, RACH resource parameters, route cost, and all recognized IEs.

**MAC-BCN-005** — The Beacon Manager SHALL implement the Network Beacon scanning procedure (clause 5.1.4) and SHALL expose a scan results interface delivering sorted lists of candidate FTs (by RSSI-2 quality and route cost) to the Association Manager.

**MAC-BCN-006** — The Beacon Manager SHALL support broadcast and multicast data transmission (non-beacon) on PCH/BCH transport channel as defined in the channel mapping table.

**MAC-BCN-007** — Beacon transmission intervals SHALL be configurable at runtime via the module API without requiring re-association.

**MAC-BCN-008** — Beacon generation and transmission (MAC-BCN-002, MAC-BCN-003, MAC-BCN-006) SHALL be conditionally compiled under `CONFIG_OPENER_FT`. Beacon reception and scanning (MAC-BCN-004, MAC-BCN-005) SHALL be conditionally compiled under `CONFIG_OPENER_PT`.

**MAC-BCN-009** — In device+host deployment, the Beacon and Broadcast Manager is a candidate for host execution (REQUIRED ON HOST when `CONFIG_OPENER_MODULE_BEACON_MANAGER=n`). When hosted remotely, beacon content SHALL be delivered to the on-device scheduler via RPC in time to meet the transmission window; the RPC latency budget SHALL be documented and validated against the beacon period.

---

### 4.6 HARQ Engine (MAC-HRQ)

**MAC-HRQ-001** — The HARQ Engine SHALL implement Hybrid ARQ with incremental redundancy and soft-combining as defined in ETSI TS 103 636-4 clause 5.5.

**MAC-HRQ-002** — The HARQ Engine SHALL support the Physical Header Field feedback encoding/decoding as defined in clause 6.2.2 (Format 0, Format 1, Format 2, Format 3, Format 4, Format 5, Format 6).

**MAC-HRQ-003** — The HARQ Engine SHALL manage a configurable number of HARQ processes per link (up to the maximum defined in Annex A of TS 103 636-4). Each HARQ process SHALL be independently tracked (sequence number, soft buffer, process state).

**MAC-HRQ-004** — The HARQ Engine SHALL deliver transmission status (success/failure) to the DLC layer via a status callback after each HARQ process completes (either ACK received or max retransmissions exhausted).

**MAC-HRQ-005** — The HARQ feedback delay (number of subslots between reception and feedback transmission) SHALL be configurable as a RD capability, as defined in TS 103 636-4 Annex A.2.6.

**MAC-HRQ-006** — HARQ soft buffers SHALL be allocated from a statically defined memory pool sized at compile time via Kconfig.

---

### 4.7 Random Access Controller (MAC-RAC)

**MAC-RAC-001** — The Random Access Controller SHALL implement the random access procedure defined in ETSI TS 103 636-4 clause 5.3 including: RACH resource announcement (FT side), RACH transmission with LBT and exponential backoff (PT side), and RACH response handling.

**MAC-RAC-002** — The Random Access Controller SHALL implement the configurable parameters: `RACH subslots`, `Repetition`, `RACH Period`, `Maximum Random Access TX time`, `Response Window`, `CW_MIN`, `CW_MAX`.

**MAC-RAC-003** — The Random Access Controller SHALL implement the Listen Before Talk procedure for RACH transmissions using the `rachBackOff` timer as specified in clause 5.3.1.

**MAC-RAC-004** — The Random Access Controller SHALL support RACH resource announcement via unicast signalling (in addition to beacon) for mesh system operation, as specified in clause 5.3.2.

---

### 4.8 Connection Configuration Controller (MAC-CCC)

**MAC-CCC-001** — The Connection Configuration Controller SHALL implement the Reconfiguration procedure defined in ETSI TS 103 636-4 clause 5.10, including Reconfiguration Request and Response message exchange.

**MAC-CCC-002** — The CCC SHALL manage per-link configuration: active logical channel mapping, MCS selection, HARQ process count, multiplexing header configuration, and security mode.

**MAC-CCC-003** — The CCC SHALL implement the multiplexing and assembly procedure defined in clause 5.6, packing multiple logical channel SDUs into a single MAC PDU according to the multiplexing header format defined in clause 6.3.4.

**MAC-CCC-004** — The CCC SHALL implement Group Assignment procedures as defined in clause 5.11, allocating shared resources and distributing Group Assignment IEs.

---

### 4.9 MAC Security Engine (MAC-SEC)

**MAC-SEC-001** — The MAC Security Engine SHALL implement Security Mode 1 as defined in ETSI TS 103 636-4 clause 5.9.1.

**MAC-SEC-002** — Integrity protection SHALL use AES-CMAC (per NIST SP 800-38B) producing a 64-bit MAC truncated to 32 bits, carried in the MAC PDU as MIC. The input to CMAC SHALL include the MAC PDU header and payload.

**MAC-SEC-003** — Ciphering SHALL use AES in CTR mode (per FIPS PUB 197). The initialization vector SHALL be constructed from the Hyper Packet Counter (HPC), the Short RD ID, and the Network ID as specified in TS 103 636-4.

**MAC-SEC-004** — The MAC Security Engine SHALL maintain and synchronize the Hyper Packet Counter (HPC) per link. HPC overflow SHALL be handled gracefully and SHALL trigger a re-keying notification to upper layers.

**MAC-SEC-005** — The MAC Security Engine SHALL expose a key management interface (`struct opener_sec_key_ops`) for: key installation, key revocation, HPC initialization, and HPC synchronization. Key material SHALL NOT be exposed outside the security engine.

**MAC-SEC-006** — The on-device security engine SHALL use the Zephyr crypto subsystem API (`<zephyr/crypto/crypto.h>`) as the backend, enabling hardware-accelerated AES on supported platforms.

**MAC-SEC-007** — The MAC Security Engine SHALL be deployable on either the device or the host, controlled by `CONFIG_OPENER_MODULE_MAC_SECURITY`. When disabled on device (REQUIRED ON HOST), the RPC transport SHALL carry pre-encrypted MAC PDU payloads and pre-computed MIC values. The interface between the scheduler and the security engine SHALL be designed so that this split is transparent to the scheduler.

**MAC-SEC-008** — Key derivation and key provisioning are outside the scope of the MAC Security Engine. The engine SHALL accept fully derived session keys via the `struct opener_sec_key_ops` interface and SHALL NOT implement any key exchange protocol.

---

### 4.10 Mobility Manager (MAC-MOB)

**MAC-MOB-001** — The Mobility Manager SHALL implement intra-cell and inter-cell handover procedures as defined in ETSI TS 103 636-4 clause 5.7.

**MAC-MOB-002** — The Mobility Manager SHALL trigger handover evaluation based on: RSSI-2 measurements from beacon scanning, current link BLER reported by the HARQ Engine, and channel occupancy reported by the Spectrum Manager.

**MAC-MOB-003** — The Mobility Manager SHALL support seamless handover: resources on the target FT SHALL be allocated and confirmed before resources on the source FT are released.

**MAC-MOB-004** — The Mobility Manager SHALL expose a handover event interface (`struct opener_mob_event_ops`) reporting: handover initiated, handover completed (with new FT identity), handover failed.

**MAC-MOB-005** — The Mobility Manager SHALL be conditionally compiled under `CONFIG_OPENER_PT`, as handover is a PT-side function. In device+host deployment it MAY be REQUIRED ON HOST.

---

### 4.11 Paging Controller (MAC-PAG)

**MAC-PAG-001** — The Paging Controller SHALL implement paging transmission (FT side) and paging reception (PT side) as defined in ETSI TS 103 636-4 clause 5.12.

**MAC-PAG-002** — The Paging Controller SHALL allow an upper layer or host to request paging of a specific RD (identified by Long RD ID) by enqueuing a paging message on PCH/BCH.

**MAC-PAG-003** — The Paging Controller on the PT side SHALL wake the device from a low-power listening state upon paging reception and SHALL notify the Association Manager.

**MAC-PAG-004** — Paging transmission (FT side, MAC-PAG-002) SHALL be conditionally compiled under `CONFIG_OPENER_FT`. Paging reception (PT side, MAC-PAG-003) SHALL be conditionally compiled under `CONFIG_OPENER_PT`. In device+host deployment the Paging Controller MAY be REQUIRED ON HOST.

---

### 4.12 MAC Multiplexer/Demultiplexer (MAC-MUX)

**MAC-MUX-001** — The MAC Multiplexer SHALL implement the MAC multiplexing header format defined in ETSI TS 103 636-4 clause 6.3.4, supporting up to 4 user plane data flows and 2 signalling flows per link.

**MAC-MUX-002** — The MAC Multiplexer SHALL perform logical channel prioritization (UL and DL) as defined in the MAC structure overview.

**MAC-MUX-003** — The MAC Demultiplexer SHALL parse incoming MAC PDUs, extract MAC SDUs per flow, and deliver them to the correct DLC entity set based on flow ID.

---

### 4.13 MAC Public API (MAC-API)

**MAC-API-001** — The MAC layer SHALL expose a single header (`opener_mac.h`) defining:
- `struct opener_mac_config`: compile-time and runtime configuration parameters
- `struct opener_mac_ops`: operations the DLC layer calls into the MAC layer (data transfer, resource request, configuration)
- `struct opener_mac_callbacks`: callbacks the MAC layer delivers to the DLC layer (data received, tx status, link event)
- `opener_mac_init()`: initialize the MAC layer with a config and HAL ops struct
- `opener_mac_module_register()`: register a module implementation
- `opener_mac_module_deregister()`: deregister and optionally replace a module
- `opener_mac_scheduler_set_policy()`: install or remove a scheduler policy
- `opener_mac_get_diagnostics()`: retrieve a snapshot of all module diagnostics

**MAC-API-002** — All MAC API functions SHALL be thread-safe. Internal locking SHALL use Zephyr mutexes or spinlocks as appropriate to the execution context.

**MAC-API-003** — MAC API functions called from the DLC layer SHALL NOT block for more than a configurable timeout. If the operation cannot complete within the timeout, they SHALL return an error code and leave state unchanged.

**MAC-API-004** — All public MAC structures and enumerations SHALL be defined in a single versioned header. The header SHALL define a `OPENER_MAC_API_VERSION` constant. Changes that break binary compatibility SHALL increment the major version.

---

## 5. DLC Layer Requirements (DLC)

> _Detailed DLC requirements will be defined in a subsequent document iteration._

**DLC-001** — The DLC layer SHALL implement all four service types (DLC-T, DLC-S, DLC-A, DLC-SA) as defined in ETSI TS 103 636-5 clause 4.3.1.

**DLC-002** — The DLC layer SHALL implement the DLC Routing Service including: backend uplink routing, backend downlink routing, RD-to-RD routing, hop-limited flooding, and selective source routing, as defined in ETSI TS 103 636-5 clause 5.2.8.

**DLC-003** — The DLC layer architecture SHALL follow the same modular design principles as the MAC layer: pluggable implementations behind defined interfaces, an RPC wrapper per module, and a module registry.

**DLC-004** — In device+host deployment, the DLC layer MAY be implemented fully or partially on the host. Each DLC module MAY be individually disabled on the device (`CONFIG_OPENER_MODULE_DLC_<NAME>=n`) and REQUIRED ON HOST. The DLC Routing Service is a primary candidate for host execution when the host manages a large number of associations or routing table entries that exceed device RAM.

**DLC-005** — When the DLC layer is fully implemented on the host, the on-device MAC layer SHALL expose its data plane interface (`struct opener_mac_ops`) directly to the RPC transport, with no DLC processing occurring on the device.

---

## 6. Convergence Layer Requirements (CVG)

> _Detailed CVG requirements will be defined in a subsequent document iteration._

**CVG-001** — The Convergence Layer SHALL implement all five CVG service types (0–4) as defined in ETSI TS 103 636-5 clause 6.2.2.

**CVG-002** — The Convergence Layer SHALL implement endpoint multiplexing (EP Mux and Mux Tag) as defined in clause 6.2.4.

**CVG-003** — The Convergence Layer SHALL implement CVG-level security (AES-CTR ciphering + AES-CMAC integrity), independently from MAC-level security, operating end-to-end as specified in clause 6.2.13.

**CVG-004** — The Convergence Layer architecture SHALL follow the same modular design principles as the MAC and DLC layers.

**CVG-005** — In device+host deployment, the CVG layer MAY be implemented fully or partially on the host. Each CVG service (multiplexing, transmission, security) MAY be individually disabled on the device (`CONFIG_OPENER_MODULE_CVG_<NAME>=n`) and REQUIRED ON HOST. Given that CVG operates end-to-end and intermediate mesh nodes do not process CVG payload, host-side CVG implementation is the natural deployment for cloud-connected backends.

**CVG-006** — When both the DLC and CVG layers are fully implemented on the host, the device firmware acts as a pure MAC+PHY radio modem accessible entirely through the RPC transport.

---

## 7. Coding Standard Conventions (CODE)

**CODE-001** — Source files SHALL comply with the Zephyr coding style: 8-space indentation expressed as tabs, K&R brace style, `snake_case` for all identifiers, `ALL_CAPS` for macros and Kconfig symbols.

**CODE-002** — All MISRA C:2012 mandatory rules SHALL be followed without deviation.

**CODE-003** — MISRA C:2012 required rules MAY be deviated where technically justified. Each deviation SHALL be recorded in a `misra_deviations.md` file co-located with the relevant module, containing: rule identifier, file and line range, justification, and compensating measure.

**CODE-004** — The following MISRA C:2012 advisory rules SHALL be treated as required for this project: Rule 15.5 (single exit point), Rule 17.7 (return value of non-void functions used), Rule 20.7 (macro parameter parenthesization).

**CODE-005** — All integers used in protocol field encoding/decoding SHALL be of explicitly sized types (`uint8_t`, `uint16_t`, `uint32_t`, `int32_t`, etc.). Plain `int`, `long`, or `unsigned` types are prohibited in protocol data structures and PDU handling code.

**CODE-006** — Pointer arithmetic SHALL NOT be used outside of the serialization layer and the HAL. All other array traversal SHALL use indexed access.

**CODE-007** — All `switch` statements on an enumeration type SHALL have a `default` case. All `switch` statements on protocol field values SHALL explicitly handle every defined value; unrecognized values SHALL be handled in the `default` case with a logged warning and a defined fallback action.

**CODE-008** — Functions MAY NOT exceed 100 lines (excluding blank lines and comments). Functions exceeding this limit SHOULD be refactored into smaller units; exceptions require a brief inline comment justifying why the length is warranted.

---

## 8. Module Interface Conventions (CONV)

**CONV-001** — Every module interface SHALL be expressed as a C struct of function pointers. The struct name SHALL follow the pattern `struct opener_<module>_ops`.

**CONV-002** — Every function pointer in a module ops struct SHALL have its first argument be a `void *ctx` pointer to module-private context. This enables multiple instances of the same module (e.g., one Association Manager per operating mode).

**CONV-003** — Return values SHALL use a signed integer error code: 0 for success, negative errno-compatible values for errors. Module-specific error codes SHALL be defined in a dedicated header.

**CONV-004** — Every module ops struct SHALL include a `uint32_t version` field. Version checking SHALL be performed at registration time. An incompatible version SHALL cause registration to fail with `-EINVAL`.

**CONV-005** — Asynchronous event delivery from a module to its consumers SHALL use callback structs (`struct opener_<module>_event_ops`) registered separately from the ops struct, allowing multiple consumers to subscribe to the same module's events.

**CONV-006** — Module diagnostics SHALL be exposed as a `struct opener_<module>_diag` containing only fixed-width integer counters and status fields. No pointers or dynamic structures.

---

## 9. Open Questions

The following items are unresolved and require further investigation or stakeholder decision before the corresponding implementation requirements can be written.

Items prefixed **OQ-UC** are elevated from [use-cases.md](use-cases.md); items prefixed **OQ** are internal to the implementation design.

**OQ-UC-01** — **Group addressing API**: DECT-2020 NR MAC supports group short radio device identities. The stack must decide whether group management (creation, membership, deletion) is part of the Association Manager API or a separate module. The maximum supported group size must be specified.

**OQ-UC-02** — **Random access channel (RACH) configuration**: the number of RACH slots per superframe and the back-off algorithm for contention-based uplink (used by alert events UC-SC-005 and mid-show receiver joins UC-AU-004) must be specified. This affects the MAC scheduler design and the UC-SC-005 and UC-AU-004 latency budgets.

**OQ-UC-03** — **Audio stream throughput and latency feasibility**: the 512 kbps (64 kB/s) group downlink target (UC-AU-001) and the ≤ 10 ms per-frame transmission deadline (UC-AU-002) must be validated jointly against the maximum MAC PDU size at the chosen MCS and the TDMA frame structure. If the combination is unachievable in a single superframe, a reduced-quality codec profile or multi-superframe scheduling must be specified.

**OQ-UC-04** — **Receiver detection and group admission without full association**: DECT-2020 NR random-access frames carry the PT short ID. Admitting a probing receiver into an ongoing audio group (UC-AU-005) without completing a full association — whether via a lightweight group-join procedure or a vendor-specific random-access response extension — is not defined by the standard and must be specified before the Association Manager API can be finalised.

**OQ-UC-05** — **Zone handover imperceptibility**: ETSI TS 103 636 defines intra-cluster handover procedures. The standard's handover timer values must be validated against the ≤ 10 ms service gap budget of UC-AU-006. If the minimum handover time exceeds one audio frame period, a pre-emptive handover mechanism — initiated before the PT leaves coverage of the source FT and concealed by the PT-side audio buffer — must be specified before the Association Manager handover requirements can be written.

**OQ-UC-06** — **Session-scoped resource allocation API**: the MAC scheduler exposes `schedule_tx` / `schedule_rx` for individual frame requests. UC-OTA-001/002 require explicit session open/close semantics so that additional slots are allocated only for the duration of an OTA transfer and reclaimed immediately on completion. A session concept with a distinct handle and lifecycle (priority inheritance, pre-emption ceiling, automatic reclaim on timeout) is not yet represented in the scheduler API; its design must be specified before MAC-SCH-004 can be considered complete.

**OQ-UC-07** — **Multicast ARQ for group OTA delivery**: DECT-2020 NR does not define per-PT negative acknowledgement for group-addressed MAC frames. Reliable multicast OTA (UC-OTA-004) therefore requires an ARQ mechanism above the MAC. The most practical implementation is at the DLC layer: per-PT NACK bitmask piggybacked on a dedicated uplink slot, driving DLC-SA selective retransmission. This interaction between the group addressing mechanism and the DLC-SA layer must be specified before DLC-001 can be considered complete.

**OQ-07** — **Maximum SPI frame payload size**: the current wire protocol fixes the frame size at 512 bytes. This limits a single RPC call payload to 502 bytes. MAC PDUs may exceed this; fragmentation across multiple RPC frames may be needed. The fragmentation mechanism, if any, must be specified before RPC-006 can be considered final.

**OQ-08** — **DLC routing host vs. device split**: when the DLC Routing Service is partially on the host (DLC-004), the boundary between on-device and host-side routing logic must be precisely defined. In particular, the routing decision for downlink frames arriving at the device from the PHY must be specified.

**OQ-09** — **PHY direct access fallback behaviour**: when `CONFIG_OPENER_DIRECT_PHY_NRF91X1` is enabled and the direct call fails (e.g., modem in error state), PLT-001a does not specify a fallback path. Whether the stack should silently retry via `struct opener_hal_ops` or propagate the error must be decided.

**OQ-10** — **GPIO pin configuration via Devicetree**: the IRQ GPIO used by the SPI RPC transport (RPC-008) should be configured via Zephyr Devicetree rather than a Kconfig integer. The Devicetree binding for the RPC node needs to be defined.
