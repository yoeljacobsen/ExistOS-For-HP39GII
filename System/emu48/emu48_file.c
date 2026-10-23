#include "FreeRTOS.h"
#include "task.h"

#include "ff.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * On-demand set-associative LRU unpacked ROM page cache for Emu48.
 *
 * Instead of allocating 512 KB - 1 MB of heap for the entire HP 39g ROM,
 * we keep the FatFs file handle open and cache 32 pages of 1024 unpacked
 * nibbles (plus a 16-nibble overlap tail so FASTPTR instruction fetches
 * never straddle a page boundary). Total cache memory: ~33 KB.
 */

#define ROM_PAGE_SHIFT      10
#define ROM_PAGE_NIBBLES    (1U << ROM_PAGE_SHIFT) /* 1024 nibbles per page */
#define ROM_PAGE_MASK       (ROM_PAGE_NIBBLES - 1U)
#define ROM_PAGE_TAIL       16U                    /* >= max opcode length (13) */

#define ROM_CACHE_SETS      8U
#define ROM_CACHE_WAYS      4U

typedef struct {
    uint32_t page_id;
    uint32_t last_used;
    uint8_t  data[ROM_PAGE_NIBBLES + ROM_PAGE_TAIL];
} rom_cache_way_t;

typedef struct {
    rom_cache_way_t ways[ROM_CACHE_WAYS];
} rom_cache_set_t;

void *emu_rom_addr = NULL;

static FIL *romf = NULL;
static size_t rom_size = 0;
static size_t rom_nibble_size = 0;
static bool rom_isPacked = true;
static rom_cache_set_t *rom_cache = NULL;
static uint32_t rom_cache_tick = 0;
static uint8_t rom_io_buf[(ROM_PAGE_NIBBLES + ROM_PAGE_TAIL) / 2];

bool rom_is_packed(void)
{
    return rom_isPacked;
}

static void rom_cache_populate_way(rom_cache_way_t *way, uint32_t page_id)
{
    UINT br = 0;
    uint32_t start_nibble = page_id << ROM_PAGE_SHIFT;

    memset(way->data, 0, sizeof(way->data));

    if (!romf || start_nibble >= rom_nibble_size) {
        way->page_id = page_id;
        way->last_used = ++rom_cache_tick;
        return;
    }

    if (rom_isPacked) {
        uint32_t file_offset = start_nibble >> 1;
        uint32_t bytes_to_read = (ROM_PAGE_NIBBLES + ROM_PAGE_TAIL) >> 1;
        if (file_offset + bytes_to_read > rom_size) {
            bytes_to_read = (file_offset < rom_size) ? (uint32_t)(rom_size - file_offset) : 0;
        }
        if (bytes_to_read > 0) {
            if (f_lseek(romf, file_offset) == FR_OK &&
                f_read(romf, rom_io_buf, bytes_to_read, &br) == FR_OK) {
                for (UINT i = 0, j = 0; i < br; i++) {
                    way->data[j++] = rom_io_buf[i] & 0x0F;
                    way->data[j++] = (rom_io_buf[i] >> 4) & 0x0F;
                }
            }
        }
    } else {
        uint32_t file_offset = start_nibble;
        uint32_t bytes_to_read = ROM_PAGE_NIBBLES + ROM_PAGE_TAIL;
        if (file_offset + bytes_to_read > rom_size) {
            bytes_to_read = (file_offset < rom_size) ? (uint32_t)(rom_size - file_offset) : 0;
        }
        if (bytes_to_read > 0) {
            if (f_lseek(romf, file_offset) == FR_OK &&
                f_read(romf, way->data, bytes_to_read, &br) == FR_OK) {
                for (UINT i = 0; i < br; i++) {
                    way->data[i] &= 0x0F;
                }
            }
        }
    }

    way->page_id = page_id;
    way->last_used = ++rom_cache_tick;
}

uint8_t *rom_cache_fastptr(uint32_t nibble_offset)
{
    static uint8_t fallback_zero[ROM_PAGE_TAIL] = {0};

    if (!rom_cache) {
        return fallback_zero;
    }
    if (rom_nibble_size > 0) {
        nibble_offset %= (uint32_t)rom_nibble_size;
    }

    uint32_t page_id = nibble_offset >> ROM_PAGE_SHIFT;
    uint32_t in_page = nibble_offset & ROM_PAGE_MASK;
    rom_cache_set_t *set = &rom_cache[page_id & (ROM_CACHE_SETS - 1U)];

    for (uint32_t w = 0; w < ROM_CACHE_WAYS; w++) {
        if (set->ways[w].page_id == page_id) {
            set->ways[w].last_used = ++rom_cache_tick;
            return &set->ways[w].data[in_page];
        }
    }

    uint32_t victim = 0;
    uint32_t oldest = set->ways[0].last_used;
    for (uint32_t w = 1; w < ROM_CACHE_WAYS; w++) {
        if (set->ways[w].last_used < oldest) {
            oldest = set->ways[w].last_used;
            victim = w;
        }
    }

    rom_cache_populate_way(&set->ways[victim], page_id);
    return &set->ways[victim].data[in_page];
}

int load_rom(const char *romfile)
{
    FRESULT fr;
    UINT btr = 0;
    uint8_t header[16];

    romf = pvPortMalloc(sizeof(FIL));
    if (!romf) {
        return -1;
    }

    fr = f_open(romf, romfile, FA_OPEN_EXISTING | FA_READ);
    if (fr != FR_OK && strcmp(romfile, "/rom.39g") == 0) {
        fr = f_open(romf, "/rom.g39", FA_OPEN_EXISTING | FA_READ);
    }
    if (fr != FR_OK) {
        vPortFree(romf);
        romf = NULL;
        return -1;
    }

    rom_size = f_size(romf);
    if (rom_size < 16) {
        f_close(romf);
        vPortFree(romf);
        romf = NULL;
        rom_size = 0;
        return -1;
    }

    fr = f_read(romf, header, sizeof(header), &btr);
    if (fr != FR_OK || btr != sizeof(header)) {
        f_close(romf);
        vPortFree(romf);
        romf = NULL;
        rom_size = 0;
        return -1;
    }

    uint8_t upper_mask = 0;
    for (size_t i = 0; i < sizeof(header); i++) {
        upper_mask |= (header[i] & 0xF0);
    }
    rom_isPacked = (upper_mask != 0);
    rom_nibble_size = rom_isPacked ? (rom_size * 2) : rom_size;

    rom_cache = pvPortMalloc(sizeof(rom_cache_set_t) * ROM_CACHE_SETS);
    if (!rom_cache) {
        f_close(romf);
        vPortFree(romf);
        romf = NULL;
        rom_size = 0;
        rom_nibble_size = 0;
        return -1;
    }

    rom_cache_tick = 0;
    for (uint32_t s = 0; s < ROM_CACHE_SETS; s++) {
        for (uint32_t w = 0; w < ROM_CACHE_WAYS; w++) {
            rom_cache[s].ways[w].page_id = 0xFFFFFFFFU;
            rom_cache[s].ways[w].last_used = 0;
        }
    }

    emu_rom_addr = (void *)0x70000000;
    printf("Emu48 ROM opened (%s, %u bytes, %u nibbles)\n",
           rom_isPacked ? "packed" : "unpacked",
           (unsigned int)rom_size,
           (unsigned int)rom_nibble_size);
    return 0;
}

void unload_rom(void)
{
    if (rom_cache) {
        vPortFree(rom_cache);
        rom_cache = NULL;
    }
    if (romf) {
        f_close(romf);
        vPortFree(romf);
        romf = NULL;
    }
    emu_rom_addr = NULL;
    rom_size = 0;
    rom_nibble_size = 0;
}

int load_port_file(const char *path, void *buf, size_t size)
{
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

int save_port_file(const char *path, const void *buf, size_t size)
{
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

uint8_t rom_read_nibbles(void *dst, uint32_t nibble_addr, uint32_t size)
{
    if (nibble_addr >= 0x70000000U) {
        uint32_t offset = nibble_addr - 0x70000000U;
        uint8_t *out = (uint8_t *)dst;
        while (size > 0) {
            uint32_t in_page = offset & ROM_PAGE_MASK;
            uint32_t chunk = ROM_PAGE_NIBBLES - in_page;
            if (chunk > size) {
                chunk = size;
            }
            const uint8_t *src = rom_cache_fastptr(offset);
            memcpy(out, src, chunk);
            out += chunk;
            offset += chunk;
            size -= chunk;
        }
        return 0;
    } else {
        memcpy(dst, (void *)nibble_addr, size);
        return 0;
    }
}

void *get_emu_rom_addr(void)
{
    return emu_rom_addr;
}

size_t get_rom_size(void)
{
    return rom_size;
}

size_t get_rom_nibble_size(void)
{
    return rom_nibble_size;
}
