# Usage

## The launcher

`◀` / `▶` select an application, `ENTER` launches it.

| Icon | Application |
| :--- | :--- |
| KhiCAS | Computer algebra (Xcas / Python syntax) |
| C47 | RPN scientific calculator (WP43 family) |

`SHIFT`+`ON` turns the calculator off. On battery it powers down properly. With
USB connected the hardware cannot stay off — the PMU powers it straight back up
— so it goes into **standby** instead: panel off, clock at minimum, any `ON`
press wakes it.

## C47

A port of [C47](https://gitlab.com/rpncalculators/c43), the WP43-family RPN
scientific calculator. Its 400×240 screen is rendered to the 39gII's 256×127
panel with anti-aliasing.

### Leaving C47

`SHIFT`+`ON` — C47's own OFF — returns to the launcher and auto-saves to
`/SAVFILES/C47auto.sav`. A plain `ON`/`C` tap is C47's EXIT.

### Keys

`APPS` shows a two-page keymap overlay on the calculator itself; `VIEWS` cycles
the display mode. The rest is as printed, with these specifics:

| HP 39gII key | C47 function |
| :--- | :--- |
| `F1`–`F6` | softkeys |
| `SYMB` `PLOT` `NUM` | Σ+, 1/x, √ |
| `LOG` `LN` | log/10ˣ, ln/eˣ |
| `HOME` `VARS` `a b/c` | XEQ, STO, RCL |
| `X,T,θ,n` | R↓ (roll down) |
| `,` | x↔y |
| `(` `)` | EEX, R/S |
| `(-)` | +/− (CHS) |
| `SHIFT` | f, then g |
| `ON`/`C` | EXIT |
| `←` `→` | x↔y, R↓ (the DM42 has no horizontal arrows) |

Four HP keys have no DM42 counterpart and are sent as a shifted pair:

| Key | Sends | Meaning |
| :--- | :--- | :--- |
| `x²` | f + √ | x² |
| `x^y` | f + 1/x | yˣ |
| `ALPHA` | f + XEQ | alpha entry (AIM) |
| `MATH` | f + "+" | CATALOG |

Three of the HP's printed shifted legends are redirected to the C47 function
they name:

| Legend | C47 |
| :--- | :--- |
| `SHIFT`+`1` PRGM | P/R — program entry mode |
| `SHIFT`+`4` MATRIX | MATX menu |
| `SHIFT`+`7` LIST | STAT menu (C47 has no list type) |

This costs C47's own f+1 (ASSIGN), f+4 (BASE) and f+7 (EQN); all three remain
reachable through CATALOG on the `MATH` key, whose FCNS and MENUS pages list
every function and menu. g-shift on those keys is untouched.

### Display modes — `VIEWS`

1. **Full** — the whole 400×240 screen, anti-aliased to 256×127.
2. **Zoom stack** — status bar, enlarged Y/X registers, softkeys.
3. **1:1 pixel-exact** — a 256×127 window; the arrow keys pan it. Switch back to
   another mode to give C47 its arrows again.

> **Known issue:** in mode 2 the input line renders but the value is not visible
> once `ENTER` pushes it onto the stack.

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
