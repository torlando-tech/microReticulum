#!/bin/bash
#
# Segment Resource Test Script
#
# Tests multi-segment resource transfer between Python RNS and C++ microReticulum.
#
# Test 1: Python sends 2MB resource to C++ (tests C++ receiving segments)
# Test 2: C++ sends 2MB resource to Python (tests C++ sending segments)
#
# Prerequisites:
#   - Python RNS installed (pip install rns)
#   - C++ example built (pio run -e native in examples/link)
#
# Usage: ./run_segment_test.sh [--json-output FILE]
# Note: This is an interactive/manual test script

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EXAMPLE_DIR="$SCRIPT_DIR/../../examples/link"
PYTHON_DIR="$SCRIPT_DIR/python"
CONFIG_DIR="$PYTHON_DIR/test_rns_config"

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

# Test sizes
SMALL_SIZE=1024          # 1KB (single segment)
MEDIUM_SIZE=2097152      # 2MB (2 segments)
LARGE_SIZE=5242880       # 5MB (5 segments)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test results arrays (empty for manual tests)
declare -a PASSED_TESTS
declare -a FAILED_TESTS

echo "==========================================="
echo "  Segment Resource Transfer Test"
echo "==========================================="
echo ""
echo "MAX_EFFICIENT_SIZE is now 1MB, matching Python RNS."
echo "Resources larger than 1MB will be split into segments."
echo ""

# Initialize JSON test run tracking
json_init_test_run

# Build C++ example
echo "Building C++ example..."
cd "$EXAMPLE_DIR"
pio run -e native > /dev/null 2>&1
echo "  Build complete."
echo ""

# Function to start Python server
start_python_server() {
    local size=$1
    echo "Starting Python RNS server (sending $size byte resource)..."
    cd "$PYTHON_DIR"
    python resource_server.py -c "$CONFIG_DIR" -s "$size" &
    PYTHON_PID=$!
    sleep 3

    # Get destination hash from server output (would need to parse, for now just wait)
    echo "  Python server started (PID: $PYTHON_PID)"
}

# Function to stop Python server
stop_python_server() {
    if [ -n "$PYTHON_PID" ]; then
        echo "Stopping Python server..."
        kill $PYTHON_PID 2>/dev/null || true
        wait $PYTHON_PID 2>/dev/null || true
        unset PYTHON_PID
    fi
}

# Cleanup on exit
cleanup() {
    stop_python_server
    echo ""
    echo "Cleanup complete."
}
trap cleanup EXIT

echo "==========================================="
echo "  Test Instructions"
echo "==========================================="
echo ""
echo "Run these tests in TWO terminals:"
echo ""
echo "TERMINAL 1 - Start Python server:"
echo "  cd $PYTHON_DIR"
echo "  python resource_server.py -c $CONFIG_DIR -s 2097152"
echo "  (Note the destination hash)"
echo ""
echo "TERMINAL 2 - Run C++ client:"
echo "  cd $EXAMPLE_DIR"
echo "  .pio/build/native/program <destination_hash>"
echo ""
echo "Then in the C++ client, type:"
echo "  send 2097152   (to send 2MB from C++ to Python)"
echo ""
echo "==========================================="
echo ""
echo "Or run automated test:"
echo ""

# Ask user what to do
echo "Choose test mode:"
echo "  1) Python sends 2MB to C++ (test receiving)"
echo "  2) Instructions only (manual testing)"
echo ""
read -p "Enter choice [1-2]: " choice

case $choice in
    1)
        echo ""
        echo "Test: Python → C++ (2MB, 2 segments)"
        echo ""
        start_python_server $MEDIUM_SIZE
        echo ""
        echo "Please note the destination hash from the Python server output,"
        echo "then run in another terminal:"
        echo ""
        echo "  cd $EXAMPLE_DIR"
        echo "  .pio/build/native/program <destination_hash>"
        echo ""
        echo "Press Enter when done testing..."
        read
        ;;
    2)
        echo ""
        echo "Manual testing mode. Follow the instructions above."
        ;;
    *)
        echo "Invalid choice"
        exit 1
        ;;
esac

echo ""
echo "Test session ended."

# Generate JSON output if requested (manual test - no automated results)
if [[ -n "$JSON_OUTPUT_FILE" ]]; then
    # Note: This is a manual test, so we output empty results
    # User should manually verify test success
    echo "Note: This is a manual/interactive test."
    echo "JSON output will show 0 tests (manual verification required)."
    json_generate_results "$JSON_OUTPUT_FILE"
    json_print_summary "$JSON_OUTPUT_FILE"
fi
