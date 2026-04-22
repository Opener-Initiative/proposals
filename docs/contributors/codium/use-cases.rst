.. _codium-use-cases-1:

opener — Use-Case Requirements
==============================

| **Document version**: 0.1
| **Status**: Draft
| **Relation to implementation requirements**: This document defines system-level, application-perspective requirements derived from concrete deployment scenarios. Each requirement traces to one or more implementation requirements in :ref:`codium-requirements-1`. The implementation requirements document references this document as a primary source of design drivers.

--------------

Purpose
-------

This document characterises two target deployment scenarios and derives the system-level requirements that each imposes on the DECT-2020 NR protocol stack. Requirements here are stated at the service level — what the deployed system must do — rather than at the protocol or implementation level. They are the primary source for verifying that implementation choices in :ref:`codium-requirements-1` serve real application needs.

--------------

Requirement notation
--------------------

-  **SHALL**: the deployed system cannot meet the use case without this property.
-  **SHOULD**: strongly recommended; absence must be justified.
-  **MAY**: optional enhancement.

Identifiers follow the scheme ``UC-[DOMAIN]-[NNN]``, where ``DOMAIN`` is ``SC`` (Smart City), ``AU`` (Concert Audio), or ``OTA`` (OTA Firmware Update).

--------------

1. Smart City — Street Infrastructure
-------------------------------------

1.1 Scenario description
~~~~~~~~~~~~~~~~~~~~~~~~

A public-lighting and utility-monitoring deployment covers a city street segment. A single gateway node acts as FT. Between 32 and 256 field devices (PTs) are distributed along the street:

-  **Street-light controllers**: embedded in lighting poles, one per lamp or one per cluster of lamps. Receive group ON/OFF/dim commands; report energy consumption and fault status periodically.
-  **Utility meters**: water meters, electricity meters. Report readings on a low-frequency schedule (every 1–60 minutes). Payloads are small (≤ 64 bytes per reading).
-  **Cabinet monitors**: detect physical events such as the opening of a telecom distribution cabinet or a tamper alert on a meter enclosure. Generate rare, high-priority, reliable event messages.

All field devices are mains-powered or have a primary battery with a multi-year service life requirement. Radio duty cycles must be low for battery-powered nodes.

1.2 Deployment topology
~~~~~~~~~~~~~~~~~~~~~~~

::

                               ┌──────────────────────────────────┐
                               │  Gateway / FT                     │
                               │  (device+host or device-only)     │
                               │  – Association Manager            │
                               │  – Group scheduler                │
                               │  – Metering data collector        │
                               │  – Alert event handler            │
                               └──────────┬───────────────────────┘
                                          │ DECT-2020 NR
                  ┌───────────────────────┼───────────────────────┐
                  │                       │                       │
       ┌──────────┴────────┐  ┌───────────┴──────────┐  ┌────────┴──────────┐
       │  Light cluster PT  │  │  Utility meter PT     │  │  Cabinet mon. PT  │
       │  (FT+PT capable)   │  │  (PT, low power)      │  │  (PT, event-driven│
       │  group member      │  │  scheduled uplink     │  │  random access)   │
       └───────────────────┘  └──────────────────────┘  └───────────────────┘

The gateway may itself be connected to a backend system (cloud or local controller) via Ethernet or cellular. In a device+host deployment the Association Manager and data aggregation logic run on the host processor; the radio MCU handles MAC/PHY timing. In a device-only deployment the gateway runs the full stack on the nRF91x1.

1.3 Traffic profiles
~~~~~~~~~~~~~~~~~~~~

+---------------------+--------------------------+----------------------------------+-----------+----------------------------------------------------+--------------------------------------------------------+
| Service             | Direction                | Periodicity                      | Payload   | Latency                                            | Reliability                                            |
+=====================+==========================+==================================+===========+====================================================+========================================================+
| Meter reading       | PT → FT uplink           | 1 min – 60 min                   | ≤ 64 B    | Insensitive (≤ 30 s end-to-end acceptable)         | Confirmed delivery (DLC ARQ or app-level retry)        |
+---------------------+--------------------------+----------------------------------+-----------+----------------------------------------------------+--------------------------------------------------------+
| Light group command | FT → PT downlink (group) | Event-driven (operator action)   | ≤ 16 B    | ≤ 500 ms from command issue to all lights actuated | Best-effort group frame + optional per-PT confirmation |
+---------------------+--------------------------+----------------------------------+-----------+----------------------------------------------------+--------------------------------------------------------+
| Cabinet open alert  | PT → FT uplink           | Rare event (< 1/hour per device) | ≤ 32 B    | ≤ 2 s from event to FT reception                   | Confirmed delivery, at-least-once                      |
+---------------------+--------------------------+----------------------------------+-----------+----------------------------------------------------+--------------------------------------------------------+
| Fault / status      | PT → FT uplink           | 15 min – 1 h                     | ≤ 128 B   | Insensitive                                        | Best-effort                                            |
+---------------------+--------------------------+----------------------------------+-----------+----------------------------------------------------+--------------------------------------------------------+

1.4 DECT-2020 NR features exercised
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  **FT coordination**: beacon, association management for up to 256 PTs.
-  **Group addressing**: MAC-layer group frame directed at a defined set of light-controller PTs.
-  **Scheduled uplink**: metering PTs negotiate a periodic transmission slot at association time; radio is off between slots.
-  **Random access uplink**: cabinet-monitor PTs use a contention-based random access burst for event alerts; no prior slot reservation.
-  **Low-power beacon wake-up**: battery PTs sleep and wake on beacon to check for pending downlink.
-  **Concurrent service classes**: the scheduler must interleave group downlink commands (high priority, low latency) with bulk scheduled metering uplinks (low priority, high volume) without starvation.

1.5 Requirements
~~~~~~~~~~~~~~~~

**UC-SC-001** — The FT node SHALL support simultaneous associations with at least 64 PTs in device-only deployment and at least 256 PTs in device+host deployment.

   Rationale: street segments may carry 50–200 field devices; the gateway must not require segmentation within a single street.

**UC-SC-002** — The stack SHALL support MAC-layer group addressing, allowing the FT to transmit a single frame received and acted upon by all members of a pre-configured PT group.

   Rationale: lighting ON/OFF commands must reach an entire pole cluster in a single radio transaction, not a sequential unicast loop that would introduce per-lamp latency.

**UC-SC-003** — A group downlink command issued by the FT SHALL be received and processed by all group-member PTs within 500 ms under normal channel conditions (no interference, no severe multipath), with at least 99 % of commands meeting this bound over 1000 trials.

**UC-SC-004** — The system SHALL support periodic scheduled uplink for PTs with configurable periods from 1 minute to 60 minutes. The FT scheduler SHALL allocate a transmission slot at association time and maintain it without renegotiation for the lifetime of the association.

**UC-SC-005** — The stack SHALL support random-access uplink for event-driven PTs that do not maintain a pre-allocated slot. A cabinet-monitor PT SHALL be able to deliver an alert frame to the FT within 2 seconds of the triggering event, with at least 99.9 % reliability over 10 000 trials under normal channel load (< 50 % channel utilisation).

**UC-SC-006** — Battery-powered PTs SHALL be able to operate with a radio duty cycle of ≤ 0.1 % during idle periods (no pending uplink or downlink) while remaining reachable by the FT within one beacon interval.

**UC-SC-007** — The MAC scheduler SHALL support concurrent operation of the following service classes without starvation of any class: - High-priority group downlink (lights, pre-empts low-priority traffic) - Confirmed event uplink (alerts, admitted ahead of metering) - Scheduled periodic uplink (metering, background)

**UC-SC-008** — In device+host deployment, the Association Manager running on the host SHALL be able to scale to 256 concurrent PT associations without requiring changes to the on-device firmware or its static memory allocation.

**UC-SC-009** — The system SHALL provide an API for the application to define PT groups and modify group membership at runtime without disrupting associations of unaffected PTs.

--------------

2. Concert Audio Distribution — Wireless In-Ear Monitoring and Stage Management
-------------------------------------------------------------------------------

.. _scenario-description-1:

2.1 Scenario description
~~~~~~~~~~~~~~~~~~~~~~~~

A live event venue deploys DECT-2020 NR for wireless audio distribution. An audio distribution hub — connected to the mixing desk via SPI RPC in device+host deployment — acts as the primary FT and broadcasts one or more audio streams as a continuous group downlink. The system serves two sub-use-cases that are active simultaneously during a show.

**2.1.1 Multi-receiver audio broadcast**

The mixing desk produces one or more audio mixes (main IEM mix, monitor mix, front-of-house fill, hearing-assistance stream). Each mix is broadcast continuously to its associated receiver group. Receivers are:

-  **Performer IEM beltpacks**: worn by musicians and crew; mobile across stage and backstage. Receive the personal audio mix; report RSSI, buffer fill level, and battery status uplink every few seconds.
-  **Stage monitor processors**: fixed receivers driving floor wedges or side-fill cabinets. Receive a fixed mix; send health metrics uplink.
-  **Audience hearing-assistance receivers**: distributed among audience members. May join or leave mid-show using random access without pre-configuration. Do not send uplink data.

The audio stream is the primary service. Any frame loss that exceeds roughly 0.1 % produces an audible artefact; latency above 10 ms (FT-to-PT one-way radio hop) is perceptible by performers on a live stage.

**2.1.2 Roaming performer / zone handover**

For large stages and outdoor festivals, multiple zone FTs cover different areas (stage left, stage right, pit, backstage). A performer wearing an IEM beltpack moves continuously between zones. Handover must complete within one audio frame period to remain imperceptible. PT-side buffering bridges momentary null spots (e.g. a performer stepping behind a metallic lighting rig or into a shielded backstage corridor).

.. _deployment-topology-1:

2.2 Deployment topology
~~~~~~~~~~~~~~~~~~~~~~~

::

     Mixing desk ──SPI RPC──► Audio distribution FT (device+host)
                                             │
                  ┌──────────────────────────┼──────────────────────────┐
                  │                          │                          │
           ┌──────┴───────┐           ┌──────┴───────┐          ┌───────┴──────┐
           │  Zone FT A   │           │  Zone FT B   │          │  FOH / Aud.  │
           │ (stage left) │           │(stage right) │          │     FT       │
           └──┬───────┬───┘           └──┬───────┬───┘          └──────┬───────┘
              │       │                  │       │                     │
          IEM PT  Monitor PT         IEM PT  Monitor PT        Hearing asst. PT
         (mobile,  (fixed,          (mobile,  (fixed,          (random access,
         performer) stage wedge)   performer) side-fill)        no uplink)

The mixing desk is the sole audio source. Zone FTs relay or independently transmit the stream depending on deployment topology. In a device-only deployment, a single FT covers the whole venue and the audio source is co-located on the device.

.. _traffic-profiles-1:

2.3 Traffic profiles
~~~~~~~~~~~~~~~~~~~~

+--------------------------+---------------------------+-------------------------------------+--------------------+-------------------------------------------+--------------------------------------------+
| Service                  | Direction                 | Periodicity                         | Payload            | Latency                                   | Reliability                                |
+==========================+===========================+=====================================+====================+===========================================+============================================+
| Audio stream             | FT → PT group (broadcast) | Continuous; one frame every 5–10 ms | 64–256 B per frame | ≤ 10 ms FT-to-PT one-way radio hop        | < 0.1 % frame loss (perceptible threshold) |
+--------------------------+---------------------------+-------------------------------------+--------------------+-------------------------------------------+--------------------------------------------+
| Receiver quality metrics | PT → FT uplink            | Every 500 ms – 5 s per receiver     | ≤ 32 B             | ≤ 10 s end-to-end                         | Best-effort                                |
+--------------------------+---------------------------+-------------------------------------+--------------------+-------------------------------------------+--------------------------------------------+
| Receiver join (mid-show) | PT → FT (random access)   | On connection event                 | ≤ 48 B             | ≤ 1 s from first probe to audio reception | Confirmed                                  |
+--------------------------+---------------------------+-------------------------------------+--------------------+-------------------------------------------+--------------------------------------------+
| Zone handover            | PT ↔ FT                   | At zone boundary                    | Control only       | ≤ one audio frame period (≤ 10 ms)        | Mandatory                                  |
+--------------------------+---------------------------+-------------------------------------+--------------------+-------------------------------------------+--------------------------------------------+

.. _dect-2020-nr-features-exercised-1:

2.4 DECT-2020 NR features exercised
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  **Sustained high-throughput group downlink**: the audio stream must be delivered continuously to all associated receivers in a group with deterministic per-frame timing; throughput and latency requirements are tighter than typical IoT telemetry.
-  **Deterministic real-time scheduling**: a late audio frame is nearly as harmful as a lost one; the scheduler must guarantee per-frame transmission deadlines, not just average throughput.
-  **Concurrent scheduled uplink and random access**: quality metrics arrive on scheduled uplink slots; audience hearing-assistance receivers and late-joining beltpacks join via random access without interrupting the ongoing stream.
-  **Detection without full association**: a hearing-assistance receiver probing the FT can be identified and accepted for the stream without completing a full association exchange.
-  **Mobility / zone handover**: a performer’s IEM beltpack must transition between zone FTs within one audio frame period; any longer gap produces an audible dropout.
-  **PT-side buffering**: a small receiver-side audio buffer bridges momentary radio null spots (behind metallic structures, in shielded corridors) without an audible break.
-  **Strict priority scheduling**: the audio downlink stream pre-empts all uplink traffic; no quality-metrics uplink may delay a scheduled audio frame transmission.
-  **Low-latency host-injected downlink**: the mixing desk injects audio frames into the radio scheduler via the RPC transport; the injection-to-transmission latency is a direct component of the end-to-end audio latency budget.

.. _requirements-1:

2.5 Requirements
~~~~~~~~~~~~~~~~

**UC-AU-001** — The stack SHALL support scheduled group downlink transmissions sufficient to carry a continuous compressed audio stream to all associated receivers. The sustained application-layer group downlink throughput SHALL be at least 512 kbps (64 kB/s).

   Rationale: a single low-latency stereo audio stream (e.g. LC3plus or aptX LL) at broadcast quality requires approximately 256–512 kbps; headroom is required for MAC framing, ARQ overhead, and simultaneous streams to different receiver groups.

**UC-AU-002** — The Radio Scheduler SHALL support reserved-slot, deterministic scheduling for the audio group downlink stream. Each scheduled audio frame SHALL be transmitted within one audio frame period (≤ 10 ms) of its nominal transmission time under normal channel conditions, with at least 99.9 % of frames meeting this bound.

**UC-AU-003** — The stack SHALL support simultaneous operation of scheduled uplink (quality metrics from associated receivers) and random-access uplink (new or rejoining receivers) on the same FT node, without either mode delaying a scheduled audio downlink frame.

**UC-AU-004** — A receiver joining mid-show via random access SHALL begin receiving the audio stream within 1 second of its first random-access probe, with at least 99 % reliability under normal channel load (< 50 % channel utilisation from the existing audio stream and metrics traffic).

**UC-AU-005** — The FT SHALL be able to detect a previously unassociated receiver from its random-access probe, extract its identity, and admit it to the audio group without completing a full association procedure.

   Rationale: audience hearing-assistance receivers may probe the FT to determine whether a compatible stream is available before deciding to associate. The FT must respond with group membership information without forcing a full association on every probe.

**UC-AU-006** — The stack SHALL support zone handover: a performer’s IEM beltpack PT associated with zone FT A SHALL be able to transition to zone FT B with a service gap of at most one audio frame period (≤ 10 ms) measured from last audio frame received from FT A to first audio frame received from FT B. Quality-metrics reporting continuity SHALL be restored within 500 ms of handover completion.

**UC-AU-007** — When a receiver temporarily enters a radio null spot (behind metallic structures, in a shielded corridor), the PT-side audio buffer SHALL sustain uninterrupted audio playback for at least 200 ms, and buffered metrics SHALL be delivered to the FT once radio contact is restored.

**UC-AU-008** — The MAC scheduler SHALL give the audio group downlink stream strict priority over all uplink traffic. No quality-metrics uplink frame, random-access probe, or control message SHALL delay or pre-empt a scheduled audio downlink transmission.

**UC-AU-009** — In device+host deployment, the host processor (mixing desk or audio interface) SHALL be able to inject audio frame data into the downlink group scheduler through the RPC transport with a latency from write call to first radio frame transmission of at most 5 ms.

   Rationale: the RPC injection latency is a direct additive component of the end-to-end audio latency. A 5 ms budget leaves room for the audio processing chain (A/D conversion, codec encode, packetisation) within a total end-to-end target of 15–20 ms.

--------------

3. OTA Firmware Update
----------------------

.. _scenario-description-2:

3.1 Scenario description
~~~~~~~~~~~~~~~~~~~~~~~~

An operator deploys a firmware update to one or more field devices (PTs). Image sizes range from 100 KB to 2 MB. The transfer is operator-triggered, not periodic. Radio resources must be allocated on-demand for the duration of the transfer only, then released so that normal channel capacity is fully restored for periodic metering, alerts, and real-time commands.

The update may target a **single device** (unicast: staged rollout, device-specific patch) or a **group of devices** (multicast: all light controllers of the same type in a street segment, all trackers sharing a firmware revision). In the multicast case each PT must be able to NACK and receive retransmissions for the specific blocks it missed, independently of other group members.

This use case cuts across the two deployment scenarios above: the same OTA mechanism serves Smart City field devices and concert audio receivers (IEM beltpacks, stage monitor processors).

.. _deployment-topology-2:

3.2 Deployment topology
~~~~~~~~~~~~~~~~~~~~~~~

::

     Backend / Operator console
           │  trigger OTA session
           ▼
     Gateway / FT
     ┌───────────────────────────────────┐
     │  OTA session: allocate burst      │
     │  downlink slots for duration ─────┼──────────────────────────┐
     │                                   │                          │
     │  Scheduler policy: OTA > metering │        PT-1              │  PT-2  ...  PT-N
     │  OTA < alert / group command      │  ┌────────────────┐  ┌───┴──────────────┐
     │                                   │  │ OTA client /   │  │ OTA client /     │
     │  On completion / abort:           │  │ bootloader     │  │ bootloader       │
     │    release slots ─────────────────┼─►│ DLC-SA ARQ     │  │ DLC-SA ARQ       │
     │    remove OTA scheduler policy    │  │ flash write    │  │ independent NACK │
     │    resume normal allocation       │  │ image CRC ✓    │  │ image CRC ✓      │
     └───────────────────────────────────┘  └────────────────┘  └──────────────────┘

3.3 Traffic profile
~~~~~~~~~~~~~~~~~~~

+-------------------------------------+----------------------------+--------------------+---------------------+---------------------------------------+------------------------------------------------+
| Service                             | Direction                  | Duration           | Payload             | Required throughput                   | Reliability                                    |
+=====================================+============================+====================+=====================+=======================================+================================================+
| Firmware block stream               | FT → PT (unicast or group) | Seconds to minutes | 100 KB – 2 MB total | ≥ 10 kB/s application-layer sustained | 100 % (full image, CRC verified before reboot) |
+-------------------------------------+----------------------------+--------------------+---------------------+---------------------------------------+------------------------------------------------+
| Block NACK / retransmission request | PT → FT uplink             | Per missing block  | ≤ 32 B              | N/A                                   | Best-effort (loss triggers retransmit)         |
+-------------------------------------+----------------------------+--------------------+---------------------+---------------------------------------+------------------------------------------------+
| Session open / close                | FT ↔ PT                    | Session boundary   | ≤ 32 B              | N/A                                   | Confirmed                                      |
+-------------------------------------+----------------------------+--------------------+---------------------+---------------------------------------+------------------------------------------------+

.. _dect-2020-nr-features-exercised-2:

3.4 DECT-2020 NR features exercised
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  **Session-scoped on-demand resource allocation**: the scheduler allocates a burst of downlink slots at session open and releases them at session close. Unlike the permanent slot reserved for metering (UC-SC-004), the OTA allocation has an explicit lifetime.
-  **Dynamic scheduler policy**: a temporary high-priority OTA policy is installed for the session duration, pre-empting background metering traffic but yielding to alert events and real-time group commands. The policy is removed on session end.
-  **DLC-SA (segmentation + ARQ)**: the firmware image is segmented into MAC PDU-sized blocks; ARQ retransmits any missing block before declaring the session complete.
-  **Group addressing for multicast OTA**: a single downlink frame reaches multiple PTs; each PT independently tracks received blocks and unicasts NACKs for gaps.
-  **Resumable transfer**: if a PT temporarily loses coverage, the partial transfer state is retained and the session resumes from the last acknowledged block without retransmitting already-received data.

.. _requirements-2:

3.5 Requirements
~~~~~~~~~~~~~~~~

**UC-OTA-001** — The stack SHALL support session-scoped on-demand resource allocation: the application SHALL be able to open an OTA session specifying target PT(s), and the MAC scheduler SHALL allocate a dedicated burst of downlink slots for the session duration without requiring re-association of any target PT.

**UC-OTA-002** — On session completion or explicit abort, the scheduler SHALL release all slots reserved for the session and make them available to other traffic within one DECT NR superframe.

   Rationale: an unreleased OTA allocation that persists after the transfer degrades normal channel capacity for metering, alerts, and real-time commands.

**UC-OTA-003** — A unicast OTA session SHALL be able to transfer a 512 KB image to a single PT in at most 10 minutes under normal channel conditions using the maximum allocation the scheduler can grant without starving other services.

**UC-OTA-004** — The stack SHALL support multicast OTA to a PT group. Each group member SHALL independently acknowledge received blocks and NACK missing ones. The protocol SHALL allow per-PT retransmission of individual missing blocks without requiring all group members to be at the same offset.

**UC-OTA-005** — OTA transfers SHALL use a reliable data delivery service providing segmentation and ARQ (DLC-SA or equivalent). The session SHALL be declared complete only when the PT has confirmed receipt of every block and the application has verified image integrity (CRC or cryptographic hash at minimum).

**UC-OTA-006** — An OTA session SHALL be resumable after interruption. If a PT loses radio contact or the session is explicitly suspended, the transfer state (last acknowledged block offset) SHALL be retained on both FT and PT. On reconnection, the session SHALL resume from the last acknowledged block without retransmitting already-confirmed data.

**UC-OTA-007** — The MAC scheduler SHALL support installation and removal of a temporary per-session scheduling policy. The OTA policy SHALL: - pre-empt background scheduled traffic (metering uplinks, status reports); - yield to confirmed event uplinks (alert frames, UC-SC-005) and real-time group commands (UC-SC-003); - be automatically removed when the session ends, restoring the prior scheduling state.

**UC-OTA-008** — Normal scheduled services (metering uplinks, alert reception, real-time commands) SHALL continue operating during an active OTA session subject to the policy in UC-OTA-007. An OTA session SHALL NOT starve alert delivery or disrupt any existing association.

--------------

4. Stack Feature Requirements Summary
-------------------------------------

The following table maps use-case requirements to the MAC scheduler, access mode, and configuration features that the stack must provide. This serves as a cross-reference for validating that the implementation requirements in :ref:`codium-requirements-1` cover every use-case driver.

+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+
| Use-case requirement   | MAC feature required                                                  | Implementation requirement                             |
+========================+=======================================================================+========================================================+
| UC-SC-002, UC-SC-003   | MAC group/multicast addressing                                        | MAC-SCH-001 (scheduler), open (group addressing)       |
+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+
| UC-SC-004              | Scheduled uplink slot allocation at association                       | MAC-SCH-001, MAC-ASC-001                               |
+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+
| UC-SC-005              | Random access uplink                                                  | MAC-SCH-001, open (RACH)                               |
+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+
| UC-SC-006              | Beacon wake-up, low-duty-cycle sleep                                  | MAC-BCN (open), SYS-010                                |
+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+
| UC-SC-007              | Priority-based scheduling, pre-emption                                | MAC-SCH-004, MAC-SCH-006                               |
+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+
| UC-SC-001, UC-SC-008   | Association scaling                                                   | MAC-ASC-004a, MAC-ASC-004b                             |
+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+
| UC-AU-001, UC-AU-002   | Scheduled group downlink, deterministic per-frame deadline            | MAC-SCH-001, MAC-SCH-006                               |
+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+
| UC-AU-003              | Concurrent scheduled uplink + random access without audio frame delay | MAC-SCH-001, open (RACH)                               |
+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+
| UC-AU-004, UC-AU-005   | Random-access join and detection without full association             | open (RACH detection)                                  |
+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+
| UC-AU-006              | Mobility / zone handover                                              | open (MAC-MOB)                                         |
+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+
| UC-AU-007              | PT-side buffering                                                     | CVG-001 (DLC-A / CVG-2+)                               |
+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+
| UC-AU-008              | Strict priority: audio downlink over all uplink                       | MAC-SCH-004, MAC-SCH-006                               |
+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+
| UC-AU-009              | Host-injected downlink data, RPC latency                              | RPC-005, RPC-012, MAC-SCH-001                          |
+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+
| UC-OTA-001, UC-OTA-002 | Session-scoped slot allocation (open/close)                           | MAC-SCH-001, MAC-SCH-004, open (session API)           |
+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+
| UC-OTA-003             | Sustained scheduled unicast or multicast throughput                   | MAC-SCH-001, MAC-SCH-006                               |
+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+
| UC-OTA-004             | Multicast delivery with per-PT ARQ                                    | UC-SC-002 (group addressing), open (DLC multicast ARQ) |
+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+
| UC-OTA-005             | Segmentation and reassembly of large payloads                         | DLC-001 (DLC-SA)                                       |
+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+
| UC-OTA-006             | Resumable transfer (offset tracking)                                  | open (resumable transfer)                              |
+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+
| UC-OTA-007, UC-OTA-008 | Temporary scheduler policy, pre-emption ceiling                       | MAC-SCH-005, MAC-SCH-006, MAC-SCH-007                  |
+------------------------+-----------------------------------------------------------------------+--------------------------------------------------------+

Cells marked **open** indicate features not yet covered by a numbered implementation requirement; they represent input to the Open Questions section of :ref:`codium-requirements-1`.

--------------

5. Open Questions Derived from Use Cases
----------------------------------------

The following items arise from the use cases and are not yet resolved in :ref:`codium-requirements-1`. They should be tracked as open questions there.

**OQ-UC-01** — Group addressing format: DECT-2020 NR MAC supports group short radio device identities. Should the stack expose a group management API, or delegate group construction to the application? What is the maximum group size?

**OQ-UC-02** — Random access channel (RACH): what contention-access mechanism should be used for event alerts (UC-SC-005) and mid-show receiver joins (UC-AU-004)? DECT-2020 NR defines slotted-ALOHA-style random access in the MAC; the exact configuration (number of RACH slots per superframe, back-off algorithm) needs to be determined per deployment.

**OQ-UC-03** — Audio stream throughput feasibility: the 512 kbps (64 kB/s) group downlink target for the audio stream (UC-AU-001) must be validated against the maximum MAC PDU size, the selected MCS, and the TDMA frame structure. The per-frame latency bound of ≤ 10 ms (UC-AU-002) must also be validated against the superframe timing. If the combination of throughput and latency is unachievable in a single superframe, multi-superframe scheduling or a reduced-quality codec profile must be specified.

**OQ-UC-04** — Receiver detection without full association: DECT-2020 NR random-access frames contain the PT short ID, but the mechanism for admitting a probing receiver into an ongoing audio group — without completing a full association — is not defined by the standard. Whether this requires a partial association, a lightweight group-join procedure, or a vendor-specific extension to the random-access response must be specified before UC-AU-005 can be implemented.

**OQ-UC-05** — Zone handover imperceptibility: ETSI TS 103 636 defines procedures for PT handover between FTs within a network cluster. The standard’s handover timer values must be validated against the ≤ 10 ms service gap budget of UC-AU-006. If the standard’s minimum handover time exceeds one audio frame period, a pre-emptive handover mechanism (initiated before the PT leaves coverage of FT A, using PT-side buffer to conceal the gap) must be specified.

**OQ-UC-06** — Session-scoped resource allocation API: the MAC scheduler currently exposes ``schedule_tx`` and ``schedule_rx`` for individual frame requests. UC-OTA-001/002 require explicit session open/close semantics so that additional slots are allocated only for the duration of an OTA transfer and reclaimed immediately on completion. A session concept with a distinct handle and lifecycle is not yet represented in the scheduler API; its design (opaque handle, priority inheritance, pre-emption ceiling) must be specified.

**OQ-UC-07** — Multicast ARQ for OTA: DECT-2020 NR does not natively define per-PT negative acknowledgement for group-addressed frames at the MAC layer. Reliable multicast OTA (UC-OTA-004) therefore requires an ARQ mechanism above the MAC. The natural implementation is at the DLC layer (DLC-SA retransmit driven by per-PT NACK bitmask piggybacked on a dedicated uplink slot), but this is not yet specified in the DLC requirements.
