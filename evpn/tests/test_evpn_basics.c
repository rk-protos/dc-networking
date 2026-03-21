
/*****************************************************************************
 * Project     : EVPN Protocol Implementation (RFC 8365)
 * Description :  Basic EVPN Unit Tests
 *                
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : test_evpn_basics.c  
 * Purpose     : 
 *                This demonstrates the KEY feature of EVPN :
 *                  Tests core EVPN functionality without requiring 
 *                      network connectivity
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include "../include/evpn.h"
#include "../include/evpn_bgp.h"

#define ANSI_GREEN  "\033[32m"
#define ANSI_RED    "\033[31m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_RESET  "\033[0m"

static int tests_passed = 0;
static int tests_failed = 0;

void test_pass(const char *test_name) {
    printf(ANSI_GREEN "  ✓ PASS:" ANSI_RESET " %s\n", test_name);
    tests_passed++;
}

void test_fail(const char *test_name, const char *reason) {
    printf(ANSI_RED "  ✗ FAIL:" ANSI_RESET " %s - %s\n", test_name, reason);
    tests_failed++;
}

/* Test 1: EVPN Context Initialization */
void test_evpn_init() {
    printf("\n" ANSI_YELLOW "Test 1:" ANSI_RESET " EVPN Context Initialization\n");
    
    evpn_ctx_t ctx;
    uint32_t local_asn = 65000;
    uint32_t router_id = inet_addr("10.0.0.1");
    
    int ret = evpn_init(&ctx, NULL, local_asn, router_id);
    
    if (ret == 0 && 
        ctx.local_asn == local_asn && 
        ctx.router_id == router_id &&
        ctx.peer_count == 0 &&
        ctx.running == true) {
        test_pass("EVPN context initialized correctly");
    } else {
        test_fail("EVPN context initialization", "Values not set correctly");
    }
    
    evpn_cleanup(&ctx);
}

/* Test 2: BGP Peer Addition */
void test_bgp_peer_add() {
    printf("\n" ANSI_YELLOW "Test 2:" ANSI_RESET " BGP Peer Addition\n");
    
    evpn_ctx_t ctx;
    evpn_init(&ctx, NULL, 65000, inet_addr("10.0.0.1"));
    
    uint32_t peer_ip = inet_addr("10.0.0.254");
    uint32_t peer_asn = 65000;
    
    int ret = evpn_add_peer(&ctx, peer_ip, peer_asn);
    
    if (ret == 0 && ctx.peer_count == 1 && ctx.peers[0] != NULL) {
        if (ctx.peers[0]->peer_ip == peer_ip &&
            ctx.peers[0]->peer_asn == peer_asn) {
            test_pass("BGP peer added successfully");
        } else {
            test_fail("BGP peer addition", "Peer parameters incorrect");
        }
    } else {
        test_fail("BGP peer addition", "Failed to add peer");
    }
    
    evpn_cleanup(&ctx);
}

/* Test 3: MAC-VRF Creation */
void test_mac_vrf_creation() {
    printf("\n" ANSI_YELLOW "Test 3:" ANSI_RESET " MAC-VRF Creation\n");
    
    evpn_ctx_t ctx;
    evpn_init(&ctx, NULL, 65000, inet_addr("10.0.0.1"));
    
    uint32_t vni = 1000;
    evpn_rd_t rd = {
        .type = 0,
        .value.asn_based = {.asn = 65000, .number = 1}
    };
    evpn_rt_t rt = {
        .type = 0,
        .value.asn_based = {.asn = 65000, .number = 100}
    };
    
    int ret = evpn_create_mac_vrf(&ctx, vni, &rd, &rt);
    
    if (ret == 0 && ctx.mac_vrf_count == 1 && ctx.mac_vrfs[0] != NULL) {
        if (ctx.mac_vrfs[0]->vni == vni) {
            test_pass("MAC-VRF created successfully");
        } else {
            test_fail("MAC-VRF creation", "VNI mismatch");
        }
    } else {
        test_fail("MAC-VRF creation", "Failed to create MAC-VRF");
    }
    
    evpn_cleanup(&ctx);
}

/* Test 4: BGP Header Construction */
void test_bgp_header() {
    printf("\n" ANSI_YELLOW "Test 4:" ANSI_RESET " BGP Header Construction\n");
    
    uint8_t buf[BGP_HEADER_SIZE];
    int ret = bgp_build_header(buf, BGP_MSG_KEEPALIVE, 0);
    
    if (ret == BGP_HEADER_SIZE) {
        bgp_header_t *hdr = (bgp_header_t *)buf;
        
        /* Check marker (all 1s) */
        bool marker_ok = true;
        for (int i = 0; i < 16; i++) {
            if (hdr->marker[i] != 0xFF) {
                marker_ok = false;
                break;
            }
        }
        
        /* Check length */
        uint16_t length = ntohs(hdr->length);
        
        /* Check type */
        if (marker_ok && length == BGP_HEADER_SIZE && hdr->type == BGP_MSG_KEEPALIVE) {
            test_pass("BGP header constructed correctly");
        } else {
            test_fail("BGP header construction", "Header fields incorrect");
        }
    } else {
        test_fail("BGP header construction", "Incorrect return value");
    }
}

/* Test 5: RIB Operations */
void test_rib_operations() {
    printf("\n" ANSI_YELLOW "Test 5:" ANSI_RESET " RIB Operations\n");
    
    evpn_ctx_t ctx;
    evpn_init(&ctx, NULL, 65000, inet_addr("10.0.0.1"));
    
    /* Create a test route */
    evpn_rib_entry_t route;
    memset(&route, 0, sizeof(route));
    route.type = EVPN_ROUTE_TYPE_MAC_IP_ADVERTISEMENT;
    
    uint8_t test_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    memcpy(route.route.mac_ip.mac, test_mac, 6);
    route.route.mac_ip.label1 = 1000;  /* VNI */
    route.route.mac_ip.ip = inet_addr("192.168.1.10");
    route.route.mac_ip.ip_len = 32;
    route.next_hop = inet_addr("10.0.0.2");
    route.local = false;
    
    /* Test add */
    int ret = evpn_rib_add(&ctx, &route);
    if (ret != 0 || ctx.rib_count != 1) {
        test_fail("RIB add operation", "Failed to add route");
        evpn_cleanup(&ctx);
        return;
    }
    
    /* Test lookup */
    evpn_rib_entry_t *found = evpn_rib_lookup_mac(&ctx, test_mac, 1000);
    if (found == NULL) {
        test_fail("RIB lookup operation", "Failed to find route");
        evpn_cleanup(&ctx);
        return;
    }
    
    if (memcmp(found->route.mac_ip.mac, test_mac, 6) != 0) {
        test_fail("RIB lookup operation", "MAC mismatch");
        evpn_cleanup(&ctx);
        return;
    }
    
    /* Test remove */
    ret = evpn_rib_remove(&ctx, EVPN_ROUTE_TYPE_MAC_IP_ADVERTISEMENT, test_mac, 1000);
    if (ret != 0 || ctx.rib_count != 0) {
        test_fail("RIB remove operation", "Failed to remove route");
        evpn_cleanup(&ctx);
        return;
    }
    
    test_pass("RIB add/lookup/remove operations");
    
    evpn_cleanup(&ctx);
}

/* Test 6: Multiple MAC-VRFs */
void test_multiple_mac_vrfs() {
    printf("\n" ANSI_YELLOW "Test 6:" ANSI_RESET " Multiple MAC-VRFs\n");
    
    evpn_ctx_t ctx;
    evpn_init(&ctx, NULL, 65000, inet_addr("10.0.0.1"));
    
    evpn_rd_t rd = {.type = 0, .value.asn_based = {.asn = 65000, .number = 1}};
    evpn_rt_t rt = {.type = 0, .value.asn_based = {.asn = 65000, .number = 100}};
    
    /* Create multiple MAC-VRFs */
    int success = 1;
    for (int i = 0; i < 5; i++) {
        uint32_t vni = 1000 + i;
        rd.value.asn_based.number = i + 1;
        rt.value.asn_based.number = 100 + i;
        
        if (evpn_create_mac_vrf(&ctx, vni, &rd, &rt) != 0) {
            success = 0;
            break;
        }
    }
    
    if (success && ctx.mac_vrf_count == 5) {
        /* Verify VNIs */
        bool all_correct = true;
        for (int i = 0; i < 5; i++) {
            if (ctx.mac_vrfs[i]->vni != (uint32_t)(1000 + i)) {
                all_correct = false;
                break;
            }
        }
        
        if (all_correct) {
            test_pass("Multiple MAC-VRFs created successfully");
        } else {
            test_fail("Multiple MAC-VRFs", "VNI mismatch");
        }
    } else {
        test_fail("Multiple MAC-VRFs", "Failed to create all MAC-VRFs");
    }
    
    evpn_cleanup(&ctx);
}

/* Test 7: BGP State Machine Names */
void test_bgp_state_names() {
    printf("\n" ANSI_YELLOW "Test 7:" ANSI_RESET " BGP State Names\n");
    
    const char *idle = bgp_state_name(BGP_STATE_IDLE);
    const char *connect = bgp_state_name(BGP_STATE_CONNECT);
    const char *established = bgp_state_name(BGP_STATE_ESTABLISHED);
    
    if (strcmp(idle, "IDLE") == 0 &&
        strcmp(connect, "CONNECT") == 0 &&
        strcmp(established, "ESTABLISHED") == 0) {
        test_pass("BGP state names correct");
    } else {
        test_fail("BGP state names", "Incorrect state name returned");
    }
}

/* Test 8: EVPN Statistics */
void test_evpn_statistics() {
    printf("\n" ANSI_YELLOW "Test 8:" ANSI_RESET " EVPN Statistics\n");
    
    evpn_ctx_t ctx;
    evpn_init(&ctx, NULL, 65000, inet_addr("10.0.0.1"));
    
    /* Set some statistics */
    ctx.routes_advertised = 10;
    ctx.routes_received = 20;
    ctx.mac_learned_bgp = 5;
    ctx.mac_moved = 2;
    
    /* Get statistics */
    uint64_t routes_adv, routes_rcv, mac_learned, mac_moved;
    evpn_get_stats(&ctx, &routes_adv, &routes_rcv, &mac_learned, &mac_moved);
    
    if (routes_adv == 10 && routes_rcv == 20 && mac_learned == 5 && mac_moved == 2) {
        test_pass("EVPN statistics correct");
    } else {
        test_fail("EVPN statistics", "Statistics mismatch");
    }
    
    evpn_cleanup(&ctx);
}

/* Test 9: RIB Statistics */
void test_rib_statistics() {
    printf("\n" ANSI_YELLOW "Test 9:" ANSI_RESET " RIB Statistics\n");
    
    evpn_ctx_t ctx;
    evpn_init(&ctx, NULL, 65000, inet_addr("10.0.0.1"));
    
    /* Add Type 2 routes */
    for (int i = 0; i < 3; i++) {
        evpn_rib_entry_t route;
        memset(&route, 0, sizeof(route));
        route.type = EVPN_ROUTE_TYPE_MAC_IP_ADVERTISEMENT;
        route.route.mac_ip.mac[5] = i;
        route.route.mac_ip.label1 = 1000;
        evpn_rib_add(&ctx, &route);
    }
    
    /* Add Type 3 routes */
    for (int i = 0; i < 2; i++) {
        evpn_rib_entry_t route;
        memset(&route, 0, sizeof(route));
        route.type = EVPN_ROUTE_TYPE_INCLUSIVE_MULTICAST;
        route.route.inclusive_mcast.vni = 1000 + i;
        evpn_rib_add(&ctx, &route);
    }
    
    /* Get statistics */
    int total, type2, type3;
    evpn_rib_get_stats(&ctx, &total, &type2, &type3);
    
    if (total == 5 && type2 == 3 && type3 == 2) {
        test_pass("RIB statistics correct");
    } else {
        test_fail("RIB statistics", "Statistics mismatch");
    }
    
    evpn_cleanup(&ctx);
}

/* Test 10: EVPN Capability Construction */
void test_evpn_capability() {
    printf("\n" ANSI_YELLOW "Test 10:" ANSI_RESET " EVPN Capability Construction\n");
    
    uint8_t buf[128];
    int len = bgp_build_evpn_capability(buf);
    
    if (len > 0) {
        /* Parse capability */
        bgp_opt_param_t *opt = (bgp_opt_param_t *)buf;
        
        if (opt->type == BGP_OPT_PARAM_CAPABILITY) {
            bgp_capability_t *cap = (bgp_capability_t *)(buf + sizeof(bgp_opt_param_t));
            
            if (cap->code == BGP_CAP_MULTIPROTOCOL) {
                bgp_mp_capability_t *mp = (bgp_mp_capability_t *)(buf + sizeof(bgp_opt_param_t) + sizeof(bgp_capability_t));
                
                uint16_t afi = ntohs(mp->afi);
                uint8_t safi = mp->safi;
                
                if (afi == BGP_AFI_L2VPN && safi == BGP_SAFI_EVPN) {
                    test_pass("EVPN capability constructed correctly");
                } else {
                    test_fail("EVPN capability", "AFI/SAFI incorrect");
                }
            } else {
                test_fail("EVPN capability", "Capability code incorrect");
            }
        } else {
            test_fail("EVPN capability", "Optional parameter type incorrect");
        }
    } else {
        test_fail("EVPN capability", "Failed to build capability");
    }
}

int main() {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║               EVPN Basic Unit Tests                           ║\n");
    printf("║                  RFC 8365 Week 1                              ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    
    /* Run all tests */
    test_evpn_init();
    test_bgp_peer_add();
    test_mac_vrf_creation();
    test_bgp_header();
    test_rib_operations();
    test_multiple_mac_vrfs();
    test_bgp_state_names();
    test_evpn_statistics();
    test_rib_statistics();
    test_evpn_capability();
    
    /* Print summary */
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    if (tests_failed == 0) {
        printf("║                    " ANSI_GREEN "ALL TESTS PASSED" ANSI_RESET "                         ║\n");
    } else {
        printf("║                    " ANSI_RED "SOME TESTS FAILED" ANSI_RESET "                        ║\n");
    }
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Results:\n");
    printf("  " ANSI_GREEN "Passed: %d" ANSI_RESET "\n", tests_passed);
    if (tests_failed > 0) {
        printf("  " ANSI_RED "Failed: %d" ANSI_RESET "\n", tests_failed);
    } else {
        printf("  Failed: 0\n");
    }
    printf("  Total:  %d\n", tests_passed + tests_failed);
    printf("\n");
    
    return tests_failed == 0 ? 0 : 1;
}
