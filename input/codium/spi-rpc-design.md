# opener — SPI RPC Transport: Design and Implementation

**Document version**: 0.2  
**Status**: Draft  
**Scope**: SPI RPC wire protocol, device-side Zephyr implementation, host-side Linux kernel module implementation.  
**Illustrative module**: Radio Scheduler (`MAC-SCH`)

---

## 1. Introduction

In a device+host deployment, the Zephyr device (nRF91x1) acts as a radio modem. A Linux host communicates with it over SPI to call into on-device modules or to provide host-side module implementations that the device stack calls back into. The SPI RPC transport is the mechanism that makes all of this transparent: every module interface defined as a `struct opener_<module>_ops` can be backed either by a local implementation or by an RPC proxy, with no change to the caller.

This document uses the Radio Scheduler as a concrete, end-to-end example. The scheduler runs on the device. A Linux host application calls `schedule_tx` to queue a transmission, and receives a `on_tx_complete` event asynchronously when the radio finishes. The same framing, dispatch, and proxy patterns apply to every other module.

---

## 2. Architecture Overview

The host-side driver is a DKMS kernel module (`opener_host`). It registers as a Linux SPI driver, exposes a standard `net_device` (`opener0`) for IPv6 traffic, and provides a Generic Netlink family (`OPENER`) for management operations. Placing the driver in the kernel avoids the per-packet context-switch cost of a spidev userspace daemon and integrates transparently with the Linux routing stack, netfilter, and tc.

> **Note — TUN/TAP alternative**: A fully userspace implementation is also possible. A userspace daemon bridging packets between the spidev transport and a TUN (IPv6, layer 3) or TAP (Ethernet-over-DECT, layer 2) device is worth evaluating for early development and platforms without DKMS. See Section 9 for a detailed comparison.

```
 Linux host (kernel space)                        Zephyr device (nRF91x1)
 ────────────────────────────────────────         ─────────────────────────────────────
  IPv6 stack / routing
      ↕  sk_buff
  net_device "opener0"                             ┌─ Radio Scheduler (real implementation)
  (opener_netdev.c)                             │       ▲
      │ ndo_start_xmit                           │       │ schedule_tx / cancel / stats
      ▼                                          │       │
  TX work queue (opener_tx_work)                │  RPC server / dispatch
      │ frame_build → spi_sync                   │  (opener_rpc_server.c)
      │                                          │       │ deserialize → call real module
      ▼                                          │       │ serialize result → TX buffer
  SPI kernel driver ──── SPI 8 MHz ─────────────►        │
  (opener_main.c /      CS, MOSI, MISO,        ◄────── SPI slave (Zephyr)
   opener_rpc.c)        SCLK                           │
      ▲                                                  │ async event: on_tx_complete
      │ request_irq    ◄──── IRQ GPIO ───────────────────┘    → push to TX buffer
      │                                                        → assert IRQ GPIO
  RX work queue (opener_rx_work)
      │ spi_sync → process_frame → complete() / netif_rx
      │
 ─────────────────────────── user / kernel boundary ───────────────────────────────────
 Linux host (userspace)
  Standard IPv6 socket API         ← applications see a normal network interface
  Generic Netlink (OPENER family) ← management: stats, channel config, ...
  (ip link, ip -6 addr, custom tools)
```

**Key principle**: from anywhere in the kernel (scheduler proxy, DLC module, netdev TX path), `struct opener_scheduler_ops` is just a struct of function pointers. Whether those pointers lead to a local implementation or to SPI RPC calls is irrelevant to the caller.

---

## 3. Shared Definitions

These headers are compiled on both the device (Zephyr) and the host (Linux). They live in a `common/` directory that is part of both build systems.

### 3.1 Scheduler API (`opener_scheduler.h`)

```c
#ifndef OPENER_SCHEDULER_H
#define OPENER_SCHEDULER_H

#include <stdint.h>
#include <stddef.h>

#define OPENER_SCHEDULER_API_VERSION  1U

/* Opaque handle returned by schedule_tx / schedule_rx. */
typedef uint32_t opener_sched_handle_t;
#define OPENER_SCHED_HANDLE_INVALID   UINT32_MAX

/* Modulation and coding scheme index (0–11, maps to BPSK..1024-QAM). */
typedef uint8_t opener_mcs_t;

/* ------------------------------------------------------------------ */
/*  Request structures                                                  */
/* ------------------------------------------------------------------ */

struct opener_tx_request {
    uint8_t          channel;        /* Operating channel index        */
    uint8_t          subslot_start;  /* Earliest acceptable subslot    */
    uint8_t          subslot_count;  /* Number of subslots to allocate */
    opener_mcs_t       mcs;            /* Requested MCS                  */
    uint8_t          priority;       /* 0 = lowest, 7 = highest        */
    uint16_t         earliest_sfn;   /* Earliest acceptable frame      */
    uint16_t         latest_sfn;     /* Deadline frame (0 = no limit)  */
    const uint8_t   *payload;        /* MAC PDU payload                */
    uint16_t         payload_len;    /* Payload length in bytes        */
};

struct opener_rx_request {
    uint8_t          channel;
    uint8_t          subslot_start;
    uint8_t          subslot_count;
    uint16_t         sfn;            /* Frame to listen on (0 = any)   */
    uint32_t         peer_short_id;  /* Expected transmitter short ID  */
};

/* ------------------------------------------------------------------ */
/*  Result structures (delivered via event callbacks)                   */
/* ------------------------------------------------------------------ */

/* TX completion status codes. */
#define OPENER_TX_STATUS_OK          0  /* ACK received or no-ACK mode   */
#define OPENER_TX_STATUS_NACK        1  /* HARQ NACK, max retries reached */
#define OPENER_TX_STATUS_CANCELLED   2  /* Cancelled before transmission  */
#define OPENER_TX_STATUS_NO_RESOURCE 3  /* Scheduler could not allocate  */

struct opener_tx_result {
    opener_sched_handle_t handle;
    uint8_t             status;       /* OPENER_TX_STATUS_* */
    uint8_t             harq_retries; /* Number of HARQ rounds used    */
    int8_t              rssi_dbm;     /* RSSI at peer (if reported)    */
};

struct opener_rx_result {
    opener_sched_handle_t handle;
    uint8_t             channel;
    uint8_t             subslot;
    int8_t              rssi_dbm;
    uint16_t            payload_len;
    uint8_t             payload[256]; /* Max PDU size for this example */
};

/* ------------------------------------------------------------------ */
/*  Scheduler operations interface                                       */
/* ------------------------------------------------------------------ */

struct opener_scheduler_ops {
    uint32_t version;   /* Must equal OPENER_SCHEDULER_API_VERSION */

    /**
     * Request transmission of a MAC PDU.
     * Returns 0 and fills *handle on success, negative errno on failure.
     * Completion is reported via opener_scheduler_event_ops.on_tx_complete.
     */
    int (*schedule_tx)(void *ctx,
                       const struct opener_tx_request *req,
                       opener_sched_handle_t *handle);

    /**
     * Request a reception window.
     * Returns 0 and fills *handle on success, negative errno on failure.
     * Completion is reported via opener_scheduler_event_ops.on_rx_received.
     */
    int (*schedule_rx)(void *ctx,
                       const struct opener_rx_request *req,
                       opener_sched_handle_t *handle);

    /**
     * Cancel a pending TX or RX request.
     * Returns 0 if cancelled, -EALREADY if already completed.
     */
    int (*cancel)(void *ctx, opener_sched_handle_t handle);

    /**
     * Retrieve scheduler performance counters.
     * Returns 0 on success, negative errno on failure.
     */
    int (*get_stats)(void *ctx, struct opener_scheduler_stats *out);
};

struct opener_scheduler_stats {
    uint32_t tx_requests;
    uint32_t tx_completed_ok;
    uint32_t tx_completed_nack;
    uint32_t tx_cancelled;
    uint32_t rx_requests;
    uint32_t rx_completed;
    uint32_t lbt_backoffs;
};

/* ------------------------------------------------------------------ */
/*  Scheduler event callbacks (device → caller, async)                  */
/* ------------------------------------------------------------------ */

struct opener_scheduler_event_ops {
    uint32_t version;

    /** Called when a previously scheduled TX completes (any outcome). */
    void (*on_tx_complete)(void *ctx, const struct opener_tx_result *result);

    /** Called when a scheduled RX window yields a received packet. */
    void (*on_rx_received)(void *ctx, const struct opener_rx_result *result);
};

#endif /* OPENER_SCHEDULER_H */
```

### 3.2 RPC Wire Protocol Constants (`opener_rpc.h`)

```c
#ifndef OPENER_RPC_H
#define OPENER_RPC_H

#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Frame layout                                                        */
/* ------------------------------------------------------------------ */
/*
 * Every SPI transfer exchanges exactly OPENER_RPC_FRAME_SIZE bytes.
 * Unused bytes are zero-padded. A frame with magic == 0x00 is a NOOP
 * (no data to send on this side of the transfer).
 *
 *  Offset  Size  Field
 *  ------  ----  -----------------------------------------
 *    0      1    MAGIC   (0xDE for valid frame, 0x00 for NOOP)
 *    1      1    FLAGS   (see OPENER_RPC_FLAG_*)
 *    2      1    MOD_ID  (see OPENER_RPC_MOD_*)
 *    3      1    PROC_ID (procedure or event identifier)
 *    4      2    SEQ     (sequence number, big-endian)
 *    6      2    PLEN    (payload length in bytes, big-endian)
 *    8      var  PAYLOAD (serialized arguments or return values)
 *    8+PLEN 2    CRC16   (CRC-16/CCITT over bytes 0..8+PLEN-1)
 */

#define OPENER_RPC_MAGIC              0xDE
#define OPENER_RPC_FRAME_SIZE         512U   /* Fixed transfer size, bytes */
#define OPENER_RPC_HEADER_SIZE        8U
#define OPENER_RPC_CRC_SIZE           2U
#define OPENER_RPC_MAX_PAYLOAD        (OPENER_RPC_FRAME_SIZE \
                                     - OPENER_RPC_HEADER_SIZE \
                                     - OPENER_RPC_CRC_SIZE)  /* 502 bytes */

/* FLAGS bits */
#define OPENER_RPC_FLAG_RESPONSE      (1U << 0)  /* Frame is a response   */
#define OPENER_RPC_FLAG_EVENT         (1U << 1)  /* Frame is an async event */
#define OPENER_RPC_FLAG_ERROR         (1U << 2)  /* Payload is error code  */

/* Module identifiers */
#define OPENER_RPC_MOD_RPC            0x00  /* RPC layer (discovery, ping) */
#define OPENER_RPC_MOD_SCHEDULER      0x01
#define OPENER_RPC_MOD_SPECTRUM       0x02
#define OPENER_RPC_MOD_ASSOC          0x03
#define OPENER_RPC_MOD_BEACON         0x04
#define OPENER_RPC_MOD_HARQ           0x05
#define OPENER_RPC_MOD_SECURITY       0x06
#define OPENER_RPC_MOD_PAGING         0x07
#define OPENER_RPC_MOD_MOBILITY       0x08
#define OPENER_RPC_MOD_DLC            0x10
#define OPENER_RPC_MOD_CVG            0x11

/* Scheduler procedure identifiers */
#define OPENER_RPC_SCHED_SCHEDULE_TX  0x01  /* host → device */
#define OPENER_RPC_SCHED_SCHEDULE_RX  0x02  /* host → device */
#define OPENER_RPC_SCHED_CANCEL       0x03  /* host → device */
#define OPENER_RPC_SCHED_GET_STATS    0x04  /* host → device */

/* Scheduler event identifiers (device → host, FLAG_EVENT set) */
#define OPENER_RPC_SCHED_EVT_TX_COMPLETE  0x01
#define OPENER_RPC_SCHED_EVT_RX_RECEIVED  0x02

/* RPC-layer procedure identifiers (MOD_ID = OPENER_RPC_MOD_RPC) */
#define OPENER_RPC_PROC_DISCOVER      0x01  /* Query active modules       */
#define OPENER_RPC_PROC_PING          0x02  /* Liveness check             */

/* Error codes carried in FLAG_ERROR response frames */
#define OPENER_RPC_ERR_UNKNOWN_MODULE 0x01
#define OPENER_RPC_ERR_UNKNOWN_PROC   0x02
#define OPENER_RPC_ERR_BAD_PAYLOAD    0x03
#define OPENER_RPC_ERR_MODULE_BUSY    0x04

/* ------------------------------------------------------------------ */
/*  Frame header struct (always big-endian on wire)                     */
/* ------------------------------------------------------------------ */

struct opener_rpc_header {
    uint8_t  magic;
    uint8_t  flags;
    uint8_t  mod_id;
    uint8_t  proc_id;
    uint16_t seq;    /* big-endian */
    uint16_t plen;   /* big-endian */
} __attribute__((packed));

/* ------------------------------------------------------------------ */
/*  Serialized PDU definitions (shared between device and host)         */
/* ------------------------------------------------------------------ */

/* schedule_tx call payload (host → device) */
struct opener_rpc_schedule_tx_req {
    uint8_t  channel;
    uint8_t  subslot_start;
    uint8_t  subslot_count;
    uint8_t  mcs;
    uint8_t  priority;
    uint8_t  _pad;
    uint16_t earliest_sfn;   /* big-endian */
    uint16_t latest_sfn;     /* big-endian */
    uint16_t payload_len;    /* big-endian */
    uint8_t  payload[];      /* flexible array, length = payload_len */
} __attribute__((packed));

/* schedule_tx response payload (device → host) */
struct opener_rpc_schedule_tx_resp {
    int32_t  result;          /* 0 = ok, negative errno */
    uint32_t handle;          /* big-endian */
} __attribute__((packed));

/* schedule_rx call payload (host → device) */
struct opener_rpc_schedule_rx_req {
    uint8_t  channel;
    uint8_t  subslot_start;
    uint8_t  subslot_count;
    uint8_t  _pad;
    uint16_t sfn;             /* big-endian */
    uint32_t peer_short_id;   /* big-endian */
} __attribute__((packed));

/* cancel call payload (host → device) */
struct opener_rpc_cancel_req {
    uint32_t handle;          /* big-endian */
} __attribute__((packed));

/* on_tx_complete event payload (device → host) */
struct opener_rpc_tx_complete_event {
    uint32_t handle;          /* big-endian */
    uint8_t  status;          /* OPENER_TX_STATUS_* */
    uint8_t  harq_retries;
    int8_t   rssi_dbm;
    uint8_t  _pad;
} __attribute__((packed));

/* on_rx_received event payload (device → host) */
struct opener_rpc_rx_received_event {
    uint32_t handle;          /* big-endian */
    uint8_t  channel;
    uint8_t  subslot;
    int8_t   rssi_dbm;
    uint8_t  _pad;
    uint16_t payload_len;     /* big-endian */
    uint8_t  payload[];
} __attribute__((packed));

/* get_stats response payload (device → host) */
struct opener_rpc_stats_resp {
    int32_t  result;
    uint32_t tx_requests;
    uint32_t tx_completed_ok;
    uint32_t tx_completed_nack;
    uint32_t tx_cancelled;
    uint32_t rx_requests;
    uint32_t rx_completed;
    uint32_t lbt_backoffs;
} __attribute__((packed));

#endif /* OPENER_RPC_H */
```

---

## 4. Wire Protocol

### 4.1 Transfer model

Every SPI transfer exchanges exactly `OPENER_RPC_FRAME_SIZE` (512) bytes, full-duplex. Both sides always provide exactly that many bytes: real frames are header + payload + CRC16, zero-padded to 512; frames with `magic == 0x00` are NOOPs (the sending side has nothing to say this round).

This fixed-size model simplifies DMA buffer management on both the MCU and Linux sides, and eliminates any ambiguity about transfer boundaries.

```
 Host TX buffer (512 B)        Device TX buffer (512 B)
 ┌──────────────────────┐      ┌──────────────────────┐
 │ [header][payload][crc│  ←→  │ [header][payload][crc│
 │ ..zero-padding...... │      │ ..zero-padding...... │
 └──────────────────────┘      └──────────────────────┘
           └──── single SPI_IOC_MESSAGE ioctl ────┘
```

### 4.2 Synchronous call flow (host → device)

```
  Host                          Device
   │                              │
   │─── SPI transfer ────────────►│  TX: schedule_tx request
   │◄────────────────── SPI ──────│  RX: NOOP (or a pending event)
   │                              │  (device processes request)
   │         IRQ GPIO ◄───────────│  device asserts IRQ: response ready
   │   (epoll wakes up)           │
   │─── SPI transfer ────────────►│  TX: NOOP
   │◄────────────────── SPI ──────│  RX: schedule_tx response (handle)
   │                              │
```

If the device processes the request fast enough (sub-microsecond), it MAY pre-load the response and the host MAY receive it in the same transfer round-trip. The protocol is designed to handle both cases: the host always checks both the first and any subsequent received frame for the matching sequence number.

### 4.3 Asynchronous event flow (device → host)

```
  Host                          Device
   │                              │
   │        IRQ GPIO ◄────────────│  TX complete → load event frame
   │   (epoll wakes up)           │  → assert IRQ GPIO
   │─── SPI transfer ────────────►│  TX: NOOP
   │◄────────────────── SPI ──────│  RX: on_tx_complete event frame
   │                              │
   │  dispatch to user callback   │
```

### 4.4 Sequence numbers

The host maintains a 16-bit sequence counter, incrementing by 1 per call. Responses carry the same sequence number as the request. Events carry a device-side sequence number, starting at 0 on boot and wrapping. The host uses the sequence number to match responses to pending calls and to detect dropped events.

### 4.5 CRC

CRC-16/CCITT (polynomial 0x1021, initial value 0xFFFF) over the entire frame from `magic` through the last byte of the payload. The two CRC bytes are appended immediately after the payload, big-endian.

---

## 5. Device-Side Implementation (Zephyr)

### 5.1 Directory layout

```
subsys/opener/rpc/
├── opener_rpc_server.c       # SPI slave driver, frame RX/TX, dispatch
├── opener_rpc_server.h
├── modules/
│   ├── opener_rpc_scheduler.c  # Scheduler RPC handler (dispatch + event push)
│   └── ...
```

### 5.2 RPC server core (`opener_rpc_server.c`)

```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "opener_rpc.h"
#include "opener_rpc_server.h"

LOG_MODULE_REGISTER(opener_rpc_server, CONFIG_OPENER_RPC_LOG_LEVEL);

/* ------------------------------------------------------------------ */
/*  SPI and GPIO configuration (from devicetree)                        */
/* ------------------------------------------------------------------ */

static const struct device *spi_dev =
    DEVICE_DT_GET(DT_NODELABEL(spi1));

static const struct spi_config spi_cfg = {
    .frequency = 8000000U,
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_SLAVE,
};

static const struct gpio_dt_spec irq_gpio =
    GPIO_DT_SPEC_GET(DT_NODELABEL(rpc_irq_gpio), gpios);

/* ------------------------------------------------------------------ */
/*  Module handler registry                                             */
/* ------------------------------------------------------------------ */

typedef int (*opener_rpc_handler_fn)(const uint8_t *payload, uint16_t plen,
                                   uint8_t *resp_buf, uint16_t *resp_len);

struct opener_rpc_module_entry {
    uint8_t               mod_id;
    uint8_t               proc_id;
    opener_rpc_handler_fn   handler;
};

/* Populated by opener_rpc_server_register_handler(). */
static struct opener_rpc_module_entry handlers[32];
static uint8_t handler_count;

int opener_rpc_server_register_handler(uint8_t mod_id, uint8_t proc_id,
                                     opener_rpc_handler_fn fn)
{
    if (handler_count >= ARRAY_SIZE(handlers)) {
        return -ENOMEM;
    }
    handlers[handler_count].mod_id  = mod_id;
    handlers[handler_count].proc_id = proc_id;
    handlers[handler_count].handler = fn;
    handler_count++;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  TX queue: frames the device wants to push to the host               */
/* ------------------------------------------------------------------ */

K_MSGQ_DEFINE(rpc_tx_queue, OPENER_RPC_FRAME_SIZE, 8, 4);

/*
 * Push a frame into the TX queue and assert the IRQ GPIO so the host
 * knows to initiate a transfer to collect it.
 */
int opener_rpc_server_push(const uint8_t *frame, size_t len)
{
    uint8_t buf[OPENER_RPC_FRAME_SIZE];

    if (len > OPENER_RPC_FRAME_SIZE) {
        return -EMSGSIZE;
    }

    memcpy(buf, frame, len);
    memset(buf + len, 0, OPENER_RPC_FRAME_SIZE - len);

    int ret = k_msgq_put(&rpc_tx_queue, buf, K_NO_WAIT);
    if (ret == 0) {
        gpio_pin_set_dt(&irq_gpio, 1);
    }
    return ret;
}

/* ------------------------------------------------------------------ */
/*  Frame helpers                                                       */
/* ------------------------------------------------------------------ */

static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i] << 8);
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static bool frame_crc_valid(const uint8_t *frame, uint16_t plen)
{
    size_t data_len = OPENER_RPC_HEADER_SIZE + plen;
    uint16_t expected = crc16_ccitt(frame, data_len);
    uint16_t actual = ((uint16_t)frame[data_len] << 8) |
                       (uint16_t)frame[data_len + 1];
    return expected == actual;
}

static void frame_build(uint8_t *buf, uint8_t flags,
                        uint8_t mod_id, uint8_t proc_id,
                        uint16_t seq, const uint8_t *payload, uint16_t plen)
{
    struct opener_rpc_header *hdr = (struct opener_rpc_header *)buf;

    memset(buf, 0, OPENER_RPC_FRAME_SIZE);

    hdr->magic   = OPENER_RPC_MAGIC;
    hdr->flags   = flags;
    hdr->mod_id  = mod_id;
    hdr->proc_id = proc_id;
    hdr->seq     = sys_cpu_to_be16(seq);
    hdr->plen    = sys_cpu_to_be16(plen);

    if (payload != NULL && plen > 0) {
        memcpy(buf + OPENER_RPC_HEADER_SIZE, payload, plen);
    }

    uint16_t crc = crc16_ccitt(buf, OPENER_RPC_HEADER_SIZE + plen);
    buf[OPENER_RPC_HEADER_SIZE + plen]     = (uint8_t)(crc >> 8);
    buf[OPENER_RPC_HEADER_SIZE + plen + 1] = (uint8_t)(crc & 0xFF);
}

/* ------------------------------------------------------------------ */
/*  RPC server thread                                                   */
/* ------------------------------------------------------------------ */

static uint8_t rx_buf[OPENER_RPC_FRAME_SIZE];
static uint8_t tx_buf[OPENER_RPC_FRAME_SIZE];

static void rpc_server_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    struct spi_buf rx = { .buf = rx_buf, .len = OPENER_RPC_FRAME_SIZE };
    struct spi_buf tx = { .buf = tx_buf, .len = OPENER_RPC_FRAME_SIZE };
    struct spi_buf_set rx_set = { .buffers = &rx, .count = 1 };
    struct spi_buf_set tx_set = { .buffers = &tx, .count = 1 };

    LOG_INF("RPC server started");

    while (true) {
        /* Pre-load TX buffer: use queued frame or NOOP. */
        if (k_msgq_get(&rpc_tx_queue, tx_buf, K_NO_WAIT) != 0) {
            memset(tx_buf, 0, OPENER_RPC_FRAME_SIZE);
        }

        /* Block waiting for the host to initiate a transfer. */
        int ret = spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
        if (ret != 0) {
            LOG_ERR("SPI transceive error: %d", ret);
            continue;
        }

        /* De-assert IRQ if TX queue is now empty. */
        if (k_msgq_num_used_get(&rpc_tx_queue) == 0) {
            gpio_pin_set_dt(&irq_gpio, 0);
        }

        /* Ignore NOOP frames from host. */
        if (rx_buf[0] != OPENER_RPC_MAGIC) {
            continue;
        }

        const struct opener_rpc_header *hdr =
            (const struct opener_rpc_header *)rx_buf;
        uint16_t seq  = sys_be16_to_cpu(hdr->seq);
        uint16_t plen = sys_be16_to_cpu(hdr->plen);

        /* Validate CRC. */
        if (!frame_crc_valid(rx_buf, plen)) {
            LOG_WRN("CRC error on frame seq=%u", seq);
            continue;
        }

        /* Look up handler. */
        opener_rpc_handler_fn handler = NULL;
        for (uint8_t i = 0; i < handler_count; i++) {
            if (handlers[i].mod_id  == hdr->mod_id &&
                handlers[i].proc_id == hdr->proc_id) {
                handler = handlers[i].handler;
                break;
            }
        }

        uint8_t  resp_payload[OPENER_RPC_MAX_PAYLOAD];
        uint16_t resp_len = 0;
        uint8_t  resp_flags = OPENER_RPC_FLAG_RESPONSE;

        if (handler == NULL) {
            LOG_WRN("No handler: mod=0x%02x proc=0x%02x",
                    hdr->mod_id, hdr->proc_id);
            uint8_t err = OPENER_RPC_ERR_UNKNOWN_PROC;
            resp_flags |= OPENER_RPC_FLAG_ERROR;
            memcpy(resp_payload, &err, 1);
            resp_len = 1;
        } else {
            const uint8_t *payload = rx_buf + OPENER_RPC_HEADER_SIZE;
            ret = handler(payload, plen, resp_payload, &resp_len);
            if (ret != 0) {
                resp_flags |= OPENER_RPC_FLAG_ERROR;
            }
        }

        /* Build and enqueue the response frame. */
        uint8_t resp_frame[OPENER_RPC_FRAME_SIZE];
        frame_build(resp_frame, resp_flags, hdr->mod_id, hdr->proc_id,
                    seq, resp_payload, resp_len);
        opener_rpc_server_push(resp_frame, OPENER_RPC_FRAME_SIZE);
    }
}

K_THREAD_DEFINE(rpc_server, 2048,
                rpc_server_thread, NULL, NULL, NULL,
                CONFIG_OPENER_RPC_THREAD_PRIORITY, 0, 0);
```

### 5.3 Scheduler RPC handler (`opener_rpc_scheduler.c`)

```c
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>

#include "opener_rpc.h"
#include "opener_rpc_server.h"
#include "opener_scheduler.h"

/* Reference to the real on-device scheduler instance. */
extern struct opener_scheduler_ops  g_scheduler_ops;
extern void                      *g_scheduler_ctx;

/* ------------------------------------------------------------------ */
/*  Incoming call handlers (host → device)                              */
/* ------------------------------------------------------------------ */

static int handle_schedule_tx(const uint8_t *payload, uint16_t plen,
                               uint8_t *resp_buf, uint16_t *resp_len)
{
    if (plen < sizeof(struct opener_rpc_schedule_tx_req)) {
        return -EINVAL;
    }

    const struct opener_rpc_schedule_tx_req *req =
        (const struct opener_rpc_schedule_tx_req *)payload;

    struct opener_tx_request tx_req = {
        .channel       = req->channel,
        .subslot_start = req->subslot_start,
        .subslot_count = req->subslot_count,
        .mcs           = req->mcs,
        .priority      = req->priority,
        .earliest_sfn  = sys_be16_to_cpu(req->earliest_sfn),
        .latest_sfn    = sys_be16_to_cpu(req->latest_sfn),
        .payload       = req->payload,
        .payload_len   = sys_be16_to_cpu(req->payload_len),
    };

    opener_sched_handle_t handle;
    int ret = g_scheduler_ops.schedule_tx(g_scheduler_ctx, &tx_req, &handle);

    struct opener_rpc_schedule_tx_resp *resp =
        (struct opener_rpc_schedule_tx_resp *)resp_buf;
    resp->result = sys_cpu_to_be32((uint32_t)(int32_t)ret);
    resp->handle = sys_cpu_to_be32(handle);
    *resp_len = sizeof(*resp);

    return ret;
}

static int handle_schedule_rx(const uint8_t *payload, uint16_t plen,
                               uint8_t *resp_buf, uint16_t *resp_len)
{
    if (plen < sizeof(struct opener_rpc_schedule_rx_req)) {
        return -EINVAL;
    }

    const struct opener_rpc_schedule_rx_req *req =
        (const struct opener_rpc_schedule_rx_req *)payload;

    struct opener_rx_request rx_req = {
        .channel       = req->channel,
        .subslot_start = req->subslot_start,
        .subslot_count = req->subslot_count,
        .sfn           = sys_be16_to_cpu(req->sfn),
        .peer_short_id = sys_be32_to_cpu(req->peer_short_id),
    };

    opener_sched_handle_t handle;
    int ret = g_scheduler_ops.schedule_rx(g_scheduler_ctx, &rx_req, &handle);

    struct opener_rpc_schedule_tx_resp *resp =   /* reuse: same layout */
        (struct opener_rpc_schedule_tx_resp *)resp_buf;
    resp->result = sys_cpu_to_be32((uint32_t)(int32_t)ret);
    resp->handle = sys_cpu_to_be32(handle);
    *resp_len = sizeof(*resp);

    return ret;
}

static int handle_cancel(const uint8_t *payload, uint16_t plen,
                         uint8_t *resp_buf, uint16_t *resp_len)
{
    if (plen < sizeof(struct opener_rpc_cancel_req)) {
        return -EINVAL;
    }

    const struct opener_rpc_cancel_req *req =
        (const struct opener_rpc_cancel_req *)payload;

    int ret = g_scheduler_ops.cancel(g_scheduler_ctx,
                                     sys_be32_to_cpu(req->handle));

    int32_t result_be = sys_cpu_to_be32((uint32_t)(int32_t)ret);
    memcpy(resp_buf, &result_be, sizeof(result_be));
    *resp_len = sizeof(result_be);
    return ret;
}

static int handle_get_stats(const uint8_t *payload, uint16_t plen,
                             uint8_t *resp_buf, uint16_t *resp_len)
{
    ARG_UNUSED(payload); ARG_UNUSED(plen);

    struct opener_scheduler_stats stats;
    int ret = g_scheduler_ops.get_stats(g_scheduler_ctx, &stats);

    struct opener_rpc_stats_resp *resp =
        (struct opener_rpc_stats_resp *)resp_buf;
    resp->result             = sys_cpu_to_be32((uint32_t)(int32_t)ret);
    resp->tx_requests        = sys_cpu_to_be32(stats.tx_requests);
    resp->tx_completed_ok    = sys_cpu_to_be32(stats.tx_completed_ok);
    resp->tx_completed_nack  = sys_cpu_to_be32(stats.tx_completed_nack);
    resp->tx_cancelled       = sys_cpu_to_be32(stats.tx_cancelled);
    resp->rx_requests        = sys_cpu_to_be32(stats.rx_requests);
    resp->rx_completed       = sys_cpu_to_be32(stats.rx_completed);
    resp->lbt_backoffs       = sys_cpu_to_be32(stats.lbt_backoffs);
    *resp_len = sizeof(*resp);
    return ret;
}

/* ------------------------------------------------------------------ */
/*  Outgoing event (device → host): called by the real scheduler        */
/* ------------------------------------------------------------------ */

static void push_tx_complete_event(void *ctx,
                                   const struct opener_tx_result *result)
{
    ARG_UNUSED(ctx);

    uint8_t frame[OPENER_RPC_FRAME_SIZE];
    struct opener_rpc_tx_complete_event evt = {
        .handle      = sys_cpu_to_be32(result->handle),
        .status      = result->status,
        .harq_retries = result->harq_retries,
        .rssi_dbm    = result->rssi_dbm,
    };

    /* Reuse a static sequence counter for device-originated events. */
    static uint16_t evt_seq;

    frame_build(frame,
                OPENER_RPC_FLAG_EVENT,
                OPENER_RPC_MOD_SCHEDULER,
                OPENER_RPC_SCHED_EVT_TX_COMPLETE,
                evt_seq++,
                (const uint8_t *)&evt, sizeof(evt));

    opener_rpc_server_push(frame, OPENER_RPC_FRAME_SIZE);
}

static void push_rx_received_event(void *ctx,
                                   const struct opener_rx_result *result)
{
    ARG_UNUSED(ctx);

    /* Variable-length: header + fixed fields + payload. */
    uint8_t evt_buf[sizeof(struct opener_rpc_rx_received_event) + 256];
    struct opener_rpc_rx_received_event *evt =
        (struct opener_rpc_rx_received_event *)evt_buf;

    evt->handle      = sys_cpu_to_be32(result->handle);
    evt->channel     = result->channel;
    evt->subslot     = result->subslot;
    evt->rssi_dbm    = result->rssi_dbm;
    evt->_pad        = 0;
    evt->payload_len = sys_cpu_to_be16(result->payload_len);
    memcpy(evt->payload, result->payload, result->payload_len);

    uint16_t evt_len = (uint16_t)(sizeof(struct opener_rpc_rx_received_event)
                                  + result->payload_len);

    uint8_t frame[OPENER_RPC_FRAME_SIZE];
    static uint16_t evt_seq;

    frame_build(frame,
                OPENER_RPC_FLAG_EVENT,
                OPENER_RPC_MOD_SCHEDULER,
                OPENER_RPC_SCHED_EVT_RX_RECEIVED,
                evt_seq++,
                evt_buf, evt_len);

    opener_rpc_server_push(frame, OPENER_RPC_FRAME_SIZE);
}

/*
 * The scheduler event ops struct that the real scheduler calls into.
 * This bridges the real scheduler to the RPC push mechanism.
 */
struct opener_scheduler_event_ops g_sched_rpc_event_ops = {
    .version        = OPENER_SCHEDULER_API_VERSION,
    .on_tx_complete = push_tx_complete_event,
    .on_rx_received = push_rx_received_event,
};

/* ------------------------------------------------------------------ */
/*  Registration                                                        */
/* ------------------------------------------------------------------ */

int opener_rpc_scheduler_init(void)
{
    int ret;

    ret = opener_rpc_server_register_handler(OPENER_RPC_MOD_SCHEDULER,
              OPENER_RPC_SCHED_SCHEDULE_TX, handle_schedule_tx);
    ret |= opener_rpc_server_register_handler(OPENER_RPC_MOD_SCHEDULER,
              OPENER_RPC_SCHED_SCHEDULE_RX, handle_schedule_rx);
    ret |= opener_rpc_server_register_handler(OPENER_RPC_MOD_SCHEDULER,
              OPENER_RPC_SCHED_CANCEL, handle_cancel);
    ret |= opener_rpc_server_register_handler(OPENER_RPC_MOD_SCHEDULER,
              OPENER_RPC_SCHED_GET_STATS, handle_get_stats);

    return ret;
}
```

---

## 6. Host-Side Implementation (Linux Kernel Module)

### 6.1 Directory layout

```
opener_host/                      (DKMS module root)
├── Kbuild
├── dkms.conf
├── opener_main.c                 # spi_driver probe/remove, IRQ, module init
├── opener_priv.h                 # private data structure shared across files
├── opener_rpc.c                  # RPC frame build/parse, TX/RX work queues
├── opener_rpc.h
├── opener_netdev.c               # net_device, ndo_* ops, IPv6 profile
├── opener_netdev.h
├── opener_netlink.c              # Generic Netlink UAPI (OPENER family)
├── opener_netlink.h
└── opener_scheduler.c            # Scheduler RPC proxy using kernel ops struct
```

The `common/` headers (`opener_rpc.h`, `opener_scheduler.h`) are compiled on both the device and the host module.

### 6.2 Private data structure (`opener_priv.h`)

One `struct opener_priv` per SPI device instance, allocated via `alloc_netdev` and accessed through `netdev_priv`.

```c
#ifndef OPENER_PRIV_H
#define OPENER_PRIV_H

#include <linux/spi/spi.h>
#include <linux/netdevice.h>
#include <linux/gpio/consumer.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include "../../common/opener_rpc.h"

#define OPENER_MAX_EVENT_HANDLERS  16

typedef void (*opener_event_handler_fn)(
    const struct opener_rpc_header *hdr,
    const u8 *payload, u16 plen, void *ctx);

struct opener_priv {
    struct spi_device        *spi;
    struct net_device        *netdev;
    struct gpio_desc         *irq_gpio;
    int                       irq;

    struct workqueue_struct  *wq;
    struct work_struct        rx_work;   /* fired by IRQ bottom half        */
    struct work_struct        tx_work;   /* drains netdev TX queue          */

    struct sk_buff_head       tx_queue;  /* sk_buffs queued for TX          */

    /*
     * Synchronous RPC call state.
     *
     * call_mutex  — serializes concurrent callers (only one in-flight call).
     * resp_lock   — spinlock protecting call_seq / call_resp / call_pending;
     *               taken from both kernel-thread context (rpc_call) and
     *               workqueue context (rx_work → process_frame).
     * call_done   — completion signalled by process_frame when the matching
     *               response arrives.
     */
    struct mutex              call_mutex;
    spinlock_t                resp_lock;
    struct completion         call_done;
    u16                       call_seq;
    u8                        call_resp[OPENER_RPC_MAX_PAYLOAD];
    u16                       call_resp_len;
    int                       call_result;
    bool                      call_pending;

    u16                       seq_counter;

    struct {
        u8                        mod_id;
        opener_event_handler_fn  fn;
        void                     *ctx;
    } ev_handlers[OPENER_MAX_EVENT_HANDLERS];
    int ev_handler_count;
};

#endif /* OPENER_PRIV_H */
```

### 6.3 SPI driver and IRQ (`opener_main.c`)

```c
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include "opener_priv.h"
#include "opener_rpc.h"
#include "opener_netdev.h"
#include "opener_netlink.h"

/*
 * IRQ top half: just wake the RX work queue.
 * The bottom half (opener_rx_work) performs the SPI transfer.
 */
static irqreturn_t opener_irq_handler(int irq, void *dev_id)
{
    struct opener_priv *priv = dev_id;

    queue_work(priv->wq, &priv->rx_work);
    return IRQ_HANDLED;
}

static int opener_spi_probe(struct spi_device *spi)
{
    struct net_device *netdev;
    struct opener_priv *priv;
    int ret;

    netdev = alloc_netdev(sizeof(*priv), "opener%d",
                          NET_NAME_ENUM, opener_netdev_setup);
    if (!netdev)
        return -ENOMEM;

    priv = netdev_priv(netdev);
    priv->spi    = spi;
    priv->netdev = netdev;
    spi_set_drvdata(spi, priv);

    mutex_init(&priv->call_mutex);
    spin_lock_init(&priv->resp_lock);
    init_completion(&priv->call_done);
    skb_queue_head_init(&priv->tx_queue);

    priv->wq = alloc_workqueue("opener_%s", WQ_HIGHPRI | WQ_MEM_RECLAIM,
                                0, dev_name(&spi->dev));
    if (!priv->wq) { ret = -ENOMEM; goto err_netdev; }

    INIT_WORK(&priv->rx_work, opener_rx_work);
    INIT_WORK(&priv->tx_work, opener_tx_work);

    /* "irq-gpios" property in the devicetree node. */
    priv->irq_gpio = devm_gpiod_get(&spi->dev, "irq", GPIOD_IN);
    if (IS_ERR(priv->irq_gpio)) {
        ret = PTR_ERR(priv->irq_gpio);
        goto err_wq;
    }

    priv->irq = gpiod_to_irq(priv->irq_gpio);
    ret = request_irq(priv->irq, opener_irq_handler,
                      IRQF_TRIGGER_RISING, "opener", priv);
    if (ret)
        goto err_wq;

    ret = register_netdev(netdev);
    if (ret)
        goto err_irq;

    ret = opener_netlink_init(priv);
    if (ret)
        goto err_register;

    dev_info(&spi->dev, "opener host driver probed (%s)\n", netdev->name);
    return 0;

err_register: unregister_netdev(netdev);
err_irq:      free_irq(priv->irq, priv);
err_wq:       destroy_workqueue(priv->wq);
err_netdev:   free_netdev(netdev);
    return ret;
}

static void opener_spi_remove(struct spi_device *spi)
{
    struct opener_priv *priv = spi_get_drvdata(spi);

    opener_netlink_exit(priv);
    unregister_netdev(priv->netdev);
    free_irq(priv->irq, priv);
    flush_workqueue(priv->wq);
    destroy_workqueue(priv->wq);
    skb_queue_purge(&priv->tx_queue);
    free_netdev(priv->netdev);
}

static const struct of_device_id opener_of_match[] = {
    { .compatible = "nordic,opener-rpc" },
    { }
};
MODULE_DEVICE_TABLE(of, opener_of_match);

static struct spi_driver opener_spi_driver = {
    .driver = {
        .name           = "opener",
        .of_match_table = opener_of_match,
    },
    .probe  = opener_spi_probe,
    .remove = opener_spi_remove,
};
module_spi_driver(opener_spi_driver);

MODULE_AUTHOR("...");
MODULE_DESCRIPTION("opener host driver over SPI RPC");
MODULE_LICENSE("GPL");
```

The corresponding devicetree node on the Linux side:

```dts
&spi0 {
    opener: opener@0 {
        compatible = "nordic,opener-rpc";
        reg = <0>;
        spi-max-frequency = <8000000>;
        irq-gpios = <&gpio0 17 GPIO_ACTIVE_HIGH>;
    };
};
```

### 6.4 RPC frame I/O (`opener_rpc.c`)

This replaces `opener_rpc_host.c`. The SPI transfer uses `spi_sync` (blocking, serialized by the kernel SPI bus lock). Async event handling moves from `epoll + pthread` to an IRQ top half + workqueue bottom half.

```c
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include "opener_priv.h"
#include "opener_rpc.h"
#include "opener_netdev.h"
#include "../../common/opener_rpc.h"

/* ------------------------------------------------------------------ */
/*  CRC-16/CCITT (polynomial 0x1021, init 0xFFFF)                      */
/* ------------------------------------------------------------------ */

static u16 crc16_ccitt(const u8 *data, size_t len)
{
    u16 crc = 0xFFFF;

    while (len--) {
        crc ^= ((u16)(*data++) << 8);
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (u16)((crc << 1) ^ 0x1021) : (u16)(crc << 1);
    }
    return crc;
}

/* ------------------------------------------------------------------ */
/*  SPI transfer (always 512 bytes, full-duplex)                        */
/* ------------------------------------------------------------------ */

static int opener_spi_xfer(struct opener_priv *priv,
                             const u8 *tx_buf, u8 *rx_buf)
{
    struct spi_transfer xfer = {
        .tx_buf   = tx_buf,
        .rx_buf   = rx_buf,
        .len      = OPENER_RPC_FRAME_SIZE,
        .speed_hz = 8000000U,
    };
    struct spi_message msg;

    spi_message_init(&msg);
    spi_message_add_tail(&xfer, &msg);
    return spi_sync(priv->spi, &msg);
}

/* ------------------------------------------------------------------ */
/*  Frame builder                                                       */
/* ------------------------------------------------------------------ */

static void frame_build(u8 *buf, u8 flags, u8 mod_id, u8 proc_id,
                        u16 seq, const u8 *payload, u16 plen)
{
    struct opener_rpc_header *hdr = (struct opener_rpc_header *)buf;
    u16 crc;

    memset(buf, 0, OPENER_RPC_FRAME_SIZE);
    hdr->magic   = OPENER_RPC_MAGIC;
    hdr->flags   = flags;
    hdr->mod_id  = mod_id;
    hdr->proc_id = proc_id;
    hdr->seq     = cpu_to_be16(seq);
    hdr->plen    = cpu_to_be16(plen);

    if (payload && plen)
        memcpy(buf + OPENER_RPC_HEADER_SIZE, payload, plen);

    crc = crc16_ccitt(buf, OPENER_RPC_HEADER_SIZE + plen);
    buf[OPENER_RPC_HEADER_SIZE + plen]     = (u8)(crc >> 8);
    buf[OPENER_RPC_HEADER_SIZE + plen + 1] = (u8)(crc & 0xFF);
}

/* ------------------------------------------------------------------ */
/*  Frame processing (called from both rx_work and rpc_call contexts)   */
/* ------------------------------------------------------------------ */

static void process_frame(struct opener_priv *priv, const u8 *buf)
{
    const struct opener_rpc_header *hdr;
    u16 seq, plen, crc_exp, crc_got;
    const u8 *payload;
    int i;
    bool wake = false;

    if (buf[0] != OPENER_RPC_MAGIC)
        return;

    hdr  = (const struct opener_rpc_header *)buf;
    seq  = be16_to_cpu(hdr->seq);
    plen = be16_to_cpu(hdr->plen);

    if (plen > OPENER_RPC_MAX_PAYLOAD) {
        dev_warn(&priv->spi->dev, "RPC frame plen overflow: %u\n", plen);
        return;
    }

    crc_exp = crc16_ccitt(buf, OPENER_RPC_HEADER_SIZE + plen);
    crc_got = ((u16)buf[OPENER_RPC_HEADER_SIZE + plen] << 8) |
               buf[OPENER_RPC_HEADER_SIZE + plen + 1];
    if (crc_exp != crc_got) {
        dev_warn(&priv->spi->dev, "RPC CRC error seq=%u\n", seq);
        return;
    }

    payload = buf + OPENER_RPC_HEADER_SIZE;

    if (hdr->flags & OPENER_RPC_FLAG_EVENT) {
        for (i = 0; i < priv->ev_handler_count; i++) {
            if (priv->ev_handlers[i].mod_id == hdr->mod_id)
                priv->ev_handlers[i].fn(hdr, payload, plen,
                                        priv->ev_handlers[i].ctx);
        }
        return;
    }

    if (hdr->flags & OPENER_RPC_FLAG_RESPONSE) {
        spin_lock(&priv->resp_lock);
        if (priv->call_pending && priv->call_seq == seq) {
            u16 copy = min_t(u16, plen, OPENER_RPC_MAX_PAYLOAD);
            memcpy(priv->call_resp, payload, copy);
            priv->call_resp_len = copy;
            priv->call_result   = (hdr->flags & OPENER_RPC_FLAG_ERROR)
                                  ? -EIO : 0;
            priv->call_pending  = false;
            wake = true;
        }
        spin_unlock(&priv->resp_lock);
        if (wake)
            complete(&priv->call_done);
    }
}

/* ------------------------------------------------------------------ */
/*  Work queue bottom halves                                            */
/* ------------------------------------------------------------------ */

/*
 * RX work: drain frames from device after IRQ assertion.
 * Continues until the device sends a NOOP (nothing more to deliver).
 */
void opener_rx_work(struct work_struct *work)
{
    struct opener_priv *priv =
        container_of(work, struct opener_priv, rx_work);
    static const u8 tx_noop[OPENER_RPC_FRAME_SIZE]; /* all-zero = NOOP */
    u8 rx_buf[OPENER_RPC_FRAME_SIZE];

    do {
        if (opener_spi_xfer(priv, tx_noop, rx_buf) != 0)
            break;
        process_frame(priv, rx_buf);
    } while (rx_buf[0] == OPENER_RPC_MAGIC);
}

/*
 * TX work: drain the netdev sk_buff queue.
 * Each sk_buff payload is wrapped in a OPENER_RPC_MOD_DLC data-request
 * frame (procedure IDs TBD when the DLC RPC layer is defined).
 */
void opener_tx_work(struct work_struct *work)
{
    struct opener_priv *priv =
        container_of(work, struct opener_priv, tx_work);
    struct sk_buff *skb;
    u8 tx_buf[OPENER_RPC_FRAME_SIZE];
    u8 rx_buf[OPENER_RPC_FRAME_SIZE];

    while ((skb = skb_dequeue(&priv->tx_queue)) != NULL) {
        frame_build(tx_buf, 0,
                    OPENER_RPC_MOD_DLC, OPENER_RPC_DLC_DATA_REQ, /* TBD */
                    priv->seq_counter++,
                    skb->data, (u16)skb->len);

        if (opener_spi_xfer(priv, tx_buf, rx_buf) == 0)
            process_frame(priv, rx_buf);

        priv->netdev->stats.tx_packets++;
        priv->netdev->stats.tx_bytes += skb->len;
        dev_kfree_skb(skb);
    }
}

/* ------------------------------------------------------------------ */
/*  Synchronous RPC call (callable from any non-atomic kernel context)  */
/* ------------------------------------------------------------------ */

int opener_rpc_call(struct opener_priv *priv,
                     u8 mod_id, u8 proc_id,
                     const u8 *req, u16 req_len,
                     u8 *resp, u16 *resp_len,
                     unsigned int timeout_ms)
{
    u8 tx_buf[OPENER_RPC_FRAME_SIZE];
    u8 rx_buf[OPENER_RPC_FRAME_SIZE];
    unsigned long flags;
    u16 seq;
    int ret;

    /* Serialize concurrent callers. */
    if (mutex_lock_interruptible(&priv->call_mutex))
        return -ERESTARTSYS;

    seq = priv->seq_counter++;

    reinit_completion(&priv->call_done);
    spin_lock_irqsave(&priv->resp_lock, flags);
    priv->call_seq     = seq;
    priv->call_pending = true;
    spin_unlock_irqrestore(&priv->resp_lock, flags);

    frame_build(tx_buf, 0, mod_id, proc_id, seq, req, req_len);

    ret = opener_spi_xfer(priv, tx_buf, rx_buf);
    if (ret) {
        spin_lock_irqsave(&priv->resp_lock, flags);
        priv->call_pending = false;
        spin_unlock_irqrestore(&priv->resp_lock, flags);
        mutex_unlock(&priv->call_mutex);
        return ret;
    }

    /* The response may arrive in this very transfer (fast device). */
    process_frame(priv, rx_buf);

    /* Otherwise wait: rx_work will call complete() when IRQ fires. */
    if (!wait_for_completion_timeout(&priv->call_done,
                                     msecs_to_jiffies(timeout_ms))) {
        spin_lock_irqsave(&priv->resp_lock, flags);
        priv->call_pending = false;
        spin_unlock_irqrestore(&priv->resp_lock, flags);
        mutex_unlock(&priv->call_mutex);
        return -ETIMEDOUT;
    }

    if (resp && resp_len) {
        u16 copy = min_t(u16, *resp_len, priv->call_resp_len);
        memcpy(resp, priv->call_resp, copy);
        *resp_len = priv->call_resp_len;
    }
    ret = priv->call_result;
    mutex_unlock(&priv->call_mutex);
    return ret;
}

int opener_rpc_register_event_handler(struct opener_priv *priv,
                                        u8 mod_id,
                                        opener_event_handler_fn fn,
                                        void *ctx)
{
    if (priv->ev_handler_count >= OPENER_MAX_EVENT_HANDLERS)
        return -ENOMEM;
    priv->ev_handlers[priv->ev_handler_count].mod_id = mod_id;
    priv->ev_handlers[priv->ev_handler_count].fn     = fn;
    priv->ev_handlers[priv->ev_handler_count].ctx    = ctx;
    priv->ev_handler_count++;
    return 0;
}
```

### 6.5 Network device (`opener_netdev.c`)

The `net_device` is a point-to-point, ARP-less interface carrying IPv6 frames directly (matching the ETSI TS 103 874-3 IPv6 profile). MTU is set to the IPv6 minimum (1280 bytes) as a safe default; the actual maximum depends on DECT NR subslot allocation and will be configurable via netlink.

```c
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include "opener_priv.h"

static int opener_open(struct net_device *dev)
{
    netif_start_queue(dev);
    return 0;
}

static int opener_stop(struct net_device *dev)
{
    netif_stop_queue(dev);
    return 0;
}

static netdev_tx_t opener_start_xmit(struct sk_buff *skb,
                                       struct net_device *dev)
{
    struct opener_priv *priv = netdev_priv(dev);

    if (unlikely(skb->len > OPENER_RPC_MAX_PAYLOAD)) {
        dev->stats.tx_dropped++;
        dev_kfree_skb(skb);
        return NETDEV_TX_OK;
    }

    skb_queue_tail(&priv->tx_queue, skb);
    queue_work(priv->wq, &priv->tx_work);
    return NETDEV_TX_OK;
}

/*
 * Called from opener_rpc.c when a data-indication RPC event arrives
 * from the device carrying a received IPv6 PDU.
 */
void opener_netdev_rx(struct opener_priv *priv, const u8 *data, u16 len)
{
    struct net_device *dev = priv->netdev;
    struct sk_buff *skb;

    skb = netdev_alloc_skb_ip_align(dev, len);
    if (unlikely(!skb)) {
        dev->stats.rx_dropped++;
        return;
    }

    skb_put_data(skb, data, len);
    skb->protocol = htons(ETH_P_IPV6);
    skb->dev      = dev;
    skb_reset_network_header(skb);

    dev->stats.rx_packets++;
    dev->stats.rx_bytes += len;
    netif_rx(skb);
}

static const struct net_device_ops opener_netdev_ops = {
    .ndo_open       = opener_open,
    .ndo_stop       = opener_stop,
    .ndo_start_xmit = opener_start_xmit,
};

/* Called by alloc_netdev during probe. */
void opener_netdev_setup(struct net_device *dev)
{
    dev->netdev_ops      = &opener_netdev_ops;
    dev->type            = ARPHRD_NONE;
    dev->hard_header_len = 0;
    dev->addr_len        = 0;
    dev->mtu             = 1280;        /* IPv6 minimum; configurable later */
    dev->flags           = IFF_NOARP | IFF_POINTOPOINT;
    dev->features       |= NETIF_F_LLTX;
    dev->needs_free_netdev = true;
}
```

### 6.6 Generic Netlink UAPI (`opener_netlink.c`)

Management operations are exposed through a Generic Netlink family named `OPENER`. Standard tools (`ip link`, `ip -6 addr`, `ip -6 route`) use the `net_device` directly; the netlink family covers DECT-specific operations: scheduler statistics, channel configuration, association state, and link-layer parameters.

```c
#include <linux/genetlink.h>
#include <net/genetlink.h>
#include "opener_priv.h"
#include "opener_rpc.h"
#include "opener_netlink.h"
#include "../../common/opener_scheduler.h"

/* ------------------------------------------------------------------ */
/*  Attribute definitions                                               */
/* ------------------------------------------------------------------ */

enum opener_nl_attr {
    OPENER_ATTR_UNSPEC,
    OPENER_ATTR_IFINDEX,            /* u32 — identifies the opener instance */
    OPENER_ATTR_SCHED_TX_REQUESTS,  /* u32 */
    OPENER_ATTR_SCHED_TX_OK,        /* u32 */
    OPENER_ATTR_SCHED_TX_NACK,      /* u32 */
    OPENER_ATTR_SCHED_TX_CANCELLED, /* u32 */
    OPENER_ATTR_SCHED_RX_REQUESTS,  /* u32 */
    OPENER_ATTR_SCHED_RX_COMPLETED, /* u32 */
    OPENER_ATTR_SCHED_LBT_BACKOFFS, /* u32 */
    OPENER_ATTR_CHANNEL,            /* u8  — operating channel index */
    __OPENER_ATTR_MAX,
};
#define OPENER_ATTR_MAX (__OPENER_ATTR_MAX - 1)

static const struct nla_policy opener_policy[OPENER_ATTR_MAX + 1] = {
    [OPENER_ATTR_IFINDEX]  = { .type = NLA_U32 },
    [OPENER_ATTR_CHANNEL]  = { .type = NLA_U8  },
};

/* ------------------------------------------------------------------ */
/*  Command definitions                                                 */
/* ------------------------------------------------------------------ */

enum opener_nl_cmd {
    OPENER_CMD_UNSPEC,
    OPENER_CMD_GET_STATS,       /* query scheduler statistics */
    OPENER_CMD_SET_CHANNEL,     /* set operating channel       */
    __OPENER_CMD_MAX,
};

/* ------------------------------------------------------------------ */
/*  Helper: resolve priv from OPENER_ATTR_IFINDEX                      */
/* ------------------------------------------------------------------ */

static struct opener_priv *priv_from_info(struct genl_info *info)
{
    struct net_device *dev;
    u32 ifindex;

    if (!info->attrs[OPENER_ATTR_IFINDEX])
        return NULL;

    ifindex = nla_get_u32(info->attrs[OPENER_ATTR_IFINDEX]);
    dev = dev_get_by_index(&init_net, ifindex);
    if (!dev)
        return NULL;

    if (dev->netdev_ops->ndo_start_xmit != opener_start_xmit) {
        dev_put(dev);
        return NULL;
    }

    /* Caller must call dev_put() when done. */
    return netdev_priv(dev);
}

/* ------------------------------------------------------------------ */
/*  Command handlers                                                    */
/* ------------------------------------------------------------------ */

static int opener_nl_get_stats(struct sk_buff *skb, struct genl_info *info)
{
    struct opener_priv *priv;
    struct net_device *dev;
    struct opener_scheduler_stats stats;
    struct sk_buff *reply;
    void *hdr;
    int ret;

    priv = priv_from_info(info);
    if (!priv)
        return -ENODEV;
    dev = priv->netdev;

    ret = opener_sched_get_stats(priv, &stats);
    if (ret) { dev_put(dev); return ret; }

    reply = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
    if (!reply) { dev_put(dev); return -ENOMEM; }

    hdr = genlmsg_put_reply(reply, info, &opener_genl_family,
                             0, OPENER_CMD_GET_STATS);
    if (!hdr) { nlmsg_free(reply); dev_put(dev); return -EMSGSIZE; }

    nla_put_u32(reply, OPENER_ATTR_SCHED_TX_REQUESTS,   stats.tx_requests);
    nla_put_u32(reply, OPENER_ATTR_SCHED_TX_OK,         stats.tx_completed_ok);
    nla_put_u32(reply, OPENER_ATTR_SCHED_TX_NACK,       stats.tx_completed_nack);
    nla_put_u32(reply, OPENER_ATTR_SCHED_TX_CANCELLED,  stats.tx_cancelled);
    nla_put_u32(reply, OPENER_ATTR_SCHED_RX_REQUESTS,   stats.rx_requests);
    nla_put_u32(reply, OPENER_ATTR_SCHED_RX_COMPLETED,  stats.rx_completed);
    nla_put_u32(reply, OPENER_ATTR_SCHED_LBT_BACKOFFS,  stats.lbt_backoffs);

    genlmsg_end(reply, hdr);
    dev_put(dev);
    return genlmsg_reply(reply, info);
}

static int opener_nl_set_channel(struct sk_buff *skb, struct genl_info *info)
{
    /* Channel configuration via RPC — procedure IDs TBD with MAC-SPM. */
    return -ENOSYS;
}

/* ------------------------------------------------------------------ */
/*  Family registration                                                 */
/* ------------------------------------------------------------------ */

static const struct genl_ops opener_genl_ops[] = {
    {
        .cmd    = OPENER_CMD_GET_STATS,
        .doit   = opener_nl_get_stats,
        .flags  = GENL_ADMIN_PERM,
    },
    {
        .cmd    = OPENER_CMD_SET_CHANNEL,
        .doit   = opener_nl_set_channel,
        .flags  = GENL_ADMIN_PERM,
    },
};

struct genl_family opener_genl_family = {
    .name    = "OPENER",
    .version = 1,
    .maxattr = OPENER_ATTR_MAX,
    .policy  = opener_policy,
    .ops     = opener_genl_ops,
    .n_ops   = ARRAY_SIZE(opener_genl_ops),
};

int opener_netlink_init(struct opener_priv *priv)
{
    return genl_register_family(&opener_genl_family);
}

void opener_netlink_exit(struct opener_priv *priv)
{
    genl_unregister_family(&opener_genl_family);
}
```

### 6.7 Scheduler proxy — kernel (`opener_scheduler.c`)

The kernel proxy uses exactly the same `struct opener_scheduler_ops` struct-of-function-pointers pattern as the userspace version. The only mechanical differences are:
- `opener_rpc_host_call` → `opener_rpc_call`
- `htonl/ntohs` → `cpu_to_be32/cpu_to_be16`
- `ntohl/ntohs` → `be32_to_cpu/be16_to_cpu`
- memory allocation uses kernel slab (`kmalloc/kfree`) where needed

```c
#include <linux/kernel.h>
#include <linux/byteorder/generic.h>
#include <linux/string.h>
#include "opener_priv.h"
#include "opener_rpc.h"
#include "../../common/opener_rpc.h"
#include "../../common/opener_scheduler.h"

struct opener_sched_ctx {
    struct opener_priv                   *priv;
    const struct opener_scheduler_event_ops *events;
    void                                  *events_ctx;
};

static int kproxy_schedule_tx(void *ctx,
                               const struct opener_tx_request *req,
                               opener_sched_handle_t *handle)
{
    struct opener_sched_ctx *c = ctx;
    u8 req_buf[sizeof(struct opener_rpc_schedule_tx_req) + 256];
    struct opener_rpc_schedule_tx_req *r =
        (struct opener_rpc_schedule_tx_req *)req_buf;
    struct opener_rpc_schedule_tx_resp resp;
    u16 req_len, resp_len = sizeof(resp);
    int ret;

    r->channel       = req->channel;
    r->subslot_start = req->subslot_start;
    r->subslot_count = req->subslot_count;
    r->mcs           = req->mcs;
    r->priority      = req->priority;
    r->_pad          = 0;
    r->earliest_sfn  = cpu_to_be16(req->earliest_sfn);
    r->latest_sfn    = cpu_to_be16(req->latest_sfn);
    r->payload_len   = cpu_to_be16(req->payload_len);
    memcpy(r->payload, req->payload, req->payload_len);
    req_len = (u16)(sizeof(*r) + req->payload_len);

    ret = opener_rpc_call(c->priv,
                            OPENER_RPC_MOD_SCHEDULER,
                            OPENER_RPC_SCHED_SCHEDULE_TX,
                            req_buf, req_len,
                            (u8 *)&resp, &resp_len, 500);
    if (ret)
        return ret;

    ret = (int)(s32)be32_to_cpu(resp.result);
    if (ret < 0)
        return ret;

    *handle = be32_to_cpu(resp.handle);
    return 0;
}

/*
 * kproxy_schedule_rx, kproxy_cancel, kproxy_get_stats follow the same
 * pattern as the userspace versions in Section 6.4, substituting
 * kernel byteorder macros and opener_rpc_call.
 */

/* Event handler: called from rx_work via process_frame on incoming events. */
static void sched_event_handler(const struct opener_rpc_header *hdr,
                                const u8 *payload, u16 plen, void *ctx)
{
    struct opener_sched_ctx *c = ctx;

    if (!c->events)
        return;

    if (hdr->proc_id == OPENER_RPC_SCHED_EVT_TX_COMPLETE) {
        const struct opener_rpc_tx_complete_event *e =
            (const struct opener_rpc_tx_complete_event *)payload;
        struct opener_tx_result result = {
            .handle       = be32_to_cpu(e->handle),
            .status       = e->status,
            .harq_retries = e->harq_retries,
            .rssi_dbm     = e->rssi_dbm,
        };
        if (c->events->on_tx_complete)
            c->events->on_tx_complete(c->events_ctx, &result);

    } else if (hdr->proc_id == OPENER_RPC_SCHED_EVT_RX_RECEIVED) {
        const struct opener_rpc_rx_received_event *e =
            (const struct opener_rpc_rx_received_event *)payload;
        struct opener_rx_result result = {
            .handle      = be32_to_cpu(e->handle),
            .channel     = e->channel,
            .subslot     = e->subslot,
            .rssi_dbm    = e->rssi_dbm,
            .payload_len = be16_to_cpu(e->payload_len),
        };
        memcpy(result.payload, e->payload,
               min_t(u16, result.payload_len, sizeof(result.payload)));
        if (c->events->on_rx_received)
            c->events->on_rx_received(c->events_ctx, &result);
    }
}

int opener_sched_init(struct opener_priv *priv,
                        const struct opener_scheduler_event_ops *events,
                        void *events_ctx,
                        struct opener_sched_ctx *ctx_out,
                        struct opener_scheduler_ops *ops_out)
{
    ctx_out->priv       = priv;
    ctx_out->events     = events;
    ctx_out->events_ctx = events_ctx;

    ops_out->version     = OPENER_SCHEDULER_API_VERSION;
    ops_out->schedule_tx = kproxy_schedule_tx;
    ops_out->schedule_rx = kproxy_schedule_rx;
    ops_out->cancel      = kproxy_cancel;
    ops_out->get_stats   = kproxy_get_stats;

    return opener_rpc_register_event_handler(priv,
               OPENER_RPC_MOD_SCHEDULER,
               sched_event_handler, ctx_out);
}

/* Convenience wrapper used by the netlink GET_STATS handler. */
int opener_sched_get_stats(struct opener_priv *priv,
                             struct opener_scheduler_stats *out)
{
    struct opener_sched_ctx ctx = { .priv = priv };
    return kproxy_get_stats(&ctx, out);
}
```

---

## 7. Usage

### 7.1 Network interface (standard kernel networking)

With the DKMS module loaded and the SPI devicetree node present, the kernel enumerates a `opener0` network interface. Any application using standard POSIX sockets transmits and receives through the DECT NR radio link without any awareness of the underlying transport:

```sh
# Bring up the interface and assign an IPv6 address
ip link set opener0 up
ip -6 addr add 2001:db8::1/64 dev opener0

# Verify
ip link show opener0
# 3: opener0: <NOARP,POINTOPOINT,UP,LOWER_UP> mtu 1280 qdisc pfifo_fast ...
#     link/none

ip -6 route show dev opener0
# 2001:db8::/64 dev opener0 proto kernel metric 256 ...

# Standard socket — unaware of DECT NR
ping6 -I opener0 2001:db8::2

# Interface statistics via standard kernel counters
ip -s link show opener0
```

Link-layer statistics (TX/RX packets, bytes, drops) are maintained by the `net_device` and reported through the standard `rtnl` interface. DECT-specific counters (HARQ retries, LBT back-offs, scheduler queue depth) require the `OPENER` Generic Netlink family.

### 7.2 Management via Generic Netlink

A management tool communicates with the `OPENER` genl family to query DECT-specific state. The example below uses libnl; the same can be done with raw `sendmsg`/`recvmsg` on a `NETLINK_GENERIC` socket.

```c
/* opener_ctl.c — query scheduler statistics */
#include <stdio.h>
#include <net/if.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

/* These mirror the kernel enum opener_nl_attr / opener_nl_cmd. */
enum { OPENER_ATTR_UNSPEC, OPENER_ATTR_IFINDEX,
       OPENER_ATTR_SCHED_TX_REQUESTS, OPENER_ATTR_SCHED_TX_OK,
       OPENER_ATTR_SCHED_TX_NACK,     OPENER_ATTR_SCHED_TX_CANCELLED,
       OPENER_ATTR_SCHED_RX_REQUESTS, OPENER_ATTR_SCHED_RX_COMPLETED,
       OPENER_ATTR_SCHED_LBT_BACKOFFS };
enum { OPENER_CMD_UNSPEC, OPENER_CMD_GET_STATS, OPENER_CMD_SET_CHANNEL };

static int recv_cb(struct nl_msg *msg, void *arg)
{
    struct nlattr *attrs[OPENER_ATTR_SCHED_LBT_BACKOFFS + 1];
    struct genlmsghdr *ghdr = nlmsg_data(nlmsg_hdr(msg));

    nla_parse(attrs, OPENER_ATTR_SCHED_LBT_BACKOFFS,
              genlmsg_attrdata(ghdr, 0), genlmsg_attrlen(ghdr, 0), NULL);

    printf("tx_req=%-6u  ok=%-6u  nack=%-6u  cancelled=%-6u\n"
           "rx_req=%-6u  rx_ok=%-6u  lbt_backoffs=%u\n",
           nla_get_u32(attrs[OPENER_ATTR_SCHED_TX_REQUESTS]),
           nla_get_u32(attrs[OPENER_ATTR_SCHED_TX_OK]),
           nla_get_u32(attrs[OPENER_ATTR_SCHED_TX_NACK]),
           nla_get_u32(attrs[OPENER_ATTR_SCHED_TX_CANCELLED]),
           nla_get_u32(attrs[OPENER_ATTR_SCHED_RX_REQUESTS]),
           nla_get_u32(attrs[OPENER_ATTR_SCHED_RX_COMPLETED]),
           nla_get_u32(attrs[OPENER_ATTR_SCHED_LBT_BACKOFFS]));
    return NL_OK;
}

int main(int argc, char **argv)
{
    const char *ifname = (argc > 1) ? argv[1] : "opener0";

    struct nl_sock *sk = nl_socket_alloc();
    genl_connect(sk);
    int family = genl_ctrl_resolve(sk, "OPENER");
    if (family < 0) { fprintf(stderr, "OPENER family not found\n"); return 1; }

    struct nl_msg *msg = nlmsg_alloc();
    genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ,
                family, 0, 0, OPENER_CMD_GET_STATS, 1);
    nla_put_u32(msg, OPENER_ATTR_IFINDEX, if_nametoindex(ifname));

    nl_socket_modify_cb(sk, NL_CB_VALID, NL_CB_CUSTOM, recv_cb, NULL);
    nl_send_auto(sk, msg);
    nl_recvmsgs_default(sk);

    nlmsg_free(msg);
    nl_socket_free(sk);
    return 0;
}
```

---

## 8. Extending to Other Modules

Adding RPC support for a new module requires four files. The kernel module and Zephyr device sides are symmetric:

| File | Content | Where it lives |
|---|---|---|
| `common/opener_<module>.h` | `struct opener_<module>_ops`, request/result structs, event callbacks | Shared (`common/`) |
| `common/opener_rpc.h` | `OPENER_RPC_MOD_<MODULE>` constant and all `OPENER_RPC_<MODULE>_*` procedure/event IDs | Shared (`common/`) |
| `subsys/opener/rpc/modules/opener_rpc_<module>.c` | Call handlers + event pushers registered with `opener_rpc_server_register_handler` | Device (Zephyr) |
| `opener_host/opener_<module>.c` | Kernel proxy filling `struct opener_<module>_ops` + event handler registered with `opener_rpc_register_event_handler`; optional genl commands added to `opener_netlink.c` | Host (kernel module) |

Neither the device RPC core (`opener_rpc_server.c`) nor the kernel RPC core (`opener_rpc.c`) requires modification. Serialized PDU structs are added to `opener_rpc.h` and compiled on both sides.

In a future iteration, the PDU structs and the serialization glue (`handle_*` on device, `kproxy_*` on host) SHOULD be generated from a declarative interface description (IDL file) to eliminate manual boilerplate and prevent serialization mismatches.

---

## 9. TUN/TAP Alternative (Investigation Note)

A kernel module is the preferred implementation for production: zero-copy data path, full integration with netfilter/tc/BPF, standard `rtnl` statistics, and no per-packet context switch. However, a fully userspace daemon using TUN or TAP is worth evaluating for early bring-up and platforms without DKMS support.

### 9.1 TUN device — IPv6 profile (layer 3)

```
 IPv6 stack
     ↕  sk_buff
 TUN device "opener0"  (/dev/net/tun, IFF_TUN | IFF_NO_PI)
     ↕  read()/write() on tun_fd
 Userspace daemon
     ↕  opener_rpc_host_call() / event callbacks
 spidev + GPIO char device   (/dev/spidev0.0, /dev/gpiochip0)
     ↕  SPI 8 MHz
 Zephyr device
```

The daemon opens `/dev/net/tun` with `IFF_TUN | IFF_NO_PI`, creating `opener0`. It polls both `tun_fd` (outgoing IPv6 packets from the kernel) and the GPIO IRQ fd (incoming events from the device) using `epoll`, bridging each direction through the existing spidev-based `opener_rpc_host.c` transport.

The kernel sees a standard TUN interface: `ip link`, `ip -6 addr`, and `ip -6 route` work without modification.

### 9.2 TAP device — Ethernet-over-DECT profile (layer 2)

A TAP device (`IFF_TAP | IFF_NO_PI`) operates at layer 2 and carries full Ethernet frames. This suits a custom Ethernet-over-DECT NR convergence profile (not standardised by ETSI; would require a custom CVG type). The daemon bridges Ethernet frames between the TAP fd and the SPI transport. The kernel sees an Ethernet-like interface and can bridge, filter, or route at layer 2.

The 14-byte Ethernet header overhead is non-trivial relative to DECT NR PDU sizes; this approach is most appropriate when the application requires L2 bridging or non-IP protocols, not for the standard IPv6 profile.

### 9.3 Comparison

| Criterion | Kernel DKMS module | TUN daemon | TAP daemon |
|---|---|---|---|
| Data-plane latency | Minimal (in-kernel) | +2 context switches/packet | +2 context switches/packet |
| Zero-copy path | Yes | No | No |
| Standard `ip -s link` stats | Yes | Partial (tun counters) | Partial (tap counters) |
| DECT-specific stats (genl) | Yes | Separate IPC needed | Separate IPC needed |
| netfilter / tc / eBPF | Full support | Full support | Full support |
| IPv6 profile compliance | Full | Full | Via Ethernet encap |
| Development complexity | High (kernel APIs, DKMS) | Low (POSIX) | Low (POSIX) |
| Portability (non-Linux host) | No | Partial (BSD TUN) | Partial (BSD TAP) |
| Recommended for | Production | Bring-up / CI / no DKMS | L2 bridging use cases |
