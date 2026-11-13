/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GUARDIAN_FATFS_H
#define GUARDIAN_FATFS_H
#include "guardian_core.h"
void guardian_fatfs_init(gc_context *ctx);
int guardian_match_sd_identity(gc_context *ctx, const char *expected);
#endif
