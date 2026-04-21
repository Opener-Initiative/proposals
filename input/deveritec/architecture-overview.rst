..  architecture-overview:

=======
Preface
=======

This document acts as an starting point to document DECT NR+ Stack. 
It is work in progress and first collects some ideas.

Following proposal is intended for integration into Zephyr net-stack architecture `Network Stack Architecture
<https://github.com/zephyrproject-rtos/zephyr/blob/v4.2-branch/doc/connectivity/networking/net-stack-architecture.rst>`_ 
but may be also used in an Zephyr independent environment.


============
Architecture
============

.. image:: architecture-overview.svg

----------------------
Modules
----------------------

*   Driver(s)

    *   For each supported modem there should be a driver which abstracts the vendor specific implementation and maps it to the Driver API.
    *   Currently only nRF91x1 supporting nRF9151, nRF9161, nRF9131 (all have same core nRF9120).
    *   One driver selected at build time.
*   Scheduler

    *   Scheduler module does the timed interactions with the modem/driver via Driver API. 
    *   It schedules beacon (FT) and beacon-scan operations (PT, FT&PT).
    *   It schedules RX, TX and RSSI1 operations on resources on working channel / multiple channels.
    *   There may be multiple implementations for different scheduling strategies.
*   MAC layer encoding/decoding

    *   Module which does the encoding of out-going MAC layer PDUs which should be transmitted via Driver API and decoding of in-coming MAC layer PDUs received via Driver API.
    *   All supported SDUs should have encoding and decoding functionality.
*   DLC layer

    *   Module which does the encoding of out-going DLC layer PDUs which should be handed to MAC layer encoding and decoding of in-coming DLC layer PDUs handed over from MAC layer decoding.
    *   Should handle different service types

        *   Transparent service
        *   Segmentation/Assembly
        *   ARQ
        *   Routing
*   CVG layer

    *   Module which does the encoding of out-going CVG layer PDUs which should be handed to DLC layer encoding and decoding of in-coming CVG layer PDUs handed over from DLC layer decoding.
    *   Should handle different service types
    *   Endpoint multiplexing  
*   Control Module / DECT NR+ Core

    *   Core module which should manage the state(s) and coordinate services of the stack.
    *   Coordination of control messages / incoming control messages.
    *   Multiple implementations possible for FT/PT/FT&PT.
*   Device & Connection Manager

    *   Holds own device parameters (as network-id, long ID, short ID)
    *   Holds device parameters and state of remote devices
    *   Association management
*   Resource & Power Manager

    *   Holds and manages radio resources and related tx-powers
    *   (Power control may be sourced out?)
*   Secure Storage

    *   Securely stores/persists security redentials as keys etc
*   Persistence Module

    *   The common part of the persistance implementation.
    *   Should map vendor/hardware specific persitance up to the Persistence API.
*   Managment

    *   The module which connects the Managment API with different blocks of the stack mainly the Control Module.
*   Shell

    *   Module providing some sort of shell (via uart etc) to the remote user.
    *   It mainly uses the Managment module to run commands etc on the stack.
*   Sockets

    *   A common POSIX conform socket implementation to allow the user/application to communicate with the application.

-------------
API Reference
-------------

^^^^^^^^^^^^^^^^^^^^^
DECT NR+ API Overview
^^^^^^^^^^^^^^^^^^^^^

*   Driver API

    *   The common interface between Scheduler and Driver.
    *   This interface dispatches so called Driver Operations (RX, TX, RSSI1) between scheduler and driver.
*   Scheduler API

    *   A common interface for different scheduler implementations.
    *   Provides an interface for the Core Module to manage the scheduler.
*   Persistence API

    *   A common interface to persist and read non-volatile parameters of the stack like network-id, long device id etc.
*   Secure Storage API

    *   A common interface to manage and access security items like crypto keys.
*   Security API

    *   A common interface to handle encryption/authentication and decryption/verification of secured DECT NR+ messages.
*   Managment API

    *   A common interface between DECT NR+ stack and application by which the application can manage the stack, get information about state and behaviour.
*   Socket API

    *   A common POSIX socket interface to allow the user/application to provide data towards the stack (tx) and receive data from remote devices (rx).    


^^^^^^^^^^^^^^^^^^^^^^
DECT NR+ Driver API
^^^^^^^^^^^^^^^^^^^^^^

`dectnrp-driver-api <dectnrp-driver-api.rst>`_.