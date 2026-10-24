# ExistOS for HP 39gII (Linux Edition)

An open-source firmware replacement for the **HP 39gII Graphing Calculator** (Freescale/SigmaTel STMP3770 SoC, ARM926EJ-S).

[![GPL Licence](https://badges.frapsoft.com/os/gpl/gpl.png?v=103)](https://opensource.org/licenses/GPL-3.0/)

> **Documentation:**
> [`docs/PLATFORM.md`](./docs/PLATFORM.md) — hardware constraints that govern every design decision here (memory, keyboard, power, display) ·
> [`docs/DESIGN.md`](./docs/DESIGN.md) — design of the native RPN/RPL application ·
> [`docs/IMPLEMENTATION.md`](./docs/IMPLEMENTATION.md) — status, build/flash/debug workflow, continuation notes ·
> [`docs/USAGE.md`](./docs/USAGE.md) — using the calculator and its applications
>
> **Architecture Reference:** See [`ARCHITECTURE.md`](./ARCHITECTURE.md) for a deep-dive into the hardware, two-stage OSLoader/System hypervisor design, virtual memory manager (VROM/VRAM/ZRAM/Swap), Low-Level API (`LLAPI`), and the improvement roadmap.

---

## Overview

ExistOS replaces the proprietary HP 39gII firmware with a modern two-stage operating system built on:
- **[FreeRTOS](https://github.com/FreeRTOS/FreeRTOS)** (independent schedulers in both `OSLoader` hypervisor and `System` user-space OS)
- **[TinyUSB](https://github.com/hathach/tinyusb)** (USB CDC serial console/EDB bridge & USB Mass Storage Mode)
- **[Dhara](https://github.com/dlbeer/dhara)** (NAND Flash Translation Layer with wear-leveling and bad-block management)
- **[FatFs](http://elm-chan.org/fsw/ff/00index_e.html)** (FAT16 filesystem on internal NAND Flash)
- **[KhiCAS / Giac](http://www-fourier.ujf-grenoble.fr/~parisse/giac.html)** (Computer Algebra System with Xcas and Python syntax modes)
- **[C47](https://gitlab.com/rpncalculators/c43)** (WP43-family RPN scientific calculator, built for ARM by `Libs/src/build_c47.sh`)

---

## Feature Status

- [x] Bootloader & Hypervisor (`OSLoader.sb`)
- [x] UART debug serial & USB CDC serial console
- [x] 256x128 256-level grayscale LCD driver (`STMP3770` LCDIF)
- [x] STMP3770 interrupt controller (`ICOLL`) & timer drivers (`TIMROT`)
- [x] GPIO matrix keyboard driver
- [x] Dynamic CPU frequency scaling & RTC clock
- [x] USB Mass Storage (MSC) mode & dynamic USB configuration
- [x] GPMI + ECC8 NAND Flash driver + Dhara FTL + FatFs
- [x] Demand-paged Virtual Memory (`VmMgr`) with 1 KB pages, MiniLZO compressed ZRAM, and NAND swap
- [x] Graphical User Interface (`UICore`), Console (`SimpShell`), File Manager, and Persistent Settings (`SystemConfig`)
- [x] KhiCAS Computer Algebra System (`Xcas` & `Python` modes, 2D/polar/field plotting, script editor)
- [x] C47 RPN Scientific Calculator (port of [C47](https://gitlab.com/rpncalculators/c43), the WP43-family DMCP5 firmware, with a 400x240 -> 256x127 anti-aliased display adapter)
- [x] Native Linux build & deployment toolchain (`elftosb`, `sbloader`, `sysigner`, `edb`)

---

## Building on Linux

### 1. Prerequisites

Install standard Linux build tools and libraries:

| Distribution | Command |
| :--- | :--- |
| **Debian / Ubuntu** | `sudo apt-get install build-essential cmake pkg-config libcrypto++-dev libusb-1.0-0-dev` |
| **Arch Linux** | `sudo pacman -Syu base-devel cmake pkgconf libusb crypto++` |

#### ARM Toolchain (`gcc-arm-none-eabi` v10.3)
ExistOS links against precompiled C++ static libraries (`Libs/libcas.libcpp`, `Libs/libkcasgui.libcpp`, `Libs/libgmp.libcpp`, `Libs/libtommath.libcpp`) built with **GNU Arm Embedded Toolchain 10.3-2021.10**.
Download and extract `gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2` to `~/.local/opt/gcc-arm-none-eabi-10.3-2021.10` or `/opt/gcc-arm-none-eabi-10.3-2021.10`:

```bash
mkdir -p ~/.local/opt
curl -L https://developer.arm.com/-/media/Files/downloads/gnu-rm/10.3-2021.10/gcc-arm-none-eabi-10.3-2021.10-x86_64-linux.tar.bz2 \
  | tar -xjf - -C ~/.local/opt
```

`toolchain.cmake` and `build.sh` automatically detect the toolchain in `~/.local/opt/gcc-arm-none-eabi-10.3-2021.10/bin` or `/opt/gcc-arm-none-eabi-10.3-2021.10/bin` (or via `$ARM_GCC_DIR`).

#### Linux `udev` Rules (For Hardware Flashing)
To access the HP 39gII over USB without root privileges:
```bash
sudo cp 99-hp39gii.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

---

### 2. One-Command Build (`./build.sh`)

Clone the repository with submodules and run `./build.sh`:

```bash
git clone --recurse-submodules https://github.com/yoeljacobsen/ExistOS-For-HP39GII.git
cd ExistOS-For-HP39GII
./build.sh
```

`./build.sh` automatically builds:
1. **Linux Host Tools** (`tools/`):
   - `tools/sbtools/elftosb` & `tools/sbtools/sbloader` (copied to `tools/elftosb` and `tools/sbloader`)
   - `tools/sys_signer/build/sysigner` (copied to `tools/sysigner`)
   - `tools/edb-unix/build/edb` (copied to `tools/edb`)
2. **Firmware Images** (`build/`):
   - `build/OSLoader/OSLoader.sb` — Stage-1 Bootloader & Hypervisor (flashed to NAND block 22 / page 1408)
   - `build/System/ExistOS.sys` — Stage-2 Main Operating System (flashed to NAND block 31 / page 1984)

Available `./build.sh` subcommands:
```bash
./build.sh all          # Build Linux host tools (if needed) and firmware (default)
./build.sh tools        # Rebuild all Linux host tools (sbtools, sysigner, edb)
./build.sh firmware     # Configure and build OSLoader.sb and ExistOS.sys
./build.sh clean        # Remove all build directories and generated binaries
./build.sh flash-ram    # Load OSLoader.sb to calculator SRAM (USB HID recovery mode)
./build.sh flash-loader # Flash OSLoader.sb to NAND via EDB
./build.sh flash-sys    # Flash ExistOS.sys to NAND via EDB
```

---

## Flashing to an HP 39gII on Linux

### Step 1: Initial Bootstrap via USB HID Recovery (Only if ExistOS is not yet installed)
1. Remove all batteries from the calculator.
2. Hold down `ON/C` and plug the calculator into your Linux machine via USB (`lsusb` will show `066f:3770 SigmaTel, Inc.`).
3. Load `OSLoader.sb` into SRAM:
   ```bash
   ./build.sh flash-ram
   # or: ./tools/sbloader build/OSLoader/OSLoader.sb -d
   ```
   The calculator will boot into `OSLoader`. Keep the USB cable connected and proceed to Step 2.

### Step 2: Flashing `OSLoader.sb` and `ExistOS.sys` to NAND via EDB
Once `OSLoader` is running (either from Step 1 or an existing ExistOS installation):
```bash
# Flash OSLoader to NAND block 22 (page 1408)
./build.sh flash-loader
# or: ./tools/edb -r -s -f build/OSLoader/OSLoader.sb 1408 b

# Flash ExistOS System to NAND block 31 (page 1984)
./build.sh flash-sys
# or: ./tools/edb -r -s -f build/System/ExistOS.sys 1984
```
The calculator will reboot automatically into ExistOS.

---

## Linux Emulator (`EmuGII`)

To run and test ExistOS on a Linux desktop without physical hardware, the ExistOS team maintains a QEMU-based STMP3770 / HP 39gII emulator (`ExistOS-Team/EmuGII`) as well as the memory-mapped emulator interface supported by `OSLoader` (`g_system_in_emulator` at `0xD0000000` / `EMU_DATA_PORT` at `0x20000000`). See [`ARCHITECTURE.md`](./ARCHITECTURE.md#linux-emulator-setup) for details on building and running the emulator on Linux.

---

## Basic Firmware Usage

### First Boot
On first boot, ExistOS prompts to format the NAND Flash data partition as FAT16. Press `F2` (or `ENTER` when prompted) to format (~30 seconds).

### System Shortcuts
These shortcuts work anywhere in the OS (even if a user task hangs):
- `ON` + `F5`: Open the OSLoader maintenance / format / reboot menu
- `ON` + `F6`: Immediate reboot (when in Out-of-Memory or maintenance screen)
- `ON` + `+` / `-`: Increase / decrease LCD contrast
- `SHIFT` + `ON`: Power off (on battery) or standby (while USB-powered — the PMU cannot keep the chip off with the 5 V rail up)
- `Views`: Leave USB mass storage mode

### Accessing Internal Storage (USB Mass Storage Mode)
Hold `F2` while booting (or select USB MSC mode from the `ON` + `F5` menu) and connect USB to your Linux PC. An ~80 MB FAT16 USB drive will mount containing:
- `/xcas/`: KhiCAS scripts and saved sessions
- `/SAVFILES/`, `/PROGRAMS/`: C47 saved state and programs
- `/crash/`: Crash logs with register and stack dumps
- `/existos_config.json`: Persistent system settings

Always safely unmount (`sync` / `udisksctl unmount`) before disconnecting so Dhara FTL flushes all blocks.

### C47 RPN Scientific Calculator
Select **C47** on the Application tab (`F1`) and press `ENTER`. See
[`docs/USAGE.md`](./docs/USAGE.md) for the full key map; on the calculator itself,
`APPS` shows a two-page keymap overlay and `VIEWS` cycles the display mode.
`SHIFT` + `ON` leaves C47 and auto-saves.

### KhiCAS Computer Algebra System
Select **KhiCAS** on the Application tab (`F1`) and press `ENTER`:
- Choose **Xcas** (`F1`) or **Python** (`F6`) syntax mode on launch.
- `F1` / `F2`: Quick math/algebra menus.
- `F3` (`View`): Natural textbook expression editor.
- `F4` (`cmds`): Full catalog of algebraic, calculus, matrix, probability, and plotting functions.
- `F6` (`File`): Session management and Python/Xcas script editor.
- `ON/C`: Clear history.
- `SHIFT` + `ON/C`: Save session and return to ExistOS main UI.

## Uninstalling ExistOS / Returning to Stock HP Firmware

Before flashing the stock HP firmware, you **must** erase the entire NAND Flash:
1. Press `ON` + `F5` to enter the maintenance menu.
2. Select full Flash erase and wait for completion.

---

## License

[GPL-3.0](./LICENSE)
