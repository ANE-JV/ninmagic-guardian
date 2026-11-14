#define main guardian_core_test_entry_unused
#include "core_test.c"
#undef main
#include "guardian_recovery.h"

#define Q "config/NinMagicGuardian/quarantine/active"
#define PLAN Q "/plan.txt"
#define INTENT Q "/discard.txt"
#define DONE Q "/discard.done"
#define MOD1 "010000000000100d"
#define MOD2 "010000000000100e"
#define HOME "0100000000001000"
#define F1 "atmosphere/contents/" MOD1 "/flags/boot2.flag"
#define F2 "atmosphere/contents/" MOD2 "/flags/boot2.flag"
#define THEME "atmosphere/contents/" HOME "/romfs"
#define QTHEME Q "/data/" HOME "/romfs"
#define QF1 Q "/data/" MOD1 "/flags/boot2.flag"
#define QF2 Q "/data/" MOD2 "/flags/boot2.flag"

static unsigned scenarios;
static const char *remove_error, *special_path;
static int remove_fired;
static int guarded_remove(void *u, const char *p, gc_type type) {
  if (remove_error && !remove_fired && !strcmp(remove_error, p)) {
    remove_fired = 1;
    return -1;
  }
  return hd(u, p, type);
}
static int guarded_stat(void *u, const char *p, gc_stat *s) {
  int r = hs(u, p, s);
  if (r == 1 && special_path && !strcmp(special_path, p))
    s->type = GC_OTHER;
  return r;
}
static void remove_file(host *h, const char *p) {
  char b[1024];
  full(h, p, b);
  CHECK(!unlink(b));
}
static void make_dir(host *h, const char *p) {
  char b[1024];
  dirs(h, p);
  full(h, p, b);
  CHECK(!mkdir(b, 0700));
}
static void rm_dir(host *h, const char *p) {
  char b[1024];
  full(h, p, b);
  CHECK(!rmdir(b));
}
static void must_ga(ga_result r) {
  if (!r.ok) {
    fprintf(stderr, "Unexpected add-on error: %s\n", r.message);
    exit(1);
  }
}
static void setup(host *h, gc_context *c, ga_review *r) {
  char tmp[] = "/tmp/ninmagic-guardian-discard-XXXXXX";
  memset(h, 0, sizeof *h);
  CHECK(mkdtemp(tmp));
  snprintf(h->root, sizeof h->root, "%s", tmp);
  h->space = 16ull * 1024 * 1024 * 1024;
  *c = context(h);
  fixture(h);
  CHECK(!gc_scan_source(c, 4).ok);
  put(h, F1, "OLD-FLAG");
  put(h, "atmosphere/contents/" MOD1 "/exefs.nsp", "OLD-MODULE");
  must_ok(gc_create_backup(c, 0));
  must_ga(ga_capture(c));
  put(h, F1, "CHANGED-FLAG");
  put(h, "atmosphere/contents/" MOD1 "/exefs.nsp", "CHANGED-MODULE");
  put(h, F2, "");
  put(h, "atmosphere/contents/" MOD2 "/exefs.nsp", "NEW-MODULE");
  put(h, THEME "/bad-theme", "CHANGED-THEME");
  put(h, THEME "/sub/a", "THEME-SUB-A");
  put(h, THEME "/sub/deeper/b", "THEME-SUB-B");
  make_dir(h, THEME "/empty");
  put(h, "switch/new-tool.nro", "NEW-NRO-OPAQUE");
  put(h, "switch/new-game.nro", "NEW-PORT-OPAQUE");
  put(h, "atmosphere/contents/0100123456789000/romfs/new-game-mod", "GAME-MOD");
  put(h, "atmosphere/package3", "DAMAGED-CORE");
  *r = ga_inspect(c);
  CHECK(r->ok && r->count == 3 && !r->quarantine_pending);
  ++scenarios;
}
static void untouched(host *h) {
  same(h, "switch/new-tool.nro", "NEW-NRO-OPAQUE");
  same(h, "switch/new-game.nro", "NEW-PORT-OPAQUE");
  same(h, "switch/port/data/game.dat", "PORT");
  same(h, "Nintendo/Contents/registered/game.nca", "GAMES");
  same(h, "atmosphere/contents/0100123456789000/romfs/new-game-mod",
       "GAME-MOD");
  same(h, "atmosphere/contents/" MOD1 "/exefs.nsp", "CHANGED-MODULE");
  same(h, "atmosphere/contents/" MOD2 "/exefs.nsp", "NEW-MODULE");
  same(h, "atmosphere/contents/" HOME "/cheats/123.txt", "CHEATS");
  same(h, "config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin",
       "RESCUE-A");
}
static void completed(host *h, gc_context *c) {
  ga_review p;
  CHECK(!present(h, F1) && !present(h, F2) && !present(h, THEME));
  CHECK(!present(h, QF1) && !present(h, QF2) && !present(h, QTHEME));
  CHECK(!present(h, PLAN) && !present(h, INTENT) && present(h, DONE));
  CHECK(!ga_pending_review(c, &p) && p.ok && !p.quarantine_pending &&
        !p.discard_pending);
  same(h, "atmosphere/package3", "PACKAGE-A");
  untouched(h);
}
static void test_success_and_new_baseline(void) {
  host h;
  gc_context c;
  ga_review r;
  gr_result result;
  setup(&h, &c, &r);
  result = gr_restore_mode(&c, &r, 1);
  if (!result.result.ok)
    fprintf(stderr, "Permanent recovery: %s\n", result.result.message);
  CHECK(result.result.ok && !result.addons_quarantined &&
        result.addons_changed == 3);
  completed(&h, &c);
  CHECK(!ga_restore_quarantine(&c).ok);
  must_ga(ga_discard_quarantine(
      &c)); /* already committed: no deletion is repeated */
  must_ok(gc_create_backup(&c, 0));
  must_ga(ga_capture(&c));
  put(&h, F2, "ANOTHER-FLAG");
  r = ga_inspect(&c);
  CHECK(r.ok && r.count == 1);
  must_ga(ga_quarantine(&c, &r)); /* clears only the old metadata tombstone */
  CHECK(!present(&h, DONE));
  must_ga(ga_restore_quarantine(&c));
  same(&h, F2, "ANOTHER-FLAG");
}
static void test_selection_and_order(void) {
  host h;
  gc_context c;
  ga_review r, p, subset;
  gr_result result;
  setup(&h, &c, &r);
  result = gr_restore_mode(&c, NULL, 1);
  CHECK(!result.result.ok && !present(&h, PLAN));
  same(&h, "atmosphere/package3", "DAMAGED-CORE");
  must_ga(ga_quarantine(&c, &r));
  CHECK(!ga_pending_review(&c, &p) && p.count == 3 && !p.discard_pending);
  subset = p;
  subset.count = 1;
  result = gr_restore_mode(&c, &subset, 1);
  CHECK(!result.result.ok && !present(&h, INTENT));
  same(&h, "atmosphere/package3", "DAMAGED-CORE");
  subset = p;
  subset.candidates[1] = subset.candidates[0];
  result = gr_restore_mode(&c, &subset, 1);
  CHECK(!result.result.ok && !present(&h, INTENT));
  h.fault = "restore.retired";
  h.fault_path = "atmosphere/package3";
  result = gr_restore_mode(&c, &p, 1);
  CHECK(!result.result.ok && result.result.recovery_required && h.fired);
  CHECK(!present(
      &h, INTENT)); /* no permanent deletion until core restore succeeds */
  CHECK(present(&h, QF1) && present(&h, QTHEME "/sub/a"));
  h.fault = NULL;
  result = gr_restore_mode(&c, &p, 1);
  CHECK(result.result.ok);
  completed(&h, &c);
}
static void test_cuts(void) {
  static const struct {
    const char *point, *path;
  } cuts[] = {{"addons.quarantined", F1},
              {"addons.discard.intent", INTENT},
              {"addons.discard.file", QTHEME "/bad-theme"},
              {"addons.discard.file", QTHEME "/sub/a"},
              {"addons.discard.file", QTHEME "/sub/deeper/b"},
              {"addons.discard.file", QF1},
              {"addons.discard.file", QF2},
              {"addons.discard.directory", QTHEME "/sub/deeper"},
              {"addons.discard.directory", QTHEME "/sub"},
              {"addons.discard.directory", QTHEME "/empty"},
              {"addons.discard.directory", QTHEME},
              {"addons.discard.done", DONE},
              {"addons.discard.intent_removed", INTENT},
              {"addons.discard.plan_removed", PLAN}};
  unsigned i;
  for (i = 0; i < sizeof cuts / sizeof cuts[0]; i++) {
    host h;
    gc_context c, ro;
    ga_review r, p;
    gr_result result;
    setup(&h, &c, &r);
    h.fault = cuts[i].point;
    h.fault_path = cuts[i].path;
    result = gr_restore_mode(&c, &r, 1);
    CHECK(!result.result.ok && h.fired);
    untouched(&h);
    h.fault = NULL;
    if (present(&h, PLAN)) {
      ro = c;
      ro.fs.open = readonly_open;
      ro.fs.write = NULL;
      ro.fs.file_sync = NULL;
      ro.fs.mkdir = NULL;
      ro.fs.rename = NULL;
      ro.fs.remove = NULL;
      ro.fs.sync = NULL;
      ro.fs.free_space = NULL;
      CHECK(!ga_pending_review(&ro, &p) && p.ok && p.count == 3);
      if (strcmp(cuts[i].point, "addons.quarantined")) {
        CHECK(p.discard_pending);
        CHECK(!ga_restore_quarantine(&c).ok);
        CHECK(!ga_quarantine(&c, &p).ok);
        result = gr_restore_mode(&c, NULL, 0);
        CHECK(!result.result.ok);
        result = gr_restore_mode(&c, &p, 0);
        CHECK(!result.result.ok);
      } else
        CHECK(!p.discard_pending);
      result = gr_restore_mode(&c, &p, 1);
      CHECK(result.result.ok);
    } else {
      CHECK(!strcmp(cuts[i].point, "addons.discard.plan_removed"));
      must_ga(ga_discard_quarantine(&c));
    }
    completed(&h, &c);
  }
}
static void begin_intent(host *h, gc_context *c, ga_review *r) {
  gr_result g;
  setup(h, c, r);
  h->fault = "addons.discard.intent";
  g = gr_restore_mode(c, r, 1);
  CHECK(!g.result.ok && h->fired && present(h, INTENT));
  h->fault = NULL;
  CHECK(!ga_pending_review(c, r) && r->discard_pending);
}
static void test_tamper(void) {
  host h;
  gc_context c;
  ga_review r, p;
  char *saved, *body, b[1024], sha[65];
  ga_result a;
  begin_intent(&h, &c, &r);
  saved = get(&h, QTHEME "/sub/a");
  put(&h, QTHEME "/sub/a", "REPLACED-DATA");
  a = ga_discard_quarantine(&c);
  CHECK(!a.ok && present(&h, QF1) && present(&h, QTHEME "/bad-theme"));
  same(&h, QTHEME "/sub/a", "REPLACED-DATA");
  put(&h, QTHEME "/sub/a", saved);
  free(saved);
  put(&h, QTHEME "/unconfirmed", "UNCONFIRMED");
  CHECK(!ga_discard_quarantine(&c).ok && present(&h, QF1));
  same(&h, QTHEME "/unconfirmed", "UNCONFIRMED");
  remove_file(&h, QTHEME "/unconfirmed");
  make_dir(&h, QTHEME "/unconfirmed-empty");
  CHECK(!ga_discard_quarantine(&c).ok && present(&h, QF1));
  CHECK(present(&h, QTHEME "/unconfirmed-empty"));
  rm_dir(&h, QTHEME "/unconfirmed-empty");
  c.fs.stat = guarded_stat;
  special_path = QTHEME "/sub/a";
  CHECK(!ga_discard_quarantine(&c).ok && present(&h, QF1));
  special_path = NULL;
  put(&h, F1, "NEW-ACTIVE-FLAG");
  CHECK(!ga_discard_quarantine(&c).ok && present(&h, QF1));
  same(&h, F1, "NEW-ACTIVE-FLAG");
  remove_file(&h, F1);
  c.identity[0] = 'f';
  CHECK(ga_pending_review(&c, &p) < 0 && !p.ok && p.discard_pending);
  CHECK(!ga_discard_quarantine(&c).ok && present(&h, QF1));
  c = context(&h);
  h.stat_error_path = INTENT;
  CHECK(ga_pending_review(&c, &p) < 0 && !p.ok && p.quarantine_pending &&
        p.discard_pending);
  CHECK(!ga_discard_quarantine(&c).ok && present(&h, QF1));
  h.stat_error_path = NULL;
  saved = get(&h, INTENT);
  put(&h, INTENT, "BROKEN");
  CHECK(!ga_discard_quarantine(&c).ok && present(&h, QF1));
  CHECK(ga_pending_review(&c, &p) < 0 && p.discard_pending);
  put(&h, INTENT, saved);
  free(saved);
  /* A correctly sealed but borrowed path is still rejected by the plan-derived
   * allowlist. */
  body = get(&h, INTENT);
  saved = strdup(body);
  CHECK(saved);
  {
    char *path = strstr(body, "data/" MOD1 "/flags/boot2.flag");
    char *seal;
    CHECK(path);
    memcpy(path, "data/0100000000009999/flags/boot2.flag",
           strlen("data/" MOD1 "/flags/boot2.flag"));
    seal = strstr(body, "sha256=");
    CHECK(seal);
    hashhex(body, (size_t)(seal - body), sha);
    memcpy(seal + 7, sha, 64);
  }
  put(&h, INTENT, body);
  CHECK(!ga_discard_quarantine(&c).ok && present(&h, QF1));
  put(&h, INTENT, saved);
  free(saved);
  free(body);
  /* Partial authorized deletion is resumable without the original whole-theme
   * hash. */
  h.fault = "addons.discard.file";
  h.fault_path = QTHEME "/sub/a";
  h.fired = 0;
  CHECK(!ga_discard_quarantine(&c).ok && h.fired);
  CHECK(!present(&h, QTHEME "/sub/a"));
  CHECK(!ga_pending_review(&c, &p) && p.count == 3 && p.discard_pending);
  put(&h, QTHEME "/sub/new-after-cut", "NEW-AFTER-CUT");
  CHECK(!ga_discard_quarantine(&c).ok && present(&h, QF1));
  remove_file(&h, QTHEME "/sub/new-after-cut");
  h.fault = NULL;
  /* The sealed deletion intent is sufficient for retries; a missing inventory
   * must not orphan a plan after data has already been deleted. */
  full(&h, "config/NinMagicGuardian/addons/inventory.txt", b);
  CHECK(!unlink(b));
  must_ga(ga_discard_quarantine(&c));
  completed(&h, &c);
}
static void test_remove_errors_and_metadata(void) {
  static const char *paths[] = {QTHEME "/bad-theme",
                                QTHEME "/sub/a",
                                QF1,
                                QF2,
                                QTHEME "/sub/deeper",
                                QTHEME "/sub",
                                QTHEME "/empty",
                                QTHEME,
                                INTENT,
                                PLAN};
  unsigned i;
  for (i = 0; i < sizeof paths / sizeof paths[0]; i++) {
    host h;
    gc_context c;
    ga_review r;
    gr_result g;
    setup(&h, &c, &r);
    c.fs.remove = guarded_remove;
    remove_error = paths[i];
    remove_fired = 0;
    g = gr_restore_mode(&c, &r, 1);
    CHECK(!g.result.ok && g.result.recovery_required && remove_fired);
    CHECK(present(&h, PLAN));
    remove_error = NULL;
    CHECK(!ga_pending_review(&c, &r) && r.count == 3 && r.discard_pending);
    g = gr_restore_mode(&c, &r, 1);
    CHECK(g.result.ok);
    completed(&h, &c);
  }
}
static void test_preconditions(void) {
  host h;
  gc_context c;
  ga_review r, p;
  setup(&h, &c, &r);
  must_ga(ga_quarantine(&c, &r));
  remove_file(&h, "config/NinMagicGuardian/addons/inventory.txt");
  CHECK(!ga_discard_quarantine(&c).ok && !present(&h, INTENT));
  CHECK(present(&h, QF1));
  CHECK(!ga_pending_review(&c, &p) && !p.discard_pending && p.count == 3);
  must_ga(ga_restore_quarantine(&c));
  same(&h, F1, "CHANGED-FLAG");
  untouched(&h);
}
static void test_subset_and_orphan(void) {
  host h;
  gc_context c;
  ga_review r, p, subset;
  gr_result g;
  char *saved, *before, *after;
  unsigned i;
  setup(&h, &c, &r);
  subset = r;
  subset.count = 1;
  for (i = 0; i < r.count; i++)
    if (r.candidates[i].kind == GA_BOOT_MODULE &&
        !strcmp(r.candidates[i].title_id, MOD1))
      subset.candidates[0] = r.candidates[i];
  CHECK(!strcmp(subset.candidates[0].title_id, MOD1));
  g = gr_restore_mode(&c, &subset, 1);
  CHECK(g.result.ok && g.addons_changed == 1 && !present(&h, F1));
  CHECK(present(&h, F2) && present(&h, THEME "/bad-theme"));
  same(&h, THEME "/sub/a", "THEME-SUB-A");
  untouched(&h);
  begin_intent(&h, &c, &r);
  before = get(&h, CUR "/manifest.txt");
  saved = get(&h, PLAN);
  remove_file(&h, PLAN); /* impossible normal commit order: fail closed */
  CHECK(!gc_create_backup(&c, 0).ok);
  CHECK(!ga_capture(&c).ok);
  after = get(&h, CUR "/manifest.txt");
  CHECK(!strcmp(before, after));
  free(before);
  free(after);
  CHECK(ga_pending_review(&c, &p) < 0 && !p.ok && p.discard_pending &&
        p.quarantine_pending);
  p = ga_inspect(&c);
  CHECK(!p.ok && p.discard_pending && p.quarantine_pending);
  g = gr_restore_mode(&c, NULL, 0);
  CHECK(!g.result.ok && g.result.recovery_required);
  CHECK(!ga_discard_quarantine(&c).ok && present(&h, QF1));
  put(&h, PLAN, saved);
  free(saved);
  CHECK(!ga_pending_review(&c, &p) && p.count == 3 && p.discard_pending);
  g = gr_restore_mode(&c, &p, 1);
  CHECK(g.result.ok);
  completed(&h, &c);
}
int main(void) {
  test_success_and_new_baseline();
  test_selection_and_order();
  test_cuts();
  test_tamper();
  test_remove_errors_and_metadata();
  test_preconditions();
  test_subset_and_orphan();
  printf("PASS: %u purge/recovery scenarios; complete confirmed plan, durable "
         "exact file+directory intent, all commit cuts/restarts, "
         "tamper/CID/borrowed paths, deletion failures, no reactivation after "
         "intent, new baseline and switch/game preservation\n",
         scenarios);
  return 0;
}
