/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GUARDIAN_FORMAT_H
#define GUARDIAN_FORMAT_H
#include <stddef.h>
#include <stdarg.h>
int guardian_vsnprintf(char *buffer,size_t cap,const char *fmt,va_list args);
int guardian_snprintf(char *buffer,size_t cap,const char *fmt,...);
#endif
