
/*****************************************************************************
 * Project     : EVPN Protocol Implementation (RFC 8365)
 * Description : Part of minimal implementation of EVPN
 *                BGP Protocol Implementation for EVPN 
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : evpn_bgp.h  
 * Purpose     : This implementation provides  
 *                Minimal BGP-4 implementation focused on EVPN requirements
 *                RFC 4271 - A Border Gateway Protocol 4 (BGP-4)
 *                RFC 4760 - Multiprotocol Extensions for BGP-4
 *****************************************************************************/

#ifndef EVPN_BGP_H
#define EVPN_BGP_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <sys/socket.h>
#include <pthread.h>

/* Forward declaration */
typedef struct evpn_ctx evpn_ctx_t;

/* BGP Message Types */
#define BGP_MSG_OPEN            1
#define BGP_MSG_UPDATE          2
#define BGP_MSG_NOTIFICATION    3
#define BGP_MSG_KEEPALIVE       4

/* BGP Constants */
#define BGP_VERSION             4
#define BGP_PORT                179

/* BGP Error Codes */
#define BGP_ERROR_MESSAGE_HEADER    1
#define BGP_ERROR_OPEN_MESSAGE      2
#define BGP_ERROR_UPDATE_MESSAGE    3
#define BGP_ERROR_HOLD_TIMER        4
#define BGP_ERROR_FSM               5
#define BGP_ERROR_CEASE             6

/* BGP Header */
#define BGP_HEADER_SIZE         19
#define BGP_MARKER_SIZE         16
#define BGP_MAX_MESSAGE_SIZE    4096

/* BGP Timers */
#define BGP_CONNECT_RETRY_TIME  120
#define BGP_HOLD_TIME           180
#define BGP_KEEPALIVE_TIME      60

/* BGP Path Attributes */
#define BGP_ATTR_FLAG_OPTIONAL      0x80
#define BGP_ATTR_FLAG_TRANSITIVE    0x40
#define BGP_ATTR_FLAG_PARTIAL       0x20
#define BGP_ATTR_FLAG_EXTENDED      0x10

#define BGP_ATTR_ORIGIN             1
#define BGP_ATTR_AS_PATH            2
#define BGP_ATTR_NEXT_HOP           3
#define BGP_ATTR_MED                4
#define BGP_ATTR_LOCAL_PREF         5
#define BGP_ATTR_ATOMIC_AGGREGATE   6
#define BGP_ATTR_AGGREGATOR         7
#define BGP_ATTR_COMMUNITIES        8
#define BGP_ATTR_ORIGINATOR_ID      9
#define BGP_ATTR_CLUSTER_LIST       10
#define BGP_ATTR_MP_REACH_NLRI      14  /* RFC 4760 */
#define BGP_ATTR_MP_UNREACH_NLRI    15  /* RFC 4760 */
#define BGP_ATTR_EXTENDED_COMMUNITIES 16

/* AFI/SAFI for EVPN */
#define BGP_AFI_L2VPN               25
#define BGP_SAFI_EVPN               70

/**
 * BGP Message Header
 */
typedef struct {
    uint8_t marker[16];         /* All 1s */
    uint16_t length;            /* Total message length including header */
    uint8_t type;               /* Message type */
} __attribute__((packed)) bgp_header_t;

/**
 * BGP OPEN Message
 */
typedef struct {
    uint8_t version;            /* BGP version (4) */
    uint16_t my_asn;           /* My Autonomous System */
    uint16_t hold_time;        /* Hold time in seconds */
    uint32_t bgp_identifier;   /* BGP Identifier (router ID) */
    uint8_t opt_param_len;     /* Optional parameters length */
    /* Optional parameters follow */
} __attribute__((packed)) bgp_open_msg_t;

/**
 * BGP Optional Parameter
 */
typedef struct {
    uint8_t type;              /* Parameter type */
    uint8_t length;            /* Parameter length */
    /* Parameter value follows */
} __attribute__((packed)) bgp_opt_param_t;

/* Optional Parameter Types */
#define BGP_OPT_PARAM_CAPABILITY    2

/**
 * BGP Capability
 */
typedef struct {
    uint8_t code;              /* Capability code */
    uint8_t length;            /* Capability length */
    /* Capability value follows */
} __attribute__((packed)) bgp_capability_t;

/* Capability Codes */
#define BGP_CAP_MULTIPROTOCOL       1
#define BGP_CAP_ROUTE_REFRESH       2
#define BGP_CAP_4BYTE_ASN          65
#define BGP_CAP_EXTENDED_NEXTHOP    5

/**
 * Multiprotocol Capability (for EVPN)
 */
typedef struct {
    uint16_t afi;              /* Address Family Identifier */
    uint8_t reserved;          /* Reserved (must be 0) */
    uint8_t safi;              /* Subsequent Address Family Identifier */
} __attribute__((packed)) bgp_mp_capability_t;

/**
 * BGP NOTIFICATION Message
 */
typedef struct {
    uint8_t error_code;        /* Error code */
    uint8_t error_subcode;     /* Error subcode */
    /* Error data follows */
} __attribute__((packed)) bgp_notification_msg_t;

/**
 * BGP Path Attribute
 */
typedef struct {
    uint8_t flags;             /* Attribute flags */
    uint8_t type;              /* Attribute type code */
    /* Length (1 or 2 bytes based on extended flag) */
    /* Value follows */
} __attribute__((packed)) bgp_path_attr_t;

/**
 * MP_REACH_NLRI (for EVPN routes)
 */
typedef struct {
    uint16_t afi;              /* AFI = 25 (L2VPN) */
    uint8_t safi;              /* SAFI = 70 (EVPN) */
    uint8_t next_hop_len;      /* Next hop length */
    /* Next hop follows */
    uint8_t reserved;          /* Reserved */
    /* NLRI follows */
} __attribute__((packed)) bgp_mp_reach_nlri_t;

/**
 * MP_UNREACH_NLRI (for EVPN withdrawals)
 */
typedef struct {
    uint16_t afi;              /* AFI = 25 (L2VPN) */
    uint8_t safi;              /* SAFI = 70 (EVPN) */
    /* Withdrawn routes follow */
} __attribute__((packed)) bgp_mp_unreach_nlri_t;

/**
 * BGP Connection
 */
typedef struct {
    int sockfd;                /* Socket file descriptor */
    uint32_t peer_ip;          /* Peer IP address */
    uint16_t peer_port;        /* Peer port */
    uint32_t peer_asn;         /* Peer AS number */
    
    /* Local configuration */
    uint32_t local_asn;        /* Local AS number */
    uint32_t router_id;        /* Local router ID */
    
    /* State */
    int state;                 /* BGP FSM state */
    time_t connect_retry_timer;
    time_t hold_timer;
    time_t keepalive_timer;
    time_t last_keepalive;     /* Last keepalive received */
    time_t last_update;        /* Last update received */
    
    /* Receive buffer */
    uint8_t rcv_buf[BGP_MAX_MESSAGE_SIZE];
    size_t rcv_len;
    
    /* Send buffer */
    uint8_t snd_buf[BGP_MAX_MESSAGE_SIZE];
    size_t snd_len;
    
    /* Statistics */
    uint64_t msg_sent;
    uint64_t msg_received;
    uint64_t updates_sent;
    uint64_t updates_received;
    
    /* Thread */
    pthread_t thread;
    bool running;
} bgp_connection_t;

/* Function Prototypes */

/**
 * Initialize BGP connection
 * 
 * @param conn      BGP connection structure
 * @param peer_ip   Peer IP address
 * @param peer_asn  Peer AS number
 * @param local_asn Local AS number
 * @param router_id Local router ID
 * @return          0 on success, -1 on error
 */
int bgp_connection_init(bgp_connection_t *conn, uint32_t peer_ip, 
                       uint32_t peer_asn, uint32_t local_asn, 
                       uint32_t router_id);

/**
 * Connect to BGP peer
 * 
 * @param conn  BGP connection
 * @return      0 on success, -1 on error
 */
int bgp_connect(bgp_connection_t *conn);

/**
 * Send BGP OPEN message
 * 
 * @param conn      BGP connection
 * @param local_asn Local AS number
 * @param router_id Router ID
 * @return          0 on success, -1 on error
 */
int bgp_send_open(bgp_connection_t *conn, uint32_t local_asn, uint32_t router_id);

/**
 * Send BGP KEEPALIVE message
 * 
 * @param conn  BGP connection
 * @return      0 on success, -1 on error
 */
int bgp_send_keepalive(bgp_connection_t *conn);

/**
 * Send BGP UPDATE message with EVPN route
 * 
 * @param conn   BGP connection
 * @param nlri   NLRI data
 * @param len    NLRI length
 * @param attrs  Path attributes
 * @param attr_len Attributes length
 * @return       0 on success, -1 on error
 */
int bgp_send_update(bgp_connection_t *conn, const uint8_t *nlri, size_t len,
                   const uint8_t *attrs, size_t attr_len);

/**
 * Send BGP NOTIFICATION message
 * 
 * @param conn     BGP connection
 * @param err_code Error code
 * @param err_sub  Error subcode
 * @return         0 on success, -1 on error
 */
int bgp_send_notification(bgp_connection_t *conn, uint8_t err_code, uint8_t err_sub);

/**
 * Receive BGP message
 * 
 * @param conn     BGP connection
 * @param msg_type Output: message type
 * @param data     Output: message data
 * @param len      Output: data length
 * @return         0 on success, -1 on error, 1 on incomplete
 */
int bgp_receive_message(bgp_connection_t *conn, uint8_t *msg_type, 
                       uint8_t **data, size_t *len);

/**
 * Process BGP OPEN message
 * 
 * @param conn  BGP connection
 * @param data  Message data
 * @param len   Data length
 * @return      0 on success, -1 on error
 */
int bgp_process_open(bgp_connection_t *conn, const uint8_t *data, size_t len);

/**
 * Process BGP UPDATE message
 * 
 * @param conn  BGP connection
 * @param evpn  EVPN context
 * @param data  Message data
 * @param len   Data length
 * @return      0 on success, -1 on error
 */
int bgp_process_update(bgp_connection_t *conn, evpn_ctx_t *evpn,
                      const uint8_t *data, size_t len);

/**
 * Process BGP KEEPALIVE message
 * 
 * @param conn  BGP connection
 * @return      0 on success, -1 on error
 */
int bgp_process_keepalive(bgp_connection_t *conn);

/**
 * Process BGP NOTIFICATION message
 * 
 * @param conn  BGP connection
 * @param data  Message data
 * @param len   Data length
 * @return      0 on success, -1 on error
 */
int bgp_process_notification(bgp_connection_t *conn, const uint8_t *data, size_t len);

/**
 * BGP FSM (Finite State Machine) processing
 * 
 * @param conn  BGP connection
 * @param event Event type
 * @return      0 on success, -1 on error
 */
int bgp_fsm_process(bgp_connection_t *conn, int event);

/**
 * Build BGP message header
 * 
 * @param buf      Output buffer
 * @param type     Message type
 * @param msg_len  Message length (excluding header)
 * @return         Header size
 */
int bgp_build_header(uint8_t *buf, uint8_t type, uint16_t msg_len);

/**
 * Build EVPN capability for OPEN message
 * 
 * @param buf  Output buffer
 * @return     Capability size
 */
int bgp_build_evpn_capability(uint8_t *buf);

/**
 * Parse path attributes from UPDATE message
 * 
 * @param data     Attribute data
 * @param len      Data length
 * @param callback Callback for each attribute
 * @param user     User data for callback
 * @return         0 on success, -1 on error
 */
int bgp_parse_path_attributes(const uint8_t *data, size_t len,
                              void (*callback)(uint8_t type, const uint8_t *value, 
                                              size_t value_len, void *user),
                              void *user);

/**
 * Cleanup BGP connection
 * 
 * @param conn  BGP connection
 */
void bgp_connection_cleanup(bgp_connection_t *conn);

/**
 * Get BGP state name (for debugging)
 * 
 * @param state  BGP state
 * @return       State name string
 */
const char *bgp_state_name(int state);

#endif /* EVPN_BGP_H */
