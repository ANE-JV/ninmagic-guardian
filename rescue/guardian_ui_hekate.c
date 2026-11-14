/* SPDX-License-Identifier: GPL-2.0-only */
#include <bdk.h>
#include "guardian_ui_text.h"
int guardian_ui_backend_snapshot(guardian_ui_surface *s) {
    if(!s || !gfx_ctxt.fb || !gfx_con.gfx_ctxt || gfx_con.mute)return 0;
    *s=(guardian_ui_surface){gfx_ctxt.width,gfx_ctxt.height,gfx_ctxt.stride,
        gfx_con.x,gfx_con.y,gfx_con.fntsz==16?16U:8U};
    return 1;
}
void guardian_ui_backend_position(unsigned x,unsigned y) {gfx_con_setpos(x,y);}
int guardian_ui_backend_glyph(unsigned x,unsigned y,char c) {
    unsigned cell=gfx_con.fntsz==16?16U:8U;
    if(!gfx_ctxt.fb || !gfx_con.gfx_ctxt || gfx_con.mute || c<32 || c>126 ||
       gfx_ctxt.width<cell || gfx_ctxt.height<cell || gfx_ctxt.stride<gfx_ctxt.width ||
       x>gfx_ctxt.width-cell || y>gfx_ctxt.height-cell)return 0;
    gfx_con_setpos(x,y);gfx_putc(c);return 1;
}
