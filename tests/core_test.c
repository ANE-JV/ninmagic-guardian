#define _GNU_SOURCE
#include "guardian_addons.h"
#include "guardian_core.h"
#include "guardian_sha256.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);             \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
#define CUR GC_BACKUP_ROOT "/current"
#define WORK "config/NinMagicGuardian/restore"
typedef struct {
  char root[512];
  const char *fault, *fault_path;
  int fired, writes, fail_write, special_file;
  uint64_t space;
} host;
static void full(host *h, const char *p, char out[1024]) {
  CHECK(p && *p && p[0] != '/' && !strstr(p, "..") && !strchr(p, '\\'));
  CHECK(snprintf(out, 1024, "%s/%s", h->root, p) < 1024);
}
static int hs(void *v, const char *p, gc_stat *s) {
  host *h = v;
  char b[1024];
  struct stat st;
  full(h, p, b);
  if (lstat(b, &st)) {
    return errno == ENOENT ? 0 : -1;
  }
  s->size = (uint64_t)st.st_size;
  s->type = S_ISREG(st.st_mode)   ? GC_FILE
            : S_ISDIR(st.st_mode) ? GC_DIR
                                  : GC_OTHER;
  if (h->special_file && !strcmp(p, "bootloader/sys/unsafe.bin"))
    s->type = GC_OTHER;
  return 1;
}
static int hl(void *v, const char *p, gc_visit visit, void *user) {
  host *h = v;
  char b[1024], child[1024];
  DIR *d;
  struct dirent *e;
  gc_stat s;
  int rc = 0;
  full(h, p, b);
  d = opendir(b);
  if (!d)
    return -1;
  while ((e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
      continue;
    CHECK(snprintf(child, sizeof child, "%s/%s", p, e->d_name) <
          (int)sizeof child);
    if (hs(v, child, &s) != 1 || visit(user, e->d_name, s.type)) {
      rc = -1;
      break;
    }
  }
  closedir(d);
  return rc;
}
static void *ho(void *v, const char *p, char mode) {
  host *h = v;
  char b[1024];
  int fd;
  FILE *f;
  full(h, p, b);
  fd = open(b, mode == 'r' ? O_RDONLY : O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0)
    return NULL;
  f = fdopen(fd, mode == 'r' ? "rb" : "wb");
  if (!f)
    close(fd);
  return f;
}
static int hr(void *v, void *f, void *b, size_t n, size_t *got) {
  (void)v;
  *got = fread(b, 1, n, f);
  return ferror((FILE *)f) ? -1 : 0;
}
static int hw(void *v, void *f, const void *b, size_t n) {
  host *h = v;
  if (h->fail_write && ++h->writes == h->fail_write)
    return -1;
  return fwrite(b, 1, n, f) == n ? 0 : -1;
}
static int hf(void *v, void *f) {
  (void)v;
  return fflush(f) || fsync(fileno(f)) ? -1 : 0;
}
static int hc(void *v, void *f) {
  (void)v;
  return fclose(f);
}
static int hm(void *v, const char *p) {
  host *h = v;
  char b[1024];
  full(h, p, b);
  return mkdir(b, 0700);
}
static int hn(void *v, const char *from, const char *to) {
  host *h = v;
  char a[1024], b[1024];
  struct stat st;
  full(h, from, a);
  full(h, to, b);
  if (!lstat(b, &st))
    return -1;
  return rename(a, b);
}
static int hd(void *v, const char *p, gc_type type) {
  host *h = v;
  char b[1024];
  full(h, p, b);
  return type == GC_DIR ? rmdir(b) : unlink(b);
}
static int hy(void *v) {
  (void)v;
  return 0;
}
static int hb(void *v, uint64_t *bytes) {
  host *h = v;
  *bytes = h->space;
  return 0;
}
static int hp(void *v, const char *point, const char *path) {
  host *h = v;
  if (!h->fired && h->fault && !strcmp(point, h->fault) &&
      (!h->fault_path || !strcmp(path, h->fault_path))) {
    h->fired = 1;
    return -1;
  }
  return 0;
}
static gc_context context(host *h) {
  gc_context c;
  memset(&c, 0, sizeof c);
  c.fs = (gc_fs){h, hs, hl, ho, hr, hw, hf, hc, hm, hn, hd, hy, hb, hp};
  snprintf(c.identity, sizeof c.identity, "00112233445566778899aabbccddeeff");
  snprintf(c.firmware, 64, "test-fw");
  snprintf(c.model, 64, "host-SD");
  snprintf(c.created, 64, "2026-11-13T12:00:00Z");
  return c;
}
static void dirs(host *h, const char *p) {
  char b[1024];
  size_t i;
  full(h, p, b);
  for (i = strlen(h->root) + 1; b[i]; i++)
    if (b[i] == '/') {
      b[i] = 0;
      CHECK(!mkdir(b, 0700) || errno == EEXIST);
      b[i] = '/';
    }
}
static void put(host *h, const char *p, const char *s) {
  char b[1024];
  FILE *f;
  dirs(h, p);
  full(h, p, b);
  f = fopen(b, "wb");
  CHECK(f);
  CHECK(fwrite(s, 1, strlen(s), f) == strlen(s));
  CHECK(!fclose(f));
}
static char *get(host *h, const char *p) {
  char b[1024];
  FILE *f;
  long n;
  char *out;
  full(h, p, b);
  f = fopen(b, "rb");
  CHECK(f);
  CHECK(!fseek(f, 0, SEEK_END));
  n = ftell(f);
  CHECK(n >= 0);
  rewind(f);
  out = malloc((size_t)n + 1);
  CHECK(out);
  CHECK(fread(out, 1, (size_t)n, f) == (size_t)n);
  out[n] = 0;
  CHECK(!fclose(f));
  return out;
}
static void same(host *h, const char *p, const char *s) {
  char *b = get(h, p);
  CHECK(!strcmp(b, s));
  free(b);
}
static int present(host *h, const char *p) {
  gc_stat s;
  return hs(h, p, &s) == 1;
}
static void must_ok(gc_result r) {
  if (!r.ok) {
    fprintf(stderr, "Unexpected: %s\n", r.message);
    exit(1);
  }
}
static void hashhex(const void *p, size_t n, char out[65]) {
  unsigned char h[32];
  unsigned i;
  static const char *d = "0123456789abcdef";
  gc_sha256_buffer(p, n, h);
  for (i = 0; i < 32; i++) {
    out[2 * i] = d[h[i] >> 4];
    out[2 * i + 1] = d[h[i] & 15];
  }
  out[64] = 0;
}
static void fixture(host *h) {
  put(h, "atmosphere/package3", "PACKAGE-A");
  put(h, "bootloader/hekate_ipl.ini",
      "[Atmosphere]\nfss0=atmosphere/package3\npayload=bootloader/payloads/"
      "fusee.bin\n[Linux]\npayload=not-backed-linux.bin\n");
  put(h, "bootloader/sys/nyx.bin", "NYX-A");
  put(h, "bootloader/sys/libsys_minerva.bso", "MINERVA-A");
  put(h, "bootloader/sys/res.pak", "RESOURCES-A");
  put(h, "bootloader/sys/emummc.kipm", "EMUMMC-KIP-A");
  put(h, "bootloader/update.bin", "HEKATE-A");
  put(h, "bootloader/ini/ninmagic-guardian.ini",
      "[Guardian]\npayload=config/NinMagicGuardian/rescue/"
      "ninmagic-guardian-rescue.bin\n");
  put(h, "config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin",
      "RESCUE-A");
  put(h, "config/NinMagicGuardian/rescue/libsys_minerva.bso",
      "RESCUE-MINERVA-A");
  put(h, "bootloader/payloads/fusee.bin", "FUSEE-A");
  put(h, "bootloader/payloads/spare.bin", "OPTIONAL-PAYLOAD");
  put(h, "bootloader/res/background.bmp", "OPTIONAL-BACKGROUND");
  put(h, "bootloader/bootlogo.bmp", "OPTIONAL-LOGO");
  put(h, "atmosphere/config/system_settings.ini", "[settings]\nkey=value\n");
  put(h, "atmosphere/contents/0100000000001000/romfs/bad-theme", "THEME");
  put(h, "atmosphere/contents/0100000000001000/cheats/123.txt", "CHEATS");
  put(h, "Nintendo/Contents/registered/game.nca", "GAMES");
  put(h, "switch/port/data/game.dat", "PORT");
  put(h, "switchroot/android/data.img", "ANDROID");
  put(h, "linux/data.img", "LINUX");
  put(h, "exosphere.ini", "[exosphere]\ndebugmode=1\n");
  put(h, "emuMMC/emummc.ini", "[emummc]\nenabled=1\npath=emuMMC/RAW1\n");
}
static void test_paths(void) {
  CHECK(gc_safe_path("bootloader/hekate_ipl.ini"));
  CHECK(!gc_safe_path("../atmosphere/package3"));
  CHECK(!gc_safe_path("atmosphere/../Nintendo/a"));
  CHECK(!gc_safe_path("/atmosphere/package3"));
  CHECK(!gc_safe_path("atmosphere\\package3"));
  CHECK(!gc_safe_path("a//b"));
  CHECK(!gc_safe_path("a/./b"));
  CHECK(!gc_safe_path("a./b"));
  CHECK(!gc_safe_path("a/b "));
  CHECK(!gc_managed_path("Nintendo/Contents/a.nca", 3));
  CHECK(!gc_managed_path("atmosphere/contents/0100/cheats/a.txt", 3));
  CHECK(!gc_managed_path("switch/port/a.nro", 3));
  CHECK(!gc_managed_path("emuMMC/RAW1/eMMC/00", 3));
  CHECK(!gc_managed_path("atmosphere/automatic_backups/prod.keys", 3));
  CHECK(gc_managed_path("bootloader/res/bg.bmp", GC_OPT_VISUALS));
  CHECK(!gc_managed_path("bootloader/res/bg.bmp", 0));
}
static void test_sha(void) {
  char h[65];
  hashhex("", 0, h);
  CHECK(!strcmp(
      h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  hashhex("abc", 3, h);
  CHECK(!strcmp(
      h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}
int main(void) {
  host h;
  gc_context c;
  gc_result r;
  char temp[] = "/tmp/ninmagic-guardian-test-XXXXXX", path[1024], digest[66],
       *body, *original;
  size_t pos;
  memset(&h, 0, sizeof h);
  CHECK(mkdtemp(temp));
  snprintf(h.root, sizeof h.root, "%s", temp);
  h.space = 16ull * 1024 * 1024 * 1024;
  c = context(&h);
  test_paths();
  test_sha();
  fixture(&h);
  /* Source estimate is read-only and leaves optional/excluded data untouched.
   */
  r = gc_scan_source(&c, 0);
  must_ok(r);
  CHECK(r.info.files == 14);
  CHECK(!present(&h, GC_BACKUP_ROOT));
  must_ok(gc_create_backup(&c, 0));
  r = gc_inspect(&c, 1);
  must_ok(r);
  CHECK(r.info.present && r.info.verified && r.info.files == 14);
  CHECK(!present(&h, CUR "/data/Nintendo"));
  CHECK(!present(&h, CUR "/data/atmosphere/contents"));
  CHECK(!present(&h, CUR "/data/bootloader/payloads/spare.bin"));
  CHECK(!present(&h, CUR "/data/bootloader/res"));
  same(&h, CUR "/data/atmosphere/package3", "PACKAGE-A");
  /* A CID-less inspection is allowed; writes with a different CID are refused.
   */
  c.identity[0] = 0;
  r = gc_inspect(&c, 1);
  must_ok(r);
  CHECK(!strcmp(r.info.identity, "00112233445566778899aabbccddeeff"));
  r = gc_restore(&c);
  CHECK(!r.ok);
  c = context(&h);
  c.identity[0] = 'f';
  r = gc_restore(&c);
  CHECK(!r.ok && !present(&h, WORK));
  c = context(&h);
  /* Backup corruption must be caught before changing any managed target. */
  put(&h, "atmosphere/package3", "DAMAGED-LIVE");
  put(&h, CUR "/data/atmosphere/package3", "CORRUPTED");
  r = gc_restore(&c);
  CHECK(!r.ok);
  same(&h, "atmosphere/package3", "DAMAGED-LIVE");
  CHECK(!present(&h, WORK));
  put(&h, CUR "/data/atmosphere/package3", "PACKAGE-A");
  /* Simulated power loss after the old destination is durably retired. */
  h.fault = "restore.retired";
  h.fault_path = "atmosphere/package3";
  h.fired = 0;
  r = gc_restore(&c);
  CHECK(!r.ok && r.recovery_required && h.fired);
  CHECK(present(&h, WORK "/old/atmosphere/package3"));
  same(&h, WORK "/old/atmosphere/package3", "DAMAGED-LIVE");
  CHECK(!present(&h, "atmosphere/package3"));
  r = gc_create_backup(&c, 0);
  CHECK(!r.ok);
  h.fault = NULL;
  must_ok(gc_restore(&c));
  same(&h, "atmosphere/package3", "PACKAGE-A");
  CHECK(!present(&h, WORK));
  same(&h, "Nintendo/Contents/registered/game.nca", "GAMES");
  same(&h, "atmosphere/contents/0100000000001000/romfs/bad-theme", "THEME");
  same(&h, "switch/port/data/game.dat", "PORT");
  /* A new backup interrupted before promotion leaves the prior one selected. */
  put(&h, "atmosphere/package3", "PACKAGE-B");
  h.fault = "backup.staged";
  h.fault_path = NULL;
  h.fired = 0;
  r = gc_create_backup(&c, 0);
  CHECK(!r.ok && h.fired);
  same(&h, CUR "/data/atmosphere/package3", "PACKAGE-A");
  h.fault = NULL;
  /* A short write never destroys the last completed backup. */
  h.writes = 0;
  h.fail_write = 1;
  r = gc_create_backup(&c, 0);
  CHECK(!r.ok);
  same(&h, CUR "/data/atmosphere/package3", "PACKAGE-A");
  h.fail_write = 0;
  /* Power loss between the two directory renames is recoverable by the same
   * CID. */
  h.fault = "backup.previous";
  h.fired = 0;
  r = gc_create_backup(&c, 0);
  CHECK(!r.ok && r.recovery_required && h.fired);
  CHECK(!present(&h, CUR));
  c.identity[0] = 0;
  r = gc_inspect(&c, 1);
  must_ok(r);
  CHECK(r.info.present && r.info.verified);
  c = context(&h);
  h.fault = NULL;
  must_ok(gc_recover_backup(&c));
  same(&h, CUR "/data/atmosphere/package3", "PACKAGE-B");
  CHECK(!present(&h, GC_BACKUP_ROOT "/previous"));
  CHECK(!present(&h, GC_BACKUP_ROOT "/promotion.pending"));
  /* Optional selections copy only their explicit allowlisted assets. */
  must_ok(gc_create_backup(&c, GC_OPT_VISUALS | GC_OPT_PAYLOADS));
  CHECK(present(&h, CUR "/data/bootloader/payloads/spare.bin"));
  CHECK(present(&h, CUR "/data/bootloader/res/background.bmp"));
  CHECK(present(&h, CUR "/data/bootloader/bootlogo.bmp"));
  CHECK(present(&h, CUR "/data/bootloader/sys/res.pak"));
  CHECK(present(&h, CUR "/data/bootloader/sys/emummc.kipm"));
  {
    static const char *points[] = {"restore.before", "restore.retired",
                                   "restore.applied", "restore.complete"};
    unsigned t;
    for (t = 0; t < sizeof points / sizeof points[0]; t++) {
      put(&h, "atmosphere/package3", "NEWER-LIVE");
      put(&h, "bootloader/sys/res.pak", "BROKEN-RESOURCES");
      put(&h, "bootloader/sys/emummc.kipm", "BROKEN-KIP");
      h.fault = points[t];
      h.fault_path = t == 3 ? NULL : "atmosphere/package3";
      h.fired = 0;
      r = gc_restore(&c);
      CHECK(!r.ok && r.recovery_required && h.fired);
      h.fault = NULL;
      must_ok(gc_restore(&c));
      same(&h, "atmosphere/package3", "PACKAGE-B");
      same(&h, "bootloader/sys/res.pak", "RESOURCES-A");
      same(&h, "bootloader/sys/emummc.kipm", "EMUMMC-KIP-A");
    }
    /* Short writes while publishing a plan or copying its new data are
     * retryable. */
    for (t = 2; t <= 4; t += 2) {
      put(&h, "atmosphere/package3", "NEWER-LIVE");
      h.fail_write = (int)t;
      h.writes = 0;
      r = gc_restore(&c);
      CHECK(!r.ok && r.recovery_required);
      h.fail_write = 0;
      must_ok(gc_restore(&c));
      same(&h, "atmosphere/package3", "PACKAGE-B");
    }
  }
  h.space = 0;
  r = gc_create_backup(&c, 0);
  CHECK(!r.ok);
  h.space = 16ull * 1024 * 1024 * 1024;
  /* A symlink/special entry reported by the adapter must not be followed.
   * Emulation avoids requiring Windows' CreateSymbolicLink privilege. */
  put(&h, "bootloader/sys/unsafe.bin", "not a regular source");
  h.special_file = 1;
  r = gc_scan_source(&c, 0);
  CHECK(!r.ok);
  /* Boot add-on inventory stores only hashes, supports explicit subsets and
   * never moves game mods. Restoring quarantine does not require the backup. */
  {
    ga_result ar;
    ga_review review, subset, forged;
    const char *module = "010000000000100d";
    const char *flag = "atmosphere/contents/010000000000100d/flags/boot2.flag";
    const char *qflag = "config/NinMagicGuardian/quarantine/active/data/"
                        "010000000000100d/flags/boot2.flag";
    const char *theme = "atmosphere/contents/0100000000001000/romfs/bad-theme";
    const char *qtheme = "config/NinMagicGuardian/quarantine/active/data/"
                         "0100000000001000/romfs/bad-theme";
    char long_theme[512];
    unsigned k;
    snprintf(long_theme, sizeof long_theme,
             "atmosphere/contents/0100000000001000/romfs/%0180u", 0u);
    put(&h, long_theme, "LONG-PATH-THEME");
    ar = ga_capture(&c);
    CHECK(ar.ok);
    review = ga_inspect(&c);
    CHECK(review.ok && review.count == 0);
    put(&h, flag, "");
    put(&h, "atmosphere/contents/010000000000100d/exefs.nsp", "MODULE-A");
    put(&h, theme, "NEW-THEME");
    review = ga_inspect(&c);
    CHECK(review.ok && review.count == 2);
    subset = review;
    subset.count = 0;
    for (k = 0; k < review.count; k++)
      if (review.candidates[k].kind == GA_BOOT_MODULE)
        subset.candidates[subset.count++] = review.candidates[k];
    CHECK(subset.count == 1 && !strcmp(subset.candidates[0].title_id, module));
    forged = subset;
    strcpy(forged.candidates[0].title_id, "0100000000009999");
    ar = ga_quarantine(&c, &forged);
    CHECK(!ar.ok && present(&h, flag));
    c.identity[0] = 'f';
    ar = ga_quarantine(&c, &subset);
    CHECK(!ar.ok && present(&h, flag));
    c = context(&h);
    ar = ga_quarantine(&c, &subset);
    CHECK(ar.ok && ar.changed == 1 && !present(&h, flag) && present(&h, qflag));
    same(&h, theme, "NEW-THEME");
    same(&h, "atmosphere/contents/0100000000001000/cheats/123.txt", "CHEATS");
    r = gc_create_backup(&c, 0);
    CHECK(!r.ok);
    ar = ga_capture(&c);
    CHECK(!ar.ok);
    put(&h, qflag, "TAMPERED");
    ar = ga_quarantine(&c, &subset);
    CHECK(!ar.ok);
    ar = ga_restore_quarantine(&c);
    CHECK(!ar.ok && !present(&h, flag));
    put(&h, qflag, "");
    put(&h, flag, "NEW-USER-FLAG");
    ar = ga_restore_quarantine(&c);
    CHECK(!ar.ok);
    same(&h, flag, "NEW-USER-FLAG");
    CHECK(present(&h, qflag));
    full(&h, flag, path);
    CHECK(!unlink(path));
    CHECK(!hn(&h, CUR "/complete.sha256", CUR "/complete.saved"));
    ar = ga_restore_quarantine(&c);
    CHECK(ar.ok && present(&h, flag));
    CHECK(!hn(&h, CUR "/complete.saved", CUR "/complete.sha256"));
    /* The old reviewed snapshot cannot silently include later binary changes.
     */
    ar = ga_capture(&c);
    CHECK(ar.ok);
    put(&h, "atmosphere/contents/010000000000100d/exefs.nsp", "MODULE-B");
    review = ga_inspect(&c);
    CHECK(review.ok && review.count == 1 && !review.candidates[0].newly_added);
    put(&h, "atmosphere/contents/010000000000100d/exefs.nsp", "MODULE-C");
    ar = ga_quarantine(&c, &review);
    CHECK(!ar.ok && present(&h, flag));
    put(&h, theme, "THEME-C");
    review = ga_inspect(&c);
    CHECK(review.ok && review.count == 2);
    h.fault = "addons.quarantined";
    h.fault_path = "atmosphere/contents/0100000000001000/romfs";
    h.fired = 0;
    ar = ga_quarantine(&c, &review);
    CHECK(!ar.ok && ar.recovery_required && h.fired);
    h.fault = NULL;
    ar = ga_quarantine(&c, &review);
    CHECK(ar.ok && !present(&h, flag) && present(&h, qtheme));
    put(&h, qtheme, "TAMPERED-THEME");
    ar = ga_restore_quarantine(&c);
    CHECK(!ar.ok && present(&h, qtheme));
    put(&h, qtheme, "THEME-C");
    ar = ga_restore_quarantine(&c);
    CHECK(ar.ok && present(&h, flag));
    same(&h, theme, "THEME-C");
    same(&h, long_theme, "LONG-PATH-THEME");
    /* A directory in place of the inventory must remain where it was. */
    CHECK(!hn(&h, "config/NinMagicGuardian/addons/inventory.txt",
              "config/NinMagicGuardian/addons/inventory.saved"));
    CHECK(!hm(&h, "config/NinMagicGuardian/addons/inventory.txt"));
    put(&h, "config/NinMagicGuardian/addons/inventory.txt/keep", "KEEP");
    ar = ga_capture(&c);
    CHECK(!ar.ok);
    same(&h, "config/NinMagicGuardian/addons/inventory.txt/keep", "KEEP");
    full(&h, "config/NinMagicGuardian/addons/inventory.txt/keep", path);
    CHECK(!unlink(path));
    CHECK(!hd(&h, "config/NinMagicGuardian/addons/inventory.txt", GC_DIR));
    CHECK(!hn(&h, "config/NinMagicGuardian/addons/inventory.saved",
              "config/NinMagicGuardian/addons/inventory.txt"));
  }
  h.special_file = 0;
  full(&h, "bootloader/sys/unsafe.bin", path);
  CHECK(!unlink(path));
  /* Even a recomputed public manifest checksum cannot permit excluded paths. */
  original = get(&h, CUR "/manifest.txt");
  body = malloc(strlen(original) + 1);
  CHECK(body);
  strcpy(body, original);
  pos = (size_t)(strstr(body, "atmosphere/package3") - body);
  CHECK(pos < strlen(body));
  memcpy(body + pos, "Nintendo/1234567890", 18);
  put(&h, CUR "/manifest.txt", body);
  hashhex(body, strlen(body), digest);
  digest[64] = '\n';
  digest[65] = 0;
  put(&h, CUR "/complete.sha256", digest);
  r = gc_inspect(&c, 1);
  CHECK(!r.ok);
  r = gc_restore(&c);
  CHECK(!r.ok);
  same(&h, "Nintendo/Contents/registered/game.nca", "GAMES");
  put(&h, CUR "/manifest.txt", original);
  hashhex(original, strlen(original), digest);
  digest[64] = '\n';
  digest[65] = 0;
  put(&h, CUR "/complete.sha256", digest);
  free(original);
  free(body);
  must_ok(gc_inspect(&c, 1));
  /* HOS dependency omissions are reported; tool/Linux payloads do not pull in
   * their data. */
  put(&h, "bootloader/ini/test.ini", "[CFW]\nkip1=atmosphere/kips/*\n");
  put(&h, "atmosphere/kips/example.kip", "KIP");
  must_ok(gc_scan_source(&c, 0));
  put(&h, "bootloader/ini/test.ini",
      "[CFW]\nfss0=Nintendo/Contents/registered/game.nca\n");
  r = gc_scan_source(&c, 0);
  CHECK(!r.ok);
  printf("PASS: core SHA-256, source exclusions, CID, corruption, write "
         "failure, backup promotion, interrupted restore and manifest "
         "allowlist; add-on inventory, explicit subset, stale/forged review, "
         "CID, quarantine interruption, tamper and restore collisions\nTest "
         "data retained for inspection: %s\n",
         h.root);
  return 0;
}
