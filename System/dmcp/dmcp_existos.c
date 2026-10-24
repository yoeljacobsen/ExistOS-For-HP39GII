#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "FreeRTOS.h"
#include "task.h"

#include "sys_llapi.h"
#include "SystemUI.h"
#include "keyboard_gii39.h"
#include "filesystem/fatfs/ff.h"

// Capture HP 39gII key enum values before dmcp.h redefines KEY_* macros
enum {
    K39_F1             = KEY_F1,
    K39_F2             = KEY_F2,
    K39_F3             = KEY_F3,
    K39_F4             = KEY_F4,
    K39_F5             = KEY_F5,
    K39_F6             = KEY_F6,
    K39_UP             = KEY_UP,
    K39_RIGHT          = KEY_RIGHT,
    K39_LEFT           = KEY_LEFT,
    K39_DOWN           = KEY_DOWN,
    K39_SYMB           = KEY_SYMB,
    K39_NUM            = KEY_NUM,
    K39_HOME           = KEY_HOME,
    K39_PLOT           = KEY_PLOT,
    K39_VIEWS          = KEY_VIEWS,
    K39_XTPHIN         = KEY_XTPHIN,
    K39_VARS           = KEY_VARS,
    K39_APPS           = KEY_APPS,
    K39_ABC            = KEY_ABC,
    K39_BACKSPACE      = KEY_BACKSPACE,
    K39_SIN            = KEY_SIN,
    K39_MATH           = KEY_MATH,
    K39_TAN            = KEY_TAN,
    K39_LN             = KEY_LN,
    K39_LOG            = KEY_LOG,
    K39_X2             = KEY_X2,
    K39_COS            = KEY_COS,
    K39_LEFTBRACKET    = KEY_LEFTBRACKET,
    K39_RIGHTBRACKET   = KEY_RIGHTBRACKET,
    K39_DIVISION       = KEY_DIVISION,
    K39_COMMA          = KEY_COMMA,
    K39_XY             = KEY_XY,
    K39_8              = KEY_8,
    K39_9              = KEY_9,
    K39_MULTIPLICATION = KEY_MULTIPLICATION,
    K39_ALPHA          = KEY_ALPHA,
    K39_7              = KEY_7,
    K39_5              = KEY_5,
    K39_6              = KEY_6,
    K39_SUBTRACTION    = KEY_SUBTRACTION,
    K39_SHIFT          = KEY_SHIFT,
    K39_4              = KEY_4,
    K39_2              = KEY_2,
    K39_3              = KEY_3,
    K39_PLUS           = KEY_PLUS,
    K39_0              = KEY_0,
    K39_1              = KEY_1,
    K39_DOT            = KEY_DOT,
    K39_NEGATIVE       = KEY_NEGATIVE,
    K39_ENTER          = KEY_ENTER,
    K39_ON             = KEY_ON
};

#define __FF_IFC_H__
#include "dmcp.h"

// The DMCP applications (C47, DB48X) each supply these. Only one runs at a
// time, so one platform layer serves whichever is active.
#include <setjmp.h>

#include "dmcp_app.h"

const dmcp_app_t *g_dmcp_app = NULL;

// Cached from the active application: read per pixel in the flush loop.
static bool g_dmcp_ink_set = false;

// DB48X's program_main() is an infinite loop: on a DM42 the firmware ends the
// program from inside sys_sleep(). Reproduce that with a jump back into
// dmcp_run(), so the platform layer's teardown still runs.
static jmp_buf g_dmcp_exit_jmp;

static void dmcp_leave_if_asked(void) {
    if (g_dmcp_app && g_dmcp_app->never_returns && dmcp_exit_requested()) {
        longjmp(g_dmcp_exit_jmp, 1);
    }
}

// Build identifiers. DB48X refuses to start if these differ, which on a DM42
// catches a program flashed against a stale QSPI image. There is no separate
// QSPI image here, so they are equal by construction.
const uint prog_build_id = 0;
const uint qspi_build_id = 0;

// ExistOS built-in 5x8 and 6x12 fonts from System/graphics/vgafont.c
extern const unsigned char VGA_Ascii_5x8[];
extern const unsigned char orp_Ascii_6x12[];

// ExistOS heap / swap management (System/core/main.c, System/utils/newlib_stub.c)
extern uint32_t OnChipMemorySize;
extern uint32_t SwapMemorySize;
extern bool     MemorySwapEnable;
extern void     enableMemSwap(bool enable);
extern size_t   getOnChipHeapAllocated(void);
extern size_t   getSwapMemHeapAllocated(void);
extern uint32_t getHeapAddr(void);


// DMCP System Data Block (sdb)
sys_sdb_t sdb;
static FIL c47_pgm_fp;
static disp_stat_t ds_t20;
static disp_stat_t ds_t24;
static disp_stat_t ds_fReg;
static uint32_t c47_tim2_cnt = 0;
static uint32_t c47_tim3_cnt = 0;

// Dynamically allocated buffers for C47 session
#define C47_LCD_BUF_BYTES   ((LCD_Y + 1) * 52 + 16) // 240 lines + 1 guard line read by screen.c
#define C47_AUX_BUF_BYTES   AUX_BUF_SIZE   // 2,560 bytes
#define C47_WRITE_BUF_BYTES 4096           // 4,096 bytes
#define C47_STRIP_ROWS      16             // 16 rows * 256 bytes = 4,096 bytes

// Statically allocated, not on the heap: DB48X binds its framebuffer in a
// global constructor --
//     surface Screen((pixword *) lcd_line_addr(0), LCD_W, LCD_H, ...)
// -- which runs from .init_array at System boot, long before any application
// starts. A heap buffer does not exist yet at that point, so Screen captured a
// null base and every draw faulted at address 0. The buffer is mostly zero
// pages while idle, which cost nothing in ZRAM.
static uint8_t c47_lcd_storage[C47_LCD_BUF_BYTES];
static uint8_t *c47_raw_lcd_buf = c47_lcd_storage;
static char    *c47_aux_buf     = NULL;
static uint8_t *c47_write_buf   = NULL;
static uint8_t *c47_strip_buf   = NULL;

// One-shot entry trace for the DMCP callbacks C47 reaches in order during startup, so
// the serial log shows how far program_main() got before a fault. Build with
// -DC47_DEBUG_TRACE to turn it on; a faulting indirect call through an unset sdb hook
// leaves no stack trace, and this is what located the last one.
#ifdef DMCP_DEBUG_TRACE
    #define DMCP_TRACE(name)                                   \
        do {                                                  \
            static uint8_t seen_##name = 0;                   \
            if (!seen_##name) {                               \
                seen_##name = 1;                              \
                printf("DMCP trace: " #name "\n");             \
            }                                                 \
        } while (0)
#else
    #define DMCP_TRACE(name) do { } while (0)
#endif

static int c47_buf_cleared = 0;
static int c47_beep_vol    = 2;
static int c47_disk_write  = 1;
static uint32_t c47_printer_delay = 1800;
static char c47_reset_state_file[64] = "/SAVFILES/c47auto.sav";

// Display scaling mode on HP 39gII (256x127 8-bit grayscale):
//   0 = Full 400x240 -> 256x127 anti-aliased grayscale
//   1 = Zoomed Stack + Softkeys (Status bar + large Y/X stack + softkeys)
//   2 = 1:1 Pixel-Exact Pan (hold ON + Arrow keys to pan across 400x240)
static int c47_disp_mode = 0;
static int c47_pan_x = 0;
static int c47_pan_y = 113; // Default to bottom half (stack + softkeys)
static int c47_help_page = 0; // 0 = off, 1 = page 1, 2 = page 2
static uint32_t c47_banner_until_ms = 0;
static char c47_banner_text[32] = "";

// The HP 39gII panel is 8-bit grayscale with 0x00 darkest and 0xFF lightest (the
// ExistOS UI draws black text as 0 on a 208 background). C47 composes a 1bpp image
// where a set bit is "ink", so ink maps to dark shades and bare paper to light ones.
#define C47_PAPER   0xFF
#define C47_INK     0x00
#define C47_PANEL   0xE8  // help-overlay body
#define C47_BAR     0x20  // help-overlay title bar
#define C47_BAR_TXT 0xF8  // text on the title bar

// Precomputed X mapping table: col_x0[x] = (x * 400) / 256
static uint16_t c47_col_x0[LCD_PIX_W + 1];
static bool c47_tables_initialized = false;

static void c47_init_tables(void) {
    if (c47_tables_initialized) return;
    for (int x = 0; x <= LCD_PIX_W; x++) {
        c47_col_x0[x] = (uint16_t)((x * LCD_X) / LCD_PIX_W);
    }
    c47_tables_initialized = true;
}

// Helper: test if pixel (cx, cy) in C47 400x240 buffer is black (ON)
static inline int c47_px_black(const uint8_t *line_ptr, int cx) {
    int rx = (LCD_X - 1) - cx;
    uint8_t b = line_ptr[2 + (rx >> 3)];
    int bit = (b >> (rx & 7)) & 1;
    return g_dmcp_ink_set ? bit : !bit;
}

// Draw a 5x8 ASCII string into the 256-pixel strip buffer (sy0..sy0+sh-1)
static void strip_draw_str5x8(uint8_t *strip, int sy0, int sh, int x, int y, const char *s, uint8_t fg, uint8_t bg) {
    while (*s) {
        unsigned char ch = (unsigned char)*s++;
        if (ch < 32 || ch > 126) ch = '?';
        const unsigned char *glyph = &VGA_Ascii_5x8[(ch - 32) * 8];
        for (int r = 0; r < 8; r++) {
            int py = y + r;
            if (py < sy0 || py >= sy0 + sh) continue;
            uint8_t rowbits = glyph[r];
            uint8_t *dst = &strip[(py - sy0) * LCD_PIX_W];
            for (int c = 0; c < 6; c++) {
                int px = x + c;
                if (px < 0 || px >= LCD_PIX_W) continue;
                bool bit = (c < 5) ? ((rowbits >> (7 - c)) & 1) : false;
                dst[px] = bit ? fg : bg;
            }
        }
        x += 6;
    }
}

// Fill a rectangle in the strip buffer
static void strip_fill_rect(uint8_t *strip, int sy0, int sh, int x0, int y0, int w, int h, uint8_t color) {
    int x1 = x0 + w;
    int y1 = y0 + h;
    if (x0 < 0) x0 = 0;
    if (x1 > LCD_PIX_W) x1 = LCD_PIX_W;
    if (x0 >= x1) return;
    for (int py = y0; py < y1; py++) {
        if (py < sy0 || py >= sy0 + sh) continue;
        memset(&strip[(py - sy0) * LCD_PIX_W + x0], color, (size_t)(x1 - x0));
    }
}

static void c47_render_help_strip(uint8_t *strip, int sy0, int sh) {
    strip_fill_rect(strip, sy0, sh, 0, sy0, LCD_PIX_W, sh, C47_PANEL);
    strip_fill_rect(strip, sy0, sh, 0, 0, LCD_PIX_W, 11, C47_BAR);

    if (c47_help_page == 1) {
        strip_draw_str5x8(strip, sy0, sh, 4,   2, "C47 KEYMAP (1/2)            [APPS] next", C47_BAR_TXT, C47_BAR);
        strip_draw_str5x8(strip, sy0, sh, 4,  14, "F1..F6  softkeys      SYMB   Sigma+",     C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4,  24, "PLOT    1/x           NUM    sqrt",       C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4,  34, "LOG     log/10^x      LN     ln/e^x",     C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4,  44, "HOME    XEQ           VARS   STO",        C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4,  54, "a b/c   RCL           X,T,n  Rv (roll)",  C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4,  64, "SIN COS TAN  as labelled",                C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4,  74, ",       x<>y          (-)    +/- (CHS)",  C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4,  84, "(       EEX           )      R/S",        C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4,  94, "SHIFT   f, then g     BKSP   backspace",  C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4, 104, "ON/C    EXIT      SHIFT+ON  quit & save", C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4, 116, "Digits, . and + - x / are direct.",       C47_INK, C47_PANEL);
    } else {
        strip_draw_str5x8(strip, sy0, sh, 4,   2, "C47 KEYMAP (2/2)           [APPS] close", C47_BAR_TXT, C47_BAR);
        strip_draw_str5x8(strip, sy0, sh, 4,  14, "Keys the HP has and the DM42 does not,",  C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4,  24, "sent as a shifted pair:",                 C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4,  34, " x^2    f+sqrt        x^y    f+1/x",      C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4,  44, " ALPHA  f+XEQ (AIM)   MATH   f+ (CATALOG)",C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4,  56, "HP shifted legends that C47 keeps",       C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4,  66, "elsewhere, redirected to match:",         C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4,  76, " SHIFT+1 PRGM (P/R)   SHIFT+4 MATRIX",    C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4,  86, " SHIFT+7 LIST (STAT)  others: CATALOG",   C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4,  98, "UP/DOWN stack & menus  LEFT x<>y",        C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4, 108, "RIGHT Rv   VIEWS cycles display mode",    C47_INK, C47_PANEL);
        strip_draw_str5x8(strip, sy0, sh, 4, 118, "In 1:1 mode the arrows pan the view.",    C47_INK, C47_PANEL);
    }
}

static void c47_update_indicators(void) {
    int ind = (g_dmcp_app && g_dmcp_app->indicators) ? g_dmcp_app->indicators() : 0;
    ll_disp_set_indicator(ind, -1);
}

static void c47_flush_screen(void) {
    if (!c47_raw_lcd_buf || !c47_strip_buf) return;
    c47_init_tables();
    c47_update_indicators();

    uint32_t now = ll_get_time_ms();
    bool show_banner = (c47_banner_until_ms != 0 && now < c47_banner_until_ms);

    for (int sy0 = 0; sy0 < LCD_PIX_H; sy0 += C47_STRIP_ROWS) {
        int sh = C47_STRIP_ROWS;
        if (sy0 + sh > LCD_PIX_H) sh = LCD_PIX_H - sy0;

        if (c47_help_page != 0) {
            c47_render_help_strip(c47_strip_buf, sy0, sh);
            ll_disp_put_area(c47_strip_buf, 0, (uint32_t)sy0, LCD_PIX_W - 1, (uint32_t)(sy0 + sh - 1));
            continue;
        }

        for (int r = 0; r < sh; r++) {
            int dy = sy0 + r;
            uint8_t *dst = &c47_strip_buf[r * LCD_PIX_W];

            if (c47_disp_mode == 2) {
                // Mode 2: 1:1 Pixel-Exact Pan (256x127 window into 400x240)
                int cy = c47_pan_y + dy;
                if (cy < 0) cy = 0;
                if (cy >= LCD_Y) cy = LCD_Y - 1;
                const uint8_t *line = &c47_raw_lcd_buf[cy * 52];
                for (int dx = 0; dx < LCD_PIX_W; dx++) {
                    int cx = c47_pan_x + dx;
                    if (cx < 0) cx = 0;
                    if (cx >= LCD_X) cx = LCD_X - 1;
                    dst[dx] = c47_px_black(line, cx) ? C47_INK : C47_PAPER;
                }
            } else {
                // Mode 0 (Full 400x240) or Mode 1 (Zoomed Status + Stack Y/X + Softkeys)
                int cy0, cy1;
                if (c47_disp_mode == 0) {
                    cy0 = (dy * LCD_Y) / LCD_PIX_H;
                    cy1 = ((dy + 1) * LCD_Y) / LCD_PIX_H;
                } else {
                    // Mode 1:
                    //   dy 0..11   (12 rows) -> cy 0..17    (18 rows: status bar)
                    //   dy 12..98  (87 rows) -> cy 84..199  (116 rows: Z, Y, X registers / AIM / NIM / PEM)
                    //   dy 99..126 (28 rows) -> cy 200..239 (40 rows: softmenu bar)
                    if (dy < 12) {
                        cy0 = (dy * 18) / 12;
                        cy1 = ((dy + 1) * 18) / 12;
                    } else if (dy < 99) {
                        cy0 = 84 + ((dy - 12) * 116) / 87;
                        cy1 = 84 + ((dy - 11) * 116) / 87;
                    } else {
                        cy0 = 200 + ((dy - 99) * 40) / 28;
                        cy1 = 200 + ((dy - 98) * 40) / 28;
                    }
                }
                if (cy1 <= cy0) cy1 = cy0 + 1;
                if (cy1 > LCD_Y) cy1 = LCD_Y;

                const uint8_t *line0 = &c47_raw_lcd_buf[cy0 * 52];
                const uint8_t *line1 = (cy0 + 1 < cy1) ? &c47_raw_lcd_buf[(cy0 + 1) * 52] : NULL;

                for (int dx = 0; dx < LCD_PIX_W; dx++) {
                    int cx0 = c47_col_x0[dx];
                    int cx1 = c47_col_x0[dx + 1];
                    if (cx1 <= cx0) cx1 = cx0 + 1;

                    int black = 0;
                    int total = 0;
                    for (int cx = cx0; cx < cx1; cx++) {
                        black += c47_px_black(line0, cx);
                        total++;
                        if (line1) {
                            black += c47_px_black(line1, cx);
                            total++;
                        }
                    }

                    // Coverage -> shade, biased dark: on this STN panel a stroke thinned
                    // to one subpixel by the 400->256 downscale still has to read as a
                    // stroke, so partial coverage lands nearer ink than linear would put it.
                    uint8_t shade = C47_PAPER;
                    if (black > 0) {
                        if (black == total) {
                            shade = C47_INK;
                        } else if (total == 4) {
                            static const uint8_t lut4[5] = {0xFF, 0x8F, 0x47, 0x17, 0x00};
                            shade = lut4[black];
                        } else if (total == 2) {
                            shade = (black == 1) ? 0x67 : C47_INK;
                        } else {
                            int v = 255 - (black * 300) / total;
                            if (v < 0) v = 0;
                            shade = (uint8_t)v;
                        }
                    }
                    dst[dx] = shade;
                }
            }
        }

        if (show_banner && sy0 == 0) {
            int bw = (int)strlen(c47_banner_text) * 6 + 6;
            int bx = (LCD_PIX_W - bw) / 2;
            strip_fill_rect(c47_strip_buf, sy0, sh, bx, 0, bw, 10, C47_BAR);
            strip_draw_str5x8(c47_strip_buf, sy0, sh, bx + 3, 1, c47_banner_text, C47_BAR_TXT, C47_BAR);
        }

        ll_disp_put_area(c47_strip_buf, 0, (uint32_t)sy0, LCD_PIX_W - 1, (uint32_t)(sy0 + sh - 1));
    }

    // Clear dirty flags in C47 lcd_buffer
    for (int row = 0; row < LCD_Y; row++) {
        c47_raw_lcd_buf[row * 52] = 0;
    }
}

static void c47_show_banner(const char *msg) {
    strncpy(c47_banner_text, msg, sizeof(c47_banner_text) - 1);
    c47_banner_text[sizeof(c47_banner_text) - 1] = '\0';
    c47_banner_until_ms = ll_get_time_ms() + 1200;
    c47_flush_screen();
}

// ============================================================================
// DMCP5 LCD & Graphics API Implementation
// ============================================================================

uint8_t *lcd_line_addr(int y) {
    DMCP_TRACE(lcd_line_addr);
    if (!c47_raw_lcd_buf) return NULL;
    if (y < 0) y = 0;
    if (y >= LCD_Y) y = LCD_Y - 1;
    c47_raw_lcd_buf[y * 52] = 1u;
    return &c47_raw_lcd_buf[y * 52 + 2];
}

void LCD_write_line(uint8_t *buf) {
    if (!buf) return;
    buf[0] = 0u;
}

void lcd_clear_buf(void) {
    DMCP_TRACE(lcd_clear_buf);
    if (!c47_raw_lcd_buf) return;
    for (int row = 0; row < LCD_Y; row++) {
        uint8_t *line_buf = &c47_raw_lcd_buf[row * 52];
        line_buf[0] = 1u;
        line_buf[1] = (uint8_t)(LCD_Y - 1 - row);
        memset(&line_buf[2], g_dmcp_ink_set ? 0x00 : 0xFF, 50);
    }
    c47_buf_cleared = 1;
}

void lcd_refresh(void) {
    DMCP_TRACE(lcd_refresh);
    c47_flush_screen();
}

void lcd_refresh_dma(void) {
    c47_flush_screen();
}

void lcd_refresh_wait(void) {
    c47_flush_screen();
}

void lcd_forced_refresh(void) {
    DMCP_TRACE(lcd_forced_refresh);
    c47_flush_screen();
}

void lcd_refresh_lines(int ln, int cnt) {
    DMCP_TRACE(lcd_refresh_lines);
    (void)ln;
    (void)cnt;
    c47_flush_screen();
}

void lcd_set_buf_cleared(int val) {
    c47_buf_cleared = val;
}

int lcd_get_buf_cleared(void) {
    return c47_buf_cleared;
}

void LCD_power_on(void) {
}

void LCD_power_off(int clear) {
    (void)clear;
    // When user presses SHIFT + EXIT (OFF) in C47, exit cleanly back to ExistOS launcher
    dmcp_request_exit();
}

void draw_power_off_image(int allow_errors) {
    (void)allow_errors;
}

void bitblt24(uint32_t x, uint32_t dx, uint32_t y, uint32_t val, int blt_op, int fill) {
    DMCP_TRACE(bitblt24);
    if (!c47_raw_lcd_buf || dx < 1 || dx > 24 || y >= LCD_Y) {
        return;
    }
    if (x >= LCD_X || x + dx > LCD_X) {
        return;
    }
    x = LCD_X - dx - x;

    const uint32_t byte_i   = x >> 3;
    const uint32_t bit_off  = x & 7u;
    const uint32_t lowmask  = (1u << dx) - 1u;
    const uint32_t bytes_needed = (bit_off + dx + 7) / 8;

    const uint32_t srcbits  = (val & lowmask) << bit_off;
    const uint32_t fillbits = (fill == BLT_SET) ? (lowmask << bit_off) : 0u;

    uint8_t srcbytes[4] = {
        (uint8_t)(srcbits >> 0),
        (uint8_t)(srcbits >> 8),
        (uint8_t)(srcbits >> 16),
        (uint8_t)(srcbits >> 24),
    };
    uint8_t fillbytes[4] = {
        (uint8_t)(fillbits >> 0),
        (uint8_t)(fillbits >> 8),
        (uint8_t)(fillbits >> 16),
        (uint8_t)(fillbits >> 24),
    };

    uint8_t *j = &c47_raw_lcd_buf[y * (LCD_LINE_SIZE + 2) + byte_i + 2];
    switch (blt_op) {
        case BLT_OR:
            for (uint32_t i = 0; i < bytes_needed; i++) {
                j[i] = (j[i] | fillbytes[i]) & ~srcbytes[i];
            }
            break;
        case BLT_XOR:
            for (uint32_t i = 0; i < bytes_needed; i++) {
                j[i] ^= srcbytes[i];
            }
            break;
        case BLT_ANDN:
            for (uint32_t i = 0; i < bytes_needed; i++) {
                j[i] = (j[i] & ~fillbytes[i]) | srcbytes[i];
            }
            break;
        default:
            return;
    }
    c47_raw_lcd_buf[y * (LCD_LINE_SIZE + 2)] = 1u;
}

void lcd_fill_rect(uint32_t x, uint32_t y, uint32_t dx, uint32_t dy, int val) {
    uint32_t endX = x + dx;
    uint32_t endY = y + dy;
    if (endX > LCD_X || endY > LCD_Y) {
        return;
    }
    int blt_op = val ? BLT_OR : BLT_ANDN;
    for (uint32_t col = x; col < endX; col += 24) {
        uint32_t cols = (endX - col < 24) ? (endX - col) : 24;
        for (uint32_t line = y; line < endY; line++) {
            bitblt24(col, cols, line, 0xFFFFFFu, blt_op, BLT_NONE);
        }
    }
}

void lcd_setLine(disp_stat_t *ds, int ln_nr) {
    if (!ds) return;
    ds->x = 4;
    ds->y = (int16_t)(ln_nr * 24);
}

void lcd_writeText(disp_stat_t *ds, const char *text) {
    DMCP_TRACE(lcd_writeText);
    if (!ds || !text) return;
    int x = ds->x;
    int y = ds->y;
    if (y < 0) y = 0;
    if (y + 24 <= LCD_Y) {
        lcd_fill_rect(0, (uint32_t)y, LCD_X, 24, ds->inv ? LCD_EMPTY_VALUE : LCD_SET_VALUE);
    }
    while (*text && x + 12 <= LCD_X) {
        unsigned char ch = (unsigned char)*text++;
        if (ch < 32 || ch > 126) ch = '?';
        const unsigned char *glyph = &orp_Ascii_6x12[(ch - 32) * 12];
        for (int r = 0; r < 12; r++) {
            uint8_t rowbits = glyph[r];
            uint32_t pat = 0;
            for (int c = 0; c < 6; c++) {
                int bit = (rowbits >> (7 - c)) & 1;
                pat = (pat << 2) | (bit ? 3u : 0u);
            }
            int op = ds->inv ? BLT_ANDN : BLT_OR;
            if (y + r * 2 + 1 < LCD_Y) {
                bitblt24((uint32_t)x, 12, (uint32_t)(y + r * 2),     pat, op, BLT_NONE);
                bitblt24((uint32_t)x, 12, (uint32_t)(y + r * 2 + 1), pat, op, BLT_NONE);
            }
        }
        x += 12;
    }
    ds->y += 24;
    ds->x = 4;
}

int create_screenshot(int report_error) {
    (void)report_error;
    return 0;
}

// ============================================================================
// HP 39gII -> C47 Keyboard Mapping & Input Queue
// ============================================================================

#define C47_KEY_QUEUE_SIZE 32
static int c47_key_q[C47_KEY_QUEUE_SIZE];
static int c47_key_q_head = 0;
static int c47_key_q_tail = 0;

static uint32_t c47_last_raw_keys = 0;
static bool     c47_on_held       = false;
static int      c47_active_c47_key = 0;

static void c47_q_push(int k) {
    int next = (c47_key_q_tail + 1) & (C47_KEY_QUEUE_SIZE - 1);
    if (next != c47_key_q_head) {
        c47_key_q[c47_key_q_tail] = k;
        c47_key_q_tail = next;
    }
}

// Push a synthetic prefix key tap (e.g. f-shift) before the main key
static void c47_push_shifted_key(int shift_key, int main_key) {
    c47_q_push(shift_key);
    c47_q_push(0);
    c47_q_push(main_key);
    c47_active_c47_key = main_key;
}

// f/g shift state. The applications keep their own, so the platform layer
// tracks what it has sent: 0 = none, 1 = f pending, 2 = g pending.
static int c47_shift_level = 0;

static int c47_shift_pending(void) {
    return c47_shift_level;
}

static int c47_map_39gii_key(uint32_t k39) {
    switch (k39) {
        // Row 1: 6 Softmenu Function Keys
        case K39_F1:             return 38; // KEY_F1
        case K39_F2:             return 39; // KEY_F2
        case K39_F3:             return 40; // KEY_F3
        case K39_F4:             return 41; // KEY_F4
        case K39_F5:             return 42; // KEY_F5
        case K39_F6:             return 43; // KEY_F6

        // Row 2 & Navigation Cluster: Top Scientific / Stack Keys
        case K39_SYMB:           return 1;  // KEY_SIGMA (Sigma+ / catalog)
        case K39_PLOT:           return 2;  // KEY_INV   (1/x / y^x)
        case K39_NUM:            return 3;  // KEY_SQRT  (sqrt / x^2)
        case K39_HOME:           return 6;  // KEY_XEQ   (XEQ / GTO / MATH)
        case K39_VARS:           return 7;  // KEY_STO   (STO)
        case K39_ABC:            return 8;  // KEY_RCL   (RCL)
        case K39_XTPHIN:         return 9;  // KEY_RDN   (R down / pi)
        case K39_BACKSPACE:      return 17; // KEY_BSP   (Backspace / CLX / DROP)

        // D-pad. The DM42 keyboard has no horizontal arrows, so left/right take the
        // stack meanings that read naturally in that direction; they duplicate the
        // "," and X,T,n keys rather than going unused.
        case K39_UP:             return 18; // KEY_UP    (up / BST)
        case K39_DOWN:           return 23; // KEY_DOWN  (down / SST)
        case K39_LEFT:           return 14; // KEY_SWAP  (x<>y)
        case K39_RIGHT:          return 9;  // KEY_RDN   (roll down)

        // Row 4: Trig & Logs
        case K39_SIN:            return 10; // KEY_SIN
        case K39_COS:            return 11; // KEY_COS
        case K39_TAN:            return 12; // KEY_TAN
        case K39_LN:             return 5;  // KEY_LN
        case K39_LOG:            return 4;  // KEY_LOG

        // Row 5 & 6: Brackets, EEX, R/S, Swap
        case K39_LEFTBRACKET:    return 16; // KEY_E     (EEX)
        case K39_RIGHTBRACKET:   return 36; // KEY_RUN   (R/S)
        case K39_COMMA:          return 14; // KEY_SWAP  (x<>y)

        // Shifts & Modifiers
        case K39_SHIFT:          return 28; // KEY_SHIFT (f / g shift toggle)

        // Numeric & Arithmetic Keypad
        case K39_DIVISION:       return 22; // KEY_DIV
        case K39_MULTIPLICATION: return 27; // KEY_MUL
        case K39_SUBTRACTION:    return 32; // KEY_SUB
        case K39_PLUS:           return 37; // KEY_ADD
        case K39_ENTER:          return 13; // KEY_ENTER
        case K39_NEGATIVE:       return 15; // KEY_CHS   (+/-)
        case K39_DOT:            return 35; // KEY_DOT   (.)
        case K39_0:              return 34; // KEY_0
        case K39_1:              return 29; // KEY_1
        case K39_2:              return 30; // KEY_2
        case K39_3:              return 31; // KEY_3
        case K39_4:              return 24; // KEY_4
        case K39_5:              return 25; // KEY_5
        case K39_6:              return 26; // KEY_6
        case K39_7:              return 19; // KEY_7
        case K39_8:              return 20; // KEY_8
        case K39_9:              return 21; // KEY_9
        default:                 return 0;
    }
}

static void c47_poll_hardware_keys(void) {
    uint32_t keys = ll_vm_check_key();
    if (keys == c47_last_raw_keys) {
        return;
    }
    c47_last_raw_keys = keys;

    uint32_t key = keys & 0xFFFFu;
    uint32_t kpress = (keys >> 16) & 0xFFFFu;

    if (kpress) {
        // Key Press event.
        //
        // ON + <key> combinations are unreachable here: OSLoader's key_task_capt()
        // state machine stops publishing key_status as soon as ON goes down and hands
        // the combination to capt_ON_Key() instead, which owns ON+F2/F3/F5/F6 and
        // ON+/- globally. ON+F3 there is portBoardPowerOff(). So the port must not
        // claim any ON+key chord; a plain ON tap is still delivered and means EXIT.
        if (key == K39_ON) {
            c47_on_held = true;
            return;
        }

        // 1:1 pan mode owns the arrows while it is showing, since panning is the whole
        // point of that mode; switch back with VIEWS to give C47 its arrows again.
        if (c47_disp_mode == 2 && c47_help_page == 0) {
            switch (key) {
                case K39_LEFT:  c47_pan_x -= 36; if (c47_pan_x < 0) c47_pan_x = 0; c47_flush_screen(); return;
                case K39_RIGHT: c47_pan_x += 36; if (c47_pan_x > LCD_X - LCD_PIX_W) c47_pan_x = LCD_X - LCD_PIX_W; c47_flush_screen(); return;
                case K39_UP:    c47_pan_y -= 28; if (c47_pan_y < 0) c47_pan_y = 0; c47_flush_screen(); return;
                case K39_DOWN:  c47_pan_y += 28; if (c47_pan_y > LCD_Y - LCD_PIX_H) c47_pan_y = LCD_Y - LCD_PIX_H; c47_flush_screen(); return;
                default: break;
            }
        }

        // APPS key: toggle 2-page on-screen keymap reference overlay
        if (key == K39_APPS) {
            c47_help_page = (c47_help_page + 1) % 3;
            c47_flush_screen();
            return;
        }
        // If help overlay is open, any other key closes it
        if (c47_help_page != 0) {
            c47_help_page = 0;
            c47_flush_screen();
            return;
        }

        // VIEWS key: cycle display scaling mode (Full AA -> Zoom Stack -> 1:1 Pan)
        if (key == K39_VIEWS) {
            c47_disp_mode = (c47_disp_mode + 1) % 3;
            if (c47_disp_mode == 0) {
                c47_show_banner("VIEW: FULL 400x240 GRAYSCALE");
            } else if (c47_disp_mode == 1) {
                c47_show_banner("VIEW: ZOOM STACK + SOFTKEYS");
            } else {
                c47_show_banner("VIEW: 1:1 PAN (ON+ARROWS)");
            }
            return;
        }

        // Four HP keys have no DM42 counterpart but name a function C47 reaches via
        // f-shift, so they are sent as a synthetic f + key pair:
        //   x^2   -> f + sqrt  (key 3)   x^y   -> f + 1/x  (key 2)
        //   ALPHA -> f + XEQ   (key 6)   MATH  -> f + "+"  (key 37, CATALOG)
        // With a shift already pending the user is after that shift's own meaning, so
        // pass the base key straight through (e.g. g + ALPHA = g + XEQ = GTO).
        {
            int base = 0;
            switch (key) {
                case K39_X2:    base = 3;  break;  // f + sqrt = x^2
                case K39_XY:    base = 2;  break;  // f + 1/x  = y^x
                case K39_ALPHA: base = 6;  break;  // f + XEQ  = AIM (alpha entry)
                case K39_MATH:  base = 37; break;  // f + "+"  = CATALOG
                default: break;
            }
            if (base) {
                if (!c47_shift_pending()) {
                    c47_push_shifted_key(28, base);
                    c47_shift_level = 0;
                } else {
                    c47_active_c47_key = base;
                    c47_q_push(base);
                }
                return;
            }
        }

        // The HP 39gII prints shifted legends on its numeric block, and three of them
        // name functions C47 keeps on other keys. With f pending, redirect those keys
        // to the DM42 key whose f-shift *is* the labelled function, so the legend on
        // the case tells the truth:
        //
        //   SHIFT+1 "PRGM"   -> f + R/S = P/R, program entry mode
        //   SHIFT+4 "MATRIX" -> f + 9   = MATX menu
        //   SHIFT+7 "LIST"   -> f + /   = STAT menu (C47 has no list type; its
        //                                statistics registers are the nearest thing)
        //
        // This costs C47's own f+1 (ASSIGN), f+4 (BASE) and f+7 (EQN); all three stay
        // reachable through CATALOG (the MATH key), whose MENUS and FCNS pages list
        // every menu and function. g-shift on these keys is untouched.
        if (c47_shift_pending() == 1) {
            int redirect = 0;
            switch (key) {
                case K39_1: redirect = 36; break; // R/S -> f = P/R
                case K39_4: redirect = 21; break; // 9   -> f = MATX
                case K39_7: redirect = 22; break; // /   -> f = STAT
                default: break;
            }
            if (redirect) {
                c47_shift_level = 0;
                c47_active_c47_key = redirect;
                c47_q_push(redirect);
                return;
            }
        }

        int mapped = c47_map_39gii_key(key);
        if (mapped > 0) {
            if (mapped == 28) {                 // SHIFT: none -> f -> g -> none
                c47_shift_level = (c47_shift_level + 1) % 3;
            } else {
                c47_shift_level = 0;            // any other key consumes it
            }
            c47_active_c47_key = mapped;
            c47_q_push(mapped);
        }
    } else {
        // Key Release event
        if (key == K39_ON) {
            if (c47_on_held) {
                // Release of ON/C -> tap C47 KEY_EXIT (33). With f pending this is
                // C47's OFF, which is how the user quits back to the ExistOS launcher.
                c47_q_push(33);
                c47_q_push(0);
            }
            c47_on_held = false;
            return;
        }
        if (c47_active_c47_key != 0) {
            c47_q_push(0);
            c47_active_c47_key = 0;
        }
    }
}

int key_empty(void) {
    c47_poll_hardware_keys();
    return (c47_key_q_head == c47_key_q_tail) ? 1 : 0;
}

int key_pop(void) {
    DMCP_TRACE(key_pop);
    c47_poll_hardware_keys();
    if (c47_key_q_head == c47_key_q_tail) {
        return -1;
    }
    int k = c47_key_q[c47_key_q_head];
    c47_key_q_head = (c47_key_q_head + 1) & (C47_KEY_QUEUE_SIZE - 1);
    return k;
}

void key_pop_all(void) {
    c47_key_q_head = 0;
    c47_key_q_tail = 0;
}

int key_push(int k1) {
    c47_q_push(k1);
    return 0;
}

int key_tail(void) {
    if (c47_key_q_head == c47_key_q_tail) {
        return -1;
    }
    int prev = (c47_key_q_tail - 1) & (C47_KEY_QUEUE_SIZE - 1);
    return c47_key_q[prev];
}

int runner_get_key(int *repeat) {
    dmcp_leave_if_asked();
    if (repeat) *repeat = 0;
    while (!dmcp_exit_requested()) {
        int k = key_pop();
        if (k > 0) {
            return k;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    return KEY_EXIT;
}

void wait_for_key_press(void) {
    int rep = 0;
    (void)runner_get_key(&rep);
}

void wait_for_key_release(int tout) {
    (void)tout;
    uint32_t start = ll_get_time_ms();
    while (ll_get_time_ms() - start < 500) {
        c47_poll_hardware_keys();
        if (c47_active_c47_key == 0 && !c47_on_held) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    // Drain any trailing key-up (0) events
    while (c47_key_q_head != c47_key_q_tail && c47_key_q[c47_key_q_head] == 0) {
        c47_key_q_head = (c47_key_q_head + 1) & (C47_KEY_QUEUE_SIZE - 1);
    }
}

// ============================================================================
// DMCP5 Timers, Sleep, RTC, Power, Buzzer, Printer & File Dialogs
// ============================================================================

static uint32_t c47_systim_deadline[SYSTIM_COUNT];
static uint8_t  c47_systim_enabled[SYSTIM_COUNT];

uint32_t sys_current_ms(void) {
    DMCP_TRACE(sys_current_ms);
    return ll_get_time_ms();
}

void sys_delay(uint32_t ms_delay) {
    vTaskDelay(pdMS_TO_TICKS(ms_delay > 0 ? ms_delay : 1));
}

void sys_timer_start(int timer_ix, uint32_t ms_value) {
    if (timer_ix >= 0 && timer_ix < SYSTIM_COUNT) {
        c47_systim_deadline[timer_ix] = ll_get_time_ms() + ms_value;
        c47_systim_enabled[timer_ix]  = 1;
    }
}

void sys_timer_disable(int timer_ix) {
    if (timer_ix >= 0 && timer_ix < SYSTIM_COUNT) {
        c47_systim_enabled[timer_ix] = 0;
    }
}

int sys_timer_active(int timer_ix) {
    if (timer_ix >= 0 && timer_ix < SYSTIM_COUNT) {
        return c47_systim_enabled[timer_ix];
    }
    return 0;
}

int sys_timer_timeout(int timer_ix) {
    if (timer_ix >= 0 && timer_ix < SYSTIM_COUNT && c47_systim_enabled[timer_ix]) {
        return ((int32_t)(ll_get_time_ms() - c47_systim_deadline[timer_ix]) >= 0) ? 1 : 0;
    }
    return 0;
}

void sys_sleep(void) {
    DMCP_TRACE(sys_sleep);

    // The application may have asked to end itself (DB48X sets STAT_PGM_END and
    // expects sys_sleep() not to return).
    if (calc_state & STAT_PGM_END) {
        dmcp_request_exit();
    }
    dmcp_leave_if_asked();
    // Sleep until either a key arrives, a DMCP timer expires, or 40 ms elapses
    uint32_t start = ll_get_time_ms();
    uint32_t max_wait = 40;

    for (int i = 0; i < SYSTIM_COUNT; i++) {
        if (c47_systim_enabled[i]) {
            int32_t rem = (int32_t)(c47_systim_deadline[i] - start);
            if (rem <= 0) {
                max_wait = 1;
                break;
            }
            if ((uint32_t)rem < max_wait) {
                max_wait = (uint32_t)rem;
            }
        }
    }

    uint32_t elapsed = 0;
    while (elapsed < max_wait && !dmcp_exit_requested()) {
        c47_poll_hardware_keys();
        if (c47_key_q_head != c47_key_q_tail) {
            break;
        }
        uint32_t step = (max_wait - elapsed > 10) ? 10 : (max_wait - elapsed);
        if (step == 0) step = 1;
        vTaskDelay(pdMS_TO_TICKS(step));
        elapsed = ll_get_time_ms() - start;
    }
}

void reset_auto_off(void) {
}

int is_menu_auto_off(void) {
    return 0;
}

void sys_reset(void) {
    dmcp_request_exit();
}

int run_menu_item_sys(uint8_t line_id) {
    if (line_id == MI_DMCP_MENU) {
        // Triggered if user invokes Activated System Menu -> return to ExistOS
        dmcp_request_exit();
        return MRET_EXIT;
    }
    return 0;
}

// Convert Unix days since 1970-01-01 to civil year/month/day + day of week
static uint32_t c47_rtc_day_offset = 20749; // 2026-10-23 (days since 1970-01-01)

void rtc_read(tm_t *tm, dt_t *dt) {
    DMCP_TRACE(rtc_read);
    uint32_t sec = ll_rtc_get_sec();
    uint32_t sod = sec % 86400u;
    uint32_t days = c47_rtc_day_offset + (sec / 86400u);

    if (tm) {
        tm->hour = (uint8_t)(sod / 3600u);
        tm->min  = (uint8_t)((sod % 3600u) / 60u);
        tm->sec  = (uint8_t)(sod % 60u);
        tm->csec = (uint8_t)((ll_get_time_ms() / 10u) % 100u);
        tm->dow  = (uint8_t)((days + 3u) % 7u); // 0 = Monday .. 6 = Sunday
    }
    if (dt) {
        // Howard Hinnant civil_from_days algorithm
        int32_t z = (int32_t)days + 719468;
        int32_t era = (z >= 0 ? z : z - 146096) / 146097;
        uint32_t doe = (uint32_t)(z - era * 146097);
        uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        int32_t y = (int32_t)yoe + era * 400;
        uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        uint32_t mp = (5 * doy + 2) / 153;
        uint32_t d = doy - (153 * mp + 2) / 5 + 1;
        uint32_t m = mp + (mp < 10 ? 3 : -9);
        y += (m <= 2);
        dt->year  = (uint16_t)y;
        dt->month = (uint8_t)m;
        dt->day   = (uint8_t)d;
    }
}

void rtc_write(tm_t *tm, dt_t *dt) {
    if (dt) {
        int32_t y = dt->year;
        uint32_t m = dt->month;
        uint32_t d = dt->day;
        y -= (m <= 2);
        int32_t era = (y >= 0 ? y : y - 399) / 400;
        uint32_t yoe = (uint32_t)(y - era * 400);
        uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        int32_t days = era * 146097 + (int32_t)doe - 719468;
        if (days > 0) {
            c47_rtc_day_offset = (uint32_t)days;
        }
    }
    if (tm) {
        uint32_t sod = (uint32_t)tm->hour * 3600u + (uint32_t)tm->min * 60u + (uint32_t)tm->sec;
        ll_rtc_set_sec(sod);
    }
}

void rtc_wakeup_delay(void) {
}

// ll_get_bat_voltage() returns POWER_BATTMONITOR.BATT_VAL * 8, the STMP3770 battery
// monitor's single-cell reading in mV. OSLoader's own thresholds put the usable window
// at roughly 1000..1600 mV (it drops the DCDC at 1420 mV and stops charging at 1500 mV).
//
// C47 expects the DM42's CR2032 scale instead: 3100 mV is a fresh cell, below 2500 mV
// raises FLAG_LOWBAT, and below BAT_MINIMUM (2100 mV) checkBattery() sets STAT_PGM_END
// and shuts the calculator down. Handing it the HP reading unscaled reads as a dead cell,
// so C47 powered itself off the first time it polled, a second or two after launch.
#define C47_HP_VBAT_FLAT   1000  // HP scale: discharged
#define C47_HP_VBAT_FRESH  1600  // HP scale: fresh cells
#define C47_DM_VBAT_FLOOR  2100  // == BAT_MINIMUM; see note below
#define C47_DM_VBAT_FRESH  3100

int get_vbat(void) {
    DMCP_TRACE(get_vbat);
    uint32_t mv = ll_get_bat_voltage();

    // Outside the plausible window the monitor is not reporting a cell at all (running
    // from USB, or no conversion yet - it reads ~176 mV on USB power). Report a healthy
    // cell rather than let C47 shut down on a meaningless number.
    if (mv < 700 || mv > 2000) {
        return 3000;
    }
    if (mv <= C47_HP_VBAT_FLAT) {
        // Clamped at BAT_MINIMUM, not below it: low-power policy belongs to OSLoader,
        // which monitors the pack itself, so C47 may warn but must not pull the plug.
        return C47_DM_VBAT_FLOOR;
    }
    if (mv >= C47_HP_VBAT_FRESH) {
        return C47_DM_VBAT_FRESH;
    }
    return C47_DM_VBAT_FLOOR +
           (int)(((mv - C47_HP_VBAT_FLAT) * (C47_DM_VBAT_FRESH - C47_DM_VBAT_FLOOR)) /
                 (C47_HP_VBAT_FRESH - C47_HP_VBAT_FLAT));
}

int usb_powered(void) {
    // VDD5V_GT_VDDIO, i.e. externally powered. ll_get_charge_status() is not a
    // substitute: it reports whether OSLoader has the charger enabled, which it
    // turns off once the pack is full.
    return ll_get_vdd5v_present() ? 1 : 0;
}

int power_check_screen(void) {
    return 0;
}

void start_buzzer_freq(uint32_t freq) {
    (void)freq;
}

void stop_buzzer(void) {
}

void beep_volume_up(void) {
    if (c47_beep_vol < 4) c47_beep_vol++;
}

void beep_volume_down(void) {
    if (c47_beep_vol > 0) c47_beep_vol--;
}

int get_beep_volume(void) {
    return c47_beep_vol;
}

void print_byte(uint8_t b) {
    (void)b;
}

void printer_advance_buf(int what) {
    (void)what;
}

int printer_busy_for(int what) {
    (void)what;
    return 0;
}

uint printer_get_delay(void) {
    return c47_printer_delay;
}

void printer_set_delay(uint val) {
    c47_printer_delay = val;
}

char *aux_buf_ptr(void) {
    DMCP_TRACE(aux_buf_ptr);
    return c47_aux_buf;
}

void *write_buf_ptr(void) {
    DMCP_TRACE(write_buf_ptr);
    return c47_write_buf;
}

int write_buf_size(void) {
    return C47_WRITE_BUF_BYTES;
}

// Convert backslashes in C47 paths (e.g. "\SAVFILES\c47auto.sav") to forward slashes for FatFs
static void normalize_path(const char *in, char *out, size_t out_sz) {
    size_t i = 0;
    if (!in || !out || out_sz == 0) return;
    for (; in[i] && i + 1 < out_sz; i++) {
        out[i] = (in[i] == '\\') ? '/' : in[i];
    }
    out[i] = '\0';
}

int check_create_dir(const char *dir) {
    DMCP_TRACE(check_create_dir);
    char path[64];
    normalize_path(dir, path, sizeof(path));
    FILINFO fno;
    if (f_stat(path, &fno) == FR_OK) {
        return 0;
    }
    return (f_mkdir(path) == FR_OK) ? 0 : -1;
}

int sys_disk_write_enable(int val) {
    c47_disk_write = val;
    return 0;
}

int sys_is_disk_write_enable(void) {
    DMCP_TRACE(sys_is_disk_write_enable);
    return c47_disk_write;
}

void make_date_filename(char *str, const char *dir, const char *ext) {
    tm_t tm;
    dt_t dt;
    rtc_read(&tm, &dt);
    char norm_dir[40];
    normalize_path(dir, norm_dir, sizeof(norm_dir));
    sprintf(str, "%s/%04u%02u%02u-%02u%02u%02u%s",
            norm_dir, dt.year, dt.month, dt.day, tm.hour, tm.min, tm.sec, ext ? ext : "");
}

void disp_disk_info(const char *hdr) {
    lcd_clear_buf();
    lcd_setLine(t24, 0);
    ds_t24.inv = 1;
    lcd_writeText(t24, hdr ? hdr : "Disk Info");
    ds_t24.inv = 0;

    FATFS *fs = NULL;
    DWORD fre_clust = 0;
    if (f_getfree("0:", &fre_clust, &fs) == FR_OK && fs) {
        uint32_t tot_kb = ((fs->n_fatent - 2) * fs->csize) * (fs->ssize / 1024);
        uint32_t fre_kb = (fre_clust * fs->csize) * (fs->ssize / 1024);
        char line[40];
        lcd_setLine(t24, 2);
        snprintf(line, sizeof(line), "Total Space: %lu KB", (unsigned long)tot_kb);
        lcd_writeText(t24, line);
        snprintf(line, sizeof(line), "Free Space:  %lu KB", (unsigned long)fre_kb);
        lcd_writeText(t24, line);
    }
    lcd_setLine(t24, 7);
    lcd_writeText(t24, "Press any key to continue.");
    lcd_refresh();
}

int file_selection_screen(const char *title, const char *base_dir, const char *ext,
                          file_sel_fn_t sel_fn, int disp_new, int overwrite_check, void *data) {
    (void)overwrite_check;
    char norm_dir[64];
    normalize_path(base_dir, norm_dir, sizeof(norm_dir));
    check_create_dir(norm_dir);

    // Collect up to 16 matching files in norm_dir
    char files[16][32];
    int count = 0;

    if (disp_new) {
        tm_t tm;
        dt_t dt;
        rtc_read(&tm, &dt);
        snprintf(files[count++], 32, "c47_%02u%02u_%02u%02u%s",
                 dt.month, dt.day, tm.hour, tm.min, ext ? ext : ".s47");
    }

    DIR dir;
    FILINFO fno;
    if (f_opendir(&dir, norm_dir) == FR_OK) {
        while (count < 16 && f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0') {
            if (fno.fattrib & AM_DIR) continue;
            if (ext && ext[0]) {
                size_t nl = strlen(fno.fname);
                size_t el = strlen(ext);
                if (nl < el || strcasecmp(&fno.fname[nl - el], ext) != 0) continue;
            }
            strncpy(files[count], fno.fname, 31);
            files[count][31] = '\0';
            count++;
        }
        f_closedir(&dir);
    }

    int sel = 0;
    wait_for_key_release(-1);
    while (!dmcp_exit_requested()) {
        lcd_clear_buf();
        lcd_setLine(t24, 0);
        ds_t24.inv = 1;
        lcd_writeText(t24, title ? title : "Select File");
        ds_t24.inv = 0;

        if (count == 0) {
            lcd_setLine(t24, 3);
            lcd_writeText(t24, "  (No matching files)");
        } else {
            int start_idx = (sel / 6) * 6;
            for (int i = 0; i < 6 && (start_idx + i) < count; i++) {
                int idx = start_idx + i;
                lcd_setLine(t24, i + 1);
                ds_t24.inv = (idx == sel) ? 1 : 0;
                lcd_writeText(t24, files[idx]);
                ds_t24.inv = 0;
            }
        }
        lcd_setLine(t24, 8);
        lcd_writeText(t24, "[UP/DN] Select [ENTER] OK [ON] Exit");
        lcd_refresh();

        int k = runner_get_key(NULL);
        if (IS_EXIT_KEY(k)) {
            return MRET_EXIT;
        }
        if (k == KEY_UP && sel > 0) {
            sel--;
        } else if (k == KEY_DOWN && sel + 1 < count) {
            sel++;
        } else if (k == KEY_ENTER && count > 0) {
            char fullpath[96];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", norm_dir, files[sel]);
            if (sel_fn) {
                int r = sel_fn(fullpath, files[sel], data);
                if (r != 0) return r;
            } else {
                if (data) strcpy((char *)data, fullpath);
                return 0;
            }
        }
    }
    return MRET_EXIT;
}

void set_reset_state_file(const char *str) {
    if (str) {
        normalize_path(str, c47_reset_state_file, sizeof(c47_reset_state_file));
    }
}

char *get_reset_state_file(void) {
    DMCP_TRACE(get_reset_state_file);
    return c47_reset_state_file;
}

int is_reset_state_file(void) {
    DMCP_TRACE(is_reset_state_file);
    return (c47_reset_state_file[0] != '\0') ? 1 : 0;
}

// ============================================================================
// ExistOS Task & Entry Point for C47
// ============================================================================

// ---------------------------------------------------------------------------
// DMCP system flags.
//
// On a DM42 these live in the firmware and `sdb` arrives with the firmware's
// accessors already installed. Here nothing fills them in, so they have to be
// provided by the port: C47 calls set_flag_clk24() from timeSetSystemTimeFormat()
// while restoring flags during doFnReset(), and set_flag_dmy() from
// timeSetSystemDateFormat(), both through sdb function pointers.
// ---------------------------------------------------------------------------
static int c47_flag_dmy   = 1;       // C47 SYS_DMY (0=MDY, 1=DMY, 2=YMD), see c47/defines.h
static int c47_flag_clk24 = 1;       // 24-hour clock
static int c47_flag_mute  = 0;       // buzzer muted

static int c47_get_flag_dmy(void) {
    return c47_flag_dmy;
}

static void c47_set_flag_dmy(int val) {
    c47_flag_dmy = val;
}

static int c47_is_flag_clk24(void) {
    return c47_flag_clk24;
}

static void c47_set_flag_clk24(int val) {
    c47_flag_clk24 = val ? 1 : 0;
    if (c47_flag_clk24) {
        calc_state |= STAT_CLK24;
    } else {
        calc_state &= ~(uint32_t)STAT_CLK24;
    }
}

static int c47_is_beep_mute(void) {
    return c47_flag_mute;
}

static void c47_set_beep_mute(int val) {
    c47_flag_mute = val ? 1 : 0;
}

static int c47_run_menu_item_app(uint8_t line_id) {
    (void)line_id;
    return 0;
}

static const char *c47_menu_line_str_app(uint8_t line_id, char *s, const int slen) {
    (void)line_id;
    if (s && slen > 0) {
        s[0] = '\0';
    }
    return s;
}

static void c47_noop_cb(void) {
}

// ============================================================================
// DMCP API that C47 did not use but DB48X does
// ============================================================================

int get_lowbat_state(void) {
    // get_vbat() already maps the HP reading onto the DM42's scale and guards
    // the out-of-window values this hardware produces; 2500 mV is DMCP's warning
    // threshold on that scale.
    return get_vbat() < 2500 ? 1 : 0;
}

uint32_t read_power_voltage(void) {
    return get_vbat();
}

int sys_disk_ok(void) {
    return 1;   // FatFs on the internal NAND is mounted for the whole session
}

// Days since 1 Jan 4713 BC. Fliegel & Van Flandern, as DMCP uses it.
int julian_day(dt_t *dt) {
    if (!dt) return 0;
    int y = dt->year, m = dt->month, d = dt->day;
    int a = (14 - m) / 12;
    int yy = y + 4800 - a;
    int mm = m + 12 * a - 3;
    return (int)(d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045);
}

const char *get_wday_shortcut(int day) {
    // dmcp.h: 0 = Monday
    static const char *const wd[7] = {"Mon","Tue","Wed","Thu","Fri","Sat","Sun"};
    return wd[(unsigned)day % 7];
}

const char *get_month_shortcut(int month) {
    static const char *const mo[13] = {"???","Jan","Feb","Mar","Apr","May","Jun",
                                       "Jul","Aug","Sep","Oct","Nov","Dec"};
    return mo[(month >= 1 && month <= 12) ? month : 0];
}

// DMCP's firmware menus and message boxes. ExistOS has its own UI and the
// application reaches its own menus through softkeys, so these decline rather
// than pretending: returning 0 sends the application back to its own screen.
int handle_menu(const smenu_t *menu, int action, int cur_line) {
    (void)menu; (void)action; (void)cur_line;
    return 0;
}

void msg_box(disp_stat_t *ds, const char *txt, int inv) {
    (void)ds; (void)inv;
    if (txt) {
        const char *lines[2] = { txt, "Press any key" };
        dmcp_show_message(lines, 2);
    }
}

int lcd_for_calc(int what) {
    (void)what;     // DM42 screen-layout presets; this panel has its own adapter
    return 0;
}

void lcd_switchFont(disp_stat_t *ds, int nr) {
    if (ds) ds->f = (void *)(uintptr_t)nr;
}

void lcd_writeClr(disp_stat_t *ds) {
    if (!ds) return;
    ds->x = ds->y = 0;
    ds->inv = 0;
}

void set_reset_magic(uint32_t value) {
    (void)value;    // DMCP reboots into a chosen mode; OSLoader owns reset here
}

void sys_critical_start(void) { vTaskSuspendAll(); }
void sys_critical_end(void)   { (void)xTaskResumeAll(); }

// How much the application may take for its heap. Deliberately a budget, not
// the real free heap: the VM keeps only ~40 KB of RAM resident and spills the
// rest to NAND, so an application that takes everything thrashes. DB48X sizes
// its RPL heap from this. Tune against OSLoader's VMRATE output - if nandrd/s
// climbs into the hundreds the budget is too large, not too small.
#ifndef DMCP_APP_HEAP_BUDGET
#define DMCP_APP_HEAP_BUDGET (64 * 1024)
#endif

int sys_free_mem(void) {
    return DMCP_APP_HEAP_BUDGET;
}

int __sysfn_read_key(int *k1, int *k2) {
    int n = 0;
    uint32_t keys = ll_vm_check_key();
    if (k1) *k1 = 0;
    if (k2) *k2 = 0;
    if ((keys >> 16) & 0xFFFFu) {
        int mapped = c47_map_39gii_key(keys & 0xFFFFu);
        if (mapped > 0) {
            if (k1) *k1 = mapped;
            n = 1;
        }
    }
    return n;
}

// ============================================================================
// Shared application lifecycle
// ============================================================================

void dmcp_show_message(const char *const *lines, int count) {
    if (!c47_strip_buf) return;
    for (int sy0 = 0; sy0 < LCD_PIX_H; sy0 += C47_STRIP_ROWS) {
        int sh = C47_STRIP_ROWS;
        if (sy0 + sh > LCD_PIX_H) sh = LCD_PIX_H - sy0;
        strip_fill_rect(c47_strip_buf, sy0, sh, 0, sy0, LCD_PIX_W, sh, C47_PAPER);
        for (int i = 0; i < count; i++) {
            strip_draw_str5x8(c47_strip_buf, sy0, sh, 4, 12 + i * 10,
                              lines[i], C47_INK, C47_PAPER);
        }
        ll_disp_put_area(c47_strip_buf, 0, (uint32_t)sy0,
                         LCD_PIX_W - 1, (uint32_t)(sy0 + sh - 1));
    }
    uint32_t last = ll_vm_check_key();
    for (int i = 0; i < 400; i++) {
        vTaskDelay(pdMS_TO_TICKS(50));
        uint32_t k = ll_vm_check_key();
        if (k != last) {
            last = k;
            if (k >> 16) break;
        }
    }
}

static void dmcp_init_sdb(void) {
    memset(&sdb, 0, sizeof(sdb));
    memset(&ds_t20, 0, sizeof(ds_t20));
    memset(&ds_t24, 0, sizeof(ds_t24));
    memset(&ds_fReg, 0, sizeof(ds_fReg));

    calc_state     = STAT_RUNNING | STAT_CLK24;
    ppgm_fp        = &c47_pgm_fp;
    sdb.pds_t20    = &ds_t20;
    sdb.pds_t24    = &ds_t24;
    sdb.pds_fReg   = &ds_fReg;
    timer2_counter = &c47_tim2_cnt;
    timer3_counter = &c47_tim3_cnt;

    // Every callback an application may dispatch through sdb must be non-NULL:
    // an unset one is an indirect branch to a wild address, not a no-op. Assign
    // through the macro names - dmcp.h #defines the bare field names to
    // (sdb.<field>), so "sdb.set_flag_clk24 = x" preprocesses into nonsense.
    get_flag_dmy      = c47_get_flag_dmy;
    set_flag_dmy      = c47_set_flag_dmy;
    is_flag_clk24     = c47_is_flag_clk24;
    set_flag_clk24    = c47_set_flag_clk24;
    is_beep_mute      = c47_is_beep_mute;
    set_beep_mute     = c47_set_beep_mute;
    run_menu_item_app = c47_run_menu_item_app;
    menu_line_str_app = c47_menu_line_str_app;
    after_fat_format  = c47_noop_cb;
    msc_end_cb        = c47_noop_cb;
}

bool dmcp_run(const dmcp_app_t *app) {
    bool ok = false;

    g_dmcp_app = app;
    g_dmcp_ink_set = app->ink_is_set_bit;
    SystemUISuspend();

    // These applications want far more RAM than the on-chip heap holds, so the
    // NAND-backed swap heap has to be live before anything large is allocated.
    if (!MemorySwapEnable) {
        enableMemSwap(true);
        printf("%s: memory swap enabled (%lu KB)\n",
               app->name, (unsigned long)(SwapMemorySize / 1024));
    }

    f_mkdir("/SAVFILES");
    f_mkdir("/PROGRAMS");

    c47_aux_buf     = (char *)pvPortMalloc(C47_AUX_BUF_BYTES);
    c47_write_buf   = (uint8_t *)pvPortMalloc(C47_WRITE_BUF_BYTES);
    c47_strip_buf   = (uint8_t *)pvPortMalloc(LCD_PIX_W * C47_STRIP_ROWS);

    if (c47_aux_buf && c47_write_buf && c47_strip_buf) {
        lcd_clear_buf();
        memset(c47_aux_buf, 0, C47_AUX_BUF_BYTES);
        memset(c47_write_buf, 0, C47_WRITE_BUF_BYTES);
        dmcp_init_sdb();

        key_pop_all();
        c47_last_raw_keys  = ll_vm_check_key();
        c47_on_held        = false;
        c47_active_c47_key = 0;
        c47_help_page      = 0;
        memset(c47_systim_enabled, 0, sizeof(c47_systim_enabled));

        c47_banner_until_ms = ll_get_time_ms() + 1800;
        snprintf(c47_banner_text, sizeof(c47_banner_text),
                 "%s: [APPS]=Keys [VIEWS]=Zoom", app->name);

        printf("%s: entering program_main()\n", app->name);
        if (setjmp(g_dmcp_exit_jmp) == 0) {
            app->run();
            printf("%s: program_main() returned\n", app->name);
        } else {
            printf("%s: left via sys_sleep\n", app->name);
        }
        ok = true;
    } else {
        printf("%s: out of memory, aborting launch\n", app->name);
    }

    if (c47_strip_buf)   { vPortFree(c47_strip_buf);   c47_strip_buf   = NULL; }
    if (c47_write_buf)   { vPortFree(c47_write_buf);   c47_write_buf   = NULL; }
    if (c47_aux_buf)     { vPortFree(c47_aux_buf);     c47_aux_buf     = NULL; }

    ll_disp_set_indicator(0, -1);
    SystemUIResume();
    g_dmcp_app = NULL;
    return ok;
}
