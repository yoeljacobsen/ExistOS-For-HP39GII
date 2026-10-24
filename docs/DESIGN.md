# Design — a native RPN/RPL calculator for ExistOS

## Goal

A native RPN/RPL calculator in the HP spirit, running as an ExistOS
application alongside KhiCAS and C47. Native, not emulated: the Emu48 route was
tried and abandoned (`IMPLEMENTATION.md` records why in detail).

## Choice of base: DB48X

Two candidates were evaluated against the platform, not against reputation.

### newRPL — rejected

`firmware/sys/target_50g/mem.c` manages **its own ARM MMU page tables**
(`MEM_REVERSEMMU`, `MEM_RSTKMMU`, `MEM_DSTKMMU`, `MEM_DIRMMU`, `MEM_LAMMMU`,
`MEM_TEMPOBMMU`), mapping six independently-growable RPL regions.

ExistOS's `System` runs in **ARM User Mode inside OSLoader's pager** and cannot
touch the MMU — that is precisely what the VROM/VRAM/ZRAM design owns. newRPL's
memory manager *is* newRPL, so this is not a porting detail but a rewrite. It
also needs a complete new HAL target (screen, keyboard, alarm, CPU, flash),
with nothing reusable from the existing C47 work.

### DB48X — chosen

DB48X targets the SwissMicros DM42 through **DMCP — the same API already
implemented and proven on this device for C47** (`program_main`,
`lcd_line_addr`, `key_pop`, `sys_sleep`, `aux_buf_ptr`, `sys_timer_start`,
`sys_free_mem`, `SET_ST`).

Three properties decide it:

1. **It sizes its heap to the platform at runtime.** `src/dmcp/main.cc`:
   ```c
   size_t size = sys_free_mem() - 10 * 1024;
   rt.memory(malloc(size), size);
   ```
   We control `sys_free_mem()`. This is the decisive property: Emu48 demanded a
   fixed ~570 KB against a 40 KB resident window and died. DB48X bends to
   whatever it is given, and its garbage collector is built for that scale.

2. **The framebuffer is byte-identical to C47's.** `LCD_W=400, LCD_H=240,
   BITS_PER_PIXEL=1, LCD_SCANLINE=416` — 416 bits is 52 bytes, exactly the
   2-byte header + 50-byte data stride the existing adapter walks, and
   `MONOCHROME_REVERSE` matches its bit reversal. The 400×240 → 256×127
   anti-aliased renderer applies unchanged.

3. **Same 37-key + 6-softkey layout as the DM42**, so the HP 39gII key map built
   for C47 transfers directly, including the composite keys and the PRGM /
   MATRIX / LIST legend redirects.

Code size is a non-issue: `vmROM` is 9 MB, demand-paged, with measured
`vrom=0/s` faults under load.

## Architecture

```
  System/dmcp/          shared DMCP platform layer (extracted from System/c47)
    dmcp_display.c        400x240 1bpp -> 256x127 greyscale, 3 view modes
    dmcp_keyboard.c       HP 39gII matrix -> DMCP key codes
    dmcp_sys.c            timers, RTC, battery, sleep, sdb callbacks
    dmcp.h                ExistOS-adapted DMCP API
         |                        |
   System/c47/              System/db48x/
   c47_existos.c            db48x_existos.c      (app glue: task, heap, entry)
         |                        |
   Libs/libc47_core.libc    Libs/libdb48x_core.libc   (cross-built by Libs/src/*.sh)
```

The platform layer is shared rather than copied. C47 is the reference consumer
and must keep working as the extraction proceeds.

## Memory plan

The whole risk of this port is the heap. `sys_free_mem()` will report a
deliberately chosen figure rather than the real free heap:

- **start at 64 KB**, which keeps the live set inside the 40 KB resident window
  plus ZRAM headroom;
- tune against `VMRATE`: if `nandrd/s` climbs into the hundreds the heap is too
  large for the pager, not too small for RPL;
- expect arithmetic, stack work and modest programs to be comfortable, and
  large symbolic work to be tight. That trade is the platform's, not DB48X's.

## Non-goals

- HP 39G *aplet* compatibility. Neither candidate provides it; that was Emu48's
  job and Emu48 is gone.
- Bit-exact DM42 behaviour. The screen is 256×127, not 400×240, so the display
  is an adaptation and some layouts will differ.
