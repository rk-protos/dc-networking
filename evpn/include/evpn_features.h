/*****************************************************************************
 * Project     : EVPN Protocol Implementation (RFC 8365)
 * Description : Production Features Header
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : evpn_features.h  
 * Purpose     : DCI, Graceful Restart, RR Redundancy, Extended Communities,
 *               Performance Optimizations, Monitoring & Debugging
 *****************************************************************************/

#ifndef EVPN_FEATURES_H
#define EVPN_FEATURES_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "evpn.h"

/* ============================================================
 * Feature 1: DCI (Data Center Interconnect)
 * ============================================================ */

typedef struct {
    uint32_t dc_id;
    uint32_t gateway_ip;
    bool active;
    uint64_t routes_leaked;
} evpn_dci_peer_t;

int evpn_enable_dci_gateway(evpn_ctx_t *ctx, uint32_t local_dc_id);
int evpn_add_remote_dc(evpn_ctx_t *ctx, uint32_t dc_id, uint32_t gateway_ip);
int evpn_leak_route_to_dc(evpn_ctx_t *ctx, uint32_t target_dc, 
                          evpn_route_type_t type, uint32_t vni);

/* ============================================================
 * Feature 2: Graceful Restart
 * ============================================================ */

typedef enum {
    EVPN_GR_STATE_DISABLED,
    EVPN_GR_STATE_ENABLED,
    EVPN_GR_STATE_RESTARTING,
    EVPN_GR_STATE_RECOVERING
} evpn_gr_state_t;

typedef struct {
    evpn_gr_state_t state;
    uint32_t restart_time;
    uint32_t stale_path_time;
    time_t restart_start;
    bool helper_mode;
} evpn_graceful_restart_t;

int evpn_enable_graceful_restart(evpn_ctx_t *ctx, uint32_t restart_time);
int evpn_gr_start_restart(evpn_ctx_t *ctx);
int evpn_gr_complete_restart(evpn_ctx_t *ctx);
int evpn_gr_mark_stale_routes(evpn_ctx_t *ctx);

/* ============================================================
 * Feature 3: Route Reflector Redundancy
 * ============================================================ */

typedef struct {
    uint32_t rr_ip;
    bool active;
    uint32_t cluster_id;
    time_t last_update;
} evpn_route_reflector_t;

int evpn_add_route_reflector(evpn_ctx_t *ctx, uint32_t rr_ip, uint32_t cluster_id);
int evpn_rr_failover(evpn_ctx_t *ctx, uint32_t failed_rr);
int evpn_get_active_rr(evpn_ctx_t *ctx, uint32_t *rr_ip);

/* ============================================================
 * Feature 4: Extended Communities
 * ============================================================ */

typedef struct {
    uint8_t type;
    uint8_t subtype;
    uint8_t value[6];
} evpn_ext_community_t;

int evpn_add_rt_community(evpn_ctx_t *ctx, uint32_t asn, uint32_t target);
int evpn_add_encap_community(evpn_ctx_t *ctx, uint8_t encap_type);
int evpn_add_color_community(evpn_ctx_t *ctx, uint32_t color);

/* ============================================================
 * Feature 5: Performance Optimizations
 * ============================================================ */

typedef struct {
    void **entries;
    int size;
    int count;
} evpn_hash_table_t;

evpn_hash_table_t* evpn_hash_create(int size);
void evpn_hash_destroy(evpn_hash_table_t *table);
int evpn_hash_insert(evpn_hash_table_t *table, const void *key, void *value);
void* evpn_hash_lookup(evpn_hash_table_t *table, const void *key);

/* Memory pooling */
void* evpn_mempool_alloc(evpn_ctx_t *ctx, size_t size);
void evpn_mempool_free(evpn_ctx_t *ctx, void *ptr);

/* Batch processing */
int evpn_batch_process_routes(evpn_ctx_t *ctx, int max_routes);

/* ============================================================
 * Feature 6: Monitoring & Debugging
 * ============================================================ */

typedef struct {
    uint64_t routes_advertised;
    uint64_t routes_received;
    uint64_t routes_withdrawn;
    uint64_t mac_moves;
    uint64_t arp_suppressed;
    uint64_t failovers;
    uint64_t bgp_updates_sent;
    uint64_t bgp_updates_received;
    time_t uptime_start;
} evpn_statistics_t;

int evpn_get_statistics(evpn_ctx_t *ctx, evpn_statistics_t *stats);
int evpn_reset_statistics(evpn_ctx_t *ctx);
void evpn_dump_statistics(evpn_ctx_t *ctx);
void evpn_enable_debug(evpn_ctx_t *ctx, const char *component);
void evpn_set_log_level(evpn_ctx_t *ctx, int level);

#endif /* EVPN_FEATURES_H */
