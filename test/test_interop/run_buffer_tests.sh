#!/bin/bash
#
# Automated Buffer interoperability tests for microReticulum
#
# This script:
# 1. Builds the C++ client
# 2. Starts the Python test server (link_server.py with buffer support)
# 3. Runs the C++ client with buffer test commands
# 4. Parses output for PASS/FAIL markers
# 5. Generates a summary report
#
# Usage: ./run_buffer_tests.sh [test_name]
#        test_name: ping, small, big, all (default: all)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MICRO_RET_DIR="$SCRIPT_DIR/../.."
PYTHON_SERVER="$SCRIPT_DIR/python/link_server.py"
CPP_CLIENT_DIR="$MICRO_RET_DIR/examples/link"
CONFIG_DIR="$SCRIPT_DIR/python/test_rns_config"

# Test selection
TEST_TO_RUN="${1:-all}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test results
declare -a PASSED_TESTS
declare -a FAILED_TESTS

log_info() {
    echo -e "${YELLOW}[INFO]${NC} $1"
}

log_pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
    PASSED_TESTS+=("$1")
}

log_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
    FAILED_TESTS+=("$1")
}

cleanup() {
    log_info "Cleaning up..."
    if [[ -n "$SERVER_PID" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    if [[ -n "$CLIENT_PID" ]]; then
        kill "$CLIENT_PID" 2>/dev/null || true
        wait "$CLIENT_PID" 2>/dev/null || true
    fi
    rm -f /tmp/buffer_test_fifo_* 2>/dev/null || true
}

trap cleanup EXIT

# Kill any stale processes
setup_clean_environment() {
    log_info "Cleaning up stale processes..."
    # Use SIGKILL (-9) to ensure processes are definitely killed
    pkill -9 -f "link_server" 2>/dev/null || true
    pkill -9 -f "channel_test_server" 2>/dev/null || true
    # Kill any C++ test clients
    pkill -9 -f "\.pio/build/native/program" 2>/dev/null || true
    sleep 3

    # Clean RNS storage for fresh identities
    rm -rf "$CONFIG_DIR/storage" 2>/dev/null || true

    # Verify ports are free
    if ss -tuln 2>/dev/null | grep -q ":1424[23]"; then
        log_info "Warning: Test ports may still be in use"
        ss -tuln 2>/dev/null | grep ":1424" || true
        sleep 2
    fi
}

# Build C++ client
build_client() {
    log_info "Building C++ client..."
    cd "$CPP_CLIENT_DIR"
    pio run -e native > /dev/null 2>&1
    if [[ $? -ne 0 ]]; then
        log_fail "C++ build failed"
        exit 1
    fi
    log_info "Build successful"
}

# Start Python server and capture destination hash
start_server() {
    # NOTE: Don't use log_info here - it goes to stdout and would be captured
    echo -e "${YELLOW}[INFO]${NC} Starting Python test server..." >&2

    # Clean RNS storage for fresh identity each time
    rm -rf "$CONFIG_DIR/storage" 2>/dev/null || true

    # Create config dir if needed
    mkdir -p "$CONFIG_DIR"

    # Start server and capture output
    /usr/bin/python "$PYTHON_SERVER" -c "$CONFIG_DIR" > /tmp/buffer_server_output.txt 2>&1 &
    SERVER_PID=$!

    # Wait for server to output destination hash
    local timeout=30
    local dest_hash=""
    while [[ $timeout -gt 0 ]]; do
        if grep -q '\[DEST:' /tmp/buffer_server_output.txt 2>/dev/null; then
            dest_hash=$(grep '\[DEST:' /tmp/buffer_server_output.txt | head -1 | sed 's/.*\[DEST:\([a-f0-9]*\)\].*/\1/')
            break
        fi
        sleep 1
        ((timeout--))
    done

    if [[ -z "$dest_hash" ]]; then
        echo -e "${RED}[FAIL]${NC} Failed to get server destination hash" >&2
        echo "--- Server output ---" >&2
        cat /tmp/buffer_server_output.txt >&2
        echo "--- End server output ---" >&2
        # Return empty string instead of exiting to allow test to fail gracefully
        echo ""
        return 1
    fi

    echo "$dest_hash"
}

# Run a single test
run_test() {
    local test_name="$1"
    local test_cmd="$2"
    local dest_hash="$3"
    local timeout="${4:-30}"

    log_info "Running test: $test_name (timeout: ${timeout}s)"

    # Create a named pipe for client input
    local fifo="/tmp/buffer_test_fifo_$$"
    rm -f "$fifo"
    mkfifo "$fifo" 2>/dev/null || true

    # Start C++ client with destination hash
    cd "$CPP_CLIENT_DIR"
    timeout "$timeout" .pio/build/native/program "$dest_hash" < "$fifo" > /tmp/buffer_client_output.txt 2>&1 &
    CLIENT_PID=$!

    # Wait for link to establish
    sleep 3

    # Send test command
    echo "$test_cmd" > "$fifo"

    # Wait for test to complete (look for PASS or FAIL)
    # Use longer timeout for big test
    local test_timeout=15
    if [[ "$test_name" == "buffer_big" ]]; then
        test_timeout=65
    fi

    local result=""
    while [[ $test_timeout -gt 0 ]]; do
        if grep -q "\[TEST:.*:PASS\]" /tmp/buffer_client_output.txt 2>/dev/null; then
            result="PASS"
            break
        fi
        if grep -q "\[TEST:.*:FAIL" /tmp/buffer_client_output.txt 2>/dev/null; then
            result="FAIL"
            break
        fi
        sleep 1
        ((test_timeout--))
    done

    # Send quit command
    echo "quit" > "$fifo" 2>/dev/null || true
    sleep 1

    # Clean up fifo and client
    rm -f "$fifo"
    kill "$CLIENT_PID" 2>/dev/null || true
    wait "$CLIENT_PID" 2>/dev/null || true
    CLIENT_PID=""

    # Check result
    if [[ "$result" == "PASS" ]]; then
        log_pass "$test_name"
        return 0
    elif [[ "$result" == "FAIL" ]]; then
        local reason=$(grep "\[TEST:.*:FAIL" /tmp/buffer_client_output.txt | head -1)
        log_fail "$test_name: $reason"
        return 1
    else
        log_fail "$test_name: Timeout - no PASS/FAIL marker found"
        echo "--- Client output ---"
        cat /tmp/buffer_client_output.txt
        echo "--- Server output (last 50 lines) ---"
        tail -50 /tmp/buffer_server_output.txt
        return 1
    fi
}

# Run all tests
run_all_tests() {
    local dest_hash="$1"

    case "$TEST_TO_RUN" in
        ping)
            run_test "buffer_ping" "buffer ping" "$dest_hash" 30
            ;;
        small)
            run_test "buffer_small" "buffer test small" "$dest_hash" 30
            ;;
        big)
            run_test "buffer_big" "buffer test big" "$dest_hash" 90
            ;;
        all)
            # Run all tests sequentially using the same server
            # Server handles multiple link connections, so no need to restart
            run_test "buffer_ping" "buffer ping" "$dest_hash" 30 || true
            sleep 2
            run_test "buffer_small" "buffer test small" "$dest_hash" 30 || true
            sleep 2
            run_test "buffer_big" "buffer test big" "$dest_hash" 90 || true
            ;;
        *)
            log_fail "Unknown test: $TEST_TO_RUN"
            echo "Usage: $0 [ping|small|big|all]"
            exit 1
            ;;
    esac
}

# Print summary
print_summary() {
    echo ""
    echo "========================================"
    echo "       BUFFER TEST SUMMARY"
    echo "========================================"
    echo ""

    if [[ ${#PASSED_TESTS[@]} -gt 0 ]]; then
        echo -e "${GREEN}PASSED (${#PASSED_TESTS[@]}):${NC}"
        for test in "${PASSED_TESTS[@]}"; do
            echo "  - $test"
        done
    fi

    if [[ ${#FAILED_TESTS[@]} -gt 0 ]]; then
        echo ""
        echo -e "${RED}FAILED (${#FAILED_TESTS[@]}):${NC}"
        for test in "${FAILED_TESTS[@]}"; do
            echo "  - $test"
        done
    fi

    echo ""
    local total=$((${#PASSED_TESTS[@]} + ${#FAILED_TESTS[@]}))
    echo "Total: $total tests, ${#PASSED_TESTS[@]} passed, ${#FAILED_TESTS[@]} failed"
    echo ""

    if [[ ${#FAILED_TESTS[@]} -gt 0 ]]; then
        return 1
    fi
    return 0
}

# Main
main() {
    echo "========================================"
    echo "   Buffer Interoperability Tests"
    echo "========================================"
    echo ""

    # Clean environment
    setup_clean_environment

    # Build client
    build_client

    # Start server
    DEST_HASH=$(start_server)
    log_info "Server destination: $DEST_HASH"

    # Wait for server to be ready
    sleep 2

    # Run tests
    run_all_tests "$DEST_HASH"

    # Print summary
    print_summary
    exit $?
}

main
