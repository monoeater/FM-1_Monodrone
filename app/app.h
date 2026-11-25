/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Michael (MONODRONE) */
/* MONODRONE: a drone instrument for the M-VAVE FM-1, on fm1-base. This header is the app's half of the contract
 * with the platform (base/device/unity.c, base/host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "MONODRONE"
#define APP_SPLASH_RGB RGB(170, 230, 40)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_TOUCH                        /* the keys' pressure swells the drone they hold (lib/touch.h) */
#define APP_CANVAS_PX (240u * 112u)      /* the tallest band drawn at once */

/* the store: the panel calibration (the platform's) and MONODRONE's 16 scenes with how it was left. Each object is
 * two 4 KB sectors. 0xD4000..0xD5FFF is in the data area every Felucca-family firmware shares (see NOTES.md):
 * saving there can overwrite another firmware's data at those addresses. */
enum { OBJ_PANEL, OBJ_SCENES, OBJ_NOBJ };
#define APP_STORE_MAP {0xDC000u, 0xD4000u}

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
#define APP_OVERLOAD                     /* the render over 90 % of its time: the header's load goes red */
void app_overload(void);

#ifdef PLAT_HOST
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#endif
