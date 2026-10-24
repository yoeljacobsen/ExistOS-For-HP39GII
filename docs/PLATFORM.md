# Platform notes — HP 39gII under ExistOS

Hard-won facts about this hardware that are easy to get wrong and expensive to
rediscover. Everything here was measured on the device, not inferred.

## Memory is the binding constraint, not CPU and not code size

| Resource | Size | Notes |
| :--- | :--- | :--- |
| Physical SRAM | 512 KB | the entire machine; no external DRAM |
| `vmROM` (code + rodata) | 9 MB | demand-paged from NAND, **effectively free** |
| VROM page cache | 128 KB | `NUM_CACHEPAGE_VROM`, caches read-only code pages |
| VRAM page cache | **40 KB** | `NUM_CACHEPAGE_VRAM` — only this much RAM is *resident* at once |
| ZRAM | 124 KB | MiniLZO-compressed RAM pool; zero pages cost nothing |
| NAND swap | 2–3 MB | correct but ~1.5 ms per page read — a cliff, not a slope |

The number that matters is the **40 KB resident window**. Any RAM the
application touches outside it costs a page fault: cheap if ZRAM holds the page,
catastrophic if it has gone to NAND swap. CPU is 392 MHz and rarely the limit;
code size is irrelevant because `vmROM` pages well (measured `vrom=0/s` faults
under load).

**Design rule: keep the hot working set under ~40 KB and the total RAM
footprint within ~120 KB.** An application that needs several hundred KB of
randomly-accessed RAM will not run usably, however fast its code is. This is
what killed the Emu48 port (see `IMPLEMENTATION.md`).

### Measuring it

OSLoader prints, once a second, whenever anything is paging:

    VMRATE: vram=<faults/s> vrom=<faults/s> nandrd=<NAND reads/s>

Line that up against the application's own work rate in the same log. `nandrd`
above a few hundred per second means the CPU is mostly blocked on flash:
at ~1.5 ms each, 600/s is ~900 ms of I/O in every second.

## Keyboard

`ON` is special and **no `ON`+key chord can ever reach an application**.
OSLoader's `key_task_capt()` stops publishing key state the moment `ON` goes
down and hands the combination to `capt_ON_Key()`, which owns these globally:

| Chord | Effect |
| :--- | :--- |
| `ON`+`F3` | `portBoardPowerOff()` — powers the calculator down |
| `ON`+`F6` | `portBoardReset()` — reboots |
| `ON`+`F5` | maintenance / erase-data menu |
| `ON`+`F2` | screen capture over CDC |
| `ON`+`±` | contrast |

Both the C47 and Emu48 ports originally used `ON`+`F3` to exit, which instead
powered the calculator off. **Use a long press of `ON` for an app exit**, or a
key the firmware does not claim.

`ll_vm_check_key()` reports a *level*, not a queue: a release that coalesces
with the next press is never seen. Always release the previously-reported key
before acting on a new press, or a key wedges down.

## Power

`portBoardPowerOff()` writes `HW_POWER_RESET`. Two traps:

1. **`UNLOCK` reads back as zero**, so the key must be in the *same* 32-bit
   write as `PWD`. Three read-modify-writes of individual fields silently did
   nothing for years.
2. **With USB connected the chip cannot stay off.** The PMU powers it straight
   back up. Verified against `BRWNOUT_PWD`, `PWDN_BATTBRNOUT`, masking the
   battery brownout and restoring `DISABLE_PSWITCH` — all still came back.
   On battery it powers down correctly. The UI therefore *stands by* (panel
   off via `LL_SWI_DISP_SLEEP`, clock to minimum, wait for `ON`) when
   `ll_get_vdd5v_present()` is true.

Do **not** disable the 5 V detect comparators before `PWD`: it kills USB
without powering down, which looks like a successful shutdown from the host and
like a hang from the keyboard.

`HW_POWER_STS.BATT_BO` is permanently asserted and the battery monitor reads
~180 mV regardless, so any battery reading needs an out-of-window guard.

## Display

256×127, 8-bit greyscale, **`0x00` is black and `0xFF` is white**. Painting the
framebuffer black drives every pixel and costs *more* power than a lit screen —
to blank it, sleep the panel (`LL_SWI_DISP_SLEEP`, UC1698 `0x28`/`0x10`).

## Debugging

See `IMPLEMENTATION.md` for the build/flash/serial loop, which is the only
practical way to diagnose anything here.
