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

static int db48x_indicators(void) {
    return 0;   // TODO: map DB48X's shift/alpha state onto INDICATE_*
}

static void db48x_run(void) {
    db48x_program_main();
}

static const dmcp_app_t db48x_app = {
    .name       = "DB48X",
    .run        = db48x_run,
    .indicators = db48x_indicators,
    .exit_flag  = &db48x_exit,
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
