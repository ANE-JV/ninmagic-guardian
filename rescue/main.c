/* SPDX-License-Identifier: GPL-2.0-only
 * Hardware initialization derived from Hekate main.c (CTCaer/naehrwert).
 * This entry replaces Hekate's normal entry. It cannot launch Horizon, Nyx,
 * UMS, eMMC tools, or autoboot; it exposes only the mounted SD filesystem.
 */
#include <string.h>
#include <stdlib.h>
#include <bdk.h>
#include "config.h"
#include "guardian_core.h"
#include "guardian_addons.h"
#include "guardian_fatfs.h"

hekate_config h_cfg;
boot_cfg_t __attribute__((section ("._boot_cfg"))) b_cfg;
const volatile ipl_ver_meta_t __attribute__((section ("._ipl_version"))) ipl_ver={
    .magic=BL_MAGIC,
    .version=(BL_VER_MJ+'0')|((BL_VER_MN+'0')<<8)|((BL_VER_HF+'0')<<16)|(BL_VER_RL<<24),
    .rcfg.rsvd_flags=0,.rcfg.bclk_t210=BPMP_CLK_LOWER_BOOST,.rcfg.bclk_t210b01=BPMP_CLK_DEFAULT_BOOST
};
volatile nyx_storage_t *nyx_str=(nyx_storage_t *)NYX_STORAGE_ADDR;
static gc_context ctx;
static u32 last_progress;
static int sd_ready,minerva_ready;
static char backup_firmware[64];
static ga_review addon_review,addon_selected;
static unsigned char addon_marks[GA_MAX_CANDIDATES];
static u32 last_memory_maintenance;
#define BG 0xff111526
#define BLUE 0xff329aff
#define NEON_GREEN 0xff70ff8b
#define AMBER 0xffffb44d

static void maintain_memory(void) {
    u32 now=get_tmr_ms();
    if (now-last_memory_maintenance>=1000) {
        last_memory_maintenance=now; minerva_periodic_training();
    }
}

static void header(const char *title) {
    gfx_clear_color(BG); gfx_con.fntsz=16;
    gfx_con_setcol(BLUE,1,BG); gfx_con_setpos(24,30);
    gfx_puts("NINMAGIC GUARDIAN\n\n");
    gfx_con_setcol(NEON_GREEN,1,BG); gfx_printf("  RESCATE SD - 0.1.1\n\n");
    gfx_con_setcol(TXT_CLR_DEFAULT,1,BG); gfx_printf("  %s\n\n",title);
    gfx_printf("  %s | Solo archivos de la microSD\n",ctx.model);
    gfx_puts("  Sin acceso a NAND / emuMMC\n\n");
    gfx_con_setcol(AMBER,1,BG);
    gfx_puts("  PRUEBA PREVIA: hardware aun no validado\n\n");
    gfx_con_setcol(TXT_CLR_DEFAULT,1,BG);
}
static u32 wait_buttons(void) {
    const u32 mask=BTN_POWER|BTN_VOL_UP|BTN_VOL_DOWN;
    while (btn_read()&mask) { maintain_memory(); msleep(16); }
    for (;;) {
        u32 b=btn_read()&mask; if (b) return b;
        maintain_memory(); msleep(16);
    }
}
static void wait_key(void) {
    gfx_con_setcol(NEON_GREEN,1,BG); gfx_con_setpos(24,1160);
    gfx_puts("VOL +/- o POWER: volver al menu\nSalir / reiniciar esta en el menu.");
    (void)wait_buttons();
}
static int confirm(const char *title,const char *message,int firmware_warning) {
    int yes=0;
    for (;;) {
        header(title); gfx_printf("  %s\n\n",message);
        gfx_puts("  Juegos y partidas se conservan.\n  Solo se aplica la operacion indicada.\n\n");
        if (firmware_warning) {
            gfx_con_setcol(AMBER,1,BG);
            gfx_printf("  Firmware de la copia: %s\n",backup_firmware);
            gfx_puts("  Este rescate no lee el firmware actual.\n  Confirma solo si sigue siendo compatible.\n\n");
        }
        gfx_con_setcol(TXT_CLR_DEFAULT,1,BG);
        gfx_printf("  %s Cancelar\n\n  %s Confirmar\n\n",yes?" ":">",yes?">":" ");
        gfx_puts("  VOL: cambiar | POWER: elegir\n");
        u32 b=wait_buttons(); if (b&(BTN_VOL_UP|BTN_VOL_DOWN)) yes=!yes;
        if (b&BTN_POWER) return yes;
    }
}
static void progress(void *u,const char *path,uint64_t done,uint64_t total) {
    u32 now=get_tmr_ms(); (void)u;
    if (now-last_progress<100 && done!=total) return;
    last_progress=now;
    gfx_clear_partial_grey(0x11,400,500);
    gfx_con_setpos(24,410); gfx_con_setcol(BLUE,1,BG);
    gfx_printf("Trabajando: %u %%\n\n",total?(u32)(done*100/total):0);
    gfx_con_setcol(TXT_CLR_DEFAULT,1,BG); gfx_puts(path);
    gfx_puts("\n\nNo apagues ni retires la microSD.\n");
    maintain_memory();
}
static void show_result(gc_result r) {
    header(r.ok?"Operacion completada":"Revisa el resultado");
    gfx_con_setcol(r.ok?BLUE:AMBER,1,BG);
    gfx_printf("  %s\n",r.message);
    if (r.info.present) {
        u32 tenths=(u32)((r.info.bytes*10U+524288U)/1048576U);
        gfx_printf("\n  Archivos: %u\n  Respaldo: %u.%u MiB\n  Creado: %s\n",r.info.files,tenths/10,tenths%10,r.info.created);
    }
    if (r.recovery_required) gfx_puts("\n  Conserva el respaldo. Reintenta recuperar.\n");
    if (r.info.present) gfx_printf("\n  Firmware de la copia: %s\n",r.info.firmware);
    wait_key();
}
static int matching_backup(void) {
    guardian_fatfs_clear_error();
    gc_result r=gc_inspect(&ctx,0);
    if (!r.ok) {
        const guardian_fs_error *fs=guardian_fatfs_last_error();
        header("No se pudo leer el respaldo");
        gfx_printf("  %s\n\n",r.message);
        if (fs->code) gfx_printf("  FatFs: %d / %s\n  %s\n",fs->code,fs->operation,fs->path);
        gfx_puts("\n  No significa que la copia no exista.\n  No se ha restaurado ni borrado nada.\n");
        wait_key(); return 0;
    }
    if (!r.info.present) {
        header("No se encontro una copia en esta ruta");
        gfx_puts("  config/NinMagicGuardian/backup/current\n\n");
        gfx_printf("  %s\n\n",r.message);
        gfx_puts("  Vuelve al NRO para comprobar la ubicacion.\n  No se ha modificado ningun archivo.\n");
        wait_key(); return 0;
    }
    if (!guardian_match_sd_identity(&ctx,r.info.identity)) {
        char raw[33],sdk[33]; guardian_sd_cids(raw,sdk);
        header("Respaldo encontrado: identidad diferente");
        gfx_printf("  CID del respaldo:\n  %s\n\n  CID de la SD, formato SDK:\n  %s\n\n  Registro fisico Hekate:\n  %s\n",r.info.identity,sdk,raw);
        gfx_puts("\n  La restauracion sigue bloqueada.\n  Fotografia este diagnostico para revisarlo.\n");
        wait_key(); return 0;
    }
    memcpy(backup_firmware,r.info.firmware,sizeof(backup_firmware));
    backup_firmware[sizeof(backup_firmware)-1]=0;
    return 1;
}
static int can_write(void) {
    int soc=0;
    if (!minerva_ready) { header("Falta recurso de memoria"); gfx_puts("  Copia de nuevo la carpeta de rescate.\n  Restauracion deshabilitada.\n"); wait_key(); return 0; }
    if (max17050_get_property(MAX17050_RepSOC,&soc) || (soc>>8)<20) {
        header("Carga la bateria antes de recuperar");
        gfx_puts("  Se necesita al menos un 20 % de bateria.\n"); wait_key(); return 0;
    }
    return matching_backup();
}
static void show_addon_result(ga_result r) {
    header(r.ok?"Operacion de extras completada":"Revisa los extras");
    gfx_con_setcol(r.ok?BLUE:AMBER,1,BG);
    gfx_printf("  %s\n\n  Elementos cambiados: %u\n",r.message,r.changed);
    if (r.recovery_required) gfx_puts("\n  Hay una operacion confirmada pendiente.\n  Puedes reanudarla desde esta opcion.\n");
    wait_key();
}
static void restore_system(void) {
    if (!can_write()) return;
    /* Finish only backup-directory promotion first, so the firmware shown in
     * the confirmation belongs to the exact backup gc_restore will use. */
    header("Preparando la copia definitiva");
    gc_result r=gc_recover_backup(&ctx);
    if (!r.ok) { show_result(r); return; }
    r=gc_inspect(&ctx,1);
    if (!r.ok || !r.info.present) { show_result(r); return; }
    if (!guardian_match_sd_identity(&ctx,r.info.identity)) {
        header("La identidad de la SD no coincide"); wait_key(); return;
    }
    memcpy(backup_firmware,r.info.firmware,sizeof(backup_firmware));
    backup_firmware[sizeof(backup_firmware)-1]=0;
    if (confirm("Restaurar respaldo","La copia definitiva ya esta verificada.",1)) {
        header("Restaurando"); show_result(gc_restore(&ctx));
    }
}
static void review_addons(void) {
    if (!matching_backup()) return;
    header("Comparando modulos y tema de HOME");
    addon_review=ga_inspect(&ctx);
    if (!addon_review.quarantine_pending && (!addon_review.ok || !addon_review.count)) {
        header("Revision de extras"); gfx_printf("  %s\n",addon_review.message); wait_key(); return;
    }
    if (addon_review.quarantine_pending) {
        if (can_write() && confirm("Continuar aislamiento pendiente","Continua el plan ya confirmado.",0)) {
            header("Continuando aislamiento"); show_addon_result(ga_quarantine(&ctx,&addon_review));
        }
        return;
    }
    memset(addon_marks,0,sizeof(addon_marks));
    unsigned selected=0;
    for (;;) {
        unsigned marked=0,start=selected>4?selected-4:0,end=start+9;
        if (end>addon_review.count+2) end=addon_review.count+2;
        for (unsigned i=0;i<addon_review.count;i++) marked+=addon_marks[i]!=0;
        header("Elige extras que quieres apartar");
        gfx_con_setcol(AMBER,1,BG);
        gfx_puts("  Son nuevos o cambiados desde la copia.\n  No prueba que sean la causa del fallo.\n\n");
        gfx_con_setcol(TXT_CLR_DEFAULT,1,BG);
        gfx_printf("  %u elegidos. Se pueden reactivar despues.\n\n",marked);
        for (unsigned i=start;i<end;i++) {
            gfx_con_setcol(i==selected?NEON_GREEN:TXT_CLR_DEFAULT,1,BG);
            if (i<addon_review.count) {
                ga_candidate *c=&addon_review.candidates[i];
                gfx_printf("  %s [%s] %s\n    %s | %s\n\n",i==selected?">":" ",addon_marks[i]?"X":" ",c->title_id,
                    c->kind==GA_HOME_THEME?"Tema HOME":"Modulo de arranque",c->newly_added?"Nuevo":"Cambiado");
            } else gfx_printf("  %s %s\n\n",i==selected?">":" ",i==addon_review.count?"Continuar con lo elegido":"Cancelar");
        }
        gfx_con_setcol(TXT_CLR_DEFAULT,1,BG); gfx_puts("\n  VOL: mover | POWER: marcar / elegir\n");
        u32 b=wait_buttons(); unsigned count=addon_review.count+2;
        if (b&BTN_VOL_UP) selected=(selected+count-1)%count;
        if (b&BTN_VOL_DOWN) selected=(selected+1)%count;
        if (!(b&BTN_POWER)) continue;
        if (selected<addon_review.count) { addon_marks[selected]=!addon_marks[selected]; continue; }
        if (selected==addon_review.count+1) return;
        if (!marked) continue;
        addon_selected=addon_review; addon_selected.count=0;
        for (unsigned i=0;i<addon_review.count;i++) if (addon_marks[i]) addon_selected.candidates[addon_selected.count++]=addon_review.candidates[i];
        if (can_write() && confirm("Apartar extras elegidos","Se moveran a una carpeta de recuperacion.",0)) {
            header("Apartando extras elegidos"); show_addon_result(ga_quarantine(&ctx,&addon_selected)); return;
        }
    }
}
void guardian_hw_init(void) {
    if (ipl_ver.rcfg.rsvd_flags&RSVD_FLAG_DRAM_8GB) fuse_force_8gb_dramid();
    hw_init();
}
void guardian_main(void) {
    heap_init((void *)IPL_HEAP_START);
    set_default_configuration(); max77620_rtc_prep_read();
    display_init(); bpmp_clk_rate_set(h_cfg.t210b01?ipl_ver.rcfg.bclk_t210b01:ipl_ver.rcfg.bclk_t210);
    sd_ready=sd_mount()==0;
    if (sd_ready) minerva_ready=minerva_init((minerva_str_t *)&nyx_str->minerva)==0;
    u32 *fb=display_init_window_a_pitch(); gfx_init_ctxt(fb,720,1280,720); gfx_con_init();
    display_backlight_pwm_init(); display_backlight_brightness(100,1000);
    guardian_fatfs_init(&ctx); ctx.progress=progress;
    int selected=0;
    const char *menu[]={"Verificar respaldo (solo lectura)","Restaurar respaldo / continuar rescate","Revisar modulos nuevos y tema HOME","Reactivar extras apartados","Recuperar respaldo interrumpido","Salir / reiniciar consola","Salir / apagar consola"};
    for (;;) {
        header("Recupera Atmosphere y el arranque");
        if (!sd_ready) gfx_puts("  No se pudo montar la SD. Apaga y revisala.\n\n");
        for (int i=0;i<7;i++) { gfx_con_setcol(i==selected?NEON_GREEN:TXT_CLR_DEFAULT,1,BG); gfx_printf("  %s %s\n\n",i==selected?">":" ",menu[i]); }
        gfx_con_setcol(TXT_CLR_DEFAULT,1,BG); gfx_puts("\n  VOL +/-: mover | POWER: elegir\n");
        u32 b=wait_buttons();
        if (b&BTN_VOL_UP) selected=(selected+6)%7;
        if (b&BTN_VOL_DOWN) selected=(selected+1)%7;
        if (!(b&BTN_POWER)) continue;
        if (selected==5) {
            if (confirm("Reiniciar consola","Usara el metodo de arranque configurado.\n  Puede necesitar cargar tu payload habitual.\n  No garantiza regresar a Hekate.",0)) power_set_state(POWER_OFF_REBOOT);
            continue;
        }
        if (selected==6) { sd_end(); power_set_state(POWER_OFF_RESET); }
        if (!sd_ready) continue;
        if (selected==0) { if (matching_backup()) { header("Verificando archivos"); show_result(gc_inspect(&ctx,1)); } }
        else if (selected==1) restore_system();
        else if (selected==2) review_addons();
        else if (selected==3 && can_write() && confirm("Reactivar extras apartados","No se sustituiran archivos nuevos existentes.",0)) { header("Reactivando extras"); show_addon_result(ga_restore_quarantine(&ctx)); }
        else if (selected==4 && can_write() && confirm("Recuperar respaldo","Continua una copia interrumpida.",0)) { header("Recuperando respaldo"); show_result(gc_recover_backup(&ctx)); }
    }
}
