
/*****************************************************************************
 * Project     : EVPN Protocol Implementation (RFC 8365)
 * Description : BGP Session Establishment demo
 *                
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : bgp_session_demo.c  
 * Purpose     : Tests EVPN BGP session establishment with a Route Reflector
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include "../include/evpn.h"
#include "../include/evpn_bgp.h"

#define ANSI_GREEN  "\033[32m"
#define ANSI_RED    "\033[31m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_BLUE   "\033[34m"
#define ANSI_RESET  "\033[0m"

static volatile sig_atomic_t keep_running = 1;

void sigint_handler(int sig) {
    (void)sig;
    keep_running = 0;
    printf("\nReceived interrupt, shutting down...\n");
}

void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\nOptions:\n");
    printf("  -p <peer-ip>    BGP peer (Route Reflector) IP address (default: 127.0.0.1)\n");
    printf("  -a <asn>        Local AS number (default: 65000)\n");
    printf("  -r <router-id>  BGP Router ID (default: 10.0.0.1)\n");
    printf("  -t <seconds>    Test duration in seconds (default: 30)\n");
    printf("  -h              Show this help\n");
    printf("\nExample:\n");
    printf("  %s -p 192.168.1.254 -a 65000 -r 10.0.0.1 -t 60\n", prog);
}

int main(int argc, char *argv[]) {
    /* Default parameters */
    uint32_t peer_ip = inet_addr("127.0.0.1");
    uint32_t local_asn = 65000;
    uint32_t router_id = inet_addr("10.0.0.1");
    int test_duration = 30;  /* seconds */
    
    /* Parse command line arguments */
    int opt;
    while ((opt = getopt(argc, argv, "p:a:r:t:h")) != -1) {
        switch (opt) {
            case 'p':
                peer_ip = inet_addr(optarg);
                if (peer_ip == INADDR_NONE) {
                    fprintf(stderr, "Invalid peer IP: %s\n", optarg);
                    return 1;
                }
                break;
            case 'a':
                local_asn = atoi(optarg);
                break;
            case 'r':
                router_id = inet_addr(optarg);
                if (router_id == INADDR_NONE) {
                    fprintf(stderr, "Invalid router ID: %s\n", optarg);
                    return 1;
                }
                break;
            case 't':
                test_duration = atoi(optarg);
                if (test_duration <= 0) {
                    fprintf(stderr, "Invalid test duration: %s\n", optarg);
                    return 1;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    /* Set up signal handler */
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
    
    printf("\n");
    printf(ANSI_BLUE);
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║          EVPN BGP Session Establishment Test                 ║\n");
    printf("║                   RFC 8365 Week 1                             ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf(ANSI_RESET "\n");
    
    /* Display configuration */
    struct in_addr addr;
    
    addr.s_addr = peer_ip;
    printf("Configuration:\n");
    printf("  BGP Peer (RR): %s\n", inet_ntoa(addr));
    printf("  Local ASN: %u\n", local_asn);
    
    addr.s_addr = router_id;
    printf("  Router ID: %s\n", inet_ntoa(addr));
    printf("  Test Duration: %d seconds\n", test_duration);
    printf("\n");
    
    /* Initialize EVPN */
    evpn_ctx_t evpn;
    
    printf(ANSI_YELLOW "Step 1:" ANSI_RESET " Initializing EVPN context...\n");
    if (evpn_init(&evpn, NULL, local_asn, router_id) != 0) {
        fprintf(stderr, ANSI_RED "✗ FAIL:" ANSI_RESET " EVPN initialization failed\n");
        return 1;
    }
    printf(ANSI_GREEN "  ✓ EVPN context initialized\n" ANSI_RESET);
    printf("\n");
    
    /* Add BGP peer */
    printf(ANSI_YELLOW "Step 2:" ANSI_RESET " Adding BGP peer (Route Reflector)...\n");
    if (evpn_add_peer(&evpn, peer_ip, local_asn) != 0) {
        fprintf(stderr, ANSI_RED "✗ FAIL:" ANSI_RESET " Failed to add BGP peer\n");
        evpn_cleanup(&evpn);
        return 1;
    }
    printf(ANSI_GREEN "  ✓ BGP peer added successfully\n" ANSI_RESET);
    printf("\n");
    
    /* Start BGP session */
    printf(ANSI_YELLOW "Step 3:" ANSI_RESET " Starting BGP session...\n");
    if (evpn.peer_count > 0 && evpn.peers[0]) {
        /* Store local ASN and router ID in peer for BGP OPEN message */
        evpn.peers[0]->local_asn = local_asn;
        evpn.peers[0]->router_id = router_id;
        
        if (bgp_peer_start(evpn.peers[0]) != 0) {
            fprintf(stderr, ANSI_RED "✗ FAIL:" ANSI_RESET " Failed to start BGP peer\n");
            evpn_cleanup(&evpn);
            return 1;
        }
        printf(ANSI_GREEN "  ✓ BGP peer thread started\n" ANSI_RESET);
    } else {
        fprintf(stderr, ANSI_RED "✗ FAIL:" ANSI_RESET " No BGP peer configured\n");
        evpn_cleanup(&evpn);
        return 1;
    }
    printf("\n");
    
    /* Wait for session establishment */
    printf(ANSI_YELLOW "Step 4:" ANSI_RESET " Waiting for BGP session establishment...\n");
    printf("  (Maximum %d seconds)\n", test_duration);
    printf("  ");
    fflush(stdout);
    
    bool established = false;
    for (int i = 0; i < test_duration && keep_running; i++) {
        sleep(1);
        printf(".");
        fflush(stdout);
        
        if (evpn.peers[0] && evpn.peers[0]->state == BGP_STATE_ESTABLISHED) {
            established = true;
            printf("\n\n");
            break;
        }
    }
    
    printf("\n\n");
    
    /* Check result */
    if (established) {
        printf(ANSI_GREEN "╔═══════════════════════════════════════════════════════════════╗\n");
        printf("║                  ✓ TEST PASSED                                ║\n");
        printf("║           BGP Session ESTABLISHED Successfully!               ║\n");
        printf("╚═══════════════════════════════════════════════════════════════╝\n");
        printf(ANSI_RESET "\n");
        
        /* Display statistics */
        printf("BGP Session Statistics:\n");
        printf("  State: %s\n", bgp_state_name(evpn.peers[0]->state));
        printf("  Messages Sent: %lu\n", evpn.peers[0]->msg_sent);
        printf("  Messages Received: %lu\n", evpn.peers[0]->msg_received);
        printf("  Updates Sent: %lu\n", evpn.peers[0]->updates_sent);
        printf("  Updates Received: %lu\n", evpn.peers[0]->updates_received);
        printf("  Keepalives Sent: %lu\n", evpn.peers[0]->keepalives_sent);
        printf("  Keepalives Received: %lu\n", evpn.peers[0]->keepalives_received);
        
        /* Get EVPN statistics */
        uint64_t routes_adv, routes_rcv, mac_learned, mac_moved;
        evpn_get_stats(&evpn, &routes_adv, &routes_rcv, &mac_learned, &mac_moved);
        
        printf("\nEVPN Statistics:\n");
        printf("  Routes Advertised: %lu\n", routes_adv);
        printf("  Routes Received: %lu\n", routes_rcv);
        printf("  MACs Learned (BGP): %lu\n", mac_learned);
        printf("  MAC Mobility Events: %lu\n", mac_moved);
        
        /* Get RIB statistics */
        int total, type2, type3;
        evpn_rib_get_stats(&evpn, &total, &type2, &type3);
        
        printf("\nRIB Statistics:\n");
        printf("  Total Routes: %d\n", total);
        printf("  Type 2 (MAC/IP): %d\n", type2);
        printf("  Type 3 (Inclusive Mcast): %d\n", type3);
        
        /* Keep session running for a bit */
        printf("\nMaintaining session for 10 more seconds...\n");
        for (int i = 0; i < 10 && keep_running; i++) {
            sleep(1);
            printf(".");
            fflush(stdout);
        }
        printf("\n");
        
    } else {
        printf(ANSI_RED "╔═══════════════════════════════════════════════════════════════╗\n");
        printf("║                  ✗ TEST FAILED                                ║\n");
        printf("║          BGP Session NOT Established                          ║\n");
        printf("╚═══════════════════════════════════════════════════════════════╝\n");
        printf(ANSI_RESET "\n");
        
        if (evpn.peers[0]) {
            printf("Final BGP State: %s\n", bgp_state_name(evpn.peers[0]->state));
            printf("Messages Sent: %lu\n", evpn.peers[0]->msg_sent);
            printf("Messages Received: %lu\n", evpn.peers[0]->msg_received);
        }
        
        printf("\nPossible Issues:\n");
        printf("  1. Route Reflector not reachable at %s\n", inet_ntoa((struct in_addr){.s_addr = peer_ip}));
        printf("  2. Firewall blocking TCP port 179\n");
        printf("  3. Route Reflector not configured for EVPN\n");
        printf("  4. AS number mismatch\n");
    }
    
    /* Cleanup */
    printf("\n" ANSI_YELLOW "Step 5:" ANSI_RESET " Cleaning up...\n");
    evpn_cleanup(&evpn);
    printf(ANSI_GREEN "  ✓ Cleanup complete\n" ANSI_RESET);
    printf("\n");
    
    return established ? 0 : 1;
}
