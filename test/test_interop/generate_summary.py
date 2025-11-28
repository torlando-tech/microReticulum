#!/usr/bin/env python3
"""
Generate summary report from all diagnostic test results.

Usage:
    python generate_summary.py /tmp/diagnostic_dumps
"""

import os
import sys
import glob
from datetime import datetime


def parse_report(report_path):
    """Extract key information from HTML report."""
    with open(report_path, 'r') as f:
        content = f.read()

    # Extract test size from filename
    filename = os.path.basename(report_path)
    if 'report_' in filename:
        size_str = filename.replace('report_', '').replace('.html', '')
        try:
            size = int(size_str)
        except ValueError:
            size = "Unknown"
    else:
        size = "Unknown"

    # Determine status from content
    if 'ALL STAGES MATCH' in content:
        status = 'PASS'
        first_corruption = 'N/A'
    else:
        status = 'FAIL'
        # Try to extract first corruption stage
        if 'First corruption appears in stage:' in content:
            start = content.find('First corruption appears in stage:')
            end = content.find('</strong>', start)
            if end > start:
                first_corruption = content[start:end].split('<strong>')[-1].strip()
            else:
                first_corruption = 'Unknown'
        else:
            first_corruption = 'Unknown'

    return {
        'size': size,
        'status': status,
        'first_corruption': first_corruption,
        'report_file': report_path
    }


def generate_summary(dump_dir):
    """Generate summary of all test runs."""

    # Find all report HTML files
    report_pattern = os.path.join(dump_dir, 'report_*.html')
    report_files = glob.glob(report_pattern)

    if not report_files:
        print(f"No report files found in {dump_dir}")
        print(f"Looking for: {report_pattern}")
        return

    print(f"Found {len(report_files)} report(s)")

    # Collect results
    all_results = []
    for report_file in sorted(report_files):
        result = parse_report(report_file)
        all_results.append(result)
        print(f"  {os.path.basename(report_file)}: {result['status']}")

    # Generate HTML summary
    html = f"""<!DOCTYPE html>
<html>
<head>
    <title>Diagnostic Test Summary</title>
    <style>
        body {{ font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }}
        .container {{ max-width: 1200px; margin: 0 auto; background: white; padding: 20px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }}
        h1 {{ color: #333; border-bottom: 3px solid #4CAF50; padding-bottom: 10px; }}
        table {{ border-collapse: collapse; width: 100%; margin: 20px 0; }}
        th, td {{ border: 1px solid #ddd; padding: 12px; text-align: left; }}
        th {{ background-color: #4CAF50; color: white; }}
        tr:nth-child(even) {{ background-color: #f9f9f9; }}
        .pass {{ background-color: #d4edda; color: #155724; font-weight: bold; }}
        .fail {{ background-color: #f8d7da; color: #721c24; font-weight: bold; }}
        a {{ color: #2196F3; text-decoration: none; }}
        a:hover {{ text-decoration: underline; }}
        .info {{ background: #e3f2fd; padding: 15px; border-left: 4px solid #2196F3; margin: 15px 0; }}
    </style>
</head>
<body>
    <div class="container">
        <h1>Resource Transfer Diagnostic Summary</h1>
        <div class="info">
            <strong>Generated:</strong> {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}<br>
            <strong>Total Tests:</strong> {len(all_results)}<br>
            <strong>Passed:</strong> {sum(1 for r in all_results if r['status'] == 'PASS')}<br>
            <strong>Failed:</strong> {sum(1 for r in all_results if r['status'] == 'FAIL')}
        </div>

        <table>
            <tr>
                <th>Test Size</th>
                <th>Status</th>
                <th>First Corruption Stage</th>
                <th>Report</th>
            </tr>
"""

    for result in all_results:
        size = result['size']
        if isinstance(size, int):
            if size >= 1048576:
                size_display = f"{size / 1048576:.1f} MB"
            elif size >= 1024:
                size_display = f"{size / 1024:.0f} KB"
            else:
                size_display = f"{size} bytes"
        else:
            size_display = str(size)

        status = result['status']
        css_class = 'pass' if status == 'PASS' else 'fail'

        first_corruption = result['first_corruption']
        report_link = os.path.basename(result['report_file'])

        html += f"""
            <tr>
                <td>{size_display}</td>
                <td class='{css_class}'>{status}</td>
                <td>{first_corruption}</td>
                <td><a href='{report_link}'>View Report</a></td>
            </tr>
        """

    html += """
        </table>
    </div>
</body>
</html>
"""

    summary_path = os.path.join(dump_dir, 'summary.html')
    with open(summary_path, 'w') as f:
        f.write(html)

    print(f"\nSummary report generated: {summary_path}")

    # Print console summary
    print("\n" + "="*60)
    print("SUMMARY")
    print("="*60)
    passed = sum(1 for r in all_results if r['status'] == 'PASS')
    failed = sum(1 for r in all_results if r['status'] == 'FAIL')
    print(f"Total:  {len(all_results)}")
    print(f"Passed: {passed}")
    print(f"Failed: {failed}")

    if failed > 0:
        print("\nFailed tests:")
        for result in all_results:
            if result['status'] == 'FAIL':
                size = result['size']
                corruption = result['first_corruption']
                print(f"  - {size} bytes: Corruption in {corruption}")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        dump_dir = sys.argv[1]
    else:
        dump_dir = "/tmp/diagnostic_dumps"

    if not os.path.exists(dump_dir):
        print(f"Error: Directory {dump_dir} does not exist")
        sys.exit(1)

    generate_summary(dump_dir)
