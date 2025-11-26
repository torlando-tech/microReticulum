#!/bin/bash
# Link Interop Test Script
# Tests C++ microReticulum Link establishment with Python RNS server

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PYTHON_DIR="$SCRIPT_DIR/python"
LINK_EXAMPLE_DIR="$PROJECT_ROOT/examples/link"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================"
echo "Link Interop Test"
echo "========================================"

# Cleanup function
cleanup() {
    echo "Cleaning up..."
    pkill -f "link_server.py" 2>/dev/null || true
    rm -f /tmp/py_link_test.log /tmp/cpp_link_test.log
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

# Clean up any existing Python servers
pkill -f "link_server.py" 2>/dev/null || true
sleep 1

# Remove old storage to get fresh identities
rm -rf "$PYTHON_DIR/test_rns_config/storage" 2>/dev/null

# Start Python server
echo -n "Starting Python RNS server... "
cd "$PYTHON_DIR"
python -u link_server.py -c ./test_rns_config > /tmp/py_link_test.log 2>&1 &
PY_PID=$!
sleep 3

# Get destination hash from server output
DEST_HASH=$(grep "Destination hash:" /tmp/py_link_test.log | head -1 | awk '{print $NF}')
if [ -z "$DEST_HASH" ]; then
    echo -e "${RED}FAIL${NC}"
    echo "Could not get destination hash from Python server"
    cat /tmp/py_link_test.log
    exit 1
fi
echo -e "${GREEN}OK${NC} (hash: $DEST_HASH)"

# Run C++ client
echo -n "Running C++ Link client... "
cd "$LINK_EXAMPLE_DIR"
timeout 20 ./.pio/build/native/program "$DEST_HASH" > /tmp/cpp_link_test.log 2>&1 &
CPP_PID=$!

# Wait for link establishment (max 15 seconds)
for i in {1..15}; do
    sleep 1
    if grep -q "Link established with server" /tmp/cpp_link_test.log 2>/dev/null; then
        break
    fi
done

# Check results
CPP_LINK_OK=false
PY_LINK_OK=false

if grep -q "Link established with server" /tmp/cpp_link_test.log 2>/dev/null; then
    CPP_LINK_OK=true
fi

if grep -q "LINK ESTABLISHED" /tmp/py_link_test.log 2>/dev/null; then
    PY_LINK_OK=true
fi

# Kill the C++ client
kill $CPP_PID 2>/dev/null || true

if [ "$CPP_LINK_OK" = true ] && [ "$PY_LINK_OK" = true ]; then
    echo -e "${GREEN}OK${NC}"
    echo ""
    echo "========================================"
    echo -e "${GREEN}LINK INTEROP TEST PASSED${NC}"
    echo "========================================"
    echo ""
    echo "C++ client successfully established link with Python RNS server"
    echo ""
    echo "--- C++ Output ---"
    head -15 /tmp/cpp_link_test.log
    echo ""
    echo "--- Python Server Log ---"
    grep -A5 "LINK ESTABLISHED" /tmp/py_link_test.log 2>/dev/null || tail -10 /tmp/py_link_test.log
    exit 0
else
    echo -e "${RED}FAIL${NC}"
    echo ""
    echo "========================================"
    echo -e "${RED}LINK INTEROP TEST FAILED${NC}"
    echo "========================================"
    echo ""
    echo "C++ Link OK: $CPP_LINK_OK"
    echo "Python Link OK: $PY_LINK_OK"
    echo ""
    echo "--- C++ Output ---"
    cat /tmp/cpp_link_test.log
    echo ""
    echo "--- Python Server Log ---"
    cat /tmp/py_link_test.log
    exit 1
fi
