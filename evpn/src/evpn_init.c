
/*****************************************************************************
 * Project     : BGP Protocol Implementation for EVPN (RFC 8365)
 * Description : EVPN Context Initialization and Cleanup
 *                
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : evpn_init.c  
 * Purpose     : EVPN Network Virtualization Overlay
 *                
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "../include/evpn.h"
#include "../include/evpn_bgp.h"

/**
 * Initialize EVPN context
 */
int evpn_init(evpn_ctx_t *ctx, void *vxlan_ctx, uint32_t local_asn, uint32_t router_id) {
    if (!ctx) {
        fprintf(stderr, "evpn_init: NULL context\n");
        return -1;
    }
    
    memset(ctx, 0, sizeof(evpn_ctx_t));
    
    /* Link to VXLAN data plane */
    ctx->vxlan_ctx = vxlan_ctx;
    
    /* BGP configuration */
    ctx->local_asn = local_asn;
    ctx->router_id = router_id;
    ctx->peer_count = 0;
    
    /* EVPN configuration */
    ctx->evi = 0;  /* Will be set per MAC-VRF */
    ctx->mac_vrf_count = 0;
    
    /* Ethernet Segments */
    ctx->segment_count = 0;
    ctx->ethernet_segments = ctx->segments;  /* Point to segments array */
    if (pthread_mutex_init(&ctx->segment_lock, NULL) != 0) {
        perror("pthread_mutex_init segment_lock");
        return -1;
    }
    
    /* Route Information Base */
    ctx->rib = NULL;
    ctx->rib_count = 0;
    if (pthread_mutex_init(&ctx->rib_lock, NULL) != 0) {
        perror("pthread_mutex_init rib_lock");
        pthread_mutex_destroy(&ctx->segment_lock);
        return -1;
    }
    
    /* Statistics */
    ctx->routes_advertised = 0;
    ctx->routes_withdrawn = 0;
    ctx->routes_received = 0;
    ctx->mac_learned_bgp = 0;
    ctx->mac_moved = 0;
    
    /* Control */
    ctx->running = true;
    
    struct in_addr addr;
    addr.s_addr = router_id;
    printf("EVPN initialized:\n");
    printf("  Local ASN: %u\n", local_asn);
    printf("  Router ID: %s\n", inet_ntoa(addr));
    printf("  VXLAN integration: %s\n", vxlan_ctx ? "Enabled" : "Disabled");
    
    return 0;
}

/**
 * Cleanup EVPN context
 */
void evpn_cleanup(evpn_ctx_t *ctx) {
    if (!ctx) {
        return;
    }
    
    printf("EVPN cleanup started...\n");
    
    /* Stop running */
    ctx->running = false;
    
    /* Cleanup BGP peers */
    for (int i = 0; i < ctx->peer_count; i++) {
        if (ctx->peers[i]) {
            /* Close BGP connection */
            if (ctx->peers[i]->sockfd >= 0) {
                close(ctx->peers[i]->sockfd);
            }
            
            /* Stop peer thread */
            if (ctx->peers[i]->running) {
                ctx->peers[i]->running = false;
                pthread_join(ctx->peers[i]->thread, NULL);
            }
            
            free(ctx->peers[i]);
            ctx->peers[i] = NULL;
        }
    }
    
    /* Cleanup MAC-VRFs */
    for (int i = 0; i < ctx->mac_vrf_count; i++) {
        if (ctx->mac_vrfs[i]) {
            pthread_mutex_destroy(&ctx->mac_vrfs[i]->lock);
            free(ctx->mac_vrfs[i]);
            ctx->mac_vrfs[i] = NULL;
        }
    }
    
    /* Cleanup Ethernet Segments */
    pthread_mutex_lock(&ctx->segment_lock);
    for (int i = 0; i < ctx->segment_count; i++) {
        if (ctx->segments[i]) {
            free(ctx->segments[i]);
            ctx->segments[i] = NULL;
        }
    }
    pthread_mutex_unlock(&ctx->segment_lock);
    
    /* Cleanup RIB */
    pthread_mutex_lock(&ctx->rib_lock);
    evpn_rib_entry_t *entry = ctx->rib;
    while (entry) {
        evpn_rib_entry_t *next = entry->next;
        free(entry);
        entry = next;
    }
    ctx->rib = NULL;
    ctx->rib_count = 0;
    pthread_mutex_unlock(&ctx->rib_lock);
    
    /* Destroy mutexes */
    pthread_mutex_destroy(&ctx->segment_lock);
    pthread_mutex_destroy(&ctx->rib_lock);
    
    printf("EVPN cleanup complete\n");
    printf("  Routes advertised: %lu\n", ctx->routes_advertised);
    printf("  Routes received: %lu\n", ctx->routes_received);
    printf("  MACs learned via BGP: %lu\n", ctx->mac_learned_bgp);
}

/**
 * Add BGP peer (Route Reflector)
 */
int evpn_add_peer(evpn_ctx_t *ctx, uint32_t peer_ip, uint32_t peer_asn) {
    if (!ctx) {
        return -1;
    }
    
    if (ctx->peer_count >= 8) {
        fprintf(stderr, "Maximum number of BGP peers reached (8)\n");
        return -1;
    }
    
    /* Allocate peer structure */
    evpn_bgp_peer_t *peer = (evpn_bgp_peer_t *)calloc(1, sizeof(evpn_bgp_peer_t));
    if (!peer) {
        perror("calloc peer");
        return -1;
    }
    
    /* Initialize peer */
    peer->peer_ip = peer_ip;
    peer->peer_asn = peer_asn;
    peer->peer_port = BGP_PORT;
    peer->state = BGP_STATE_IDLE;
    peer->sockfd = -1;
    peer->running = false;
    
    /* Add to context */
    ctx->peers[ctx->peer_count] = peer;
    ctx->peer_count++;
    
    struct in_addr addr;
    addr.s_addr = peer_ip;
    printf("BGP peer added: %s (ASN %u)\n", inet_ntoa(addr), peer_asn);
    
    /* Note: BGP session will be started by main thread */
    
    return 0;
}

/**
 * Create MAC-VRF for VNI
 */
int evpn_create_mac_vrf(evpn_ctx_t *ctx, uint32_t vni, evpn_rd_t *rd, evpn_rt_t *rt) {
    if (!ctx || !rd || !rt) {
        return -1;
    }
    
    if (ctx->mac_vrf_count >= 256) {
        fprintf(stderr, "Maximum number of MAC-VRFs reached (256)\n");
        return -1;
    }
    
    /* Allocate MAC-VRF */
    evpn_mac_vrf_t *vrf = (evpn_mac_vrf_t *)calloc(1, sizeof(evpn_mac_vrf_t));
    if (!vrf) {
        perror("calloc MAC-VRF");
        return -1;
    }
    
    /* Initialize MAC-VRF */
    vrf->vni = vni;
    memcpy(&vrf->rd, rd, sizeof(evpn_rd_t));
    
    /* Add Route Target */
    memcpy(&vrf->rt_import[0], rt, sizeof(evpn_rt_t));
    memcpy(&vrf->rt_export[0], rt, sizeof(evpn_rt_t));
    vrf->rt_import_count = 1;
    vrf->rt_export_count = 1;
    
    /* Initialize mutex */
    if (pthread_mutex_init(&vrf->lock, NULL) != 0) {
        perror("pthread_mutex_init vrf lock");
        free(vrf);
        return -1;
    }
    
    /* MAC table linked to VXLAN (will be set during operation) */
    vrf->mac_table = NULL;
    vrf->local_mac_count = 0;
    vrf->remote_mac_count = 0;
    
    /* Add to context */
    ctx->mac_vrfs[ctx->mac_vrf_count] = vrf;
    ctx->mac_vrf_count++;
    
    printf("MAC-VRF created for VNI %u\n", vni);
    printf("  RD: %u:%u\n", rd->value.asn_based.asn, rd->value.asn_based.number);
    printf("  RT: %u:%u\n", rt->value.asn_based.asn, rt->value.asn_based.number);
    
    return 0;
}

/**
 * Get statistics
 */
void evpn_get_stats(evpn_ctx_t *ctx, uint64_t *routes_adv, uint64_t *routes_rcv,
                    uint64_t *mac_learned, uint64_t *mac_moved) {
    if (!ctx) {
        return;
    }
    
    if (routes_adv) *routes_adv = ctx->routes_advertised;
    if (routes_rcv) *routes_rcv = ctx->routes_received;
    if (mac_learned) *mac_learned = ctx->mac_learned_bgp;
    if (mac_moved) *mac_moved = ctx->mac_moved;
}

/**
 * Dump RIB contents (debugging)
 */
void evpn_dump_rib(evpn_ctx_t *ctx) {
    if (!ctx) {
        return;
    }
    
    printf("\n=== EVPN Route Information Base ===\n");
    printf("Total routes: %d\n\n", ctx->rib_count);
    
    pthread_mutex_lock(&ctx->rib_lock);
    
    evpn_rib_entry_t *entry = ctx->rib;
    int count = 0;
    
    while (entry && count < 100) {  /* Limit output */
        printf("Route %d:\n", ++count);
        printf("  Type: %d\n", entry->type);
        printf("  Local: %s\n", entry->local ? "Yes" : "No");
        
        struct in_addr addr;
        addr.s_addr = entry->next_hop;
        printf("  Next Hop: %s\n", inet_ntoa(addr));
        
        if (entry->type == EVPN_ROUTE_TYPE_MAC_IP_ADVERTISEMENT) {
            printf("  MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                   entry->route.mac_ip.mac[0], entry->route.mac_ip.mac[1],
                   entry->route.mac_ip.mac[2], entry->route.mac_ip.mac[3],
                   entry->route.mac_ip.mac[4], entry->route.mac_ip.mac[5]);
            
            if (entry->route.mac_ip.ip_len > 0) {
                addr.s_addr = entry->route.mac_ip.ip;
                printf("  IP: %s\n", inet_ntoa(addr));
            }
            
            printf("  VNI: %u\n", entry->route.mac_ip.label1);
        }
        
        printf("\n");
        entry = entry->next;
    }
    
    pthread_mutex_unlock(&ctx->rib_lock);
    
    if (count >= 100 && ctx->rib_count > 100) {
        printf("... and %d more routes\n", ctx->rib_count - 100);
    }
    
    printf("===================================\n\n");
}

