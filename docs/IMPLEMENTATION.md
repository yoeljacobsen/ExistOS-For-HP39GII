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
| DB48X RPN/RPL app | **runs**; layout fixes pending verification on device |

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
`clearall`, `scrcap`.

**`key` and `scrcap` together make a closed debugging loop** — the calculator can
be driven and observed entirely from the host, with no human at the keypad.

`key <code> <code> ...` taps each `Keys_t` code in turn (`(row << 3) + col`,
see `System/core/keyboard_gii39.h`). A dedicated OSLoader task paces press and
release 120 ms apart, because the System polls keys every 20 ms and anything
faster is missed. The physical scan only writes `g_latest_key_status` when a key
actually changes, so an injected value survives until the next real press.

Useful codes: `RIGHT` 12, `LEFT` 20, `UP` 4, `DOWN` 28, `ENTER` 76, `ON` 80,
`VIEWS` 18, `APPS` 25, `0`–`1` 72–73, `2`–`3` 66–67, `+` 68.
From the launcher, `key 12 12 76` starts DB48X.

`/tmp/shot.sh [codes...]` taps the codes then captures, writing a 3x PNG to
`/tmp/db48x_screen.png`.

**`scrcap` is the most useful of these.** It streams the panel as raw 8-bit
greyscale between `<<SCRCAP 256x127>>` and `<<SCRCAPEND>>` markers, so the
display can be looked at directly instead of described. While it streams,
OSLoader switches to `CDC_PATH_SCRCAP` so the log task does not interleave into
the image. Extract with:

```python
# /tmp/scrcap.py — writes /tmp/screen.pgm and prints an ASCII preview
raw = open('/tmp/c47_serial.log','rb').read()
b = raw.rfind(b'<<SCRCAP 256x127>>\n'); e = raw.rfind(b'\n<<SCRCAPEND>>')
data = raw[b+19:e][:256*127]
open('/tmp/screen.pgm','wb').write(b"P5\n256 127\n255\n" + data)
ramp = " .:-=+*#%@"
for y in range(0, 127, 3):
    print("".join(ramp[(255-data[y*256+x])*9//255] for x in range(0, 256, 2)))
```

Debugging a display problem by description is slow and gets the wrong answer;
capture the screen.

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

## Shared DMCP platform layer

`System/dmcp/` holds one DMCP implementation serving whichever application is
running — C47, DB48X, and anything else built on the API. Only one runs at a
time. Layout:

    System/dmcp/dmcp_existos.c   the platform layer (was System/c47/c47_existos.c)
    System/dmcp/dmcp_app.h       the hooks an application supplies
    System/c47/c47_app.c         C47 glue: RAM pool, indicators, StartC47()
    System/db48x/db48x_app.c     DB48X glue: StartDB48X()

An application supplies `name`, `run()` (its renamed `program_main`),
`indicators()` (its own shift/alpha/busy state → `INDICATE_*`), an `exit_flag`,
and `never_returns`.

`never_returns` matters: **DB48X's `program_main()` is `while (true)`**. On a
DM42 the firmware ends the program from inside `sys_sleep()`. Here `dmcp_run()`
wraps the call in `setjmp`, and `sys_sleep()` / `runner_get_key()` `longjmp`
back out when an exit is requested, so the layer's teardown still runs. C47 sets
`never_returns = false` because it returns normally after saving its state, and
must be allowed to.

Four link-level problems came out of putting two DMCP applications in one
binary. Each is handled in the build scripts, not by patching upstream:

1. **Both cores define `program_main`.** `objcopy --redefine-sym` renames them
   to `c47_program_main` / `db48x_program_main`.
2. **Both define `Settings`.** `build_db48x.sh` recomputes the collision set
   against `libc47_core` and renames each with a `db48x_` prefix, so a future
   clash reports itself instead of failing with a bare symbol name.
3. **Circular references** — the cores call the DMCP API, the layer calls back
   into the application — so the libraries are linked inside
   `-Wl,--start-group ... --end-group`.
4. **`ff_ifc.h` must be inside `dmcp.h`'s `extern "C"` block**, or C++ callers
   get mangled `f_open` and friends.

One host-tool bug surfaced: `tools/sys_signer/main.c` read symbol names with an
unbounded `sscanf("%s")` into `char[1024]`. DB48X's C++ mangled names run past
that and smashed the stack, so signing segfaulted on the larger image. Bounded
to `%1023s`.

## DB48X build

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

## DB48X on-device findings

Four things stood between "links" and "runs". Each is the same shape: DB48X
assumes it is the only program on a DM42, where the firmware owns resources for
the whole session. Under ExistOS it is launched and exited repeatedly.

1. **`Screen` is bound in a global constructor.**
   `surface Screen((pixword *) lcd_line_addr(0), ...)` runs from `.init_array`
   at System boot, long before any application starts, so it captured a
   not-yet-allocated heap pointer — null — and every draw faulted at address 0.
   The LCD buffer is a static array now, so it exists before static init.

2. **Pixel polarity is the opposite of C47's.** `blitter.h` says it outright:
   *"On the DM42, white is 0 and black is 1."* C47 blanks to `0xFF` and treats a
   clear bit as ink. Per-application now (`ink_is_set_bit`).

3. **`program_init()` mallocs the RPL arena on every launch and never frees it.**
   Harmless on a DM42; here the second launch leaked it. The build patches
   `main.cc` to call `dmcp_rpl_arena()`, which allocates once and reuses.
   `runtime::memory()` reinitialises everything from the block it is given, so
   one block across launches is exactly right.

4. **`HeaderFont` defaults to `LIB17`**, a DM42 *firmware* bitmap font reached
   through `dmcp_font`, whose metrics come from DMCP tables that do not exist
   here. `draw_battery()` puts annunciators at
   `(header_font()->height() + 1 - 12) / 2`; the captured icon at y≈114 of 240
   back-solves to a reported header height of ~240 — the header claimed the
   whole screen, leaving no room for the stack or the menu. Every other font
   setting already defaults to one of DB48X's own compiled-in sparse fonts;
   the build patches this one to `REDUCED`.

Separately, the "works once per boot" symptom was **not** the arena — the log
shows it allocating fine. `db48x_exit` is the platform layer's flag, set on exit
and never cleared. C47 clears its own `backToDMCP` at the top of
`program_main()`, but an application whose main loop never returns has no such
moment, so the next launch saw the flag already set and quit within half a
second. `dmcp_run()` clears it on entry.

## DB48X: where the display stands

Driven and verified from the host with `key` + `scrcap`. Working: launch,
inverse-video header, hardware annunciators, editor line, **stack values**
(`1 ENTER 2 +` -> 3, `4 ENTER 5 x` -> 20, long decimals with digit grouping),
exit and relaunch, softkey menus (e.g. `MATH`), and online help (`SYMB` ->
`F1`, once the help files are copied to `/help/` -- see USAGE.md).

### The unaligned-surface bug

Stack values never rendered, and the blit occasionally crashed on a wild
coordinate. Both had one cause. A rendered stack value is drawn into a **grob in
the RPL arena** and blitted to the screen; the editor draws text directly, which
is why it worked. `blitter::surface` documents its base as a "word-aligned
address", but a grob's bitmap sits right after its LEB128 type, width and
height, at an arbitrary byte offset -- the crash dumps show source bases of
`0x0202dfdf` and `0x0202e881`. A Cortex-M4 (DM42) does unaligned word loads in
hardware. **The ARM926 silently returns the word rotated.** Rotated pixel words
render as nothing; coordinates computed from them occasionally run off the map.

`build_db48x.sh` patches `src/blitter.h`: the surface constructor rounds the
base down to a word and records the misalignment in `existos_origin` (8 bits per
byte, since byte k of a little-endian word holds bits 8k..8k+7), and the two
helpers every word/bit lookup goes through -- `pixel_address()` and
`pixel_shift()` -- add it. Any other code ported from a Cortex-M target deserves
the same suspicion: **this CPU does not fault on unaligned access, it gives wrong
answers.**

### Still open

- The input line sits at the very bottom and can clip in the 240 -> 127 scale.
- DB48X shows all three shift planes of the softkey menu at once (three rows),
  which costs a lot of a 127-row panel; consider a single-plane menu setting.

The softkey menu itself works: `menu_screen_bottom()` is 0 only while no menu is
active (and the geometry log prints only when stack depth changes, so it can be
stale). `MATH` brings up the three-plane bar correctly.

## Next steps

1. **Verify the header-font and exit-flag fixes on device** (built, not yet
   flashed at the time of writing). Capture with `scrcap` rather than working
   from a description.
2. `sys_free_mem()` returns `DMCP_APP_HEAP_BUDGET`, currently 64 KB. It is a
   deliberate budget, not the real free heap: the VM keeps ~40 KB of RAM
   resident and spills the rest to NAND. Tune against `VMRATE` — `nandrd/s` in
   the hundreds means the budget is too large, not too small.
3. `db48x_indicators()` is a stub returning 0; map DB48X's shift/alpha state.
4. DB48X reuses C47's launcher icon; it needs its own.
5. `handle_menu()` and `msg_box()` decline rather than implementing DMCP's
   firmware menus. Revisit if DB48X depends on them for anything important.
