
/*****************************************************************************
 * Project     : BGP Protocol Implementation for EVPN (RFC 8365)
 * Description : 
 *                
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : evpn_bgp_fsm.c  
 * Purpose     : BGP Finite State Machine
 *               RFC 4271 - RFC 4271 - BGP Finite State Machine
 *****************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include "../include/evpn.h"
#include "../include/evpn_bgp.h"

/* BGP FSM Events */
#define BGP_EVENT_START                 1
#define BGP_EVENT_STOP                  2
#define BGP_EVENT_TCP_CONNECTED         3
#define BGP_EVENT_TCP_FAILED            4
#define BGP_EVENT_OPEN_RECEIVED         5
#define BGP_EVENT_KEEPALIVE_RECEIVED    6
#define BGP_EVENT_UPDATE_RECEIVED       7
#define BGP_EVENT_NOTIFICATION_RECEIVED 8
#define BGP_EVENT_HOLD_TIMER_EXPIRED    9
#define BGP_EVENT_KEEPALIVE_TIMER_EXPIRED 10

/**
 * BGP FSM Process
 */
int bgp_fsm_process(bgp_connection_t *conn, int event) {
    if (!conn) {
        return -1;
    }
    
    int old_state = conn->state;
    int new_state = old_state;
    
    switch (conn->state) {
        case BGP_STATE_IDLE:
            if (event == BGP_EVENT_START) {
                /* Initiate TCP connection */
                printf("BGP FSM: Starting connection...\n");
                if (bgp_connect(conn) == 0) {
                    new_state = BGP_STATE_CONNECT;
                    conn->connect_retry_timer = time(NULL) + BGP_CONNECT_RETRY_TIME;
                } else {
                    fprintf(stderr, "BGP FSM: Connection failed\n");
                }
            }
            break;
            
        case BGP_STATE_CONNECT:
            if (event == BGP_EVENT_TCP_CONNECTED) {
                /* Send OPEN message */
                printf("BGP FSM: TCP connected, sending OPEN...\n");
                if (bgp_send_open(conn, conn->local_asn, conn->router_id) == 0) {
                    new_state = BGP_STATE_OPENSENT;
                    conn->hold_timer = time(NULL) + BGP_HOLD_TIME;
                } else {
                    fprintf(stderr, "BGP FSM: Failed to send OPEN\n");
                    new_state = BGP_STATE_IDLE;
                }
            } else if (event == BGP_EVENT_TCP_FAILED) {
                fprintf(stderr, "BGP FSM: TCP connection failed\n");
                new_state = BGP_STATE_IDLE;
            }
            break;
            
        case BGP_STATE_OPENSENT:
            if (event == BGP_EVENT_OPEN_RECEIVED) {
                /* Send KEEPALIVE */
                printf("BGP FSM: OPEN received, sending KEEPALIVE...\n");
                if (bgp_send_keepalive(conn) == 0) {
                    new_state = BGP_STATE_OPENCONFIRM;
                    conn->keepalive_timer = time(NULL) + BGP_KEEPALIVE_TIME;
                } else {
                    fprintf(stderr, "BGP FSM: Failed to send KEEPALIVE\n");
                    new_state = BGP_STATE_IDLE;
                }
            } else if (event == BGP_EVENT_NOTIFICATION_RECEIVED) {
                fprintf(stderr, "BGP FSM: NOTIFICATION received in OPENSENT\n");
                new_state = BGP_STATE_IDLE;
            } else if (event == BGP_EVENT_HOLD_TIMER_EXPIRED) {
                fprintf(stderr, "BGP FSM: Hold timer expired in OPENSENT\n");
                bgp_send_notification(conn, BGP_ERROR_HOLD_TIMER, 0);
                new_state = BGP_STATE_IDLE;
            }
            break;
            
        case BGP_STATE_OPENCONFIRM:
            if (event == BGP_EVENT_KEEPALIVE_RECEIVED) {
                /* Session established! */
                printf("BGP FSM: KEEPALIVE received - Session ESTABLISHED!\n");
                new_state = BGP_STATE_ESTABLISHED;
                conn->keepalive_timer = time(NULL) + BGP_KEEPALIVE_TIME;
                conn->hold_timer = time(NULL) + BGP_HOLD_TIME;
            } else if (event == BGP_EVENT_NOTIFICATION_RECEIVED) {
                fprintf(stderr, "BGP FSM: NOTIFICATION received in OPENCONFIRM\n");
                new_state = BGP_STATE_IDLE;
            } else if (event == BGP_EVENT_HOLD_TIMER_EXPIRED) {
                fprintf(stderr, "BGP FSM: Hold timer expired in OPENCONFIRM\n");
                bgp_send_notification(conn, BGP_ERROR_HOLD_TIMER, 0);
                new_state = BGP_STATE_IDLE;
            }
            break;
            
        case BGP_STATE_ESTABLISHED:
            if (event == BGP_EVENT_KEEPALIVE_TIMER_EXPIRED) {
                /* Send KEEPALIVE */
                if (bgp_send_keepalive(conn) == 0) {
                    conn->keepalive_timer = time(NULL) + BGP_KEEPALIVE_TIME;
                } else {
                    fprintf(stderr, "BGP FSM: Failed to send KEEPALIVE\n");
                    new_state = BGP_STATE_IDLE;
                }
            } else if (event == BGP_EVENT_KEEPALIVE_RECEIVED) {
                /* Reset hold timer */
                conn->last_keepalive = time(NULL);
                conn->hold_timer = time(NULL) + BGP_HOLD_TIME;
            } else if (event == BGP_EVENT_UPDATE_RECEIVED) {
                /* Reset hold timer */
                conn->last_update = time(NULL);
                conn->hold_timer = time(NULL) + BGP_HOLD_TIME;
                conn->updates_received++;
            } else if (event == BGP_EVENT_NOTIFICATION_RECEIVED) {
                fprintf(stderr, "BGP FSM: NOTIFICATION received in ESTABLISHED\n");
                new_state = BGP_STATE_IDLE;
            } else if (event == BGP_EVENT_HOLD_TIMER_EXPIRED) {
                fprintf(stderr, "BGP FSM: Hold timer expired in ESTABLISHED\n");
                bgp_send_notification(conn, BGP_ERROR_HOLD_TIMER, 0);
                new_state = BGP_STATE_IDLE;
            }
            break;
            
        default:
            fprintf(stderr, "BGP FSM: Unknown state %d\n", conn->state);
            new_state = BGP_STATE_IDLE;
            break;
    }
    
    /* State transition */
    if (new_state != old_state) {
        printf("BGP FSM: %s -> %s (event %d)\n",
               bgp_state_name(old_state),
               bgp_state_name(new_state),
               event);
        conn->state = new_state;
        
        /* Cleanup on transition to IDLE */
        if (new_state == BGP_STATE_IDLE && conn->sockfd >= 0) {
            close(conn->sockfd);
            conn->sockfd = -1;
        }
    }
    
    return 0;
}

/**
 * BGP peer thread - main event loop
 */
static void *bgp_peer_thread(void *arg) {
    bgp_connection_t *conn = (bgp_connection_t *)arg;
    
    struct in_addr addr;
    addr.s_addr = conn->peer_ip;
    printf("BGP: Peer thread started for %s\n", inet_ntoa(addr));
    
    /* Start BGP FSM */
    bgp_fsm_process(conn, BGP_EVENT_START);
    
    /* Wait for TCP connection to complete */
    if (conn->state == BGP_STATE_CONNECT && conn->sockfd >= 0) {
        struct pollfd pfd;
        pfd.fd = conn->sockfd;
        pfd.events = POLLOUT;
        
        int ret = poll(&pfd, 1, 10000);  /* 10 second timeout */
        if (ret > 0 && (pfd.revents & POLLOUT)) {
            /* Check if connection succeeded */
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(conn->sockfd, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0) {
                printf("BGP: TCP connection successful\n");
                bgp_fsm_process(conn, BGP_EVENT_TCP_CONNECTED);
            } else {
                fprintf(stderr, "BGP: TCP connection failed: %s\n", strerror(error));
                bgp_fsm_process(conn, BGP_EVENT_TCP_FAILED);
            }
        } else {
            fprintf(stderr, "BGP: TCP connection timeout\n");
            bgp_fsm_process(conn, BGP_EVENT_TCP_FAILED);
        }
    }
    
    /* Main message processing loop */
    while (conn->running && conn->state != BGP_STATE_IDLE) {
        /* Check for incoming messages */
        if (conn->sockfd >= 0) {
            uint8_t msg_type;
            uint8_t *data;
            size_t len;
            
            int ret = bgp_receive_message(conn, &msg_type, &data, &len);
            if (ret == 0) {
                /* Message received - process it */
                switch (msg_type) {
                    case BGP_MSG_OPEN:
                        if (bgp_process_open(conn, data, len) == 0) {
                            bgp_fsm_process(conn, BGP_EVENT_OPEN_RECEIVED);
                        } else {
                            bgp_send_notification(conn, BGP_ERROR_OPEN_MESSAGE, 0);
                            bgp_fsm_process(conn, BGP_EVENT_NOTIFICATION_RECEIVED);
                        }
                        break;
                        
                    case BGP_MSG_KEEPALIVE:
                        bgp_process_keepalive(conn);
                        bgp_fsm_process(conn, BGP_EVENT_KEEPALIVE_RECEIVED);
                        break;
                        
                    case BGP_MSG_UPDATE:
                        /* Process UPDATE (Week 2 will implement full processing) */
                        printf("BGP: Received UPDATE message (%zu bytes)\n", len);
                        bgp_fsm_process(conn, BGP_EVENT_UPDATE_RECEIVED);
                        break;
                        
                    case BGP_MSG_NOTIFICATION:
                        bgp_process_notification(conn, data, len);
                        bgp_fsm_process(conn, BGP_EVENT_NOTIFICATION_RECEIVED);
                        break;
                        
                    default:
                        fprintf(stderr, "BGP: Unknown message type %u\n", msg_type);
                        break;
                }
            } else if (ret < 0) {
                /* Error - connection closed or failed */
                fprintf(stderr, "BGP: Connection error\n");
                bgp_fsm_process(conn, BGP_EVENT_NOTIFICATION_RECEIVED);
                break;
            }
            /* ret == 1 means incomplete message, continue */
        }
        
        /* Check timers */
        time_t now = time(NULL);
        
        if (conn->state == BGP_STATE_ESTABLISHED) {
            /* Check keepalive timer */
            if (now >= conn->keepalive_timer) {
                bgp_fsm_process(conn, BGP_EVENT_KEEPALIVE_TIMER_EXPIRED);
            }
            
            /* Check hold timer */
            if (now >= conn->hold_timer) {
                fprintf(stderr, "BGP: Hold timer expired (no messages from peer)\n");
                bgp_fsm_process(conn, BGP_EVENT_HOLD_TIMER_EXPIRED);
                break;
            }
        } else if (conn->state == BGP_STATE_OPENSENT || conn->state == BGP_STATE_OPENCONFIRM) {
            /* Check hold timer in these states too */
            if (now >= conn->hold_timer) {
                fprintf(stderr, "BGP: Hold timer expired in state %s\n", 
                       bgp_state_name(conn->state));
                bgp_fsm_process(conn, BGP_EVENT_HOLD_TIMER_EXPIRED);
                break;
            }
        }
        
        /* Sleep briefly to avoid busy loop */
        usleep(100000);  /* 100ms */
    }
    
    printf("BGP: Peer thread stopped (final state: %s)\n", bgp_state_name(conn->state));
    
    /* Cleanup */
    if (conn->sockfd >= 0) {
        close(conn->sockfd);
        conn->sockfd = -1;
    }
    
    conn->running = false;
    conn->state = BGP_STATE_IDLE;
    
    return NULL;
}

/**
 * Start BGP peer session
 */
int bgp_peer_start(evpn_bgp_peer_t *peer) {
    if (!peer) {
        return -1;
    }
    
    if (peer->running) {
        fprintf(stderr, "BGP peer already running\n");
        return -1;
    }
    
    /* Initialize connection with local ASN and router ID from peer */
    bgp_connection_t *conn = &peer->connection;
    memset(conn, 0, sizeof(bgp_connection_t));
    
    conn->peer_ip = peer->peer_ip;
    conn->peer_asn = peer->peer_asn;
    conn->peer_port = peer->peer_port;
    conn->state = BGP_STATE_IDLE;
    conn->sockfd = -1;
    conn->running = true;
    
    /* Store local info for OPEN message */
    conn->local_asn = peer->local_asn;
    conn->router_id = peer->router_id;
    
    /* Start peer thread */
    if (pthread_create(&peer->thread, NULL, bgp_peer_thread, conn) != 0) {
        perror("pthread_create bgp_peer_thread");
        conn->running = false;
        return -1;
    }
    
    peer->running = true;
    
    struct in_addr addr;
    addr.s_addr = peer->peer_ip;
    printf("BGP: Started peer session with %s (ASN %u)\n", 
           inet_ntoa(addr), peer->peer_asn);
    
    return 0;
}

/**
 * Stop BGP peer session
 */
int bgp_peer_stop(evpn_bgp_peer_t *peer) {
    if (!peer || !peer->running) {
        return -1;
    }
    
    printf("BGP: Stopping peer session...\n");
    
    /* Signal thread to stop */
    peer->connection.running = false;
    peer->running = false;
    
    /* Send NOTIFICATION if connected */
    if (peer->connection.state == BGP_STATE_ESTABLISHED &&
        peer->connection.sockfd >= 0) {
        bgp_send_notification(&peer->connection, BGP_ERROR_CEASE, 0);
    }
    
    /* Wait for thread to finish */
    pthread_join(peer->thread, NULL);
    
    /* Cleanup connection */
    if (peer->connection.sockfd >= 0) {
        close(peer->connection.sockfd);
        peer->connection.sockfd = -1;
    }
    
    peer->connection.state = BGP_STATE_IDLE;
    
    printf("BGP: Peer session stopped\n");
    
    return 0;
}
