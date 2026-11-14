/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GUARDIAN_UI_TEXT_H
#define GUARDIAN_UI_TEXT_H
#include "guardian_core.h"
#if defined(__GNUC__)
#define GUARDIAN_UI_FORMAT __attribute__((format(printf,1,2)))
#else
#define GUARDIAN_UI_FORMAT
#endif
/* All Guardian frontend formatting passes through this function. Hekate's
 * gfx_printf has a different format language and must not receive our %u. */
int guardian_ui_printf(const char *fmt,...) GUARDIAN_UI_FORMAT;
void guardian_ui_puts(const char *text);
void guardian_ui_clip(unsigned left,unsigned top,unsigned right,unsigned bottom);
typedef struct {unsigned width,height,stride,x,y,cell;} guardian_ui_surface;
/* Small backend boundary: production uses Hekate globals, host tests validate
 * every emitted glyph against the exact same bounds contract. */
int guardian_ui_backend_snapshot(guardian_ui_surface *surface);
void guardian_ui_backend_position(unsigned x,unsigned y);
int guardian_ui_backend_glyph(unsigned x,unsigned y,char glyph);
void guardian_ui_backup_summary(const gc_backup_info *info);
#endif
