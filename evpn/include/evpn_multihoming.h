
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
 * Single-Active Multi-homing (WEEK 4)
 * ============================================================ */

/**
 * Enable single-active mode for Ethernet Segment
 * 
 * In single-active mode, only one PE actively forwards traffic
 * to/from the multi-homed device. Other PEs are in standby.
 * Provides faster convergence than all-active for some scenarios.
 * 
 * RFC 7432 Section 8.4 - Single-Active Redundancy Mode
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          0 on success, -1 on error
 */
int evpn_enable_single_active(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Elect active PE for single-active Ethernet Segment
 * 
 * Uses DF election algorithm to determine which PE is active.
 * Active PE forwards all traffic, standby PEs block traffic.
 * 
 * Election criteria:
 * - Lowest IP address among operational PEs
 * - Or use DF election result
 * - Deterministic selection across all PEs
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @param active_pe Output: Elected active PE IP address
 * @return          0 on success, -1 on error
 */
int evpn_elect_active_pe(evpn_ctx_t *ctx, const evpn_esi_t *esi, 
                         uint32_t *active_pe);

/**
 * Check if we are the active PE for this Ethernet Segment
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          true if we are active PE, false if standby
 */
bool evpn_am_i_active_pe(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Get the currently active PE for Ethernet Segment
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @param active_pe Output: Active PE IP address
 * @return          0 on success, -1 on error
 */
int evpn_get_active_pe(evpn_ctx_t *ctx, const evpn_esi_t *esi, 
                       uint32_t *active_pe);

/**
 * Handle PE failure in single-active mode
 * 
 * When active PE fails:
 * 1. Detect failure (via BGP session loss or route withdrawal)
 * 2. Elect new active PE
 * 3. Standby PE takes over immediately
 * 4. Converge within sub-second timeframe
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @param failed_pe Failed PE IP address
 * @return          0 on success, -1 on error
 */
int evpn_handle_pe_failure(evpn_ctx_t *ctx, const evpn_esi_t *esi, 
                           uint32_t failed_pe);

/**
 * Forward traffic in single-active mode
 * 
 * Decision logic:
 * - If we are active PE: FORWARD
 * - If we are standby PE: DROP
 * - Applies to both ingress and egress traffic
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @param packet    Packet data
 * @param len       Packet length
 * @param direction 0 = ingress (from CE), 1 = egress (to CE)
 * @return          0 if should forward, -1 if should drop
 */
int evpn_single_active_forward(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                               const uint8_t *packet, size_t len, int direction);

/**
 * Transition from all-active to single-active mode
 * 
 * Handles mode change gracefully:
 * 1. Stop all-active load balancing
 * 2. Elect active PE
 * 3. Non-active PEs withdraw MAC routes
 * 4. Ensure no traffic loops or blackholing
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          0 on success, -1 on error
 */
int evpn_transition_to_single_active(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Transition from single-active to all-active mode
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          0 on success, -1 on error
 */
int evpn_transition_to_all_active(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Get single-active status and statistics
 * 
 * @param ctx           EVPN context
 * @param esi           Ethernet Segment Identifier
 * @param active_pe     Output: Current active PE
 * @param standby_count Output: Number of standby PEs
 * @param failover_count Output: Number of failovers that occurred
 * @return              0 on success, -1 on error
 */
int evpn_single_active_get_status(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                                   uint32_t *active_pe, int *standby_count,
                                   uint64_t *failover_count);


/* ============================================================
 * Aliasing Support (WEEK 4 Feature 3)
 * RFC 7432 Section 8.4 - Aliasing and Backup Path
 * ============================================================ */

/**
 * Enable aliasing for an Ethernet Segment
 * 
 * Aliasing allows a MAC address to have multiple paths (aliases) to
 * different PEs. When a remote PE receives MAC routes from multiple
 * PEs with the same ESI, it installs all paths for load balancing.
 * 
 * Benefits:
 * - Multiple paths to same destination
 * - Per-flow load balancing (ECMP-like)
 * - Better bandwidth utilization
 * - Enhanced redundancy
 * 
 * RFC 7432 Section 8.4: "All PEs attached to the same ES may
 * advertise the same MAC address, allowing traffic to be
 * load-balanced across all PEs."
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          0 on success, -1 on error
 */
int evpn_enable_aliasing(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Disable aliasing for an Ethernet Segment
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          0 on success, -1 on error
 */
int evpn_disable_aliasing(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Check if MAC is aliased (reachable via multiple PEs)
 * 
 * @param ctx       EVPN context
 * @param mac       MAC address
 * @param vni       VNI
 * @return          true if aliased, false otherwise
 */
bool evpn_is_mac_aliased(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t vni);

/**
 * Get all PEs (aliases) for a MAC address
 * 
 * Returns list of all PEs that can reach this MAC address.
 * Used for load balancing decisions.
 * 
 * @param ctx       EVPN context
 * @param mac       MAC address
 * @param vni       VNI
 * @param pe_ips    Output: Array of PE IPs (aliases)
 * @param count     Input/Output: Max count / Actual count
 * @return          0 on success, -1 on error
 */
int evpn_get_aliased_pes(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t vni,
                         uint32_t *pe_ips, int *count);

/**
 * Select best PE for a flow using aliasing
 * 
 * Given multiple PEs that can reach a MAC (aliases), select the
 * best PE for this specific flow using hash-based load balancing.
 * 
 * Selection algorithm:
 * - Hash on flow 5-tuple (src IP, dst IP, protocol, src port, dst port)
 * - Consistent hashing across all available PEs
 * - Per-flow stickiness (same flow → same PE)
 * - Distributes traffic evenly
 * 
 * @param ctx           EVPN context
 * @param mac           Destination MAC address
 * @param vni           VNI
 * @param flow_hash     Flow identifier (hash of 5-tuple)
 * @param selected_pe   Output: Selected PE IP
 * @return              0 on success, -1 on error
 */
int evpn_alias_select_pe(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t vni,
                         uint32_t flow_hash, uint32_t *selected_pe);

/**
 * Add alias path for a MAC address
 * 
 * When receiving a Type 2 MAC/IP route, add this PE as an alias
 * if the MAC is multi-homed (same ESI from multiple PEs).
 * 
 * @param ctx       EVPN context
 * @param mac       MAC address
 * @param vni       VNI
 * @param pe_ip     PE IP address (alias)
 * @param esi       ESI of the Ethernet Segment
 * @return          0 on success, -1 on error
 */
int evpn_add_mac_alias(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t vni,
                       uint32_t pe_ip, const evpn_esi_t *esi);

/**
 * Remove alias path for a MAC address
 * 
 * When a PE fails or withdraws a MAC route, remove it from the
 * alias list.
 * 
 * @param ctx       EVPN context
 * @param mac       MAC address
 * @param vni       VNI
 * @param pe_ip     PE IP address to remove
 * @return          0 on success, -1 on error
 */
int evpn_remove_mac_alias(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t vni,
                          uint32_t pe_ip);

/**
 * Perform per-flow load balancing across aliases
 * 
 * Given a packet, determine which PE to forward to based on
 * flow hash and available aliases.
 * 
 * @param ctx       EVPN context
 * @param packet    Packet data (for extracting 5-tuple)
 * @param len       Packet length
 * @param dst_mac   Destination MAC
 * @param vni       VNI
 * @param next_hop  Output: Selected next-hop PE IP
 * @return          0 on success, -1 on error
 */
int evpn_alias_load_balance(evpn_ctx_t *ctx, const uint8_t *packet, size_t len,
                            const uint8_t *dst_mac, uint32_t vni, 
                            uint32_t *next_hop);

/**
 * Get aliasing statistics
 * 
 * @param ctx               EVPN context
 * @param esi               Ethernet Segment Identifier
 * @param aliased_macs      Output: Number of aliased MACs
 * @param total_aliases     Output: Total alias paths
 * @param flows_balanced    Output: Flows load-balanced
 * @return                  0 on success, -1 on error
 */
int evpn_get_aliasing_stats(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                            int *aliased_macs, int *total_aliases,
                            uint64_t *flows_balanced);

/**
 * Compute flow hash for load balancing
 * 
 * Hash function that extracts 5-tuple from packet and computes
 * a hash value for consistent per-flow load balancing.
 * 
 * @param packet    Packet data
 * @param len       Packet length
 * @return          Flow hash value
 */
uint32_t evpn_compute_flow_hash(const uint8_t *packet, size_t len);

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

/* ============================================================
 * Mass Withdrawal (WEEK 4 Feature 2)
 * RFC 7432 Section 8.5 - Fast Convergence
 * ============================================================ */

/**
 * Perform mass withdrawal of all routes for an Ethernet Segment
 * 
 * When an Ethernet Segment fails or goes down, withdraw ALL associated
 * routes (Type 1, Type 2, Type 4) simultaneously for fast convergence.
 * 
 * This is critical for:
 * - Fast failover (sub-second convergence)
 * - Preventing blackholing during failures
 * - Reducing route churn during ES failures
 * 
 * RFC 7432 Section 8.5: "When all PEs attached to a given ES lose
 * connectivity to that ES, a mass withdraw mechanism is used."
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          Number of routes withdrawn, -1 on error
 */
int evpn_mass_withdraw(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Withdraw all Type 2 (MAC/IP) routes for an Ethernet Segment
 * 
 * Used as part of mass withdrawal or when transitioning to single-active
 * mode and we are not the active PE.
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @param vni       VNI (0 for all VNIs)
 * @return          Number of routes withdrawn, -1 on error
 */
int evpn_withdraw_all_mac_routes(evpn_ctx_t *ctx, const evpn_esi_t *esi, 
                                 uint32_t vni);

/**
 * Withdraw all Type 1 (Auto-Discovery) routes for Ethernet Segment
 * 
 * Signals to remote PEs that we are no longer attached to this ES.
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          0 on success, -1 on error
 */
int evpn_withdraw_all_ad_routes(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Withdraw Type 4 (Ethernet Segment) route
 * 
 * Removes ES membership advertisement.
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          0 on success, -1 on error
 */
int evpn_withdraw_es_route(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Fast convergence on ES failure
 * 
 * Complete fast convergence process:
 * 1. Detect ES failure (link down, all local CEs unreachable)
 * 2. Perform mass withdrawal of all routes
 * 3. Update local state
 * 4. Trigger re-election if needed
 * 
 * Convergence time target: < 1 second
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          0 on success, -1 on error
 */
int evpn_es_failure_fast_convergence(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Batch withdraw multiple MAC routes efficiently
 * 
 * Instead of sending one BGP UPDATE per MAC, batch multiple MACs
 * into a single UPDATE message for efficiency.
 * 
 * @param ctx           EVPN context
 * @param mac_list      Array of MAC addresses
 * @param mac_count     Number of MACs
 * @param vni           VNI
 * @return              Number of routes withdrawn, -1 on error
 */
int evpn_batch_withdraw_macs(evpn_ctx_t *ctx, const uint8_t (*mac_list)[6],
                             int mac_count, uint32_t vni);

/**
 * Mark Ethernet Segment as failed/down
 * 
 * Updates ES state and triggers mass withdrawal if configured.
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          0 on success, -1 on error
 */
int evpn_es_mark_down(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Mark Ethernet Segment as operational/up
 * 
 * Re-advertises all routes after ES recovery.
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          0 on success, -1 on error
 */
int evpn_es_mark_up(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Get mass withdrawal statistics
 * 
 * @param ctx               EVPN context
 * @param esi               Ethernet Segment Identifier
 * @param withdrawal_count  Output: Total withdrawals performed
 * @param last_withdrawal   Output: Timestamp of last withdrawal
 * @return                  0 on success, -1 on error
 */
int evpn_get_mass_withdrawal_stats(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                                    uint64_t *withdrawal_count,
                                    time_t *last_withdrawal);


/* ============================================================
 * Local Bias (WEEK 4 Feature 4)
 * Traffic Optimization
 * ============================================================ */

/**
 * Enable local bias for an Ethernet Segment
 * 
 * Local bias prefers the local PE for forwarding when available,
 * reducing inter-PE traffic and improving performance.
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @return          0 on success, -1 on error
 */
int evpn_enable_local_bias(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Check if we should prefer local forwarding
 * 
 * @param ctx       EVPN context
 * @param esi       Ethernet Segment Identifier
 * @param dst_mac   Destination MAC
 * @return          true if local PE preferred, false otherwise
 */
bool evpn_should_use_local(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                           const uint8_t *dst_mac);

/**
 * Get local bias statistics
 * 
 * @param ctx               EVPN context
 * @param esi               Ethernet Segment Identifier
 * @param local_forwards    Output: Packets forwarded locally
 * @param remote_forwards   Output: Packets forwarded remotely
 * @return                  0 on success, -1 on error
 */
int evpn_get_local_bias_stats(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                              uint64_t *local_forwards, 
                              uint64_t *remote_forwards);

#endif /* EVPN_MULTIHOMING_H */
