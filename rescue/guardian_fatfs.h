/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GUARDIAN_FATFS_H
#define GUARDIAN_FATFS_H
#include "guardian_core.h"
void guardian_fatfs_init(gc_context *ctx);
int guardian_match_sd_identity(gc_context *ctx, const char *expected);
typedef struct { int code; char operation[24],path[512]; } guardian_fs_error;
void guardian_fatfs_clear_error(void);
const guardian_fs_error *guardian_fatfs_last_error(void);
void guardian_sd_cids(char raw[33],char sdk[33]);
#endif
