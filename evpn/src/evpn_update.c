
/*****************************************************************************
 * Project     : EVPN Protocol Implementation (RFC 8365)
 * Description :  BGP UPDATE Message Processing for EVPN
 *                
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : evpn_update.c  
 * Purpose     : 
 * 
 *                This module processes BGP UPDATE messages containing EVPN routes.
 *                It extracts MP_REACH_NLRI and MP_UNREACH_NLRI attributes and
 *                dispatches them to appropriate route handlers.
 * 
 *                 RFC 4760 - Multiprotocol Extensions for BGP-4
 *                 RFC 7432 - BGP MPLS-Based Ethernet VPN
 *                 RFC 8365 - EVPN Network Virtualization Overlay
 *****************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "../include/evpn.h"
#include "../include/evpn_bgp.h"
#include "../include/evpn_routes.h"

/* ============================================================
 * BGP UPDATE Message Processing
 * ============================================================ */

/**
 * Extract MP_REACH_NLRI attribute (AFI=25, SAFI=70 for EVPN)
 */
int bgp_extract_mp_reach_nlri(const uint8_t *attr_value, size_t attr_len,
                              uint16_t *afi, uint8_t *safi,
                              uint32_t *next_hop,
                              uint8_t **nlri, size_t *nlri_len) {
    if (!attr_value || attr_len < 5 || !afi || !safi || !next_hop || !nlri || !nlri_len) {
        return -1;
    }
    
    const uint8_t *p = attr_value;
    
    // AFI (2 bytes)
    *afi = ntohs(*(uint16_t *)p);
    p += 2;
    
    // SAFI (1 byte)
    *safi = *p++;
    
    // Next Hop Length (1 byte)
    uint8_t nh_len = *p++;
    
    if (nh_len != 4 && nh_len != 16) {
        fprintf(stderr, "BGP: Invalid next hop length %u in MP_REACH_NLRI\n", nh_len);
        return -1;
    }
    
    // Next Hop (4 or 16 bytes)
    if (nh_len == 4) {
        *next_hop = *(uint32_t *)p;
    } else {
        // IPv6 - use first 4 bytes for now
        *next_hop = *(uint32_t *)p;
    }
    p += nh_len;
    
    // Reserved (1 byte)
    p++;
    
    // NLRI (remaining bytes)
    *nlri = (uint8_t *)p;
    *nlri_len = attr_len - (p - attr_value);
    
    return 0;
}

/**
 * Extract MP_UNREACH_NLRI attribute
 */
int bgp_extract_mp_unreach_nlri(const uint8_t *attr_value, size_t attr_len,
                                uint16_t *afi, uint8_t *safi,
                                uint8_t **nlri, size_t *nlri_len) {
    if (!attr_value || attr_len < 3 || !afi || !safi || !nlri || !nlri_len) {
        return -1;
    }
    
    const uint8_t *p = attr_value;
    
    // AFI (2 bytes)
    *afi = ntohs(*(uint16_t *)p);
    p += 2;
    
    // SAFI (1 byte)
    *safi = *p++;
    
    // Withdrawn Routes (remaining bytes)
    *nlri = (uint8_t *)p;
    *nlri_len = attr_len - 3;
    
    return 0;
}

/**
 * Parse path attributes from UPDATE message
 */
int bgp_parse_update_attributes(const uint8_t *data, size_t len,
                               uint32_t *next_hop,
                               uint8_t **mp_reach_nlri, size_t *mp_reach_len,
                               uint8_t **mp_unreach_nlri, size_t *mp_unreach_len) {
    if (!data || len < 1 || !next_hop || !mp_reach_nlri || !mp_reach_len ||
        !mp_unreach_nlri || !mp_unreach_len) {
        return -1;
    }
    
    *next_hop = 0;
    *mp_reach_nlri = NULL;
    *mp_reach_len = 0;
    *mp_unreach_nlri = NULL;
    *mp_unreach_len = 0;
    
    const uint8_t *p = data;
    const uint8_t *end = data + len;
    
    while (p < end) {
        if (p + 3 > end) {
            fprintf(stderr, "BGP: Path attribute too short\n");
            return -1;
        }
        
        // Attribute flags
        uint8_t flags = *p++;
        
        // Attribute type code
        uint8_t type = *p++;
        
        // Attribute length
        uint16_t attr_len;
        if (flags & BGP_ATTR_FLAG_EXTENDED) {
            if (p + 2 > end) return -1;
            attr_len = ntohs(*(uint16_t *)p);
            p += 2;
        } else {
            if (p + 1 > end) return -1;
            attr_len = *p++;
        }
        
        // Attribute value
        if (p + attr_len > end) {
            fprintf(stderr, "BGP: Attribute value extends beyond message\n");
            return -1;
        }
        
        const uint8_t *value = p;
        p += attr_len;
        
        // Process important attributes
        switch (type) {
            case BGP_ATTR_NEXT_HOP:
                if (attr_len == 4) {
                    *next_hop = *(uint32_t *)value;
                }
                break;
                
            case BGP_ATTR_MP_REACH_NLRI: {
                uint16_t afi;
                uint8_t safi;
                
                if (bgp_extract_mp_reach_nlri(value, attr_len, &afi, &safi,
                                             next_hop, mp_reach_nlri,
                                             mp_reach_len) == 0) {
                    if (afi != BGP_AFI_L2VPN || safi != BGP_SAFI_EVPN) {
                        // Not EVPN, ignore
                        *mp_reach_nlri = NULL;
                        *mp_reach_len = 0;
                    }
                }
                break;
            }
            
            case BGP_ATTR_MP_UNREACH_NLRI: {
                uint16_t afi;
                uint8_t safi;
                
                if (bgp_extract_mp_unreach_nlri(value, attr_len, &afi, &safi,
                                               mp_unreach_nlri,
                                               mp_unreach_len) == 0) {
                    if (afi != BGP_AFI_L2VPN || safi != BGP_SAFI_EVPN) {
                        // Not EVPN, ignore
                        *mp_unreach_nlri = NULL;
                        *mp_unreach_len = 0;
                    }
                }
                break;
            }
            
            default:
                // Ignore other attributes
                break;
        }
    }
    
    return 0;
}

/**
 * Process BGP UPDATE message
 * 
 * This is called from BGP FSM when UPDATE message is received
 */
int bgp_process_update(bgp_connection_t *conn, evpn_ctx_t *evpn,
                      const uint8_t *data, size_t len) {
    if (!conn || !evpn || !data || len < 4) {
        return -1;
    }
    
    const uint8_t *p = data;
    
    // Withdrawn Routes Length (2 bytes)
    uint16_t withdrawn_len = ntohs(*(uint16_t *)p);
    p += 2;
    
    if (len < 4 + withdrawn_len) {
        fprintf(stderr, "BGP: UPDATE message too short for withdrawn routes\n");
        return -1;
    }
    
    // Withdrawn Routes (skip for now - EVPN uses MP_UNREACH_NLRI)
    p += withdrawn_len;
    
    // Total Path Attribute Length (2 bytes)
    uint16_t attr_len = ntohs(*(uint16_t *)p);
    p += 2;
    
    if (len < 4 + withdrawn_len + attr_len) {
        fprintf(stderr, "BGP: UPDATE message too short for attributes\n");
        return -1;
    }
    
    // Parse Path Attributes
    uint32_t next_hop = 0;
    uint8_t *mp_reach_nlri = NULL;
    size_t mp_reach_len = 0;
    uint8_t *mp_unreach_nlri = NULL;
    size_t mp_unreach_len = 0;
    
    if (bgp_parse_update_attributes(p, attr_len, &next_hop,
                                   &mp_reach_nlri, &mp_reach_len,
                                   &mp_unreach_nlri, &mp_unreach_len) != 0) {
        fprintf(stderr, "BGP: Failed to parse path attributes\n");
        return -1;
    }
    
    // Process MP_REACH_NLRI (new routes)
    if (mp_reach_nlri && mp_reach_len > 0) {
        printf("BGP: Processing MP_REACH_NLRI (%zu bytes)\n", mp_reach_len);
        
        struct in_addr addr;
        addr.s_addr = next_hop;
        printf("     Next Hop: %s\n", inet_ntoa(addr));
        
        // Process EVPN NLRI
        evpn_process_nlri(evpn, mp_reach_nlri, mp_reach_len, next_hop, false);
    }
    
    // Process MP_UNREACH_NLRI (withdrawals)
    if (mp_unreach_nlri && mp_unreach_len > 0) {
        printf("BGP: Processing MP_UNREACH_NLRI (%zu bytes)\n", mp_unreach_len);
        
        // Process EVPN withdrawals
        evpn_process_nlri(evpn, mp_unreach_nlri, mp_unreach_len, 0, true);
    }
    
    return 0;
}

/* ============================================================
 * BGP UPDATE Message Construction
 * ============================================================ */

/**
 * Build MP_REACH_NLRI attribute
 */
int bgp_build_mp_reach_nlri(uint32_t next_hop,
                            const uint8_t *nlri, size_t nlri_len,
                            uint8_t *buf, size_t buf_size, size_t *attr_len) {
    if (!nlri || !buf || !attr_len || buf_size < nlri_len + 20) {
        return -1;
    }
    
    uint8_t *p = buf;
    
    // Attribute Flags
    *p++ = BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_EXTENDED;
    
    // Attribute Type Code
    *p++ = BGP_ATTR_MP_REACH_NLRI;
    
    // Attribute Length (will be filled at end)
    uint8_t *len_ptr = p;
    p += 2;
    
    uint8_t *value_start = p;
    
    // AFI (2 bytes) - L2VPN
    *(uint16_t *)p = htons(BGP_AFI_L2VPN);
    p += 2;
    
    // SAFI (1 byte) - EVPN
    *p++ = BGP_SAFI_EVPN;
    
    // Next Hop Length (1 byte)
    *p++ = 4;  // IPv4
    
    // Next Hop (4 bytes)
    *(uint32_t *)p = next_hop;
    p += 4;
    
    // Reserved (1 byte)
    *p++ = 0;
    
    // NLRI
    memcpy(p, nlri, nlri_len);
    p += nlri_len;
    
    // Fill in attribute length
    uint16_t value_len = p - value_start;
    *(uint16_t *)len_ptr = htons(value_len);
    
    *attr_len = p - buf;
    
    return 0;
}

/**
 * Build MP_UNREACH_NLRI attribute
 */
int bgp_build_mp_unreach_nlri(const uint8_t *nlri, size_t nlri_len,
                              uint8_t *buf, size_t buf_size, size_t *attr_len) {
    if (!nlri || !buf || !attr_len || buf_size < nlri_len + 10) {
        return -1;
    }
    
    uint8_t *p = buf;
    
    // Attribute Flags
    *p++ = BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_EXTENDED;
    
    // Attribute Type Code
    *p++ = BGP_ATTR_MP_UNREACH_NLRI;
    
    // Attribute Length
    uint16_t value_len = 3 + nlri_len;
    *(uint16_t *)p = htons(value_len);
    p += 2;
    
    // AFI (2 bytes) - L2VPN
    *(uint16_t *)p = htons(BGP_AFI_L2VPN);
    p += 2;
    
    // SAFI (1 byte) - EVPN
    *p++ = BGP_SAFI_EVPN;
    
    // Withdrawn Routes
    memcpy(p, nlri, nlri_len);
    p += nlri_len;
    
    *attr_len = p - buf;
    
    return 0;
}

/**
 * Build BGP UPDATE message with EVPN route
 */
int evpn_build_update_message(evpn_ctx_t *ctx,
                              const uint8_t *nlri, size_t nlri_len,
                              uint32_t next_hop,
                              uint8_t *buf, size_t buf_size,
                              size_t *msg_len) {
    if (!ctx || !nlri || !buf || !msg_len || buf_size < BGP_MAX_MESSAGE_SIZE) {
        return -1;
    }
    
    uint8_t *p = buf;
    
    // Skip BGP header (will be added by bgp_send_update)
    p += BGP_HEADER_SIZE;
    
    // Withdrawn Routes Length (2 bytes) - none
    *(uint16_t *)p = 0;
    p += 2;
    
    // Build MP_REACH_NLRI attribute
    uint8_t attr_buf[1024];
    size_t attr_len;
    
    if (bgp_build_mp_reach_nlri(next_hop, nlri, nlri_len,
                                attr_buf, sizeof(attr_buf), &attr_len) != 0) {
        fprintf(stderr, "EVPN: Failed to build MP_REACH_NLRI\n");
        return -1;
    }
    
    // Total Path Attribute Length (2 bytes)
    *(uint16_t *)p = htons(attr_len);
    p += 2;
    
    // Path Attributes
    memcpy(p, attr_buf, attr_len);
    p += attr_len;
    
    // NLRI (empty for EVPN - NLRI is in MP_REACH_NLRI)
    
    *msg_len = p - buf;
    
    return 0;
}

/**
 * Build BGP withdrawal message for EVPN route
 */
int evpn_build_withdrawal_message(evpn_ctx_t *ctx,
                                  const uint8_t *nlri, size_t nlri_len,
                                  uint8_t *buf, size_t buf_size,
                                  size_t *msg_len) {
    if (!ctx || !nlri || !buf || !msg_len || buf_size < BGP_MAX_MESSAGE_SIZE) {
        return -1;
    }
    
    uint8_t *p = buf;
    
    // Skip BGP header
    p += BGP_HEADER_SIZE;
    
    // Withdrawn Routes Length (2 bytes) - none (use MP_UNREACH_NLRI)
    *(uint16_t *)p = 0;
    p += 2;
    
    // Build MP_UNREACH_NLRI attribute
    uint8_t attr_buf[1024];
    size_t attr_len;
    
    if (bgp_build_mp_unreach_nlri(nlri, nlri_len,
                                  attr_buf, sizeof(attr_buf), &attr_len) != 0) {
        fprintf(stderr, "EVPN: Failed to build MP_UNREACH_NLRI\n");
        return -1;
    }
    
    // Total Path Attribute Length (2 bytes)
    *(uint16_t *)p = htons(attr_len);
    p += 2;
    
    // Path Attributes
    memcpy(p, attr_buf, attr_len);
    p += attr_len;
    
    *msg_len = p - buf;
    
    return 0;
}

/**
 * Send BGP UPDATE with EVPN route to all established peers
 */
int evpn_send_update_to_peers(evpn_ctx_t *ctx,
                              const uint8_t *nlri, size_t nlri_len,
                              uint32_t next_hop) {
    if (!ctx || !nlri) {
        return -1;
    }
    
    // Build UPDATE message
    uint8_t update_buf[BGP_MAX_MESSAGE_SIZE];
    size_t update_len;
    
    if (evpn_build_update_message(ctx, nlri, nlri_len, next_hop,
                                  update_buf, sizeof(update_buf),
                                  &update_len) != 0) {
        return -1;
    }
    
    // Send to all established peers
    int sent_count = 0;
    
    for (int i = 0; i < ctx->peer_count; i++) {
        if (ctx->peers[i] && ctx->peers[i]->state == BGP_STATE_ESTABLISHED) {
            // Use existing bgp_send_update function
            const uint8_t *attrs = update_buf + BGP_HEADER_SIZE + 4;
            uint16_t attr_len = ntohs(*(uint16_t *)(update_buf + BGP_HEADER_SIZE + 2));
            
            if (bgp_send_update(&ctx->peers[i]->connection, NULL, 0,
                               attrs, attr_len) == 0) {
                sent_count++;
            }
        }
    }
    
    if (sent_count > 0) {
        printf("EVPN: Sent UPDATE to %d peer(s)\n", sent_count);
    }
    
    return sent_count > 0 ? 0 : -1;
}

/**
 * Send BGP withdrawal to all established peers
 */
int evpn_send_withdrawal_to_peers(evpn_ctx_t *ctx,
                                  const uint8_t *nlri, size_t nlri_len) {
    if (!ctx || !nlri) {
        return -1;
    }
    
    // Build withdrawal message
    uint8_t withdraw_buf[BGP_MAX_MESSAGE_SIZE];
    size_t withdraw_len;
    
    if (evpn_build_withdrawal_message(ctx, nlri, nlri_len,
                                      withdraw_buf, sizeof(withdraw_buf),
                                      &withdraw_len) != 0) {
        return -1;
    }
    
    // Send to all established peers
    int sent_count = 0;
    
    for (int i = 0; i < ctx->peer_count; i++) {
        if (ctx->peers[i] && ctx->peers[i]->state == BGP_STATE_ESTABLISHED) {
            const uint8_t *attrs = withdraw_buf + BGP_HEADER_SIZE + 4;
            uint16_t attr_len = ntohs(*(uint16_t *)(withdraw_buf + BGP_HEADER_SIZE + 2));
            
            if (bgp_send_update(&ctx->peers[i]->connection, NULL, 0,
                               attrs, attr_len) == 0) {
                sent_count++;
            }
        }
    }
    
    if (sent_count > 0) {
        printf("EVPN: Sent withdrawal to %d peer(s)\n", sent_count);
    }
    
    return sent_count > 0 ? 0 : -1;
}
