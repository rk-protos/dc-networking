  
/*****************************************************************************
 * Project     : VXLAN Protocol Implementation (RFC 7348)
 * Description : Part of minimal implementation of VXLAN (RFC-7348)
 *                Virtual eXtensible Local Area Network (VXLAN)
 *                encapsulation and decapsulation 
 *                implementation with minimal testing 
 * Author      : RK (kvrkr866@gmail.com)
 * File name   : test_vlan.c  
 * Purpose     : VLAN VLAN Tag Handling Test Suite
 *                Tests RFC 7348 Section 6.1 compliance
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include "../include/vxlan.h"
#include "../include/vxlan_vlan.h"

#define ANSI_GREEN  "\033[32m"
#define ANSI_RED    "\033[31m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_BLUE   "\033[34m"
#define ANSI_RESET  "\033[0m"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    printf("\n" ANSI_RESET "Testing: %s ... ", name); \
    fflush(stdout);

#define PASS() \
    do { \
        printf(ANSI_GREEN "PASS" ANSI_RESET "\n"); \
        tests_passed++; \
    } while(0)

#define FAIL(msg) \
    do { \
        printf(ANSI_RED "FAIL" ANSI_RESET ": %s\n", msg); \
        tests_failed++; \
    } while(0)

#define ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            FAIL(msg); \
            return; \
        } \
    } while(0)

/**
 * Helper: Create test Ethernet frame
 */
void create_test_frame(uint8_t *frame, size_t *len, const char *payload) {
    eth_hdr_t *eth = (eth_hdr_t *)frame;
    
    /* Destination MAC */
    uint8_t dst_mac[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    memcpy(eth->dst_mac, dst_mac, 6);
    
    /* Source MAC */
    uint8_t src_mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    memcpy(eth->src_mac, src_mac, 6);
    
    /* EtherType */
    eth->ether_type = htons(0x0800); /* IPv4 */
    
    /* Payload */
    strcpy((char *)(frame + sizeof(eth_hdr_t)), payload);
    *len = sizeof(eth_hdr_t) + strlen(payload);
}

/* Test 1: VLAN Tag Detection */
void test_vlan_detection() {
    TEST("VLAN Tag Detection");
    
    uint8_t untagged_frame[128];
    size_t untagged_len;
    create_test_frame(untagged_frame, &untagged_len, "Test");
    
    ASSERT(!vxlan_vlan_is_tagged(untagged_frame, untagged_len), 
           "Untagged frame detected as tagged");
    
    /* Create VLAN tagged frame */
    uint8_t tagged_frame[128];
    size_t tagged_len;
    
    int ret = vxlan_vlan_add(untagged_frame, untagged_len, 
                             100, 3, tagged_frame, &tagged_len);
    ASSERT(ret == 0, "Failed to add VLAN tag");
    ASSERT(vxlan_vlan_is_tagged(tagged_frame, tagged_len),
           "Tagged frame not detected");
    
    PASS();
}

/* Test 2: VLAN Tag Addition */
void test_vlan_add() {
    TEST("VLAN Tag Addition");
    
    uint8_t frame[128];
    size_t frame_len;
    create_test_frame(frame, &frame_len, "Payload");
    
    uint8_t tagged[128];
    size_t tagged_len;
    
    /* Add VLAN tag (VLAN 100, Priority 3) */
    int ret = vxlan_vlan_add(frame, frame_len, 100, 3, tagged, &tagged_len);
    
    ASSERT(ret == 0, "VLAN add failed");
    ASSERT(tagged_len == frame_len + VLAN_TAG_SIZE, "Length incorrect");
    
    /* Verify VLAN ID */
    uint16_t vlan_id;
    ret = vxlan_vlan_get_id(tagged, &vlan_id);
    ASSERT(ret == 0, "Failed to get VLAN ID");
    ASSERT(vlan_id == 100, "VLAN ID mismatch");
    
    /* Verify details */
    uint8_t priority, dei;
    ret = vxlan_vlan_get_details(tagged, &vlan_id, &priority, &dei);
    ASSERT(ret == 0, "Failed to get VLAN details");
    ASSERT(vlan_id == 100, "VLAN ID mismatch");
    ASSERT(priority == 3, "Priority mismatch");
    
    PASS();
}

/* Test 3: VLAN Tag Stripping */
void test_vlan_strip() {
    TEST("VLAN Tag Stripping");
    
    uint8_t frame[128];
    size_t frame_len;
    create_test_frame(frame, &frame_len, "TestData");
    
    /* Add VLAN tag */
    uint8_t tagged[128];
    size_t tagged_len;
    vxlan_vlan_add(frame, frame_len, 200, 5, tagged, &tagged_len);
    
    /* Strip VLAN tag */
    uint8_t stripped[128];
    size_t stripped_len;
    
    int ret = vxlan_vlan_strip(tagged, tagged_len, stripped, &stripped_len);
    
    ASSERT(ret == 0, "VLAN strip failed");
    ASSERT(stripped_len == frame_len, "Stripped length mismatch");
    ASSERT(memcmp(frame, stripped, frame_len) == 0, "Frame mismatch after strip");
    
    PASS();
}

/* Test 4: RFC 7348 Default Behavior - Discard Inner VLAN */
void test_rfc_discard_inner_vlan() {
    TEST("RFC 7348: Discard Inner VLAN (Default)");
    
    vxlan_ctx_t ctx;
    vxlan_init(&ctx, inet_addr("192.168.1.100"), 100);
    
    /* Default config should discard inner VLAN */
    ASSERT(ctx.vlan_config.discard_on_decap == true, 
           "Default should discard inner VLAN");
    ASSERT(ctx.vlan_config.allow_inner_vlan == false,
           "Default should not allow inner VLAN");
    
    /* Create VLAN-tagged frame */
    uint8_t untagged[128];
    size_t untagged_len;
    create_test_frame(untagged, &untagged_len, "Data");
    
    uint8_t tagged[128];
    size_t tagged_len;
    vxlan_vlan_add(untagged, untagged_len, 100, 0, tagged, &tagged_len);
    
    /* Validate should fail (discard) */
    int ret = vxlan_vlan_validate(tagged, tagged_len, &ctx.vlan_config);
    ASSERT(ret != 0, "Should discard frame with inner VLAN per RFC 7348");
    
    /* Untagged frame should pass */
    ret = vxlan_vlan_validate(untagged, untagged_len, &ctx.vlan_config);
    ASSERT(ret == 0, "Should accept untagged frame");
    
    vxlan_cleanup(&ctx);
    PASS();
}

/* Test 5: RFC 7348 Encapsulation - Strip VLAN */
void test_rfc_strip_on_encap() {
    TEST("RFC 7348: Strip VLAN on Encapsulation");
    
    vxlan_ctx_t ctx;
    vxlan_init(&ctx, inet_addr("192.168.1.100"), 100);
    
    /* Default should strip VLAN */
    ASSERT(ctx.vlan_config.strip_on_encap == true,
           "Default should strip VLAN on encap");
    
    /* Create VLAN-tagged inner frame */
    uint8_t untagged[128];
    size_t untagged_len;
    create_test_frame(untagged, &untagged_len, "TestPayload");
    
    uint8_t tagged[128];
    size_t tagged_len;
    vxlan_vlan_add(untagged, untagged_len, 50, 2, tagged, &tagged_len);
    
    /* Encapsulate tagged frame */
    uint8_t outer[2048];
    size_t outer_len;
    
    int ret = vxlan_encapsulate(&ctx, tagged, tagged_len,
                                outer, &outer_len, inet_addr("192.168.1.200"));
    
    ASSERT(ret == 0, "Encapsulation failed");
    
    /* Decapsulate and check that inner frame is NOT tagged */
    uint8_t inner[2048];
    size_t inner_len;
    uint32_t src_vtep;
    uint32_t vni;
    
    ret = vxlan_decapsulate(&ctx, outer, outer_len,
                           inner, &inner_len, &src_vtep, &vni);
    
    ASSERT(ret == 0, "Decapsulation failed");
    ASSERT(!vxlan_vlan_is_tagged(inner, inner_len),
           "Inner frame should NOT have VLAN tag (stripped)");
    
    vxlan_cleanup(&ctx);
    PASS();
}

/* Test 6: Allow Inner VLAN (Configured) */
void test_allow_inner_vlan() {
    TEST("Allow Inner VLAN (Configured Mode)");
    
    vxlan_ctx_t ctx;
    vxlan_init(&ctx, inet_addr("192.168.1.100"), 100);
    
    /* Configure to allow inner VLAN */
    ctx.vlan_config.allow_inner_vlan = true;
    ctx.vlan_config.discard_on_decap = false;
    ctx.vlan_config.strip_on_encap = false;
    
    /* Create VLAN-tagged frame */
    uint8_t untagged[128];
    size_t untagged_len;
    create_test_frame(untagged, &untagged_len, "Data");
    
    uint8_t tagged[128];
    size_t tagged_len;
    vxlan_vlan_add(untagged, untagged_len, 100, 0, tagged, &tagged_len);
    
    /* Should NOT discard */
    int ret = vxlan_vlan_validate(tagged, tagged_len, &ctx.vlan_config);
    ASSERT(ret == 0, "Should allow inner VLAN when configured");
    
    vxlan_cleanup(&ctx);
    PASS();
}

/* Test 7: Gateway Mode - VLAN to VNI Mapping */
void test_gateway_vlan_to_vni() {
    TEST("Gateway Mode: VLAN to VNI Mapping");
    
    vlan_config_t config;
    vxlan_vlan_config_init(&config);
    
    /* Enable gateway mode */
    vxlan_vlan_set_gateway_mode(&config, true);
    ASSERT(config.gateway_mode == true, "Gateway mode not enabled");
    
    /* Set mappings */
    vxlan_vlan_set_mapping(&config, 10, 1000);
    vxlan_vlan_set_mapping(&config, 20, 2000);
    vxlan_vlan_set_mapping(&config, 30, 3000);
    
    /* Test VLAN to VNI mapping */
    uint32_t vni;
    int ret = vxlan_vlan_to_vni(&config, 10, &vni);
    ASSERT(ret == 0, "VLAN to VNI mapping failed");
    ASSERT(vni == 1000, "VNI mismatch");
    
    ret = vxlan_vlan_to_vni(&config, 20, &vni);
    ASSERT(ret == 0, "VLAN to VNI mapping failed");
    ASSERT(vni == 2000, "VNI mismatch");
    
    /* Test VNI to VLAN mapping */
    uint16_t vlan_id;
    ret = vxlan_vni_to_vlan(&config, 3000, &vlan_id);
    ASSERT(ret == 0, "VNI to VLAN mapping failed");
    ASSERT(vlan_id == 30, "VLAN ID mismatch");
    
    /* Test unmapped */
    ret = vxlan_vlan_to_vni(&config, 99, &vni);
    ASSERT(ret != 0, "Should fail for unmapped VLAN");
    
    PASS();
}

/* Test 8: Round-trip with VLAN Stripping */
void test_roundtrip_vlan_strip() {
    TEST("Round-trip: VLAN Stripped During Encapsulation");
    
    vxlan_ctx_t ctx;
    vxlan_init(&ctx, inet_addr("192.168.1.100"), 100);
    
    /* Create untagged frame */
    uint8_t untagged[128];
    size_t untagged_len;
    create_test_frame(untagged, &untagged_len, "OriginalPayload");
    
    /* Add VLAN tag */
    uint8_t tagged[128];
    size_t tagged_len;
    vxlan_vlan_add(untagged, untagged_len, 777, 4, tagged, &tagged_len);
    
    /* Encapsulate (should strip VLAN) */
    uint8_t outer[2048];
    size_t outer_len;
    vxlan_encapsulate(&ctx, tagged, tagged_len, outer, &outer_len,
                      inet_addr("192.168.1.200"));
    
    /* Decapsulate */
    uint8_t inner[2048];
    size_t inner_len;
    uint32_t src_vtep, vni;
    vxlan_decapsulate(&ctx, outer, outer_len, inner, &inner_len, &src_vtep, &vni);
    
    /* Result should match original untagged frame */
    ASSERT(inner_len == untagged_len, "Length mismatch");
    ASSERT(memcmp(inner, untagged, untagged_len) == 0, "Frame mismatch");
    
    vxlan_cleanup(&ctx);
    PASS();
}

/* Test 9: Invalid VLAN Parameters */
void test_vlan_invalid_params() {
    TEST("Invalid VLAN Parameters");
    
    uint8_t frame[128];
    size_t frame_len;
    create_test_frame(frame, &frame_len, "Test");
    
    uint8_t output[128];
    size_t output_len;
    
    /* Invalid VLAN ID (>4095) */
    int ret = vxlan_vlan_add(frame, frame_len, 5000, 0, output, &output_len);
    ASSERT(ret != 0, "Should reject VLAN ID > 4095");
    
    /* Invalid priority (>7) */
    ret = vxlan_vlan_add(frame, frame_len, 100, 10, output, &output_len);
    ASSERT(ret != 0, "Should reject priority > 7");
    
    PASS();
}

/* Test 10: VLAN Tag Preservation (Gateway Mode) */
void test_vlan_preservation_gateway() {
    TEST("VLAN Tag Preservation in Gateway Mode");
    
    vxlan_ctx_t ctx;
    vxlan_init(&ctx, inet_addr("192.168.1.100"), 100);
    
    /* Configure for gateway mode */
    vxlan_vlan_set_gateway_mode(&ctx.vlan_config, true);
    ctx.vlan_config.strip_on_encap = false;  /* Don't strip in gateway mode */
    
    /* Create VLAN-tagged frame */
    uint8_t untagged[128];
    size_t untagged_len;
    create_test_frame(untagged, &untagged_len, "GatewayData");
    
    uint8_t tagged[128];
    size_t tagged_len;
    vxlan_vlan_add(untagged, untagged_len, 42, 1, tagged, &tagged_len);
    
    /* Encapsulate (should NOT strip in gateway mode) */
    uint8_t outer[2048];
    size_t outer_len;
    vxlan_encapsulate(&ctx, tagged, tagged_len, outer, &outer_len,
                      inet_addr("192.168.1.200"));
    
    /* Decapsulate */
    uint8_t inner[2048];
    size_t inner_len;
    uint32_t src_vtep, vni;
    vxlan_decapsulate(&ctx, outer, outer_len, inner, &inner_len, &src_vtep, &vni);
    
    /* Inner frame should still have VLAN tag */
    ASSERT(vxlan_vlan_is_tagged(inner, inner_len),
           "VLAN tag should be preserved in gateway mode");
    
    uint16_t vlan_id;
    vxlan_vlan_get_id(inner, &vlan_id);
    ASSERT(vlan_id == 42, "VLAN ID mismatch");
    
    vxlan_cleanup(&ctx);
    PASS();
}

int main() {
    printf("\n");
    printf(ANSI_BLUE);
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║      VXLAN Inner VLAN Tag Handling Test Suite                 ║\n");
    printf("║              RFC 7348 Section 6.1 Compliance                  ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf(ANSI_RESET "\n");
    
    /* Run all tests */
    test_vlan_detection();
    test_vlan_add();
    test_vlan_strip();
    test_rfc_discard_inner_vlan();
    test_rfc_strip_on_encap();
    test_allow_inner_vlan();
    test_gateway_vlan_to_vni();
    test_roundtrip_vlan_strip();
    test_vlan_invalid_params();
    test_vlan_preservation_gateway();
    
    /* Summary */
    printf("\n");
    printf(ANSI_BLUE "═══════════════════════════════════════════════════════════\n" ANSI_RESET);
    printf("                     Test Results\n");
    printf(ANSI_BLUE "═══════════════════════════════════════════════════════════\n" ANSI_RESET);
    printf("Tests passed: " ANSI_GREEN "%d" ANSI_RESET "\n", tests_passed);
    printf("Tests failed: " ANSI_RED "%d" ANSI_RESET "\n", tests_failed);
    printf(ANSI_BLUE "═══════════════════════════════════════════════════════════\n" ANSI_RESET);
    
    if (tests_failed == 0) {
        printf("\n" ANSI_GREEN);
        printf("╔═══════════════════════════════════════════════════════════════╗\n");
        printf("║         ✓ All RFC 7348 VLAN Tests Passed Successfully         ║\n");
        printf("╚═══════════════════════════════════════════════════════════════╝\n");
        printf(ANSI_RESET "\n");
    }
    
    return (tests_failed == 0) ? 0 : 1;
}
