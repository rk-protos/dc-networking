
 /*****************************************************************************
 * Project     : EVPN Protocol Implementation (RFC 8365)
 * Description : Part of minimal implementation of EVPN
 *                 * Author      : RK (kvrkr866@gmail.com)
 * File name   : evpn_routes.h  
 * Purpose     : This implementation provides - EVPN Route Types Processing
 *                RFC 8365 - EVPN Route Types 1-5
 *                RFC 7432 - BGP MPLS-Based Ethernet VPN
 * 
 *              This module handles encoding/decoding of 
 *                 EVPN NLRI for all route types
  *****************************************************************************/

#ifndef EVPN_ROUTES_H
#define EVPN_ROUTES_H

#include <stdint.h>
#include <stdbool.h>
#include "evpn.h"

/* EVPN NLRI Constants */
#define EVPN_NLRI_MAX_SIZE      256

/* EVPN Route Type Lengths (excluding route type byte) */
#define EVPN_TYPE1_MIN_LENGTH   23   /* Ethernet AD route */
#define EVPN_TYPE2_MIN_LENGTH   33   /* MAC/IP Advertisement */
#define EVPN_TYPE3_LENGTH       17   /* Inclusive Multicast */
#define EVPN_TYPE4_LENGTH       23   /* Ethernet Segment */
#define EVPN_TYPE5_MIN_LENGTH   34   /* IP Prefix */

/* Label Constants */
#define EVPN_LABEL_BITS         20
#define EVPN_LABEL_MAX          0xFFFFF

/**
 * EVPN NLRI Header (common to all route types)
 */
typedef struct {
    uint8_t route_type;        /* Route type (1-5) */
    uint8_t length;            /* Length of NLRI (excluding this byte) */
} __attribute__((packed)) evpn_nlri_header_t;

/* Function Prototypes */

/* ============================================================
 * Type 2 Routes - MAC/IP Advertisement (CRITICAL)
 * ============================================================ */

/**
 * Encode Type 2 route to EVPN NLRI format
 * 
 * @param route     MAC/IP advertisement route
 * @param buf       Output buffer
 * @param buf_size  Buffer size
 * @param len       Output: NLRI length
 * @return          0 on success, -1 on error
 */
int evpn_encode_type2_route(const evpn_mac_ip_route_t *route,
                            uint8_t *buf, size_t buf_size, size_t *len);

/**
 * Decode Type 2 route from EVPN NLRI
 * 
 * @param nlri      NLRI data (starts after route type)
 * @param nlri_len  NLRI length
 * @param route     Output: MAC/IP route
 * @return          0 on success, -1 on error
 */
int evpn_decode_type2_route(const uint8_t *nlri, size_t nlri_len,
                            evpn_mac_ip_route_t *route);

/**
 * Advertise local MAC/IP to BGP peers
 * 
 * @param ctx       EVPN context
 * @param mac       MAC address
 * @param ip        IP address (0 if MAC-only)
 * @param vni       VNI
 * @return          0 on success, -1 on error
 */
int evpn_advertise_mac_ip(evpn_ctx_t *ctx, const uint8_t *mac, 
                          uint32_t ip, uint32_t vni);

/**
 * Withdraw MAC/IP route
 * 
 * @param ctx       EVPN context
 * @param mac       MAC address
 * @param vni       VNI
 * @return          0 on success, -1 on error
 */
int evpn_withdraw_mac_ip(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t vni);

/**
 * Process received Type 2 route
 * 
 * @param ctx       EVPN context
 * @param route     MAC/IP route
 * @param next_hop  Next hop (VTEP IP)
 * @param withdraw  Is this a withdrawal?
 * @return          0 on success, -1 on error
 */
int evpn_process_mac_ip_route(evpn_ctx_t *ctx, 
                              const evpn_mac_ip_route_t *route,
                              uint32_t next_hop,
                              bool withdraw);

/* ============================================================
 * Type 3 Routes - Inclusive Multicast (CRITICAL)
 * ============================================================ */

/**
 * Encode Type 3 route to EVPN NLRI format
 * 
 * @param route     Inclusive multicast route
 * @param buf       Output buffer
 * @param buf_size  Buffer size
 * @param len       Output: NLRI length
 * @return          0 on success, -1 on error
 */
int evpn_encode_type3_route(const evpn_inclusive_mcast_route_t *route,
                            uint8_t *buf, size_t buf_size, size_t *len);

/**
 * Decode Type 3 route from EVPN NLRI
 * 
 * @param nlri      NLRI data
 * @param nlri_len  NLRI length
 * @param route     Output: Inclusive multicast route
 * @return          0 on success, -1 on error
 */
int evpn_decode_type3_route(const uint8_t *nlri, size_t nlri_len,
                            evpn_inclusive_mcast_route_t *route);

/**
 * Advertise Inclusive Multicast route
 * 
 * @param ctx       EVPN context
 * @param vni       VNI
 * @return          0 on success, -1 on error
 */
int evpn_advertise_inclusive_mcast(evpn_ctx_t *ctx, uint32_t vni);

/**
 * Withdraw Inclusive Multicast route
 * 
 * @param ctx       EVPN context
 * @param vni       VNI
 * @return          0 on success, -1 on error
 */
int evpn_withdraw_inclusive_mcast(evpn_ctx_t *ctx, uint32_t vni);

/**
 * Process received Type 3 route
 * 
 * @param ctx       EVPN context
 * @param route     Inclusive multicast route
 * @param next_hop  Next hop (originating VTEP IP)
 * @param withdraw  Is this a withdrawal?
 * @return          0 on success, -1 on error
 */
int evpn_process_inclusive_mcast_route(evpn_ctx_t *ctx,
                                       const evpn_inclusive_mcast_route_t *route,
                                       uint32_t next_hop,
                                       bool withdraw);

/* ============================================================
 * Type 1 Routes - Ethernet Auto-Discovery (Week 3)
 * ============================================================ */

int evpn_encode_type1_route(const evpn_ethernet_ad_route_t *route,
                            uint8_t *buf, size_t buf_size, size_t *len);
int evpn_decode_type1_route(const uint8_t *nlri, size_t nlri_len,
                            evpn_ethernet_ad_route_t *route);

/* ============================================================
 * Type 4 Routes - Ethernet Segment (Week 3)
 * ============================================================ */

int evpn_encode_type4_route(const evpn_ethernet_segment_route_t *route,
                            uint8_t *buf, size_t buf_size, size_t *len);
int evpn_decode_type4_route(const uint8_t *nlri, size_t nlri_len,
                            evpn_ethernet_segment_route_t *route);

/* ============================================================
 * Generic NLRI Processing
 * ============================================================ */

/**
 * Process EVPN NLRI (dispatches to appropriate type handler)
 * 
 * @param ctx       EVPN context
 * @param nlri      NLRI data
 * @param nlri_len  NLRI length
 * @param next_hop  Next hop IP
 * @param withdraw  Is this a withdrawal?
 * @return          0 on success, -1 on error
 */
int evpn_process_nlri(evpn_ctx_t *ctx, const uint8_t *nlri, size_t nlri_len,
                     uint32_t next_hop, bool withdraw);

/**
 * Build BGP UPDATE message with EVPN route
 * 
 * @param ctx       EVPN context
 * @param nlri      EVPN NLRI data
 * @param nlri_len  NLRI length
 * @param next_hop  Next hop IP
 * @param buf       Output buffer
 * @param buf_size  Buffer size
 * @param msg_len   Output: Message length
 * @return          0 on success, -1 on error
 */
int evpn_build_update_message(evpn_ctx_t *ctx,
                              const uint8_t *nlri, size_t nlri_len,
                              uint32_t next_hop,
                              uint8_t *buf, size_t buf_size,
                              size_t *msg_len);

/**
 * Build BGP withdrawal message for EVPN route
 * 
 * @param ctx       EVPN context
 * @param nlri      EVPN NLRI data
 * @param nlri_len  NLRI length
 * @param buf       Output buffer
 * @param buf_size  Buffer size
 * @param msg_len   Output: Message length
 * @return          0 on success, -1 on error
 */
int evpn_build_withdrawal_message(evpn_ctx_t *ctx,
                                  const uint8_t *nlri, size_t nlri_len,
                                  uint8_t *buf, size_t buf_size,
                                  size_t *msg_len);

/* ============================================================
 * Helper Functions
 * ============================================================ */

/**
 * Encode MPLS label (20 bits)
 * 
 * @param label     Label value
 * @param buf       Output buffer (3 bytes)
 */
void evpn_encode_label(uint32_t label, uint8_t *buf);

/**
 * Decode MPLS label
 * 
 * @param buf       Input buffer (3 bytes)
 * @return          Label value
 */
uint32_t evpn_decode_label(const uint8_t *buf);

/**
 * Encode ESI (10 bytes)
 * 
 * @param esi       ESI structure
 * @param buf       Output buffer (10 bytes)
 */
void evpn_encode_esi(const evpn_esi_t *esi, uint8_t *buf);

/**
 * Decode ESI
 * 
 * @param buf       Input buffer (10 bytes)
 * @param esi       Output: ESI structure
 */
void evpn_decode_esi(const uint8_t *buf, evpn_esi_t *esi);

/**
 * Encode Route Distinguisher
 * 
 * @param rd        RD structure
 * @param buf       Output buffer (8 bytes)
 */
void evpn_encode_rd(const evpn_rd_t *rd, uint8_t *buf);

/**
 * Decode Route Distinguisher
 * 
 * @param buf       Input buffer (8 bytes)
 * @param rd        Output: RD structure
 */
void evpn_decode_rd(const uint8_t *buf, evpn_rd_t *rd);

/**
 * Get route type name
 * 
 * @param type      Route type (1-5)
 * @return          Route type name string
 */
const char *evpn_route_type_name(evpn_route_type_t type);

/**
 * Validate EVPN NLRI
 * 
 * @param nlri      NLRI data
 * @param nlri_len  NLRI length
 * @return          true if valid, false otherwise
 */
bool evpn_validate_nlri(const uint8_t *nlri, size_t nlri_len);

#endif /* EVPN_ROUTES_H */
