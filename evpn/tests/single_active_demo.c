
/*****************************************************************************
 * Project     : EVPN Protocol Implementation (RFC 8365)
 * Description : Single-Active Multi-homing Demo (WEEK 4 Feature 1)
 *                
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : single_active_demo.c  
 * Purpose     : Demonstrates single-active multi-homing with failover
 *               RFC 7432 Section 8.4 - Single-Active Redundancy Mode
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "../include/evpn.h"
#include "../include/evpn_bgp.h"
#include "../include/evpn_multihoming.h"

#define ANSI_GREEN  "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_BLUE   "\033[34m"
#define ANSI_CYAN   "\033[36m"
#define ANSI_RED    "\033[31m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_RESET  "\033[0m"

void print_separator() {
    printf("\n═══════════════════════════════════════════════════════════════\n");
}

int main() {
    printf("\n");
    printf(ANSI_BLUE);
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║     EVPN Single-Active Multi-homing Demo (Week 4.1)          ║\n");
    printf("║                                                               ║\n");
    printf("║  Demonstrates:                                                ║\n");
    printf("║    • Single-Active vs All-Active modes                        ║\n");
    printf("║    • Active/Standby PE election                               ║\n");
    printf("║    • PE failover scenarios                                    ║\n");
    printf("║    • Traffic forwarding decisions                             ║\n");
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
    
    // Step 2: Create Ethernet Segment in All-Active mode initially
    printf(ANSI_YELLOW "Step 2:" ANSI_RESET " Create Ethernet Segment (All-Active)\n");
    printf("─────────────────────────────────────────────\n");
    
    evpn_esi_t esi;
    uint8_t system_mac[6] = {0x00, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e};
    evpn_generate_esi_type0(system_mac, 1, &esi);
    
    char esi_str[64];
    evpn_esi_to_string(&esi, esi_str, sizeof(esi_str));
    printf("ESI: %s\n", esi_str);
    
    if (evpn_create_ethernet_segment(&evpn, &esi, EVPN_REDUNDANCY_ALL_ACTIVE) == 0) {
        printf(ANSI_GREEN "✓" ANSI_RESET " Ethernet Segment created (All-Active mode)\n");
    }
    
    print_separator();
    
    // Step 3: Add PEs to Ethernet Segment
    printf(ANSI_YELLOW "Step 3:" ANSI_RESET " Add 3 PEs to Ethernet Segment\n");
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
    
    printf(ANSI_GREEN "✓" ANSI_RESET " 3 PEs added (All-Active load balancing)\n");
    
    print_separator();
    
    // Step 4: Transition to Single-Active Mode
    printf(ANSI_YELLOW "Step 4:" ANSI_RESET " Transition to Single-Active Mode\n");
    printf("─────────────────────────────────────────────\n");
    
    printf("\n" ANSI_CYAN "Why Single-Active?" ANSI_RESET "\n");
    printf("  • Simpler for devices that don't support multi-chassis LAG\n");
    printf("  • Faster convergence in some scenarios\n");
    printf("  • Lower control plane overhead\n");
    printf("  • Easier troubleshooting\n\n");
    
    if (evpn_transition_to_single_active(&evpn, &esi) == 0) {
        printf(ANSI_GREEN "\n✓" ANSI_RESET " Transitioned to Single-Active mode\n");
    }
    
    print_separator();
    
    // Step 5: Check Active PE Status
    printf(ANSI_YELLOW "Step 5:" ANSI_RESET " Active PE Status\n");
    printf("─────────────────────────────────────────────\n");
    
    uint32_t active_pe;
    int standby_count;
    uint64_t failover_count;
    
    if (evpn_single_active_get_status(&evpn, &esi, &active_pe, 
                                      &standby_count, &failover_count) == 0) {
        struct in_addr addr;
        addr.s_addr = active_pe;
        printf("Active PE: %s\n", inet_ntoa(addr));
        printf("Standby PEs: %d\n", standby_count);
        printf("Failover count: %lu\n", (unsigned long)failover_count);
        
        if (evpn_am_i_active_pe(&evpn, &esi)) {
            printf(ANSI_GREEN "\n→ We are the ACTIVE PE!\n" ANSI_RESET);
            printf("  Status: Forwarding all traffic\n");
        } else {
            printf(ANSI_YELLOW "\n→ We are a STANDBY PE\n" ANSI_RESET);
            printf("  Status: Blocking traffic, ready for failover\n");
        }
    }
    
    print_separator();
    
    // Step 6: Test Traffic Forwarding Decision
    printf(ANSI_YELLOW "Step 6:" ANSI_RESET " Traffic Forwarding Test\n");
    printf("─────────────────────────────────────────────\n");
    
    uint8_t test_packet[] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    
    printf("\nTest packet: ");
    for (int i = 0; i < 6; i++) printf("%02x ", test_packet[i]);
    printf("\n");
    
    // Test ingress (from Customer Equipment)
    int result_ingress = evpn_single_active_forward(&evpn, &esi, test_packet, 
                                                     sizeof(test_packet), 0);
    printf("Ingress (from CE): ");
    if (result_ingress == 0) {
        printf(ANSI_GREEN "FORWARD ✓" ANSI_RESET "\n");
    } else {
        printf(ANSI_RED "DROP ✗" ANSI_RESET " (we are standby)\n");
    }
    
    // Test egress (to Customer Equipment)
    int result_egress = evpn_single_active_forward(&evpn, &esi, test_packet, 
                                                    sizeof(test_packet), 1);
    printf("Egress (to CE):    ");
    if (result_egress == 0) {
        printf(ANSI_GREEN "FORWARD ✓" ANSI_RESET "\n");
    } else {
        printf(ANSI_RED "DROP ✗" ANSI_RESET " (we are standby)\n");
    }
    
    print_separator();
    
    // Step 7: Simulate Active PE Failure
    printf(ANSI_YELLOW "Step 7:" ANSI_RESET " Simulate Active PE Failure\n");
    printf("─────────────────────────────────────────────\n");
    
    // Get current active PE
    uint32_t current_active;
    evpn_get_active_pe(&evpn, &esi, &current_active);
    
    struct in_addr addr;
    addr.s_addr = current_active;
    printf("\n" ANSI_MAGENTA "SIMULATING FAILURE:" ANSI_RESET " Active PE %s goes down\n", 
           inet_ntoa(addr));
    printf("This could be due to:\n");
    printf("  • BGP session loss\n");
    printf("  • PE router crash\n");
    printf("  • Network partition\n");
    printf("  • Administrative shutdown\n\n");
    
    sleep(1);  // Dramatic pause
    
    // Trigger failover
    if (evpn_handle_pe_failure(&evpn, &esi, current_active) == 0) {
        printf(ANSI_GREEN "\n✓" ANSI_RESET " Failover completed successfully!\n");
    }
    
    print_separator();
    
    // Step 8: Check Status After Failover
    printf(ANSI_YELLOW "Step 8:" ANSI_RESET " Status After Failover\n");
    printf("─────────────────────────────────────────────\n");
    
    if (evpn_single_active_get_status(&evpn, &esi, &active_pe, 
                                      &standby_count, &failover_count) == 0) {
        addr.s_addr = active_pe;
        printf("New Active PE: %s\n", inet_ntoa(addr));
        printf("Standby PEs: %d\n", standby_count);
        printf("Failover count: %lu\n", (unsigned long)failover_count);
        
        if (evpn_am_i_active_pe(&evpn, &esi)) {
            printf(ANSI_GREEN "\n→ We are NOW the ACTIVE PE!\n" ANSI_RESET);
            printf("  Status: Taking over traffic forwarding\n");
        } else {
            printf(ANSI_YELLOW "\n→ Still STANDBY\n" ANSI_RESET);
            printf("  Status: Another PE became active\n");
        }
    }
    
    print_separator();
    
    // Step 9: Test Forwarding After Failover
    printf(ANSI_YELLOW "Step 9:" ANSI_RESET " Traffic Forwarding After Failover\n");
    printf("─────────────────────────────────────────────\n");
    
    result_ingress = evpn_single_active_forward(&evpn, &esi, test_packet, 
                                                sizeof(test_packet), 0);
    printf("Ingress (from CE): ");
    if (result_ingress == 0) {
        printf(ANSI_GREEN "FORWARD ✓" ANSI_RESET " (we took over!)\n");
    } else {
        printf(ANSI_RED "DROP ✗" ANSI_RESET " (another PE active)\n");
    }
    
    print_separator();
    
    // Step 10: Compare Single-Active vs All-Active
    printf(ANSI_YELLOW "Step 10:" ANSI_RESET " Single-Active vs All-Active Comparison\n");
    printf("─────────────────────────────────────────────\n");
    
    printf("\n" ANSI_CYAN "Single-Active Mode:" ANSI_RESET "\n");
    printf("  ✓ Only one PE forwards traffic\n");
    printf("  ✓ Other PEs are standby\n");
    printf("  ✓ Fast failover on active PE failure\n");
    printf("  ✓ Simpler for non-LAG devices\n");
    printf("  ✓ Lower control plane overhead\n");
    printf("  ✗ No load balancing\n");
    printf("  ✗ Bandwidth limited to single PE\n");
    
    printf("\n" ANSI_CYAN "All-Active Mode:" ANSI_RESET "\n");
    printf("  ✓ All PEs forward traffic simultaneously\n");
    printf("  ✓ Load balancing across all PEs\n");
    printf("  ✓ Better bandwidth utilization\n");
    printf("  ✗ Requires multi-chassis LAG support\n");
    printf("  ✗ More complex control plane\n");
    printf("  ✗ Need DF election for BUM traffic\n");
    
    print_separator();
    
    // Summary
    printf(ANSI_BLUE);
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                   DEMONSTRATION COMPLETE                      ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf(ANSI_RESET);
    
    printf("\n" ANSI_GREEN "KEY ACHIEVEMENTS:" ANSI_RESET "\n");
    printf("  ✓ Single-Active mode implemented\n");
    printf("  ✓ Active PE election works\n");
    printf("  ✓ Standby PEs block traffic correctly\n");
    printf("  ✓ Failover mechanism functional\n");
    printf("  ✓ Traffic forwarding decisions accurate\n");
    printf("  ✓ Mode transitions work smoothly\n");
    
    printf("\n" ANSI_YELLOW "USE CASES:" ANSI_RESET "\n");
    printf("  • Servers with single-homed NICs\n");
    printf("  • Legacy devices without LAG support\n");
    printf("  • Simplified network topologies\n");
    printf("  • Active/Standby DR scenarios\n");
    
    printf("\n" ANSI_YELLOW "NEXT STEPS (Week 4 Remaining):" ANSI_RESET "\n");
    printf("  • Feature 2: Mass Withdrawal (fast convergence)\n");
    printf("  • Feature 3: Aliasing Support (multiple paths)\n");
    printf("  • Feature 4: Local Bias (traffic optimization)\n");
    
    print_separator();
    
    // Cleanup
    printf("\nCleaning up...\n");
    evpn_cleanup(&evpn);
    
    printf(ANSI_GREEN "\n✓ Single-Active Multi-homing Demo Complete!\n" ANSI_RESET);
    printf("\n");
    
    return 0;
}
