#ifndef NINMAGIC_GUARDIAN_CORE_H
#define NINMAGIC_GUARDIAN_CORE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define GC_PATH_MAX 240
#define GC_MAX_FILES 4096
#define GC_BACKUP_ROOT "config/NinMagicGuardian/backup"
#define GC_OPT_VISUALS 1u
#define GC_OPT_PAYLOADS 2u
typedef enum { GC_MISSING = 0, GC_FILE = 1, GC_DIR = 2, GC_OTHER = 3 } gc_type;
typedef struct {
  gc_type type;
  uint64_t size;
} gc_stat;
/* list supplies bare names only; visit returns 0 to continue, -1 to stop. */
typedef int (*gc_visit)(void *visit_user, const char *name, gc_type type);
/* Every path is SD-root-relative. All functions return 0 on success except
 * stat: 1 found, 0 missing, -1 error. open: NULL is failure. No symlink follow.
 * open mode is 'r' read-only or 'x' create-new/exclusive (never truncate).
 * sync commits directory/file metadata; file_sync flushes an open writer. */
typedef struct {
  void *user;
  int (*stat)(void *, const char *, gc_stat *);
  int (*list)(void *, const char *, gc_visit, void *);
  void *(*open)(void *, const char *, char);
  int (*read)(void *, void *, void *, size_t, size_t *);
  int (*write)(void *, void *, const void *, size_t);
  int (*file_sync)(void *, void *);
  int (*close)(void *, void *);
  int (*mkdir)(void *, const char *);
  int (*rename)(void *, const char *, const char *);
  int (*remove)(void *, const char *, gc_type);
  int (*sync)(void *);
  int (*free_space)(void *, uint64_t *);
  /* Optional fault hook: zero success, nonzero interruption/error. */
  int (*checkpoint)(void *, const char *, const char *);
} gc_fs;
typedef struct {
  gc_fs fs;
  /* 32 nonzero hexadecimal digits identifying this SD. Same representation
   * in NRO and rescue is mandatory; unknown identity blocks writes. */
  char identity[33];
  char firmware[64];
  char model[64];
  char created[64];
  void (*progress)(void *, const char *, uint64_t, uint64_t);
  void *progress_user;
} gc_context;
typedef struct {
  unsigned files, options;
  uint64_t bytes;
  char identity[33], firmware[64], model[64], created[64];
  int present, verified, restore_pending;
} gc_backup_info;
typedef struct {
  int ok, recovery_required;
  char message[512];
  gc_backup_info info;
} gc_result;
/* Inspection is read-only; verify hashes all payloads when verify!=0. */
gc_result gc_inspect(gc_context *, int verify);
/* Read-only source estimate; does not create directories or backup data. */
gc_result gc_scan_source(gc_context *, unsigned options);
/* backup uses essential allowlisted files automatically; optional flags above.
 * Existing verified backup is retained until staging has verified and promoted.
 */
gc_result gc_create_backup(gc_context *, unsigned options);
/* Restores only paths named by the verified allowlisted backup. A durable
 * per-file journal makes retry safe. No NAND, games or extras are deleted. */
gc_result gc_restore(gc_context *);
/* Resume interrupted backup promotion/cleanup, then inspect; does not restore
 * system files. gc_restore itself resumes an interrupted restore. */
gc_result gc_recover_backup(gc_context *);
int gc_safe_path(const char *);
int gc_managed_path(const char *, unsigned options);
#ifdef __cplusplus
}
#endif
#endif
