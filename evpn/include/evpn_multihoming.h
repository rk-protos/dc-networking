
/*****************************************************************************
 * Project     : EVPN Protocol Implementation (RFC 8365)
 * Description : Part of minimal implementation of EVPN
 *                Ethernet VPN (EVPN) Control Plane for Network Virtualization Overlay
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : evpn_multihoming.h  
 * Purpose     : This implementation provides - 
 *                 RFC 8365 Section 8 - Multi-homing
 *                 RFC 7432 Section 8 - Multihoming Functions
 * 
 *               Implements:
 *                - Ethernet Segments (ES)
 *                - Designated Forwarder (DF) Election
 *                - Split-horizon filtering
 *                - All-active multi-homing
 *                - Type 1 routes (Ethernet Auto-Discovery)
 *                - Type 4 routes (Ethernet Segment)
  *****************************************************************************/

#ifndef EVPN_MULTIHOMING_H
#define EVPN_MULTIHOMING_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "evpn.h"

/* ============================================================
 * Ethernet Segment Management
 * ============================================================ */

/**
 * Create Ethernet Segment
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @param mode      Redundancy mode
 * @return          0 on success, -1 on error
 */
int evpn_create_ethernet_segment(evpn_ctx_t *ctx, 
                                  const evpn_esi_t *esi,
                                  evpn_redundancy_mode_t mode);

/**
 * Delete Ethernet Segment
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          0 on success, -1 on error
 */
int evpn_delete_ethernet_segment(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Find Ethernet Segment by ESI
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          Ethernet Segment pointer or NULL
 */
evpn_ethernet_segment_t *evpn_find_ethernet_segment(evpn_ctx_t *ctx,
                                                     const evpn_esi_t *esi);

/**
 * Add PE to Ethernet Segment
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @param pe_ip     PE Router IP address
 * @return          0 on success, -1 on error
 */
int evpn_es_add_pe(evpn_ctx_t *ctx, const evpn_esi_t *esi, uint32_t pe_ip);

/**
 * Remove PE from Ethernet Segment
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @param pe_ip     PE Router IP address
 * @return          0 on success, -1 on error
 */
int evpn_es_remove_pe(evpn_ctx_t *ctx, const evpn_esi_t *esi, uint32_t pe_ip);

/**
 * Get list of PEs on Ethernet Segment
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @param pe_ips    Output: Array of PE IPs
 * @param count     Input/Output: Max count / Actual count
 * @return          0 on success, -1 on error
 */
int evpn_es_get_peer_list(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                          uint32_t *pe_ips, int *count);

/* ============================================================
 * Type 1 Routes - Ethernet Auto-Discovery
 * ============================================================ */

/**
 * Encode Type 1 route (Ethernet Auto-Discovery)
 * 
 * @param route     Ethernet AD route
 * @param buf       Output buffer
 * @param buf_size  Buffer size
 * @param len       Output: NLRI length
 * @return          0 on success, -1 on error
 */
int evpn_encode_type1_route(const evpn_ethernet_ad_route_t *route,
                            uint8_t *buf, size_t buf_size, size_t *len);

/**
 * Decode Type 1 route
 * 
 * @param nlri      NLRI data
 * @param nlri_len  NLRI length
 * @param route     Output: Ethernet AD route
 * @return          0 on success, -1 on error
 */
int evpn_decode_type1_route(const uint8_t *nlri, size_t nlri_len,
                            evpn_ethernet_ad_route_t *route);

/**
 * Advertise Ethernet Auto-Discovery route (per-EVI)
 * 
 * @param ctx           EVPN context
 * @param esi           Ethernet Segment Identifier
 * @param ethernet_tag  Ethernet Tag (0 for all VLANs)
 * @param vni           VNI/label
 * @return              0 on success, -1 on error
 */
int evpn_advertise_ethernet_ad(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                                uint32_t ethernet_tag, uint32_t vni);

/**
 * Withdraw Ethernet Auto-Discovery route
 * 
 * @param ctx           EVPN context
 * @param esi           Ethernet Segment Identifier
 * @param ethernet_tag  Ethernet Tag
 * @return              0 on success, -1 on error
 */
int evpn_withdraw_ethernet_ad(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                               uint32_t ethernet_tag);

/**
 * Process received Type 1 route
 * 
 * @param ctx       EVPN context
 * @param route     Ethernet AD route
 * @param next_hop  Next hop (PE IP)
 * @param withdraw  Is this a withdrawal?
 * @return          0 on success, -1 on error
 */
int evpn_process_ethernet_ad_route(evpn_ctx_t *ctx,
                                    const evpn_ethernet_ad_route_t *route,
                                    uint32_t next_hop,
                                    bool withdraw);

/* ============================================================
 * Type 4 Routes - Ethernet Segment
 * ============================================================ */

/**
 * Encode Type 4 route (Ethernet Segment)
 * 
 * @param route     Ethernet Segment route
 * @param buf       Output buffer
 * @param buf_size  Buffer size
 * @param len       Output: NLRI length
 * @return          0 on success, -1 on error
 */
int evpn_encode_type4_route(const evpn_ethernet_segment_route_t *route,
                            uint8_t *buf, size_t buf_size, size_t *len);

/**
 * Decode Type 4 route
 * 
 * @param nlri      NLRI data
 * @param nlri_len  NLRI length
 * @param route     Output: Ethernet Segment route
 * @return          0 on success, -1 on error
 */
int evpn_decode_type4_route(const uint8_t *nlri, size_t nlri_len,
                            evpn_ethernet_segment_route_t *route);

/**
 * Advertise Ethernet Segment route
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          0 on success, -1 on error
 */
int evpn_advertise_ethernet_segment(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Withdraw Ethernet Segment route
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          0 on success, -1 on error
 */
int evpn_withdraw_ethernet_segment(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Process received Type 4 route
 * 
 * @param ctx       EVPN context
 * @param route     Ethernet Segment route
 * @param next_hop  Next hop (PE IP)
 * @param withdraw  Is this a withdrawal?
 * @return          0 on success, -1 on error
 */
int evpn_process_ethernet_segment_route(evpn_ctx_t *ctx,
                                         const evpn_ethernet_segment_route_t *route,
                                         uint32_t next_hop,
                                         bool withdraw);

/* ============================================================
 * Designated Forwarder (DF) Election
 * ============================================================ */

/**
 * DF Election Algorithms
 */
typedef enum {
    EVPN_DF_ELECTION_MODULO,       /* Default: service_id modulo #PEs */
    EVPN_DF_ELECTION_HRW,          /* Highest Random Weight */
    EVPN_DF_ELECTION_PREFERENCE    /* Preference-based */
} evpn_df_election_type_t;

/**
 * Perform DF election for Ethernet Segment
 * 
 * Uses default modulo algorithm.
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          0 on success, -1 on error
 */
int evpn_df_election(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Perform DF election with specific algorithm
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @param type      Election algorithm
 * @return          0 on success, -1 on error
 */
int evpn_df_election_with_type(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                                evpn_df_election_type_t type);

/**
 * Modulo DF Election (RFC 7432 Section 8.5)
 * 
 * DF = PE with IP = min(PE_IPs) + (service_id mod #PEs)
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @param df_ip     Output: DF IP address
 * @return          0 on success, -1 on error
 */
int evpn_df_election_modulo(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                            uint32_t *df_ip);

/**
 * HRW DF Election (Highest Random Weight)
 * 
 * DF = PE with highest hash(PE_IP, ESI, service_id)
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @param df_ip     Output: DF IP address
 * @return          0 on success, -1 on error
 */
int evpn_df_election_hrw(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                         uint32_t *df_ip);

/**
 * Check if we are the DF for this Ethernet Segment
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          true if we are DF, false otherwise
 */
bool evpn_am_i_df(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Get DF for Ethernet Segment
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @param df_ip     Output: DF IP address
 * @return          0 on success, -1 on error
 */
int evpn_get_df(evpn_ctx_t *ctx, const evpn_esi_t *esi, uint32_t *df_ip);

/**
 * Per-VLAN DF election
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @param vlan_id   VLAN ID
 * @param df_ip     Output: DF IP address
 * @return          0 on success, -1 on error
 */
int evpn_df_election_per_vlan(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                              uint32_t vlan_id, uint32_t *df_ip);

/* ============================================================
 * Split-Horizon Filtering
 * ============================================================ */

/**
 * Check if packet should be filtered (split-horizon)
 * 
 * Prevents loops: Don't forward packet received from ES back to same ES
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @param src_ip    Source PE IP
 * @return          true if should filter, false otherwise
 */
bool evpn_split_horizon_filter(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                               uint32_t src_ip);

/**
 * Check if packet should be filtered on outgoing interface
 * 
 * @param ctx           EVPN context
 * @param incoming_esi  ESI where packet was received
 * @param outgoing_esi  ESI where packet would be sent
 * @return              true if should filter, false otherwise
 */
bool evpn_split_horizon_check(evpn_ctx_t *ctx,
                              const evpn_esi_t *incoming_esi,
                              const evpn_esi_t *outgoing_esi);

/**
 * Allocate split-horizon label for Ethernet Segment
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @param label     Output: Allocated label
 * @return          0 on success, -1 on error
 */
int evpn_alloc_split_horizon_label(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                                   uint32_t *label);

/* ============================================================
 * All-Active Multi-homing
 * ============================================================ */

/**
 * Enable all-active mode for Ethernet Segment
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          0 on success, -1 on error
 */
int evpn_enable_all_active(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Disable all-active mode (switch to single-active)
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          0 on success, -1 on error
 */
int evpn_disable_all_active(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Load balance traffic across PEs in all-active mode
 * 
 * @param ctx           EVPN context
 * @param esi           Ethernet Segment Identifier
 * @param dst_mac       Destination MAC address
 * @param selected_pe   Output: Selected PE IP
 * @return              0 on success, -1 on error
 */
int evpn_multihome_load_balance(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                                const uint8_t *dst_mac, uint32_t *selected_pe);

/**
 * Handle BUM traffic in all-active mode
 * 
 * Only DF forwards BUM traffic to avoid duplication.
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @param packet    BUM packet
 * @param len       Packet length
 * @return          0 if should forward, -1 if should drop
 */
int evpn_multihome_bum_forward(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                               const uint8_t *packet, size_t len);

/**
 * Local bias - prefer local PE for forwarding
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @param dst_mac   Destination MAC
 * @return          true if local PE preferred, false otherwise
 */
bool evpn_multihome_local_bias(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                               const uint8_t *dst_mac);

/* ============================================================
 * Aliasing Support
 * ============================================================ */

/**
 * Check if MAC is aliased (multi-homed)
 * 
 * @param ctx       EVPN context
 * @param mac       MAC address
 * @param vni       VNI
 * @return          true if aliased, false otherwise
 */
bool evpn_is_mac_aliased(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t vni);

/**
 * Get all PEs for aliased MAC
 * 
 * @param ctx       EVPN context
 * @param mac       MAC address
 * @param vni       VNI
 * @param pe_ips    Output: Array of PE IPs
 * @param count     Input/Output: Max count / Actual count
 * @return          0 on success, -1 on error
 */
int evpn_get_aliased_pes(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t vni,
                         uint32_t *pe_ips, int *count);

/* ============================================================
 * Mass Withdrawal (Fast Convergence)
 * ============================================================ */

/**
 * Mass withdraw all routes for Ethernet Segment
 * 
 * Used when ES goes down for fast convergence.
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          Number of routes withdrawn, -1 on error
 */
int evpn_mass_withdraw_es(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Process mass withdrawal from peer
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          0 on success, -1 on error
 */
int evpn_process_mass_withdrawal(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/* ============================================================
 * Utilities and Diagnostics
 * ============================================================ */

/**
 * Generate ESI from system info (Type 0 - arbitrary)
 * 
 * @param system_mac    System MAC address
 * @param discriminator Local discriminator
 * @param esi           Output: Generated ESI
 */
void evpn_generate_esi_type0(const uint8_t *system_mac, uint32_t discriminator,
                             evpn_esi_t *esi);

/**
 * Generate ESI from LACP (Type 1)
 * 
 * @param lacp_sys_mac  LACP system MAC
 * @param port_key      LACP port key
 * @param esi           Output: Generated ESI
 */
void evpn_generate_esi_type1_lacp(const uint8_t *lacp_sys_mac, uint16_t port_key,
                                  evpn_esi_t *esi);

/**
 * Compare ESIs
 * 
 * @param esi1      First ESI
 * @param esi2      Second ESI
 * @return          0 if equal, <0 if esi1 < esi2, >0 if esi1 > esi2
 */
int evpn_compare_esi(const evpn_esi_t *esi1, const evpn_esi_t *esi2);

/**
 * Check if ESI is zero (single-homing)
 * 
 * @param esi       ESI to check
 * @return          true if zero, false otherwise
 */
bool evpn_is_zero_esi(const evpn_esi_t *esi);

/**
 * Dump Ethernet Segment information
 * 
 * @param ctx       EVPN context
 * @param esi       ESI (NULL for all)
 */
void evpn_dump_segments(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Print ESI in human-readable format
 * 
 * @param esi       ESI to print
 * @param buf       Output buffer
 * @param buf_size  Buffer size
 */
void evpn_esi_to_string(const evpn_esi_t *esi, char *buf, size_t buf_size);

#endif /* EVPN_MULTIHOMING_H */
