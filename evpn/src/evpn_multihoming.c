
/*****************************************************************************
 * Project     : BGP Protocol Implementation for EVPN (RFC 8365)
 * Description : EVPN Multi-homing Implementation
 *                
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : evpn_multihoming.c  
 * Purpose     : Implements:
 * RFC 8365 Section 8 - Multi-homing
 * RFC 7432 Section 8 - Multihoming Functions
 *                
 *****************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <time.h>
#include "../include/evpn.h"
#include "../include/evpn_bgp.h"
#include "../include/evpn_routes.h"
#include "../include/evpn_multihoming.h"

/* ============================================================
 * Ethernet Segment Management
 * ============================================================ */

/**
 * Create Ethernet Segment
 */
int evpn_create_ethernet_segment(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                                  evpn_redundancy_mode_t mode) {
    if (!ctx || !esi) {
        return -1;
    }
    
    // Check if already exists
    if (evpn_find_ethernet_segment(ctx, esi) != NULL) {
        fprintf(stderr, "EVPN: Ethernet Segment already exists\n");
        return -1;
    }
    
    // Allocate new ES
    evpn_ethernet_segment_t *es = (evpn_ethernet_segment_t *)
        malloc(sizeof(evpn_ethernet_segment_t));
    if (!es) {
        perror("malloc ethernet segment");
        return -1;
    }
    
    memset(es, 0, sizeof(*es));
    memcpy(&es->esi, esi, sizeof(evpn_esi_t));
    es->mode = mode;
    es->df_ip = 0;
    es->pe_count = 0;
    pthread_mutex_init(&es->lock, NULL);
    
    // Add to context
    pthread_mutex_lock(&ctx->es_lock);
    
    if (ctx->es_count >= MAX_ETHERNET_SEGMENTS) {
        pthread_mutex_unlock(&ctx->es_lock);
        free(es);
        fprintf(stderr, "EVPN: Maximum Ethernet Segments reached\n");
        return -1;
    }
    
    ctx->ethernet_segments[ctx->es_count++] = es;
    
    pthread_mutex_unlock(&ctx->es_lock);
    
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    
    printf("EVPN: Created Ethernet Segment %s\n", esi_str);
    printf("      Mode: %s\n", 
           mode == EVPN_REDUNDANCY_ALL_ACTIVE ? "All-Active" : "Single-Active");
    
    return 0;
}

/**
 * Delete Ethernet Segment
 */
int evpn_delete_ethernet_segment(evpn_ctx_t *ctx, const evpn_esi_t *esi) {
    if (!ctx || !esi) {
        return -1;
    }
    
    pthread_mutex_lock(&ctx->es_lock);
    
    for (int i = 0; i < ctx->es_count; i++) {
        if (ctx->ethernet_segments[i] &&
            evpn_compare_esi(&ctx->ethernet_segments[i]->esi, esi) == 0) {
            
            evpn_ethernet_segment_t *es = ctx->ethernet_segments[i];
            
            // Remove from array
            for (int j = i; j < ctx->es_count - 1; j++) {
                ctx->ethernet_segments[j] = ctx->ethernet_segments[j + 1];
            }
            ctx->es_count--;
            
            pthread_mutex_unlock(&ctx->es_lock);
            
            pthread_mutex_destroy(&es->lock);
            free(es);
            
            char esi_str[64];
            evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
            printf("EVPN: Deleted Ethernet Segment %s\n", esi_str);
            
            return 0;
        }
    }
    
    pthread_mutex_unlock(&ctx->es_lock);
    
    return -1;
}

/**
 * Find Ethernet Segment by ESI
 */
evpn_ethernet_segment_t *evpn_find_ethernet_segment(evpn_ctx_t *ctx,
                                                     const evpn_esi_t *esi) {
    if (!ctx || !esi) {
        return NULL;
    }
    
    pthread_mutex_lock(&ctx->es_lock);
    
    for (int i = 0; i < ctx->es_count; i++) {
        if (ctx->ethernet_segments[i] &&
            evpn_compare_esi(&ctx->ethernet_segments[i]->esi, esi) == 0) {
            
            evpn_ethernet_segment_t *es = ctx->ethernet_segments[i];
            pthread_mutex_unlock(&ctx->es_lock);
            return es;
        }
    }
    
    pthread_mutex_unlock(&ctx->es_lock);
    
    return NULL;
}

/**
 * Add PE to Ethernet Segment
 */
int evpn_es_add_pe(evpn_ctx_t *ctx, const evpn_esi_t *esi, uint32_t pe_ip) {
    if (!ctx || !esi) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        fprintf(stderr, "EVPN: Ethernet Segment not found\n");
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    
    // Check if already exists
    for (int i = 0; i < es->pe_count; i++) {
        if (es->pe_ips[i] == pe_ip) {
            pthread_mutex_unlock(&es->lock);
            return 0;  // Already exists
        }
    }
    
    // Add new PE
    if (es->pe_count >= MAX_ES_PE_COUNT) {
        pthread_mutex_unlock(&es->lock);
        fprintf(stderr, "EVPN: Maximum PEs per ES reached\n");
        return -1;
    }
    
    es->pe_ips[es->pe_count++] = pe_ip;
    
    pthread_mutex_unlock(&es->lock);
    
    struct in_addr addr;
    addr.s_addr = pe_ip;
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    
    printf("EVPN: Added PE %s to ES %s (total: %d PEs)\n",
           inet_ntoa(addr), esi_str, es->pe_count);
    
    // Trigger DF election
    evpn_df_election(ctx, esi);
    
    return 0;
}

/**
 * Remove PE from Ethernet Segment
 */
int evpn_es_remove_pe(evpn_ctx_t *ctx, const evpn_esi_t *esi, uint32_t pe_ip) {
    if (!ctx || !esi) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    
    for (int i = 0; i < es->pe_count; i++) {
        if (es->pe_ips[i] == pe_ip) {
            // Remove PE
            for (int j = i; j < es->pe_count - 1; j++) {
                es->pe_ips[j] = es->pe_ips[j + 1];
            }
            es->pe_count--;
            
            pthread_mutex_unlock(&es->lock);
            
            struct in_addr addr;
            addr.s_addr = pe_ip;
            printf("EVPN: Removed PE %s from ES\n", inet_ntoa(addr));
            
            // Re-run DF election
            evpn_df_election(ctx, esi);
            
            return 0;
        }
    }
    
    pthread_mutex_unlock(&es->lock);
    
    return -1;
}

/**
 * Get list of PEs on Ethernet Segment
 */
int evpn_es_get_peer_list(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                          uint32_t *pe_ips, int *count) {
    if (!ctx || !esi || !pe_ips || !count) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        *count = 0;
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    
    int max_count = *count;
    int actual_count = es->pe_count < max_count ? es->pe_count : max_count;
    
    memcpy(pe_ips, es->pe_ips, actual_count * sizeof(uint32_t));
    *count = actual_count;
    
    pthread_mutex_unlock(&es->lock);
    
    return 0;
}

/* ============================================================
 * Designated Forwarder (DF) Election
 * ============================================================ */

/**
 * Sort PE IPs in ascending order (for modulo algorithm)
 */
static void sort_pe_ips(uint32_t *pe_ips, int count) {
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (ntohl(pe_ips[i]) > ntohl(pe_ips[j])) {
                uint32_t temp = pe_ips[i];
                pe_ips[i] = pe_ips[j];
                pe_ips[j] = temp;
            }
        }
    }
}

/**
 * Modulo DF Election (RFC 7432 Section 8.5)
 * 
 * DF = PE_list[service_id mod #PEs]
 */
int evpn_df_election_modulo(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                            uint32_t *df_ip) {
    if (!ctx || !esi || !df_ip) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    
    if (es->pe_count == 0) {
        pthread_mutex_unlock(&es->lock);
        return -1;
    }
    
    // Sort PE IPs
    uint32_t sorted_pes[MAX_ES_PE_COUNT];
    memcpy(sorted_pes, es->pe_ips, es->pe_count * sizeof(uint32_t));
    sort_pe_ips(sorted_pes, es->pe_count);
    
    // Service ID = 0 for now (can be VLAN ID for per-VLAN DF)
    uint32_t service_id = 0;
    
    // DF = PE_list[service_id mod #PEs]
    int df_index = service_id % es->pe_count;
    *df_ip = sorted_pes[df_index];
    
    pthread_mutex_unlock(&es->lock);
    
    return 0;
}

/**
 * HRW DF Election (Highest Random Weight)
 * 
 * DF = PE with highest hash(PE_IP, ESI)
 */
int evpn_df_election_hrw(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                         uint32_t *df_ip) {
    if (!ctx || !esi || !df_ip) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    
    if (es->pe_count == 0) {
        pthread_mutex_unlock(&es->lock);
        return -1;
    }
    
    // Simple hash function for HRW
    uint32_t max_hash = 0;
    uint32_t selected_pe = 0;
    
    for (int i = 0; i < es->pe_count; i++) {
        // Hash = (PE_IP XOR ESI_bytes)
        uint32_t hash = es->pe_ips[i];
        for (int j = 0; j < 10; j++) {
            hash ^= (esi->value[j] << (j % 4));
        }
        
        if (i == 0 || hash > max_hash) {
            max_hash = hash;
            selected_pe = es->pe_ips[i];
        }
    }
    
    *df_ip = selected_pe;
    
    pthread_mutex_unlock(&es->lock);
    
    return 0;
}

/**
 * Perform DF election
 */
int evpn_df_election(evpn_ctx_t *ctx, const evpn_esi_t *esi) {
    if (!ctx || !esi) {
        return -1;
    }
    
    return evpn_df_election_with_type(ctx, esi, EVPN_DF_ELECTION_MODULO);
}

/**
 * Perform DF election with specific algorithm
 */
int evpn_df_election_with_type(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                                evpn_df_election_type_t type) {
    if (!ctx || !esi) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return -1;
    }
    
    uint32_t df_ip = 0;
    int ret;
    
    switch (type) {
        case EVPN_DF_ELECTION_MODULO:
            ret = evpn_df_election_modulo(ctx, esi, &df_ip);
            break;
            
        case EVPN_DF_ELECTION_HRW:
            ret = evpn_df_election_hrw(ctx, esi, &df_ip);
            break;
            
        default:
            fprintf(stderr, "EVPN: Unknown DF election type\n");
            return -1;
    }
    
    if (ret != 0) {
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    uint32_t old_df = es->df_ip;
    es->df_ip = df_ip;
    pthread_mutex_unlock(&es->lock);
    
    struct in_addr addr;
    addr.s_addr = df_ip;
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    
    printf("EVPN: DF Election for ES %s\n", esi_str);
    printf("      Algorithm: %s\n", 
           type == EVPN_DF_ELECTION_MODULO ? "Modulo" : "HRW");
    printf("      DF: %s\n", inet_ntoa(addr));
    
    if (df_ip == ctx->router_id) {
        printf("      → We are the DF!\n");
    }
    
    // If DF changed, trigger updates
    if (old_df != df_ip && old_df != 0) {
        printf("      DF changed from %s\n", inet_ntoa((struct in_addr){.s_addr = old_df}));
    }
    
    return 0;
}

/**
 * Check if we are the DF
 */
bool evpn_am_i_df(evpn_ctx_t *ctx, const evpn_esi_t *esi) {
    if (!ctx || !esi) {
        return false;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return false;
    }
    
    pthread_mutex_lock(&es->lock);
    bool is_df = (es->df_ip == ctx->router_id);
    pthread_mutex_unlock(&es->lock);
    
    return is_df;
}

/**
 * Get DF for Ethernet Segment
 */
int evpn_get_df(evpn_ctx_t *ctx, const evpn_esi_t *esi, uint32_t *df_ip) {
    if (!ctx || !esi || !df_ip) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    *df_ip = es->df_ip;
    pthread_mutex_unlock(&es->lock);
    
    return 0;
}

/* ============================================================
 * Split-Horizon Filtering
 * ============================================================ */

/**
 * Check if packet should be filtered (split-horizon)
 */
bool evpn_split_horizon_filter(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                               uint32_t src_ip) {
    if (!ctx || !esi) {
        return false;
    }
    
    // Zero ESI means single-homing, no split-horizon
    if (evpn_is_zero_esi(esi)) {
        return false;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return false;
    }
    
    pthread_mutex_lock(&es->lock);
    
    // Check if source is a peer PE on same ES
    for (int i = 0; i < es->pe_count; i++) {
        if (es->pe_ips[i] == src_ip && src_ip != ctx->router_id) {
            pthread_mutex_unlock(&es->lock);
            return true;  // Filter: received from same ES
        }
    }
    
    pthread_mutex_unlock(&es->lock);
    
    return false;
}

/**
 * Check split-horizon for outgoing interface
 */
bool evpn_split_horizon_check(evpn_ctx_t *ctx,
                              const evpn_esi_t *incoming_esi,
                              const evpn_esi_t *outgoing_esi) {
    if (!ctx || !incoming_esi || !outgoing_esi) {
        return false;
    }
    
    // If either is zero ESI, no filtering
    if (evpn_is_zero_esi(incoming_esi) || evpn_is_zero_esi(outgoing_esi)) {
        return false;
    }
    
    // If same ESI, filter (don't send back to same ES)
    if (evpn_compare_esi(incoming_esi, outgoing_esi) == 0) {
        return true;
    }
    
    return false;
}

/* ============================================================
 * All-Active Multi-homing
 * ============================================================ */

/**
 * Enable all-active mode
 */
int evpn_enable_all_active(evpn_ctx_t *ctx, const evpn_esi_t *esi) {
    if (!ctx || !esi) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    es->mode = EVPN_REDUNDANCY_ALL_ACTIVE;
    pthread_mutex_unlock(&es->lock);
    
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    printf("EVPN: Enabled All-Active mode for ES %s\n", esi_str);
    
    return 0;
}

/**
 * Load balance across PEs in all-active mode
 */
int evpn_multihome_load_balance(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                                const uint8_t *dst_mac, uint32_t *selected_pe) {
    if (!ctx || !esi || !dst_mac || !selected_pe) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    
    if (es->pe_count == 0) {
        pthread_mutex_unlock(&es->lock);
        return -1;
    }
    
    // Simple hash-based load balancing
    uint32_t hash = 0;
    for (int i = 0; i < 6; i++) {
        hash = (hash * 31) + dst_mac[i];
    }
    
    int pe_index = hash % es->pe_count;
    *selected_pe = es->pe_ips[pe_index];
    
    pthread_mutex_unlock(&es->lock);
    
    return 0;
}

/**
 * Handle BUM traffic in all-active mode
 */
int evpn_multihome_bum_forward(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                               const uint8_t *packet, size_t len) {
    if (!ctx || !esi || !packet) {
        return -1;
    }
    
    // Only DF forwards BUM traffic to avoid duplication
    if (!evpn_am_i_df(ctx, esi)) {
        return -1;  // Drop: we are not DF
    }
    
    return 0;  // Forward: we are DF
}

/* ============================================================
 * Utilities
 * ============================================================ */

/**
 * Generate ESI Type 0 (arbitrary)
 */
void evpn_generate_esi_type0(const uint8_t *system_mac, uint32_t discriminator,
                             evpn_esi_t *esi) {
    if (!system_mac || !esi) {
        return;
    }
    
    memset(esi, 0, sizeof(*esi));
    esi->type = 0;  // Arbitrary
    
    // Copy system MAC
    memcpy(esi->value, system_mac, 6);
    
    // Add 3-byte discriminator
    esi->value[6] = (discriminator >> 16) & 0xFF;
    esi->value[7] = (discriminator >> 8) & 0xFF;
    esi->value[8] = discriminator & 0xFF;
}

/**
 * Compare ESIs
 */
int evpn_compare_esi(const evpn_esi_t *esi1, const evpn_esi_t *esi2) {
    if (!esi1 || !esi2) {
        return -1;
    }
    
    if (esi1->type != esi2->type) {
        return esi1->type - esi2->type;
    }
    
    return memcmp(esi1->value, esi2->value, 9);
}

/**
 * Check if ESI is zero
 */
bool evpn_is_zero_esi(const evpn_esi_t *esi) {
    if (!esi) {
        return true;
    }
    
    if (esi->type != 0) {
        return false;
    }
    
    for (int i = 0; i < 9; i++) {
        if (esi->value[i] != 0) {
            return false;
        }
    }
    
    return true;
}

/**
 * Print ESI to string
 */
void evpn_esi_to_string(const evpn_esi_t *esi, char *buf, size_t buf_size) {
    if (!esi || !buf || buf_size < 32) {
        return;
    }
    
    snprintf(buf, buf_size, "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
             esi->type, esi->value[0], esi->value[1], esi->value[2],
             esi->value[3], esi->value[4], esi->value[5],
             esi->value[6], esi->value[7], esi->value[8]);
}

/**
 * Dump Ethernet Segment information
 */
void evpn_dump_segments(evpn_ctx_t *ctx, const evpn_esi_t *esi) {
    if (!ctx) {
        return;
    }
    
    printf("\n=== Ethernet Segments ===\n");
    
    pthread_mutex_lock(&ctx->es_lock);
    
    for (int i = 0; i < ctx->es_count; i++) {
        evpn_ethernet_segment_t *es = ctx->ethernet_segments[i];
        
        if (esi && evpn_compare_esi(&es->esi, esi) != 0) {
            continue;  // Skip if filtering for specific ESI
        }
        
        char esi_str[64];
        evpn_esi_to_string(&es->esi, esi_str, sizeof(esi_str));
        
        printf("\nES %d: %s\n", i + 1, esi_str);
        printf("  Mode: %s\n",
               es->mode == EVPN_REDUNDANCY_ALL_ACTIVE ? "All-Active" : "Single-Active");
        printf("  PE Count: %d\n", es->pe_count);
        
        if (es->pe_count > 0) {
            printf("  PEs:\n");
            for (int j = 0; j < es->pe_count; j++) {
                struct in_addr addr;
                addr.s_addr = es->pe_ips[j];
                printf("    %d. %s", j + 1, inet_ntoa(addr));
                if (es->pe_ips[j] == ctx->router_id) {
                    printf(" (local)");
                }
                printf("\n");
            }
        }
        
        if (es->df_ip != 0) {
            struct in_addr addr;
            addr.s_addr = es->df_ip;
            printf("  DF: %s", inet_ntoa(addr));
            if (es->df_ip == ctx->router_id) {
                printf(" (we are DF)");
            }
            printf("\n");
        }
    }
    
    pthread_mutex_unlock(&ctx->es_lock);
    
    if (ctx->es_count == 0) {
        printf("  (No Ethernet Segments configured)\n");
    }
    
    printf("=========================\n\n");
}
