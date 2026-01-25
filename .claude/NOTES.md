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

BytesPool tier sizing tuned from runtime observation (2026-01-24):

| Tier | Size | Slots | PSRAM | Internal RAM |
|------|------|-------|-------|--------------|
| Tiny | 64B | 512 | 32KB | ~8KB metadata |
| Small | 256B | 8 | 2KB | ~128B |
| Medium | 512B | 8 | 4KB | ~128B |
| Large | 1024B | 8 | 8KB | ~128B |

Total: ~46KB PSRAM + ~8.5KB internal RAM

**Critical constraint:** Each tiny slot uses ~16 bytes internal RAM for vector metadata + stack pointer. Internal RAM is precious on ESP32-S3.

**History:**
- Started at 512 slots (quiet network showed 281 peak)
- Reduced to 384 slots - caused 100% exhaustion on busy networks
- Increased to 768 slots - caused device crashes (internal RAM exhaustion)
- Reverted to 512 slots - balanced for memory pressure

**Known destinations pool:** Increased from 192 to 512 slots (~72KB in PSRAM).

---
*Last updated: 2026-01-24*
