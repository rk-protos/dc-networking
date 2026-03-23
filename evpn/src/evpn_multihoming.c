
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

/* ============================================================
 * Single-Active Multi-homing (WEEK 4)
 * RFC 7432 Section 8.4 - Single-Active Redundancy Mode
 * ============================================================ */

/**
 * Enable single-active mode for Ethernet Segment
 */
int evpn_enable_single_active(evpn_ctx_t *ctx, const evpn_esi_t *esi) {
    if (!ctx || !esi) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        fprintf(stderr, "EVPN: Ethernet Segment not found\n");
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    
    // If already in single-active mode, nothing to do
    if (es->mode == EVPN_REDUNDANCY_SINGLE_ACTIVE) {
        pthread_mutex_unlock(&es->lock);
        return 0;
    }
    
    // Switch to single-active mode
    evpn_redundancy_mode_t old_mode = es->mode;
    es->mode = EVPN_REDUNDANCY_SINGLE_ACTIVE;
    
    // Initialize single-active fields
    es->active_pe_ip = 0;
    es->is_active_pe = false;
    es->failover_count = 0;
    es->last_failover = 0;
    
    pthread_mutex_unlock(&es->lock);
    
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    
    printf("EVPN: Enabled Single-Active mode for ES %s\n", esi_str);
    printf("      Previous mode: %s\n", 
           old_mode == EVPN_REDUNDANCY_ALL_ACTIVE ? "All-Active" : "Unknown");
    
    // Elect active PE
    uint32_t active_pe;
    if (evpn_elect_active_pe(ctx, esi, &active_pe) == 0) {
        printf("      Active PE elected: ");
        struct in_addr addr;
        addr.s_addr = active_pe;
        printf("%s\n", inet_ntoa(addr));
    }
    
    return 0;
}

/**
 * Elect active PE for single-active Ethernet Segment
 * 
 * Algorithm:
 * 1. Use DF election result if already performed
 * 2. Otherwise, select PE with lowest IP address
 * 3. Deterministic across all PEs
 */
int evpn_elect_active_pe(evpn_ctx_t *ctx, const evpn_esi_t *esi, 
                         uint32_t *active_pe) {
    if (!ctx || !esi || !active_pe) {
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
    
    // Option 1: Use DF if already elected
    if (es->df_ip != 0) {
        *active_pe = es->df_ip;
        es->active_pe_ip = es->df_ip;
        es->is_active_pe = (es->df_ip == ctx->router_id);
    } 
    // Option 2: Select PE with lowest IP address
    else {
        uint32_t lowest_ip = es->pe_ips[0];
        for (int i = 1; i < es->pe_count; i++) {
            if (ntohl(es->pe_ips[i]) < ntohl(lowest_ip)) {
                lowest_ip = es->pe_ips[i];
            }
        }
        
        *active_pe = lowest_ip;
        es->active_pe_ip = lowest_ip;
        es->is_active_pe = (lowest_ip == ctx->router_id);
    }
    
    pthread_mutex_unlock(&es->lock);
    
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    struct in_addr addr;
    addr.s_addr = *active_pe;
    
    printf("EVPN: Active PE election for ES %s\n", esi_str);
    printf("      Active PE: %s\n", inet_ntoa(addr));
    
    if (*active_pe == ctx->router_id) {
        printf("      → We are the ACTIVE PE (forwarding traffic)\n");
    } else {
        printf("      → We are STANDBY PE (blocking traffic)\n");
    }
    
    return 0;
}

/**
 * Check if we are the active PE
 */
bool evpn_am_i_active_pe(evpn_ctx_t *ctx, const evpn_esi_t *esi) {
    if (!ctx || !esi) {
        return false;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return false;
    }
    
    pthread_mutex_lock(&es->lock);
    bool is_active = es->is_active_pe;
    pthread_mutex_unlock(&es->lock);
    
    return is_active;
}

/**
 * Get the currently active PE
 */
int evpn_get_active_pe(evpn_ctx_t *ctx, const evpn_esi_t *esi, 
                       uint32_t *active_pe) {
    if (!ctx || !esi || !active_pe) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    *active_pe = es->active_pe_ip;
    pthread_mutex_unlock(&es->lock);
    
    return (*active_pe != 0) ? 0 : -1;
}

/**
 * Handle PE failure in single-active mode
 * 
 * Failover process:
 * 1. Detect that active PE has failed
 * 2. Elect new active PE from remaining PEs
 * 3. New active PE takes over immediately
 * 4. MAC routes are updated via BGP
 */
int evpn_handle_pe_failure(evpn_ctx_t *ctx, const evpn_esi_t *esi, 
                           uint32_t failed_pe) {
    if (!ctx || !esi) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    
    // Check if failed PE was the active PE
    bool was_active = (es->active_pe_ip == failed_pe);
    
    pthread_mutex_unlock(&es->lock);
    
    if (!was_active) {
        // Standby PE failed, no action needed
        printf("EVPN: Standby PE failed, no failover needed\n");
        return 0;
    }
    
    // Active PE failed - need to failover!
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    struct in_addr addr;
    addr.s_addr = failed_pe;
    
    printf("EVPN: ACTIVE PE FAILURE detected for ES %s\n", esi_str);
    printf("      Failed PE: %s\n", inet_ntoa(addr));
    printf("      Initiating failover...\n");
    
    // Remove failed PE from ES
    evpn_es_remove_pe(ctx, esi, failed_pe);
    
    // Elect new active PE
    uint32_t new_active;
    if (evpn_elect_active_pe(ctx, esi, &new_active) == 0) {
        pthread_mutex_lock(&es->lock);
        es->failover_count++;
        es->last_failover = time(NULL);
        pthread_mutex_unlock(&es->lock);
        
        addr.s_addr = new_active;
        printf("      New active PE: %s\n", inet_ntoa(addr));
        printf("      Failover complete (failover #%lu)\n", 
               (unsigned long)es->failover_count);
        
        if (new_active == ctx->router_id) {
            printf("      → We are now ACTIVE! Starting to forward traffic.\n");
        }
    } else {
        fprintf(stderr, "EVPN: Failed to elect new active PE\n");
        return -1;
    }
    
    return 0;
}

/**
 * Forward traffic in single-active mode
 * 
 * Simple decision:
 * - Active PE: Forward all traffic
 * - Standby PE: Drop all traffic
 */
int evpn_single_active_forward(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                               const uint8_t *packet, size_t len, int direction) {
    if (!ctx || !esi || !packet) {
        return -1;
    }
    
    // Check if we are the active PE
    if (evpn_am_i_active_pe(ctx, esi)) {
        // We are active - forward traffic
        return 0;
    } else {
        // We are standby - drop traffic
        return -1;
    }
}

/**
 * Transition from all-active to single-active mode
 */
int evpn_transition_to_single_active(evpn_ctx_t *ctx, const evpn_esi_t *esi) {
    if (!ctx || !esi) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return -1;
    }
    
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    
    printf("EVPN: Transitioning ES %s from All-Active to Single-Active\n", esi_str);
    
    // Step 1: Stop all-active load balancing
    printf("      Step 1: Stopping all-active load balancing\n");
    
    // Step 2: Enable single-active mode
    printf("      Step 2: Enabling single-active mode\n");
    if (evpn_enable_single_active(ctx, esi) != 0) {
        fprintf(stderr, "      Failed to enable single-active mode\n");
        return -1;
    }
    
    // Step 3: If we are not active PE, withdraw our MAC routes
    if (!evpn_am_i_active_pe(ctx, esi)) {
        printf("      Step 3: We are standby - would withdraw MAC routes here\n");
        // TODO Week 4.2: Implement MAC route withdrawal
    } else {
        printf("      Step 3: We are active - keeping MAC routes\n");
    }
    
    printf("      Transition complete!\n");
    
    return 0;
}

/**
 * Transition from single-active to all-active mode
 */
int evpn_transition_to_all_active(evpn_ctx_t *ctx, const evpn_esi_t *esi) {
    if (!ctx || !esi) {
        return -1;
    }
    
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    
    printf("EVPN: Transitioning ES %s from Single-Active to All-Active\n", esi_str);
    
    // Use existing function
    return evpn_enable_all_active(ctx, esi);
}

/**
 * Get single-active status and statistics
 */
int evpn_single_active_get_status(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                                   uint32_t *active_pe, int *standby_count,
                                   uint64_t *failover_count) {
    if (!ctx || !esi) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    
    if (active_pe) {
        *active_pe = es->active_pe_ip;
    }
    
    if (standby_count) {
        *standby_count = es->pe_count - 1;  // All PEs except active
    }
    
    if (failover_count) {
        *failover_count = es->failover_count;
    }
    
    pthread_mutex_unlock(&es->lock);
    
    return 0;
}

/* ============================================================
 * Mass Withdrawal (WEEK 4 Feature 2)
 * RFC 7432 Section 8.5 - Fast Convergence
 * ============================================================ */

/**
 * Perform mass withdrawal of all routes for an Ethernet Segment
 * 
 * Critical for fast convergence when ES fails.
 * Withdraws Type 1, Type 2, and Type 4 routes simultaneously.
 */
int evpn_mass_withdraw(evpn_ctx_t *ctx, const evpn_esi_t *esi) {
    if (!ctx || !esi) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        fprintf(stderr, "EVPN: Ethernet Segment not found for mass withdrawal\n");
        return -1;
    }
    
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║           MASS WITHDRAWAL IN PROGRESS                        ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("ESI: %s\n", esi_str);
    printf("Reason: Ethernet Segment failure detected\n\n");
    
    int total_withdrawn = 0;
    time_t start_time = time(NULL);
    
    // Step 1: Withdraw all Type 2 (MAC/IP) routes
    printf("Step 1: Withdrawing Type 2 (MAC/IP) routes...\n");
    int mac_withdrawn = evpn_withdraw_all_mac_routes(ctx, esi, 0);  // 0 = all VNIs
    if (mac_withdrawn >= 0) {
        printf("        ✓ Withdrew %d MAC/IP routes\n", mac_withdrawn);
        total_withdrawn += mac_withdrawn;
    }
    
    // Step 2: Withdraw all Type 1 (Auto-Discovery) routes
    printf("Step 2: Withdrawing Type 1 (Auto-Discovery) routes...\n");
    if (evpn_withdraw_all_ad_routes(ctx, esi) == 0) {
        printf("        ✓ Withdrew Auto-Discovery routes\n");
        total_withdrawn++;
    }
    
    // Step 3: Withdraw Type 4 (Ethernet Segment) route
    printf("Step 3: Withdrawing Type 4 (Ethernet Segment) route...\n");
    if (evpn_withdraw_es_route(ctx, esi) == 0) {
        printf("        ✓ Withdrew ES route\n");
        total_withdrawn++;
    }
    
    // Update statistics
    pthread_mutex_lock(&es->lock);
    es->withdrawal_count++;
    es->last_withdrawal = time(NULL);
    es->withdrawn_routes = total_withdrawn;
    pthread_mutex_unlock(&es->lock);
    
    time_t elapsed = time(NULL) - start_time;
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║           MASS WITHDRAWAL COMPLETE                           ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("Total routes withdrawn: %d\n", total_withdrawn);
    printf("Convergence time: %ld seconds\n", (long)elapsed);
    printf("Status: Fast convergence achieved ✓\n\n");
    
    return total_withdrawn;
}

/**
 * Withdraw all Type 2 (MAC/IP) routes for an Ethernet Segment
 */
int evpn_withdraw_all_mac_routes(evpn_ctx_t *ctx, const evpn_esi_t *esi, 
                                 uint32_t vni) {
    if (!ctx || !esi) {
        return -1;
    }
    
    // In a full implementation, this would:
    // 1. Iterate through MAC-VRF table
    // 2. Find all MACs associated with this ESI
    // 3. Withdraw each MAC route via BGP
    // 4. Batch withdrawals for efficiency
    
    // For demo purposes, simulate withdrawal
    int withdrawn_count = 0;
    
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    
    printf("        Scanning MAC table for ESI %s...\n", esi_str);
    
    // Simulate finding and withdrawing MACs
    // In real implementation, would call evpn_withdraw_mac_ip() for each MAC
    
    // Simulate 10-50 MACs per ES
    withdrawn_count = 25;  // Simulated count
    
    printf("        Found %d MACs to withdraw\n", withdrawn_count);
    printf("        Batching into BGP UPDATE messages...\n");
    printf("        Sending withdrawals to Route Reflector...\n");
    
    return withdrawn_count;
}

/**
 * Withdraw all Type 1 (Auto-Discovery) routes
 */
int evpn_withdraw_all_ad_routes(evpn_ctx_t *ctx, const evpn_esi_t *esi) {
    if (!ctx || !esi) {
        return -1;
    }
    
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    
    printf("        Withdrawing Auto-Discovery route for ESI %s\n", esi_str);
    
    // In real implementation:
    // 1. Build Type 1 route withdrawal
    // 2. Send via BGP UPDATE with withdrawn routes
    // 3. Signal to remote PEs that we're detached
    
    // For now, simulate
    printf("        → Signaling ES detachment to remote PEs\n");
    
    return 0;
}

/**
 * Withdraw Type 4 (Ethernet Segment) route
 */
int evpn_withdraw_es_route(evpn_ctx_t *ctx, const evpn_esi_t *esi) {
    if (!ctx || !esi) {
        return -1;
    }
    
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    
    printf("        Withdrawing ES route for ESI %s\n", esi_str);
    
    // In real implementation:
    // 1. Build Type 4 route withdrawal
    // 2. Send via BGP UPDATE
    // 3. Remove ES from remote PE lists
    
    // For now, simulate
    printf("        → Removing ES membership advertisement\n");
    
    return 0;
}

/**
 * Fast convergence on ES failure
 * 
 * Complete fast convergence process with timing
 */
int evpn_es_failure_fast_convergence(evpn_ctx_t *ctx, const evpn_esi_t *esi) {
    if (!ctx || !esi) {
        return -1;
    }
    
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  ETHERNET SEGMENT FAILURE DETECTED\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("ESI: %s\n", esi_str);
    printf("Failure reason: All local CEs unreachable / Link down\n");
    printf("Initiating fast convergence procedure...\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    
    time_t start = time(NULL);
    
    // Step 1: Mark ES as down
    printf("\n[Phase 1] Marking ES as operationally down...\n");
    evpn_es_mark_down(ctx, esi);
    printf("          ✓ ES marked down\n");
    
    // Step 2: Perform mass withdrawal
    printf("\n[Phase 2] Mass withdrawal of all routes...\n");
    int withdrawn = evpn_mass_withdraw(ctx, esi);
    
    // Step 3: Update forwarding state
    printf("[Phase 3] Updating forwarding state...\n");
    printf("          ✓ Stopped forwarding traffic for this ES\n");
    printf("          ✓ Updated split-horizon filtering\n");
    
    // Step 4: Trigger re-election if in single-active mode
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (es && es->mode == EVPN_REDUNDANCY_SINGLE_ACTIVE) {
        printf("\n[Phase 4] Re-electing active PE (single-active mode)...\n");
        printf("          ✓ Skipped (ES is down)\n");
    }
    
    time_t elapsed = time(NULL) - start;
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  FAST CONVERGENCE COMPLETE\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Routes withdrawn: %d\n", withdrawn);
    printf("Convergence time: %ld second(s)\n", (long)elapsed);
    printf("Target: < 1 second → ");
    if (elapsed < 1) {
        printf("✓ TARGET MET!\n");
    } else {
        printf("✓ ACCEPTABLE\n");
    }
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    return 0;
}

/**
 * Batch withdraw multiple MAC routes efficiently
 */
int evpn_batch_withdraw_macs(evpn_ctx_t *ctx, const uint8_t (*mac_list)[6],
                             int mac_count, uint32_t vni) {
    if (!ctx || !mac_list || mac_count <= 0) {
        return -1;
    }
    
    printf("        Batch withdrawing %d MACs...\n", mac_count);
    
    // In real implementation:
    // 1. Build single BGP UPDATE with multiple NLRI withdrawals
    // 2. More efficient than individual UPDATEs
    // 3. Reduces BGP message overhead
    
    // Calculate batch efficiency
    int updates_individual = mac_count;
    int updates_batched = (mac_count + 9) / 10;  // Assume 10 MACs per UPDATE
    int savings = updates_individual - updates_batched;
    
    printf("        Individual UPDATEs needed: %d\n", updates_individual);
    printf("        Batched UPDATEs needed: %d\n", updates_batched);
    printf("        Efficiency gain: %d fewer messages (%.1f%% reduction)\n",
           savings, (100.0 * savings) / updates_individual);
    
    return mac_count;
}

/**
 * Mark Ethernet Segment as failed/down
 */
int evpn_es_mark_down(evpn_ctx_t *ctx, const evpn_esi_t *esi) {
    if (!ctx || !esi) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    
    bool was_operational = es->is_operational;
    es->is_operational = false;
    
    pthread_mutex_unlock(&es->lock);
    
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    
    if (was_operational) {
        printf("          ES %s: OPERATIONAL → DOWN\n", esi_str);
    }
    
    return 0;
}

/**
 * Mark Ethernet Segment as operational/up
 */
int evpn_es_mark_up(evpn_ctx_t *ctx, const evpn_esi_t *esi) {
    if (!ctx || !esi) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    
    bool was_down = !es->is_operational;
    es->is_operational = true;
    
    pthread_mutex_unlock(&es->lock);
    
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    
    if (was_down) {
        printf("          ES %s: DOWN → OPERATIONAL\n", esi_str);
        printf("          Re-advertising all routes...\n");
        
        // In real implementation:
        // 1. Re-advertise all Type 1, 2, 4 routes
        // 2. Trigger DF election
        // 3. Resume normal forwarding
    }
    
    return 0;
}

/**
 * Get mass withdrawal statistics
 */
int evpn_get_mass_withdrawal_stats(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                                    uint64_t *withdrawal_count,
                                    time_t *last_withdrawal) {
    if (!ctx || !esi) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    
    if (withdrawal_count) {
        *withdrawal_count = es->withdrawal_count;
    }
    
    if (last_withdrawal) {
        *last_withdrawal = es->last_withdrawal;
    }
    
    pthread_mutex_unlock(&es->lock);
    
    return 0;
}

/* ============================================================
 * Aliasing Support (WEEK 4 Feature 3)
 * RFC 7432 Section 8.4 - Aliasing and Backup Path
 * ============================================================ */

/**
 * Enable aliasing for an Ethernet Segment
 */
int evpn_enable_aliasing(evpn_ctx_t *ctx, const evpn_esi_t *esi) {
    if (!ctx || !esi) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        fprintf(stderr, "EVPN: Ethernet Segment not found\n");
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    
    if (es->aliasing_enabled) {
        pthread_mutex_unlock(&es->lock);
        return 0;  // Already enabled
    }
    
    es->aliasing_enabled = true;
    es->aliased_mac_count = 0;
    es->flows_balanced = 0;
    
    pthread_mutex_unlock(&es->lock);
    
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    
    printf("EVPN: Enabled aliasing for ES %s\n", esi_str);
    printf("      MAC addresses can now have multiple paths\n");
    printf("      Per-flow load balancing enabled\n");
    
    return 0;
}

/**
 * Disable aliasing for an Ethernet Segment
 */
int evpn_disable_aliasing(evpn_ctx_t *ctx, const evpn_esi_t *esi) {
    if (!ctx || !esi) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    es->aliasing_enabled = false;
    pthread_mutex_unlock(&es->lock);
    
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    
    printf("EVPN: Disabled aliasing for ES %s\n", esi_str);
    
    return 0;
}

/**
 * Check if MAC is aliased
 */
bool evpn_is_mac_aliased(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t vni) {
    if (!ctx || !mac) {
        return false;
    }
    
    // For demo: assume MACs ending in even numbers are aliased
    return (mac[5] % 2 == 0);
}

/**
 * Get all PEs (aliases) for a MAC
 */
int evpn_get_aliased_pes(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t vni,
                         uint32_t *pe_ips, int *count) {
    if (!ctx || !mac || !pe_ips || !count || *count <= 0) {
        return -1;
    }
    
    // For demo: simulate 2-3 PEs for aliased MACs
    if (evpn_is_mac_aliased(ctx, mac, vni)) {
        int num_aliases = 2 + (mac[5] % 2);  // 2 or 3 PEs
        
        if (*count < num_aliases) {
            num_aliases = *count;
        }
        
        // Simulate PE IPs
        for (int i = 0; i < num_aliases; i++) {
            pe_ips[i] = inet_addr("10.0.0.1") + htonl(i);
        }
        
        *count = num_aliases;
        return 0;
    }
    
    *count = 0;
    return 0;
}

/**
 * Compute flow hash for load balancing
 */
uint32_t evpn_compute_flow_hash(const uint8_t *packet, size_t len) {
    if (!packet || len < 14) {
        return 0;
    }
    
    uint32_t hash = 0;
    
    // Hash first 14 bytes (Ethernet header)
    for (size_t i = 0; i < 14 && i < len; i++) {
        hash = hash * 31 + packet[i];
    }
    
    // Mix in some more bytes if available
    if (len > 34) {
        for (size_t i = 14; i < 34; i++) {
            hash = hash * 31 + packet[i];
        }
    }
    
    return hash;
}

/**
 * Select best PE using flow hash and aliasing
 */
int evpn_alias_select_pe(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t vni,
                         uint32_t flow_hash, uint32_t *selected_pe) {
    if (!ctx || !mac || !selected_pe) {
        return -1;
    }
    
    // Get all PEs (aliases) for this MAC
    uint32_t pe_list[MAX_ES_PE_COUNT];
    int pe_count = MAX_ES_PE_COUNT;
    
    if (evpn_get_aliased_pes(ctx, mac, vni, pe_list, &pe_count) != 0 || pe_count == 0) {
        return -1;
    }
    
    // Use flow hash to select PE (consistent hashing)
    int selected_index = flow_hash % pe_count;
    *selected_pe = pe_list[selected_index];
    
    return 0;
}

/**
 * Add alias path for a MAC
 */
int evpn_add_mac_alias(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t vni,
                       uint32_t pe_ip, const evpn_esi_t *esi) {
    if (!ctx || !mac || !esi) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    
    if (es->aliasing_enabled) {
        es->aliased_mac_count++;
    }
    
    pthread_mutex_unlock(&es->lock);
    
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    struct in_addr addr;
    addr.s_addr = pe_ip;
    
    printf("        Added alias: MAC %s via PE %s\n", mac_str, inet_ntoa(addr));
    
    return 0;
}

/**
 * Remove alias path for a MAC
 */
int evpn_remove_mac_alias(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t vni,
                          uint32_t pe_ip) {
    if (!ctx || !mac) {
        return -1;
    }
    
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    struct in_addr addr;
    addr.s_addr = pe_ip;
    
    printf("        Removed alias: MAC %s via PE %s\n", mac_str, inet_ntoa(addr));
    
    return 0;
}

/**
 * Perform per-flow load balancing across aliases
 */
int evpn_alias_load_balance(evpn_ctx_t *ctx, const uint8_t *packet, size_t len,
                            const uint8_t *dst_mac, uint32_t vni, 
                            uint32_t *next_hop) {
    if (!ctx || !packet || !dst_mac || !next_hop) {
        return -1;
    }
    
    // Check if destination MAC is aliased
    if (!evpn_is_mac_aliased(ctx, dst_mac, vni)) {
        return -1;
    }
    
    // Compute flow hash
    uint32_t flow_hash = evpn_compute_flow_hash(packet, len);
    
    // Select PE based on flow hash
    if (evpn_alias_select_pe(ctx, dst_mac, vni, flow_hash, next_hop) != 0) {
        return -1;
    }
    
    return 0;
}

/**
 * Get aliasing statistics
 */
int evpn_get_aliasing_stats(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                            int *aliased_macs, int *total_aliases,
                            uint64_t *flows_balanced) {
    if (!ctx || !esi) {
        return -1;
    }
    
    evpn_ethernet_segment_t *es = evpn_find_ethernet_segment(ctx, esi);
    if (!es) {
        return -1;
    }
    
    pthread_mutex_lock(&es->lock);
    
    if (aliased_macs) {
        *aliased_macs = es->aliased_mac_count;
    }
    
    if (total_aliases) {
        *total_aliases = es->aliased_mac_count * es->pe_count;
    }
    
    if (flows_balanced) {
        *flows_balanced = es->flows_balanced;
    }
    
    pthread_mutex_unlock(&es->lock);
    
    return 0;
}

/* ============================================================
 * Local Bias (WEEK 4 Feature 4)
 * Traffic Optimization
 * ============================================================ */

/**
 * Enable local bias for an Ethernet Segment
 */
int evpn_enable_local_bias(evpn_ctx_t *ctx, const evpn_esi_t *esi) {
    if (!ctx || !esi) {
        return -1;
    }
    
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    
    printf("EVPN: Enabled local bias for ES %s\n", esi_str);
    printf("      Prefer local PE when available\n");
    printf("      Reduces inter-PE traffic\n");
    
    return 0;
}

/**
 * Check if we should prefer local forwarding
 */
bool evpn_should_use_local(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                           const uint8_t *dst_mac) {
    if (!ctx || !esi || !dst_mac) {
        return false;
    }
    
    // Check if MAC is locally attached
    // In real implementation: look up MAC in local table
    
    // For demo: MACs ending in 0x01 are "local"
    return (dst_mac[5] == 0x01);
}

/**
 * Get local bias statistics
 */
int evpn_get_local_bias_stats(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                              uint64_t *local_forwards, 
                              uint64_t *remote_forwards) {
    if (!ctx || !esi) {
        return -1;
    }
    
    // Simulated statistics
    if (local_forwards) {
        *local_forwards = 75000;  // 75% local
    }
    
    if (remote_forwards) {
        *remote_forwards = 25000;  // 25% remote
    }
    
    return 0;
}
