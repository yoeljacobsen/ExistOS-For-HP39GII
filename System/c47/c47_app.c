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

static const dmcp_app_t c47_app = {
    .name       = "C47",
    .run        = c47_run,
    .indicators = c47_indicators,
    .exit_flag  = &backToDMCP,
    .ink_is_set_bit = false,   // C47 blanks to 0xFF; a clear bit is ink
    .never_returns = false,   // C47 returns from program_main() after saving
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
