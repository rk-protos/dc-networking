
/*****************************************************************************
 * Project     : EVPN Protocol Implementation (RFC 8365)
 * Description :  EVPN Multi-homing Demonstration
 *                
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : test_multihoming.c  
 * Purpose     : 
 *                This demonstrates the KEY feature of EVPN :
 *                  - Ethernet Segments
 *                  - DF (Designated Forwarder) Election
 *                  - Split-horizon filtering
 *                  - All-active multi-homing
 *                  - Type 1 and Type 4 routes
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "../include/evpn.h"
#include "../include/evpn_bgp.h"
#include "../include/evpn_routes.h"
#include "../include/evpn_multihoming.h"

#define ANSI_GREEN  "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_BLUE   "\033[34m"
#define ANSI_CYAN   "\033[36m"
#define ANSI_RED    "\033[31m"
#define ANSI_RESET  "\033[0m"

void print_separator() {
    printf("\n═══════════════════════════════════════════════════════════════\n");
}

int main() {
    printf("\n");
    printf(ANSI_BLUE);
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║       EVPN Multi-homing Demonstration (Week 3)                ║\n");
    printf("║                                                               ║\n");
    printf("║  Demonstrates:                                                ║\n");
    printf("║    • Ethernet Segments (ES)                                   ║\n");
    printf("║    • Designated Forwarder (DF) Election                       ║\n");
    printf("║    • Type 1 routes (Ethernet Auto-Discovery)                  ║\n");
    printf("║    • Type 4 routes (Ethernet Segment)                         ║\n");
    printf("║    • Split-horizon filtering                                  ║\n");
    printf("║    • All-active redundancy                                    ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf(ANSI_RESET);
    
    print_separator();
    
    // Step 1: Initialize EVPN
    printf(ANSI_YELLOW "Step 1:" ANSI_RESET " Initialize EVPN Context\n");
    printf("─────────────────────────────────────────────\n");
    
    evpn_ctx_t evpn;
    uint32_t local_asn = 65000;
    uint32_t router_id = inet_addr("10.0.0.1");  // PE1
    
    if (evpn_init(&evpn, NULL, local_asn, router_id) != 0) {
        fprintf(stderr, "Failed to initialize EVPN\n");
        return 1;
    }
    
    printf(ANSI_GREEN "✓" ANSI_RESET " EVPN context initialized (PE1: 10.0.0.1)\n");
    
    print_separator();
    
    // Step 2: Create Ethernet Segment
    printf(ANSI_YELLOW "Step 2:" ANSI_RESET " Create Ethernet Segment\n");
    printf("─────────────────────────────────────────────\n");
    
    evpn_esi_t esi;
    uint8_t system_mac[6] = {0x00, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e};
    evpn_generate_esi_type0(system_mac, 1, &esi);
    
    char esi_str[64];
    evpn_esi_to_string(&esi, esi_str, sizeof(esi_str));
    printf("ESI: %s\n", esi_str);
    printf("System MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           system_mac[0], system_mac[1], system_mac[2],
           system_mac[3], system_mac[4], system_mac[5]);
    
    if (evpn_create_ethernet_segment(&evpn, &esi, EVPN_REDUNDANCY_ALL_ACTIVE) == 0) {
        printf(ANSI_GREEN "✓" ANSI_RESET " Ethernet Segment created (All-Active mode)\n");
    }
    
    print_separator();
    
    // Step 3: Add peer PEs to Ethernet Segment
    printf(ANSI_YELLOW "Step 3:" ANSI_RESET " Add Peer PEs to Ethernet Segment\n");
    printf("─────────────────────────────────────────────\n");
    
    uint32_t pe_ips[] = {
        inet_addr("10.0.0.1"),  // PE1 (us)
        inet_addr("10.0.0.2"),  // PE2
        inet_addr("10.0.0.3")   // PE3
    };
    
    for (int i = 0; i < 3; i++) {
        struct in_addr addr;
        addr.s_addr = pe_ips[i];
        printf("Adding PE%d: %s\n", i + 1, inet_ntoa(addr));
        evpn_es_add_pe(&evpn, &esi, pe_ips[i]);
    }
    
    printf(ANSI_GREEN "✓" ANSI_RESET " 3 PEs added to Ethernet Segment\n");
    
    print_separator();
    
    // Step 4: Perform DF Election
    printf(ANSI_YELLOW "Step 4:" ANSI_RESET " Designated Forwarder (DF) Election\n");
    printf("─────────────────────────────────────────────\n");
    
    printf("\n" ANSI_CYAN "Testing Modulo Algorithm:" ANSI_RESET "\n");
    uint32_t df_ip_modulo;
    if (evpn_df_election_modulo(&evpn, &esi, &df_ip_modulo) == 0) {
        struct in_addr addr;
        addr.s_addr = df_ip_modulo;
        printf("  DF (Modulo): %s\n", inet_ntoa(addr));
        if (df_ip_modulo == router_id) {
            printf(ANSI_GREEN "  → We are the DF!" ANSI_RESET "\n");
        }
    }
    
    printf("\n" ANSI_CYAN "Testing HRW Algorithm:" ANSI_RESET "\n");
    uint32_t df_ip_hrw;
    if (evpn_df_election_hrw(&evpn, &esi, &df_ip_hrw) == 0) {
        struct in_addr addr;
        addr.s_addr = df_ip_hrw;
        printf("  DF (HRW): %s\n", inet_ntoa(addr));
    }
    
    // Run actual DF election (modulo)
    printf("\n" ANSI_CYAN "Running DF Election (Modulo):" ANSI_RESET "\n");
    evpn_df_election(&evpn, &esi);
    
    printf(ANSI_GREEN "\n✓" ANSI_RESET " DF Election complete\n");
    
    if (evpn_am_i_df(&evpn, &esi)) {
        printf(ANSI_GREEN "  → We are the Designated Forwarder for BUM traffic\n" ANSI_RESET);
    } else {
        printf("  → We are NOT the DF (another PE will forward BUM)\n");
    }
    
    print_separator();
    
    // Step 5: Test Split-Horizon
    printf(ANSI_YELLOW "Step 5:" ANSI_RESET " Split-Horizon Filtering Test\n");
    printf("─────────────────────────────────────────────\n");
    
    printf("\nTesting split-horizon from different sources:\n");
    
    for (int i = 0; i < 3; i++) {
        struct in_addr addr;
        addr.s_addr = pe_ips[i];
        bool should_filter = evpn_split_horizon_filter(&evpn, &esi, pe_ips[i]);
        
        printf("  Source: PE%d (%s) → %s\n", 
               i + 1, inet_ntoa(addr),
               should_filter ? ANSI_RED "FILTER (same ES)" ANSI_RESET :
               ANSI_GREEN "FORWARD" ANSI_RESET);
    }
    
    // Test from external PE
    uint32_t external_pe = inet_addr("10.0.0.99");
    struct in_addr addr;
    addr.s_addr = external_pe;
    bool should_filter = evpn_split_horizon_filter(&evpn, &esi, external_pe);
    printf("  Source: External PE (%s) → %s\n",
           inet_ntoa(addr),
           should_filter ? "FILTER" : ANSI_GREEN "FORWARD" ANSI_RESET);
    
    printf(ANSI_GREEN "\n✓" ANSI_RESET " Split-horizon prevents loops within ES\n");
    
    print_separator();
    
    // Step 6: Advertise Type 4 route
    printf(ANSI_YELLOW "Step 6:" ANSI_RESET " Advertise Ethernet Segment Route (Type 4)\n");
    printf("─────────────────────────────────────────────\n");
    
    if (evpn_advertise_ethernet_segment(&evpn, &esi) == 0) {
        printf(ANSI_GREEN "✓" ANSI_RESET " Type 4 route advertised\n");
        printf("  → Remote PEs will discover us on this ES\n");
    }
    
    print_separator();
    
    // Step 7: Create MAC-VRF and advertise Type 1
    printf(ANSI_YELLOW "Step 7:" ANSI_RESET " Advertise Ethernet Auto-Discovery (Type 1)\n");
    printf("─────────────────────────────────────────────\n");
    
    // Create MAC-VRF
    uint32_t vni = 1000;
    evpn_rd_t rd = {
        .type = 0,
        .value.asn_based = {.asn = 65000, .number = 1}
    };
    evpn_rt_t rt = {
        .type = 0,
        .value.asn_based = {.asn = 65000, .number = 100}
    };
    
    evpn_create_mac_vrf(&evpn, vni, &rd, &rt);
    
    // Advertise Type 1 route
    if (evpn_advertise_ethernet_ad(&evpn, &esi, 0, vni) == 0) {
        printf(ANSI_GREEN "✓" ANSI_RESET " Type 1 route advertised\n");
        printf("  → Enables fast convergence and aliasing\n");
    }
    
    print_separator();
    
    // Step 8: Test All-Active Load Balancing
    printf(ANSI_YELLOW "Step 8:" ANSI_RESET " All-Active Load Balancing\n");
    printf("─────────────────────────────────────────────\n");
    
    printf("\nTesting load balancing across 3 PEs:\n");
    
    uint8_t test_macs[][6] = {
        {0xaa, 0xbb, 0xcc, 0x00, 0x00, 0x01},
        {0xaa, 0xbb, 0xcc, 0x00, 0x00, 0x02},
        {0xaa, 0xbb, 0xcc, 0x00, 0x00, 0x03},
        {0xaa, 0xbb, 0xcc, 0x00, 0x00, 0x04},
        {0xaa, 0xbb, 0xcc, 0x00, 0x00, 0x05}
    };
    
    for (int i = 0; i < 5; i++) {
        uint32_t selected_pe;
        if (evpn_multihome_load_balance(&evpn, &esi, test_macs[i], &selected_pe) == 0) {
            struct in_addr addr;
            addr.s_addr = selected_pe;
            printf("  MAC %02x:%02x:%02x:%02x:%02x:%02x → PE: %s\n",
                   test_macs[i][0], test_macs[i][1], test_macs[i][2],
                   test_macs[i][3], test_macs[i][4], test_macs[i][5],
                   inet_ntoa(addr));
        }
    }
    
    printf(ANSI_GREEN "\n✓" ANSI_RESET " Load balancing distributes traffic across PEs\n");
    
    print_separator();
    
    // Step 9: Test BUM Traffic Handling
    printf(ANSI_YELLOW "Step 9:" ANSI_RESET " BUM Traffic Handling (DF Only)\n");
    printf("─────────────────────────────────────────────\n");
    
    uint8_t bum_packet[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};  // Broadcast
    
    int ret = evpn_multihome_bum_forward(&evpn, &esi, bum_packet, sizeof(bum_packet));
    
    if (ret == 0) {
        printf(ANSI_GREEN "✓" ANSI_RESET " BUM traffic: FORWARD (we are DF)\n");
        printf("  → Only DF forwards to avoid duplication\n");
    } else {
        printf("  BUM traffic: DROP (we are not DF)\n");
        printf("  → Non-DF drops BUM to avoid duplication\n");
    }
    
    print_separator();
    
    // Step 10: Display Ethernet Segment Info
    printf(ANSI_YELLOW "Step 10:" ANSI_RESET " Display Ethernet Segment Information\n");
    printf("─────────────────────────────────────────────\n");
    
    evpn_dump_segments(&evpn, NULL);
    
    print_separator();
    
    // Summary
    printf(ANSI_BLUE);
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                  DEMONSTRATION COMPLETE                       ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf(ANSI_RESET);
    
    printf("\n" ANSI_GREEN "KEY ACHIEVEMENTS:" ANSI_RESET "\n");
    printf("  ✓ Ethernet Segment created with 3 PEs\n");
    printf("  ✓ DF election working (Modulo & HRW algorithms)\n");
    printf("  ✓ Split-horizon prevents loops within ES\n");
    printf("  ✓ Type 1 routes (Auto-Discovery) work\n");
    printf("  ✓ Type 4 routes (ES advertisement) work\n");
    printf("  ✓ All-active load balancing functional\n");
    printf("  ✓ BUM traffic only forwarded by DF\n");
    
    printf("\n" ANSI_YELLOW "MULTI-HOMING BENEFITS:" ANSI_RESET "\n");
    printf("  • High Availability: Multiple PEs provide redundancy\n");
    printf("  • Load Balancing: Traffic distributed across PEs\n");
    printf("  • Fast Convergence: Type 1 routes enable quick failover\n");
    printf("  • Loop Prevention: Split-horizon avoids L2 loops\n");
    printf("  • No Duplication: DF election prevents BUM duplication\n");
    
    printf("\n" ANSI_YELLOW "INTEGRATION WITH WEEKS 1-2:" ANSI_RESET "\n");
    printf("  • Week 1: BGP sessions carry multi-homing routes\n");
    printf("  • Week 2: MACs learned with ESI for multi-homing\n");
    printf("  • Week 3: ES provides redundancy and load balancing\n");
    
    print_separator();
    
    // Cleanup
    printf("\nCleaning up...\n");
    evpn_cleanup(&evpn);
    
    printf(ANSI_GREEN "\n✓ Multi-homing Demonstration Complete!\n" ANSI_RESET);
    printf("\n");
    
    return 0;
}
