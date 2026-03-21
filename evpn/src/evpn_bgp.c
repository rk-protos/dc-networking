
/*****************************************************************************
 * Project     : BGP Protocol Implementation for EVPN (RFC 8365)
 * Description : 
 *                
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : evpn_bgp.c  
 * Purpose     : BGP Protocol Implementation for EVPN
 *               RFC 4271 - BGP-4
 *               RFC 4760 - Multiprotocol Extensions
 *               RFC 7432 - BGP MPLS-Based EVPN
 *****************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include "../include/evpn.h"
#include "../include/evpn_bgp.h"

/**
 * Build BGP message header
 */
int bgp_build_header(uint8_t *buf, uint8_t type, uint16_t msg_len) {
    bgp_header_t *hdr = (bgp_header_t *)buf;
    
    /* Marker: all 1s (16 bytes) */
    memset(hdr->marker, 0xFF, BGP_MARKER_SIZE);
    
    /* Length: header + message */
    hdr->length = htons(BGP_HEADER_SIZE + msg_len);
    
    /* Type */
    hdr->type = type;
    
    return BGP_HEADER_SIZE;
}

/**
 * Build EVPN capability for OPEN message
 */
int bgp_build_evpn_capability(uint8_t *buf) {
    int offset = 0;
    
    /* Optional Parameter: Capability */
    bgp_opt_param_t *opt = (bgp_opt_param_t *)buf;
    opt->type = BGP_OPT_PARAM_CAPABILITY;
    offset += sizeof(bgp_opt_param_t);
    
    /* Multiprotocol capability for EVPN */
    bgp_capability_t *cap = (bgp_capability_t *)(buf + offset);
    cap->code = BGP_CAP_MULTIPROTOCOL;
    cap->length = sizeof(bgp_mp_capability_t);
    offset += sizeof(bgp_capability_t);
    
    /* EVPN: AFI=25 (L2VPN), SAFI=70 (EVPN) */
    bgp_mp_capability_t *mp = (bgp_mp_capability_t *)(buf + offset);
    mp->afi = htons(BGP_AFI_L2VPN);
    mp->reserved = 0;
    mp->safi = BGP_SAFI_EVPN;
    offset += sizeof(bgp_mp_capability_t);
    
    /* Set optional parameter length */
    opt->length = offset - sizeof(bgp_opt_param_t);
    
    return offset;
}

/**
 * Send BGP OPEN message
 */
int bgp_send_open(bgp_connection_t *conn, uint32_t local_asn, uint32_t router_id) {
    uint8_t buf[512];
    int offset = BGP_HEADER_SIZE;
    
    /* Build capability for EVPN */
    uint8_t cap_buf[128];
    int cap_len = bgp_build_evpn_capability(cap_buf);
    
    /* Build OPEN message */
    bgp_open_msg_t *open = (bgp_open_msg_t *)(buf + offset);
    open->version = BGP_VERSION;
    open->my_asn = htons(local_asn & 0xFFFF);  /* 2-byte ASN for now */
    open->hold_time = htons(BGP_HOLD_TIME);
    open->bgp_identifier = htonl(router_id);
    open->opt_param_len = cap_len;
    
    offset += sizeof(bgp_open_msg_t);
    
    /* Copy capabilities */
    memcpy(buf + offset, cap_buf, cap_len);
    offset += cap_len;
    
    int msg_len = offset - BGP_HEADER_SIZE;
    
    /* Build header */
    bgp_build_header(buf, BGP_MSG_OPEN, msg_len);
    
    /* Send */
    ssize_t sent = send(conn->sockfd, buf, offset, 0);
    if (sent < 0) {
        perror("send OPEN");
        return -1;
    }
    
    if (sent != offset) {
        fprintf(stderr, "Incomplete OPEN send: %zd/%d\n", sent, offset);
        return -1;
    }
    
    conn->msg_sent++;
    
    struct in_addr addr;
    addr.s_addr = conn->peer_ip;
    printf("BGP: Sent OPEN to %s (ASN %u, Hold Time %u, Router ID %u.%u.%u.%u)\n",
           inet_ntoa(addr), local_asn, BGP_HOLD_TIME,
           (router_id >> 24) & 0xFF, (router_id >> 16) & 0xFF,
           (router_id >> 8) & 0xFF, router_id & 0xFF);
    
    return 0;
}

/**
 * Send BGP KEEPALIVE message
 */
int bgp_send_keepalive(bgp_connection_t *conn) {
    uint8_t buf[BGP_HEADER_SIZE];
    
    /* KEEPALIVE has no data, just header */
    bgp_build_header(buf, BGP_MSG_KEEPALIVE, 0);
    
    ssize_t sent = send(conn->sockfd, buf, BGP_HEADER_SIZE, 0);
    if (sent < 0) {
        perror("send KEEPALIVE");
        return -1;
    }
    
    conn->msg_sent++;
    
    return 0;
}

/**
 * Send BGP NOTIFICATION message
 */
int bgp_send_notification(bgp_connection_t *conn, uint8_t err_code, uint8_t err_sub) {
    uint8_t buf[BGP_HEADER_SIZE + 2];
    
    /* Build NOTIFICATION */
    bgp_notification_msg_t *notif = (bgp_notification_msg_t *)(buf + BGP_HEADER_SIZE);
    notif->error_code = err_code;
    notif->error_subcode = err_sub;
    
    /* Build header */
    bgp_build_header(buf, BGP_MSG_NOTIFICATION, 2);
    
    /* Send */
    ssize_t sent = send(conn->sockfd, buf, BGP_HEADER_SIZE + 2, 0);
    if (sent < 0) {
        perror("send NOTIFICATION");
        return -1;
    }
    
    conn->msg_sent++;
    
    printf("BGP: Sent NOTIFICATION (Error %u/%u)\n", err_code, err_sub);
    
    return 0;
}

/**
 * Send BGP UPDATE message
 */
int bgp_send_update(bgp_connection_t *conn, const uint8_t *nlri, size_t nlri_len,
                   const uint8_t *attrs, size_t attr_len) {
    uint8_t buf[BGP_MAX_MESSAGE_SIZE];
    int offset = BGP_HEADER_SIZE;
    
    /* Withdrawn Routes Length (0 for now) */
    uint16_t *withdrawn_len = (uint16_t *)(buf + offset);
    *withdrawn_len = 0;
    offset += 2;
    
    /* Total Path Attribute Length */
    uint16_t *attr_len_ptr = (uint16_t *)(buf + offset);
    *attr_len_ptr = htons(attr_len);
    offset += 2;
    
    /* Path Attributes */
    if (attr_len > 0 && attrs) {
        memcpy(buf + offset, attrs, attr_len);
        offset += attr_len;
    }
    
    /* NLRI */
    if (nlri_len > 0 && nlri) {
        memcpy(buf + offset, nlri, nlri_len);
        offset += nlri_len;
    }
    
    int msg_len = offset - BGP_HEADER_SIZE;
    
    /* Build header */
    bgp_build_header(buf, BGP_MSG_UPDATE, msg_len);
    
    /* Send */
    ssize_t sent = send(conn->sockfd, buf, offset, 0);
    if (sent < 0) {
        perror("send UPDATE");
        return -1;
    }
    
    conn->msg_sent++;
    conn->updates_sent++;
    
    printf("BGP: Sent UPDATE (%zu bytes NLRI, %zu bytes attributes)\n", 
           nlri_len, attr_len);
    
    return 0;
}

/**
 * Receive BGP message
 */
int bgp_receive_message(bgp_connection_t *conn, uint8_t *msg_type, 
                       uint8_t **data, size_t *len) {
    /* Receive header if needed */
    if (conn->rcv_len < BGP_HEADER_SIZE) {
        ssize_t rcvd = recv(conn->sockfd, 
                           conn->rcv_buf + conn->rcv_len,
                           BGP_HEADER_SIZE - conn->rcv_len,
                           MSG_DONTWAIT);
        
        if (rcvd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 1; /* No data available */
            }
            perror("recv header");
            return -1;
        }
        
        if (rcvd == 0) {
            /* Connection closed */
            return -1;
        }
        
        conn->rcv_len += rcvd;
        
        if (conn->rcv_len < BGP_HEADER_SIZE) {
            return 1; /* Incomplete header */
        }
    }
    
    /* Parse header */
    bgp_header_t *hdr = (bgp_header_t *)conn->rcv_buf;
    uint16_t msg_len = ntohs(hdr->length);
    
    /* Validate marker */
    uint8_t marker[16];
    memset(marker, 0xFF, 16);
    if (memcmp(hdr->marker, marker, 16) != 0) {
        fprintf(stderr, "BGP: Invalid marker\n");
        return -1;
    }
    
    /* Validate length */
    if (msg_len < BGP_HEADER_SIZE || msg_len > BGP_MAX_MESSAGE_SIZE) {
        fprintf(stderr, "BGP: Invalid length %u\n", msg_len);
        return -1;
    }
    
    /* Receive full message if needed */
    if (conn->rcv_len < msg_len) {
        ssize_t rcvd = recv(conn->sockfd,
                           conn->rcv_buf + conn->rcv_len,
                           msg_len - conn->rcv_len,
                           MSG_DONTWAIT);
        
        if (rcvd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 1; /* Incomplete */
            }
            perror("recv message");
            return -1;
        }
        
        if (rcvd == 0) {
            return -1; /* Connection closed */
        }
        
        conn->rcv_len += rcvd;
        
        if (conn->rcv_len < msg_len) {
            return 1; /* Still incomplete */
        }
    }
    
    /* Message complete */
    *msg_type = hdr->type;
    *data = conn->rcv_buf + BGP_HEADER_SIZE;
    *len = msg_len - BGP_HEADER_SIZE;
    
    /* Reset buffer for next message */
    conn->rcv_len = 0;
    conn->msg_received++;
    
    return 0;
}

/**
 * Process BGP OPEN message
 */
int bgp_process_open(bgp_connection_t *conn, const uint8_t *data, size_t len) {
    if (len < sizeof(bgp_open_msg_t)) {
        fprintf(stderr, "BGP OPEN too short: %zu\n", len);
        return -1;
    }
    
    const bgp_open_msg_t *open = (const bgp_open_msg_t *)data;
    
    /* Validate version */
    if (open->version != BGP_VERSION) {
        fprintf(stderr, "BGP: Unsupported version %u\n", open->version);
        bgp_send_notification(conn, BGP_ERROR_OPEN_MESSAGE, 1);
        return -1;
    }
    
    /* Extract parameters */
    uint16_t peer_asn = ntohs(open->my_asn);
    uint16_t hold_time = ntohs(open->hold_time);
    uint32_t bgp_id = ntohl(open->bgp_identifier);
    
    struct in_addr addr;
    addr.s_addr = htonl(bgp_id);
    printf("BGP: Received OPEN from peer\n");
    printf("  ASN: %u\n", peer_asn);
    printf("  Hold Time: %u\n", hold_time);
    printf("  BGP ID: %s\n", inet_ntoa(addr));
    
    /* TODO: Parse capabilities */
    
    return 0;
}

/**
 * Process BGP KEEPALIVE message
 */
int bgp_process_keepalive(bgp_connection_t *conn) {
    time_t now = time(NULL);
    conn->last_keepalive = now;
    
    /* Silently accept - keepalives are expected */
    return 0;
}

/**
 * Process BGP NOTIFICATION message
 */
int bgp_process_notification(bgp_connection_t *conn, const uint8_t *data, size_t len) {
    if (len < 2) {
        fprintf(stderr, "BGP NOTIFICATION too short\n");
        return -1;
    }
    
    const bgp_notification_msg_t *notif = (const bgp_notification_msg_t *)data;
    
    printf("BGP: Received NOTIFICATION from peer\n");
    printf("  Error Code: %u\n", notif->error_code);
    printf("  Error Subcode: %u\n", notif->error_subcode);
    
    /* Connection will be closed */
    return -1;
}

/**
 * Parse path attributes from UPDATE message
 */
int bgp_parse_path_attributes(const uint8_t *data, size_t len,
                              void (*callback)(uint8_t type, const uint8_t *value, 
                                              size_t value_len, void *user),
                              void *user) {
    size_t offset = 0;
    
    while (offset < len) {
        if (offset + 3 > len) {
            fprintf(stderr, "Path attribute too short\n");
            return -1;
        }
        
        const bgp_path_attr_t *attr = (const bgp_path_attr_t *)(data + offset);
        uint8_t flags = attr->flags;
        uint8_t type = attr->type;
        
        offset += 2;  /* flags + type */
        
        /* Length */
        uint16_t attr_len;
        if (flags & BGP_ATTR_FLAG_EXTENDED) {
            if (offset + 2 > len) return -1;
            attr_len = ntohs(*(uint16_t *)(data + offset));
            offset += 2;
        } else {
            if (offset + 1 > len) return -1;
            attr_len = data[offset];
            offset += 1;
        }
        
        /* Value */
        if (offset + attr_len > len) {
            fprintf(stderr, "Path attribute value too long\n");
            return -1;
        }
        
        const uint8_t *value = data + offset;
        
        /* Callback */
        if (callback) {
            callback(type, value, attr_len, user);
        }
        
        offset += attr_len;
    }
    
    return 0;
}

/**
 * Initialize BGP connection
 */
int bgp_connection_init(bgp_connection_t *conn, uint32_t peer_ip, 
                       uint32_t peer_asn, uint32_t local_asn, 
                       uint32_t router_id) {
    if (!conn) {
        return -1;
    }
    
    memset(conn, 0, sizeof(bgp_connection_t));
    
    conn->peer_ip = peer_ip;
    conn->peer_asn = peer_asn;
    conn->peer_port = BGP_PORT;
    conn->state = BGP_STATE_IDLE;
    conn->sockfd = -1;
    conn->running = false;
    
    /* Store local info (used for OPEN message) */
    conn->local_asn = local_asn;
    conn->router_id = router_id;
    
    return 0;
}

/**
 * Connect to BGP peer
 */
int bgp_connect(bgp_connection_t *conn) {
    if (!conn) {
        return -1;
    }
    
    /* Create socket */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }
    
    /* Set socket options */
    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    
    /* Set non-blocking */
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    
    /* Connect */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(conn->peer_port);
    addr.sin_addr.s_addr = conn->peer_ip;
    
    struct in_addr peer_addr;
    peer_addr.s_addr = conn->peer_ip;
    printf("BGP: Connecting to %s:%u...\n", inet_ntoa(peer_addr), conn->peer_port);
    
    int ret = connect(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        if (errno != EINPROGRESS) {
            perror("connect");
            close(sockfd);
            return -1;
        }
        /* Connection in progress - will complete asynchronously */
    }
    
    conn->sockfd = sockfd;
    
    return 0;
}

/**
 * Cleanup BGP connection
 */
void bgp_connection_cleanup(bgp_connection_t *conn) {
    if (!conn) {
        return;
    }
    
    if (conn->sockfd >= 0) {
        close(conn->sockfd);
        conn->sockfd = -1;
    }
    
    conn->state = BGP_STATE_IDLE;
    conn->running = false;
}

/**
 * Get BGP state name
 */
const char *bgp_state_name(int state) {
    switch (state) {
        case BGP_STATE_IDLE: return "IDLE";
        case BGP_STATE_CONNECT: return "CONNECT";
        case BGP_STATE_ACTIVE: return "ACTIVE";
        case BGP_STATE_OPENSENT: return "OPENSENT";
        case BGP_STATE_OPENCONFIRM: return "OPENCONFIRM";
        case BGP_STATE_ESTABLISHED: return "ESTABLISHED";
        default: return "UNKNOWN";
    }
}
