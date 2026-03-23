
/*****************************************************************************
 * Project     : EVPN Protocol Implementation (RFC 8365)
 * Description :  EVPN Route Types Processing
 *                
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : evpn_routes.c  
 * Purpose     : 
 *                This includes :
 *                 RFC 8365 - EVPN Network Virtualization Overlay
 *                 RFC 7432 - BGP MPLS-Based Ethernet VPN
 * 
 *                 Implements encoding/decoding of EVPN NLRI for route types 1-5
 *****************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "../include/evpn.h"
#include "../include/evpn_bgp.h"
#include "../include/evpn_routes.h"
#include "../include/evpn_multihoming.h"

/* ============================================================
 * Helper Functions
 * ============================================================ */

/**
 * Encode MPLS label (20 bits)
 */
void evpn_encode_label(uint32_t label, uint8_t *buf) {
    // MPLS label format: 20 bits label + 3 bits exp + 1 bit S + 8 bits TTL
    // For EVPN, we set S=1 (bottom of stack) and TTL=0
    uint32_t label_stack = ((label & 0xFFFFF) << 12) | 0x00000100;
    
    buf[0] = (label_stack >> 16) & 0xFF;
    buf[1] = (label_stack >> 8) & 0xFF;
    buf[2] = label_stack & 0xFF;
}

/**
 * Decode MPLS label
 */
uint32_t evpn_decode_label(const uint8_t *buf) {
    uint32_t label_stack = ((uint32_t)buf[0] << 16) | 
                          ((uint32_t)buf[1] << 8) | 
                          buf[2];
    return (label_stack >> 12) & 0xFFFFF;
}

/**
 * Encode ESI (10 bytes)
 */
void evpn_encode_esi(const evpn_esi_t *esi, uint8_t *buf) {
    buf[0] = esi->type;
    memcpy(buf + 1, esi->value, 9);
}

/**
 * Decode ESI
 */
void evpn_decode_esi(const uint8_t *buf, evpn_esi_t *esi) {
    esi->type = buf[0];
    memcpy(esi->value, buf + 1, 9);
}

/**
 * Encode Route Distinguisher (8 bytes)
 */
void evpn_encode_rd(const evpn_rd_t *rd, uint8_t *buf) {
    *(uint16_t *)buf = htons(rd->type);
    
    if (rd->type == 0) {
        // Type 0: ASN:nn (2-byte ASN)
        *(uint16_t *)(buf + 2) = htons(rd->value.asn_based.asn & 0xFFFF);
        *(uint32_t *)(buf + 4) = htonl(rd->value.asn_based.number);
    } else if (rd->type == 1) {
        // Type 1: IP:nn
        *(uint32_t *)(buf + 2) = rd->value.ip_based.ip;
        *(uint16_t *)(buf + 6) = htons(rd->value.ip_based.number);
    } else if (rd->type == 2) {
        // Type 2: 4-byte ASN:nn
        *(uint32_t *)(buf + 2) = htonl(rd->value.asn_based.asn);
        *(uint16_t *)(buf + 6) = htons(rd->value.asn_based.number & 0xFFFF);
    }
}

/**
 * Decode Route Distinguisher
 */
void evpn_decode_rd(const uint8_t *buf, evpn_rd_t *rd) {
    rd->type = ntohs(*(uint16_t *)buf);
    
    if (rd->type == 0) {
        rd->value.asn_based.asn = ntohs(*(uint16_t *)(buf + 2));
        rd->value.asn_based.number = ntohl(*(uint32_t *)(buf + 4));
    } else if (rd->type == 1) {
        rd->value.ip_based.ip = *(uint32_t *)(buf + 2);
        rd->value.ip_based.number = ntohs(*(uint16_t *)(buf + 6));
    } else if (rd->type == 2) {
        rd->value.asn_based.asn = ntohl(*(uint32_t *)(buf + 2));
        rd->value.asn_based.number = ntohs(*(uint16_t *)(buf + 6));
    }
}

/**
 * Get route type name
 */
const char *evpn_route_type_name(evpn_route_type_t type) {
    switch (type) {
        case EVPN_ROUTE_TYPE_ETHERNET_AD:
            return "Ethernet Auto-Discovery";
        case EVPN_ROUTE_TYPE_MAC_IP_ADVERTISEMENT:
            return "MAC/IP Advertisement";
        case EVPN_ROUTE_TYPE_INCLUSIVE_MULTICAST:
            return "Inclusive Multicast";
        case EVPN_ROUTE_TYPE_ETHERNET_SEGMENT:
            return "Ethernet Segment";
        case EVPN_ROUTE_TYPE_IP_PREFIX:
            return "IP Prefix";
        default:
            return "Unknown";
    }
}

/**
 * Validate EVPN NLRI
 */
bool evpn_validate_nlri(const uint8_t *nlri, size_t nlri_len) {
    if (!nlri || nlri_len < 2) {
        return false;
    }
    
    uint8_t route_type = nlri[0];
    uint8_t length = nlri[1];
    
    // Check route type
    if (route_type < 1 || route_type > 5) {
        return false;
    }
    
    // Check length matches
    if (nlri_len < (size_t)(length + 2)) {
        return false;
    }
    
    return true;
}

/* ============================================================
 * Type 2 Routes - MAC/IP Advertisement
 * ============================================================ */

/**
 * Encode Type 2 route to EVPN NLRI format
 * 
 * RFC 7432 Section 7.2 format
 */
int evpn_encode_type2_route(const evpn_mac_ip_route_t *route,
                            uint8_t *buf, size_t buf_size, size_t *len) {
    if (!route || !buf || !len || buf_size < EVPN_NLRI_MAX_SIZE) {
        return -1;
    }
    
    uint8_t *p = buf;
    
    // Route Type (1 byte)
    *p++ = EVPN_ROUTE_TYPE_MAC_IP_ADVERTISEMENT;
    
    // Length (will be filled at end)
    uint8_t *len_ptr = p++;
    uint8_t *start = p;
    
    // Route Distinguisher (8 bytes)
    evpn_encode_rd(&route->rd, p);
    p += 8;
    
    // Ethernet Segment Identifier (10 bytes)
    evpn_encode_esi(&route->esi, p);
    p += 10;
    
    // Ethernet Tag ID (4 bytes)
    *(uint32_t *)p = htonl(route->ethernet_tag);
    p += 4;
    
    // MAC Address Length (1 byte) - always 48 for Ethernet
    *p++ = 48;
    
    // MAC Address (6 bytes)
    memcpy(p, route->mac, 6);
    p += 6;
    
    // IP Address Length (1 byte)
    *p++ = route->ip_len;
    
    // IP Address (variable)
    if (route->ip_len == 32) {
        *(uint32_t *)p = route->ip;
        p += 4;
    } else if (route->ip_len == 128) {
        // IPv6 support (placeholder)
        memset(p, 0, 16);
        p += 16;
    }
    // ip_len == 0 means MAC-only, no IP bytes
    
    // MPLS Label 1 (3 bytes) - VNI for VXLAN
    evpn_encode_label(route->label1, p);
    p += 3;
    
    // MPLS Label 2 (optional, 3 bytes)
    if (route->label2 != 0) {
        evpn_encode_label(route->label2, p);
        p += 3;
    }
    
    // Fill in length
    *len_ptr = p - start;
    *len = p - buf;
    
    return 0;
}

/**
 * Decode Type 2 route from EVPN NLRI
 */
int evpn_decode_type2_route(const uint8_t *nlri, size_t nlri_len,
                            evpn_mac_ip_route_t *route) {
    if (!nlri || !route || nlri_len < EVPN_TYPE2_MIN_LENGTH) {
        return -1;
    }
    
    const uint8_t *p = nlri;
    memset(route, 0, sizeof(*route));
    
    // Skip route type
    p++;
    
    // Length
    uint8_t length = *p++;
    
    // Route Distinguisher (8 bytes)
    evpn_decode_rd(p, &route->rd);
    p += 8;
    
    // ESI (10 bytes)
    evpn_decode_esi(p, &route->esi);
    p += 10;
    
    // Ethernet Tag (4 bytes)
    route->ethernet_tag = ntohl(*(uint32_t *)p);
    p += 4;
    
    // MAC Address Length (1 byte)
    route->mac_len = *p++;
    if (route->mac_len != 48) {
        fprintf(stderr, "Invalid MAC length: %u\n", route->mac_len);
        return -1;
    }
    
    // MAC Address (6 bytes)
    memcpy(route->mac, p, 6);
    p += 6;
    
    // IP Address Length (1 byte)
    route->ip_len = *p++;
    
    // IP Address (variable)
    if (route->ip_len == 32) {
        route->ip = *(uint32_t *)p;
        p += 4;
    } else if (route->ip_len == 128) {
        // IPv6 (skip for now)
        p += 16;
    }
    
    // MPLS Label 1 (3 bytes)
    route->label1 = evpn_decode_label(p);
    p += 3;
    
    // MPLS Label 2 (optional, 3 bytes)
    if (p < nlri + length + 2) {
        route->label2 = evpn_decode_label(p);
    }
    
    return 0;
}

/**
 * Advertise local MAC/IP to BGP peers
 */
int evpn_advertise_mac_ip(evpn_ctx_t *ctx, const uint8_t *mac, 
                          uint32_t ip, uint32_t __attribute__((unused)) vni) {
    if (!ctx || !mac) {
        return -1;
    }
    
    // Find MAC-VRF for this VNI
    evpn_mac_vrf_t *vrf = NULL;
    for (int i = 0; i < ctx->mac_vrf_count; i++) {
        if (ctx->mac_vrfs[i] && ctx->mac_vrfs[i]->vni == vni) {
            vrf = ctx->mac_vrfs[i];
            break;
        }
    }
    
    if (!vrf) {
        fprintf(stderr, "EVPN: No MAC-VRF found for VNI %u\n", vni);
        return -1;
    }
    
    // Build Type 2 route
    evpn_mac_ip_route_t route;
    memset(&route, 0, sizeof(route));
    
    memcpy(&route.rd, &vrf->rd, sizeof(evpn_rd_t));
    
    // Zero ESI for single-homing (add multi-homing)
    memset(&route.esi, 0, sizeof(evpn_esi_t));
    
    route.ethernet_tag = 0;  // Single broadcast domain
    route.mac_len = 48;
    memcpy(route.mac, mac, 6);
    
    if (ip != 0) {
        route.ip_len = 32;
        route.ip = ip;
    } else {
        route.ip_len = 0;
    }
    
    route.label1 = vni;  // VNI encoded as MPLS label
    route.label2 = 0;
    
    // Encode NLRI
    uint8_t nlri[EVPN_NLRI_MAX_SIZE];
    size_t nlri_len;
    
    if (evpn_encode_type2_route(&route, nlri, sizeof(nlri), &nlri_len) != 0) {
        fprintf(stderr, "EVPN: Failed to encode Type 2 route\n");
        return -1;
    }
    
    // Build and send BGP UPDATE (will be implemented in evpn_update.c)
    // For now, just add to RIB and log
    evpn_rib_entry_t rib_entry;
    memset(&rib_entry, 0, sizeof(rib_entry));
    rib_entry.type = EVPN_ROUTE_TYPE_MAC_IP_ADVERTISEMENT;
    memcpy(&rib_entry.route.mac_ip, &route, sizeof(route));
    rib_entry.next_hop = ctx->router_id;
    rib_entry.local = true;
    
    evpn_rib_add(ctx, &rib_entry);
    
    ctx->routes_advertised++;
    
    printf("EVPN: Advertised MAC %02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (ip != 0) {
        struct in_addr addr;
        addr.s_addr = ip;
        printf(" / %s", inet_ntoa(addr));
    }
    printf(" (VNI %u)\n", vni);
    
    return 0;
}

/**
 * Withdraw MAC/IP route
 */
int evpn_withdraw_mac_ip(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t __attribute__((unused)) vni) {
    if (!ctx || !mac) {
        return -1;
    }
    
    // Remove from RIB
    evpn_rib_remove(ctx, EVPN_ROUTE_TYPE_MAC_IP_ADVERTISEMENT, mac, vni);
    
    ctx->routes_withdrawn++;
    
    printf("EVPN: Withdrew MAC %02x:%02x:%02x:%02x:%02x:%02x (VNI %u)\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], vni);
    
    return 0;
}

/**
 * Process received Type 2 route
 */
int evpn_process_mac_ip_route(evpn_ctx_t *ctx, 
                              const evpn_mac_ip_route_t *route,
                              uint32_t next_hop,
                              bool withdraw) {
    if (!ctx || !route) {
        return -1;
    }
    
    uint32_t vni = route->label1;
    
    if (withdraw) {
        // Remove from VXLAN table
        evpn_vxlan_remove_remote_mac(ctx, route->mac, vni);
        
        // Remove from RIB
        evpn_rib_remove(ctx, EVPN_ROUTE_TYPE_MAC_IP_ADVERTISEMENT,
                       route->mac, vni);
        
        printf("EVPN: Withdrew remote MAC %02x:%02x:%02x:%02x:%02x:%02x (VNI %u)\n",
               route->mac[0], route->mac[1], route->mac[2],
               route->mac[3], route->mac[4], route->mac[5], vni);
    } else {
        // Install in VXLAN table (THE KEY INTEGRATION!)
        evpn_vxlan_install_remote_mac(ctx, route->mac, next_hop, vni);
        
        // Add to RIB
        evpn_rib_entry_t rib_entry;
        memset(&rib_entry, 0, sizeof(rib_entry));
        rib_entry.type = EVPN_ROUTE_TYPE_MAC_IP_ADVERTISEMENT;
        memcpy(&rib_entry.route.mac_ip, route, sizeof(*route));
        rib_entry.next_hop = next_hop;
        rib_entry.local = false;
        
        evpn_rib_add(ctx, &rib_entry);
        
        ctx->routes_received++;
        ctx->mac_learned_bgp++;
        
        struct in_addr addr;
        addr.s_addr = next_hop;
        printf("EVPN: Learned MAC %02x:%02x:%02x:%02x:%02x:%02x -> %s (VNI %u)\n",
               route->mac[0], route->mac[1], route->mac[2],
               route->mac[3], route->mac[4], route->mac[5],
               inet_ntoa(addr), vni);
    }
    
    return 0;
}

/* ============================================================
 * Type 3 Routes - Inclusive Multicast
 * ============================================================ */

/**
 * Encode Type 3 route to EVPN NLRI format
 */
int evpn_encode_type3_route(const evpn_inclusive_mcast_route_t *route,
                            uint8_t *buf, size_t buf_size, size_t *len) {
    if (!route || !buf || !len || buf_size < EVPN_NLRI_MAX_SIZE) {
        return -1;
    }
    
    uint8_t *p = buf;
    
    // Route Type (1 byte)
    *p++ = EVPN_ROUTE_TYPE_INCLUSIVE_MULTICAST;
    
    // Length (1 byte) - fixed for Type 3
    *p++ = EVPN_TYPE3_LENGTH;
    
    // Route Distinguisher (8 bytes)
    evpn_encode_rd(&route->rd, p);
    p += 8;
    
    // Ethernet Tag ID (4 bytes)
    *(uint32_t *)p = htonl(route->ethernet_tag);
    p += 4;
    
    // IP Address Length (1 byte) - always 32 for IPv4
    *p++ = 32;
    
    // Originating Router IP Address (4 bytes)
    *(uint32_t *)p = route->originating_router_ip;
    p += 4;
    
    *len = p - buf;
    
    return 0;
}

/**
 * Decode Type 3 route from EVPN NLRI
 */
int evpn_decode_type3_route(const uint8_t *nlri, size_t nlri_len,
                            evpn_inclusive_mcast_route_t *route) {
    if (!nlri || !route || nlri_len < EVPN_TYPE3_LENGTH + 2) {
        return -1;
    }
    
    const uint8_t *p = nlri;
    memset(route, 0, sizeof(*route));
    
    // Skip route type and length
    p += 2;
    
    // Route Distinguisher (8 bytes)
    evpn_decode_rd(p, &route->rd);
    p += 8;
    
    // Ethernet Tag (4 bytes)
    route->ethernet_tag = ntohl(*(uint32_t *)p);
    p += 4;
    
    // IP Address Length (1 byte)
    route->ip_len = *p++;
    if (route->ip_len != 32) {
        fprintf(stderr, "Invalid IP length in Type 3 route: %u\n", route->ip_len);
        return -1;
    }
    
    // Originating Router IP (4 bytes)
    route->originating_router_ip = *(uint32_t *)p;
    
    return 0;
}

/**
 * Advertise Inclusive Multicast route
 */
int evpn_advertise_inclusive_mcast(evpn_ctx_t *ctx, uint32_t __attribute__((unused)) vni) {
    if (!ctx) {
        return -1;
    }
    
    // Find MAC-VRF for this VNI
    evpn_mac_vrf_t *vrf = NULL;
    for (int i = 0; i < ctx->mac_vrf_count; i++) {
        if (ctx->mac_vrfs[i] && ctx->mac_vrfs[i]->vni == vni) {
            vrf = ctx->mac_vrfs[i];
            break;
        }
    }
    
    if (!vrf) {
        fprintf(stderr, "EVPN: No MAC-VRF found for VNI %u\n", vni);
        return -1;
    }
    
    // Build Type 3 route
    evpn_inclusive_mcast_route_t route;
    memset(&route, 0, sizeof(route));
    
    memcpy(&route.rd, &vrf->rd, sizeof(evpn_rd_t));
    route.ethernet_tag = 0;
    route.ip_len = 32;
    route.originating_router_ip = ctx->router_id;
    route.tunnel_type = 8;  // VXLAN
    route.vni = vni;
    
    // Encode NLRI
    uint8_t nlri[EVPN_NLRI_MAX_SIZE];
    size_t nlri_len;
    
    if (evpn_encode_type3_route(&route, nlri, sizeof(nlri), &nlri_len) != 0) {
        fprintf(stderr, "EVPN: Failed to encode Type 3 route\n");
        return -1;
    }
    
    // Add to RIB
    evpn_rib_entry_t rib_entry;
    memset(&rib_entry, 0, sizeof(rib_entry));
    rib_entry.type = EVPN_ROUTE_TYPE_INCLUSIVE_MULTICAST;
    memcpy(&rib_entry.route.inclusive_mcast, &route, sizeof(route));
    rib_entry.next_hop = ctx->router_id;
    rib_entry.local = true;
    
    evpn_rib_add(ctx, &rib_entry);
    
    ctx->routes_advertised++;
    
    struct in_addr addr;
    addr.s_addr = ctx->router_id;
    printf("EVPN: Advertised Inclusive Multicast from %s (VNI %u)\n",
           inet_ntoa(addr), vni);
    
    return 0;
}

/**
 * Withdraw Inclusive Multicast route
 */
int evpn_withdraw_inclusive_mcast(evpn_ctx_t *ctx, uint32_t __attribute__((unused)) vni) {
    if (!ctx) {
        return -1;
    }
    
    evpn_rib_remove(ctx, EVPN_ROUTE_TYPE_INCLUSIVE_MULTICAST, NULL, vni);
    
    ctx->routes_withdrawn++;
    
    printf("EVPN: Withdrew Inclusive Multicast (VNI %u)\n", vni);
    
    return 0;
}

/**
 * Process received Type 3 route
 */
int evpn_process_inclusive_mcast_route(evpn_ctx_t *ctx,
                                       const evpn_inclusive_mcast_route_t *route,
                                       uint32_t next_hop,
                                       bool withdraw) {
    if (!ctx || !route) {
        return -1;
    }
    
    uint32_t vni = route->vni;
    
    if (withdraw) {
        // Remove from RIB
        evpn_rib_remove(ctx, EVPN_ROUTE_TYPE_INCLUSIVE_MULTICAST, NULL, vni);
        
        struct in_addr addr;
        addr.s_addr = next_hop;
        printf("EVPN: Withdrew Inclusive Multicast from %s (VNI %u)\n",
               inet_ntoa(addr), vni);
    } else {
        // Add to RIB
        evpn_rib_entry_t rib_entry;
        memset(&rib_entry, 0, sizeof(rib_entry));
        rib_entry.type = EVPN_ROUTE_TYPE_INCLUSIVE_MULTICAST;
        memcpy(&rib_entry.route.inclusive_mcast, route, sizeof(*route));
        rib_entry.next_hop = next_hop;
        rib_entry.local = false;
        
        evpn_rib_add(ctx, &rib_entry);
        
        ctx->routes_received++;
        
        struct in_addr addr;
        addr.s_addr = next_hop;
        printf("EVPN: Discovered VTEP %s (VNI %u)\n", inet_ntoa(addr), vni);
    }
    
    return 0;
}

/* ============================================================
 * Type 1 Routes - Ethernet Auto-Discovery
 * ============================================================ */

/**
 * Encode Type 1 route (Ethernet Auto-Discovery)
 */
int evpn_encode_type1_route(const evpn_ethernet_ad_route_t *route,
                            uint8_t *buf, size_t buf_size, size_t *len) {
    if (!route || !buf || !len || buf_size < EVPN_NLRI_MAX_SIZE) {
        return -1;
    }
    
    uint8_t *p = buf;
    
    // Route Type
    *p++ = EVPN_ROUTE_TYPE_ETHERNET_AD;
    
    // Length (will be filled at end)
    uint8_t *len_ptr = p++;
    uint8_t *start = p;
    
    // Route Distinguisher (8 bytes)
    evpn_encode_rd(&route->rd, p);
    p += 8;
    
    // Ethernet Segment Identifier (10 bytes)
    evpn_encode_esi(&route->esi, p);
    p += 10;
    
    // Ethernet Tag ID (4 bytes)
    *(uint32_t *)p = htonl(route->ethernet_tag);
    p += 4;
    
    // MPLS Label (3 bytes)
    evpn_encode_label(route->label, p);
    p += 3;
    
    // Fill in length
    *len_ptr = p - start;
    *len = p - buf;
    
    return 0;
}

/**
 * Decode Type 1 route
 */
int evpn_decode_type1_route(const uint8_t *nlri, size_t nlri_len,
                            evpn_ethernet_ad_route_t *route) {
    if (!nlri || !route || nlri_len < EVPN_TYPE1_MIN_LENGTH + 2) {
        return -1;
    }
    
    const uint8_t *p = nlri;
    memset(route, 0, sizeof(*route));
    
    // Skip route type and length
    p += 2;
    
    // Route Distinguisher
    evpn_decode_rd(p, &route->rd);
    p += 8;
    
    // ESI
    evpn_decode_esi(p, &route->esi);
    p += 10;
    
    // Ethernet Tag
    route->ethernet_tag = ntohl(*(uint32_t *)p);
    p += 4;
    
    // MPLS Label
    route->label = evpn_decode_label(p);
    
    return 0;
}

/**
 * Advertise Ethernet Auto-Discovery route
 */
int evpn_advertise_ethernet_ad(evpn_ctx_t *ctx, const evpn_esi_t *esi,
                                uint32_t ethernet_tag, uint32_t __attribute__((unused)) vni) {
    if (!ctx || !esi) {
        return -1;
    }
    
    // Find MAC-VRF for this VNI
    evpn_mac_vrf_t *vrf = NULL;
    for (int i = 0; i < ctx->mac_vrf_count; i++) {
        if (ctx->mac_vrfs[i] && ctx->mac_vrfs[i]->vni == vni) {
            vrf = ctx->mac_vrfs[i];
            break;
        }
    }
    
    if (!vrf) {
        fprintf(stderr, "EVPN: No MAC-VRF for VNI %u\n", vni);
        return -1;
    }
    
    // Build Type 1 route
    evpn_ethernet_ad_route_t route;
    memset(&route, 0, sizeof(route));
    
    memcpy(&route.rd, &vrf->rd, sizeof(evpn_rd_t));
    memcpy(&route.esi, esi, sizeof(evpn_esi_t));
    route.ethernet_tag = ethernet_tag;
    route.label = vni;
    
    // Encode NLRI
    uint8_t nlri[EVPN_NLRI_MAX_SIZE];
    size_t nlri_len;
    
    if (evpn_encode_type1_route(&route, nlri, sizeof(nlri), &nlri_len) != 0) {
        fprintf(stderr, "EVPN: Failed to encode Type 1 route\n");
        return -1;
    }
    
    // Add to RIB
    evpn_rib_entry_t rib_entry;
    memset(&rib_entry, 0, sizeof(rib_entry));
    rib_entry.type = EVPN_ROUTE_TYPE_ETHERNET_AD;
    memcpy(&rib_entry.route.ad, &route, sizeof(route));
    rib_entry.next_hop = ctx->router_id;
    rib_entry.local = true;
    
    evpn_rib_add(ctx, &rib_entry);
    
    ctx->routes_advertised++;
    
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    printf("EVPN: Advertised Ethernet AD for ES %s (VNI %u)\n", esi_str, vni);
    
    return 0;
}

/**
 * Process received Type 1 route
 */
int evpn_process_ethernet_ad_route(evpn_ctx_t *ctx,
                                    const evpn_ethernet_ad_route_t *route,
                                    uint32_t next_hop,
                                    bool withdraw) {
    if (!ctx || !route) {
        return -1;
    }
    
    if (withdraw) {
        char esi_str[64];
        evpn_esi_to_string(&route->esi, esi_str, sizeof(esi_str));
        printf("EVPN: Withdrew Ethernet AD for ES %s\n", esi_str);
    } else {
        // Add PE to Ethernet Segment
        evpn_es_add_pe(ctx, &route->esi, next_hop);
        
        // Add to RIB
        evpn_rib_entry_t rib_entry;
        memset(&rib_entry, 0, sizeof(rib_entry));
        rib_entry.type = EVPN_ROUTE_TYPE_ETHERNET_AD;
        memcpy(&rib_entry.route.ad, route, sizeof(*route));
        rib_entry.next_hop = next_hop;
        rib_entry.local = false;
        
        evpn_rib_add(ctx, &rib_entry);
        
        ctx->routes_received++;
    }
    
    return 0;
}

/* ============================================================
 * Type 4 Routes - Ethernet Segment
 * ============================================================ */

/**
 * Encode Type 4 route (Ethernet Segment)
 */
int evpn_encode_type4_route(const evpn_ethernet_segment_route_t *route,
                            uint8_t *buf, size_t buf_size, size_t *len) {
    if (!route || !buf || !len || buf_size < EVPN_NLRI_MAX_SIZE) {
        return -1;
    }
    
    uint8_t *p = buf;
    
    // Route Type
    *p++ = EVPN_ROUTE_TYPE_ETHERNET_SEGMENT;
    
    // Length (fixed for Type 4)
    *p++ = EVPN_TYPE4_LENGTH;
    
    // Route Distinguisher (8 bytes)
    evpn_encode_rd(&route->rd, p);
    p += 8;
    
    // Ethernet Segment Identifier (10 bytes)
    evpn_encode_esi(&route->esi, p);
    p += 10;
    
    // Originating Router IP (4 bytes)
    *(uint32_t *)p = route->originating_router_ip;
    p += 4;
    
    *len = p - buf;
    
    return 0;
}

/**
 * Decode Type 4 route
 */
int evpn_decode_type4_route(const uint8_t *nlri, size_t nlri_len,
                            evpn_ethernet_segment_route_t *route) {
    if (!nlri || !route || nlri_len < EVPN_TYPE4_LENGTH + 2) {
        return -1;
    }
    
    const uint8_t *p = nlri;
    memset(route, 0, sizeof(*route));
    
    // Skip route type and length
    p += 2;
    
    // Route Distinguisher
    evpn_decode_rd(p, &route->rd);
    p += 8;
    
    // ESI
    evpn_decode_esi(p, &route->esi);
    p += 10;
    
    // Originating Router IP
    route->originating_router_ip = *(uint32_t *)p;
    
    return 0;
}

/**
 * Advertise Ethernet Segment route
 */
int evpn_advertise_ethernet_segment(evpn_ctx_t *ctx, const evpn_esi_t *esi) {
    if (!ctx || !esi) {
        return -1;
    }
    
    // Build Type 4 route
    evpn_ethernet_segment_route_t route;
    memset(&route, 0, sizeof(route));
    
    // Use first MAC-VRF's RD (or create default RD)
    if (ctx->mac_vrf_count > 0) {
        memcpy(&route.rd, &ctx->mac_vrfs[0]->rd, sizeof(evpn_rd_t));
    } else {
        route.rd.type = 0;
        route.rd.value.asn_based.asn = ctx->local_asn;
        route.rd.value.asn_based.number = 0;
    }
    
    memcpy(&route.esi, esi, sizeof(evpn_esi_t));
    route.originating_router_ip = ctx->router_id;
    
    // Encode NLRI
    uint8_t nlri[EVPN_NLRI_MAX_SIZE];
    size_t nlri_len;
    
    if (evpn_encode_type4_route(&route, nlri, sizeof(nlri), &nlri_len) != 0) {
        fprintf(stderr, "EVPN: Failed to encode Type 4 route\n");
        return -1;
    }
    
    // Add to RIB
    evpn_rib_entry_t rib_entry;
    memset(&rib_entry, 0, sizeof(rib_entry));
    rib_entry.type = EVPN_ROUTE_TYPE_ETHERNET_SEGMENT;
    memcpy(&rib_entry.route.es, &route, sizeof(route));
    rib_entry.next_hop = ctx->router_id;
    rib_entry.local = true;
    
    evpn_rib_add(ctx, &rib_entry);
    
    ctx->routes_advertised++;
    
    char esi_str[64];
    evpn_esi_to_string(esi, esi_str, sizeof(esi_str));
    printf("EVPN: Advertised Ethernet Segment route for %s\n", esi_str);
    
    return 0;
}

/**
 * Process received Type 4 route
 */
int evpn_process_ethernet_segment_route(evpn_ctx_t *ctx,
                                         const evpn_ethernet_segment_route_t *route,
                                         uint32_t next_hop,
                                         bool withdraw) {
    if (!ctx || !route) {
        return -1;
    }
    
    if (withdraw) {
        // Remove PE from ES
        evpn_es_remove_pe(ctx, &route->esi, next_hop);
    } else {
        // Add PE to ES
        evpn_es_add_pe(ctx, &route->esi, next_hop);
        
        // Add to RIB
        evpn_rib_entry_t rib_entry;
        memset(&rib_entry, 0, sizeof(rib_entry));
        rib_entry.type = EVPN_ROUTE_TYPE_ETHERNET_SEGMENT;
        memcpy(&rib_entry.route.es, route, sizeof(*route));
        rib_entry.next_hop = next_hop;
        rib_entry.local = false;
        
        evpn_rib_add(ctx, &rib_entry);
        
        ctx->routes_received++;
        
        char esi_str[64];
        evpn_esi_to_string(&route->esi, esi_str, sizeof(esi_str));
        struct in_addr addr;
        addr.s_addr = next_hop;
        printf("EVPN: Discovered PE %s on ES %s\n", inet_ntoa(addr), esi_str);
    }
    
    return 0;
}

/* ============================================================
 * Generic NLRI Processing
 * ============================================================ */

/**
 * Process EVPN NLRI (dispatches to appropriate type handler)
 */
int evpn_process_nlri(evpn_ctx_t *ctx, const uint8_t *nlri, size_t nlri_len,
                     uint32_t next_hop, bool withdraw) {
    if (!ctx || !nlri || nlri_len < 2) {
        return -1;
    }
    
    // Validate NLRI
    if (!evpn_validate_nlri(nlri, nlri_len)) {
        fprintf(stderr, "EVPN: Invalid NLRI\n");
        return -1;
    }
    
    uint8_t route_type = nlri[0];
    
    switch (route_type) {
        case EVPN_ROUTE_TYPE_ETHERNET_AD: {
            evpn_ethernet_ad_route_t route;
            if (evpn_decode_type1_route(nlri, nlri_len, &route) == 0) {
                return evpn_process_ethernet_ad_route(ctx, &route, next_hop, withdraw);
            }
            break;
        }
        
        case EVPN_ROUTE_TYPE_MAC_IP_ADVERTISEMENT: {
            evpn_mac_ip_route_t route;
            if (evpn_decode_type2_route(nlri, nlri_len, &route) == 0) {
                return evpn_process_mac_ip_route(ctx, &route, next_hop, withdraw);
            }
            break;
        }
        
        case EVPN_ROUTE_TYPE_INCLUSIVE_MULTICAST: {
            evpn_inclusive_mcast_route_t route;
            if (evpn_decode_type3_route(nlri, nlri_len, &route) == 0) {
                route.vni = route.ethernet_tag;  // VNI passed in ethernet_tag for Type 3
                return evpn_process_inclusive_mcast_route(ctx, &route, next_hop, withdraw);
            }
            break;
        }
        
        case EVPN_ROUTE_TYPE_ETHERNET_SEGMENT: {
            evpn_ethernet_segment_route_t route;
            if (evpn_decode_type4_route(nlri, nlri_len, &route) == 0) {
                return evpn_process_ethernet_segment_route(ctx, &route, next_hop, withdraw);
            }
            break;
        }
        
        case EVPN_ROUTE_TYPE_IP_PREFIX:
            printf("EVPN: Route type 5 (IP Prefix) not implemented yet \n");
            return 0;
        
        default:
            fprintf(stderr, "EVPN: Unknown route type %u\n", route_type);
            return -1;
    }
    
    return -1;
}

/* ============================================================
 * Type 5 Routes - IP Prefix Route (Feature 1)
 * RFC 9136 - IP Prefix Advertisement in EVPN
 * ============================================================ */

/**
 * Encode Type 5 route to BGP NLRI format
 */
int evpn_encode_type5_route(const evpn_ip_prefix_route_t *route,
                            uint8_t *buf, size_t buf_size, size_t *len) {
    if (!route || !buf || buf_size < EVPN_TYPE5_MIN_LENGTH || !len) {
        return -1;
    }
    
    uint8_t *ptr = buf;
    
    // Route Type
    *ptr++ = EVPN_ROUTE_TYPE_IP_PREFIX;
    
    // Length (will be filled at end)
    uint8_t *len_ptr = ptr++;
    
    // RD (8 bytes)
    memcpy(ptr, &route->rd, sizeof(evpn_rd_t));
    ptr += sizeof(evpn_rd_t);
    
    // ESI (10 bytes)
    memcpy(ptr, &route->esi, EVPN_ESI_LENGTH);
    ptr += EVPN_ESI_LENGTH;
    
    // Ethernet Tag ID (4 bytes)
    uint32_t eth_tag = htonl(route->ethernet_tag);
    memcpy(ptr, &eth_tag, 4);
    ptr += 4;
    
    // IP Prefix Length (1 byte)
    *ptr++ = route->ip_prefix_len;
    
    // IP Prefix (4 bytes)
    memcpy(ptr, &route->ip_prefix, 4);
    ptr += 4;
    
    // Gateway IP (4 bytes)
    memcpy(ptr, &route->gw_ip, 4);
    ptr += 4;
    
    // Label/VNI (3 bytes)
    uint32_t label_be = htonl(route->label << 12);
    memcpy(ptr, &label_be, 3);
    ptr += 3;
    
    // Fill in length
    *len_ptr = (uint8_t)(ptr - buf - 2);
    *len = ptr - buf;
    
    return 0;
}

/**
 * Decode Type 5 route from BGP NLRI
 */
int evpn_decode_type5_route(const uint8_t *nlri, size_t nlri_len,
                            evpn_ip_prefix_route_t *route) {
    if (!nlri || nlri_len < EVPN_TYPE5_MIN_LENGTH || !route) {
        return -1;
    }
    
    const uint8_t *ptr = nlri;
    
    // Skip route type and length
    ptr += 2;
    
    // RD
    memcpy(&route->rd, ptr, sizeof(evpn_rd_t));
    ptr += sizeof(evpn_rd_t);
    
    // ESI
    memcpy(&route->esi, ptr, EVPN_ESI_LENGTH);
    ptr += EVPN_ESI_LENGTH;
    
    // Ethernet Tag
    uint32_t eth_tag;
    memcpy(&eth_tag, ptr, 4);
    route->ethernet_tag = ntohl(eth_tag);
    ptr += 4;
    
    // IP Prefix Length
    route->ip_prefix_len = *ptr++;
    
    // IP Prefix
    memcpy(&route->ip_prefix, ptr, 4);
    ptr += 4;
    
    // Gateway IP
    memcpy(&route->gw_ip, ptr, 4);
    ptr += 4;
    
    // Label
    uint32_t label_be = 0;
    memcpy(&label_be, ptr, 3);
    route->label = ntohl(label_be << 8) >> 12;
    
    return 0;
}

/**
 * Advertise IP prefix via Type 5 route
 */
int evpn_advertise_ip_prefix(evpn_ctx_t *ctx, uint32_t ip_prefix,
                             uint8_t prefix_len, uint32_t gw_ip, uint32_t __attribute__((unused)) vni) {
    if (!ctx) {
        return -1;
    }
    
    // Build Type 5 route
    evpn_ip_prefix_route_t route;
    memset(&route, 0, sizeof(route));
    
    // Set RD (simplified - using router ID)
    route.rd.type = 0;
    route.rd.value.asn_based.asn = ctx->local_asn;
    route.rd.value.asn_based.number = vni;
    
    // ESI = 0 (not multi-homed for now)
    memset(&route.esi, 0, sizeof(route.esi));
    
    route.ethernet_tag = 0;
    route.ip_prefix = ip_prefix;
    route.ip_prefix_len = prefix_len;
    route.gw_ip = gw_ip;
    route.label = vni;
    
    // Encode to NLRI
    uint8_t nlri[EVPN_NLRI_MAX_SIZE];
    size_t nlri_len;
    
    if (evpn_encode_type5_route(&route, nlri, sizeof(nlri), &nlri_len) != 0) {
        fprintf(stderr, "Failed to encode Type 5 route\n");
        return -1;
    }
    
    char ip_str[32];
    struct in_addr addr;
    addr.s_addr = ip_prefix;
    snprintf(ip_str, sizeof(ip_str), "%s/%d", inet_ntoa(addr), prefix_len);
    
    printf("EVPN: Advertising IP prefix %s (VNI %u)\n", ip_str, vni);
    printf("      Gateway: ");
    addr.s_addr = gw_ip;
    printf("%s\n", inet_ntoa(addr));
    
    // In real implementation: send via BGP
    // evpn_send_update_to_peers(ctx, nlri, nlri_len, gw_ip);
    
    return 0;
}

/**
 * Withdraw IP prefix
 */
int evpn_withdraw_ip_prefix(evpn_ctx_t *ctx, uint32_t ip_prefix,
                            uint8_t prefix_len, uint32_t __attribute__((unused)) vni) {
    if (!ctx) {
        return -1;
    }
    
    char ip_str[32];
    struct in_addr addr;
    addr.s_addr = ip_prefix;
    snprintf(ip_str, sizeof(ip_str), "%s/%d", inet_ntoa(addr), prefix_len);
    
    printf("EVPN: Withdrawing IP prefix %s (VNI %u)\n", ip_str, vni);
    
    return 0;
}

/**
 * Process received Type 5 route
 */
int evpn_process_ip_prefix_route(evpn_ctx_t *ctx,
                                 const evpn_ip_prefix_route_t *route,
                                 uint32_t next_hop, bool withdraw) {
    if (!ctx || !route) {
        return -1;
    }
    
    char ip_str[32];
    struct in_addr addr;
    addr.s_addr = route->ip_prefix;
    snprintf(ip_str, sizeof(ip_str), "%s/%d", inet_ntoa(addr), route->ip_prefix_len);
    
    if (withdraw) {
        printf("EVPN: Received Type 5 withdrawal: %s\n", ip_str);
        return evpn_remove_ip_route(ctx, route->ip_prefix, route->ip_prefix_len, route->label);
    } else {
        printf("EVPN: Received Type 5 route: %s via ", ip_str);
        addr.s_addr = next_hop;
        printf("%s (VNI %u)\n", inet_ntoa(addr), route->label);
        return evpn_install_ip_route(ctx, route->ip_prefix, route->ip_prefix_len, next_hop, route->label);
    }
}

/**
 * Install IP prefix route in routing table
 */
int evpn_install_ip_route(evpn_ctx_t *ctx, uint32_t ip_prefix,
                          uint8_t prefix_len, uint32_t next_hop, uint32_t __attribute__((unused)) vni) {
    if (!ctx) {
        return -1;
    }
    
    // In real implementation: install in kernel routing table
    // ip route add <prefix> via <next_hop> dev vxlan<vni>
    
    char ip_str[32];
    struct in_addr addr;
    addr.s_addr = ip_prefix;
    snprintf(ip_str, sizeof(ip_str), "%s/%d", inet_ntoa(addr), prefix_len);
    
    printf("      → Installing route: %s via ", ip_str);
    addr.s_addr = next_hop;
    printf("%s (VNI %u)\n", inet_ntoa(addr), vni);
    
    return 0;
}

/**
 * Remove IP prefix route from routing table
 */
int evpn_remove_ip_route(evpn_ctx_t *ctx, uint32_t ip_prefix,
                        uint8_t prefix_len, uint32_t __attribute__((unused)) vni) {
    if (!ctx) {
        return -1;
    }
    
    char ip_str[32];
    struct in_addr addr;
    addr.s_addr = ip_prefix;
    snprintf(ip_str, sizeof(ip_str), "%s/%d", inet_ntoa(addr), prefix_len);
    
    printf("      → Removing route: %s (VNI %u)\n", ip_str, vni);
    
    return 0;
}

/* ============================================================
 * MAC Mobility (Feature 2)
 * RFC 7432 Section 15 - MAC Mobility
 * ============================================================ */

// Simple MAC mobility table (in real implementation: hash table)
typedef struct {
    uint8_t mac[6];
    uint32_t vni;
    uint32_t vtep_ip;
    uint32_t sequence;
    time_t last_move;
    int move_count;
} mac_mobility_entry_t;

static mac_mobility_entry_t mac_mobility_table[256];
static int mac_mobility_count = 0;

/**
 * Find MAC mobility entry
 */
static mac_mobility_entry_t* find_mac_mobility(const uint8_t *mac, uint32_t __attribute__((unused)) vni) {
    for (int i = 0; i < mac_mobility_count; i++) {
        if (memcmp(mac_mobility_table[i].mac, mac, 6) == 0 &&
            mac_mobility_table[i].vni == vni) {
            return &mac_mobility_table[i];
        }
    }
    return NULL;
}

/**
 * Advertise MAC with mobility sequence number
 */
int evpn_advertise_mac_with_seq(evpn_ctx_t *ctx, const uint8_t *mac,
                                uint32_t ip, uint32_t vni, uint32_t seq) {
    if (!ctx || !mac) {
        return -1;
    }
    
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    printf("EVPN: Advertising MAC %s with sequence %u (VNI %u)\n",
           mac_str, seq, vni);
    
    // In real implementation: add MAC Mobility extended community to BGP UPDATE
    
    return 0;
}

/**
 * Detect MAC mobility
 */
bool evpn_detect_mac_move(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t vni,
                         uint32_t new_vtep, uint32_t *old_vtep) {
    if (!ctx || !mac) {
        return false;
    }
    
    mac_mobility_entry_t *entry = find_mac_mobility(mac, vni);
    
    if (!entry) {
        // First time seeing this MAC
        if (mac_mobility_count < 256) {
            entry = &mac_mobility_table[mac_mobility_count++];
            memcpy(entry->mac, mac, 6);
            entry->vni = vni;
            entry->vtep_ip = new_vtep;
            entry->sequence = 0;
            entry->last_move = time(NULL);
            entry->move_count = 0;
        }
        return false;
    }
    
    // Check if MAC moved
    if (entry->vtep_ip != new_vtep) {
        if (old_vtep) {
            *old_vtep = entry->vtep_ip;
        }
        return true;
    }
    
    return false;
}

/**
 * Handle MAC mobility event
 */
int evpn_handle_mac_move(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t vni,
                        uint32_t old_vtep, uint32_t new_vtep) {
    if (!ctx || !mac) {
        return -1;
    }
    
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║              MAC MOBILITY DETECTED                            ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("MAC: %s\n", mac_str);
    printf("VNI: %u\n", vni);
    
    struct in_addr addr;
    addr.s_addr = old_vtep;
    printf("Old VTEP: %s\n", inet_ntoa(addr));
    
    addr.s_addr = new_vtep;
    printf("New VTEP: %s\n", inet_ntoa(addr));
    
    // Update mobility entry
    mac_mobility_entry_t *entry = find_mac_mobility(mac, vni);
    if (entry) {
        entry->vtep_ip = new_vtep;
        entry->sequence++;
        entry->move_count++;
        entry->last_move = time(NULL);
        
        printf("Sequence: %u\n", entry->sequence);
        printf("Move count: %d\n", entry->move_count);
        
        // Check for excessive moves (possible loop)
        if (entry->move_count > 5) {
            printf("\n⚠ WARNING: Excessive MAC moves detected!\n");
            printf("  Possible loop or misconfiguration\n");
            printf("  Consider marking MAC as sticky\n");
        }
    }
    
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    // Re-advertise with new sequence number
    evpn_advertise_mac_with_seq(ctx, mac, 0, vni, entry ? entry->sequence : 0);
    
    return 0;
}

/**
 * Get MAC mobility sequence number
 */
uint32_t evpn_get_mac_sequence(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t __attribute__((unused)) vni) {
    if (!ctx || !mac) {
        return 0;
    }
    
    mac_mobility_entry_t *entry = find_mac_mobility(mac, vni);
    return entry ? entry->sequence : 0;
}

/**
 * Increment MAC mobility sequence
 */
uint32_t evpn_increment_mac_sequence(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t __attribute__((unused)) vni) {
    if (!ctx || !mac) {
        return 0;
    }
    
    mac_mobility_entry_t *entry = find_mac_mobility(mac, vni);
    if (entry) {
        entry->sequence++;
        return entry->sequence;
    }
    
    return 0;
}

/**
 * Check if MAC move should be allowed
 */
bool evpn_should_allow_mac_move(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t vni,
                               uint32_t old_seq, uint32_t new_seq) {
    if (!ctx || !mac) {
        return false;
    }
    
    // Accept if new sequence is higher (normal case)
    if (new_seq > old_seq) {
        return true;
    }
    
    // Reject if new sequence is lower (old UPDATE)
    if (new_seq < old_seq) {
        printf("      Rejecting MAC move: old sequence (%u < %u)\n", new_seq, old_seq);
        return false;
    }
    
    // Equal sequence - use tiebreaker (typically router ID)
    return true;
}

/* ============================================================
 * ARP Suppression (Feature 3)
 * RFC 7432 Section 10 - ARP and ND Extended Community
 * ============================================================ */

// ARP cache (in real implementation: hash table)
static evpn_arp_entry_t arp_cache[1024];
static int arp_cache_count = 0;
static uint64_t arp_requests_received = 0;
static uint64_t arp_requests_suppressed = 0;

/**
 * Enable ARP suppression for a VNI
 */
int evpn_enable_arp_suppression(evpn_ctx_t *ctx, uint32_t __attribute__((unused)) vni) {
    if (!ctx) {
        return -1;
    }
    
    printf("EVPN: Enabled ARP suppression for VNI %u\n", vni);
    printf("      VTEP will answer ARP requests locally\n");
    printf("      Reduces ARP flooding in overlay\n");
    
    return 0;
}

/**
 * Add entry to ARP cache
 */
int evpn_arp_cache_add(evpn_ctx_t *ctx, uint32_t ip, const uint8_t *mac, uint32_t __attribute__((unused)) vni) {
    if (!ctx || !mac) {
        return -1;
    }
    
    // Check if entry exists
    for (int i = 0; i < arp_cache_count; i++) {
        if (arp_cache[i].ip == ip && arp_cache[i].vni == vni) {
            // Update existing entry
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].timestamp = time(NULL);
            return 0;
        }
    }
    
    // Add new entry
    if (arp_cache_count < 1024) {
        arp_cache[arp_cache_count].ip = ip;
        memcpy(arp_cache[arp_cache_count].mac, mac, 6);
        arp_cache[arp_cache_count].vni = vni;
        arp_cache[arp_cache_count].timestamp = time(NULL);
        arp_cache_count++;
        
        struct in_addr addr;
        addr.s_addr = ip;
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        
        printf("      ARP cache: %s → %s (VNI %u)\n", inet_ntoa(addr), mac_str, vni);
    }
    
    return 0;
}

/**
 * Lookup IP in ARP cache
 */
int evpn_arp_cache_lookup(evpn_ctx_t *ctx, uint32_t ip, uint32_t vni,
                          uint8_t *mac_out) {
    if (!ctx || !mac_out) {
        return -1;
    }
    
    for (int i = 0; i < arp_cache_count; i++) {
        if (arp_cache[i].ip == ip && arp_cache[i].vni == vni) {
            memcpy(mac_out, arp_cache[i].mac, 6);
            return 0;
        }
    }
    
    return -1;  // Not found
}

/**
 * Handle ARP request (suppress if we can answer)
 */
bool evpn_handle_arp_request(evpn_ctx_t *ctx, uint32_t target_ip, uint32_t vni,
                            uint8_t *reply_mac) {
    if (!ctx) {
        return false;
    }
    
    arp_requests_received++;
    
    // Lookup in ARP cache
    if (evpn_arp_cache_lookup(ctx, target_ip, vni, reply_mac) == 0) {
        arp_requests_suppressed++;
        
        struct in_addr addr;
        addr.s_addr = target_ip;
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                 reply_mac[0], reply_mac[1], reply_mac[2], reply_mac[3], reply_mac[4], reply_mac[5]);
        
        printf("      ARP suppression: Who has %s? → %s (cached)\n",
               inet_ntoa(addr), mac_str);
        
        return true;  // Suppressed
    }
    
    return false;  // Not in cache, allow flooding
}

/**
 * Generate ARP reply locally
 */
int evpn_generate_arp_reply(evpn_ctx_t *ctx, uint32_t src_ip, const uint8_t *src_mac,
                           uint32_t target_ip, const uint8_t *target_mac,
                           uint8_t *reply, size_t *reply_len) {
    if (!ctx || !src_mac || !target_mac || !reply || !reply_len) {
        return -1;
    }
    
    // In real implementation: construct ARP reply packet
    // For now, just simulate
    
    *reply_len = 42;  // Typical ARP reply size
    
    printf("      Generated ARP reply for ");
    struct in_addr addr;
    addr.s_addr = target_ip;
    printf("%s\n", inet_ntoa(addr));
    
    return 0;
}

/**
 * Get ARP suppression statistics
 */
int evpn_get_arp_stats(evpn_ctx_t *ctx, uint32_t __attribute__((unused)) vni,
                      uint64_t *requests_received,
                      uint64_t *requests_suppressed,
                      uint64_t *cache_entries) {
    if (!ctx) {
        return -1;
    }
    
    if (requests_received) {
        *requests_received = arp_requests_received;
    }
    
    if (requests_suppressed) {
        *requests_suppressed = arp_requests_suppressed;
    }
    
    if (cache_entries) {
        *cache_entries = arp_cache_count;
    }
    
    return 0;
}

/* ============================================================
 * Route Policies (Feature 4)
 * Import/Export Filtering
 * ============================================================ */

static evpn_route_policy_t __attribute__((unused)) import_policies[16];
static int __attribute__((unused)) import_policy_count = 0;
static evpn_route_policy_t __attribute__((unused)) export_policies[16];
static int __attribute__((unused)) export_policy_count = 0;

/**
 * Create route policy
 */
int evpn_create_policy(evpn_ctx_t *ctx, const char *name,
                       evpn_policy_action_t action) {
    if (!ctx || !name) {
        return -1;
    }
    
    printf("EVPN: Created route policy '%s' (%s)\n",
           name, action == EVPN_POLICY_PERMIT ? "PERMIT" : "DENY");
    
    return 0;
}

/**
 * Apply import policy
 */
bool evpn_apply_import_policy(evpn_ctx_t __attribute__((unused)) *ctx, evpn_route_type_t __attribute__((unused)) type,
                              uint32_t __attribute__((unused)) vni) {
    // Default: permit all
    return true;
}

/**
 * Apply export policy
 */
bool evpn_apply_export_policy(evpn_ctx_t __attribute__((unused)) *ctx, evpn_route_type_t __attribute__((unused)) type,
                              uint32_t __attribute__((unused)) vni) {
    // Default: permit all
    return true;
}
