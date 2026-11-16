#ifndef BB_DEBUG_UI_H
#define BB_DEBUG_UI_H

/*
 * Debug view with Dear ImGui (ADR-0005, point 7): one window with docked
 * panels, the game picture is one of them. Desktop debug builds only
 * (BB_DEBUG_UI). Dear ImGui is C++; debug_ui.cpp keeps it behind this C
 * interface, so the rest of the platform layer stays C and the game code
 * never sees it.
 */

#include "raylib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What the platform tells the panels about the run. */
typedef struct {
    int         paused, slow;
    int         replaying, recording;
    long        tick;
    long        end_tick;    /* length of the replay; 0 when not replaying */
    int         drifted;     /* the replay no longer matches the game */
    const char *message;     /* the last debug message, "" when none */
} DebugHost;

/* --- Provided by the platform (platform_raylib.c) ---------------------------
 * Everything that changes the game goes through these, so that it is recorded
 * in the replay file (or refused while a replay plays), as the F-keys are. */
void host_state(DebugHost *out);
void host_set_paused(int on);
void host_step(void);
void host_set_slow(int on);
void host_toggle_switch(int sw);
void host_spawn(int what);
void host_clear_round(void);
void host_goto_round(int round);
void host_mark(void);

/* --- Provided by debug_ui.cpp ------------------------------------------------ */
void debug_ui_init(void);
void debug_ui_shutdown(void);
int  debug_ui_wants_keyboard(void);   /* typing into a field: keep the keys from the game */
void debug_ui_draw(Texture2D game);   /* between BeginDrawing() and EndDrawing() */

#ifdef __cplusplus
}
#endif

#endif /* BB_DEBUG_UI_H */
