/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#ifndef NTRDPS_GINA_H
#define NTRDPS_GINA_H

#include <windows.h>

typedef struct {
    void       *ctx;
    HANDLE      desk;
    const char *desk_name;
    const char *peer;
    void (*dialog)(void *ctx, HWND dlg, int first);
    int  (*command)(void *ctx, HWND dlg, WPARAM wp);
    int  (*ending)(void *ctx);
} GINA_HOST;

#define GINA_NO      0
#define GINA_SHOWN   1
#define GINA_LOST    2

HMODULE gina_load(void);

int gina_run(GINA_HOST *h);

void gina_end_all(void);

#endif
