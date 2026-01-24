# Phase 5: Synthesis - Research

**Researched:** 2026-01-24
**Domain:** Technical debt backlog consolidation and prioritization
**Confidence:** HIGH

## Summary

This research addresses "What do I need to know to PLAN the synthesis phase well?" Phase 5 is a documentation/consolidation phase, not a technical implementation phase. The work involves extracting 31 issues from Phase 3 (Memory Audit) and Phase 4 (Concurrency Audit), normalizing severity ratings, adding fix complexity estimates, and producing a prioritized backlog ordered by severity/effort ratio.

Key findings:
- The existing audit reports already use consistent severity levels (Critical/High/Medium/Low)
- Standard prioritization uses WSJF (Weighted Shortest Job First): Priority = Impact / Effort
- For embedded systems, severity is primarily determined by system stability impact (crash, deadlock, memory exhaustion)
- Fix effort should use relative sizing (T-shirt: Trivial/Low/Medium/High) rather than absolute time estimates

**Primary recommendation:** Create a single BACKLOG.md document that consolidates all 31 issues with unified severity ratings, effort estimates, and WSJF priority scores, ordered by score descending.

## Standard Stack

This is a documentation phase - no libraries or tools beyond standard markdown authoring.

### Core

| Tool | Purpose | Why Standard |
|------|---------|--------------|
| Markdown | Backlog document format | Universal, git-trackable, parseable |
| Tables | Issue catalog with sortable columns | Clear comparison of severity/effort/priority |
| YAML Frontmatter | Machine-readable metadata | Enables tooling integration |

### Supporting

| Tool | When to Use |
|------|-------------|
| GitHub Issues | If future tracking in issue tracker is desired |
| CSV export | If spreadsheet analysis needed |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Single BACKLOG.md | Separate issue files | Single file is easier to scan/sort; separate files better for large backlogs |
| Markdown tables | JSON/YAML list | Tables are human-readable; structured data better for automation |

## Architecture Patterns

### Recommended Document Structure

```
.planning/phases/05-synthesis/
├── 05-RESEARCH.md        # This file
├── 05-01-PLAN.md         # Single plan for consolidation
├── BACKLOG.md            # Final deliverable
└── 05-VERIFICATION.md    # Verification report
```

### Pattern 1: Issue Catalog Table

**What:** Master table of all issues with key attributes
**When to use:** Primary view for stakeholders to scan all issues
**Example:**

```markdown
## Issue Catalog

| ID | Source | Severity | Effort | WSJF | Title | Status |
|----|--------|----------|--------|------|-------|--------|
| MEM-H1 | Phase 3 | High | Medium | 6.7 | Bytes COW copy allocation | Open |
| CONC-H1 | Phase 4 | High | Low | 10.0 | TWDT not configured | Open |
```

**Sorting:** Primary by WSJF descending (highest priority first)

### Pattern 2: Issue Detail Cards

**What:** Detailed specification for each issue
**When to use:** When implementers need full context
**Example:**

```markdown
### MEM-H1: Bytes COW copy allocation

**Source:** Phase 3 Memory Audit (03-AUDIT.md)
**Severity:** High
**Effort:** Medium (1-2 days)
**WSJF Score:** 6.7

**Problem:**
Per-packet writes create heap fragmentation via exclusiveData() COW copies.

**Location:**
- `src/Bytes.cpp:42` - exclusiveData() function

**Fix Recommendation:**
Implement Bytes pool or arena allocator for packet-lifetime allocations.

**Verification:**
- Fragmentation % stable during sustained packet processing
- No increase in per-packet allocation count
```

### Pattern 3: Priority Groupings

**What:** Issues grouped by priority tier (P1/P2/P3/P4)
**When to use:** Sprint planning, resource allocation
**Example:**

```markdown
## Priority 1 - Fix Immediately (Critical stability impact)

| ID | Issue | Effort |
|----|-------|--------|
| CONC-H1 | TWDT not configured | Low |

## Priority 2 - Fix Before Production (High stability impact)

| ID | Issue | Effort |
|----|-------|--------|
| CONC-H2 | BLE pending queues thread safety | Low |
| MEM-H1 | Bytes COW allocation | Medium |
```

### Anti-Patterns to Avoid

- **Copying issue text verbatim:** Synthesize into consistent format, don't copy/paste
- **Changing severity without justification:** If re-rating, document why
- **Mixing implementation work with synthesis:** This phase produces the backlog; implementation is Phase 6+
- **Including instrumentation issues:** Phases 1-2 added instrumentation, not issues to fix

## Don't Hand-Roll

Problems that look simple but have established solutions:

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Priority calculation | Custom formula | WSJF (Impact/Effort) | Industry standard, understood by teams |
| Severity levels | Project-specific scale | Critical/High/Medium/Low | Universal, maps to CVSS-like systems |
| Effort estimation | Time-based estimates | Relative sizing (T-shirt) | More reliable, focuses on complexity |
| Document format | Custom schema | Markdown with tables | Universal tooling support |

**Key insight:** The audit reports already use Critical/High/Medium/Low severity consistently. The synthesis work is normalization and calculation, not inventing new systems.

## Severity Rating System

### Embedded Systems Severity Definitions

Based on embedded firmware best practices, severity should reflect stability impact:

| Level | Score | Definition | Examples |
|-------|-------|------------|----------|
| Critical | 10 | System crash, data loss, security breach | Stack overflow, memory corruption, deadlock with no recovery |
| High | 7 | Degraded operation, potential crash over time | Memory leak, thread starvation, resource exhaustion |
| Medium | 4 | Reduced performance or reliability | Missing mutex protection, unbounded cache growth |
| Low | 1 | Minor issue, no stability impact | Code style, documentation, deprecated API usage |

**Source:** Adapted from [CVSS Severity Levels](https://www.webasha.com/blog/understanding-cvss-severity-levels-and-ratings-complete-guide) and [Barr Group Embedded Bug Classification](https://barrgroup.com/embedded-systems/how-to/top-ten-nasty-firmware-bugs)

### Current Audit Severity Mapping

Both Phase 3 and Phase 4 reports use consistent severity levels:

| Phase 3 (Memory) | Phase 4 (Concurrency) | Unified Score |
|------------------|----------------------|---------------|
| Critical (0 issues) | Critical (0 issues) | 10 |
| High (5 issues) | High (4 issues) | 7 |
| Medium (4 issues) | Medium (9 issues) | 4 |
| Low (4 issues) | Low (5 issues) | 1 |

**Finding:** No re-rating needed. The audits used consistent criteria.

## Effort Estimation System

### Relative Sizing Scale

Use T-shirt sizing for consistency and to avoid false precision:

| Size | Score | Typical Scope | Examples |
|------|-------|---------------|----------|
| Trivial | 1 | Single line change, no testing | Add `reserve()` call, fix typo |
| Low | 2 | Single file, straightforward fix | Add mutex guard, migrate API |
| Medium | 5 | Multiple files, moderate complexity | Convert to make_shared, add timeout handling |
| High | 8 | Architectural change, significant testing | Implement object pool, redesign allocation pattern |

**Source:** Adapted from [Atlassian Story Points Guide](https://www.atlassian.com/agile/project-management/estimation)

### Effort Mapping from Audit Reports

The audit reports use informal effort descriptions. Normalize as follows:

| Audit Description | Normalized Size |
|-------------------|-----------------|
| "Trivial" | Trivial (1) |
| "0.25 days", "0.5 days" | Low (2) |
| "Low", "Low-Medium", "1 day" | Low (2) |
| "Medium", "1-2 days", "2-3 days" | Medium (5) |
| "Medium-High", "High", "3-5 days" | High (8) |

## Priority Calculation

### WSJF Formula

**WSJF Score = Severity Score / Effort Score**

Higher scores = higher priority (fix high-impact, low-effort issues first)

| Severity | Effort | WSJF | Priority Tier |
|----------|--------|------|---------------|
| High (7) | Low (2) | 3.5 | P1 |
| High (7) | Medium (5) | 1.4 | P2 |
| Medium (4) | Low (2) | 2.0 | P2 |
| Medium (4) | Medium (5) | 0.8 | P3 |
| Low (1) | Trivial (1) | 1.0 | P3 |
| Low (1) | High (8) | 0.125 | P4 |

**Source:** [WSJF and RICE Prioritization](https://community.taiga.io/t/methods-of-prioritization-in-agile-ii-scoring-formulas-and-rice/1893)

### Priority Tier Definitions

| Tier | WSJF Range | Action |
|------|------------|--------|
| P1 | >= 3.0 | Fix immediately, blocks other work |
| P2 | 1.5 - 2.99 | Fix before production release |
| P3 | 0.5 - 1.49 | Fix when convenient |
| P4 | < 0.5 | Defer or document only |

## Issue Consolidation

### Phase 3 Issues (13 total)

From `.planning/phases/03-memory-allocation-audit/03-AUDIT.md`:

**High Severity (5):**
- H1: Bytes COW copy allocation (Bytes.cpp:42)
- H2: Packet Object allocation (Packet.cpp:22)
- H3: Packet 9 Bytes members overhead (Packet.h:280-328)
- H4: PacketReceipt allocation (Packet.cpp:812)
- H5: Resource vectors resize during transfers (ResourceData.h:59,68)

**Medium Severity (4):**
- M1: Bytes newData make_shared pattern (Bytes.cpp:11)
- M2: PacketReceipt default constructor allocates (Packet.h:51)
- M3: DynamicJsonDocument in Persistence (Persistence.h:475)
- M4: Duplicate static definition (Persistence.h:475, Persistence.cpp:7)

**Low Severity (4):**
- L1: toHex string reallocation (Bytes.cpp:170)
- L2: BLEFragmenter temporary vector (BLEFragmenter.cpp:38)
- L3: Fixed document size may be suboptimal (Type.h:18)
- L4: std::map in ChannelData (ChannelData.h:347)

### Phase 4 Issues (18 total)

From `.planning/phases/04-concurrency-audit/04-SUMMARY.md`:

**High Severity (4):**
- TASK-01: TWDT not configured for application tasks
- TASK-02: LXStamper CPU hogging (yields every 100 rounds)
- NIMBLE-01: Pending queues not thread-safe
- NIMBLE-02: Shutdown during active operations

**Medium Severity (9):**
- LVGL-01: SettingsScreen constructor/destructor missing LVGL_LOCK
- LVGL-02: ComposeScreen constructor/destructor missing LVGL_LOCK
- LVGL-03: AnnounceListScreen constructor/destructor missing LVGL_LOCK
- NIMBLE-03: Soft reset does not release NimBLE internal state
- NIMBLE-04: Connection mutex timeout may lose cache updates
- NIMBLE-05: Discovered devices cache unbounded
- TASK-03: LVGL mutex uses portMAX_DELAY
- TASK-04: Audio I2S blocking write with portMAX_DELAY
- MUTEX-01: No formal mutex ordering enforcement

**Low Severity (5):**
- NIMBLE-06: Native GAP handler uses volatile for complex state
- NIMBLE-07: Undocumented 50ms delay in error recovery
- TASK-05: Link watchdog TODO not implemented
- MUTEX-02: portMAX_DELAY masks deadlock detection

**Note:** Issue count in summary (18) differs from detailed list (17). Verification during synthesis will reconcile.

### Total Issue Count

| Severity | Phase 3 | Phase 4 | Total |
|----------|---------|---------|-------|
| Critical | 0 | 0 | 0 |
| High | 5 | 4 | 9 |
| Medium | 4 | 9 | 13 |
| Low | 4 | 5 | 9 |
| **Total** | **13** | **18** | **31** |

## Backlog Document Format

### Recommended Structure

```markdown
# microReticulum Stability Backlog

**Generated:** [date]
**Source:** Phases 3-4 Audits
**Total Issues:** 31

## Executive Summary

[3-4 sentences: issue breakdown, top priorities, recommended sprint allocation]

## Priority Summary

| Priority | Count | Effort Range | Sprint Recommendation |
|----------|-------|--------------|----------------------|
| P1 | X | Low-Medium | Sprint 1 |
| P2 | X | Low-High | Sprint 1-2 |
| P3 | X | Low-Medium | Sprint 2-3 |
| P4 | X | Any | Backlog |

## Issue Catalog

[Master table with all issues, sortable columns]

## Issues by Priority

### Priority 1 - Fix Immediately

[Grouped issue cards with full details]

### Priority 2 - Fix Before Production

[...]

## Appendix: Issue Details

[Full detail cards for each issue, alphabetically by ID]
```

### YAML Frontmatter (Optional)

For tooling integration:

```yaml
---
type: backlog
version: 1.0
generated: 2026-01-24
source_phases: [3, 4]
issue_count: 31
severity_distribution:
  critical: 0
  high: 9
  medium: 13
  low: 9
---
```

## Common Pitfalls

### Pitfall 1: Over-engineering the Backlog

**What goes wrong:** Creating complex tracking systems, multiple files, automated tooling
**Why it happens:** Desire to make "professional" artifact
**How to avoid:** Single BACKLOG.md is the deliverable; keep it simple
**Warning signs:** Spending time on format instead of content

### Pitfall 2: Changing Severity Without Evidence

**What goes wrong:** Re-rating issues based on gut feel rather than criteria
**Why it happens:** Different mental model than auditors
**How to avoid:** If changing severity, document specific justification
**Warning signs:** Many issues moved to different severity levels

### Pitfall 3: Including Implementation Details

**What goes wrong:** Writing fix implementation in backlog instead of recommendations
**Why it happens:** Wanting to be helpful to future implementers
**How to avoid:** Keep recommendations at "what to do" level, not "how to do it"
**Warning signs:** Code snippets longer than 10 lines in backlog

### Pitfall 4: Merging Similar Issues Prematurely

**What goes wrong:** Combining issues that seem related but have different fixes
**Why it happens:** Desire for cleaner backlog
**How to avoid:** Keep issues separate unless they are truly the same fix
**Warning signs:** "This fix also addresses..." appearing in many issues

## Code Examples

Not applicable for this documentation phase. The deliverable is a markdown document, not code.

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Severity-only ordering | WSJF (severity/effort) | ~2015 (SAFe adoption) | Prioritizes quick wins |
| Time-based estimates | Relative sizing | ~2010 (Agile adoption) | More reliable prioritization |
| Separate tech debt backlog | Unified backlog | ~2018 (industry consensus) | Better cross-prioritization |

**Deprecated/outdated:**
- Numeric severity (1-5) without clear definitions: Use named levels with criteria
- Separate "tech debt" tracking: Integrate with main backlog

## Open Questions

### 1. Should implementation phases be planned in this phase?

**What we know:** DLVR-01 requires prioritized backlog with fix recommendations
**What's unclear:** Whether Phase 5 should outline future implementation phases
**Recommendation:** Keep scope to backlog creation; future phases are separate roadmap decisions

### 2. Issue count discrepancy in Phase 4

**What we know:** Summary says 18 issues, detailed list shows 17 items
**What's unclear:** Whether an issue was miscounted or mislabeled
**Recommendation:** Reconcile during synthesis by counting from source documents

### 3. Grouping related issues

**What we know:** Some issues are related (e.g., 3 LVGL constructor issues)
**What's unclear:** Whether to combine into single backlog item
**Recommendation:** Keep separate for tracking, note relationship in description

## Sources

### Primary (HIGH confidence)

- `.planning/phases/03-memory-allocation-audit/03-AUDIT.md` - Memory audit findings (13 issues)
- `.planning/phases/04-concurrency-audit/04-SUMMARY.md` - Concurrency audit findings (18 issues)
- `.planning/REQUIREMENTS.md` - DLVR-01 requirement specification

### Secondary (MEDIUM confidence)

- [Atlassian Story Points Guide](https://www.atlassian.com/agile/project-management/estimation) - Effort estimation best practices
- [WSJF and RICE Prioritization](https://community.taiga.io/t/methods-of-prioritization-in-agile-ii-scoring-formulas-and-rice/1893) - Priority calculation formulas
- [CVSS Severity Levels Guide](https://www.webasha.com/blog/understanding-cvss-severity-levels-and-ratings-complete-guide) - Severity rating definitions
- [Technical Debt Prioritization ScienceDirect](https://www.sciencedirect.com/science/article/pii/S016412122030220X) - Academic research on TD prioritization

### Tertiary (LOW confidence)

- [Barr Group Embedded Bugs](https://barrgroup.com/embedded-systems/how-to/top-ten-nasty-firmware-bugs) - Embedded bug classification (WebSearch only)
- [Technical Debt Records Template](https://othercode.io/blog/technical-debt-records) - Document format inspiration (WebSearch only)

## Metadata

**Confidence breakdown:**
- Severity system: HIGH - Existing audits use consistent levels, CVSS-based criteria well-established
- Effort estimation: HIGH - T-shirt sizing is industry standard, audit reports provide baseline estimates
- Priority calculation: HIGH - WSJF is SAFe standard, simple and widely understood
- Document format: MEDIUM - Recommended structure based on best practices, may need adjustment

**Research date:** 2026-01-24
**Valid until:** Indefinite (methodology research, not library versions)

---

*Generated by gsd-phase-researcher*
*Phase 5: Synthesis Research*
