# ExistOS Architecture & Developer Guide

This document provides a complete technical reference for **ExistOS-For-HP39GII**, covering the hardware platform, memory map, NAND Flash layout, two-stage hypervisor/OS architecture, virtual memory manager (`VmMgr`), Low-Level API (`LLAPI`), Linux build/deployment tooling, and the roadmap for improving ExistOS and Emu48.

---

## 1. Hardware Platform (HP 39gII)

| Component | Specification | Driver / Source Location |
| :--- | :--- | :--- |
| **SoC** | Freescale / SigmaTel **STMP3770** | `OSLoader/HAL/Hardware/registers/` |
| **CPU Core** | ARM926EJ-S (ARMv5TEJ), up to 480 MHz, 16 KB I-Cache, 16 KB D-Cache, MMU | `OSLoader/VmMgr/mmu.c`, `OSLoader/HAL/clkctrl_up.c` |
| **On-Chip SRAM** | **512 KB** (`0x00000000` – `0x0007FFFF`) + **16 KB** DFLPT page table RAM (`0x800C0000`) | `Script/ldr_ld.script` |
| **NAND Flash** | **128 MB** SLC NAND (2048 B page + 64 B OOB, 64 pages/block = 128 KB/block, 1024 blocks) | `OSLoader/drivers/stmp_gpmi.c`, `OSLoader/HAL/mtd_up.c` |
| **Display** | **256 × 128** 8-bit grayscaleSTN LCD (UltraChip UC1698 controller over 8-bit System-80 bus via LCDIF) | `OSLoader/HAL/display_up.c`, `OSLoader/drivers/display/uc1698.c` |
| **Keyboard** | GPIO row/column matrix + dedicated `ON/C` wake/interrupt pin | `OSLoader/drivers/input/ keyboard_up.c`, `System/core/keyboard_gii39.h` |
| **USB** | High-Speed USB 2.0 OTG controller (ARC/ChipIdea core) | `OSLoader/Component3rd/tinyusb/` |
| **Timers & RTC** | `TIMROT` (4x 16-bit rotary/general timers) + 32.768 kHz `RTC` | `OSLoader/drivers/timer/`, `OSLoader/HAL/rtc_up.c` |
| **Power & ADC** | Integrated DC-DC converter (`POWER`) + 8-channel `LRADC` (battery voltage, die temp) | `OSLoader/drivers/stmp_power.c`, `OSLoader/drivers/stmp_lradc.c` |

---

## 2. Two-Stage Hypervisor / OS Architecture

Because the STMP3770 only has **512 KB of physical on-chip SRAM** and **no external SDRAM**, ExistOS uses a two-stage architecture where **Stage 1 (`OSLoader`)** acts as a bare-metal hypervisor and virtual memory manager in physical SRAM, and **Stage 2 (`System`)** runs in ARM User Mode (`CPSR = 0x10`) inside a demand-paged virtual address space.

```
+-----------------------------------------------------------------------+
|                     Stage 2: System (ExistOS.sys)                     |
|              Runs in ARM User Mode (0x10) in Virtual Memory           |
|  +-----------------+  +---------------+  +---------+  +------------+  |
|  | UICore / Shell  |  | KhiCAS (Giac) |  |  Emu48  |  | FatFs / UI |  |
|  +-----------------+  +---------------+  +---------+  +------------+  |
|  |         System FreeRTOS Scheduler (Virtual Timer & SWI)         |  |
+----------------------------------+------------------------------------+
                                   | SWI (0xEE00 / 0xEF00) & Data/Prefetch Aborts
+----------------------------------v------------------------------------+
|                     Stage 1: OSLoader (OSLoader.sb)                   |
|             Runs in ARM Supervisor/System Mode in Physical SRAM       |
|  +-------------------+  +-------------------+  +-------------------+  |
|  |  VmMgr (MMU +     |  |  LLAPI Dispatcher |  |  TinyUSB (CDC /   |  |
|  |  VROM/VRAM/ZRAM)  |  |  (llapi.c)        |  |  EDB / MSC)       |  |
|  +-------------------+  +-------------------+  +-------------------+  |
|  |         OSLoader FreeRTOS Scheduler + Dhara FTL + GPMI/ECC8     |  |
+-----------------------------------------------------------------------+
```

### Stage 1: `OSLoader` (`OSLoader/` -> `build/OSLoader/OSLoader.sb`)
- **Linker Script**: `Script/ldr_ld.script`
- **Physical Memory Map (512 KB SRAM)**:
  - `0x00000000` – `0x000003FF` (1 KB): ARM exception vectors (`.init.vector`)
  - `0x00000400` – `0x000283FF` (160 KB): `VROM` page cache (`PAGE_OCCUPY_VROM_SIZE = 160 KB`, 160 × 1 KB pages)
  - `0x00028400` – `0x000323FF` (40 KB): `VRAM` uncompressed page cache (`PAGE_OCCUPY_VRAM_SIZE = 40 KB`, 40 × 1 KB pages)
  - `0x00032400` – `0x000493FF` (92 KB): `ZRAM` compressed memory pool (`ZRAM_COMPRESSED_MEM_SIZE = 92 KB`, managed by TLSF allocator + MiniLZO compression)
  - `0x00049400` – `0x0007FFFF` (~219 KB): `OSLoader` code, `.data`, `.bss`, FreeRTOS heap, and mode stacks (`ABT`, `UND`, `IRQ`, `FIQ`, `SVC`, `SYS`)
  - `0x800C0000` – `0x800C3FFF` (16 KB): Hardware DFLPT RAM used for ARM MMU L1/L2 page tables (`mmu.c`)
- **Core Services (`OSLoader/boot/services/`)**:
  - `vMTDSvc`: Low-level NAND page read/write/erase via STMP3770 GPMI DMA + ECC8 engine.
  - `vFTLSvc`: Dhara NAND Flash Translation Layer service.
  - `vVMMgrSvc`: Handles Data Abort and Prefetch Abort page faults from `System`, paging 1 KB pages in/out of `VROM`, `VRAM`, `ZRAM`, or NAND Swap.
  - `vLLAPISvc`: Executes blocking `LLAPI` calls on behalf of `System`.
  - `vTaskTinyUSB`: Handles USB CDC (serial console + EDB firmware flashing protocol) and USB MSC (mass storage access to the FAT16 partition).
  - `vKeysSvc` & `vDispSvc`: Polls GPIO keypad (including global `ON+F5` maintenance menu and `ON+Plus/Minus` contrast shortcuts) and flushes display updates to the UC1698 LCD controller.

### Stage 2: `System` (`System/` -> `build/System/ExistOS.sys`)
- **Linker Script**: `Script/sys_ld.script`
- **Virtual Memory Map**:
  - `0x00100000` – `0x006FFFFF` (6 MB `vmROM`): Read-only code and `.rodata` demand-paged directly from `ExistOS.sys` on NAND Flash (and `fonts_hzk16h` at `+4 MB`).
  - `0x02004000` – `0x02035FFF` (200 KB `vmRAM` base, expandable with ZRAM/Swap): `.data`, `.bss`, `SYSTEM_STACK`, and the `OnChipHeap` (`pvPortMalloc` / wrapped `malloc`).
  - `0x02100000` – `0x022FFFFF` (2 MB `SwapMemHeap`): Secondary heap backed by NAND Flash swap when Memory Swap is enabled.
  - `0x03000000` (`app_exec`): Virtual address region for external `.exp` applications.
- **User-Mode FreeRTOS**:
  - `OSLoader` injects virtual timer ticks and virtual IRQs/SWIs into `System` by jumping to the vectors registered via `ll_set_irq_vector()` and `ll_set_svc_vector()` (`System/core/SysIRQ.c` and `System/core/main.c`).

---

## 3. NAND Flash Layout

The 128 MB NAND Flash has **1024 blocks** (64 pages/block, 2048 bytes/page):

| Block Range | Page Range | Size | Purpose |
| :--- | :--- | :--- | :--- |
| **Block 0 – 21** | `0 – 1407` | ~2.75 MB | Boot control blocks (BCB/DBBT/NVRAM) used by STMP3770 ROM bootloader |
| **Block 22 – 30** | `1408 – 1983` | 9 blocks (~1.1 MB) | **`OSLoader.sb`** partition (`BOOT1_PAGE = 1408`, flashed with `edb -r -s -f OSLoader.sb 1408 b`) |
| **Block 31 – 95** | `1984 – 6143` | 65 blocks (~8.1 MB) | **`ExistOS.sys`** partition (`SYSTEM_START_PAGE = 1984`, flashed with `edb -r -s -f ExistOS.sys 1984`) |
| **Block 96 – 1023** | `6144 – 65535` | 928 blocks (~116 MB) | **Dhara FTL region** (`FLASH_FTL_START_BLOCK = 96`):<br>• Logical sectors `0 – 4095` (8 MB): **Virtual Memory Swap** (`FLASH_FTL_SWAP_SECTOR = 0`)<br>• Logical sectors `4096+` (~80+ MB): **FAT16 Data Partition** (`FLASH_FTL_DATA_SECTOR = 4096`) |

---

## 4. Low-Level API (`LLAPI`) Interface

`System` communicates with `OSLoader` via ARM `SWI` instructions defined in `OSLoader/LowLevelAPI/llapi_code.h` and implemented in `OSLoader/LowLevelAPI/llapi.c` + `System/core/sys_llapi.c`:

- **Fast SWIs (`0xEE00` – `0xEEFF`)**: Executed directly in the `OSLoader` SWI handler (`swi_asm.c` / `llapi.c`):
  - Context switching (`LL_SWI_LOAD_CONTEXT`, `LL_SWI_SAVE_CONTEXT`)
  - IRQ/SVC vector & stack registration (`LL_SWI_SET_IRQ_STACK`, `LL_SWI_SET_IRQ_VECTOR`, `LL_SWI_SET_SVC_STACK`, `LL_SWI_SET_SVC_VECTOR`, `LL_SWI_ENABLE_IRQ`)
  - Time & RTC (`LL_SWI_GET_TIME_MS`, `LL_SWI_GET_TIME_US`, `LL_SWI_RTC_GET_SEC`, `LL_SWI_RTC_SET_SEC`)
  - CPU & Power (`LL_SWI_CPU_SLOWDOWN_ENABLE`, `LL_SWI_SYSTEM_IDLE`, `LL_SWI_PWR_SPEED`, `LL_SWI_PWR_OFF`, `LL_SWI_CHARGE_ENABLE`, `LL_SWI_BAT_VAL`, `LL_SWI_CORE_TEMP`)
  - Memory stats (`LL_SWI_MEM_COMP_RATE`, `LL_SWI_MEM_PHY_INFO`, `LL_SWI_MEM_SWAP_ENABLE`, `LL_SWI_MEM_SWAP_SIZE`)
- **Queued/Service SWIs (`0xEF00` – `0xEFFF`)**: Handled by `vLLAPISvc` in `OSLoader` (suspending the calling thread until completion):
  - Display (`LL_SWI_DISPLAY_FLUSH` / `ll_disp_put_area`, `LL_SWI_DISP_SET_INDICATOR`)
  - Flash FTL I/O for FatFs (`LL_SWI_FLASH_PAGE_READ`, `LL_SWI_FLASH_PAGE_WRITE`, `LL_SWI_FLASH_PAGE_TRIM`, `LL_SWI_FLASH_SYNC`, `LL_SWI_FLASH_INFO`)

---

## 5. Build System & Host Tooling (Linux)

### Directory Structure
```
ExistOS-For-HP39GII/
├── build.sh                  # Unified Linux build & flash script
├── CMakeLists.txt            # Top-level CMake configuration
├── toolchain.cmake           # ARM GCC 10.3 cross-compiler configuration
├── 99-hp39gii.rules          # Linux udev rules for USB HID (066f:3770) & CDC/MSC (cafe:4003)
├── Script/
│   ├── build_fw.bd           # elftosb boot descriptor for OSLoader.sb
│   ├── ldr_ld.script         # Linker script for OSLoader (SRAM 0x00000000)
│   └── sys_ld.script         # Linker script for System (Virtual ROM 0x00100000, RAM 0x02004000)
├── tools/
│   ├── sbtools/              # Linux elftosb & sbloader source (Freescale SB 2.0 format + USB HID loader)
│   ├── sys_signer/           # Appends symbol table hash & footer to ExistOS.sys
│   └── edb-unix/             # Linux Exist Debug Bridge (USB CDC flashing & file transfer tool)
├── OSLoader/                 # Stage-1 Bootloader, HAL, Drivers, VmMgr, TinyUSB, Dhara FTL
├── System/                   # Stage-2 OS: FreeRTOS, FatFs, UICore, KhiCAS, Emu48, CrashLog
└── Libs/                     # Precompiled Giac/CAS, GMP, TomMath static libraries + headers
```

---

## 6. Linux Emulator Setup (`EmuGII`)

`OSLoader` has built-in support for running under the STMP3770 QEMU emulator (`ExistOS-Team/EmuGII`):
- During boot (`OSLoader/boot/core/init.c`), `OSLoader` checks `*(uint32_t *)0xD0000000 == 0x39622390` to detect if it is running inside the emulator (`g_system_in_emulator = true`).
- When `g_system_in_emulator` is true, `OSLoader` maps extra virtual RAM (`0x00080000` – `0x00300000`, 2.5 MB) and exposes `EMU_DATA_PORT` at `0x20000000` for host-to-guest file transfers (`check_emulator_status()` in `System/core/main.c`).

---

## 7. Emu48 Architecture & Improvements

### Current State (Updated in this Fork)
`System/emu48/` is a port of the Emu48 Saturn CPU emulator configured for the **HP 39g** (`cCurrentRomType = 'E'`, `nCurrentClass = 39`, 131×64 display scaled 2× to 256×128).

#### Fixes Applied
1. **Re-enabled & Integrated into `UICore.cpp`**:
   - Converted `System/emu48/emu48ico.c` from an obsolete 2-bit LVGL image to a native 48×48 8-bit grayscale icon (`gImage_emu48_ico`).
   - Added **Emu48** to the Application launcher tab (`drawPage(0)` and `KEY_ENTER` handler in `System/graphics/UICore.cpp`).
2. **Fixed Memory Corruption & Linked-List Bugs in `emu48_porting.c`**:
   - Fixed `memset(t, 0, sizeof(t))` -> `memset(t, 0, sizeof(emu48_emu_timer_t))`.
   - Fixed `emu_del_timer()` where an unconditional `goto t_del_found` inside the traversal loop broke timer deletion.
   - Reduced worker stack from 128 KB (`32768` words) to 16 KB (`4096` words) and moved the 32 KB `full_screen_buf` from static `.bss` to dynamic heap allocation so Emu48 uses zero SRAM when not running.
   - Added integer display scaling (`(x * 250) / 131`) instead of slow software floating-point `x * 1.96` in the display refresh loop.
3. **Fixed File Leaks & Added State Persistence (`emu48_file.c`, `emu48_porting.c`)**:
   - `load_rom()` now accepts either `/rom.39g` or `/rom.g39` and properly closes/frees the FatFs `FIL` handle.
   - Added automatic saving and loading of Saturn RAM (`Port0` -> `/port0.39g`, `Port2` -> `/port2.39g`) and clean shutdown/return to ExistOS UI via `ON + F3`.

### Roadmap for Significantly Improving Emu48 & ExistOS

#### A. Emu48 Performance & Feature Roadmap
1. **Demand-Paged ROM via `VROMLoader` (Zero-RAM ROM Loading)**:
   - Currently `load_rom()` allocates `rom_size` (512 KB packed / 1 MB unpacked) on the heap (`pvPortMalloc`), which requires Memory Swap to be enabled.
   - **Improvement**: Map `/rom.39g` directly into virtual ROM address space (e.g., `0x03800000`) using `VROMLoaderCreateFileMap()`. Then `FASTPTR()` and `rom_read_nibbles()` can read directly from demand-paged virtual memory with **0 KB heap allocation** and instant startup!
2. **Fast Unpacked Nibble Cache & ARM Assembly Inner Loop (`execute.c` / `opcodes.c`)**:
   - Currently `FASTPTR()` and `rom_read_nibbles()` unpack 32–64 bytes of nibbles on every memory access.
   - **Improvement**: Cache the active 4 KB Saturn pages in unpacked form (or unpack 1 KB pages on demand in `VROMIRQLoad`), and compile `System/emu48/core/` with `-O3 -marm` instead of `-Os -mthumb` for a 2–3× Saturn CPU speedup.
3. **Full State Save/Restore (`.e39` / `.e48` State Files)**:
   - Save `Chipset` struct (CPU registers, Saturn MMU bank state, timers, IORam) alongside `Port0` and `Port2` into `/state.e39` so resuming Emu48 returns to the exact instruction and screen without rebooting the Saturn ROM.
4. **Multi-Calculator Support (HP 48GX, HP 49G, HP 40G)**:
   - Add a ROM selector dialog on Emu48 launch that scans `/` for `*.39g`, `*.48g`, `*.49g` ROMs and sets `cCurrentRomType` (`'E'` for 39g/40g, `'S'`/`'G'` for 48SX/48GX, `'X'` for 49g) and the corresponding keyboard map (`emu48_kb_48g.h` / `emu48_kb_49g.h`).

#### B. ExistOS System & UI Improvements
1. **File Manager Actions (`UICore.cpp`)**:
   - Re-enable opening `.jpg` images (via `tjpgd`), `.exp` external apps, and text/script files directly from the `FILE` tab (`curPage == 2`).
2. **Modernized App Switcher & Status Bar**:
   - Add battery percentage curve, USB connection indicator, and a clean grid launcher in `UICore.cpp` supporting built-in apps (`KhiCAS`, `Emu48`, Scientific RPN/Algebraic Calculator, Periodic Table / Constants, Function Plotter) and `.exp` user apps.
3. **ZRAM & VmMgr Tuning (`OSLoader/VmMgr/vmMgr.c`)**:
   - Replace linear page table scans in `vmMgr.c` with hash/index lookup and tune `PAGE_OCCUPY_VROM_SIZE` / `ZRAM_COMPRESSED_MEM_SIZE` based on workload profiling.
