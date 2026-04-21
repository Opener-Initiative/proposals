# ETSI IPR ISLDs affecting DECT-2020 NR

## WIRELESS MESH NETWORK

- **Filing date:** 2023-02-14
- **ETSI IPR reference:** [ISLD-202302-013](https://ipr.etsi.org/IPRDetails.aspx?IPRD_ID=7407&IPRD_TYPE_ID=2&MODE=2)
- **European patent:** [EP3506593 B1](http://worldwide.espacenet.com/publicationDetails/biblio?DB=EPODOC&adjacent=true&locale=en_EP&FT=D&date=20210630&CC=EP&NR=3506593B1&KC=B1)
- **Applicant/holder:** Wirepas Oy

### Summary

Describes a method for deriving an Initialization Vector (IV) from a Super Frame Counter (SFC, obtained from a beacon message), a Sender-Node-Specific Frame Counter (SNSC), and a transmitter ID. Radio devices (RDs) encrypt data frames by means of the IV, which is used as nonce in a CTR (counter) block cipher mode of operation. The cluster head initializes the SFC and includes it in a periodically broadcasted beacon frame. The SFC is incremented or, alternatively, reinitialized once per superframe (cluster beacon period). Receiving RDs maintain the SFC even if they missed a beacon (works when incrementing the SFC by a predefined value). The transmitter ID and the SNSC are included in all transmitted frames as plaintext. The SNSC is incremented with every transmitted frame.

### Affected specifications

| Specification | Affected clauses (not exhaustive) |
| --- | --- |
| ETSI TS 103 636-4 V2.1.1 (2024-10) | 5.9, 6.4.3.1 |
| ETSI TS 103 636-5 V2.1.1 (2024-10) | Not affected |
| ETSI TS 103 874-2 V1.1.1 (2024-09) | Not affected |

## RESOURCE SCHEDULING SYSTEM FOR A WIRELESS COMMUNICATION NETWORK

- **Filing date:** 2023-02-15
- **ETSI IPR reference:** [ISLD-202302-017](https://ipr.etsi.org/IPRDetails.aspx?IPRD_ID=7410&IPRD_TYPE_ID=2&MODE=2)
- **European patent:** [EP3927081 A1](http://worldwide.espacenet.com/publicationDetails/biblio?DB=EPODOC&adjacent=true&locale=en_EP&FT=D&date=20211222&CC=EP&NR=3927081A1&KC=A1)
- **Applicant/holder:** Wirepas Oy

### Summary

Describes a transmission resource scheduling system for wireless communication networks that frequently signal node identifiers in packet transmissions (e.g., DECT-2020 NR). Radio devices (RDs) separately transmit a transmission resource configuration and a transmission resource assignment. An RD A senses the radio environment and broadcasts a beacon message. An RD B receiving the beacon can then respond by sending an association request message in a unicast transmission. The association is completed if RD A decides to send an association acknowledged response message which includes a transmission resource configuration (RC) that specifies at least the validity, periodicity, frequency channel, and length of the resource allocation. The RC information element may also be included in other messages (e.g., cluster beacon) if, e.g., the resource configuration changes during the association. The RC information element comprises two parts for uplink and downlink transmission directions. RD A (the cluster head) broadcasts a transmission resource assignment (RA) as part of a (cluster) beacon message. The RA information element indicates the short RD IDs of the cluster members to which the resources are assigned to and whether the resource assignments are uplink or downlink. The RD IDs in the list may be mapped to the resource configurations according to their order of appearance. Alternatively, group IDs and resource tags may be used to refer to groups of RDs and specific resources in the resource configuration, respectively.

### Affected specifications

| Specification | Affected clauses (not exhaustive) |
| --- | --- |
| ETSI TS 103 636-4 V2.1.1 (2024-10) | 5.3, 5.4, 5.11, 6.4.2.5, 6.4.3.3, 6.4.3.4, 6.4.3.9 |
| ETSI TS 103 636-5 V2.1.1 (2024-10) | Not affected |
| ETSI TS 103 874-2 V1.1.1 (2024-09) | Not affected |

## ADDRESSING SYSTEM FOR A WIRELESS COMMUNICATION NETWORK

- **Filing date:** 2023-02-15
- **ETSI IPR reference:** [ISLD-202302-018](https://ipr.etsi.org/IPRDetails.aspx?IPRD_ID=7411&IPRD_TYPE_ID=2&MODE=2)
- **European patent:** [EP3876503 A1](http://worldwide.espacenet.com/publicationDetails/biblio?DB=EPODOC&adjacent=true&locale=en_EP&FT=D&date=20210908&CC=EP&NR=3876503A1&KC=A1)
- **Applicant/holder:** Wirepas Oy

### Summary

Describes a method for identifying and addressing radio devices (RDs) in a wireless communication network using long node identifiers and short node identifiers which provide network-wide uniqueness and local link-level uniqueness, respectively. The communication system uses a physical (PHY) layer packet that comprises Synchronization Training Field Symbols (STFS), a Channel Training Field (CTF), a CRC-protected PHY control field, and a data field. The PHY control field includes information such as the used Modulation and Coding Scheme (MCS), a network ID, a receiver ID, a transmitter ID, the transmission power, and information related to HARQ. The MAC PDU includes a MAC header field, which indicates the content of the MAC PDU data and contains information related to MAC level security. Integrity protection is provided by means of a Message Integrity Code (MIC). The long node ID is used to identify (address) RDs and in security procedures (ciphering, integrity protection). RDs generate a short node ID to identify and differentiate devices in a dedicated communication (association) between devices in the network. The short ID is included as transmitter address in the PHY control field of a beacon packet, whereas the long ID is included in another part of the packet to secure the communication on link level. RDs receiving the beacon packet regenerate their own short ID if they detect a collision. Association signaling is used to exchange the relation of long and short IDs between devices. If the device that sent the beacon receives an association request packet, it checks whether the short ID included in the association request packet matches its own short ID or any other recorded short ID. In case of a collision, it responds with a NACK packet so that the device requesting association can generate a new short ID.

### Affected specifications

| Specification | Affected clauses (not exhaustive) |
| --- | --- |
| ETSI TS 103 636-4 V2.1.1 (2024-10) | 4.2.3, 6.2 |
| ETSI TS 103 636-5 V2.1.1 (2024-10) | Not affected |
| ETSI TS 103 874-2 V1.1.1 (2024-09) | Not affected |

## CONFIGURATION SYSTEM FOR A WIRELESS COMMUNICATION NETWORK

- **Filing date:** 2024-11-01
- **ETSI IPR reference:** [ISLD-202411-002](https://ipr.etsi.org/IPRDetails.aspx?IPRD_ID=8883&IPRD_TYPE_ID=2&MODE=2)
- **European patent:** [EP4106414 A1](http://worldwide.espacenet.com/publicationDetails/biblio?DB=EPODOC&adjacent=true&locale=en_EP&FT=D&date=20221221&CC=EP&NR=4106414A1&KC=A1)
- **Applicant/holder:** Wirepas Oy

### Summary

Describes a method for the efficient and robust distribution of network-wide configuration data in a wireless mesh network. After receiving network-persistent configuration data (CO), sink devices and router devices periodically broadcast (as part of a beacon message) a configuration data description (DE) comprising a version identifier (VE) to indicate that they can disseminate the respective CO on request. In addition to the VE, the DE may comprise a source identifier (SO) that identifies the sink device from which the CO originated. The CO may comprise configuration information related to IPv6, 6LoWPAN, DHCP, DNS, service, and server. Additionally, it may comprise public keys or digital certificates, software update control information, software images, and security keys or security key lists. The CO originates from an external backend system, which sends the CO via a gateway device to the sink node of the mesh network, either unsolicitedly or in response to a request message. Sink nodes can receive multiple CO from the backend for different purposes. Radio devices (RDs) lacking the CO (or its most recent version) from a specific source can send a configuration data request (RE), which may be included when requesting association. Messages exceeding the maximum payload size may be sent in segments. To indicate the segmentation of a message, a segmentation IE and and a segmentation status IE can be used. Router devices store all content (including payloads not deemed relevant) of the received CO to be able to redistribute the CO on request. When an RD changes its role to become a router device and has not yet stored all content of the CO, it first requests the content of the CO before responding to an RE.

### Affected specifications**

| Specification | Affected clauses (not exhaustive) |
| --- | --- |
| ETSI TS 103 636-4 V2.1.1 (2024-10) | 6.4.2.10, 6.4.3.11, 6.4.3.17 |
| ETSI TS 103 636-5 V2.1.1 (2024-10) | Annex C (normative) |
| ETSI TS 103 874-2 V1.1.1 (2024-09) | Not affected |

## AN ASSOCIATION SOLUTION FOR A WIRELESS COMMUNICATION NETWORK

- **Filing date:** 2024-11-01
- **ETSI IPR reference:** [ISLD-202411-004](https://ipr.etsi.org/IPRDetails.aspx?IPRD_ID=8885&IPRD_TYPE_ID=2&MODE=2)
- **European patent:** [EP4145908 A1](http://worldwide.espacenet.com/publicationDetails/biblio?DB=EPODOC&adjacent=true&locale=en_EP&FT=D&date=20230308&CC=EP&NR=4145908A1&KC=A1)
- **Applicant/holder:** Wirepas Oy

### Summary

Describes a method for an association process between an initiator device and a target device in a wireless communication network that aims at avoiding unsuccessful association attempts to reduce power consumption and waste of spectral resources. The target devices (sink devices or router devices) periodically transmit (network or cluster) beacon messages comprising load data, which the initiator devices (router devices or non-router devices) may use to decide whether to attempt an association. The load data may comprise the number of currently associated devices, the maximum allowed number of associated devices, as well as performance parameters such as RACH load data, channel load data, traffic load data, and route cost data. The association request message may comprise data indicative of the initiating device's operation role, if its in an energy saving mode, its number of currently associated devices, and its traffic load. The target device may use this data to decide whether to approve or reject the association request. The maximum allowed number of associated devices may be provisioned as a network configuration parameter, e.g., if it is the same for all devices in the network. When selecting the most suitable target device for association, the initiating device may consider additional parameters such as the link quality, the target device's ability to user higher order modulation, the offered minimum bitrate, guaranteed E2E latency characteristics, and the availability of a route to a sink device. When making a decision, certain selection criteria may be prioritized over others. For example, to improve the routing performance in the network, target devices may prioritize router devices over non-router devices when deciding whether to approve or reject an association request.

### Affected specifications

| Specification | Affected clauses (not exhaustive) |
| --- | --- |
| ETSI TS 103 636-4 V2.1.1 (2024-10) | 5.1.4, 6.4.2.4, 6.4.3.10 |
| ETSI TS 103 636-5 V2.1.1 (2024-10) | Not affected |
| ETSI TS 103 874-2 V1.1.1 (2024-09) | 7.2.1.4, 7.2.8 |

## A TIMING SYSTEM FOR A WIRELESS COMMUNICATION NETWORK

- **Filing date:** 2024-11-01
- **ETSI IPR reference:** [ISLD-202411-005](https://ipr.etsi.org/IPRDetails.aspx?IPRD_ID=8886&IPRD_TYPE_ID=2&MODE=2)
- **European patent:** [EP4258760 A1](http://worldwide.espacenet.com/publicationDetails/biblio?DB=EPODOC&adjacent=true&locale=en_EP&FT=D&date=20231011&CC=EP&NR=4258760A1&KC=A1)
- **Applicant/holder:** Wirepas Oy

### Summary

Describes a timing system for a wireless mesh network, where a cluster head device receives timing information from an associated member device that periodically broadcasts beacon messages indicating the timing of the next beacon message. The cluster head device may adjust its receiving window margin according to the timing information it received in the beacon message. If a cluster member device detects that the drift of its beacon timing exceeds a threshold value, it transmits the next periodical message to the cluster head device. A cluster member device may determine the drift of its beacon timing based on the drift of the cluster head device's beacon timing and the time expired since it last sent a periodical message to the cluster head device. A cluster member device may transmit the next periodical message after it receives a request from the cluster head device. A cluster member device may include a neighboring device's beacon timing in its next beacon message to the cluster head device. Cluster beacon messages may be used to provide frame and slot timing information to the cluster. Additionally, cluster beacon messages may be used to announce radio parameters and radio resource information so that other radio devices may communicate with the sink device. Network beacon messages may indicate the presence of a network and may comprise the timing, periodicity, and operating channel of the cluster beacon messages. It may also contain information of neighboring radio devices. Network beacon messages may be transmitted on a common network channel. Information related to the timing and frequency channel of the next beacon message may also be included in other periodical messages to the cluster head device, e.g. in an association request message. By sending beacons to the cluster head device, a cluster member device periodically signals its presence and avoids losing its membership, e.g., due to a timeout.

### Affected specifications

| Specification | Affected clauses (not exhaustive) |
| --- | --- |
| ETSI TS 103 636-4 V2.1.1 (2024-10) | 5.1.5, 6.4.2.2, 6.4.2.3, 6.4.2.4, 6.4.3.6 |
| ETSI TS 103 636-5 V2.1.1 (2024-10) | Not affected |
| ETSI TS 103 874-2 V1.1.1 (2024-09) | 7.2.1.5 |
