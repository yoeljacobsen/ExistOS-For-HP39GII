#pragma once

#include <stdbool.h>
#include <stdint.h>

// DM42 key codes as the DMCP applications receive them (dmcp.h's KEY_*; that
// header cannot be included next to the HP 39gII's own KEY_* names).
enum {
    DM_SIGMA = 1, DM_INV, DM_SQRT, DM_LOG, DM_LN, DM_XEQ,
    DM_STO, DM_RCL, DM_RDN, DM_SIN, DM_COS, DM_TAN,
    DM_ENTER, DM_SWAP, DM_CHS, DM_E, DM_BSP,
    DM_UP, DM_7, DM_8, DM_9, DM_DIV,
    DM_DOWN, DM_4, DM_5, DM_6, DM_MUL,
    DM_SHIFT, DM_1, DM_2, DM_3, DM_SUB,
    DM_EXIT, DM_0, DM_DOT, DM_RUN, DM_ADD,
    DM_F1, DM_F2, DM_F3, DM_F4, DM_F5, DM_F6,
};

// What one HP 39gII key does in an application. The platform layer owns the
// 39gII SHIFT key: it remembers the shift itself and sends each key's "plain"
// or "shifted" meaning, so the legends printed on the case are what happens.
// A meaning is a DM42 key preceded by 0, 1 or 2 presses of the application's
// own shift (DMCP_F / DMCP_G). A shifted meaning of 0, or SHIFT pressed twice,
// falls through to the application's shift plane for that key, so nothing the
// application offers becomes unreachable.
#define DMCP_KEY(k, shifts) ((uint16_t)((k) | ((shifts) << 8)))
#define DMCP_F(k)           DMCP_KEY(k, 1)
#define DMCP_G(k)           DMCP_KEY(k, 2)
#define DMCP_ACT_VIEWS      0x8001  // cycle the display scaling mode
#define DMCP_ACT_KEYMAP     0x8002  // show this application's key overlay

// In the application's alpha mode, "alpha" is the DM42 key that carries the
// letter printed on the 39gII key (VARS A, MATH B, ... 3 Z), and SHIFT applies
// the application's shift planes to it. It may instead be one character with
// its shifts included (DMCP_F/DMCP_G), for legends the application keeps on a
// shifted plane. Zero: the key keeps its DM42 key in alpha mode.
typedef struct {
    uint16_t k39;       // HP 39gII KEY_* code
    uint16_t plain;
    uint16_t shifted;
    uint16_t alpha;
} dmcp_key_t;

// One DMCP platform layer (dmcp_existos.c) serves whichever application is
// running: C47, DB48X, and anything else built on the DMCP API. Only one runs
// at a time. Each supplies the few things the layer cannot know by itself.
typedef struct {
    const char *name;

    // Runs the application's DMCP entry point. The core libraries each define
    // program_main(), so the build scripts rename them apart; this hook calls
    // whichever belongs to this application.
    void (*run)(void);

    // INDICATE_* bits for the annunciator row, read from the application's own
    // shift/alpha/busy state.
    int (*indicators)(void);

    // The application's "leave now" flag, raised by the platform layer when the
    // user quits or the application powers itself off.
    volatile uint8_t *exit_flag;

    // First row of the 400x240 image worth showing, or NULL for "all of it".
    // DB48X draws a header the HP 39gII does not need: the hardware annunciator
    // row sits outside the 256x127 bitmap, so shift and alpha cost no pixels
    // there, and the rows the header occupies are better given to the stack.
    int (*content_top)(void);

    // Pixel polarity of the 1bpp framebuffer. C47 blanks the screen to 0xFF and
    // treats a clear bit as ink; DB48X follows the DM42, where "white is 0 and
    // black is 1" (blitter.h). Getting this wrong inverts the whole display.
    bool ink_is_set_bit;

    // True if the application's main loop never returns. DB48X loops forever
    // and relies on DMCP's sys_sleep() not coming back; C47 instead returns
    // from program_main() after saving its state, and must be allowed to.
    bool never_returns;

    // The 39gII keyboard, and the overlay text that explains it (SHIFT+APPS).
    const dmcp_key_t *keymap;
    int keymap_len;
    const char *const *keymap_help;
    int keymap_help_len;
} dmcp_app_t;

extern const dmcp_app_t *g_dmcp_app;

static inline bool dmcp_exit_requested(void) {
    return g_dmcp_app && g_dmcp_app->exit_flag && *g_dmcp_app->exit_flag;
}

static inline void dmcp_request_exit(void) {
    if (g_dmcp_app && g_dmcp_app->exit_flag) {
        *g_dmcp_app->exit_flag = 1;
    }
}

// Set up the platform layer, run the application, tear down. Returns false if
// the buffers could not be allocated.
bool dmcp_run(const dmcp_app_t *app);

// Draw a few lines of 5x8 text and wait for a key. Used to report a refusal to
// start without leaving the user at a blank screen.
void dmcp_show_message(const char *const *lines, int count);

// Reusable arena for an application's own heap, in place of a malloc() it never
// frees. See the note in dmcp_existos.c.
void *dmcp_rpl_arena(size_t size);
