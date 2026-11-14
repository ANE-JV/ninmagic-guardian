/* SPDX-License-Identifier: GPL-2.0-only */
#include "guardian_ui_text.h"
#include "guardian_format.h"
#include <stdarg.h>
#include <limits.h>
static unsigned clip_left,clip_top,clip_right=UINT_MAX,clip_bottom=UINT_MAX;
void guardian_ui_clip(unsigned left,unsigned top,unsigned right,unsigned bottom) {
    clip_left=left;clip_top=top;clip_right=right;clip_bottom=bottom;
}
void guardian_ui_puts(const char *text) {
    guardian_ui_surface s;unsigned left,top,right,bottom,x,y;
    if (!text || !guardian_ui_backend_snapshot(&s) ||
        (s.cell!=8 && s.cell!=16) || s.stride<s.width) return;
    left=clip_left;top=clip_top;right=clip_right;bottom=clip_bottom;
    if(right>s.width)right=s.width;
    if(right>s.stride)right=s.stride;
    if(bottom>s.height)bottom=s.height;
    if(left>=right || top>=bottom || s.cell>right-left || s.cell>bottom-top)return;
    x=s.x<left?left:s.x;y=s.y<top?top:s.y;
    for (;*text;text++) {
        unsigned char c=(unsigned char)*text;
        if (y>bottom-s.cell) break;
        if (c=='\r') {x=left;continue;}
        if (c=='\n' || x>right-s.cell) {
            x=left;
            if (bottom-y<s.cell*2U) {y=bottom;break;}
            y+=s.cell;
            if (c=='\n')continue;
        }
        if(c=='\t')c=' ';else if(c<32 || c>126)c='?';
        /* Never hand a newline to gfx_putc: it wraps vertically back to zero.
         * Every printable glyph gets an explicit checked position instead. */
        if (!guardian_ui_backend_glyph(x,y,(char)c)) break;
        x+=s.cell;
    }
    if (x>right)x=right;
    if (y>bottom)y=bottom;
    guardian_ui_backend_position(x,y);
}
int guardian_ui_printf(const char *fmt,...) {
    char buffer[1024];va_list ap;va_start(ap,fmt);
    int result=guardian_vsnprintf(buffer,sizeof(buffer),fmt,ap);
    va_end(ap);
    if (result<0) guardian_ui_puts("No se pudo mostrar este texto.\n");
    else guardian_ui_puts(buffer);
    return result;
}
void guardian_ui_backup_summary(const gc_backup_info *info) {
    if (!info || !info->present) return;
    unsigned tenths=(unsigned)((info->bytes*10U+524288U)/1048576U);
    guardian_ui_printf("\n  Archivos: %u\n  Respaldo: %u.%u MiB\n  Creado: %s\n",
        info->files,tenths/10,tenths%10,info->created);
}
