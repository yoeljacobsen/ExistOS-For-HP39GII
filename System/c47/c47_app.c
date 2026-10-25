// C47 application glue. The DMCP platform layer lives in System/dmcp and is
// shared with DB48X; this file holds only what is specific to C47.

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "SystemUI.h"
#include "keyboard_gii39.h"
#include "dmcp_app.h"

// libc47_core.libc. Its program_main() is renamed by Libs/src/build_c47.sh so
// it cannot collide with DB48X's.
void c47_program_main(void);
extern uint8_t  backToDMCP;
extern uint8_t  shiftF;
extern uint8_t  shiftG;
extern uint8_t  calcMode;
extern uint8_t  alphaCase;
extern uint8_t  programRunStop;
extern uint32_t *ram;
extern void    *globalRegister;
extern void    *freeMemoryRegions;
extern uint8_t  inKeyBuffer(uint8_t byte);
extern void     clearKeyBuffer(void);

typedef struct {
    uint16_t charCode;
    uint8_t  colsBeforeGlyph;
    uint8_t  colsGlyph;
    uint8_t  colsAfterGlyph;
    uint8_t  rowsGlyph;
    char    *data;
} c47_glyph_t;
extern c47_glyph_t glyphNotFound;

// RAM_SIZE_IN_BLOCKS (65534) blocks of 2^BPB (4) bytes.
#define C47_RAM_POOL_BYTES (65534u * 4u)

static int c47_indicators(void) {
    int ind = 0;
    if (shiftF) ind |= INDICATE_LEFT;
    if (shiftG) ind |= INDICATE_RIGHT;
    if (calcMode == 3 /* CM_AIM */ || calcMode == 11 /* CM_EIM */) {
        ind |= (alphaCase == 0) ? INDICATE_A__Z : INDICATE_a__z;
    }
    if (programRunStop == 1 /* PGM_RUNNING */) {
        ind |= INDICATE_BUSY;
    }
    return ind;
}

static void c47_run(void) {
    // doFnReset() allocates this pool itself and does not check the result, so
    // a failure there becomes a NULL memset inside the library. Do it here.
    if (ram == NULL) {
        ram = (uint32_t *)malloc(C47_RAM_POOL_BYTES);
        printf("C47: ram pool (%lu bytes) = %p\n",
               (unsigned long)C47_RAM_POOL_BYTES, (void *)ram);
    }
    if (!ram) {
        static const char *const msg[] = {
            "C47 cannot start", "", "Not enough memory for the",
            "register pool.", "", "Press any key",
        };
        dmcp_show_message(msg, sizeof(msg) / sizeof(msg[0]));
        return;
    }

    inKeyBuffer(0);
    clearKeyBuffer();

    c47_program_main();

    // Release the library's own allocations so relaunching, or another app,
    // starts from a clean heap.
    if (ram)               { free(ram);               ram = NULL; }
    if (globalRegister)    { free(globalRegister);    globalRegister = NULL; }
    if (freeMemoryRegions) { free(freeMemoryRegions); freeMemoryRegions = NULL; }
    if (glyphNotFound.data) { free(glyphNotFound.data); glyphNotFound.data = NULL; }
}

// The HP 39gII legends (plain / SHIFT), mapped onto C47's DM42 layout, where
// f and g are C47's two shifts. Keys whose 39gII legend C47 has no use for
// fall through to f (and SHIFT twice to g) on the same DM42 key.
static const dmcp_key_t c47_keymap[] = {
    { KEY_F1, DM_F1, 0 }, { KEY_F2, DM_F2, 0 }, { KEY_F3, DM_F3, 0 },
    { KEY_F4, DM_F4, 0 }, { KEY_F5, DM_F5, 0 }, { KEY_F6, DM_F6, 0 },

    // The DM42 has no horizontal arrows; left/right take the stack moves that
    // read naturally in those directions.
    { KEY_UP,    DM_UP,   0 },
    { KEY_DOWN,  DM_DOWN, 0 },
    { KEY_LEFT,  DM_SWAP, 0 },
    { KEY_RIGHT, DM_RDN,  0 },

    { KEY_SYMB,  DMCP_F(DM_7),   DMCP_F(DM_CHS) },    // EQN       / Setup: MODE
    { KEY_PLOT,  DMCP_G(DM_DIV), DMCP_F(DM_CHS) },    // PLOT      / Setup: MODE
    { KEY_NUM,   DM_XEQ,         DMCP_F(DM_CHS) },    // XEQ       / Setup: MODE
    { KEY_HOME,  DMCP_G(DM_7),   DMCP_F(DM_CHS) },    // HOME      / Modes: MODE
    { KEY_APPS,  DMCP_F(DM_EXIT), DMCP_ACT_KEYMAP },  // OFF = quit to launcher / Info
    { KEY_VIEWS, DMCP_ACT_VIEWS, DMCP_ACT_KEYMAP },   // display mode / Help

    { KEY_VARS,      DM_RCL,          DMCP_F(DM_XEQ) },  // RCL       / Chars: AIM
    { KEY_MATH,      DMCP_F(DM_ADD),  DMCP_F(DM_ADD) },  // CATALOG   / Cmds: CATALOG
    { KEY_ABC,       DMCP_G(DM_SIGMA), DMCP_G(DM_TAN) }, // a b/c FRC / ->POL
    { KEY_XTPHIN,    DM_STO,          DM_E },            // STO       / EEX
    { KEY_BACKSPACE, DM_BSP,          DMCP_G(DM_BSP) },  // <-        / Clear: CLR menu

    { KEY_SIN, DM_SIN, DMCP_F(DM_SIN) },
    { KEY_COS, DM_COS, DMCP_F(DM_COS) },
    { KEY_TAN, DM_TAN, DMCP_F(DM_TAN) },
    { KEY_LN,  DM_LN,  DMCP_F(DM_LN) },              // ln  / e^x
    { KEY_LOG, DM_LOG, DMCP_F(DM_LOG) },             // log / 10^x

    { KEY_X2,           DMCP_F(DM_SQRT), DM_SQRT },        // x^2 / sqrt
    { KEY_XY,           DMCP_F(DM_INV),  DMCP_G(DM_RDN) }, // y^x / x-th root
    { KEY_LEFTBRACKET,  DM_RDN,  0 },
    { KEY_RIGHTBRACKET, DM_SWAP, 0 },
    { KEY_DIVISION,     DM_DIV,  DM_INV },                 // /   / 1/x

    { KEY_COMMA,          DM_RUN, DMCP_G(DM_DOT) },        // R/S / Mem: INFO
    { KEY_7,              DM_7,   DMCP_F(DM_DIV) },        // 7   / List: STAT
    { KEY_8,              DM_8,   0 },
    { KEY_9,              DM_9,   0 },
    { KEY_MULTIPLICATION, DM_MUL, DMCP_F(DM_MUL) },        // x   / !: PROB

    { KEY_ALPHA,       DMCP_F(DM_XEQ), DMCP_F(DM_XEQ) },   // AIM
    { KEY_4,           DM_4,   DMCP_F(DM_9) },             // 4   / Matrix: MATX
    { KEY_5,           DM_5,   0 },
    { KEY_6,           DM_6,   0 },
    { KEY_SUBTRACTION, DM_SUB, 0 },

    { KEY_1,    DM_1,   DMCP_F(DM_RUN) },                  // 1   / Prgm: P/R
    { KEY_2,    DM_2,   DMCP_G(DM_SIN) },                  // 2   / i: j
    { KEY_3,    DM_3,   DMCP_F(DM_RDN) },                  // 3   / pi
    { KEY_PLUS, DM_ADD, DM_SIGMA },                        // +   / Sigma+

    { KEY_0,        DM_0,     0 },
    { KEY_DOT,      DM_DOT,   0 },
    { KEY_NEGATIVE, DM_CHS,   DMCP_F(DM_STO) },            // +/- / ABS: |x|
    { KEY_ENTER,    DM_ENTER, DMCP_F(DM_SWAP) },           // ENTER / ANS: LASTx
};

static const char *const c47_keymap_help[] = {
    "Plain / SHIFT.  SHIFT twice = C47 g",
    "SYMB  EQN/MODE      PLOT  PLOT/MODE",
    "NUM   XEQ/MODE      HOME  HOME/MODE",
    "APPS  quit          VIEWS zoom mode",
    "VARS  RCL/AIM       MATH  CATALOG",
    "ab/c  FRAC/>POL     XTqN  STO/EEX",
    "BKSP  <-/CLR menu   ALPHA AIM",
    "SIN COS TAN LN LOG  as labelled",
    "x^2   x^2/sqrt      x^y   y^x/xroot",
    "(     Rv            )     x<>y",
    ",     R/S/INFO      /     div/1/x",
    "x     mul/PROB      +     add/Sigma+",
    "7 STAT  4 MATX  1 P/R  2 j  3 pi",
    "(-)   CHS/|x|       ENTER ENTER/LASTx",
    "LEFT  x<>y          RIGHT Rv",
    "ON    EXIT          SHIFT+ON standby",
    "Others: SHIFT = C47 f on that key",
};

static const dmcp_app_t c47_app = {
    .name       = "C47",
    .run        = c47_run,
    .indicators = c47_indicators,
    .exit_flag  = &backToDMCP,
    .ink_is_set_bit = false,   // C47 blanks to 0xFF; a clear bit is ink
    .never_returns = false,   // C47 returns from program_main() after saving
    .keymap          = c47_keymap,
    .keymap_len      = sizeof(c47_keymap) / sizeof(c47_keymap[0]),
    .keymap_help     = c47_keymap_help,
    .keymap_help_len = sizeof(c47_keymap_help) / sizeof(c47_keymap_help[0]),
};

static void __attribute__((target("arm"))) c47_task(void *arg) {
    (void)arg;
    dmcp_run(&c47_app);
    vTaskDelete(NULL);
}

void StartC47(void) {
    // 48 KB: C47 records a 14,244-byte deepest case on DM42n hardware.
    xTaskCreate(c47_task, "C47", 12288, NULL, configMAX_PRIORITIES - 3, NULL);
}
