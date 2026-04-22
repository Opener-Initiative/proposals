---
title: "opener — Project Layout"
version: "0.1"
status: "draft"
date: "2026-04-22"
---

# opener — Project Layout

This document describes the source tree organisation for the `opener` project. The repository is a **Zephyr module** (consumable via `west`) that also ships the **Linux DKMS host kernel module** and the **shared RPC protocol headers** that are compiled into both.

## Monorepo rationale

The Zephyr firmware, the Linux DKMS kernel module, and the shared RPC protocol headers are kept in a single repository for the following reasons.

**Protocol header consistency.** The RPC wire protocol is defined once in `include/opener/rpc/`. Both build systems consume the same header files directly from the repository tree. There is no publication step, no version negotiation, and no risk of the Zephyr firmware and the Linux kernel module disagreeing on frame layout, magic bytes, module IDs, or CRC parameters.

**Independent build systems with no coupling.** Zephyr uses CMake + west; the Linux kernel module uses Kbuild + DKMS. The two build systems are entirely independent: a Kbuild failure in `linux/` does not block a Zephyr west build, and vice versa. Developers working only on the firmware side are never forced to have a kernel build environment set up, and kernel module developers are never forced to install the full nRF Connect SDK.

**Companion development.** When a new RPC procedure is added to the Zephyr server, the corresponding Linux kernel proxy is written in the same commit and reviewed together. This keeps the protocol and both implementations in step without relying on cross-repository coordination.

**Reference host implementation.** The Linux kernel module is the canonical example of how to integrate opener into a host OS. It demonstrates the full host-side contract: SPI transport, IRQ-driven receive, `net_device` registration, Generic Netlink UAPI, and per-module RPC proxy pattern. Any future port to another OS (e.g. a bare-metal host or a Windows driver) can use it as a specification by example.

---

## 1. Repository Root

```
opener/
├── zephyr/
│   └── module.yml          # Zephyr module manifest
├── west.yml                # optional: self-contained west manifest for development
├── CMakeLists.txt          # Zephyr module root CMakeLists
├── Kconfig                 # module-level Kconfig (sources subsys/ and lib/)
│
├── include/                # public headers — Zephyr stack API + shared RPC protocol
├── subsys/                 # DECT NR stack subsystems (Zephyr)
├── lib/                    # RPC transport library (Zephyr server side)
├── drivers/                # SPI RPC slave transport driver (Zephyr)
├── boards/                 # board definitions and DTS overlays
├── dts/                    # devicetree bindings
├── samples/                # ready-to-build sample applications
└── linux/                   # Linux host-side code (DKMS kernel module + tools)
```

---

## 2. Zephyr Module Manifest

**`zephyr/module.yml`**

```yaml
name: opener
build:
  cmake: .
  kconfig: Kconfig
  boards-root: .
  dts-root: .
west-commands: scripts/west_commands.py   # optional west extensions (flash, debug)
```

The `cmake` and `kconfig` keys point to the repository root so that the Zephyr build system picks up `CMakeLists.txt` and `Kconfig` automatically when the module is listed in the west workspace.

**`CMakeLists.txt`** (root, Zephyr-managed build only):

```cmake
cmake_minimum_required(VERSION 3.20)

# Make the shared RPC protocol headers available to all Zephyr targets.
zephyr_include_directories(include)

add_subdirectory(subsys/opener)
add_subdirectory(lib/opener_rpc)
add_subdirectory(drivers/spi_rpc_slave)
```

**`Kconfig`** (root):

```kconfig
rsource "subsys/opener/Kconfig"
rsource "lib/opener_rpc/Kconfig"
rsource "drivers/spi_rpc_slave/Kconfig"
```

---

## 3. `include/` — Public Headers

```
include/
└── opener/
    ├── rpc/                     # ── SHARED with Linux kernel module ──
    │   ├── frame.h              # wire frame layout: MAGIC, header struct, constants
    │   ├── module_ids.h         # MOD_ID and PROC_ID enumerations
    │   ├── error_codes.h        # RPC-level error codes
    │   └── crc16_ccitt.h        # CRC-16/CCITT inline (portable, no OS dependency)
    │
    ├── phy.h                    # HAL abstraction (struct opener_hal_ops)
    ├── mac.h                    # MAC public API (struct opener_mac_ops, assoc, sched)
    ├── dlc.h                    # DLC public API (struct opener_dlc_ops, DLC-SA)
    └── cvg.h                    # CVG public API (struct opener_cvg_ops)
```

### 3.1 Portability of Shared RPC Headers

The files under `include/opener/rpc/` are compiled into both the Zephyr firmware and the Linux kernel module. They must not include any Zephyr or Linux kernel headers. The compatibility strategy is:

**`include/opener/rpc/frame.h`**:

```c
#pragma once

/*
 * Portable fixed-width types: resolved by the including environment.
 *   Zephyr  → <zephyr/types.h> defines uint8_t / uint16_t
 *   Linux   → kernel module preamble typedef-maps __u8/__u16 → uint8_t/uint16_t
 *   Host userspace tools → <stdint.h>
 */
#ifndef __KERNEL__
#  include <stdint.h>
#endif

#define OPENER_RPC_MAGIC        0xDE
#define OPENER_RPC_FRAME_SIZE   512U
#define OPENER_RPC_HDR_SIZE     8U
#define OPENER_RPC_MAX_PLEN     (OPENER_RPC_FRAME_SIZE - OPENER_RPC_HDR_SIZE - 2U) /* -2 CRC */
#define OPENER_RPC_NOOP_MAGIC   0x00

/* Flags byte */
#define OPENER_RPC_FLAG_RESPONSE  (1U << 7)
#define OPENER_RPC_FLAG_EVENT     (1U << 6)
#define OPENER_RPC_FLAG_ERROR     (1U << 5)

struct __attribute__((packed)) opener_rpc_frame {
    uint8_t  magic;
    uint8_t  flags;
    uint8_t  mod_id;
    uint8_t  proc_id;
    uint16_t seq;    /* big-endian on wire */
    uint16_t plen;   /* big-endian on wire, payload length excluding header and CRC */
    uint8_t  payload[OPENER_RPC_MAX_PLEN];
    uint16_t crc;    /* big-endian on wire, CRC-16/CCITT over magic..payload */
};
```

The Linux kernel module includes a thin compatibility header before any shared header:

**`linux/kernel/opener_host/compat_types.h`**:

```c
#pragma once
/* Map Linux kernel types to the names expected by the shared RPC headers. */
#include <linux/types.h>
typedef __u8  uint8_t;
typedef __u16 uint16_t;
typedef __u32 uint32_t;
#define __KERNEL_COMPAT_TYPES__
```

Each kernel module `.c` file starts with:

```c
#include "compat_types.h"
#include "../../../include/opener/rpc/frame.h"
#include "../../../include/opener/rpc/module_ids.h"
```

---

## 4. `subsys/opener/` — DECT NR Stack (Zephyr)

```
subsys/opener/
├── CMakeLists.txt
├── Kconfig
│
├── phy/
│   ├── CMakeLists.txt
│   ├── Kconfig
│   ├── opener_phy_hal.c           # HAL dispatch (calls struct opener_hal_ops)
│   └── nrf91x1/
│       ├── CMakeLists.txt       # only added when CONFIG_OPENER_DIRECT_PHY_NRF91X1=y
│       └── opener_phy_nrf91x1.c   # direct nRF91x1 MDM calls
│
├── mac/
│   ├── CMakeLists.txt
│   ├── Kconfig
│   ├── opener_mac_core.c          # superframe, slot allocation, PT/FT mode
│   ├── opener_mac_scheduler.c     # TX/RX scheduler (struct opener_scheduler_ops impl)
│   ├── opener_mac_assoc.c         # association manager
│   └── opener_mac_group.c         # group addressing (MAC-GRP)
│
├── dlc/
│   ├── CMakeLists.txt
│   ├── Kconfig
│   ├── opener_dlc_core.c          # DLC dispatch, routing service
│   └── opener_dlc_sa.c            # DLC-SA: segmentation, ARQ (SAW / GBN)
│
└── cvg/
    ├── CMakeLists.txt
    ├── Kconfig
    └── opener_cvg_core.c          # convergence layer (IPv6 profile)
```

**`subsys/opener/Kconfig`** (excerpt):

```kconfig
menuconfig OPENER
    bool "DECT-2020 NR protocol stack"
    help
      Enable the DECT-2020 NR MAC/DLC/CVG stack.

if OPENER

config OPENER_FT_MODE
    bool "Fixed Termination (gateway) mode"

config OPENER_PT_MODE
    bool "Portable Termination (device) mode"

config OPENER_MAX_PT
    int "Maximum number of associated PTs (FT mode)"
    default 64
    range 1 256

config OPENER_DIRECT_PHY_NRF91X1
    bool "Direct PHY access via nRF91x1 MDM API"
    depends on SOC_NRF9120 || SOC_NRF9160

rsource "phy/Kconfig"
rsource "mac/Kconfig"
rsource "dlc/Kconfig"
rsource "cvg/Kconfig"

endif # OPENER
```

---

## 5. `lib/opener_rpc/` — RPC Transport Library (Zephyr Server Side)

```
lib/opener_rpc/
├── CMakeLists.txt
├── Kconfig
│
├── server/
│   ├── rpc_server.c             # SPI frame pump: rx_thread, tx_queue drain
│   ├── rpc_dispatch.c           # demultiplex mod_id → handler table
│   └── handlers/
│       ├── rpc_mac_handler.c    # MAC module RPC handlers (proc_ids → mac API calls)
│       ├── rpc_dlc_handler.c
│       └── rpc_cvg_handler.c
│
└── common/
    └── crc16_ccitt.c            # CRC-16/CCITT implementation (shared source,
                                 # also compiled into the Linux kernel module)
```

**`lib/opener_rpc/Kconfig`**:

```kconfig
config OPENER_RPC_SERVER
    bool "DECT NR RPC server (SPI slave transport)"
    depends on OPENER && SPI
    help
      Enable the SPI-based RPC server. The device acts as SPI slave and
      exposes all stack modules to the Linux host over the wire protocol
      defined in include/opener/rpc/.
```

---

## 6. `drivers/spi_rpc_slave/` — SPI Slave Transport Driver (Zephyr)

```
drivers/spi_rpc_slave/
├── CMakeLists.txt
├── Kconfig
└── spi_rpc_slave.c      # Zephyr SPI slave driver: DMA transfer, IRQ GPIO assertion
```

The driver asserts the IRQ GPIO when an event frame is ready to push to the host, and exposes a simple byte-stream interface to `lib/opener_rpc/server/rpc_server.c`.

---

## 7. `boards/` and `dts/` — Board Support

```
boards/
└── arm/
    ├── opener_devkit/          # reference development kit board definition
    │   ├── opener_devkit.yaml
    │   ├── opener_devkit.dts
    │   └── Kconfig.board
    └── overlays/
        └── spi_rpc.overlay      # SPI + IRQ GPIO Devicetree overlay for RPC transport

dts/
└── bindings/
    └── opener,spi-rpc-slave.yaml  # Devicetree binding for the RPC slave node
```

**`dts/bindings/opener,spi-rpc-slave.yaml`** (excerpt):

```yaml
description: DECT NR SPI RPC slave node

compatible: "opener,spi-rpc-slave"

properties:
  irq-gpios:
    type: phandle-array
    required: true
    description: GPIO used by the device to signal a pending event to the host.
  spi-max-frequency:
    type: int
    default: 8000000
```

---

## 8. `samples/` — Sample Applications

Three samples are provided, each exercising a distinct operational role and serving as the reference starting point for that class of product.

| Sample | Mode | Role |
|---|---|---|
| `opener_sensor_pt/` | PT only | Sensor node: associates with an FT, sends periodic uplink data |
| `opener_aggregator_ft/` | FT only | Aggregator / gateway: manages associations, receives uplink data, drives downlink commands; intended for host-controlled deployment with the Linux DKMS module |
| `opener_relay/` | FT + PT simultaneously | Mesh relay: associates upward with a parent FT as a PT, while acting as an FT to child PTs; demonstrates the self-organising mesh topology |

```
samples/
├── opener_sensor_pt/
│   ├── CMakeLists.txt
│   ├── prj.conf             # CONFIG_OPENER=y CONFIG_OPENER_PT_MODE=y
│   ├── boards/
│   │   └── nrf9161dk.conf
│   └── src/
│       └── main.c
│
├── opener_aggregator_ft/
│   ├── CMakeLists.txt
│   ├── prj.conf             # CONFIG_OPENER=y CONFIG_OPENER_FT_MODE=y CONFIG_OPENER_RPC_SERVER=y
│   ├── boards/
│   │   └── nrf9161dk.conf
│   └── src/
│       └── main.c
│
└── opener_relay/
    ├── CMakeLists.txt
    ├── prj.conf             # CONFIG_OPENER=y CONFIG_OPENER_FT_MODE=y CONFIG_OPENER_PT_MODE=y
    ├── boards/
    │   └── nrf9161dk.conf
    └── src/
        └── main.c
```

---

## 9. `linux/` — Linux Host Side

```
linux/
├── kernel/                          # DKMS kernel module
│   ├── dkms.conf
│   ├── Makefile                     # kernel Kbuild file
│   └── opener_host/                # module source directory
│       ├── compat_types.h           # uint8_t / uint16_t shims for kernel environment
│       ├── opener_host.h           # internal structs (opener_priv, locking, queues)
│       ├── opener_main.c           # spi_driver probe/remove, IRQ top-half
│       ├── opener_rpc.c            # SPI frame pump, RPC call serialisation
│       ├── opener_netdev.c         # net_device (ARPHRD_NONE, IPv6 profile)
│       ├── opener_netlink.c        # Generic Netlink UAPI (family "OPENER")
│       └── opener_scheduler.c      # scheduler module proxy (kernel side)
│
└── tools/                           # userspace management utilities
    ├── CMakeLists.txt
    ├── opener_ctl.c                   # libnl-based CLI: stats, channel config
    └── opener_ctl.h
```

### 9.1 DKMS Configuration

**`linux/kernel/dkms.conf`**:

```ini
PACKAGE_NAME="opener-host"
PACKAGE_VERSION="0.1.0"

BUILT_MODULE_NAME[0]="opener_host"
BUILT_MODULE_LOCATION[0]="opener_host/"
DEST_MODULE_LOCATION[0]="/kernel/drivers/net/"

MAKE[0]="make -C /lib/modules/${kernelver}/build M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build"
CLEAN="make -C /lib/modules/${kernelver}/build M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build clean"

AUTOINSTALL="yes"
```

### 9.2 Kernel Module Makefile

**`linux/kernel/Makefile`**:

```makefile
# Shared RPC protocol headers live at repo root include/.
# The DKMS build copies only linux/kernel/ into the build tree, so the
# relative path must be resolved from the original source location.
OPENER_INCLUDE := $(src)/../../include

ccflags-y := -I$(OPENER_INCLUDE)

obj-m := opener_host.o

opener_host-y := \
    opener_host/opener_main.o      \
    opener_host/opener_rpc.o       \
    opener_host/opener_netdev.o    \
    opener_host/opener_netlink.o   \
    opener_host/opener_scheduler.o
```

> **DKMS path note**: when DKMS copies the sources into its build tree it preserves the directory structure under `linux/kernel/`. The `$(src)` variable in the Makefile resolves to the module source directory inside the DKMS tree, so `$(src)/../../include` correctly walks up to the repository root `include/` directory regardless of where DKMS places the build tree.

### 9.3 Adding a New Module Proxy (Host Side)

To expose a new stack module via RPC on the kernel side, add one file following the existing pattern:

| Step | Action |
|---|---|
| 1 | Define `MOD_ID_<NAME>` in `include/opener/rpc/module_ids.h` |
| 2 | Add `opener_<name>.c` under `linux/kernel/opener_host/` |
| 3 | Implement `struct opener_<name>_ops` proxy functions using `opener_rpc_call()` and `cpu_to_be*` / `be*_to_cpu` |
| 4 | Register the event handler in `opener_rpc.c:process_frame()` dispatch table |
| 5 | Add `opener_host/<name>.o` to `opener_host-y` in `linux/kernel/Makefile` |
| 6 | Add the corresponding RPC handler in `lib/opener_rpc/server/handlers/rpc_<name>_handler.c` (Zephyr side) |

---

## 10. Source Cross-Reference

The following table shows which directories contribute to each build target.

| Directory | Zephyr firmware | Linux DKMS module | Host userspace tools |
|---|:---:|:---:|:---:|
| `include/opener/rpc/` | ✓ (headers) | ✓ (headers, via `-I`) | ✓ (headers) |
| `include/opener/` (non-rpc) | ✓ (headers) | — | — |
| `subsys/opener/` | ✓ | — | — |
| `lib/opener_rpc/common/` | ✓ | ✓ (`crc16_ccitt.c` compiled separately) | — |
| `lib/opener_rpc/server/` | ✓ | — | — |
| `drivers/spi_rpc_slave/` | ✓ | — | — |
| `linux/kernel/opener_host/` | — | ✓ | — |
| `linux/tools/` | — | — | ✓ |

---

## 11. Open Questions

**OQ-LAYOUT-01** — `crc16_ccitt.c` dual compilation: the CRC implementation lives in `lib/opener_rpc/common/` and is compiled by both the Zephyr build and the Linux kernel Makefile (via an explicit `obj-m` entry or `ccflags-y` path). An alternative is to keep it header-only as a `static inline` in `include/opener/rpc/crc16_ccitt.h`, avoiding the dual-source problem entirely. The trade-off is a slightly larger object per translation unit vs. a cleaner build dependency graph.

**OQ-LAYOUT-02** — DKMS include path stability: the `$(src)/../../include` path in the kernel Makefile assumes DKMS preserves the relative directory structure (`linux/kernel/` inside the repo root). If the DKMS installation copies only `linux/kernel/` without the repository root, this path breaks. A robust alternative is to install the shared headers as a system header package (e.g., `opener-dev`) and reference them via a fixed path such as `/usr/include/opener`.

**OQ-LAYOUT-03** — West manifest ownership: this repository can be used as a west module (added to an external manifest) or as a self-contained manifest repo (via `west.yml`). The self-contained mode is useful for standalone development but may conflict with the application's own manifest. The recommended approach (module-only) should be documented and the `west.yml` clearly marked as a development convenience.
