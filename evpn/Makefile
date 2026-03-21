
#/*****************************************************************************
# * Project     : EVPN Protocol Implementation (RFC 8365)
# * Description : Part of minimal implementation of EVPN
# * Author      : RK (kvrkr866@gmail.com)
# * File name   : evpn_bgp.h  
# * Purpose     : This implementation provides  
# *                Minimal BGP-4 implementation focused on EVPN requirements
# *****************************************************************************/

CC = gcc
CFLAGS = -Wall -Wextra -I./include -pthread  -g -O2
LDFLAGS = -pthread

# Directories
SRC_DIR = src
INCLUDE_DIR = include
TEST_DIR = tests
BIN_DIR  = bin

# Source files
SOURCES = $(SRC_DIR)/evpn_init.c \
          $(SRC_DIR)/evpn_bgp.c \
          $(SRC_DIR)/evpn_bgp_fsm.c \
          $(SRC_DIR)/evpn_rib.c \
          $(SRC_DIR)/evpn_routes.c \
          $(SRC_DIR)/evpn_vxlan.c \
          $(SRC_DIR)/evpn_update.c \
          $(SRC_DIR)/evpn_multihoming.c

# Object files
OBJECTS = $(SOURCES:.c=.o)

# Library
LIBRARY = libevpn.a


# Tests
TESTS = $(BIN_DIR)/test_evpn_basics $(BIN_DIR)/bgp_session_demo $(BIN_DIR)/mac_learning_demo $(BIN_DIR)/multihoming_demo

# Default target
all: $(LIBRARY) tests

# Build library
$(LIBRARY): $(OBJECTS)
	@echo "Building library: $@"
	ar rcs $@ $^
	@echo "✓ Library built successfully"

# Compile source files
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling: $<"
	$(CC) $(CFLAGS) -c $< -o $@

tests: $(LIBRARY) $(TEST_DIR)/bgp_session_demo.c $(TEST_DIR)/mac_learning_demo.c $(TEST_DIR)/multihoming_demo.c $(TEST_DIR)/test_evpn_basics.c  
	@echo "Building tests: $@"
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(TEST_DIR)/bgp_session_demo.c  -L. -levpn $(LDFLAGS) -o $(BIN_DIR)/bgp_session_demo
	$(CC) $(CFLAGS) $(TEST_DIR)/mac_learning_demo.c -L. -levpn $(LDFLAGS) -o $(BIN_DIR)/mac_learning_demo
	$(CC) $(CFLAGS) $(TEST_DIR)/multihoming_demo.c  -L. -levpn $(LDFLAGS) -o $(BIN_DIR)/multihoming_demo
	$(CC) $(CFLAGS) $(TEST_DIR)/test_evpn_basics.c  -L. -levpn $(LDFLAGS) -o $(BIN_DIR)/test_evpn_basics
	@echo "✓ demo and test binaries built successfully"


# Run tests
check: tests
	@echo ""
	@echo "Running tests..."
	@echo "================"
	./test_evpn_basics
	@echo ""
	@echo "✓ All tests passed!"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(OBJECTS) $(LIBRARY) $(TESTS)
	rm -f *.o
	@echo "✓ Clean complete"

# Install (optional)
install: $(LIBRARY)
	@echo "Installing EVPN library..."
	mkdir -p /usr/local/lib
	mkdir -p /usr/local/include/evpn
	cp $(LIBRARY) /usr/local/lib/
	cp $(INCLUDE_DIR)/*.h /usr/local/include/evpn/
	@echo "✓ Installation complete"

# Uninstall
uninstall:
	@echo "Uninstalling EVPN library..."
	rm -f /usr/local/lib/$(LIBRARY)
	rm -rf /usr/local/include/evpn
	@echo "✓ Uninstall complete"

# Help
help:
	@echo "EVPN (RFC 8365) Implementation - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build library, examples, and tests (default)"
	@echo "  $(LIBRARY)   - Build EVPN library only"
	@echo "  tests        - Build test programs"
	@echo "  check        - Run all tests"
	@echo "  clean        - Remove all build artifacts"
	@echo "  install      - Install library to /usr/local"
	@echo "  uninstall    - Remove installed library"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make              # Build everything"
	@echo "  make clean        # Clean build"
	@echo "  make check        # Run tests"

# Debug build
debug: CFLAGS += -DDEBUG -g3 -O0
debug: clean all
	@echo "✓ Debug build complete"

# Release build
release: CFLAGS += -DNDEBUG -O3
release: clean all
	@echo "✓ Release build complete"

# Phony targets
.PHONY: all tests check clean install uninstall help debug release

# Dependencies (automatically generated)
$(SRC_DIR)/evpn_init.o: $(SRC_DIR)/evpn_init.c $(INCLUDE_DIR)/evpn.h $(INCLUDE_DIR)/evpn_bgp.h
$(SRC_DIR)/evpn_bgp.o: $(SRC_DIR)/evpn_bgp.c $(INCLUDE_DIR)/evpn_bgp.h
$(SRC_DIR)/evpn_bgp_fsm.o: $(SRC_DIR)/evpn_bgp_fsm.c $(INCLUDE_DIR)/evpn.h $(INCLUDE_DIR)/evpn_bgp.h
$(SRC_DIR)/evpn_rib.o: $(SRC_DIR)/evpn_rib.c $(INCLUDE_DIR)/evpn.h
