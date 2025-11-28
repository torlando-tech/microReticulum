#!/bin/bash
# Resource Transfer Diagnostic Test Runner
# Tests Python-to-C++ resource transfer with intermediate stage dumps
# to identify exactly where corruption occurs
#
# Usage: ./run_diagnostic_test.sh [--json-output FILE]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PYTHON_DIR="$SCRIPT_DIR/python"
LINK_EXAMPLE_DIR="$PROJECT_ROOT/examples/link"
DUMP_DIR="/tmp/diagnostic_dumps"

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

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test sizes: 1KB, 10KB, 100KB, 1MB, 5MB
SIZES=(1024 10240 102400 1048576 5242880)

# Test results arrays
declare -a PASSED_TESTS
declare -a FAILED_TESTS

echo "========================================"
echo "Resource Transfer Diagnostic Test"
echo "========================================"

# Initialize JSON test run tracking
json_init_test_run

# Cleanup function
cleanup() {
    echo "Cleaning up..."
    pkill -f "diagnostic_resource_server.py" 2>/dev/null || true
    pkill -f "program" 2>/dev/null || true
    sleep 1
}
trap cleanup EXIT

# Check prerequisites
echo -n "Checking prerequisites... "
if ! command -v python3 &> /dev/null; then
    echo -e "${RED}FAIL${NC}"
    echo "Python3 not found"
    exit 1
fi

if [ ! -f "$LINK_EXAMPLE_DIR/.pio/build/native/program" ]; then
    echo -e "${YELLOW}Building link example...${NC}"
    cd "$LINK_EXAMPLE_DIR"
    pio run -e native
    if [ $? -ne 0 ]; then
        echo -e "${RED}FAIL${NC}"
        echo "Failed to build link example"
        exit 1
    fi
fi
echo -e "${GREEN}OK${NC}"

# Make comparison script executable
chmod +x "$SCRIPT_DIR/diagnostic_compare.py"

# Create dump directory
mkdir -p "$DUMP_DIR"

# Run tests for each size
for SIZE in "${SIZES[@]}"; do
    echo ""
    echo "========================================="
    echo -e "${BLUE}Testing size: $SIZE bytes${NC}"
    echo "========================================="

    # Clean up old dumps
    echo "Cleaning previous dumps..."
    rm -rf /tmp/cpp_stage*.bin /tmp/cpp_*.json /tmp/cpp_stage0_parts 2>/dev/null || true
    rm -f /tmp/py_diagnostic_*.log /tmp/cpp_diagnostic_*.log 2>/dev/null || true

    # Clean Python storage for fresh identity
    rm -rf "$PYTHON_DIR/test_rns_config/storage" 2>/dev/null || true

    # Start Python diagnostic server
    echo -n "Starting Python diagnostic server... "
    cd "$PYTHON_DIR"
    python3 -u diagnostic_resource_server.py \
        -c ./test_rns_config \
        --diagnostic \
        --size $SIZE \
        --output-dir "$DUMP_DIR" \
        > /tmp/py_diagnostic_${SIZE}.log 2>&1 &
    PY_PID=$!
    sleep 4

    # Check if server started successfully
    if ! ps -p $PY_PID > /dev/null; then
        echo -e "${RED}FAIL${NC}"
        echo "Python server failed to start"
        echo "--- Python Log ---"
        cat /tmp/py_diagnostic_${SIZE}.log
        FAILED=$((FAILED + 1))
        continue
    fi

    # Get destination hash
    DEST_HASH=$(grep "Destination hash:" /tmp/py_diagnostic_${SIZE}.log | head -1 | awk '{print $NF}')
    if [ -z "$DEST_HASH" ]; then
        echo -e "${RED}FAIL${NC}"
        echo "Could not get destination hash"
        echo "--- Python Log ---"
        cat /tmp/py_diagnostic_${SIZE}.log
        kill $PY_PID 2>/dev/null || true
        FAILED=$((FAILED + 1))
        continue
    fi
    echo -e "${GREEN}OK${NC} (hash: ${DEST_HASH:0:16}...)"

    # Wait for server to save diagnostic files
    sleep 2

    # Run C++ client
    echo -n "Running C++ client... "
    cd "$LINK_EXAMPLE_DIR"
    timeout 60 ./.pio/build/native/program "$DEST_HASH" \
        > /tmp/cpp_diagnostic_${SIZE}.log 2>&1 &
    CPP_PID=$!

    # Wait for resource transfer completion (max 45 seconds)
    TRANSFER_COMPLETE=false
    for i in {1..45}; do
        sleep 1
        if grep -q "Resource transfer completed successfully" /tmp/cpp_diagnostic_${SIZE}.log 2>/dev/null; then
            TRANSFER_COMPLETE=true
            break
        fi
        if grep -q "Resource transfer FAILED" /tmp/cpp_diagnostic_${SIZE}.log 2>/dev/null; then
            break
        fi
        if grep -q "Hash verification failed" /tmp/cpp_diagnostic_${SIZE}.log 2>/dev/null; then
            break
        fi
    done

    # Kill processes
    kill $CPP_PID 2>/dev/null || true
    kill $PY_PID 2>/dev/null || true
    sleep 1

    if [ "$TRANSFER_COMPLETE" = false ]; then
        echo -e "${RED}FAIL${NC}"
        echo "Resource transfer did not complete"
        echo "--- Python Log (last 20 lines) ---"
        tail -20 /tmp/py_diagnostic_${SIZE}.log
        echo "--- C++ Log (last 20 lines) ---"
        tail -20 /tmp/cpp_diagnostic_${SIZE}.log
        FAILED_TESTS+=("diagnostic_${SIZE}b")
        continue
    fi

    echo -e "${GREEN}OK${NC}"

    # Wait for all files to be written
    sleep 2

    # Run comparison script
    echo -n "Running comparison analysis... "
    cd "$SCRIPT_DIR"
    python3 diagnostic_compare.py \
        --size $SIZE \
        --python-dir "$DUMP_DIR" \
        --cpp-dir "/tmp" \
        --output "$DUMP_DIR/report_${SIZE}.html" \
        > /tmp/compare_${SIZE}.log 2>&1

    COMPARE_RESULT=$?

    if [ $COMPARE_RESULT -eq 0 ]; then
        echo -e "${GREEN}PASS - All stages match${NC}"
        PASSED_TESTS+=("diagnostic_${SIZE}b")
    else
        echo -e "${RED}FAIL - Corruption detected${NC}"
        echo "See report: $DUMP_DIR/report_${SIZE}.html"
        echo "--- Comparison Summary ---"
        cat /tmp/compare_${SIZE}.log | grep -A 10 "COMPARISON SUMMARY"
        FAILED_TESTS+=("diagnostic_${SIZE}b")
    fi
done

# Generate summary
echo ""
echo "========================================="
echo "DIAGNOSTIC TEST SUMMARY"
echo "========================================="
echo "Total tests: ${#SIZES[@]}"
echo -e "Passed: ${GREEN}${#PASSED_TESTS[@]}${NC}"
echo -e "Failed: ${RED}${#FAILED_TESTS[@]}${NC}"
echo ""
echo "Reports available in: $DUMP_DIR"
ls -lh "$DUMP_DIR"/*.html 2>/dev/null || echo "No reports generated"
echo "========================================="

# Generate JSON output if requested
if [[ -n "$JSON_OUTPUT_FILE" ]]; then
    json_generate_results "$JSON_OUTPUT_FILE"
    json_print_summary "$JSON_OUTPUT_FILE"
fi

if [ ${#FAILED_TESTS[@]} -eq 0 ]; then
    echo -e "\n${GREEN}All tests passed!${NC}\n"
    exit 0
else
    echo -e "\n${RED}Some tests failed. Check reports for details.${NC}\n"
    exit 1
fi
