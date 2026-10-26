// DB48X application glue. The DMCP platform layer lives in System/dmcp and is
// shared with C47; this file holds only what is specific to DB48X.

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"

#include "SystemUI.h"
#include "keyboard_gii39.h"
#include "dmcp_app.h"

// libdb48x_core.libc. Its program_main() is renamed by
// Libs/src/build_db48x.sh so it cannot collide with C47's.
void db48x_program_main(void);

// DB48X has no exit flag of its own: its main loop is while(true) and on a DM42
// the firmware ends the program from inside sys_sleep(). The platform layer
// raises this instead and jumps out.
static volatile uint8_t db48x_exit = 0;

// Added to DB48X by Libs/src/build_db48x.sh. Returns shift plane in bits 0-1
// (0 none, 1 shift, 2 xshift) and alpha plane in bits 2-3 (0 none, 1 upper,
// 2 lower), and reports where the stack area begins.
int db48x_ui_state(int *stack_top);
void db48x_ui_geometry(int *top, int *bottom, int *menu, int *depth);

static int db48x_indicators(void) {
    int st = db48x_ui_state(NULL);
    int ind = 0;

    switch (st & 3) {
        case 1:  ind |= INDICATE_LEFT;  break;   // shift
        case 2:  ind |= INDICATE_RIGHT; break;   // extended shift
        default: break;
    }
    switch ((st >> 2) & 3) {
        case 1:  ind |= INDICATE_A__Z; break;
        case 2:  ind |= INDICATE_a__z; break;
        default: break;
    }
    return ind;
}

// Where DB48X's header ends. The 39gII shows shift, alpha and battery on its
// own annunciator row, which costs no pixels, so the header is redundant here
// and the display layer can drop it on request.
static int db48x_content_top(void) {
    int top = 0, bottom = 0, menu = 0, depth = 0;
    db48x_ui_geometry(&top, &bottom, &menu, &depth);
    {
        static int last_depth = -1;
        if (depth != last_depth) {
            last_depth = depth;
            printf("DB48X geom: stack %d..%d menu_bottom=%d depth=%d\n",
                   top, bottom, menu, depth);
        }
    }
    return top;
}

// DB48X links objects that live on its task stack into these statics
// (runtime::GCSafe for GC-safe pointers, file::current for an open file). The
// platform layer leaves DB48X by longjmp, so the destructors that would unlink
// them never run, and on relaunch the new task stack, at the same address,
// holds whatever they pointed to. Walking that list panicked in runtime::move.
extern void *db48x_gc_safe   __asm__("_ZN7runtime6GCSafeE");
extern void *db48x_open_file __asm__("_ZN4file7currentE");

static void db48x_run(void) {
    db48x_gc_safe = NULL;
    db48x_open_file = NULL;
    db48x_program_main();
}

// The HP 39gII legends (plain / SHIFT), mapped onto DB48X's DM42 layout
// (src/user_interface.cc, defaultUnshifted/Shifted/SecondShiftedCommand).
// DMCP_F is DB48X's left shift, DMCP_G its right shift (shift pressed twice).
static const dmcp_key_t db48x_keymap[] = {
    { KEY_F1, DM_F1, 0 }, { KEY_F2, DM_F2, 0 }, { KEY_F3, DM_F3, 0 },
    { KEY_F4, DM_F4, 0 }, { KEY_F5, DM_F5, 0 }, { KEY_F6, DM_F6, 0 },

    // DB48X's two arrows move the editor cursor left (up) and right (down),
    // and scroll the stack and help; the 39gII's four reuse them.
    { KEY_UP,    DM_UP,   0 },
    { KEY_LEFT,  DM_UP,   0 },
    { KEY_DOWN,  DM_DOWN, 0 },
    { KEY_RIGHT, DM_DOWN, 0 },

    { KEY_SYMB,  DMCP_G(DM_7),     DMCP_F(DM_0) },     // Symbolic  / Setup
    { KEY_PLOT,  DMCP_G(DM_E),     DMCP_F(DM_0) },     // Plot      / Setup
    { KEY_NUM,   DMCP_F(DM_1),     DMCP_F(DM_0) },     // ->Num     / Setup
    { KEY_HOME,  DMCP_G(DM_SIGMA), DMCP_F(DM_CHS) },   // Main menu / Modes
    { KEY_APPS,  DMCP_F(DM_EXIT),  DMCP_ACT_KEYMAP },  // Off = quit to launcher / Info
    { KEY_VIEWS, DMCP_ACT_VIEWS,   DMCP_G(DM_ADD) },   // display mode / Help

    { KEY_VARS,      DMCP_F(DM_RCL),  DMCP_G(DM_XEQ) },  // Variables / Chars
    { KEY_MATH,      DM_LN,           DMCP_F(DM_ADD) },  // Math menu / Cmds: Catalog
    { KEY_ABC,       DMCP_G(DM_RDN),  DMCP_F(DM_STO) },  // Fractions / Complex
    { KEY_XTPHIN,    DM_STO,          DM_E },            // STO       / EEX
    { KEY_BACKSPACE, DM_BSP,          DMCP_F(DM_BSP) },  // <- / Drop / Clear menu

    { KEY_SIN, DM_SIN, DMCP_F(DM_SIN) },
    { KEY_COS, DM_COS, DMCP_F(DM_COS) },
    { KEY_TAN, DM_TAN, DMCP_F(DM_TAN) },
    { KEY_LN,  DMCP_G(DM_INV), DMCP_F(DM_INV) },       // ln / e^x
    { KEY_LOG, DMCP_F(DM_LN),  DMCP_F(DM_LN) },        // Powers menu (log10, 10^x)

    { KEY_X2,           DMCP_F(DM_SQRT), DM_SQRT },         // x^2 / sqrt
    { KEY_XY,           DM_LOG,          DMCP_G(DM_SQRT) }, // y^x / x-th root
    { KEY_LEFTBRACKET,  DM_XEQ,          DMCP_G(DM_DOWN) }, // '()' / Copy: Edit menu
    { KEY_RIGHTBRACKET, DM_DOWN,         DMCP_G(DM_DOWN) }, // cursor right / Paste: Edit
    { KEY_DIVISION,     DM_DIV,          DM_INV },          // /   / 1/x

    { KEY_COMMA,          DM_SWAP, DMCP_F(DM_RCL) },     // Swap / Mem: Variables
    { KEY_7,              DM_7,    DMCP_F(DM_SUB) },     // 7    / List
    { KEY_8,              DM_8,    DMCP_F(DM_SUB) },     // 8    / { : List
    { KEY_9,              DM_9,    DMCP_F(DM_SUB) },     // 9    / } : List
    { KEY_MULTIPLICATION, DM_MUL,  DMCP_F(DM_MUL) },     // x    / !: Probabilities

    { KEY_ALPHA,       DMCP_F(DM_ENTER), DMCP_F(DM_ENTER) },  // Alpha
    { KEY_4,           DM_4,   DMCP_G(DM_9) },           // 4 / Matrix
    { KEY_5,           DM_5,   DMCP_F(DM_9) },           // 5 / [ ]
    { KEY_6,           DM_6,   DMCP_F(DM_9) },           // 6 / [ ]
    { KEY_SUBTRACTION, DM_SUB, DMCP_G(DM_TAN) },         // - / angle: Angles

    { KEY_1,    DM_1,   DMCP_F(DM_3) },                  // 1 / Prgm: Program
    { KEY_2,    DM_2,   DMCP_F(DM_STO) },                // 2 / i: Complex
    { KEY_3,    DM_3,   DMCP_F(DM_RDN) },                // 3 / pi: Constants
    { KEY_PLUS, DM_ADD, DMCP_F(DM_DIV) },                // + / Sigma: Statistics

    { KEY_0,        DM_0,     DMCP_G(DM_0) },            // 0 / Notes: Files
    { KEY_DOT,      DM_DOT,   0 },
    { KEY_NEGATIVE, DM_CHS,   DMCP_F(DM_LOG) },          // +/- / ABS
    { KEY_ENTER,    DM_ENTER, DMCP_F(DM_SWAP) },         // ENTER / ANS: LastArg
};

static const char *const db48x_keymap_help[] = {
    "Plain / SHIFT. SHIFT twice = right shift",
    "SYMB  Symbolic/Setup PLOT Plot/Setup",
    "NUM   ->Num/Setup   HOME  Main/Modes",
    "APPS  quit          VIEWS zoom/Help",
    "VARS  Vars/Chars    MATH  Math/Catalog",
    "ab/c  Fracts/Cplx   XTqN  STO/EEX",
    "BKSP  Drop/Clear    ALPHA alpha",
    "SIN COS TAN  as labelled",
    "LN    ln/e^x        LOG   Powers menu",
    "x^2   x^2/sqrt      x^y   y^x/xroot",
    "(     '()'/Edit     )     right/Edit",
    ",     Swap/Vars     /     div/1/x",
    "x     mul/Prob      -     sub/Angles",
    "+     add/Stats     (-)   neg/abs",
    "7 8 9 Lists 4 Matrix 5 6 []  1 Prog",
    "2 Cplx 3 Const 0 Files ENTER LastArg",
    "Arrows: left=up, right=down (cursor)",
    "ON    EXIT          SHIFT+ON standby",
    "F1-F6 menu: bottom row; SHIFT+F middle",
    "      row; SHIFT twice+F top row.",
    "      Hold an F key: help on its command",
};

static const dmcp_app_t db48x_app = {
    .name       = "DB48X",
    .run        = db48x_run,
    .indicators  = db48x_indicators,
    .content_top = db48x_content_top,
    .exit_flag  = &db48x_exit,
    .ink_is_set_bit = false,   // ink is a *clear* bit, as for C47. blitter.h says
                               // "white is 0 and black is 1", but that describes
                               // the colour value: CONVERT(MONOCHROME,
                               // MONOCHROME_REVERSE) returns ~data, so the
                               // surface inverts on the way to the framebuffer.
    .never_returns = true,
    .keymap          = db48x_keymap,
    .keymap_len      = sizeof(db48x_keymap) / sizeof(db48x_keymap[0]),
    .keymap_help     = db48x_keymap_help,
    .keymap_help_len = sizeof(db48x_keymap_help) / sizeof(db48x_keymap_help[0]),
};

static void __attribute__((target("arm"))) db48x_task(void *arg) {
    (void)arg;
    dmcp_run(&db48x_app);
    vTaskDelete(NULL);
}

void StartDB48X(void) {
    xTaskCreate(db48x_task, "DB48X", 12288, NULL, configMAX_PRIORITIES - 3, NULL);
}
