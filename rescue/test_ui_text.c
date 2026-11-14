/* SPDX-License-Identifier: GPL-2.0-only */
#include "guardian_ui_text.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#define H 1280U
#define MAX_STRIDE 736U
#define GUARD 16U
#define UNTOUCHED 0xa5a5a5a5U
static uint32_t memory[GUARD+MAX_STRIDE*H+GUARD];
static char screen[20000];
static guardian_ui_surface surface;
static unsigned glyphs,rows[H],left,top,right,bottom;
static int ready;
static void capture(char c){size_t n=strlen(screen);assert(n+1<sizeof screen);screen[n]=c;screen[n+1]=0;}
int guardian_ui_backend_snapshot(guardian_ui_surface *out){*out=surface;return ready;}
void guardian_ui_backend_position(unsigned x,unsigned y){
    if(y>surface.y && surface.cell)for(unsigned n=(y-surface.y)/surface.cell;n;n--)capture('\n');
    surface.x=x;surface.y=y;
}
int guardian_ui_backend_glyph(unsigned x,unsigned y,char c){
    assert(c>=32 && c<=126);
    assert(surface.cell==8 || surface.cell==16);
    assert(surface.stride>=surface.width && surface.width>=surface.cell && surface.height>=surface.cell);
    assert(x<=surface.width-surface.cell && y<=surface.height-surface.cell);
    assert(x>=left && y>=top && x+surface.cell<=right && y+surface.cell<=bottom);
    guardian_ui_backend_position(x,y);capture(c);glyphs++;rows[y]++;
    /* Fill the WHOLE glyph cell, stronger than any font's actual bitmap. */
    for(unsigned yy=y;yy<y+surface.cell;yy++)for(unsigned xx=x;xx<x+surface.cell;xx++)
        memory[GUARD+yy*surface.stride+xx]=0x12345678U;
    surface.x=x+surface.cell;return 1;
}
static void clip(unsigned l,unsigned t,unsigned r,unsigned b){left=l;top=t;right=r;bottom=b;guardian_ui_clip(l,t,r,b);}
static void reset(unsigned stride){
    for(unsigned n=0;n<sizeof memory/sizeof memory[0];n++)memory[n]=UNTOUCHED;
    memset(rows,0,sizeof rows);screen[0]=0;glyphs=0;ready=1;
    surface=(guardian_ui_surface){720,H,stride,0,0,16};clip(0,0,720,H);
}
static void guards(void){
    for(unsigned n=0;n<GUARD;n++){assert(memory[n]==UNTOUCHED);assert(memory[GUARD+MAX_STRIDE*H+n]==UNTOUCHED);}
    for(unsigned y=0;y<H;y++)for(unsigned x=surface.width;x<surface.stride;x++)assert(memory[GUARD+y*surface.stride+x]==UNTOUCHED);
}
static void untouched_rows(unsigned start,unsigned end){
    for(unsigned y=start;y<end;y++)for(unsigned x=0;x<surface.stride;x++)assert(memory[GUARD+y*surface.stride+x]==UNTOUCHED);
}
int main(void){
    gc_backup_info info={0};char long_text[12000];
    info.present=1;info.files=42;info.bytes=11106448;strcpy(info.created,"2001-02-03 04:05 UTC");
    reset(720);guardian_ui_backup_summary(&info);
    assert(!strcmp(screen,"\n  Archivos: 42\n  Respaldo: 10.6 MiB\n  Creado: 2001-02-03 04:05 UTC\n"));assert(!strchr(screen,'%'));guards();
    reset(720);guardian_ui_printf("Trabajando: %u %%\n",50U);assert(!strcmp(screen,"Trabajando: 50 %\n"));guards();
    reset(720);guardian_ui_printf("FatFs: %d / %s\nCID: %s\n",-1,"leer","78563412f0debc9adf9b5713ac682400");
    assert(!strcmp(screen,"FatFs: -1 / leer\nCID: 78563412f0debc9adf9b5713ac682400\n"));guards();
    reset(720);guardian_ui_printf("%s","literal %u %s 100% ready");assert(!strcmp(screen,"literal %u %s 100% ready"));guards();
    memset(long_text,'A',sizeof long_text-1);long_text[sizeof long_text-1]=0;
    reset(736);clip(0,0,720,1120);guardian_ui_puts(long_text);assert(glyphs==45U*70U);untouched_rows(1120,H);guards();
    guardian_ui_puts("MORE MUST NOT WRAP TO TOP");assert(glyphs==45U*70U);guards();
    reset(736);surface.x=24;surface.y=30;guardian_ui_puts("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    assert(rows[30]==43 && rows[46]==45);guards();
    reset(736);surface.x=704;surface.y=1264;guardian_ui_puts("AB\nMORE");assert(glyphs==1);guards();
    reset(736);clip(0,0,720,16);guardian_ui_puts(long_text);assert(glyphs==45);untouched_rows(16,H);guards();
    reset(736);clip(0,0,720,16);guardian_ui_puts("\nX");assert(glyphs==0);untouched_rows(0,H);guards();
    reset(736);clip(0,1144,720,1264);surface.x=24;surface.y=1160;guardian_ui_puts(long_text);untouched_rows(0,1144);untouched_rows(1264,H);guards();
    reset(736);clip(0,0,720,1120);for(unsigned i=0;i<300;i++)guardian_ui_puts("\n");guardian_ui_puts("NO TOP RESET");assert(!glyphs);untouched_rows(0,H);guards();
    reset(736);surface.x=UINT_MAX;surface.y=UINT_MAX;guardian_ui_puts("BAD CURSOR");assert(!glyphs);guards();
    reset(736);guardian_ui_puts("\xff\x01\x80");assert(!strcmp(screen,"???"));guards();
    reset(736);ready=0;guardian_ui_puts("not ready");assert(!glyphs);guards();
    reset(700);guardian_ui_puts("invalid stride");assert(!glyphs);guards();
    reset(736);surface.cell=8;surface.x=712;surface.y=1272;guardian_ui_puts("AB");assert(glyphs==1);guards();
    reset(720);info.present=0;guardian_ui_backup_summary(&info);assert(!screen[0]);guards();
    puts("PASS: real frontend summary 42/10.6/date; wrap+clip 720x1280 fnt16; glyph and stride canaries; long text/path; bottom, footer, controls and percent literal");
    return 0;
}
