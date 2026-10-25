# Usage

## The launcher

`◀` / `▶` select an application, `ENTER` launches it.

| Icon | Application |
| :--- | :--- |
| KhiCAS | Computer algebra (Xcas / Python syntax) |
| C47 | RPN scientific calculator (WP43 family) |
| DB48X | RPL calculator in the HP 48 tradition |

`SHIFT`+`ON` is **off**, which on this calculator means **standby**: panel off,
clock at minimum, `ON` wakes it where it was. Inside C47 and DB48X it works the
same way and the application resumes untouched. A real power-down does not stick
on this board: the power chip turns the calculator straight back on, on battery
as well as on USB, and unmodified upstream ExistOS behaves the same.

## Keys in C47 and DB48X

Both run on a DM42 keyboard layout; the 39gII's keys are translated so that the
**legends printed on the case** are what you get, plain and with `SHIFT`. The
39gII `SHIFT` is held by ExistOS (the annunciator shows it) until the next key.
`SHIFT` pressed twice, or a `SHIFT`ed key whose legend the application has no
use for, gives the application's own shift on that key (C47's f, then g; DB48X's
left, then right shift), so nothing the application offers is out of reach.

`SHIFT`+`APPS` (Info) shows the full table on the calculator itself.

| Key | Plain / `SHIFT` | C47 | DB48X |
| :--- | :--- | :--- | :--- |
| `APPS` | quit | OFF → launcher, auto-saves | Off → launcher |
| `VIEWS` | display mode / Help | – / key overlay | – / Help |
| `ON`/`C` | EXIT / off | EXIT / standby | EXIT / standby |
| `x²` | x² / √ | x² / √x | x² / √x |
| `xʸ` | yˣ / ⁿ√ | yˣ / ˣ√y | yˣ / xroot |
| `÷` | ÷ / x⁻¹ | ÷ / 1/x | ÷ / 1/x |
| `LN` `LOG` | ln/eˣ, log/10ˣ | as labelled | ln/eˣ; LOG opens the Powers menu (log10, 10ˣ) |
| `SIN` `COS` `TAN` | / arc | as labelled | as labelled |
| `SYMB` | / Setup | EQN / MODE | Symbolic / Setup |
| `PLOT` | / Setup | PLOT / MODE | Plot / Setup |
| `NUM` | / Setup | XEQ / MODE | →Num / Setup |
| `HOME` | / Modes | HOME / MODE | Main menu / Modes |
| `VARS` | / Chars | RCL / AIM | Variables / Characters |
| `MATH` | / Cmds | CATALOG | Math menu / Catalog |
| `a b/c` | / ∠↔ | FRAC toggle / →POL | Fractions / Complex |
| `X,T,θ,N` | / EEX | STO / EEX | STO / EEX |
| `←` | / Clear | ← / CLR menu | ← Drop / Clear menu |
| `(` `)` | / Copy, Paste | R↓, x↔y | algebraic `'()'`, cursor right / Edit menu |
| `,` | / Mem | R/S / INFO | Swap / Variables |
| `ALPHA` | | AIM (alpha entry) | Alpha |
| `7` | / List | 7 / STAT | 7 / List |
| `8` `9` | / { } | | / List |
| `4` | / Matrix | 4 / MATX | 4 / Matrix |
| `5` `6` | / [ ] | | / insert `[]` |
| `1` | / Prgm | 1 / P/R | 1 / Program |
| `2` | / i | 2 / j | 2 / Complex |
| `3` | / π | 3 / π | 3 / Constants |
| `×` | / ! | × / PROB | × / Probabilities |
| `−` | / ∠ | − | − / Angles |
| `+` | / Σ | + / Σ+ | + / Statistics |
| `0` | / Notes | | 0 / Files |
| `(-)` | / ABS | CHS / \|x\| | +/− / abs |
| `ENTER` | / ANS | ENTER / LASTx | ENTER / LastArg |
| arrows | | ▲▼; ◀ x↔y, ▶ R↓ | ◀▲ cursor left / up, ▶▼ right / down |
| `F1`–`F6` | | softkeys | softkeys |

The tables live in `System/c47/c47_app.c` and `System/db48x/db48x_app.c`
(`dmcp_key_t`: 39gII key, plain meaning, `SHIFT` meaning).

## C47

A port of [C47](https://gitlab.com/rpncalculators/c43), the WP43-family RPN
scientific calculator. Its 400×240 screen is rendered to the 39gII's 256×127
panel with anti-aliasing. `APPS` leaves it (C47's OFF), auto-saving to
`/SAVFILES/C47auto.sav`.

### Display modes — `VIEWS`

1. **Full** — the whole 400×240 screen, anti-aliased to 256×127.
2. **Zoom stack** — status bar, enlarged Y/X registers, softkeys.
3. **1:1 pixel-exact** — a 256×127 window; the arrow keys pan it. Switch back to
   another mode to give the application its arrows again.

> **Known issue:** in mode 2 the input line renders but the value is not visible
> once `ENTER` pushes it onto the stack.

## DB48X

An RPL calculator in the HP 48 tradition ([DB48X](https://github.com/c3d/db48x)).
Launch it from the third launcher icon; `APPS` leaves it.

**Online help** (`SHIFT`+`VIEWS`, or `F1` on the main menu) is read at runtime from
`/help/db48x.md` and `/help/db48x.idx` on the calculator's FAT partition. They
are not part of the firmware image. `Libs/src/build_db48x.sh` writes both to
`build/db48x-help/`; copy them to `/help/` in mass-storage mode (`./flash.sh
msc`, then press `Views` on the calculator to leave that mode). Without them help
shows nothing.

## USB

The calculator exposes a small control disk (`EOSRECDISK`) used by `edb`, and a
USB CDC serial console.

- `./flash.sh msc` switches it to **mass storage** so the FAT partition can be
  mounted from a PC. **Press `Views` on the calculator to leave that mode** —
  while it is active, `edb` cannot see the device and the serial console is dead.
- `./flash.sh sys` / `./flash.sh all` flash firmware.
- `./flash.sh reboot`, `./flash.sh ram` (USB recovery).

## If it stops responding

| Symptom | Fix |
| :--- | :--- |
| Stuck in mass-storage mode | press `Views` |
| Unresponsive | `ON`+`F6` reboots |
| Maintenance / erase data | `ON`+`F5` |
| Contrast | `ON`+`+` / `ON`+`−` |
| Dead to USB (`066f:3770`) | `./flash.sh ram`, then `./flash.sh all` |

Note that **no `ON`+key combination ever reaches an application** — those chords
belong to OSLoader.
