#!/bin/bash
#
# Unified microReticulum interoperability test runner
#
# Runs all test suites in dependency order and generates a summary report.
#
# Test execution order (dependency-based):
#   1. link      - Foundation (must pass to continue)
#   2. message   - 100+ message exchange
#   3. channel   - Channel messaging
#   4. buffer    - Buffer stream I/O
#   5. segment   - Multi-segment resources (optional)
#   6. diagnostic - Comprehensive diagnostics (optional)
#
# Usage: ./run_all_tests.sh [options] [test_names...]
# Options:
#   -v, --verbose    Show detailed output
#   -q, --quick      Skip slow tests (segment, diagnostic)
#   -l, --list       List available tests
#   -h, --help       Show this help
#
# Examples:
#   ./run_all_tests.sh              # Run all tests
#   ./run_all_tests.sh -q           # Quick mode (skip slow tests)
#   ./run_all_tests.sh link channel # Run only link and channel tests

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOG_DIR="$SCRIPT_DIR/logs/full_$(date '+%Y%m%d_%H%M%S')"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Options
VERBOSE=false
QUICK_MODE=false

# Test results
declare -a PASSED_SUITES
declare -a FAILED_SUITES
declare -a SKIPPED_SUITES
START_TIME=$(date +%s)

# Test registry: name -> script:timeout:description
declare -A TESTS=(
    ["link"]="run_link_test.sh:60:Link establishment"
    ["message"]="run_message_test.sh:300:100+ message exchange"
    ["channel"]="run_channel_tests.sh:120:Channel messaging"
    ["buffer"]="run_buffer_tests.sh:180:Buffer stream I/O"
    ["segment"]="run_segment_test.sh:300:Multi-segment resources"
    ["diagnostic"]="run_diagnostic_test.sh:600:Comprehensive diagnostics"
)

# Slow tests (skipped in quick mode)
SLOW_TESTS=("segment" "diagnostic")

# Test order (dependencies)
TEST_ORDER=("link" "message" "channel" "buffer" "segment" "diagnostic")

usage() {
    echo "Usage: $0 [options] [test_names...]"
    echo ""
    echo "Options:"
    echo "  -v, --verbose    Show detailed output"
    echo "  -q, --quick      Skip slow tests (segment, diagnostic)"
    echo "  -l, --list       List available tests"
    echo "  -h, --help       Show this help"
    echo ""
    echo "Tests:"
    for name in "${TEST_ORDER[@]}"; do
        IFS=':' read -r script timeout desc <<< "${TESTS[$name]}"
        printf "  %-12s %s (timeout: %ss)\n" "$name" "$desc" "$timeout"
    done
    echo ""
    echo "Examples:"
    echo "  $0              # Run all tests"
    echo "  $0 -q           # Quick mode (skip slow tests)"
    echo "  $0 link channel # Run only link and channel tests"
}

list_tests() {
    echo "Available tests:"
    echo ""
    for name in "${TEST_ORDER[@]}"; do
        IFS=':' read -r script timeout desc <<< "${TESTS[$name]}"
        local slow_marker=""
        for slow in "${SLOW_TESTS[@]}"; do
            if [[ "$name" == "$slow" ]]; then
                slow_marker=" (slow)"
                break
            fi
        done
        printf "  %-12s %-30s timeout: %3ss%s\n" "$name" "$desc" "$timeout" "$slow_marker"
    done
}

log_info() {
    echo -e "${YELLOW}[INFO]${NC} $1"
}

log_suite_start() {
    echo -e "${CYAN}[$1]${NC} Running $2..."
}

log_suite_pass() {
    echo -e "${GREEN}[$1]${NC} PASSED ($2s)"
    PASSED_SUITES+=("$1")
}

log_suite_fail() {
    echo -e "${RED}[$1]${NC} FAILED ($2)"
    FAILED_SUITES+=("$1")
}

log_suite_skip() {
    echo -e "${YELLOW}[$1]${NC} SKIPPED ($2)"
    SKIPPED_SUITES+=("$1")
}

# Setup clean environment
setup_clean_environment() {
    log_info "Setting up clean test environment..."

    # Kill stale processes
    pkill -f "link_server.py" 2>/dev/null || true
    pkill -f "channel_test_server.py" 2>/dev/null || true
    pkill -f "resource_server.py" 2>/dev/null || true
    pkill -f "program.*[0-9a-f]{32}" 2>/dev/null || true
    sleep 2

    # Create log directory
    mkdir -p "$LOG_DIR"

    # Log environment info
    {
        echo "Test run started: $(date)"
        echo "Git commit: $(cd "$SCRIPT_DIR/../.." && git rev-parse --short HEAD 2>/dev/null || echo 'unknown')"
        echo "Platform: $(uname -sr)"
        echo "Python: $(/usr/bin/python --version 2>&1)"
        echo ""
    } > "$LOG_DIR/environment.txt"

    log_info "Logs will be saved to: $LOG_DIR"
}

# Run a test suite
run_suite() {
    local name="$1"
    local script timeout desc

    # Get test info
    if [[ -z "${TESTS[$name]}" ]]; then
        log_suite_fail "$name" "Unknown test"
        return 1
    fi

    IFS=':' read -r script timeout desc <<< "${TESTS[$name]}"

    # Check if test script exists
    if [[ ! -x "$SCRIPT_DIR/$script" ]]; then
        log_suite_fail "$name" "Script not found: $script"
        return 1
    fi

    log_suite_start "$name" "$desc"

    local start_time=$(date +%s)
    local log_file="$LOG_DIR/${name}.log"

    # Run the test with timeout
    if timeout "$timeout" "$SCRIPT_DIR/$script" > "$log_file" 2>&1; then
        local end_time=$(date +%s)
        local duration=$((end_time - start_time))
        log_suite_pass "$name" "$duration"
        return 0
    else
        local exit_code=$?
        local end_time=$(date +%s)
        local duration=$((end_time - start_time))

        if [[ $exit_code -eq 124 ]]; then
            log_suite_fail "$name" "Timeout after ${timeout}s"
        else
            log_suite_fail "$name" "Exit code $exit_code (see $log_file)"
        fi

        # Show tail of log on failure if verbose
        if $VERBOSE; then
            echo "--- Last 20 lines of $log_file ---"
            tail -20 "$log_file"
            echo "---"
        fi

        return 1
    fi
}

# Print summary
print_summary() {
    local end_time=$(date +%s)
    local total_duration=$((end_time - START_TIME))

    echo ""
    echo "========================================"
    echo "       FULL TEST SUMMARY"
    echo "========================================"
    echo ""

    if [[ ${#PASSED_SUITES[@]} -gt 0 ]]; then
        echo -e "${GREEN}PASSED (${#PASSED_SUITES[@]}):${NC}"
        for suite in "${PASSED_SUITES[@]}"; do
            echo "  - $suite"
        done
    fi

    if [[ ${#FAILED_SUITES[@]} -gt 0 ]]; then
        echo ""
        echo -e "${RED}FAILED (${#FAILED_SUITES[@]}):${NC}"
        for suite in "${FAILED_SUITES[@]}"; do
            echo "  - $suite"
        done
    fi

    if [[ ${#SKIPPED_SUITES[@]} -gt 0 ]]; then
        echo ""
        echo -e "${YELLOW}SKIPPED (${#SKIPPED_SUITES[@]}):${NC}"
        for suite in "${SKIPPED_SUITES[@]}"; do
            echo "  - $suite"
        done
    fi

    echo ""
    local total=$((${#PASSED_SUITES[@]} + ${#FAILED_SUITES[@]} + ${#SKIPPED_SUITES[@]}))
    echo "Total: $total suites, ${#PASSED_SUITES[@]} passed, ${#FAILED_SUITES[@]} failed, ${#SKIPPED_SUITES[@]} skipped"
    echo "Duration: ${total_duration}s"
    echo "Logs: $LOG_DIR/"
    echo ""

    # Save summary to file
    {
        echo "Test Summary"
        echo "============"
        echo "Passed: ${#PASSED_SUITES[@]}"
        echo "Failed: ${#FAILED_SUITES[@]}"
        echo "Skipped: ${#SKIPPED_SUITES[@]}"
        echo "Duration: ${total_duration}s"
        echo ""
        echo "Passed suites: ${PASSED_SUITES[*]}"
        echo "Failed suites: ${FAILED_SUITES[*]}"
        echo "Skipped suites: ${SKIPPED_SUITES[*]}"
    } > "$LOG_DIR/summary.txt"

    if [[ ${#FAILED_SUITES[@]} -gt 0 ]]; then
        return 1
    fi
    return 0
}

# Parse arguments
parse_args() {
    local tests_to_run=()

    while [[ $# -gt 0 ]]; do
        case "$1" in
            -v|--verbose)
                VERBOSE=true
                shift
                ;;
            -q|--quick)
                QUICK_MODE=true
                shift
                ;;
            -l|--list)
                list_tests
                exit 0
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            -*)
                echo "Unknown option: $1"
                usage
                exit 1
                ;;
            *)
                tests_to_run+=("$1")
                shift
                ;;
        esac
    done

    # If specific tests requested, use those; otherwise use all
    if [[ ${#tests_to_run[@]} -gt 0 ]]; then
        TEST_ORDER=("${tests_to_run[@]}")
    fi
}

# Main
main() {
    parse_args "$@"

    echo "========================================"
    echo "  microReticulum Full Test Suite"
    echo "  $(date '+%Y-%m-%d %H:%M:%S')"
    echo "========================================"
    echo ""

    setup_clean_environment

    local link_passed=true

    for test_name in "${TEST_ORDER[@]}"; do
        # Check if this is a slow test and we're in quick mode
        if $QUICK_MODE; then
            for slow in "${SLOW_TESTS[@]}"; do
                if [[ "$test_name" == "$slow" ]]; then
                    log_suite_skip "$test_name" "quick mode"
                    continue 2
                fi
            done
        fi

        # Critical dependency: if link test failed, skip remaining
        if [[ "$test_name" != "link" ]] && ! $link_passed; then
            log_suite_skip "$test_name" "link test failed"
            continue
        fi

        # Run the test
        if ! run_suite "$test_name"; then
            # Track if link test failed (critical)
            if [[ "$test_name" == "link" ]]; then
                link_passed=false
            fi
        fi

        # Small delay between suites for cleanup
        sleep 2
    done

    print_summary
    exit $?
}

main "$@"
