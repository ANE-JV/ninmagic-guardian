/* SPDX-License-Identifier: GPL-2.0-only
 * NinMagic Guardian SD-only adapter. No eMMC/emuMMC device is exposed.
 */
#include <string.h>
#include <stdlib.h>
#include <bdk.h>
#include <libs/fatfs/ff.h>
#include <libs/fatfs/diskio.h>
#include "guardian_fatfs.h"

static int path_ok(const char *p) {
    size_t n,begin=0; char segment[GC_PATH_MAX+1];
    if (!p || !(n=strlen(p)) || n>=512) return 0;
    if (n<=GC_PATH_MAX) return gc_safe_path(p);
    /* Managed paths are capped at GC_PATH_MAX, but their backup/journal path
     * adds a trusted prefix. Validate every segment without truncating it. */
    for (size_t end=0;end<=n;end++) if (p[end]=='/' || !p[end]) {
        size_t length=end-begin;
        if (!length || length>GC_PATH_MAX) return 0;
        memcpy(segment,p+begin,length); segment[length]=0;
        if (!gc_safe_path(segment)) return 0;
        begin=end+1;
    }
    return 1;
}
static gc_type type_of(const FILINFO *i) { return (i->fattrib & AM_DIR) ? GC_DIR : GC_FILE; }
static int fs_stat(void *u,const char *p,gc_stat *o) {
    FILINFO i; FRESULT r; (void)u;
    if (!path_ok(p)) return -1;
    r=f_stat(p,&i);
    if (r==FR_NO_FILE || r==FR_NO_PATH) return 0;
    if (r!=FR_OK) return -1;
    o->type=type_of(&i); o->size=i.fsize; return 1;
}
static int fs_list(void *u,const char *p,gc_visit v,void *vu) {
    DIR d; FILINFO i; FRESULT r; int out=0; (void)u;
    if (p && *p && !path_ok(p)) return -1;
    if (f_opendir(&d,p && *p?p:"/")!=FR_OK) return -1;
    for (;;) {
        r=f_readdir(&d,&i);
        if (r!=FR_OK) { out=-1; break; }
        if (!i.fname[0]) break;
        if (!strcmp(i.fname,".") || !strcmp(i.fname,"..")) continue;
        if (strchr(i.fname,'/') || strchr(i.fname,'\\') || strchr(i.fname,':')) { out=-1; break; }
        if (v(vu,i.fname,type_of(&i))) { out=-1; break; }
    }
    if (f_closedir(&d)!=FR_OK) out=-1;
    return out;
}
static void *fs_open(void *u,const char *p,char mode) {
    FIL *f; BYTE m; (void)u;
    if (!path_ok(p) || (mode!='r' && mode!='x')) return NULL;
    m=mode=='r'?FA_READ:(FA_WRITE|FA_CREATE_NEW);
    f=calloc(1,sizeof(*f)); if (!f) return NULL;
    if (f_open(f,p,m)!=FR_OK) { free(f); return NULL; }
    return f;
}
static int fs_read(void *u,void *f,void *b,size_t n,size_t *got) {
    UINT count=0; FRESULT r; (void)u; if (n>0x7fffffffU) return -1;
    r=f_read(f,b,(UINT)n,&count); *got=count; return r==FR_OK?0:-1;
}
static int fs_write(void *u,void *f,const void *b,size_t n) {
    UINT count=0; (void)u; if (n>0x7fffffffU) return -1;
    return f_write(f,b,(UINT)n,&count)==FR_OK && count==n?0:-1;
}
static int fs_file_sync(void *u,void *f) { (void)u; return f_sync(f)==FR_OK?0:-1; }
static int fs_close(void *u,void *f) { FRESULT r; (void)u; r=f_close(f); free(f); return r==FR_OK?0:-1; }
static int fs_mkdir(void *u,const char *p) { (void)u; return path_ok(p) && f_mkdir(p)==FR_OK?0:-1; }
static int fs_rename(void *u,const char *a,const char *b) {
    FILINFO i; FRESULT r; (void)u;
    if (!path_ok(a) || !path_ok(b)) return -1;
    r=f_stat(b,&i); if (r!=FR_NO_FILE && r!=FR_NO_PATH) return -1;
    return f_rename(a,b)==FR_OK?0:-1;
}
static int fs_remove(void *u,const char *p,gc_type type) {
    FILINFO i; (void)u;
    if (!path_ok(p) || f_stat(p,&i)!=FR_OK || type_of(&i)!=type) return -1;
    return f_unlink(p)==FR_OK?0:-1;
}
/* FatFs syncs directory changes within mkdir/rename/unlink; writers have their
 * own f_sync before close. SDMMC writes wait for the SD command to complete. */
static int fs_sync(void *u) { (void)u; return disk_ioctl(0,CTRL_SYNC,NULL)==RES_OK?0:-1; }
static int fs_free(void *u,uint64_t *n) {
    DWORD clusters; FATFS *f; (void)u;
    if (f_getfree("",&clusters,&f)!=FR_OK) return -1;
    *n=(uint64_t)clusters*f->csize*512U; return 0;
}
static void cid_hex(char *out,const u8 *bytes) {
    static const char h[]="0123456789abcdef"; unsigned j;
    for (j=0;j<16;j++) { out[j*2]=h[bytes[j]>>4]; out[j*2+1]=h[bytes[j]&15]; }
    out[32]=0;
}
/* Horizon and SDMMC implementations expose the same 128-bit CID with different
 * byte/word ordering. Match ONLY a permutation of this physical SD's CID. */
int guardian_match_sd_identity(gc_context *ctx,const char *expected) {
    u8 b[16]; char hex[33]; unsigned mode,j;
    if (!expected || strlen(expected)!=32) return 0;
    for (mode=0;mode<4;mode++) {
        for (j=0;j<16;j++) {
            unsigned k=j;
            if (mode&1) k=(3-(k/4))*4+(k%4);
            if (mode&2) k=(k/4)*4+(3-(k%4));
            b[j]=sd_storage.raw_cid[k];
        }
        cid_hex(hex,b);
        if (!strcmp(hex,expected)) { memcpy(ctx->identity,hex,33); return 1; }
    }
    memset(ctx->identity,0,sizeof(ctx->identity)); return 0;
}
void guardian_fatfs_init(gc_context *c) {
    memset(c,0,sizeof(*c));
    c->fs=(gc_fs){NULL,fs_stat,fs_list,fs_open,fs_read,fs_write,fs_file_sync,
        fs_close,fs_mkdir,fs_rename,fs_remove,fs_sync,fs_free,NULL};
    cid_hex(c->identity,sd_storage.raw_cid);
    strcpy(c->firmware,"Preboot SD rescue");
    strcpy(c->model,hw_get_chip_id()==GP_HIDREV_MAJOR_T210B01?"Mariko":"Erista");
    strcpy(c->created,"preboot");
}
