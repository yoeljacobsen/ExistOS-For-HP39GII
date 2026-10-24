#pragma once

#include <stdbool.h>
#include <stdint.h>

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

    // True if the application's main loop never returns. DB48X loops forever
    // and relies on DMCP's sys_sleep() not coming back; C47 instead returns
    // from program_main() after saving its state, and must be allowed to.
    bool never_returns;
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
