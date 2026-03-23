
/*****************************************************************************
 * Project     : EVPN Protocol Implementation (RFC 8365)
 * Description : Aliasing Support Demo (WEEK 4 Feature 3)
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : aliasing_demo.c  
 * Purpose     : Demonstrates aliasing and per-flow load balancing
 *               RFC 7432 Section 8.4 - Aliasing and Backup Path
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "../include/evpn.h"
#include "../include/evpn_multihoming.h"

#define ANSI_GREEN  "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_BLUE   "\033[34m"
#define ANSI_CYAN   "\033[36m"
#define ANSI_RESET  "\033[0m"

void print_separator() {
    printf("\n═══════════════════════════════════════════════════════════════\n");
}

int main() {
    printf("\n" ANSI_BLUE);
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║          EVPN Aliasing Support Demo (Week 4.3)               ║\n");
    printf("║  RFC 7432 Section 8.4 - Multiple Paths & Load Balancing      ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf(ANSI_RESET);
    
    print_separator();
    
    // Initialize
    evpn_ctx_t evpn;
    evpn_init(&evpn, NULL, 65000, inet_addr("10.0.0.1"));
    printf(ANSI_GREEN "✓" ANSI_RESET " EVPN initialized\n");
    
    // Create ES
    evpn_esi_t esi;
    uint8_t mac[6] = {0x00, 0xaa, 0xbb, 0xcc, 0xdd, 0xee};
    evpn_generate_esi_type0(mac, 1, &esi);
    evpn_create_ethernet_segment(&evpn, &esi, EVPN_REDUNDANCY_ALL_ACTIVE);
    
    // Add PEs
    evpn_es_add_pe(&evpn, &esi, inet_addr("10.0.0.1"));
    evpn_es_add_pe(&evpn, &esi, inet_addr("10.0.0.2"));
    evpn_es_add_pe(&evpn, &esi, inet_addr("10.0.0.3"));
    
    print_separator();
    printf(ANSI_YELLOW "Enabling Aliasing" ANSI_RESET "\n");
    printf("─────────────────────────────────────────────\n");
    evpn_enable_aliasing(&evpn, &esi);
    
    print_separator();
    printf(ANSI_YELLOW "Testing MAC Aliasing" ANSI_RESET "\n");
    printf("─────────────────────────────────────────────\n\n");
    
    // Test MAC: 00:11:22:33:44:02 (even = aliased)
    uint8_t test_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x02};
    
    printf("Test MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           test_mac[0], test_mac[1], test_mac[2], test_mac[3], test_mac[4], test_mac[5]);
    
    if (evpn_is_mac_aliased(&evpn, test_mac, 1000)) {
        printf(ANSI_GREEN "→ MAC is ALIASED" ANSI_RESET " (multiple paths available)\n\n");
        
        uint32_t pe_list[16];
        int count = 16;
        evpn_get_aliased_pes(&evpn, test_mac, 1000, pe_list, &count);
        
        printf("Available paths (%d):\n", count);
        for (int i = 0; i < count; i++) {
            struct in_addr addr;
            addr.s_addr = pe_list[i];
            printf("  Path %d: via PE %s\n", i+1, inet_ntoa(addr));
        }
    }
    
    print_separator();
    printf(ANSI_YELLOW "Per-Flow Load Balancing" ANSI_RESET "\n");
    printf("─────────────────────────────────────────────\n\n");
    
    printf("Simulating 10 flows:\n");
    uint8_t packet[64];
    for (int flow = 0; flow < 10; flow++) {
        // Different flows (simulated by changing packet)
        packet[0] = flow;
        packet[1] = flow * 2;
        
        uint32_t flow_hash = evpn_compute_flow_hash(packet, sizeof(packet));
        uint32_t selected_pe;
        
        if (evpn_alias_select_pe(&evpn, test_mac, 1000, flow_hash, &selected_pe) == 0) {
            struct in_addr addr;
            addr.s_addr = selected_pe;
            printf("  Flow %2d (hash=%08x) → PE %s\n", flow, flow_hash, inet_ntoa(addr));
        }
    }
    
    print_separator();
    printf(ANSI_YELLOW "Benefits of Aliasing" ANSI_RESET "\n");
    printf("─────────────────────────────────────────────\n\n");
    
    printf(ANSI_CYAN "Advantages:" ANSI_RESET "\n");
    printf("  ✓ Multiple paths to same destination\n");
    printf("  ✓ Per-flow load balancing (ECMP-like)\n");
    printf("  ✓ Better bandwidth utilization\n");
    printf("  ✓ Enhanced redundancy\n");
    printf("  ✓ Automatic failover if one path fails\n");
    
    printf("\n" ANSI_CYAN "Use Cases:" ANSI_RESET "\n");
    printf("  • High-bandwidth applications\n");
    printf("  • Elephant flow distribution\n");
    printf("  • Load balancing without LAG\n");
    printf("  • Enhanced availability\n");
    
    print_separator();
    printf(ANSI_GREEN "\n✓ Aliasing Demo Complete!\n" ANSI_RESET);
    
    evpn_cleanup(&evpn);
    return 0;
}
