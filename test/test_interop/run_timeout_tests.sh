#!/bin/bash
#
# Timeout Test Suite for microReticulum
#
# This script runs timeout tests to verify C++ implementation matches Python RNS.
# Each test can take 30-90 seconds due to real timeout values.
#
# Usage:
#   ./run_timeout_tests.sh [test_name]
#
# Available tests:
#   adv-timeout     Test sender advertisement timeout (C++ sends, Python ignores)
#   send-only       Test baseline - Python sends, C++ receives
#   all             Run all tests
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PYTHON_DIR="$SCRIPT_DIR/python"
CPP_DIR="$SCRIPT_DIR/../../examples/link"
CONFIG_DIR="$PYTHON_DIR/test_rns_config"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    pkill -f "timeout_server.py" 2>/dev/null || true
    pkill -f ".pio/build/native/program" 2>/dev/null || true
    sleep 1
}

trap cleanup EXIT

# Build C++ client if needed
build_cpp() {
    echo -e "${YELLOW}Building C++ client...${NC}"
    cd "$CPP_DIR"
    pio run -e native > /dev/null 2>&1
    echo -e "${GREEN}Build complete${NC}"
}

# Start Python server in background
start_server() {
    local mode=$1
    echo -e "${YELLOW}Starting Python server (mode: $mode)...${NC}"
    cd "$PYTHON_DIR"
    /usr/bin/python timeout_server.py -c "$CONFIG_DIR" -m "$mode" &
    SERVER_PID=$!
    sleep 3  # Wait for server to initialize

    # Get destination hash from server output (last line with "Destination:")
    # Note: Server prints destination hash, need to capture it
    echo -e "${GREEN}Server started (PID: $SERVER_PID)${NC}"
}

# Run C++ client with timeout
run_cpp_client() {
    local dest_hash=$1
    local timeout=$2
    local command=$3

    echo -e "${YELLOW}Running C++ client (timeout: ${timeout}s)...${NC}"
    cd "$CPP_DIR"

    if [ -n "$command" ]; then
        # Send command via stdin
        echo "$command" | timeout $timeout .pio/build/native/program "$dest_hash" 2>&1
    else
        timeout $timeout .pio/build/native/program "$dest_hash" 2>&1
    fi

    return $?
}

# Test: Advertisement timeout
# C++ sends resource, Python ignores (doesn't request parts)
# Expected: C++ times out after ~50-60s and resource status = FAILED
test_adv_timeout() {
    echo -e "\n${YELLOW}========================================${NC}"
    echo -e "${YELLOW}TEST: Advertisement Timeout${NC}"
    echo -e "${YELLOW}========================================${NC}"
    echo "C++ will send a resource, Python will not request parts."
    echo "Expected: C++ sender times out after ~50-60 seconds."
    echo ""

    cleanup
    build_cpp

    # Start server in drop-adv mode
    cd "$PYTHON_DIR"
    /usr/bin/python timeout_server.py -c "$CONFIG_DIR" -m drop-adv &
    SERVER_PID=$!
    sleep 3

    # Get the destination hash - we need to parse it from server output
    # For now, run interactively
    echo -e "${YELLOW}Server started. Run C++ client manually:${NC}"
    echo ""
    echo "  cd $CPP_DIR"
    echo "  .pio/build/native/program <destination_hash>"
    echo "  # When connected, type: send 1024"
    echo ""
    echo "Watch for 'Advertisement timed out' or 'FAILED' status in C++ output."
    echo "Press Ctrl+C when done."

    wait $SERVER_PID
}

# Test: Send-only (baseline - Python sends to C++)
test_send_only() {
    echo -e "\n${YELLOW}========================================${NC}"
    echo -e "${YELLOW}TEST: Send Only (Baseline)${NC}"
    echo -e "${YELLOW}========================================${NC}"
    echo "Python sends resource, C++ receives."
    echo "Expected: Transfer completes successfully."
    echo ""

    cleanup
    build_cpp

    cd "$PYTHON_DIR"
    /usr/bin/python timeout_server.py -c "$CONFIG_DIR" -m send-only &
    SERVER_PID=$!
    sleep 3

    echo -e "${YELLOW}Server started. Run C++ client manually:${NC}"
    echo ""
    echo "  cd $CPP_DIR"
    echo "  .pio/build/native/program <destination_hash>"
    echo ""
    echo "C++ should receive a 10KB resource and prove it."
    echo "Press Ctrl+C when done."

    wait $SERVER_PID
}

# Main
case "${1:-all}" in
    adv-timeout)
        test_adv_timeout
        ;;
    send-only)
        test_send_only
        ;;
    all)
        echo "Running all timeout tests..."
        echo "Note: Each test requires manual interaction."
        echo ""
        test_send_only
        test_adv_timeout
        ;;
    *)
        echo "Usage: $0 [test_name]"
        echo ""
        echo "Available tests:"
        echo "  adv-timeout   Test sender advertisement timeout"
        echo "  send-only     Baseline test - Python sends, C++ receives"
        echo "  all           Run all tests"
        exit 1
        ;;
esac

echo -e "\n${GREEN}Test session complete.${NC}"
