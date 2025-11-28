#!/bin/bash
#
# Automated Channel interoperability tests for microReticulum
#
# This script:
# 1. Builds the C++ client
# 2. Starts the Python test server
# 3. Runs the C++ client with test commands
# 4. Parses output for PASS/FAIL markers
# 5. Generates a summary report
# 6. Optionally generates JSON output for comparison engine
#
# Usage: ./run_channel_tests.sh [test_name] [--json-output FILE]
#        test_name: basic, wire, sequence, empty, all (default: all)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MICRO_RET_DIR="$SCRIPT_DIR/../.."
PYTHON_SERVER="$SCRIPT_DIR/python/channel_test_server.py"
CPP_CLIENT_DIR="$MICRO_RET_DIR/examples/link"
CONFIG_DIR="$SCRIPT_DIR/python/test_rns_config"

# Source JSON utilities
source "$SCRIPT_DIR/test_json_utils.sh"

# Parse arguments
TEST_TO_RUN="all"
JSON_OUTPUT_FILE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --json-output)
            JSON_OUTPUT_FILE="$2"
            shift 2
            ;;
        -*)
            echo "Unknown option: $1"
            exit 1
            ;;
        *)
            TEST_TO_RUN="$1"
            shift
            ;;
    esac
done

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
}

trap cleanup EXIT

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
    log_info "Starting Python test server..."

    # Create config dir if needed
    mkdir -p "$CONFIG_DIR"

    # Start server and capture output
    /usr/bin/python "$PYTHON_SERVER" -c "$CONFIG_DIR" > /tmp/channel_server_output.txt 2>&1 &
    SERVER_PID=$!

    # Wait for server to output destination hash
    local timeout=30
    local dest_hash=""
    while [[ $timeout -gt 0 ]]; do
        if grep -q '\[DEST:' /tmp/channel_server_output.txt 2>/dev/null; then
            dest_hash=$(grep '\[DEST:' /tmp/channel_server_output.txt | head -1 | sed 's/.*\[DEST:\([a-f0-9]*\)\].*/\1/')
            break
        fi
        sleep 1
        ((timeout--))
    done

    if [[ -z "$dest_hash" ]]; then
        log_fail "Failed to get server destination hash"
        cat /tmp/channel_server_output.txt
        exit 1
    fi

    echo "$dest_hash"
}

# Run a single test
run_test() {
    local test_name="$1"
    local test_cmd="$2"
    local dest_hash="$3"
    local timeout="${4:-30}"

    log_info "Running test: $test_name"

    # Create a named pipe for client input
    local fifo="/tmp/channel_test_fifo_$$"
    mkfifo "$fifo" 2>/dev/null || true

    # Start C++ client with destination hash
    cd "$CPP_CLIENT_DIR"
    timeout "$timeout" .pio/build/native/program "$dest_hash" < "$fifo" > /tmp/channel_client_output.txt 2>&1 &
    CLIENT_PID=$!

    # Wait for link to establish
    sleep 3

    # Send test command
    echo "$test_cmd" > "$fifo"

    # Wait for test to complete (look for PASS or FAIL)
    local test_timeout=15
    local result=""
    while [[ $test_timeout -gt 0 ]]; do
        if grep -q "\[TEST:.*:PASS\]" /tmp/channel_client_output.txt 2>/dev/null; then
            result="PASS"
            break
        fi
        if grep -q "\[TEST:.*:FAIL" /tmp/channel_client_output.txt 2>/dev/null; then
            result="FAIL"
            break
        fi
        sleep 1
        ((test_timeout--))
    done

    # Send quit command
    echo "quit" > "$fifo"
    sleep 1

    # Clean up fifo
    rm -f "$fifo"

    # Check result
    if [[ "$result" == "PASS" ]]; then
        log_pass "$test_name"
        return 0
    elif [[ "$result" == "FAIL" ]]; then
        local reason=$(grep "\[TEST:.*:FAIL" /tmp/channel_client_output.txt | head -1)
        log_fail "$test_name: $reason"
        return 1
    else
        log_fail "$test_name: Timeout - no PASS/FAIL marker found"
        echo "--- Client output ---"
        cat /tmp/channel_client_output.txt
        echo "--- Server output ---"
        cat /tmp/channel_server_output.txt
        return 1
    fi
}

# Run all tests
run_all_tests() {
    local dest_hash="$1"

    case "$TEST_TO_RUN" in
        basic)
            run_test "channel_basic_roundtrip" "test channel basic" "$dest_hash"
            ;;
        wire)
            run_test "channel_wire_format" "test channel wire" "$dest_hash"
            ;;
        sequence)
            run_test "channel_sequence_increment" "test channel sequence" "$dest_hash"
            ;;
        empty)
            run_test "channel_empty_payload" "test channel empty" "$dest_hash"
            ;;
        all)
            run_test "channel_basic_roundtrip" "test channel basic" "$dest_hash" || true
            # Need to restart server for clean state between tests
            kill "$SERVER_PID" 2>/dev/null || true
            sleep 2
            dest_hash=$(start_server)
            sleep 2

            run_test "channel_wire_format" "test channel wire" "$dest_hash" || true
            kill "$SERVER_PID" 2>/dev/null || true
            sleep 2
            dest_hash=$(start_server)
            sleep 2

            run_test "channel_sequence_increment" "test channel sequence" "$dest_hash" || true
            kill "$SERVER_PID" 2>/dev/null || true
            sleep 2
            dest_hash=$(start_server)
            sleep 2

            run_test "channel_empty_payload" "test channel empty" "$dest_hash" || true
            ;;
        *)
            log_fail "Unknown test: $TEST_TO_RUN"
            exit 1
            ;;
    esac
}

# Print summary
print_summary() {
    echo ""
    echo "========================================"
    echo "           TEST SUMMARY"
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
    echo "  Channel Interoperability Tests"
    echo "========================================"
    echo ""

    # Initialize JSON test run tracking
    json_init_test_run

    # Build client
    build_client

    # Start server
    DEST_HASH=$(start_server)
    log_info "Server destination: $DEST_HASH"

    # Wait for server to be ready
    sleep 2

    # Run tests
    run_all_tests "$DEST_HASH"

    # Generate JSON output if requested
    if [[ -n "$JSON_OUTPUT_FILE" ]]; then
        json_generate_results "$JSON_OUTPUT_FILE"
        json_print_summary "$JSON_OUTPUT_FILE"
    fi

    # Print summary
    print_summary
    exit $?
}

main
