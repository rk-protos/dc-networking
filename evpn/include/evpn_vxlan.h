
 /*****************************************************************************
 * Project     : EVPN Protocol Implementation (RFC 8365)
 * Description : EVPN to VXLAN Data Plane Integration
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : evpn_vxlan.h  
 * Purpose     : This implementation provides - EVPN Route Types Processing
 *                 This module provides integration between EVPN control plane (RFC 8365)
 *                  and VXLAN data plane (RFC 7348).
 * 
 * Implementation includes:
 * - Install MACs learned via BGP into VXLAN MAC table
 * - Advertise MACs learned on VXLAN to BGP
 * - Eliminate unknown unicast flooding
  *****************************************************************************/

#ifndef EVPN_VXLAN_H
#define EVPN_VXLAN_H

#include <stdint.h>
#include <stdbool.h>
#include "evpn.h"

/**
 * VXLAN MAC Table Entry (from RFC 7348 implementation)
 * This is the expected interface - adjust to match your actual VXLAN code
 */
typedef struct {
    uint8_t mac[6];            /* MAC address */
    uint32_t vtep_ip;          /* Remote VTEP IP */
    uint32_t vni;              /* VNI */
    time_t timestamp;          /* Last seen */
    bool local;                /* Locally learned? */
} vxlan_mac_entry_t;

/* ============================================================
 * Core Integration Functions (CRITICAL for Week 2)
 * ============================================================ */

/**
 * Install remote MAC in VXLAN forwarding table
 * 
 * Called when EVPN learns a MAC via BGP Type 2 route.
 * This populates the VXLAN MAC table so packets can be forwarded.
 * 
 * @param evpn      EVPN context
 * @param mac       MAC address
 * @param vtep_ip   Remote VTEP IP address
 * @param vni       VNI
 * @return          0 on success, -1 on error
 */
int evpn_vxlan_install_remote_mac(evpn_ctx_t *evpn,
                                   const uint8_t *mac,
                                   uint32_t vtep_ip,
                                   uint32_t vni);

/**
 * Remove remote MAC from VXLAN forwarding table
 * 
 * Called when BGP Type 2 route is withdrawn.
 * 
 * @param evpn      EVPN context
 * @param mac       MAC address
 * @param vni       VNI
 * @return          0 on success, -1 on error
 */
int evpn_vxlan_remove_remote_mac(evpn_ctx_t *evpn,
                                  const uint8_t *mac,
                                  uint32_t vni);

/**
 * Advertise local MAC to BGP
 * 
 * Called when MAC is learned on local VXLAN interface.
 * This sends BGP Type 2 route to all peers.
 * 
 * @param evpn      EVPN context
 * @param mac       MAC address
 * @param ip        IP address (0 if unknown)
 * @param vni       VNI
 * @return          0 on success, -1 on error
 */
int evpn_vxlan_advertise_local_mac(evpn_ctx_t *evpn,
                                    const uint8_t *mac,
                                    uint32_t ip,
                                    uint32_t vni);

/**
 * Lookup MAC in VXLAN table
 * 
 * Query VXLAN forwarding table for MAC.
 * 
 * @param evpn      EVPN context
 * @param mac       MAC address
 * @param vni       VNI
 * @param vtep_ip   Output: Remote VTEP IP
 * @return          0 if found, -1 if not found
 */
int evpn_vxlan_lookup_mac(evpn_ctx_t *evpn,
                          const uint8_t *mac,
                          uint32_t vni,
                          uint32_t *vtep_ip);

/* ============================================================
 * VXLAN Context Interface
 * ============================================================ */

/**
 * Your VXLAN context structure (from RFC 7348 implementation)
 * Adjust this forward declaration to match your actual vxlan_ctx_t
 */
typedef struct vxlan_ctx vxlan_ctx_t;

/**
 * Link EVPN to VXLAN context
 * 
 * Establishes connection between EVPN control plane and VXLAN data plane.
 * 
 * @param evpn      EVPN context
 * @param vxlan     VXLAN context
 * @return          0 on success, -1 on error
 */
int evpn_vxlan_link(evpn_ctx_t *evpn, vxlan_ctx_t *vxlan);

/**
 * Unlink EVPN from VXLAN
 * 
 * @param evpn      EVPN context
 */
void evpn_vxlan_unlink(evpn_ctx_t *evpn);

/* ============================================================
 * MAC Learning Modes
 * ============================================================ */

typedef enum {
    EVPN_LEARNING_DATA_PLANE,     /* Traditional VXLAN flood-and-learn */
    EVPN_LEARNING_CONTROL_PLANE,  /* EVPN BGP-based learning */
    EVPN_LEARNING_HYBRID          /* Both data and control plane */
} evpn_learning_mode_t;

/**
 * Set MAC learning mode
 * 
 * @param evpn      EVPN context
 * @param mode      Learning mode
 * @return          0 on success, -1 on error
 */
int evpn_vxlan_set_learning_mode(evpn_ctx_t *evpn, evpn_learning_mode_t mode);

/**
 * Get current MAC learning mode
 * 
 * @param evpn      EVPN context
 * @return          Current learning mode
 */
evpn_learning_mode_t evpn_vxlan_get_learning_mode(evpn_ctx_t *evpn);

/* ============================================================
 * VTEP Management
 * ============================================================ */

/**
 * Register local VTEPs with EVPN
 * 
 * @param evpn      EVPN context
 * @param vtep_ip   VTEP IP address
 * @param vni       VNI
 * @return          0 on success, -1 on error
 */
int evpn_vxlan_register_vtep(evpn_ctx_t *evpn, uint32_t vtep_ip, uint32_t vni);

/**
 * Discover remote VTEPs via Type 3 routes
 * 
 * @param evpn      EVPN context
 * @param vni       VNI
 * @param vteps     Output: Array of VTEP IPs
 * @param count     Input/Output: Max/Actual count
 * @return          0 on success, -1 on error
 */
int evpn_vxlan_get_remote_vteps(evpn_ctx_t *evpn, uint32_t vni,
                                uint32_t *vteps, int *count);

/* ============================================================
 * Statistics and Monitoring
 * ============================================================ */

typedef struct {
    uint64_t macs_learned_bgp;      /* MACs learned via BGP */
    uint64_t macs_learned_data;     /* MACs learned via data plane */
    uint64_t macs_advertised;       /* MACs advertised to BGP */
    uint64_t macs_withdrawn;        /* MACs withdrawn from BGP */
    uint64_t unknown_unicast_before; /* Unknown unicast count before EVPN */
    uint64_t unknown_unicast_after;  /* Unknown unicast count after EVPN */
    uint64_t flood_reduction_pct;    /* Flooding reduction percentage */
} evpn_vxlan_stats_t;

/**
 * Get EVPN-VXLAN integration statistics
 * 
 * @param evpn      EVPN context
 * @param stats     Output: Statistics
 */
void evpn_vxlan_get_stats(evpn_ctx_t *evpn, evpn_vxlan_stats_t *stats);

/**
 * Reset statistics counters
 * 
 * @param evpn      EVPN context
 */
void evpn_vxlan_reset_stats(evpn_ctx_t *evpn);

/* ============================================================
 * Callback Registration (for VXLAN events)
 * ============================================================ */

/**
 * Callback: New MAC learned on VXLAN interface
 */
typedef void (*evpn_vxlan_mac_learned_cb_t)(void *ctx,
                                            const uint8_t *mac,
                                            uint32_t ip,
                                            uint32_t vni);

/**
 * Callback: MAC aged out from VXLAN table
 */
typedef void (*evpn_vxlan_mac_aged_cb_t)(void *ctx,
                                         const uint8_t *mac,
                                         uint32_t vni);

/**
 * Register callback for VXLAN MAC learning events
 * 
 * @param evpn      EVPN context
 * @param callback  Callback function
 * @param user_ctx  User context passed to callback
 * @return          0 on success, -1 on error
 */
int evpn_vxlan_register_mac_learned_cb(evpn_ctx_t *evpn,
                                       evpn_vxlan_mac_learned_cb_t callback,
                                       void *user_ctx);

/**
 * Register callback for VXLAN MAC aging events
 * 
 * @param evpn      EVPN context
 * @param callback  Callback function
 * @param user_ctx  User context passed to callback
 * @return          0 on success, -1 on error
 */
int evpn_vxlan_register_mac_aged_cb(evpn_ctx_t *evpn,
                                    evpn_vxlan_mac_aged_cb_t callback,
                                    void *user_ctx);

/* ============================================================
 * MAC Synchronization
 * ============================================================ */

/**
 * Synchronize VXLAN MAC table with EVPN
 * 
 * Advertises all local MACs from VXLAN table to BGP.
 * 
 * @param evpn      EVPN context
 * @param vni       VNI (0 for all VNIs)
 * @return          Number of MACs synchronized, -1 on error
 */
int evpn_vxlan_sync_mac_table(evpn_ctx_t *evpn, uint32_t vni);

/**
 * Flush remote MACs for a VNI
 * 
 * Removes all remote MACs learned via BGP for specified VNI.
 * 
 * @param evpn      EVPN context
 * @param vni       VNI
 * @return          Number of MACs flushed, -1 on error
 */
int evpn_vxlan_flush_remote_macs(evpn_ctx_t *evpn, uint32_t vni);

/* ============================================================
 * Debugging and Diagnostics
 * ============================================================ */

/**
 * Dump MAC table (both local and remote)
 * 
 * @param evpn      EVPN context
 * @param vni       VNI (0 for all)
 */
void evpn_vxlan_dump_mac_table(evpn_ctx_t *evpn, uint32_t vni);

/**
 * Check if MAC is local or remote
 * 
 * @param evpn      EVPN context
 * @param mac       MAC address
 * @param vni       VNI
 * @return          true if local, false if remote
 */
bool evpn_vxlan_is_local_mac(evpn_ctx_t *evpn, const uint8_t *mac, uint32_t vni);

/**
 * Get MAC learning source
 * 
 * @param evpn      EVPN context
 * @param mac       MAC address
 * @param vni       VNI
 * @return          "BGP", "DataPlane", "Unknown"
 */
const char *evpn_vxlan_get_mac_source(evpn_ctx_t *evpn, 
                                     const uint8_t *mac, 
                                     uint32_t vni);

/* ============================================================
 * Integration with Existing VXLAN Functions
 * 
 * These are wrappers around your RFC 7348 implementation.
 * Adjust function names to match your actual VXLAN code.
 * ============================================================ */

/**
 * Wrapper for vxlan_mac_learn()
 * 
 * Add or update MAC entry in VXLAN forwarding table.
 * This should call your existing vxlan_mac_learn() function.
 * 
 * @param vxlan     VXLAN context
 * @param mac       MAC address
 * @param vtep_ip   Remote VTEP IP
 * @param vni       VNI
 * @return          0 on success, -1 on error
 */
int evpn_call_vxlan_mac_learn(void *vxlan,
                              const uint8_t *mac,
                              uint32_t vtep_ip,
                              uint32_t vni);

/**
 * Wrapper for vxlan_mac_lookup()
 * 
 * Lookup MAC in VXLAN forwarding table.
 * This should call your existing vxlan_mac_lookup() function.
 * 
 * @param vxlan     VXLAN context
 * @param mac       MAC address
 * @param vni       VNI
 * @param vtep_ip   Output: Remote VTEP IP
 * @return          0 if found, -1 if not found
 */
int evpn_call_vxlan_mac_lookup(void *vxlan,
                               const uint8_t *mac,
                               uint32_t vni,
                               uint32_t *vtep_ip);

/**
 * Wrapper for vxlan_mac_delete()
 * 
 * Remove MAC from VXLAN forwarding table.
 * 
 * @param vxlan     VXLAN context
 * @param mac       MAC address
 * @param vni       VNI
 * @return          0 on success, -1 on error
 */
int evpn_call_vxlan_mac_delete(void *vxlan,
                               const uint8_t *mac,
                               uint32_t vni);

#endif /* EVPN_VXLAN_H */
