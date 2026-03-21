
/*****************************************************************************
 * Project     : EVPN Protocol Implementation (RFC 8365)
 * Description : Part of minimal implementation of EVPN
 *                Ethernet VPN (EVPN) Control Plane for Network Virtualization Overlay
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : evpn.h  
 * Purpose     : This implementation provides BGP-based control plane for 
 *                 VXLAN data plane  (RFC 7348), eliminating the need for 
 *                 flooding unknown unicast traffic.
 *****************************************************************************/

#ifndef EVPN_H
#define EVPN_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

/* Include BGP structures */
#include "evpn_bgp.h"

/* EVPN Constants */
#define EVPN_MAX_ROUTE_TARGETS      16
#define EVPN_MAX_MAC_ENTRIES        100000
#define EVPN_MAX_ESI_ENTRIES        1024
#define EVPN_MAX_ETHERNET_SEGMENTS  256
#define MAX_ETHERNET_SEGMENTS       256  /* Alias for compatibility */
#define MAX_ES_PE_COUNT             16   /* Max PEs per Ethernet Segment */
#define EVPN_ESI_LENGTH             10
#define EVPN_MAC_AGING_TIME         3600    /* 1 hour */

/* BGP Constants */
#define BGP_PORT                    179
#define BGP_VERSION                 4
#define BGP_KEEPALIVE_TIME          60
#define BGP_HOLD_TIME               180

/* EVPN Route Types (RFC 7432) */
typedef enum {
    EVPN_ROUTE_TYPE_ETHERNET_AD         = 1,  /* Ethernet Auto-Discovery */
    EVPN_ROUTE_TYPE_MAC_IP_ADVERTISEMENT = 2,  /* MAC/IP Advertisement */
    EVPN_ROUTE_TYPE_INCLUSIVE_MULTICAST  = 3,  /* Inclusive Multicast */
    EVPN_ROUTE_TYPE_ETHERNET_SEGMENT     = 4,  /* Ethernet Segment */
    EVPN_ROUTE_TYPE_IP_PREFIX            = 5   /* IP Prefix Route */
} evpn_route_type_t;

/* Ethernet Segment Redundancy Mode */
typedef enum {
    EVPN_REDUNDANCY_SINGLE_ACTIVE = 0,  /* Single-Active multihoming */
    EVPN_REDUNDANCY_ALL_ACTIVE = 1,     /* All-Active multihoming */
    EVPN_SINGLE_ACTIVE = 0,             /* Alias for compatibility */
    EVPN_ALL_ACTIVE = 1                 /* Alias for compatibility */
} evpn_redundancy_mode_t;

/* DF Election Algorithm */
typedef enum {
    EVPN_DF_DEFAULT = 0,       /* Default DF election (modulo) */
    EVPN_DF_HRW = 1            /* Highest Random Weight */
} evpn_df_algorithm_t;

/**
 * Route Distinguisher (RD)
 * Format: ASN:nn or IP:nn
 */
typedef struct {
    uint16_t type;             /* 0=ASN:nn, 1=IP:nn, 2=4-byte-ASN:nn */
    union {
        struct {
            uint32_t asn;      /* 2-byte or 4-byte ASN */
            uint32_t number;
        } asn_based;
        struct {
            uint32_t ip;       /* IPv4 address */
            uint16_t number;
        } ip_based;
    } value;
} evpn_rd_t;

/**
 * Route Target (RT)
 * Used for route filtering
 */
typedef struct {
    uint16_t type;             /* Same as RD */
    union {
        struct {
            uint32_t asn;
            uint32_t number;
        } asn_based;
        struct {
            uint32_t ip;
            uint16_t number;
        } ip_based;
    } value;
} evpn_rt_t;

/**
 * Ethernet Segment Identifier (ESI)
 * 10-byte identifier for multi-homed segments
 */
typedef struct {
    uint8_t type;              /* ESI Type (0-5) */
    uint8_t value[9];          /* 9-byte ESI value */
} __attribute__((packed)) evpn_esi_t;

/**
 * MAC/IP Advertisement Route (Type 2)
 */
typedef struct {
    evpn_rd_t rd;              /* Route Distinguisher */
    evpn_esi_t esi;            /* Ethernet Segment Identifier */
    uint32_t ethernet_tag;     /* VLAN ID or 0 */
    uint8_t mac_len;           /* MAC address length (48) */
    uint8_t mac[6];            /* MAC address */
    uint8_t ip_len;            /* IP address length (0, 32, or 128) */
    uint32_t ip;               /* IPv4 address (or IPv6) */
    uint32_t label1;           /* MPLS Label 1 (or VNI for VXLAN) */
    uint32_t label2;           /* MPLS Label 2 (optional) */
    uint32_t seq_num;          /* Sequence number for MAC mobility */
    bool sticky;               /* Sticky MAC flag */
} evpn_mac_ip_route_t;

/**
 * Inclusive Multicast Route (Type 3)
 */
typedef struct {
    evpn_rd_t rd;              /* Route Distinguisher */
    uint32_t ethernet_tag;     /* VLAN ID or 0 */
    uint8_t ip_len;            /* Originating router IP length */
    uint32_t originating_router_ip; /* VTEP IP address */
    uint8_t tunnel_type;       /* VXLAN=8, NVGRE=9, etc. */
    uint32_t vni;              /* VXLAN Network Identifier */
} evpn_inclusive_mcast_route_t;

/**
 * Ethernet Segment Route (Type 4)
 */
typedef struct {
    evpn_rd_t rd;              /* Route Distinguisher */
    evpn_esi_t esi;            /* Ethernet Segment Identifier */
    uint8_t ip_len;            /* IP address length */
    uint32_t originating_router_ip; /* PE IP address */
} evpn_ethernet_segment_route_t;

/**
 * Ethernet Auto-Discovery Route (Type 1)
 */
typedef struct {
    evpn_rd_t rd;              /* Route Distinguisher */
    evpn_esi_t esi;            /* Ethernet Segment Identifier */
    uint32_t ethernet_tag;     /* VLAN ID or 0xFFFFFFFF for all */
    uint32_t label;            /* MPLS label or VNI */
} evpn_ethernet_ad_route_t;

/**
 * Ethernet Segment
 */
typedef struct {
    evpn_esi_t esi;            /* ESI identifier */
    uint32_t pe_ips[MAX_ES_PE_COUNT]; /* PE IP addresses */
    int pe_count;              /* Number of PEs */
    evpn_redundancy_mode_t mode; /* Single-active or all-active */
    evpn_df_algorithm_t df_alg;  /* DF election algorithm */
    uint32_t df_ip;            /* Designated Forwarder IP */
    bool is_df;                /* Are we the DF? */
    time_t last_update;        /* Last update time */
    pthread_mutex_t lock;      /* Thread safety */
} evpn_ethernet_segment_t;

/**
 * MAC-VRF (MAC Virtual Routing and Forwarding)
 */
typedef struct {
    uint32_t vni;              /* VXLAN Network Identifier */
    evpn_rd_t rd;              /* Route Distinguisher */
    evpn_rt_t rt_import[EVPN_MAX_ROUTE_TARGETS];
    evpn_rt_t rt_export[EVPN_MAX_ROUTE_TARGETS];
    int rt_import_count;
    int rt_export_count;
    
    /* MAC table (linked to VXLAN MAC table) */
    void *mac_table;           /* Pointer to VXLAN MAC table */
    
    /* Local MAC entries */
    int local_mac_count;
    
    /* Remote MAC entries learned via EVPN */
    int remote_mac_count;
    
    pthread_mutex_t lock;
} evpn_mac_vrf_t;

/**
 * BGP Session State
 */
typedef enum {
    BGP_STATE_IDLE = 0,
    BGP_STATE_CONNECT,
    BGP_STATE_ACTIVE,
    BGP_STATE_OPENSENT,
    BGP_STATE_OPENCONFIRM,
    BGP_STATE_ESTABLISHED
} bgp_state_t;

/**
 * BGP Peer
 */
typedef struct {
    uint32_t peer_ip;          /* Peer IP address */
    uint32_t peer_asn;         /* Peer AS number */
    uint16_t peer_port;        /* Peer port (179) */
    
    /* Local configuration (for OPEN messages) */
    uint32_t local_asn;        /* Local AS number */
    uint32_t router_id;        /* Local router ID */
    
    bgp_state_t state;         /* BGP FSM state */
    int sockfd;                /* Socket descriptor */
    
    time_t last_keepalive;     /* Last keepalive received */
    time_t last_update;        /* Last update received */
    
    /* Statistics */
    uint64_t msg_sent;
    uint64_t msg_received;
    uint64_t updates_sent;
    uint64_t updates_received;
    uint64_t keepalives_sent;
    uint64_t keepalives_received;
    
    /* BGP connection (embedded) */
    bgp_connection_t connection;
    
    pthread_t thread;          /* BGP peer thread */
    bool running;
} evpn_bgp_peer_t;

/**
 * Route Information Base (RIB) Entry
 */
typedef struct evpn_rib_entry {
    evpn_route_type_t type;    /* Route type (1-5) */
    
    union {
        evpn_mac_ip_route_t mac_ip;
        evpn_inclusive_mcast_route_t inclusive_mcast;
        evpn_ethernet_segment_route_t es;
        evpn_ethernet_ad_route_t ad;
    } route;
    
    evpn_rt_t rt;              /* Route Target */
    uint32_t next_hop;         /* Next hop IP */
    uint32_t local_pref;       /* Local preference */
    
    time_t timestamp;          /* When route was learned */
    bool local;                /* Locally originated? */
    
    struct evpn_rib_entry *next; /* Linked list */
} evpn_rib_entry_t;

/**
 * EVPN Context (Main structure)
 */
typedef struct evpn_ctx {
    /* Link to VXLAN data plane */
    void *vxlan_ctx;           /* Pointer to vxlan_ctx_t */
    
    /* BGP Configuration */
    uint32_t local_asn;        /* Local AS number */
    uint32_t router_id;        /* BGP Router ID (typically VTEP IP) */
    evpn_bgp_peer_t *peers[8]; /* BGP peers (RR typically) */
    int peer_count;
    
    /* EVPN Configuration */
    uint32_t evi;              /* EVPN Instance ID */
    evpn_mac_vrf_t *mac_vrfs[256]; /* MAC-VRFs per VNI */
    int mac_vrf_count;
    
    /* Ethernet Segments (for multi-homing) */
    evpn_ethernet_segment_t *segments[EVPN_MAX_ETHERNET_SEGMENTS];
    evpn_ethernet_segment_t **ethernet_segments; /* Alias pointing to segments */
    int segment_count;
    pthread_mutex_t segment_lock;
    
    /* Route Information Base */
    evpn_rib_entry_t *rib;     /* RIB linked list */
    int rib_count;
    pthread_mutex_t rib_lock;
    
    /* Statistics */
    uint64_t routes_advertised;
    uint64_t routes_withdrawn;
    uint64_t routes_received;
    uint64_t mac_learned_bgp;
    uint64_t mac_moved;
    
    /* Control */
    bool running;
    pthread_t main_thread;
} evpn_ctx_t;

/* Function Prototypes */

/**
 * Initialize EVPN context
 * 
 * @param ctx       EVPN context
 * @param vxlan_ctx VXLAN context (for data plane integration)
 * @param local_asn Local AS number
 * @param router_id BGP router ID (typically VTEP IP)
 * @return          0 on success, -1 on error
 */
int evpn_init(evpn_ctx_t *ctx, void *vxlan_ctx, uint32_t local_asn, uint32_t router_id);

/**
 * Cleanup EVPN context
 */
void evpn_cleanup(evpn_ctx_t *ctx);

/**
 * Add BGP peer (Route Reflector)
 * 
 * @param ctx      EVPN context
 * @param peer_ip  Peer IP address
 * @param peer_asn Peer AS number
 * @return         0 on success, -1 on error
 */
int evpn_add_peer(evpn_ctx_t *ctx, uint32_t peer_ip, uint32_t peer_asn);

/**
 * Create MAC-VRF for VNI
 * 
 * @param ctx  EVPN context
 * @param vni  VXLAN Network Identifier
 * @param rd   Route Distinguisher
 * @param rt   Route Target
 * @return     0 on success, -1 on error
 */
int evpn_create_mac_vrf(evpn_ctx_t *ctx, uint32_t vni, evpn_rd_t *rd, evpn_rt_t *rt);

/**
 * Advertise local MAC/IP to BGP
 * 
 * @param ctx  EVPN context
 * @param mac  MAC address
 * @param ip   IP address (0 if MAC-only)
 * @param vni  VNI
 * @return     0 on success, -1 on error
 */
int evpn_advertise_mac_ip(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t ip, uint32_t vni);

/**
 * Withdraw MAC/IP route
 * 
 * @param ctx  EVPN context
 * @param mac  MAC address
 * @param vni  VNI
 * @return     0 on success, -1 on error
 */
int evpn_withdraw_mac_ip(evpn_ctx_t *ctx, const uint8_t *mac, uint32_t vni);

/**
 * Process received MAC/IP route from BGP
 * 
 * @param ctx       EVPN context
 * @param route     MAC/IP route
 * @param next_hop  Next hop IP
 * @param withdraw  Is this a withdrawal?
 * @return          0 on success, -1 on error
 */
int evpn_process_mac_ip_route(evpn_ctx_t *ctx, const evpn_mac_ip_route_t *route,
                              uint32_t next_hop, bool withdraw);

/**
 * Advertise Inclusive Multicast route (Type 3)
 * 
 * @param ctx  EVPN context
 * @param vni  VNI
 * @return     0 on success, -1 on error
 */
int evpn_advertise_inclusive_mcast(evpn_ctx_t *ctx, uint32_t vni);

/**
 * Create Ethernet Segment
 * 
 * @param ctx  EVPN context
 * @param esi  Ethernet Segment Identifier
 * @param mode Redundancy mode (single-active or all-active)
 * @return     0 on success, -1 on error
 */
int evpn_create_ethernet_segment(evpn_ctx_t *ctx, const evpn_esi_t *esi, 
                                  evpn_redundancy_mode_t mode);

/**
 * Advertise Ethernet Segment route (Type 4)
 * 
 * @param ctx  EVPN context
 * @param esi  Ethernet Segment Identifier
 * @return     0 on success, -1 on error
 */
int evpn_advertise_ethernet_segment(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Perform DF election for Ethernet Segment
 * 
 * @param ctx  EVPN context
 * @param esi  Ethernet Segment Identifier
 * @return     0 on success, -1 on error
 */
int evpn_df_election(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/**
 * Check if split-horizon should filter this route
 * 
 * @param ctx  EVPN context
 * @param esi  Ethernet Segment Identifier
 * @param src_ip Source PE IP
 * @return     true if should filter, false otherwise
 */
bool evpn_split_horizon_filter(evpn_ctx_t *ctx, const evpn_esi_t *esi, uint32_t src_ip);

/**
 * Get statistics
 * 
 * @param ctx   EVPN context
 * @param stats Output statistics
 */
void evpn_get_stats(evpn_ctx_t *ctx, uint64_t *routes_adv, uint64_t *routes_rcv,
                    uint64_t *mac_learned, uint64_t *mac_moved);

/**
 * Dump RIB contents (debugging)
 * 
 * @param ctx  EVPN context
 */
void evpn_dump_rib(evpn_ctx_t *ctx);

/**
 * Add route to RIB
 * 
 * @param ctx   EVPN context
 * @param route RIB entry to add
 * @return      0 on success, -1 on error
 */
int evpn_rib_add(evpn_ctx_t *ctx, evpn_rib_entry_t *route);

/**
 * Remove route from RIB
 * 
 * @param ctx  EVPN context
 * @param type Route type
 * @param mac  MAC address (for Type 2)
 * @param vni  VNI
 * @return     0 on success, -1 on error
 */
int evpn_rib_remove(evpn_ctx_t *ctx, evpn_route_type_t type, const uint8_t *mac, uint32_t vni);

/**
 * Dump Ethernet Segments (debugging)
 * 
 * @param ctx  EVPN context
 * @param esi  ESI to dump (NULL for all)
 */
void evpn_dump_segments(evpn_ctx_t *ctx, const evpn_esi_t *esi);

/* Accessor macros for field name compatibility */
#define es_count segment_count
#define es_lock segment_lock

#endif /* EVPN_H */
