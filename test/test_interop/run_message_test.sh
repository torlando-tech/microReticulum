#!/bin/bash
# Message Exchange Test Script
# Tests 100+ message exchange between C++ microReticulum and Python RNS
# This validates the Link encryption key derivation works correctly
#
# Usage: ./run_message_test.sh [--json-output FILE]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PYTHON_DIR="$SCRIPT_DIR/python"
LINK_EXAMPLE_DIR="$PROJECT_ROOT/examples/link"

# Source JSON utilities
source "$SCRIPT_DIR/test_json_utils.sh"

# Parse arguments
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
            echo "Unknown argument: $1"
            exit 1
            ;;
    esac
done

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Test parameters
NUM_MESSAGES=100
MESSAGE_DELAY=0.1  # seconds between messages

# Test results arrays (for JSON compatibility)
declare -a PASSED_TESTS
declare -a FAILED_TESTS

echo "========================================"
echo "Message Exchange Test (100+ messages)"
echo "========================================"
echo ""

# Initialize JSON test run tracking
json_init_test_run

# Cleanup function
cleanup() {
    echo "Cleaning up..."
    pkill -f "link_server.py" 2>/dev/null || true
    pkill -f "program.*[0-9a-f]{32}" 2>/dev/null || true
    rm -f /tmp/py_msg_test.log /tmp/cpp_msg_test.log /tmp/cpp_msg_input.txt
}
trap cleanup EXIT

# Check prerequisites
echo -n "Checking prerequisites... "
if ! command -v python &> /dev/null; then
    echo -e "${RED}FAIL${NC}"
    echo "Python not found"
    exit 1
fi

if [ ! -f "$LINK_EXAMPLE_DIR/.pio/build/native/program" ]; then
    echo -e "${YELLOW}Building link example...${NC}"
    cd "$LINK_EXAMPLE_DIR"
    pio run -e native > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo -e "${RED}FAIL${NC}"
        echo "Failed to build link example"
        exit 1
    fi
fi
echo -e "${GREEN}OK${NC}"

# Clean up any existing processes
pkill -f "link_server.py" 2>/dev/null || true
sleep 1

# Remove old storage to get fresh identities
rm -rf "$PYTHON_DIR/test_rns_config/storage" 2>/dev/null

# Start Python server
echo -n "Starting Python RNS server... "
cd "$PYTHON_DIR"
python -u link_server.py -c ./test_rns_config > /tmp/py_msg_test.log 2>&1 &
PY_PID=$!
sleep 3

# Get destination hash from server output
DEST_HASH=$(grep "Destination hash:" /tmp/py_msg_test.log | head -1 | awk '{print $NF}')
if [ -z "$DEST_HASH" ]; then
    echo -e "${RED}FAIL${NC}"
    echo "Could not get destination hash from Python server"
    cat /tmp/py_msg_test.log
    exit 1
fi
echo -e "${GREEN}OK${NC} (hash: $DEST_HASH)"

# Generate input for C++ client
echo -n "Generating $NUM_MESSAGES test messages... "
rm -f /tmp/cpp_msg_input.txt
for i in $(seq 1 $NUM_MESSAGES); do
    echo "TEST_MESSAGE_$i" >> /tmp/cpp_msg_input.txt
done
echo -e "${GREEN}OK${NC}"

# Run C++ client with piped input
echo -e "${CYAN}Running C++ client with $NUM_MESSAGES messages...${NC}"
cd "$LINK_EXAMPLE_DIR"

# Use a subshell to slowly feed messages, then wait for responses
(
    # Wait for link to establish
    sleep 5
    while IFS= read -r line; do
        echo "$line"
        sleep $MESSAGE_DELAY
    done < /tmp/cpp_msg_input.txt
    # Wait for all echo responses to arrive before sending quit
    echo "Waiting for responses..." >&2
    sleep 10
    echo "quit"
) | timeout 180 ./.pio/build/native/program "$DEST_HASH" > /tmp/cpp_msg_test.log 2>&1 &
CPP_PID=$!

# Wait for completion
echo "Waiting for message exchange to complete (timeout: 180s)..."
wait $CPP_PID 2>/dev/null || true

# Give Python server time to process final messages
sleep 2

# Count results
echo ""
echo "========================================"
echo "Analyzing Results"
echo "========================================"

# Count messages received by Python server
PY_RECEIVED=$(grep -c "\[PACKET RECEIVED\]" /tmp/py_msg_test.log 2>/dev/null || echo "0")
# Count echo responses received by C++ client
CPP_RECEIVED=$(grep -c "Echo:" /tmp/cpp_msg_test.log 2>/dev/null || echo "0")
# Check link was established
LINK_ESTABLISHED=false
if grep -q "LINK ESTABLISHED" /tmp/py_msg_test.log 2>/dev/null; then
    LINK_ESTABLISHED=true
fi

echo "  Link established: $LINK_ESTABLISHED"
echo "  Messages sent: $NUM_MESSAGES"
echo "  Python received: $PY_RECEIVED"
echo "  C++ received echoes: $CPP_RECEIVED"

# Determine pass/fail
if [ "$LINK_ESTABLISHED" = true ] && [ "$PY_RECEIVED" -ge "$NUM_MESSAGES" ] && [ "$CPP_RECEIVED" -ge "$NUM_MESSAGES" ]; then
    echo ""
    echo "========================================"
    echo -e "${GREEN}MESSAGE EXCHANGE TEST PASSED${NC}"
    echo "========================================"
    echo ""
    echo "Successfully exchanged $NUM_MESSAGES messages between C++ and Python!"
    echo "This validates:"
    echo "  - Link establishment works"
    echo "  - HKDF key derivation matches"
    echo "  - Token encryption/decryption works"
    echo "  - No crashes during sustained message exchange"
    echo ""

    # Show sample messages
    echo "--- Sample C++ Output (first 5 echoes) ---"
    grep "Echo:" /tmp/cpp_msg_test.log | head -5
    echo ""
    echo "--- Sample Python Log (first 2 packets) ---"
    grep -A3 "\[PACKET RECEIVED\]" /tmp/py_msg_test.log | head -12

    # Track test result
    PASSED_TESTS+=("message")

    # Generate JSON output if requested
    if [[ -n "$JSON_OUTPUT_FILE" ]]; then
        json_generate_results "$JSON_OUTPUT_FILE"
        json_print_summary "$JSON_OUTPUT_FILE"
    fi

    exit 0
else
    echo ""
    echo "========================================"
    echo -e "${RED}MESSAGE EXCHANGE TEST FAILED${NC}"
    echo "========================================"
    echo ""
    echo "--- C++ Output ---"
    cat /tmp/cpp_msg_test.log
    echo ""
    echo "--- Python Server Log ---"
    cat /tmp/py_msg_test.log

    # Track test result
    FAILED_TESTS+=("message")

    # Generate JSON output if requested
    if [[ -n "$JSON_OUTPUT_FILE" ]]; then
        json_generate_results "$JSON_OUTPUT_FILE"
        json_print_summary "$JSON_OUTPUT_FILE"
    fi

    exit 1
fi
