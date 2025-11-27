# Resource Transfer Diagnostic System

## Overview

This diagnostic system helps identify exactly where file corruption occurs during Python-to-C++ resource transfer in microReticulum. It captures intermediate states at each processing stage and compares them byte-by-byte to pinpoint the corruption source.

## Architecture

### Data Flow
```
Python Sender:
  Original → BZ2 Compress → +Random Hash → Encrypt → Split into Parts → Send

C++ Receiver:
  Receive Parts → Assemble → Decrypt → Strip Random Hash → BZ2 Decompress → Verify → Store
```

### Diagnostic Stages Captured

**Python Side:**
- `py_stage0_original_{size}.bin` - Original uncompressed data
- `py_stage1_compressed_{size}.bin` - After BZ2 compression
- `py_stage2_with_random_{size}.bin` - After adding 4-byte random_hash prefix
- `py_stage3_encrypted_{size}.bin` - After Link Token encryption
- `py_stage4_parts_{size}/part_*.bin` - Individual encrypted parts
- `py_metadata_{size}.json` - Complete metadata with hashes and sizes

**C++ Side:**
- `cpp_stage0_parts/part_*.bin` - Individual received parts
- `cpp_parts_metadata.json` - Parts metadata
- `cpp_stage1_encrypted.bin` - Assembled encrypted data
- `cpp_stage2_decrypted.bin` - After Token decryption (includes random_hash)
- `cpp_stage3_stripped.bin` - After stripping random_hash (still compressed)
- `cpp_stage4_decompressed.bin` - After BZ2 decompression
- `cpp_stage5_final.bin` - Final verified data
- `cpp_final_metadata.json` - Final metadata

## Prerequisites

### C++ Side
- PlatformIO installed
- Link example compiled:
  ```bash
  cd examples/link
  pio run -e native
  ```

### Python Side
- Python 3.7+
- RNS (Reticulum Network Stack) Python package

**Installing RNS:**

Option 1 - System package (Arch Linux):
```bash
sudo pacman -S python-rns  # If available
```

Option 2 - pip (with caution on managed systems):
```bash
pip3 install rns --break-system-packages  # Arch/managed systems
# or
pip3 install rns  # Standard systems
```

Option 3 - Virtual environment (recommended):
```bash
python3 -m venv venv
source venv/bin/activate
pip install rns
```

## Usage

### Quick Start - Automated Test Suite

Run all test sizes automatically:

```bash
cd test/test_interop
./run_diagnostic_test.sh
```

This will:
1. Test sizes: 1KB, 10KB, 100KB, 1MB, 5MB
2. For each size:
   - Start Python diagnostic server
   - Run C++ client
   - Compare all intermediate stages
   - Generate HTML report
3. Display summary of all tests
4. Generate reports in `/tmp/diagnostic_dumps/`

**View Results:**
```bash
# Open HTML reports
firefox /tmp/diagnostic_dumps/report_1048576.html  # 1MB test
firefox /tmp/diagnostic_dumps/summary.html          # All tests

# Or view in terminal
ls -lh /tmp/diagnostic_dumps/
```

### Manual Testing - Step by Step

For debugging or custom test sizes:

#### Step 1: Start Python Diagnostic Server

```bash
cd test/test_interop/python

# Test with 1MB file in diagnostic mode
python3 diagnostic_resource_server.py \
    -c ./test_rns_config \
    --diagnostic \
    --size 1048576 \
    --output-dir /tmp/diagnostic_dumps
```

**Note the destination hash** from the output:
```
Destination hash: a1b2c3d4e5f6...
```

#### Step 2: Run C++ Client

In a **new terminal**:

```bash
cd examples/link

# Use the destination hash from step 1
./.pio/build/native/program <destination_hash>
```

Wait for the transfer to complete. You should see debug output showing each stage being saved.

#### Step 3: Compare Results

In a **third terminal**:

```bash
cd test/test_interop

python3 diagnostic_compare.py \
    --size 1048576 \
    --python-dir /tmp/diagnostic_dumps \
    --cpp-dir /tmp \
    --output /tmp/diagnostic_dumps/report_1048576.html
```

#### Step 4: View Results

```bash
# HTML report (recommended)
firefox /tmp/diagnostic_dumps/report_1048576.html

# Or terminal output
cat /tmp/diagnostic_dumps/report_1048576.html
```

## Understanding Results

### All Stages Match (No Corruption)

```
COMPARISON SUMMARY
✓ ALL STAGES MATCH - No corruption detected!
```

This means the data is identical at every stage. If you're seeing bugs, the issue is likely in:
- Hash calculation logic
- Metadata handling
- Resource advertisement parsing

### Corruption Detected

The report will show exactly where corruption first appears:

```
COMPARISON SUMMARY
✗ CORRUPTION DETECTED
First corruption point: Decompressed

Decompressed Data: ✗ DIFFER at byte 935 (0x000003a7)
  Python byte: 0x41
  C++ byte:    0x00
```

**Corruption Stage → Likely Root Cause:**

| Stage | Meaning | Likely Issue |
|-------|---------|--------------|
| **Parts** | Individual parts differ | Network transport or part assembly bug |
| **Encrypted** | Assembled encrypted data differs | Part concatenation issue |
| **Decrypted+Random** | Decrypted data differs | Token decryption mismatch or key issue |
| **Compressed** | Compressed data differs | random_hash stripping issue |
| **Decompressed** | Decompressed data differs | **BZ2 decompression bug** (likely your current issue) |
| **Final** | Final data differs | Hash verification logic issue |

### HTML Report Features

The HTML report provides:

1. **Summary Table**: All stages with MATCH/DIFFER status
2. **Hex Dumps**: For any differing stage, shows:
   - Exact byte offset of first difference
   - 64 bytes of context before/after
   - Side-by-side Python vs C++ data
   - Highlighted differing bytes

**Example Hex Dump:**
```
Difference at offset 0x000003a7 (935 decimal)

Python (File 1):
000003a0: 48 45 4c 4c 4f 5f 52 45  54 49 43 55 4c 55 4d 5f  |HELLO_RETICULUM_|
000003b0: [41] 42 43 44 45 46 47 48  49 4a 4b 4c 4d 4e 4f 50  |ABCDEFGHIJKLMNOP|

C++ (File 2):
000003a0: 48 45 4c 4c 4f 5f 52 45  54 49 43 55 4c 55 4d 5f  |HELLO_RETICULUM_|
000003b0: [00] 42 43 44 45 46 47 48  49 4a 4b 4c 4d 4e 4f 50  |.BCDEFGHIJKLMNOP|
```

The `[41]` vs `[00]` shows Python sent 'A' (0x41) but C++ received 0x00.

## Test Pattern

The diagnostic system uses a repeating pattern for easy corruption detection:

```python
b"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
```

This pattern repeats throughout the file, making it easy to:
- Visually spot corruption in hex dumps
- Identify if corruption follows block boundaries
- Detect pattern shifts or truncation

## File Locations

### Temporary Files
All diagnostic files are stored in `/tmp/` for C++ and `/tmp/diagnostic_dumps/` for Python.

```bash
# Clean up between tests
rm -rf /tmp/diagnostic_dumps/*
rm -f /tmp/cpp_stage*.bin /tmp/cpp_*.json /tmp/cpp_stage0_parts
```

### Log Files
- `/tmp/py_diagnostic_{size}.log` - Python server logs
- `/tmp/cpp_diagnostic_{size}.log` - C++ client logs
- `/tmp/compare_{size}.log` - Comparison script logs

## Troubleshooting

### "ModuleNotFoundError: No module named 'RNS'"

Install RNS Python package:
```bash
pip3 install rns --break-system-packages  # Arch/managed systems
# or
pip3 install rns  # Standard systems
```

### "Link example not built"

```bash
cd examples/link
pio run -e native
```

### "Could not get destination hash"

The Python server failed to start. Check the log:
```bash
cat /tmp/py_diagnostic_*.log
```

Common issues:
- RNS not installed
- Config directory doesn't exist
- Port already in use

### "Resource transfer did not complete"

Check both logs:
```bash
tail -50 /tmp/py_diagnostic_*.log
tail -50 /tmp/cpp_diagnostic_*.log
```

Common issues:
- Destination hash mismatch
- Network interface not configured
- Timeout too short for large files

### "MISSING" status in comparison

A file wasn't created. Check:
- Python server saved intermediate stages (look for "Saved:" in log)
- C++ client reached the assembly stage (look for "Resource::assemble" in log)
- File permissions on /tmp/

## Advanced Usage

### Custom Test Sizes

```bash
# Python server
python3 diagnostic_resource_server.py -c ./test_rns_config \
    --diagnostic --size 2097152  # 2MB

# Then run C++ client and compare with --size 2097152
```

### Multiple Runs

Run the same test multiple times to check for consistency:

```bash
for i in {1..5}; do
    echo "Run $i..."
    ./run_diagnostic_test.sh
    mv /tmp/diagnostic_dumps/report_1048576.html \
       /tmp/diagnostic_dumps/report_1048576_run${i}.html
done
```

### Generate Summary

After running multiple tests:

```bash
python3 generate_summary.py /tmp/diagnostic_dumps
firefox /tmp/diagnostic_dumps/summary.html
```

## Implementation Details

### C++ Debug Output

Added in `src/Resource.cpp`:
- Line ~988: Save individual parts before assembly
- Line ~1062: Save decompressed data after BZ2
- Line ~1077: Save final verified data

### Python Manual Replication

The diagnostic server manually replicates RNS Resource creation:
1. Generate test data with pattern
2. Compress with `bz2.compress(data, compresslevel=9)`
3. Generate 4-byte random_hash
4. Prepend random_hash to compressed data
5. Encrypt with `link.encrypt()`
6. Split into parts by SDU (`link.mdu`)
7. Calculate resource_hash: `SHA256(original_data + random_hash)`
8. Save all intermediate stages
9. Create actual RNS Resource to send

This gives us ground truth for each stage to compare against C++ receiver.

## Next Steps After Diagnosis

Once you identify the corruption stage:

1. **Review the relevant code** for that stage
2. **Check for:**
   - Buffer overflows or underflows
   - Incorrect buffer sizes
   - Endianness issues
   - Off-by-one errors
   - Uninitialized memory
3. **Add more detailed logging** to that specific function
4. **Test with different sizes** to see if it's size-dependent
5. **Compare with Python RNS implementation** for that stage

## Files Created

```
test/test_interop/
├── python/
│   └── diagnostic_resource_server.py    # Python diagnostic server
├── diagnostic_compare.py                 # Comparison script
├── generate_summary.py                   # Summary report generator
├── run_diagnostic_test.sh               # Automated test runner
└── DIAGNOSTIC_README.md                  # This file

src/
└── Resource.cpp                          # Modified with debug dumps
```

## Support

For issues or questions about this diagnostic system, check:
- Debug output in `/tmp/*.log` files
- HTML reports for detailed hex dumps
- C++ compilation warnings/errors
- Python RNS compatibility

## License

Same as microReticulum project.
