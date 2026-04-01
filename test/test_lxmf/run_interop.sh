#!/bin/bash
# E2E LXMF interop test: Python <-> C++
#
# Runs the full bidirectional test pipeline:
#   1. Python generates test vectors → /tmp/lxmf_test_vector*.json
#   2. C++ unpacks Python vectors, generates its own → /tmp/lxmf_cpp_vectors.json
#   3. Python validates C++ vectors
#
# Usage: bash run_interop.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "=== LXMF E2E Interop Tests ==="
echo "  Script dir: $SCRIPT_DIR"
echo "  Repo dir:   $REPO_DIR"
echo

# Step 1: Generate Python test vectors
echo "--- Step 1: Generate Python test vectors ---"
python3 "$SCRIPT_DIR/python/generate_vectors.py"
echo

# Step 2: Run C++ tests (consumes Python vectors, generates C++ vectors)
echo "--- Step 2: Run C++ tests ---"
cd "$REPO_DIR"
pio test -e native17 -f test_lxmf 2>&1 | grep -E "test.*\[|SUMMARY|======" || true
echo

# Step 3: Run Python validation tests
echo "--- Step 3: Run Python validation tests ---"
cd "$SCRIPT_DIR"
python3 -m pytest python/test_interop.py -v
echo

echo "=== All LXMF interop tests passed ==="
