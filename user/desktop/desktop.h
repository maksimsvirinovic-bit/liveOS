#ifndef LIVEOS_DESKTOP_H
#define LIVEOS_DESKTOP_H

#include "gui_addon/gui.h"
#include "gui_addon/window_server.h"

typedef struct {
    GuiDesktop gui;
    WindowServer server;
} DesktopState;

extern DesktopState g_desktop;

/* app_launcher */
void app_launch(void* ctx, const char* action, const char* args);

/* builtin_terminal */
void terminal_open(void);
void terminal_update(void);
void terminal_close(void);

/* builtin_ui_studio */
void ui_studio_open(void);

#endif