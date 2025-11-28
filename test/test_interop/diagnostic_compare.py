#!/usr/bin/env python3
"""
Diagnostic comparison script for identifying resource transfer corruption.

Compares intermediate stages from Python sender and C++ receiver byte-by-byte
to pinpoint exactly where corruption occurs.

Usage:
    python diagnostic_compare.py --size 1048576 \
        --python-dir /tmp/diagnostic_dumps \
        --cpp-dir /tmp \
        --output /tmp/diagnostic_dumps/report_1048576.html
"""

import os
import sys
import json
import hashlib
import argparse
from datetime import datetime


def read_binary(filepath):
    """Read binary file, return None if doesn't exist."""
    if not os.path.exists(filepath):
        return None
    with open(filepath, 'rb') as f:
        return f.read()


def format_hex_dump(data, base_offset, highlight_offset=None, context_bytes=64):
    """Format binary data as hex dump with optional highlighting."""
    lines = []

    start = max(0, highlight_offset - context_bytes) if highlight_offset is not None else 0
    end = min(len(data), highlight_offset + context_bytes) if highlight_offset is not None else len(data)

    for i in range(start, end, 16):
        offset = base_offset + i
        hex_bytes = []
        ascii_bytes = []

        for j in range(16):
            abs_pos = i + j
            if abs_pos < end and abs_pos < len(data):
                byte = data[abs_pos]
                hex_str = f"{byte:02x}"

                # Highlight the differing byte
                if highlight_offset is not None and abs_pos == highlight_offset:
                    hex_str = f"[{hex_str}]"

                hex_bytes.append(hex_str)
                ascii_bytes.append(chr(byte) if 32 <= byte < 127 else '.')
            else:
                hex_bytes.append("  ")
                ascii_bytes.append(" ")

        hex_part = " ".join(hex_bytes[:8]) + "  " + " ".join(hex_bytes[8:])
        ascii_part = "".join(ascii_bytes)
        lines.append(f"{offset:08x}: {hex_part}  |{ascii_part}|")

    return "\n".join(lines)


def generate_hex_context(data1, data2, offset, context_bytes=64):
    """Generate hex dump showing context around difference."""
    lines = []
    lines.append(f"Difference at offset 0x{offset:08x} ({offset} decimal)")

    start = max(0, offset - context_bytes)
    end = min(len(data1), len(data2), offset + context_bytes)
    lines.append(f"Context: {start} to {end} (showing {end-start} bytes)\n")

    lines.append("Python (File 1):")
    lines.append(format_hex_dump(data1, start, offset, context_bytes))

    lines.append("\nC++ (File 2):")
    lines.append(format_hex_dump(data2, start, offset, context_bytes))

    return "\n".join(lines)


def compare_files(file1, file2, stage_name):
    """Compare two binary files byte-by-byte."""
    print(f"\n{'='*60}")
    print(f"Comparing {stage_name}")
    print(f"{'='*60}")
    print(f"  File 1: {file1}")
    print(f"  File 2: {file2}")

    if not os.path.exists(file1):
        print(f"  Result: MISSING - {file1}")
        return {"status": "MISSING", "missing": file1}
    if not os.path.exists(file2):
        print(f"  Result: MISSING - {file2}")
        return {"status": "MISSING", "missing": file2}

    data1 = read_binary(file1)
    data2 = read_binary(file2)

    hash1 = hashlib.sha256(data1).hexdigest()
    hash2 = hashlib.sha256(data2).hexdigest()

    print(f"  Size 1: {len(data1)} bytes")
    print(f"  Size 2: {len(data2)} bytes")
    print(f"  SHA256 1: {hash1[:16]}...")
    print(f"  SHA256 2: {hash2[:16]}...")

    if data1 == data2:
        print(f"  Result: ✓ EXACT MATCH")
        return {"status": "MATCH", "size": len(data1), "sha256": hash1}

    # Find first difference
    min_len = min(len(data1), len(data2))
    first_diff = None

    for i in range(min_len):
        if data1[i] != data2[i]:
            first_diff = i
            break

    if first_diff is not None:
        print(f"  Result: ✗ DIFFER at byte {first_diff} (0x{first_diff:08x})")
        print(f"    Python byte: 0x{data1[first_diff]:02x}")
        print(f"    C++ byte:    0x{data2[first_diff]:02x}")
        context = generate_hex_context(data1, data2, first_diff)
        return {
            "status": "DIFFER",
            "first_diff_offset": first_diff,
            "context": context,
            "size1": len(data1),
            "size2": len(data2),
            "sha256_1": hash1,
            "sha256_2": hash2
        }
    else:
        print(f"  Result: ✗ SIZE MISMATCH")
        return {
            "status": "SIZE_MISMATCH",
            "size1": len(data1),
            "size2": len(data2),
            "sha256_1": hash1,
            "sha256_2": hash2
        }


def compare_parts_directories(py_dir, cpp_dir):
    """Compare all parts in two directories."""
    print(f"\n{'='*60}")
    print(f"Comparing parts directories")
    print(f"{'='*60}")
    print(f"  Python: {py_dir}")
    print(f"  C++:    {cpp_dir}")

    if not os.path.exists(py_dir):
        print(f"  Result: MISSING - {py_dir}")
        return {"status": "MISSING", "missing": py_dir}
    if not os.path.exists(cpp_dir):
        print(f"  Result: MISSING - {cpp_dir}")
        return {"status": "MISSING", "missing": cpp_dir}

    py_parts = sorted([f for f in os.listdir(py_dir) if f.startswith("part_")])
    cpp_parts = sorted([f for f in os.listdir(cpp_dir) if f.startswith("part_")])

    print(f"  Python parts: {len(py_parts)}")
    print(f"  C++ parts:    {len(cpp_parts)}")

    if len(py_parts) != len(cpp_parts):
        print(f"  Result: ✗ PART COUNT MISMATCH")
        return {"status": "COUNT_MISMATCH", "py_count": len(py_parts), "cpp_count": len(cpp_parts)}

    all_match = True
    mismatches = []

    for i, (py_part, cpp_part) in enumerate(zip(py_parts, cpp_parts)):
        py_path = os.path.join(py_dir, py_part)
        cpp_path = os.path.join(cpp_dir, cpp_part)

        py_data = read_binary(py_path)
        cpp_data = read_binary(cpp_path)

        if py_data != cpp_data:
            all_match = False
            mismatches.append({
                "part_index": i,
                "py_size": len(py_data),
                "cpp_size": len(cpp_data)
            })
            print(f"  Part {i}: ✗ MISMATCH (py: {len(py_data)} bytes, cpp: {len(cpp_data)} bytes)")
        else:
            print(f"  Part {i}: ✓ MATCH ({len(py_data)} bytes)")

    if all_match:
        print(f"  Result: ✓ ALL PARTS MATCH")
        return {"status": "MATCH", "part_count": len(py_parts)}
    else:
        print(f"  Result: ✗ {len(mismatches)} PARTS DIFFER")
        return {"status": "PARTS_DIFFER", "mismatches": mismatches}


def generate_html_report(results, output_path, test_size):
    """Generate comprehensive HTML report."""
    html = f"""<!DOCTYPE html>
<html>
<head>
    <title>Diagnostic Comparison Report - {test_size} bytes</title>
    <style>
        body {{ font-family: monospace; padding: 20px; background: #f5f5f5; }}
        .container {{ max-width: 1200px; margin: 0 auto; background: white; padding: 20px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }}
        h1 {{ color: #333; border-bottom: 3px solid #4CAF50; padding-bottom: 10px; }}
        h2 {{ color: #555; margin-top: 30px; border-bottom: 2px solid #ddd; padding-bottom: 5px; }}
        h3 {{ color: #666; margin-top: 20px; }}
        .match {{ color: green; font-weight: bold; }}
        .differ {{ color: red; font-weight: bold; }}
        .missing {{ color: orange; font-weight: bold; }}
        pre {{ background: #f4f4f4; padding: 15px; overflow-x: auto; border: 1px solid #ddd; font-size: 12px; line-height: 1.4; }}
        table {{ border-collapse: collapse; width: 100%; margin: 20px 0; }}
        th, td {{ border: 1px solid #ddd; padding: 12px; text-align: left; }}
        th {{ background-color: #4CAF50; color: white; }}
        tr:nth-child(even) {{ background-color: #f9f9f9; }}
        .highlight {{ background-color: yellow; font-weight: bold; }}
        .info {{ background: #e3f2fd; padding: 15px; border-left: 4px solid #2196F3; margin: 15px 0; }}
        .error {{ background: #ffebee; padding: 15px; border-left: 4px solid #f44336; margin: 15px 0; }}
        .success {{ background: #e8f5e9; padding: 15px; border-left: 4px solid #4CAF50; margin: 15px 0; }}
    </style>
</head>
<body>
    <div class="container">
        <h1>Resource Transfer Diagnostic Report</h1>
        <div class="info">
            <strong>Test Size:</strong> {test_size} bytes<br>
            <strong>Generated:</strong> {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
        </div>

        <h2>Summary</h2>
        <table>
            <tr><th>Stage</th><th>Status</th><th>Details</th></tr>
"""

    # Add summary rows
    for stage_name, result in results.items():
        status = result.get('status', 'UNKNOWN')
        css_class = 'match' if status == 'MATCH' else 'differ' if status == 'DIFFER' else 'missing'

        details = ""
        if status == 'MATCH':
            details = f"Size: {result.get('size', result.get('part_count', 'N/A'))} " + ("parts" if 'part_count' in result else "bytes")
        elif status == 'DIFFER':
            details = f"First diff at byte 0x{result.get('first_diff_offset', 0):08x}"
        elif status == 'SIZE_MISMATCH':
            details = f"Size1: {result.get('size1', 'N/A')}, Size2: {result.get('size2', 'N/A')}"
        elif status == 'MISSING':
            details = f"Missing: {os.path.basename(result.get('missing', 'N/A'))}"
        elif status == 'PARTS_DIFFER':
            details = f"{len(result.get('mismatches', []))} parts differ"
        elif status == 'COUNT_MISMATCH':
            details = f"Py: {result.get('py_count', 'N/A')}, C++: {result.get('cpp_count', 'N/A')}"

        html += f"<tr><td>{stage_name}</td><td class='{css_class}'>{status}</td><td>{details}</td></tr>\n"

    html += """
        </table>
"""

    # Determine overall result
    all_match = all(r.get('status') == 'MATCH' for r in results.values())
    if all_match:
        html += """
        <div class="success">
            <strong>✓ ALL STAGES MATCH</strong><br>
            No corruption detected! All intermediate stages are identical between Python and C++.
        </div>
"""
    else:
        # Find first failing stage
        stage_order = ["Parts", "Encrypted", "Decrypted+Random", "Compressed", "Decompressed", "Final"]
        first_fail = None
        for stage in stage_order:
            if stage in results and results[stage].get('status') != 'MATCH':
                first_fail = stage
                break

        html += f"""
        <div class="error">
            <strong>✗ CORRUPTION DETECTED</strong><br>
            First corruption appears in stage: <strong>{first_fail or 'Unknown'}</strong><br>
            See detailed comparisons below for hex dumps and analysis.
        </div>
"""

    # Add detailed comparisons
    html += "<h2>Detailed Comparisons</h2>\n"

    for stage_name, result in results.items():
        if result.get('status') == 'DIFFER' and 'context' in result:
            html += f"<h3>{stage_name}</h3>\n"
            html += f"<pre>{result['context']}</pre>\n"

    html += """
    </div>
</body>
</html>
"""

    with open(output_path, 'w') as f:
        f.write(html)

    print(f"\n{'='*60}")
    print(f"HTML report generated: {output_path}")
    print(f"{'='*60}")


def main():
    parser = argparse.ArgumentParser(
        description="Diagnostic file comparison for resource transfer debugging"
    )
    parser.add_argument("-s", "--size", type=int, required=True,
                        help="Test data size in bytes")
    parser.add_argument("-p", "--python-dir", default="/tmp/diagnostic_dumps",
                        help="Python dumps directory (default: /tmp/diagnostic_dumps)")
    parser.add_argument("-c", "--cpp-dir", default="/tmp",
                        help="C++ dumps directory (default: /tmp)")
    parser.add_argument("-o", "--output",
                        help="Output HTML report path")
    args = parser.parse_args()

    size = args.size
    py_dir = args.python_dir
    cpp_dir = args.cpp_dir

    print(f"\n{'='*60}")
    print(f"DIAGNOSTIC COMPARISON - {size} bytes")
    print(f"{'='*60}")
    print(f"  Python dir: {py_dir}")
    print(f"  C++ dir:    {cpp_dir}")
    print(f"{'='*60}")

    results = {}

    # Compare parts
    results["Parts"] = compare_parts_directories(
        os.path.join(py_dir, f"py_stage4_parts_{size}"),
        os.path.join(cpp_dir, "cpp_stage0_parts")
    )

    # Compare encrypted data
    results["Encrypted"] = compare_files(
        os.path.join(py_dir, f"py_stage3_encrypted_{size}.bin"),
        os.path.join(cpp_dir, "cpp_stage1_encrypted.bin"),
        "Encrypted Data"
    )

    # Compare decrypted data (with random_hash)
    results["Decrypted+Random"] = compare_files(
        os.path.join(py_dir, f"py_stage2_with_random_{size}.bin"),
        os.path.join(cpp_dir, "cpp_stage2_decrypted.bin"),
        "Decrypted Data (with random_hash)"
    )

    # Compare compressed data (after stripping random_hash)
    results["Compressed"] = compare_files(
        os.path.join(py_dir, f"py_stage1_compressed_{size}.bin"),
        os.path.join(cpp_dir, "cpp_stage3_stripped.bin"),
        "Compressed Data"
    )

    # Compare decompressed data
    results["Decompressed"] = compare_files(
        os.path.join(py_dir, f"py_stage0_original_{size}.bin"),
        os.path.join(cpp_dir, "cpp_stage4_decompressed.bin"),
        "Decompressed Data"
    )

    # Compare final data
    results["Final"] = compare_files(
        os.path.join(py_dir, f"py_stage0_original_{size}.bin"),
        os.path.join(cpp_dir, "cpp_stage5_final.bin"),
        "Final Verified Data"
    )

    # Generate HTML report
    if args.output:
        output_path = args.output
    else:
        output_path = os.path.join(py_dir, f"report_{size}.html")

    generate_html_report(results, output_path, size)

    # Print summary
    print(f"\n{'='*60}")
    print("COMPARISON SUMMARY")
    print(f"{'='*60}")

    all_match = all(r.get('status') == 'MATCH' for r in results.values())

    if all_match:
        print("✓ ALL STAGES MATCH - No corruption detected!")
        print(f"\nReport: {output_path}")
        return 0
    else:
        print("✗ CORRUPTION DETECTED - See report for details")

        # Find first failing stage
        stage_order = ["Parts", "Encrypted", "Decrypted+Random", "Compressed", "Decompressed", "Final"]
        for stage in stage_order:
            if stage in results and results[stage].get('status') != 'MATCH':
                print(f"\nFirst corruption point: {stage}")
                print(f"Status: {results[stage].get('status')}")
                break

        print(f"\nDetailed report: {output_path}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
