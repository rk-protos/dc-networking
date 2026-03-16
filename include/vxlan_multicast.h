
 /*****************************************************************************
 * Project     : VXLAN Protocol Implementation (RFC 7348)
 * Description : Part of minimal implementation of VXLAN (RFC-7348)
 *                Virtual eXtensible Local Area Network (VXLAN)
 *                encapsulation and decapsulation 
 *                implementation with minimal testing 
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : vxlan_multicast.h  
 * Purpose     : IGMP Multicast Support for VXLAN
 *                 RFC 7348 Sec 4.2 Broadcast Communication and Mapping to Multicast
 *               Includes:
 *                  - IGMP membership reports (join/leave)
 *                  - VNI to multicast group mapping
 *                  - BUM traffic handling (Broadcast, Unknown unicast, Multicast)
 *                  - (*,G) joins for VXLAN overlay networks 
 *****************************************************************************/

#ifndef VXLAN_MULTICAST_H
#define VXLAN_MULTICAST_H

#include <stdint.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <pthread.h>

/* Multicast Constants */
#define VXLAN_MCAST_GROUP_MIN   0xE0000000  /* 224.0.0.0 */
#define VXLAN_MCAST_GROUP_MAX   0xEFFFFFFF  /* 239.255.255.255 */
#define VXLAN_DEFAULT_TTL       64          /* Multicast TTL */
#define MAX_MCAST_GROUPS        256         /* Max multicast groups per VTEP */

/**
 * BUM Traffic Types
 */
typedef enum {
    BUM_BROADCAST,      /* L2 broadcast (FF:FF:FF:FF:FF:FF) */
    BUM_UNKNOWN_UNICAST,/* Unknown destination MAC */
    BUM_MULTICAST       /* L2 multicast (01:xx:xx:xx:xx:xx) */
} bum_type_t;

/**
 * Multicast Group Entry
 */
typedef struct {
    uint32_t vni;               /* VXLAN Network Identifier */
    uint32_t mcast_ip;          /* Multicast group IP (224.0.0.0 - 239.255.255.255) */
    int      sockfd;            /* Multicast socket file descriptor */
    bool     joined;            /* Currently joined to group */
    time_t   join_time;         /* When we joined this group */
    uint32_t tx_packets;        /* Packets transmitted to group */
    uint32_t rx_packets;        /* Packets received from group */
} mcast_group_t;

/**
 * Multicast Context
 */
typedef struct {
    mcast_group_t groups[MAX_MCAST_GROUPS];  /* Active multicast groups */
    int           group_count;               /* Number of active groups */
    pthread_mutex_t lock;                    /* Thread safety */
    
    /* Configuration */
    uint32_t local_ip;          /* Local VTEP IP for multicast source */
    uint8_t  ttl;              /* Multicast TTL (default 64) */
    bool     loop;             /* Loopback multicast packets */
    char     interface[16];     /* Network interface for multicast (e.g., "eth0") */
    
    /* Statistics */
    uint64_t total_tx_mcast;    /* Total multicast packets sent */
    uint64_t total_rx_mcast;    /* Total multicast packets received */
    uint64_t igmp_joins;        /* IGMP join count */
    uint64_t igmp_leaves;       /* IGMP leave count */
} vxlan_mcast_ctx_t;

/**
 * Initialize multicast context
 * 
 * @param ctx       Multicast context to initialize
 * @param local_ip  Local VTEP IP address
 * @param interface Network interface name (e.g., "eth0")
 * @return          0 on success, -1 on error
 */
int vxlan_mcast_init(vxlan_mcast_ctx_t *ctx, uint32_t local_ip, const char *interface);

/**
 * Cleanup multicast context
 * 
 * Leaves all multicast groups and closes sockets
 * 
 * @param ctx   Multicast context
 */
void vxlan_mcast_cleanup(vxlan_mcast_ctx_t *ctx);

/**
 * Join a multicast group for a VNI
 * 
 * RFC 7348: VTEP provides IGMP membership reports to join VXLAN multicast groups
 * 
 * @param ctx       Multicast context
 * @param vni       VXLAN Network Identifier
 * @param mcast_ip  Multicast group IP (224.0.0.0 - 239.255.255.255)
 * @return          0 on success, -1 on error
 */
int vxlan_mcast_join(vxlan_mcast_ctx_t *ctx, uint32_t vni, uint32_t mcast_ip);

/**
 * Leave a multicast group
 * 
 * RFC 7348: VTEP sends IGMP leave when VNI is no longer needed
 * 
 * @param ctx   Multicast context
 * @param vni   VXLAN Network Identifier
 * @return      0 on success, -1 on error
 */
int vxlan_mcast_leave(vxlan_mcast_ctx_t *ctx, uint32_t vni);

/**
 * Send BUM traffic to multicast group
 * 
 * RFC 7348: Broadcast/unknown unicast/multicast sent via IP multicast
 * 
 * @param ctx           Multicast context
 * @param vni           VNI for this frame
 * @param vxlan_packet  Encapsulated VXLAN packet
 * @param packet_len    Packet length
 * @param bum_type      Type of BUM traffic
 * @return              Bytes sent, or -1 on error
 */
int vxlan_mcast_send_bum(vxlan_mcast_ctx_t *ctx,
                         uint32_t vni,
                         const uint8_t *vxlan_packet,
                         size_t packet_len,
                         bum_type_t bum_type);

/**
 * Receive multicast packet
 * 
 * @param ctx       Multicast context
 * @param buffer    Buffer for received packet
 * @param buf_len   Buffer length
 * @param vni       Output: VNI of received packet
 * @param src_ip    Output: Source VTEP IP
 * @param timeout_ms Timeout in milliseconds (0 = blocking)
 * @return          Bytes received, 0 on timeout, -1 on error
 */
int vxlan_mcast_recv(vxlan_mcast_ctx_t *ctx,
                     uint8_t *buffer,
                     size_t buf_len,
                     uint32_t *vni,
                     uint32_t *src_ip,
                     int timeout_ms);

/**
 * Get multicast group for VNI
 * 
 * @param ctx       Multicast context
 * @param vni       VNI to lookup
 * @param mcast_ip  Output: multicast IP
 * @return          0 if found, -1 if not found
 */
int vxlan_mcast_get_group(vxlan_mcast_ctx_t *ctx, uint32_t vni, uint32_t *mcast_ip);

/**
 * Set multicast TTL
 * 
 * @param ctx   Multicast context
 * @param ttl   Time-to-live (1-255, default 64)
 * @return      0 on success, -1 on error
 */
int vxlan_mcast_set_ttl(vxlan_mcast_ctx_t *ctx, uint8_t ttl);

/**
 * Enable/disable multicast loopback
 * 
 * @param ctx   Multicast context
 * @param loop  true to enable loopback, false to disable
 * @return      0 on success, -1 on error
 */
int vxlan_mcast_set_loop(vxlan_mcast_ctx_t *ctx, bool loop);

/**
 * Check if frame is BUM traffic
 * 
 * @param frame     Ethernet frame
 * @param frame_len Frame length
 * @param bum_type  Output: type of BUM traffic
 * @return          true if BUM traffic, false otherwise
 */
bool vxlan_mcast_is_bum(const uint8_t *frame, size_t frame_len, bum_type_t *bum_type);

/**
 * Check if multicast group is valid
 * 
 * @param mcast_ip  Multicast IP to validate
 * @return          true if valid (224.0.0.0 - 239.255.255.255)
 */
bool vxlan_mcast_is_valid_group(uint32_t mcast_ip);

/**
 * Get multicast statistics
 * 
 * @param ctx   Multicast context
 * @param stats Output: statistics structure
 */
void vxlan_mcast_get_stats(vxlan_mcast_ctx_t *ctx, 
                           uint64_t *tx_packets,
                           uint64_t *rx_packets,
                           uint64_t *igmp_joins,
                           uint64_t *igmp_leaves);

/**
 * Dump multicast group information (debugging)
 * 
 * @param ctx   Multicast context
 */
void vxlan_mcast_dump_groups(vxlan_mcast_ctx_t *ctx);

/**
 * Auto-join multicast group based on VNI
 * 
 * Uses standard VNI-to-multicast mapping:
 * 239.0.0.0/8 range with VNI encoded in lower 24 bits
 * 
 * @param ctx   Multicast context
 * @param vni   VNI to join
 * @return      0 on success, -1 on error
 */
int vxlan_mcast_auto_join(vxlan_mcast_ctx_t *ctx, uint32_t vni);

/**
 * Calculate default multicast group from VNI
 * 
 * RFC 7348: Management layer provides VNI to multicast group mapping
 * This implements a common default mapping scheme
 * 
 * @param vni   VXLAN Network Identifier
 * @return      Multicast IP address (239.x.x.x)
 */
uint32_t vxlan_mcast_vni_to_group(uint32_t vni);

#endif /* VXLAN_MULTICAST_H */
