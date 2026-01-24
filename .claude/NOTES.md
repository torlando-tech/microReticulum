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
- Added TIER_TINY (64 bytes, 48 slots) to handle this traffic
- Reduced pressure on TIER_SMALL (256 bytes, now 24 slots)

---
*Last updated: 2026-01-24*
