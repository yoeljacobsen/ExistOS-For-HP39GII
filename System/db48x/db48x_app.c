// DB48X application glue. The DMCP platform layer lives in System/dmcp and is
// shared with C47; this file holds only what is specific to DB48X.

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "SystemUI.h"
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

static void db48x_run(void) {
    db48x_program_main();
}

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
};

static void __attribute__((target("arm"))) db48x_task(void *arg) {
    (void)arg;
    dmcp_run(&db48x_app);
    vTaskDelete(NULL);
}

void StartDB48X(void) {
    xTaskCreate(db48x_task, "DB48X", 12288, NULL, configMAX_PRIORITIES - 3, NULL);
}
