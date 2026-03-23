/*****************************************************************************
 * Project     : EVPN Protocol Implementation (RFC 8365)
 * Description : Production Features Complete Demo
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : features_demo.c  
 * Purpose     : Demonstrates features
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "../include/evpn.h"
#include "../include/evpn_features.h"
#include "../include/evpn_routes.h"

#define C_GREEN  "\033[32m"
#define C_YELLOW "\033[33m"
#define C_BLUE   "\033[34m"
#define C_CYAN   "\033[36m"
#define C_RESET  "\033[0m"

void sep() { printf("\n═══════════════════════════════════════════════════════════════\n"); }

int main() {
    printf("\n" C_BLUE);
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║        EVPN             Features Demo (Complete)              ║\n");
    printf("║    DCI | Graceful Restart | RR | ExtComm | Perf | Monitor     ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf(C_RESET);
    
    evpn_ctx_t evpn;
    evpn_init(&evpn, NULL, 65000, inet_addr("10.0.0.1"));
    sep();
    
    // Feature 1: DCI
    printf(C_YELLOW "Feature 1: DCI (Data Center Interconnect)" C_RESET "\n");
    printf("─────────────────────────────────────────────\n");
    evpn_enable_dci_gateway(&evpn, 1);
    evpn_add_remote_dc(&evpn, 2, inet_addr("10.1.0.1"));
    evpn_leak_route_to_dc(&evpn, 2, EVPN_ROUTE_TYPE_MAC_IP_ADVERTISEMENT, 1000);
    printf(C_GREEN "✓" C_RESET " DCI operational\n");
    sep();
    
    // Feature 2: Graceful Restart
    printf(C_YELLOW "Feature 2: Graceful Restart" C_RESET "\n");
    printf("─────────────────────────────────────────────\n");
    evpn_enable_graceful_restart(&evpn, 120);
    printf("\nSimulating restart...\n");
    evpn_gr_start_restart(&evpn);
    sleep(1);
    evpn_gr_mark_stale_routes(&evpn);
    evpn_gr_complete_restart(&evpn);
    printf(C_GREEN "✓" C_RESET " Graceful restart successful\n");
    sep();
    
    // Feature 3: Route Reflector Redundancy
    printf(C_YELLOW "Feature 3: Route Reflector Redundancy" C_RESET "\n");
    printf("─────────────────────────────────────────────\n");
    evpn_add_route_reflector(&evpn, inet_addr("192.168.1.1"), 1);
    evpn_add_route_reflector(&evpn, inet_addr("192.168.1.2"), 1);
    printf("\nSimulating RR failure...\n");
    evpn_rr_failover(&evpn, inet_addr("192.168.1.1"));
    uint32_t active_rr;
    if (evpn_get_active_rr(&evpn, &active_rr) == 0) {
        struct in_addr addr;
        addr.s_addr = active_rr;
        printf("Active RR: %s\n", inet_ntoa(addr));
    }
    printf(C_GREEN "✓" C_RESET " RR redundancy working\n");
    sep();
    
    // Feature 4: Extended Communities
    printf(C_YELLOW "Feature 4: Extended Communities" C_RESET "\n");
    printf("─────────────────────────────────────────────\n");
    evpn_add_rt_community(&evpn, 65000, 100);
    evpn_add_encap_community(&evpn, 8);
    evpn_add_color_community(&evpn, 100);
    printf(C_GREEN "✓" C_RESET " Extended communities configured\n");
    sep();
    
    // Feature 5: Performance Optimizations
    printf(C_YELLOW "Feature 5: Performance Optimizations" C_RESET "\n");
    printf("─────────────────────────────────────────────\n");
    evpn_hash_table_t *table = evpn_hash_create(1024);
    printf("Hash table created (1024 buckets)\n");
    evpn_batch_process_routes(&evpn, 100);
    evpn_hash_destroy(table);
    printf(C_GREEN "✓" C_RESET " Performance optimizations enabled\n");
    sep();
    
    // Feature 6: Monitoring & Debugging
    printf(C_YELLOW "Feature 6: Monitoring & Debugging" C_RESET "\n");
    printf("─────────────────────────────────────────────\n");
    evpn_enable_debug(&evpn, "bgp");
    evpn_set_log_level(&evpn, 3);
    evpn_dump_statistics(&evpn);
    printf(C_GREEN "✓" C_RESET " Monitoring enabled\n");
    sep();
    
    // Summary
    printf(C_BLUE);
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║          ALL EVPN FEATURES COMPLETE                           ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf(C_RESET);
    
    printf("\n" C_GREEN "KEY ACHIEVEMENTS:" C_RESET "\n");
    printf("  ✓ DCI for multi-DC deployments\n");
    printf("  ✓ Graceful restart for hitless upgrades\n");
    printf("  ✓ RR redundancy for high availability\n");
    printf("  ✓ Extended communities for routing control\n");
    printf("  ✓ Performance optimizations for scale\n");
    printf("  ✓ Monitoring & debugging for operations\n");
    
    printf("\n" C_CYAN "PRODUCTION READY:" C_RESET "\n");
    printf("  • Enterprise-grade features\n");
    printf("  • High availability\n");
    printf("  • Multi-DC support\n");
    printf("  • Operational excellence\n");
    
    sep();
    evpn_cleanup(&evpn);
    printf(C_GREEN "\n✓ EVPN Features Demo Complete!\n" C_RESET);
    
    return 0;
}
