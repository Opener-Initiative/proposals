/*
 * Copyright (c) 2026 Deveritec GmbH.
 *
 * SPDX-License-Identifier: Apache-2.0
 * 
 * 
 */

/**
 * @file
 * @brief Public DECT NR+ Radio Scheduler API
 * 
 * @attention This file contains pseudo code for demonstration purposes.
 * Definitions may be missing and in that case are only place 
 * holders for types representing related objects.
 */

#ifndef DECTNRP_SDLR_H_
#define DECTNRP_SDLR_H_

#include <net/net_pkt.h>
#include "dectnrp_resources.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup dectnrp_sdlr DECT NR+ Scheduler
 * @version 0.0.1
 * @ingroup dectnrp
 *
 * @brief DECT NR+ scheduler API
 *
 * @details This API provides a common representation of implementation-specific
 * schedulers of DECT NR+ L2.
 * **Application developers should never interface directly with this API.** It
 * is of interest to l2/dectnrp maintainers only.
 *
 * Implementing the basic scheduler API will ensure integration with the native L2
 * stack.
 *
 * @note References are to the ETSI TS 103 636 DECTNRP NR+ V2.1.1 (2024-10) standard
 * If not further noted all references in this file refer to ETSI TS 103 636-4.
 * 
 * @{
 */

/**
 * @brief Enumerate all events notified via dectnrp_sdlr_event_cb_t.
 */
enum dectnrp_sdlr_event {
	/** Frame sync event. */
	DECTNRP_EVENT_FRAME,
    /** A message has been received. */
	DECTNRP_EVENT_MSG_RX,
	/** A message tx has been completed. */
	DECTNRP_EVENT_MSG_TX_COMPL,
};

union dectnrp_sdlr_event_data {
    union {
		/** DECTNRP_EVENT_FRAME */
		struct {
			uint32_t frame_count;
			/* More meta data */
        } frame_sync;
		/** DECTNRP_EVENT_MESSAGE_RECEIVED */
        struct {
            struct net_pkt *pkt;
			const struct dectnrp_resource *resource;
			/* More meta data */
			int status;
        } msg_recv;
		/** DECTNRP_EVENT_MESSAGE_TX_COMPLETE */
        struct {
            struct net_pkt *pkt;
			const struct dectnrp_resource *resource;
			/* More meta data */
			int status;
        } msg_compl;
    };
};

/** Event callback function  */
typedef void (*dectnrp_sdlr_event_cb_t)(struct dectnrp_context *ctx, enum dectnrp_sdlr_event evt,
				   union dectnrp_sdlr_event_data *event_data);

/**
 * @name DECTNRP scheduler interface API.
 * @{
 */

/**
 * @brief DECTNRP scheduler initialization.
 * *
 * @param sdlr Scheduler instance
 * @param ctx The common dectnrp context the scheduler is running in.
 * @param callback Callback used by the scheduler to notify events back.
 * 
 */
void dectnrp_sdlr_init(struct dectnrp_sdlr *sdlr, struct dectnrp_context *ctx,
	dectnrp_sdlr_event_cb_t callback);

/**
 * @brief Returns the current state of the scheduler.
 * 
 * @param sdlr Scheduler instance
 * @return enum dectnrp_sdlr_state 
 */
enum dectnrp_sdlr_state dectnrp_sdlr_get_state(struct dectnrp_sdlr *sdlr);

/**
 * @brief Schedules a rssi1 scan.
 *
 * @param sdlr Scheduler instance
 * @return int success or negative error code 
 */
int dectnrp_sdlr_schedule_rssi1scan(struct dectnrp_sdlr *sdlr);

#if defined CONFIG_NET_L2_DECTNRP_FT

/**
 * @brief Returns current state of beaconing.
 *
 * @param sdlr Scheduler instance
 * @return enum dectnrp_beaconing_state
 */
enum dectnrp_beaconing_state dectnrp_sdlr_get_beaconing_state(struct dectnrp_sdlr *sdlr);

/**
 * @brief Scheduler starts beaconing process and schedules first beacon.
 *
 * @param sdlr Scheduler instance
 * @return int success or negative error code 
 */
int dectnrp_sdlr_beaconing_start(struct dectnrp_sdlr *sdlr);

/**
 * @brief Scheduler stops beaconing process
 *
 * @param sdlr Scheduler instance
 */
int dectnrp_sdlr_beaconing_stop(struct dectnrp_sdlr *sdlr);

#endif /* defined CONFIG_NET_L2_DECTNRP_FT */

#if defined CONFIG_NET_L2_DECTNRP_PT

/**
 * @brief Returns the current state of the beacon-scan.
 *
 * @param sdlr Scheduler instance
 * @return enum dectnrp_beaconscan_state
 */
enum dectnrp_beaconscan_state dectnrp_sdlr_get_beaconscan_state(struct dectnrp_sdlr *sdlr);

/**
 * @brief Scheduler starts beacon-scan process and schedules first beacon-scan.

 * @param sdlr Scheduler instance
 * @return int success or negative error code 
 */
int dectnrp_sdlr_start_beaconscan(struct dectnrp_sdlr *sdlr);

/**
 * @brief Scheduler stops beacon-scan process.

 * @param sdlr Scheduler instance
 * @return int success or negative error code 
 */
int dectnrp_sdlr_start_beaconscan(struct dectnrp_sdlr *sdlr);

#endif /* defined CONFIG_NET_L2_DECTNRP_PT */

/**
 * @brief Creates a new tx operation with the given message packet @p pkt
 * and put into into related tx message queue to wait for its scheduling.
 *
 * @param sdlr
 * @param pkt the packet containing the tx buffer to be sent.
 * 
 * @return int success or negative error code
 */
int dectnrp_sdlr_message_put(struct dectnrp_sdlr *sdlr, struct net_pkt *pkt
	 /* More meta data */ );

/** @} */

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* DECTNRP_SDLR_H_ */