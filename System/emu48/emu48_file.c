#include "FreeRTOS.h"
#include "task.h"

#include "ff.h"

#include "string.h"
#include "stdio.h"

void *emu_rom_addr = NULL;

static size_t rom_size = 0;
static bool rom_isPacked = true;

bool rom_is_packed(void)
{
    return rom_isPacked;
}

int load_rom(const char *romfile) {
    FRESULT fr;
    UINT btr = 0;
    FIL *romf = pvPortMalloc(sizeof(FIL));
    if (!romf) {
        return -1;
    }

    fr = f_open(romf, romfile, FA_OPEN_EXISTING | FA_READ);
    if (fr != FR_OK && strcmp(romfile, "/rom.39g") == 0) {
        fr = f_open(romf, "/rom.g39", FA_OPEN_EXISTING | FA_READ);
    }
    if (fr != FR_OK) {
        vPortFree(romf);
        return -1;
    }

    rom_size = f_size(romf);
    if (rom_size == 0) {
        f_close(romf);
        vPortFree(romf);
        return -1;
    }

    emu_rom_addr = pvPortMalloc(rom_size);
    if (!emu_rom_addr) {
        f_close(romf);
        vPortFree(romf);
        rom_size = 0;
        return -1;
    }

    fr = f_read(romf, emu_rom_addr, rom_size, &btr);
    f_close(romf);
    vPortFree(romf);

    if ((fr != FR_OK) || (btr != rom_size)) {
        vPortFree(emu_rom_addr);
        emu_rom_addr = NULL;
        rom_size = 0;
        return -1;
    }

    return 0;
}

void unload_rom(void) {
    if (emu_rom_addr) {
        vPortFree(emu_rom_addr);
        emu_rom_addr = NULL;
    }
    rom_size = 0;
}

int load_port_file(const char *path, void *buf, size_t size) {
    if (!buf || size == 0) return -1;
    FIL *f = pvPortMalloc(sizeof(FIL));
    if (!f) return -1;
    FRESULT fr = f_open(f, path, FA_OPEN_EXISTING | FA_READ);
    if (fr != FR_OK) {
        vPortFree(f);
        return -1;
    }
    UINT br = 0;
    fr = f_read(f, buf, size, &br);
    f_close(f);
    vPortFree(f);
    return (fr == FR_OK && br == size) ? 0 : -1;
}

int save_port_file(const char *path, const void *buf, size_t size) {
    if (!buf || size == 0) return -1;
    FIL *f = pvPortMalloc(sizeof(FIL));
    if (!f) return -1;
    FRESULT fr = f_open(f, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        vPortFree(f);
        return -1;
    }
    UINT bw = 0;
    fr = f_write(f, buf, size, &bw);
    f_close(f);
    vPortFree(f);
    return (fr == FR_OK && bw == size) ? 0 : -1;
}

static uint8_t data_packed[32];
static uint8_t data_unpacked[sizeof(data_packed) * 2];

uint8_t rom_read_nibbles(void *dst, uint32_t nibble_addr, uint32_t size)
{
    if (nibble_addr >= 0x70000000) {
        uint32_t acc_addr = nibble_addr - 0x70000000;

        memcpy(data_packed, (void *)(acc_addr / 2 + (uint8_t *)emu_rom_addr), sizeof(data_packed));

        for (int i = 0, j = 0; i < (int)sizeof(data_packed); i++) {
            data_unpacked[j++] = data_packed[i] & 0xF;
            data_unpacked[j++] = data_packed[i] >> 4;
        }
        memcpy(dst, &data_unpacked[nibble_addr & 1], size);
        return 0;
    } else {
        memcpy(dst, (void *)nibble_addr, size);
        return 0;
    }
}

void *get_emu_rom_addr(void) {
    return emu_rom_addr;
}

size_t get_rom_size(void) {
    return rom_size;
}
