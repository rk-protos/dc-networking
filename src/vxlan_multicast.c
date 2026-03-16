
/*****************************************************************************
 * Project     : VXLAN Protocol Implementation (RFC 7348)
 * Description : Part of minimal implementation of VXLAN (RFC-7348)
 *                Virtual eXtensible Local Area Network (VXLAN)
 *                encapsulation and decapsulation 
 *                implementation with minimal testing 
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : vxlan_multicast.c  
  *                 RFC 7348 Sec 4.2 Broadcast Communication and Mapping to Multicast
 *               Includes:
 *                  - IGMP membership reports (join/leave)
 *                  - VNI to multicast group mapping
 *                  - BUM traffic handling (Broadcast, Unknown unicast, Multicast)
 *                  - (*,G) joins for VXLAN overlay networks 
 * Here is how it works for vxlan IGMP:
 *
 *       VM sends ARP broadcast (at very beginning)
 *           ↓
 *       VTEP detects BUM traffic
 *           ↓
 *       IGMP join for VNI 100 → 239.0.0.100
 *           ↓
 *       Send to multicast group
 *           ↓
 *       All VTEPs in VNI 100 receive
 *           ↓
 *       Learn MAC from response
 *           ↓
 *       Future traffic uses unicast (next TX packet from that VM for that VTEP, VNI)
 *
*****************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <poll.h>
#include "../include/vxlan_multicast.h"

/**
 * Initialize multicast context
 */
int vxlan_mcast_init(vxlan_mcast_ctx_t *ctx, uint32_t local_ip, const char *interface) {
    if (!ctx) {
        return -1;
    }
    
    memset(ctx, 0, sizeof(vxlan_mcast_ctx_t));
    
    ctx->local_ip = local_ip;
    ctx->ttl = VXLAN_DEFAULT_TTL;
    ctx->loop = false;  /* Don't loop back by default */
    
    if (interface) {
        strncpy(ctx->interface, interface, sizeof(ctx->interface) - 1);
    } else {
        strcpy(ctx->interface, "eth0");  /* Default interface */
    }
    
    /* Initialize mutex */
    if (pthread_mutex_init(&ctx->lock, NULL) != 0) {
        perror("pthread_mutex_init");
        return -1;
    }
    
    /* Initialize all group entries */
    for (int i = 0; i < MAX_MCAST_GROUPS; i++) {
        ctx->groups[i].sockfd = -1;
        ctx->groups[i].joined = false;
    }
    
    printf("VXLAN Multicast initialized:\n");
    printf("  Local IP: %s\n", inet_ntoa(*(struct in_addr*)&local_ip));
    printf("  Interface: %s\n", ctx->interface);
    printf("  TTL: %u\n", ctx->ttl);
    
    return 0;
}

/**
 * Cleanup multicast context
 */
void vxlan_mcast_cleanup(vxlan_mcast_ctx_t *ctx) {
    if (!ctx) {
        return;
    }
    
    pthread_mutex_lock(&ctx->lock);
    
    /* Leave all groups and close sockets */
    for (int i = 0; i < MAX_MCAST_GROUPS; i++) {
        if (ctx->groups[i].joined) {
            /* Leave multicast group */
            struct ip_mreq mreq;
            mreq.imr_multiaddr.s_addr = ctx->groups[i].mcast_ip;
            mreq.imr_interface.s_addr = ctx->local_ip;
            
            setsockopt(ctx->groups[i].sockfd, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                      &mreq, sizeof(mreq));
            
            if (ctx->groups[i].sockfd >= 0) {
                close(ctx->groups[i].sockfd);
            }
            
            ctx->igmp_leaves++;
        }
    }
    
    pthread_mutex_unlock(&ctx->lock);
    pthread_mutex_destroy(&ctx->lock);
    
    printf("VXLAN Multicast cleaned up (%d joins, %d leaves)\n",
           (int)ctx->igmp_joins, (int)ctx->igmp_leaves);
}

/**
 * Check if multicast group is valid
 */
bool vxlan_mcast_is_valid_group(uint32_t mcast_ip) {
    /* Multicast range: 224.0.0.0 to 239.255.255.255 (Class D) */
    uint32_t ip_host = ntohl(mcast_ip);
    return (ip_host >= 0xE0000000 && ip_host <= 0xEFFFFFFF);
}

/**
 * Calculate default multicast group from VNI
 * 
 * Mapping: VNI -> 239.VNI[23:16].VNI[15:8].VNI[7:0]
 */
uint32_t vxlan_mcast_vni_to_group(uint32_t vni) {
    /* Use 239.0.0.0/8 range */
    /* Format: 239.X.Y.Z where XYZ = VNI (24 bits) */
    
    uint8_t octet1 = 239;
    uint8_t octet2 = (vni >> 16) & 0xFF;
    uint8_t octet3 = (vni >> 8) & 0xFF;
    uint8_t octet4 = vni & 0xFF;
    
    uint32_t mcast_ip = (octet1 << 24) | (octet2 << 16) | (octet3 << 8) | octet4;
    
    return htonl(mcast_ip);
}

/**
 * Join a multicast group for a VNI
 */
int vxlan_mcast_join(vxlan_mcast_ctx_t *ctx, uint32_t vni, uint32_t mcast_ip) {
    if (!ctx) {
        return -1;
    }
    
    if (!vxlan_mcast_is_valid_group(mcast_ip)) {
        fprintf(stderr, "Invalid multicast group: %s\n",
                inet_ntoa(*(struct in_addr*)&mcast_ip));
        return -1;
    }
    
    pthread_mutex_lock(&ctx->lock);
    
    /* Check if already joined */
    for (int i = 0; i < MAX_MCAST_GROUPS; i++) {
        if (ctx->groups[i].joined && ctx->groups[i].vni == vni) {
            pthread_mutex_unlock(&ctx->lock);
            return 0;  /* Already joined */
        }
    }
    
    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_MCAST_GROUPS; i++) {
        if (!ctx->groups[i].joined) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        fprintf(stderr, "Maximum multicast groups reached (%d)\n", MAX_MCAST_GROUPS);
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }
    
    /* Create multicast socket */
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }
    
    /* Allow multiple sockets to bind to same port */
    int reuse = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        close(sockfd);
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }
    
    /* Bind to VXLAN port */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(4789);  /* VXLAN port */
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sockfd);
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }
    
    /* Join multicast group (IGMP membership report) */
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = mcast_ip;
    mreq.imr_interface.s_addr = ctx->local_ip;
    
    if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("setsockopt IP_ADD_MEMBERSHIP");
        close(sockfd);
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }
    
    /* Set multicast TTL */
    if (setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_TTL, &ctx->ttl, sizeof(ctx->ttl)) < 0) {
        perror("setsockopt IP_MULTICAST_TTL");
    }
    
    /* Set multicast loopback */
    unsigned char loop = ctx->loop ? 1 : 0;
    if (setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) < 0) {
        perror("setsockopt IP_MULTICAST_LOOP");
    }
    
    /* Set outgoing interface */
    struct in_addr interface_addr;
    interface_addr.s_addr = ctx->local_ip;
    if (setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_IF, &interface_addr, sizeof(interface_addr)) < 0) {
        perror("setsockopt IP_MULTICAST_IF");
    }
    
    /* Save group information */
    ctx->groups[slot].vni = vni;
    ctx->groups[slot].mcast_ip = mcast_ip;
    ctx->groups[slot].sockfd = sockfd;
    ctx->groups[slot].joined = true;
    ctx->groups[slot].join_time = time(NULL);
    ctx->groups[slot].tx_packets = 0;
    ctx->groups[slot].rx_packets = 0;
    
    ctx->group_count++;
    ctx->igmp_joins++;
    
    pthread_mutex_unlock(&ctx->lock);
    
    printf("Joined multicast group: VNI %u -> %s\n",
           vni, inet_ntoa(*(struct in_addr*)&mcast_ip));
    
    return 0;
}

/**
 * Leave a multicast group
 */
int vxlan_mcast_leave(vxlan_mcast_ctx_t *ctx, uint32_t vni) {
    if (!ctx) {
        return -1;
    }
    
    pthread_mutex_lock(&ctx->lock);
    
    /* Find the group */
    for (int i = 0; i < MAX_MCAST_GROUPS; i++) {
        if (ctx->groups[i].joined && ctx->groups[i].vni == vni) {
            /* Leave multicast group (IGMP leave) */
            struct ip_mreq mreq;
            mreq.imr_multiaddr.s_addr = ctx->groups[i].mcast_ip;
            mreq.imr_interface.s_addr = ctx->local_ip;
            
            if (setsockopt(ctx->groups[i].sockfd, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                          &mreq, sizeof(mreq)) < 0) {
                perror("setsockopt IP_DROP_MEMBERSHIP");
            }
            
            close(ctx->groups[i].sockfd);
            
            printf("Left multicast group: VNI %u -> %s\n",
                   vni, inet_ntoa(*(struct in_addr*)&ctx->groups[i].mcast_ip));
            
            ctx->groups[i].joined = false;
            ctx->groups[i].sockfd = -1;
            ctx->group_count--;
            ctx->igmp_leaves++;
            
            pthread_mutex_unlock(&ctx->lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&ctx->lock);
    return -1;  /* Not found */
}

/**
 * Auto-join multicast group based on VNI
 */
int vxlan_mcast_auto_join(vxlan_mcast_ctx_t *ctx, uint32_t vni) {
    uint32_t mcast_ip = vxlan_mcast_vni_to_group(vni);
    return vxlan_mcast_join(ctx, vni, mcast_ip);
}

/**
 * Get multicast group for VNI
 */
int vxlan_mcast_get_group(vxlan_mcast_ctx_t *ctx, uint32_t vni, uint32_t *mcast_ip) {
    if (!ctx || !mcast_ip) {
        return -1;
    }
    
    pthread_mutex_lock(&ctx->lock);
    
    for (int i = 0; i < MAX_MCAST_GROUPS; i++) {
        if (ctx->groups[i].joined && ctx->groups[i].vni == vni) {
            *mcast_ip = ctx->groups[i].mcast_ip;
            pthread_mutex_unlock(&ctx->lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&ctx->lock);
    return -1;
}

/**
 * Check if frame is BUM traffic
 */
bool vxlan_mcast_is_bum(const uint8_t *frame, size_t frame_len, bum_type_t *bum_type) {
    if (!frame || frame_len < 6) {
        return false;
    }
    
    const uint8_t *dst_mac = frame;
    
    /* Check for broadcast (FF:FF:FF:FF:FF:FF) */
    if (dst_mac[0] == 0xFF && dst_mac[1] == 0xFF && dst_mac[2] == 0xFF &&
        dst_mac[3] == 0xFF && dst_mac[4] == 0xFF && dst_mac[5] == 0xFF) {
        if (bum_type) *bum_type = BUM_BROADCAST;
        return true;
    }
    
    /* Check for multicast (least significant bit of first octet = 1) */
    if (dst_mac[0] & 0x01) {
        if (bum_type) *bum_type = BUM_MULTICAST;
        return true;
    }
    
    /* For unknown unicast, we'd need to check MAC table (not done here) */
    /* Caller should check MAC table before calling this */
    
    return false;
}

/**
 * Send BUM traffic to multicast group
 */
int vxlan_mcast_send_bum(vxlan_mcast_ctx_t *ctx,
                         uint32_t vni,
                         const uint8_t *vxlan_packet,
                         size_t packet_len,
                         bum_type_t bum_type) {
    if (!ctx || !vxlan_packet) {
        return -1;
    }
    
    (void)bum_type;  /* Currently unused, but could be used for stats */
    
    pthread_mutex_lock(&ctx->lock);
    
    /* Find multicast group for this VNI */
    int slot = -1;
    for (int i = 0; i < MAX_MCAST_GROUPS; i++) {
        if (ctx->groups[i].joined && ctx->groups[i].vni == vni) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        fprintf(stderr, "No multicast group for VNI %u\n", vni);
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }
    
    /* Send to multicast group */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(4789);
    addr.sin_addr.s_addr = ctx->groups[slot].mcast_ip;
    
    ssize_t sent = sendto(ctx->groups[slot].sockfd, vxlan_packet, packet_len, 0,
                          (struct sockaddr*)&addr, sizeof(addr));
    
    if (sent < 0) {
        perror("sendto multicast");
        pthread_mutex_unlock(&ctx->lock);
        return -1;
    }
    
    ctx->groups[slot].tx_packets++;
    ctx->total_tx_mcast++;
    
    pthread_mutex_unlock(&ctx->lock);
    
    return sent;
}

/**
 * Receive multicast packet
 */
int vxlan_mcast_recv(vxlan_mcast_ctx_t *ctx,
                     uint8_t *buffer,
                     size_t buf_len,
                     uint32_t *vni,
                     uint32_t *src_ip,
                     int timeout_ms) {
    if (!ctx || !buffer || !vni || !src_ip) {
        return -1;
    }
    
    /* Use poll() to wait on all multicast sockets */
    struct pollfd fds[MAX_MCAST_GROUPS];
    int nfds = 0;
    int slot_map[MAX_MCAST_GROUPS];  /* Map poll index to slot */
    
    pthread_mutex_lock(&ctx->lock);
    
    for (int i = 0; i < MAX_MCAST_GROUPS; i++) {
        if (ctx->groups[i].joined && ctx->groups[i].sockfd >= 0) {
            fds[nfds].fd = ctx->groups[i].sockfd;
            fds[nfds].events = POLLIN;
            slot_map[nfds] = i;
            nfds++;
        }
    }
    
    pthread_mutex_unlock(&ctx->lock);
    
    if (nfds == 0) {
        return -1;  /* No active multicast groups */
    }
    
    /* Wait for data */
    int ret = poll(fds, nfds, timeout_ms);
    
    if (ret < 0) {
        perror("poll");
        return -1;
    }
    
    if (ret == 0) {
        return 0;  /* Timeout */
    }
    
    /* Find which socket has data */
    for (int i = 0; i < nfds; i++) {
        if (fds[i].revents & POLLIN) {
            struct sockaddr_in from;
            socklen_t fromlen = sizeof(from);
            
            ssize_t received = recvfrom(fds[i].fd, buffer, buf_len, 0,
                                       (struct sockaddr*)&from, &fromlen);
            
            if (received < 0) {
                perror("recvfrom");
                return -1;
            }
            
            pthread_mutex_lock(&ctx->lock);
            
            int slot = slot_map[i];
            *vni = ctx->groups[slot].vni;
            *src_ip = from.sin_addr.s_addr;
            ctx->groups[slot].rx_packets++;
            ctx->total_rx_mcast++;
            
            pthread_mutex_unlock(&ctx->lock);
            
            return received;
        }
    }
    
    return 0;
}

/**
 * Set multicast TTL
 */
int vxlan_mcast_set_ttl(vxlan_mcast_ctx_t *ctx, uint8_t ttl) {
    if (!ctx) {
        return -1;
    }
    
    ctx->ttl = ttl;
    
    /* Update all existing sockets */
    pthread_mutex_lock(&ctx->lock);
    
    for (int i = 0; i < MAX_MCAST_GROUPS; i++) {
        if (ctx->groups[i].joined && ctx->groups[i].sockfd >= 0) {
            setsockopt(ctx->groups[i].sockfd, IPPROTO_IP, IP_MULTICAST_TTL,
                      &ttl, sizeof(ttl));
        }
    }
    
    pthread_mutex_unlock(&ctx->lock);
    
    return 0;
}

/**
 * Enable/disable multicast loopback
 */
int vxlan_mcast_set_loop(vxlan_mcast_ctx_t *ctx, bool loop) {
    if (!ctx) {
        return -1;
    }
    
    ctx->loop = loop;
    
    pthread_mutex_lock(&ctx->lock);
    
    unsigned char loop_val = loop ? 1 : 0;
    for (int i = 0; i < MAX_MCAST_GROUPS; i++) {
        if (ctx->groups[i].joined && ctx->groups[i].sockfd >= 0) {
            setsockopt(ctx->groups[i].sockfd, IPPROTO_IP, IP_MULTICAST_LOOP,
                      &loop_val, sizeof(loop_val));
        }
    }
    
    pthread_mutex_unlock(&ctx->lock);
    
    return 0;
}

/**
 * Get statistics
 */
void vxlan_mcast_get_stats(vxlan_mcast_ctx_t *ctx,
                           uint64_t *tx_packets,
                           uint64_t *rx_packets,
                           uint64_t *igmp_joins,
                           uint64_t *igmp_leaves) {
    if (!ctx) {
        return;
    }
    
    pthread_mutex_lock(&ctx->lock);
    
    if (tx_packets) *tx_packets = ctx->total_tx_mcast;
    if (rx_packets) *rx_packets = ctx->total_rx_mcast;
    if (igmp_joins) *igmp_joins = ctx->igmp_joins;
    if (igmp_leaves) *igmp_leaves = ctx->igmp_leaves;
    
    pthread_mutex_unlock(&ctx->lock);
}

/**
 * Dump multicast groups
 */
void vxlan_mcast_dump_groups(vxlan_mcast_ctx_t *ctx) {
    if (!ctx) {
        return;
    }
    
    printf("\n=== VXLAN Multicast Groups ===\n");
    printf("%-10s %-20s %-10s %-10s\n", "VNI", "Multicast Group", "TX Pkts", "RX Pkts");
    printf("-------------------------------------------------------------\n");
    
    pthread_mutex_lock(&ctx->lock);
    
    for (int i = 0; i < MAX_MCAST_GROUPS; i++) {
        if (ctx->groups[i].joined) {
            printf("%-10u %-20s %-10u %-10u\n",
                   ctx->groups[i].vni,
                   inet_ntoa(*(struct in_addr*)&ctx->groups[i].mcast_ip),
                   ctx->groups[i].tx_packets,
                   ctx->groups[i].rx_packets);
        }
    }
    
    pthread_mutex_unlock(&ctx->lock);
    
    printf("-------------------------------------------------------------\n");
    printf("Total groups: %d\n", ctx->group_count);
    printf("Total TX: %lu, RX: %lu\n", ctx->total_tx_mcast, ctx->total_rx_mcast);
    printf("IGMP joins: %lu, leaves: %lu\n\n", ctx->igmp_joins, ctx->igmp_leaves);
}
