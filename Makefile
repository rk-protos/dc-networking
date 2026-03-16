
 #/*****************************************************************************
 #* Project     : VXLAN Protocol Implementation (RFC 7348)
 #* Description : Part of minimal implementation of VXLAN (RFC-7348)
 #*                Virtual eXtensible Local Area Network (VXLAN)
 #*                encapsulation and decapsulation 
 #*                implementation with minimal testing 
 #* Author      : RK (kvrkr866@gmail.com)
 #* File name   : Makefile  
 #* Purpose     : Makefile for VXLAN Implementation
 #*****************************************************************************/

# Makefile for VXLAN Implementation

CC      = gcc
CFLAGS  =  -Wall -Wextra -I./include -pthread 
LDFLAGS = -pthread

# Source files
SRC_DIR  = src
INC_DIR  = include
TEST_DIR = test
BIN_DIR  = bin

SOURCES = $(SRC_DIR)/vxlan_encap.c \
          $(SRC_DIR)/vxlan_decap.c \
          $(SRC_DIR)/vxlan_mac_learning.c \
          $(SRC_DIR)/vxlan_utils.c \
          $(SRC_DIR)/vxlan_init.c \
          $(SRC_DIR)/vxlan_vlan.c \
		  $(SRC_DIR)/vxlan_multicast.c

OBJECTS = $(SOURCES:.c=.o)

# Targets
all: vxlan_lib test

vxlan_lib: $(OBJECTS)
	ar rcs libvxlan.a $(OBJECTS)
	@echo "Built completed for --> libvxlan.a"

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

test: vxlan_lib
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(TEST_DIR)/test_vtep.c -L. -lvxlan $(LDFLAGS)  -o $(BIN_DIR)/test_vtep
	$(CC) $(CFLAGS) $(TEST_DIR)/test_vxlan.c -L. -lvxlan $(LDFLAGS) -o $(BIN_DIR)/test_vxlan
	$(CC) $(CFLAGS) $(TEST_DIR)/test_udp_checksum.c -L. -lvxlan $(LDFLAGS) -o $(BIN_DIR)/test_udp_checksum
	$(CC) $(CFLAGS) $(TEST_DIR)/test_vlan.c -L. -lvxlan $(LDFLAGS) -o $(BIN_DIR)/test_vlan
	$(CC) $(CFLAGS) $(TEST_DIR)/test_multicast.c -L. -lvxlan $(LDFLAGS) -o $(BIN_DIR)/test_multicast
	@echo "Built completed for tests --> test_vtep, test_vxlan, test_udp_checksum, test_vlan, test_multicast"

clean:
	rm -f $(OBJECTS) libvxlan.a $(BIN_DIR)/test_vtep $(BIN_DIR)/test_vxlan $(BIN_DIR)/test_udp_checksum $(BIN_DIR)/test_vlan $(BIN_DIR)/test_multicast
	@echo "Cleaned build artifacts"

.PHONY: all clean test vxlan_lib
