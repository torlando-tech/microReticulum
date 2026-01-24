---
phase: 05
plan: 01
subsystem: documentation
tags: [synthesis, backlog, wsjf, prioritization]
dependency-graph:
  requires: [03-AUDIT.md, 04-SUMMARY.md]
  provides: [BACKLOG.md]
  affects: [future-implementation-sprints]
tech-stack:
  added: []
  patterns: [WSJF-scoring, priority-tiers]
key-files:
  created:
    - .planning/phases/05-synthesis/BACKLOG.md
  modified:
    - .planning/STATE.md
decisions:
  - id: "05-01-D1"
    title: "WSJF scoring formula"
    choice: "Severity Score / Effort Score"
    rationale: "Industry standard, prioritizes high-impact low-effort work"
metrics:
  duration: 3min
  completed: 2026-01-24
---

# Phase 05 Plan 01: Create Prioritized Backlog Summary

**Consolidated 30 stability issues from Phases 3-4 into WSJF-prioritized BACKLOG.md**

## Objective

Consolidate all findings from Phases 3-4 into a single prioritized BACKLOG.md document with WSJF scoring.

**Status:** COMPLETE

## What Was Delivered

### BACKLOG.md

A comprehensive backlog document containing:
- 30 issues (13 memory + 17 concurrency)
- WSJF scores calculated (Severity/Effort)
- Issues sorted by priority descending
- 4 priority tiers with sprint recommendations
- Detailed cards for each issue with fix guidance

### Issue Distribution

| Severity | Memory (Phase 3) | Concurrency (Phase 4) | Total |
|----------|------------------|----------------------|-------|
| Critical | 0 | 0 | 0 |
| High | 5 | 4 | 9 |
| Medium | 4 | 9 | 13 |
| Low | 4 | 4 | 8 |
| **Total** | **13** | **17** | **30** |

### Priority Distribution

| Priority | Count | WSJF Range | Sprint |
|----------|-------|------------|--------|
| P1 | 5 | >= 3.0 | Sprint 1 (immediate) |
| P2 | 11 | 1.5-2.99 | Sprint 1-2 |
| P3 | 13 | 0.5-1.49 | Sprint 2-3 |
| P4 | 1 | < 0.5 | Backlog |

### Top 5 Priority Issues

1. **MEM-M4** (WSJF 4.00): Duplicate static definition - Trivial fix
2. **CONC-H1** (WSJF 3.50): TWDT not configured - Critical for stability
3. **CONC-H2** (WSJF 3.50): LXStamper CPU hogging - UI responsiveness
4. **CONC-H3** (WSJF 3.50): Pending queues not thread-safe - Data integrity
5. **MEM-H5** (WSJF 3.50): Resource vectors resize - Fragmentation

## Deviations from Plan

### Issue Count Discrepancy

**Plan requested:** 31 issues (13 + 18)
**Delivered:** 30 issues (13 + 17)

**Reason:** Phase 4 summary claimed 18 issues but only documented 17 unique issues in the detail table. The backlog reflects the actual documented issues.

## Decisions Made

| Decision | Choice | Rationale |
|----------|--------|-----------|
| WSJF formula | Severity/Effort | Industry standard prioritization |
| Severity scores | Critical=10, High=7, Medium=4, Low=1 | CVSS-aligned scale |
| Effort scores | Trivial=1, Low=2, Medium=5, High=8 | Fibonacci-like for estimation |
| Priority thresholds | P1>=3.0, P2>=1.5, P3>=0.5, P4<0.5 | Natural groupings from WSJF |

## Verification Results

| Check | Status |
|-------|--------|
| Issue count | 30 unique issues |
| WSJF presence | All issues scored |
| Sort order | Descending by WSJF |
| Priority tiers | 4 sections present |
| Source references | Links to 03-AUDIT.md, 04-SUMMARY.md |

## Milestone Status

**MILESTONE 1: Stability Audit - COMPLETE**

### Deliverables
- `.planning/phases/05-synthesis/BACKLOG.md` - Prioritized stability backlog

### Summary
- 5 phases completed
- 14 plans executed
- ~65 minutes total execution time
- 30 issues identified and prioritized
- Ready for implementation sprints

## Next Steps

1. **Sprint 1:** Address P1 issues (TWDT, thread-safe queues, LXStamper yield)
2. **Sprint 1-2:** Address P2 issues (LVGL locks, ArduinoJson migration)
3. **Ongoing:** P3 issues addressed opportunistically during related work

## Commits

| Hash | Description |
|------|-------------|
| 93ea78c | Create prioritized stability backlog (BACKLOG.md) |
| 4579976 | Update state for milestone completion |

---

*Phase 5 (Synthesis) COMPLETE*
*Milestone 1 (Stability Audit) DELIVERED*
