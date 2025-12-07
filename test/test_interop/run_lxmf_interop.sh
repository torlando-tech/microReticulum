#!/bin/bash
# LXMF Interoperability Test
# Tests that C++ and Python LXMF implementations can communicate

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "========================================"
echo "LXMF Interoperability Test"
echo "========================================"
echo ""

# Step 1: Run C++ unit tests
echo "Step 1: Running C++ LXMF unit tests..."
cd "$REPO_DIR"
pio test -e native17 -f test_lxmf
echo ""

# Step 2: Verify Python can unpack test vectors
echo "Step 2: Verifying Python can unpack test vectors..."
cd "$REPO_DIR"

# Read test vectors and verify each one
VECTORS_FILE="$REPO_DIR/test/test_interop/vectors/lxmf_vectors.json"
if [ -f "$VECTORS_FILE" ]; then
    PACKED_MESSAGES=$(python3 -c "import json; data=json.load(open('$VECTORS_FILE')); print(' '.join([v['packed'] for v in data['vectors']]))")

    for packed in $PACKED_MESSAGES; do
        echo "  Verifying message: ${packed:0:32}..."
        /usr/bin/python "$REPO_DIR/test/test_interop/python/lxmf_verify.py" "$packed" > /dev/null 2>&1
        if [ $? -eq 0 ]; then
            echo "    OK"
        else
            echo "    FAILED"
            exit 1
        fi
    done
else
    echo "  Test vectors file not found, generating..."
    /usr/bin/python "$REPO_DIR/test/test_interop/python/generate_lxmf_vectors.py" > "$VECTORS_FILE"
fi
echo ""

# Step 3: Test format verification
echo "Step 3: Running Python LXMF format verification..."
/usr/bin/python "$REPO_DIR/test/test_interop/python/lxmf_verify.py"
echo ""

echo "========================================"
echo "All LXMF interoperability tests PASSED"
echo "========================================"
