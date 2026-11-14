#define main guardian_core_test_entry_unused
#include "core_test.c"
#undef main
#include "guardian_recovery.h"

static void hash_path(host *h, const char *p, char out[65]) {
  char b[1024];
  FILE *f;
  gc_sha256 sha;
  unsigned char bytes[4096], digest[32];
  size_t got;
  static const char *hexchars = "0123456789abcdef";
  unsigned i;
  full(h, p, b);
  f = fopen(b, "rb");
  CHECK(f);
  gc_sha256_init(&sha);
  while ((got = fread(bytes, 1, sizeof bytes, f)))
    gc_sha256_update(&sha, bytes, got);
  CHECK(!ferror(f));
  CHECK(!fclose(f));
  gc_sha256_final(&sha, digest);
  for (i = 0; i < 32; i++) {
    out[i * 2] = hexchars[digest[i] >> 4];
    out[i * 2 + 1] = hexchars[digest[i] & 15];
  }
  out[64] = 0;
}
static void must_gr(gr_result r) {
  if (!r.result.ok) {
    fprintf(stderr, "Unexpected integrated recovery error: %s\n",
            r.result.message);
    exit(1);
  }
}
int main(void) {
  host h;
  gc_context c;
  ga_review review, fresh;
  gr_result r;
  ga_result ar;
  char temp[] = "/tmp/ninmagic-guardian-integrated-test-XXXXXX", path[1024],
       new_tool_hash[65], new_game_hash[65], hash[65];
  const char *tool = "switch/JKSV/JKSV.nro",
             *flag = "atmosphere/contents/010000000000100d/flags/boot2.flag",
             *theme = "atmosphere/contents/0100000000001000/romfs/bad-theme";
  memset(&h, 0, sizeof h);
  CHECK(mkdtemp(temp));
  snprintf(h.root, sizeof h.root, "%s", temp);
  h.space = 16ull * 1024 * 1024 * 1024;
  c = context(&h);
  fixture(&h);
  put(&h, tool, "ORIGINAL-UTILITY-NRO");
  must_ok(gc_create_backup(&c, 0));
  ar = ga_capture(&c);
  CHECK(ar.ok);
  put(&h, "switch/AddedLater/new-tool.nro", "NEW-UTILITY-NRO");
  put(&h, "switch/AddedLater/new-game.nro", "NEW-GAME-PORT-NRO");
  hash_path(&h, "switch/AddedLater/new-tool.nro", new_tool_hash);
  hash_path(&h, "switch/AddedLater/new-game.nro", new_game_hash);
  put(&h, flag, "");
  put(&h, "atmosphere/contents/010000000000100d/exefs.nsp", "MODULE-A");
  put(&h, theme, "CHANGED-THEME");
  put(&h, "atmosphere/package3", "DAMAGED");
  full(&h, tool, path);
  CHECK(!unlink(path));
  review = ga_inspect(&c);
  CHECK(review.ok && review.count == 2);
  /* Backup/identity inspection is read-only. switch is not inspected at all. */
  {
    gc_context readonly = c;
    readonly.fs.open = readonly_open;
    readonly.fs.write = NULL;
    readonly.fs.file_sync = NULL;
    readonly.fs.mkdir = NULL;
    readonly.fs.rename = NULL;
    readonly.fs.remove = NULL;
    readonly.fs.sync = NULL;
    readonly.fs.free_space = NULL;
    must_ok(gc_restore_preflight(&readonly));
  }
  r = gr_restore(&c, &review);
  must_gr(r);
  CHECK(r.addons_quarantined && r.addons_changed == 2);
  CHECK(!present(&h, flag) &&
        !present(&h, "atmosphere/contents/0100000000001000/romfs"));
  same(&h, "atmosphere/package3", "PACKAGE-A");
  CHECK(!present(&h,
                 tool)); /* A deleted NRO is intentionally outside recovery. */
  hash_path(&h, "switch/AddedLater/new-tool.nro", hash);
  CHECK(!strcmp(hash, new_tool_hash));
  hash_path(&h, "switch/AddedLater/new-game.nro", hash);
  CHECK(!strcmp(hash, new_game_hash));
  same(&h, "switch/port/data/game.dat", "PORT");
  r = gr_restore(&c, NULL);
  must_gr(r);
  CHECK(r.addons_quarantined &&
        strstr(r.result.message, "plan de extras anterior"));
  put(&h, CUR "/data/atmosphere/package3", "BAD-SNAPSHOT");
  r = gr_restore(&c, &review);
  CHECK(!r.result.ok && r.addons_quarantined && r.result.recovery_required &&
        strstr(r.result.message, "plan de extras guardado"));
  put(&h, CUR "/data/atmosphere/package3", "PACKAGE-A");
  ar = ga_restore_quarantine(&c);
  CHECK(ar.ok);
  put(&h, "atmosphere/package3", "DAMAGED-STILL");
  put(&h, tool, "CORRUPT-NRO");
  review = ga_inspect(&c);
  CHECK(review.ok && review.count == 2);
  put(&h, "atmosphere/contents/010000000000100d/exefs.nsp", "MODULE-B");
  r = gr_restore(&c, &review);
  CHECK(!r.result.ok &&
        !present(&h, "config/NinMagicGuardian/quarantine/active/plan.txt"));
  same(&h, "atmosphere/package3", "DAMAGED-STILL");
  same(&h, tool, "CORRUPT-NRO");
  CHECK(present(&h, flag));
  review = ga_inspect(&c);
  c.identity[0] = 'f';
  r = gr_restore(&c, &review);
  CHECK(!r.result.ok && present(&h, flag));
  same(&h, "atmosphere/package3", "DAMAGED-STILL");
  c = context(&h);
  h.stat_error_path = "config/NinMagicGuardian/quarantine/active/plan.txt";
  r = gr_restore(&c, &review);
  CHECK(!r.result.ok && r.result.recovery_required);
  same(&h, "atmosphere/package3", "DAMAGED-STILL");
  h.stat_error_path = NULL;
  h.fault = "addons.quarantined";
  h.fault_path = flag;
  h.fired = 0;
  r = gr_restore(&c, &review);
  CHECK(!r.result.ok && r.result.recovery_required && r.addons_quarantined &&
        h.fired);
  same(&h, "atmosphere/package3", "DAMAGED-STILL");
  same(&h, tool, "CORRUPT-NRO");
  CHECK(!present(&h, flag));
  h.fault = NULL;
  /* A different selection cannot be silently merged into the already confirmed
   * plan. */
  put(&h, "atmosphere/contents/010000000000100e/flags/boot2.flag", "");
  put(&h, "atmosphere/contents/010000000000100e/exefs.nsp", "ANOTHER-MODULE");
  fresh = ga_inspect(&c);
  CHECK(fresh.quarantine_pending);
  r = gr_restore(&c, &fresh);
  CHECK(!r.result.ok && r.result.recovery_required);
  same(&h, "atmosphere/package3", "DAMAGED-STILL");
  CHECK(present(&h, "atmosphere/contents/010000000000100e/flags/boot2.flag"));
  full(&h, "atmosphere/contents/010000000000100e/flags/boot2.flag", path);
  CHECK(!unlink(path));
  r = gr_restore(&c, &review);
  must_gr(r);
  same(&h, "atmosphere/package3", "PACKAGE-A");
  CHECK(!present(&h, flag) &&
        !present(&h, "atmosphere/contents/0100000000001000/romfs"));
  ar = ga_restore_quarantine(&c);
  CHECK(ar.ok);
  put(&h, "atmosphere/package3", "RESTORE-INTERRUPTED");
  put(&h, tool, "CORRUPT-NRO");
  review = ga_inspect(&c);
  CHECK(review.ok && review.count == 2);
  h.fault = "restore.retired";
  h.fault_path = "atmosphere/package3";
  h.fired = 0;
  r = gr_restore(&c, &review);
  CHECK(!r.result.ok && r.result.recovery_required && r.addons_quarantined &&
        h.fired);
  CHECK(!present(&h, flag) && !present(&h, "atmosphere/package3") &&
        present(&h, WORK "/pending.txt"));
  h.fault = NULL;
  fresh = ga_inspect(&c);
  CHECK(fresh.quarantine_pending && fresh.count == 0);
  r = gr_restore(&c, &fresh);
  must_gr(r);
  same(&h, "atmosphere/package3", "PACKAGE-A");
  CHECK(!present(&h, WORK));
  ar = ga_restore_quarantine(&c);
  CHECK(ar.ok);
  full(&h, "config/NinMagicGuardian/addons/inventory.txt", path);
  CHECK(!unlink(path));
  fresh = ga_inspect(&c);
  CHECK(!fresh.ok);
  put(&h, "atmosphere/package3", "FILES-ONLY-DAMAGE");
  r = gr_restore(&c, NULL);
  must_gr(r);
  CHECK(strstr(r.result.message, "no se apartaron"));
  CHECK(present(&h, flag) && present(&h, theme));
  /* Full backup validation blocks quarantine even if the review itself was
   * valid. */
  put(&h, CUR "/data/atmosphere/package3", "BAD-SNAPSHOT");
  put(&h, "atmosphere/package3", "NO-WRITES");
  r = gr_restore(&c, &review);
  CHECK(!r.result.ok);
  same(&h, "atmosphere/package3", "NO-WRITES");
  CHECK(present(&h, flag) &&
        !present(&h, "config/NinMagicGuardian/quarantine/active/plan.txt"));
  hash_path(&h, "switch/AddedLater/new-tool.nro", hash);
  CHECK(!strcmp(hash, new_tool_hash));
  hash_path(&h, "switch/AddedLater/new-game.nro", hash);
  CHECK(!strcmp(hash, new_game_hash));
  same(&h, "Nintendo/Contents/registered/game.nca", "GAMES");
  puts("PASS: integrated preflight, confirmed boot-module/HOME quarantine "
       "before system restore, stale review/identity/no-writes, "
       "quarantine+restore interruption/resume, pending-plan coverage, "
       "explicit files-only and new game/tool/data preservation");
  return 0;
}
