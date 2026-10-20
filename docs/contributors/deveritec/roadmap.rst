..  roadmap:

Roadmap
============

.. note::
    This document acts as an starting point to document a assumed roadmap for the Opener stack. 
    It is work in progress and first collects some ideas.


It is proposed to start buttom-up. That means we want to start from driver and go up the stack to layer 2 including scheduler, control with its processes like beaconing/beacon-scan, mac encoding/decoding and more processes. On top of that the DLC and CVG needs to be implemented.


::

    * Proposals
    |\
    | * dectnrp_driver_api
    | * dectnrp_nrf91x1
    | * Samples using dectnr_driver
    |/
    *   Milestone 1 - Support for nRF91x1 and basic rssi1, rx and tx operations   <--- Current status
    |\
    | * Basic dectnrp L2 implementation - implement net and kconfig interfaces
    | * Integration of dectnrp L2 into net-framework, startup works 
    | * Simple scheduler for simple tx and rx without synchronization
    | * Transparent MAC - without encoding/decoding
    | * Raw net socket for simple tx and rx of pre-encoded messages
    | * First implementation of control module - dectnrp_ctrl
    | * First implementation of managment API - dectnrp_mgmt
    | * First implementation of shell - dectnrp_shell
    |/
    *  Milestone 2 - Basic structure of the stack and simple rx/tx in place
    |\
    | * Encoding/decoding for basic IEs like beacons and association IEs
    | * Add encoding/decoding interface to be used in dectnrp_ctrl
    |/
    |\
    | * Channel selectable via dectnrp_mgmt, fixed band 1
    |/
    |\
    | * FT and PT separation - selectable at build time
    | * supports beacon sending (FT) with configurable period (run-time option).
    | * supports beacon synchronization (PT) to beacons received on pre-defined channel.
    | * Scheduler synchronizes to sent/received beacons
    |/
    |\
    | * Association process
    |/
    *   Milestone 3 - Simple FT/PT functionality of beaconing, synchronization and association
    |\
    | * Transparent Userflow messages
    |/
    |\
    | * Rssi1 scan and auto channel selection
    |/
    .
    .
    .
    * MAC IE multiplexing and assembly (concept first)
    * Resource and channel management (concept first)
    * More complex scheduler algorithms (concept first)
    * HARQ operation
    * DLC IEs - encoding/decoding
    * DLC Service 0
    * DLC Service 1..3
    * Security Procedures
    * Power control
    * CVG IEs - encoding/decoding
    * CVG Service 0..4
    * CVG Endpoint management/multiplexing
    .
    .
    .


