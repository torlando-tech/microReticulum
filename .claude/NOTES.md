# Claude Code Project Notes

## Serial Monitoring

**DO NOT use `stty` directly to configure serial ports.** This can freeze the Claude Code terminal.

Instead, for ESP32 serial monitoring use:
- `pio device monitor` (if terminal supports it)
- `screen /dev/ttyACM0 115200` (in a separate terminal)
- `minicom -D /dev/ttyACM0 -b 115200`

The issue occurred when using:
```bash
stty -F /dev/ttyACM0 115200 raw -echo && timeout 35 cat /dev/ttyACM0
```

This command works but leaves the terminal in a bad state for Claude Code.

## Pool Tuning

BytesPool tier sizing was adjusted based on real-world traffic observation:
- Most allocations are 1-64 bytes (hash fields, small Bytes objects)
- Added TIER_TINY (64 bytes, 512 slots) - high traffic tier for burst handling
- Other tiers: SMALL (256B, 24 slots), MEDIUM (512B, 16 slots), LARGE (1024B, 16 slots)
- Total memory footprint: ~78KB (32KB tiny + 6KB small + 8KB medium + 16KB large + metadata)

**Observations after tuning:**
- No pool exhaustion warnings with 512 tiny slots
- Heap stable at ~61-62KB free
- Fragmentation steady at ~19-20%

**Known issue:** "Known destinations pool" (separate from BytesPool) fills at 192 entries on busy networks.

---
*Last updated: 2026-01-24*
