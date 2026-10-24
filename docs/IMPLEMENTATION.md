# Implementation status and working notes

Written to be picked up by a different session, or on a different machine.
Read `PLATFORM.md` first: it holds the hardware facts that make or break any
change here.

## Branch

All work is on **`c47-app`** (fork `yoeljacobsen/ExistOS-For-HP39GII`).
`main` is untouched upstream.

## Status

| Component | State |
| :--- | :--- |
| C47 calculator app | **working** — launches, computes, light mode, full key map |
| OSLoader power-off | **fixed** — works on battery; stands by on USB (hardware limit) |
| OSLoader diagnostics | **fixed** — fault dumps and app output reach the USB console |
| Emu48 | **removed** — see "Why Emu48 was abandoned" |
| DB48X RPN/RPL app | **core cross-compiles** — platform layer outstanding |

## Build and flash

```bash
./build.sh firmware        # -> build/OSLoader/OSLoader.sb, build/System/ExistOS.sys
./flash.sh sys             # System only
./flash.sh all             # both; required after any OSLoader/ change
```

Toolchain: GNU Arm Embedded **10.3-2021.10** in `~/.local/opt/` or `/opt/`
(ABI-compatible with the prebuilt `Libs/*.libcpp`). `99-hp39gii.rules` must be
installed for USB recovery to work:

```bash
sudo cp 99-hp39gii.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Prebuilt application cores are committed as `Libs/*.libc` / `*.libcpp`, not
`*.a` — `.gitignore` excludes `*.a`, and a fresh clone must build. Follow that
convention for any new core library.

## Serial console — the only real diagnostic

Use a **pyserial** monitor, not `cat`/`stty`: opening the port makes Linux
cdc-acm report 9600 baud, which used to switch OSLoader away from its own log.

```python
# /tmp/c47mon.py  (port, logfile) — also polls /tmp/c47cmd and sends its contents
import serial, sys, time, os, threading
s = serial.Serial(); s.port = sys.argv[1]; s.baudrate = 115200
s.timeout = 0.3; s.dtr = False; s.rts = False; s.open()
def sender():
    while True:
        if os.path.exists('/tmp/c47cmd'):
            c = open('/tmp/c47cmd').read().strip(); os.remove('/tmp/c47cmd')
            if c: s.write(c.encode()); s.flush()
        time.sleep(0.2)
threading.Thread(target=sender, daemon=True).start()
with open(sys.argv[2], 'ab', 0) as f:
    while True:
        d = s.read(4096)
        if d: f.write(d)
```

Console commands (work in both CDC paths): `getstatus`, `pwrinfo`, `poweroff`,
`clearall`. `pwrinfo` dumps the PMU and RTC-persistent registers.

**Careful:** `pkill -f <pattern>` matches the agent's own shell when the pattern
appears in its command line. Match on `/proc/<pid>/cmdline` instead.

The user drives the keypad — key presses cannot be injected over USB, so
reproducing anything in-app needs a human at the device.

### Resolving a crash

On a page fault OSLoader prints `Access Invalid Address:<addr>` plus 16 saved
registers. `SAVED REGS[14]` is the lever:

```bash
arm-none-eabi-addr2line -f -C -e build/System/sys.elf <LR-1>
arm-none-eabi-objdump -d --start-address=<LR-0x20> --stop-address=<LR+0x10> build/System/sys.elf
```

That is how the uninitialised `sdb` hook was found; a faulting indirect call
leaves no usable stack trace.

## Recovery

| Symptom | Fix |
| :--- | :--- |
| Stuck in USB mass-storage mode | press **`Views`** (the on-screen hint) |
| Unresponsive, USB present | `ON`+`F6` (reboot) |
| `066f:3770 SigmaTel ROM Recovery` | `./flash.sh ram`, then `./flash.sh all` |
| Nothing works | pull power (no batteries ⇒ unplugging USB is a cold boot) |

## C47 notes

`System/c47/c47_existos.c` implements the DMCP API on ExistOS. Non-obvious
pieces, each of which cost a debugging round:

- **`c47_init_sdb()` must fill every `sdb` function pointer.** On a DM42 the
  firmware owns them; here nothing does, and C47 dispatches through them — an
  unset one is an indirect branch to a wild address, not a no-op. Assign through
  the *macro* name (`set_flag_clk24 = ...`), because `dmcp.h` `#define`s the bare
  field names to `(sdb.<field>)`.
- **`get_vbat()` must map onto the DM42's CR2032 scale** (3100 fresh, 2100 =
  `BAT_MINIMUM`). The HP reading is a single cell, ~1000–1600 mV, and ~180 mV on
  USB; passing it through unscaled reads as a flat cell and C47 shuts itself off.
- Swap memory is enabled before the 256 KB RAM pool is allocated.
- Task stack is 48 KB (C47 records a 14,244-byte deepest case).
- Tracing: build with `-DC47_DEBUG_TRACE` for one-shot entry traces of each DMCP
  callback, in the order C47 reaches them.

## Why Emu48 was abandoned

It worked — correct ROM, correct display, clean exit — but not usably. Measured:

    EMU48 rate: 10290 op/s, 62 miss/s, 10628 lookup/s (1 per op) shutdn=0
    VMRATE: vram=2458/s vrom=0/s nandrd=613/s

One VRAM fault per 4.2 opcodes and ~613 NAND reads/s ≈ **900 ms of flash I/O per
second**: the CPU was almost entirely blocked. Emu48 needs Port0 256 KB +
Port2 256 KB + a 33 KB ROM cache — ~570 KB of working set against 124 KB of ZRAM
behind a 40 KB resident window. At 392 MHz that rate is ~38,000 cycles per
opcode, which only page faults can account for.

Ruled out along the way, each by measurement: the ROM cache (1 lookup/opcode,
~60 misses/s), FatFs seeking (fixed with `CREATE_LINKMAP`), the display rebuild,
the speed throttle, and an `__aeabi_uidivmod` on the hot path. Dropping the
emulated Port2 to halve the footprint produced a blank screen, so the ROM
requires it. Conclusion: **no fixed-footprint emulator of a 256 KB machine fits
here.** That is what points the native app at DB48X, which sizes its heap to the
platform.

Also worth keeping: the shipped `rom.g39` was never a Saturn ROM — it is an
HP 39gs ARM firmware image (`KINPOHP39G+IMAGE`). `load_rom()` learned to say so.
A real dump (`rom.39g`, 2 M nibbles, unpacked) does work.

## DB48X port — current state

**`Libs/src/build_db48x.sh` builds the whole DB48X core for ARM926EJ-S.** All 63
sources compile (60 `src/*.cc`, the 4 fonts, and `src/dmcp/{target,sysmenu,main}.cc`),
producing a 4.1 MB `Libs/libdb48x_core.libc`. The library is *not* committed yet
because nothing links it; run the script to produce it.

Four build details that were not obvious:

- **`-std=gnu++17`, not `-std=c++17`.** `settings.h` uses `typeof(Init)` in its
  `SETTING` macro, a GNU extension; with strict ISO every one of the 55 sources
  that reaches `ids.tbl` fails.
- `-D__packed=...` and `-D__weak=...`, as for C47.
- `HELPFILE_NAME` / `HELPINDEX_NAME` are normally set by DB48X's Makefile from
  the firmware name; we set them to `/help/db48x.{md,idx}`.
- `-I src/dm42` for the MODEL-specific `main.h`.

`System/c47/dmcp.h` gained two things, both harmless to C47 (which is C):
`extern "C"` guards, since DB48X is C++ and declares the DMCP entry points with
C linkage; and a declaration of `__sysfn_read_key(int *k1, int *k2)`, the
low-level key-state reader DB48X uses for transient alpha mode.

### What the platform layer still owes DB48X

179 undefined symbols, nearly all libc/libm. Stripped of those and of symbols
C47 already implements, the real gap is:

| Area | Symbols |
| :--- | :--- |
| Power | `get_lowbat_state`, `read_power_voltage` |
| Date | `get_month_shortcut`, `get_wday_shortcut`, `julian_day` |
| UI | `handle_menu`, `msg_box`, `lcd_for_calc`, `lcd_switchFont`, `lcd_writeClr` |
| System | `set_reset_magic`, `prog_build_id`, `qspi_build_id`, `__sysfn_read_key` |
| DMCP fonts | `lcd_fonts`, `LibMonoFont{10x17,11x18,12x20,14x22,17x25,17x28}`, `SkrMono13x18`, `Free42Font`, `MenuFont`, `ErrorFont` |

The DMCP bitmap fonts are supplied by the DM42 firmware and have no counterpart
here; they need either real data or substitutes mapped onto the ExistOS fonts.
Everything else is small.

## Next steps

1. Extract `System/dmcp/` from `System/c47/c47_existos.c` — display adapter,
   keyboard map, timers/RTC/battery, `sdb`. **C47 must keep working** throughout;
   it is the regression test.
2. Implement the gap above in the shared layer.
3. `System/db48x/db48x_existos.c` — task, heap, `sys_free_mem()` returning the
   chosen budget (start 64 KB), launcher entry.
4. Third launcher icon, `StartDB48X()`.
5. Bring up to a stack display, then tune the heap against `VMRATE`.
