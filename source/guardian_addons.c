#include "guardian_addons.h"
#include "guardian_sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AROOT "config/NinMagicGuardian/addons"
#define INVENTORY AROOT "/inventory.txt"
#define QROOT "config/NinMagicGuardian/quarantine/active"
#define QPLAN QROOT "/plan.txt"
#define THEME_ID "0100000000001000"
#define A_MAX 256
#define A_FILE_LIMIT (128ull * 1024 * 1024)
#define A_READ_LIMIT (256ull * 1024 * 1024)
typedef struct {
  gc_context *c;
  char error[512];
  unsigned char *buffer;
  uint64_t read;
} ao;
typedef struct {
  unsigned count;
  ga_candidate entries[A_MAX];
  char backup[65], identity[33];
} inventory;
typedef struct {
  char path[512];
  uint64_t size;
} tree_file;
typedef struct {
  ao *o;
  tree_file *files;
  unsigned count, depth;
  uint64_t bytes;
  char root[512];
} tree;
static int af(ao *o, const char *s) {
  if (!o->error[0])
    snprintf(o->error, sizeof o->error, "%s", s);
  return -1;
}
static int ap(ao *o, const char *s, const char *p) {
  if (!o->error[0])
    snprintf(o->error, sizeof o->error, "%s: %.300s", s, p);
  return -1;
}
static int ai(ao *o, gc_context *c) {
  memset(o, 0, sizeof *o);
  o->c = c;
  if (!c || !c->fs.stat || !c->fs.open || !c->fs.read || !c->fs.close ||
      !c->fs.list)
    return af(o, "No se puede leer el inventario de complementos.");
  o->buffer = malloc(65536);
  return o->buffer ? 0
                   : af(o, "Memoria insuficiente para revisar complementos.");
}
static int as(ao *o, const char *p, gc_stat *s) {
  char b[512];
  size_t i, n = strlen(p);
  int r;
  if (n >= sizeof b)
    return af(o, "Ruta interna demasiado larga.");
  memcpy(b, p, n + 1);
  for (i = 0; i < n; i++)
    if (b[i] == '/') {
      b[i] = 0;
      r = o->c->fs.stat(o->c->fs.user, b, s);
      b[i] = '/';
      if (r < 0)
        return ap(o, "No se pudo comprobar la carpeta", p);
      if (!r)
        return 0;
      if (s->type != GC_DIR)
        return ap(o, "No se permiten enlaces ni carpetas especiales", p);
    }
  return o->c->fs.stat(o->c->fs.user, p, s);
}
static int awrite_ready(ao *o) {
  gc_fs *f = &o->c->fs;
  return f->write && f->file_sync && f->mkdir && f->rename && f->remove &&
                 f->sync
             ? 0
             : af(o, "No se pueden confirmar cambios en esta microSD.");
}
static int async(ao *o) {
  return o->c->fs.sync(o->c->fs.user)
             ? af(o, "No se pudieron confirmar los cambios en la microSD.")
             : 0;
}
static int acp(ao *o, const char *point, const char *path) {
  return o->c->fs.checkpoint && o->c->fs.checkpoint(o->c->fs.user, point, path)
             ? af(o, "Aislamiento interrumpido. Se conserva el plan para "
                     "continuar.")
             : 0;
}
static int amkdir(ao *o, const char *p) {
  char b[512];
  size_t i, n = strlen(p);
  gc_stat s;
  int r;
  if (n >= sizeof b)
    return af(o, "Ruta interna demasiado larga.");
  memcpy(b, p, n + 1);
  for (i = 0; i <= n; i++)
    if (b[i] == '/' || !b[i]) {
      char old = b[i];
      b[i] = 0;
      r = as(o, b, &s);
      if (r < 0)
        return -1;
      if (r && s.type != GC_DIR)
        return ap(o, "Carpeta no segura", b);
      if (!r && o->c->fs.mkdir(o->c->fs.user, b))
        return ap(o, "No se pudo crear la carpeta", b);
      b[i] = old;
    }
  return 0;
}
static int aparents(ao *o, const char *p) {
  char b[512];
  char *q;
  snprintf(b, sizeof b, "%s", p);
  q = strrchr(b, '/');
  if (!q)
    return 0;
  *q = 0;
  return amkdir(o, b);
}
static int aread(ao *o, const char *p, char **out, size_t *len) {
  gc_stat s;
  void *f;
  char *b;
  size_t got, pos = 0;
  if (as(o, p, &s) != 1 || s.type != GC_FILE || s.size > 65536)
    return ap(o, "Falta un registro valido", p);
  b = malloc((size_t)s.size + 1);
  if (!b)
    return af(o, "Memoria insuficiente para el registro.");
  f = o->c->fs.open(o->c->fs.user, p, 'r');
  if (!f) {
    free(b);
    return ap(o, "No se pudo leer el registro", p);
  }
  while (pos < (size_t)s.size) {
    if (o->c->fs.read(o->c->fs.user, f, b + pos, (size_t)s.size - pos, &got) ||
        !got) {
      o->c->fs.close(o->c->fs.user, f);
      free(b);
      return af(o, "Registro incompleto.");
    }
    pos += got;
  }
  if (o->c->fs.close(o->c->fs.user, f) || memchr(b, 0, pos)) {
    free(b);
    return af(o, "Registro incompleto o invalido.");
  }
  b[pos] = 0;
  *out = b;
  *len = pos;
  return 0;
}
static void ah(const unsigned char *p, char out[65]) {
  static const char *d = "0123456789abcdef";
  unsigned i;
  for (i = 0; i < 32; i++) {
    out[2 * i] = d[p[i] >> 4];
    out[2 * i + 1] = d[p[i] & 15];
  }
  out[64] = 0;
}
static int au(const char *p, unsigned bytes, unsigned char *out) {
  unsigned i;
  for (i = 0; i < bytes; i++) {
    unsigned a = (unsigned char)p[2 * i], b = (unsigned char)p[2 * i + 1];
    if (a >= 'A' && a <= 'F')
      a += 32;
    if (b >= 'A' && b <= 'F')
      b += 32;
    a = a >= '0' && a <= '9'   ? a - '0'
        : a >= 'a' && a <= 'f' ? a - 'a' + 10
                               : 99;
    b = b >= '0' && b <= '9'   ? b - '0'
        : b >= 'a' && b <= 'f' ? b - 'a' + 10
                               : 99;
    if (a > 15 || b > 15)
      return -1;
    out[i] = (unsigned char)((a << 4) | b);
  }
  return 0;
}
static int aid(const char *p) {
  unsigned char id[8];
  return strlen(p) == 16 && !au(p, 8, id);
}
static void lower(char *p) {
  while (*p) {
    if (*p >= 'A' && *p <= 'Z')
      *p += 32;
    p++;
  }
}
static int sameid(const char *a, const char *b) {
  char x[33], y[33];
  snprintf(x, sizeof x, "%s", a);
  snprintf(y, sizeof y, "%s", b);
  lower(x);
  lower(y);
  return !strcmp(x, y);
}
static int backup_info(ao *o, inventory *v, int full) {
  gc_result r = gc_inspect(o->c, full);
  char *b = NULL;
  size_t n;
  unsigned char decoded[32];
  if (!r.ok || !r.info.present) {
    return af(o, "Hace falta un respaldo del sistema valido antes de revisar "
                 "complementos.");
  }
  if (strlen(o->c->identity) != 32 || au(o->c->identity, 16, decoded) ||
      !sameid(o->c->identity, r.info.identity))
    return af(o,
              "No se pudo confirmar que el respaldo pertenece a esta microSD.");
  if (aread(o, GC_BACKUP_ROOT "/current/complete.sha256", &b, &n))
    return -1;
  if (n != 65 || b[64] != '\n' || au(b, 32, decoded)) {
    free(b);
    return af(o, "Sello de respaldo invalido.");
  }
  memcpy(v->backup, b, 64);
  v->backup[64] = 0;
  lower(v->backup);
  snprintf(v->identity, sizeof v->identity, "%s", r.info.identity);
  lower(v->identity);
  free(b);
  return 0;
}
static int ahash(ao *o, const char *p, uint64_t size, unsigned char out[32]) {
  void *f;
  gc_sha256 sha;
  size_t got;
  uint64_t n = 0;
  gc_stat st;
  if (size > A_FILE_LIMIT || o->read + size > A_READ_LIMIT)
    return af(o, "Los complementos superan el limite de revision (128 MiB por "
                 "archivo, 256 MiB en total).");
  if (as(o, p, &st) != 1 || st.type != GC_FILE || st.size != size)
    return ap(o, "El complemento cambio durante la revision", p);
  f = o->c->fs.open(o->c->fs.user, p, 'r');
  if (!f)
    return ap(o, "No se pudo leer el complemento", p);
  gc_sha256_init(&sha);
  for (;;) {
    if (o->c->fs.read(o->c->fs.user, f, o->buffer, 65536, &got)) {
      o->c->fs.close(o->c->fs.user, f);
      return ap(o, "Lectura incompleta del complemento", p);
    }
    if (!got)
      break;
    if (n + got > size) {
      o->c->fs.close(o->c->fs.user, f);
      return af(o, "El complemento cambio durante la revision.");
    }
    gc_sha256_update(&sha, o->buffer, got);
    n += got;
  }
  if (o->c->fs.close(o->c->fs.user, f) || n != size)
    return af(o, "Lectura incompleta del complemento.");
  gc_sha256_final(&sha, out);
  o->read += size;
  return 0;
}
static int fingerprint_module_at(ao *o, const char *id,
                                 const char *flag_override,
                                 unsigned char out[32]) {
  static const char *names[] = {
      "flags/boot2.flag", "exefs.nsp",  "rtld",       "main",
      "main.npdm",        "exefs/rtld", "exefs/main", "exefs/main.npdm"};
  gc_sha256 sha;
  gc_stat st;
  unsigned i;
  unsigned char h[32];
  char p[256], size[32];
  gc_sha256_init(&sha);
  for (i = 0; i < sizeof names / sizeof names[0]; i++) {
    int r;
    snprintf(p, sizeof p, "atmosphere/contents/%s/%s", id, names[i]);
    if (!i && flag_override) {
      if (strlen(flag_override) >= sizeof p)
        return af(o, "Ruta de aislamiento demasiado larga.");
      snprintf(p, sizeof p, "%s", flag_override);
    }
    r = as(o, p, &st);
    if (r < 0)
      return -1;
    gc_sha256_update(&sha, names[i], strlen(names[i]) + 1);
    if (!r) {
      gc_sha256_update(&sha, "missing", 8);
      continue;
    }
    if (st.type != GC_FILE)
      return ap(o, "Un complemento contiene un enlace o archivo especial", p);
    if (ahash(o, p, st.size, h))
      return -1;
    snprintf(size, sizeof size, "%llu", (unsigned long long)st.size);
    gc_sha256_update(&sha, size, strlen(size) + 1);
    gc_sha256_update(&sha, h, 32);
  }
  gc_sha256_final(&sha, out);
  return 0;
}
static int fingerprint_module(ao *o, const char *id, unsigned char out[32]) {
  return fingerprint_module_at(o, id, NULL, out);
}
static int tree_collect(tree *, const char *, unsigned);
typedef struct {
  tree *t;
  char path[512];
  unsigned depth;
} tree_walk;
static int tree_visit(void *v, const char *name, gc_type type) {
  tree_walk *w = v;
  tree *t = w->t;
  char p[512], logical[GC_PATH_MAX + 1];
  gc_stat st;
  int r;
  if (!strcmp(name, ".") || !strcmp(name, ".."))
    return 0;
  if (strchr(name, '/') || strchr(name, '\\') ||
      snprintf(p, sizeof p, "%s/%s", w->path, name) >= (int)sizeof p)
    return af(t->o, "Nombre no seguro en el tema de HOME.");
  if (snprintf(logical, sizeof logical,
               "atmosphere/contents/" THEME_ID "/romfs%s",
               p + strlen(t->root)) > GC_PATH_MAX ||
      !gc_safe_path(logical))
    return af(t->o, "Nombre no seguro en el tema de HOME.");
  r = as(t->o, p, &st);
  if (r != 1)
    return ap(t->o, "No se pudo comprobar el tema", p);
  if (type != st.type || st.type == GC_OTHER)
    return ap(t->o, "El tema contiene un enlace o archivo especial", p);
  if (type == GC_DIR)
    return tree_collect(t, p, w->depth + 1);
  if (type != GC_FILE || t->count >= 4096 || st.size > A_FILE_LIMIT ||
      t->bytes + st.size > A_READ_LIMIT)
    return af(t->o, "El tema supera los limites de revision.");
  snprintf(t->files[t->count].path, sizeof t->files[t->count].path, "%s", p);
  t->files[t->count++].size = st.size;
  t->bytes += st.size;
  return 0;
}
static int tree_collect(tree *t, const char *p, unsigned depth) {
  tree_walk w;
  if (depth > 8)
    return af(t->o, "El tema tiene demasiados niveles de carpetas.");
  w.t = t;
  w.depth = depth;
  snprintf(w.path, sizeof w.path, "%s", p);
  if (t->o->c->fs.list(t->o->c->fs.user, p, tree_visit, &w))
    return ap(t->o, "No se pudo revisar el tema completo", p);
  return 0;
}
static int fingerprint_theme_at(ao *o, const char *root,
                                unsigned char out[32]) {
  tree t;
  tree_file swap;
  gc_sha256 sha;
  unsigned i, j;
  unsigned char h[32];
  char size[32];
  int rc = -1;
  memset(&t, 0, sizeof t);
  t.o = o;
  snprintf(t.root, sizeof t.root, "%s", root);
  t.files = malloc(4096 * sizeof(tree_file));
  if (!t.files)
    return af(o, "Memoria insuficiente para revisar el tema.");
  if (tree_collect(&t, root, 0))
    goto end;
  for (i = 1; i < t.count; i++) {
    swap = t.files[i];
    j = i;
    while (j && strcmp(t.files[j - 1].path, swap.path) > 0) {
      t.files[j] = t.files[j - 1];
      j--;
    }
    t.files[j] = swap;
  }
  gc_sha256_init(&sha);
  for (i = 0; i < t.count; i++) {
    if (ahash(o, t.files[i].path, t.files[i].size, h))
      goto end;
    snprintf(size, sizeof size, "%llu", (unsigned long long)t.files[i].size);
    /* Fingerprint logical game paths, independent of quarantine location. */
    gc_sha256_update(&sha, "atmosphere/contents/" THEME_ID "/romfs",
                     sizeof("atmosphere/contents/" THEME_ID "/romfs") - 1);
    gc_sha256_update(&sha, t.files[i].path + strlen(root),
                     strlen(t.files[i].path + strlen(root)) + 1);
    gc_sha256_update(&sha, size, strlen(size) + 1);
    gc_sha256_update(&sha, h, 32);
  }
  gc_sha256_final(&sha, out);
  rc = 0;
end:
  free(t.files);
  return rc;
}
static int fingerprint_theme(ao *o, unsigned char out[32]) {
  return fingerprint_theme_at(o, "atmosphere/contents/" THEME_ID "/romfs", out);
}
typedef struct {
  ao *o;
  inventory *v;
} addon_walk;
static int addon_visit(void *user, const char *name, gc_type type) {
  addon_walk *w = user;
  gc_stat st;
  char p[256];
  int r;
  ga_candidate *e;
  if (!strcmp(name, ".") || !strcmp(name, ".."))
    return 0;
  if (!aid(name))
    return 0;
  if (type != GC_DIR)
    return ap(w->o, "Una entrada de complementos no es una carpeta segura",
              name);
  snprintf(p, sizeof p, "atmosphere/contents/%s/flags/boot2.flag", name);
  r = as(w->o, p, &st);
  if (r < 0)
    return -1;
  if (r) {
    if (st.type != GC_FILE)
      return ap(w->o, "El marcador de arranque no es seguro", p);
    if (w->v->count >= A_MAX)
      return af(w->o,
                "Hay demasiados complementos de arranque para revisarlos.");
    e = &w->v->entries[w->v->count++];
    memset(e, 0, sizeof *e);
    snprintf(e->title_id, sizeof e->title_id, "%s", name);
    lower(e->title_id);
    e->kind = GA_BOOT_MODULE;
    if (fingerprint_module(w->o, name, e->fingerprint))
      return -1;
  }
  return 0;
}
static int inventory_scan(ao *o, inventory *v) {
  addon_walk w = {o, v};
  gc_stat st;
  int r = as(o, "atmosphere/contents", &st);
  if (r < 0)
    return -1;
  if (r) {
    if (st.type != GC_DIR)
      return af(o, "La carpeta de complementos no es segura.");
    if (o->c->fs.list(o->c->fs.user, "atmosphere/contents", addon_visit, &w))
      return af(o, "No se pudo revisar completa la lista de complementos.");
  }
  r = as(o, "atmosphere/contents/" THEME_ID "/romfs", &st);
  if (r < 0)
    return -1;
  if (r) {
    ga_candidate *e;
    if (st.type != GC_DIR || v->count >= A_MAX)
      return af(o,
                "La carpeta del tema no es segura o hay demasiadas entradas.");
    e = &v->entries[v->count++];
    memset(e, 0, sizeof *e);
    snprintf(e->title_id, sizeof e->title_id, "%s", THEME_ID);
    e->kind = GA_HOME_THEME;
    if (fingerprint_theme(o, e->fingerprint))
      return -1;
  }
  return 0;
}
static int serialize_inventory(ao *o, inventory *v, const char *magic,
                               char **out, size_t *n) {
  size_t pos, cap = 256 + v->count * 100;
  char *b = malloc(cap), hash[65];
  unsigned i;
  unsigned char digest[32];
  int z;
  if (!b)
    return af(o, "Memoria insuficiente para guardar el inventario.");
  z = snprintf(b, cap, "%s\nidentity=%s\nbackup=%s\n", magic, v->identity,
               v->backup);
  if (z < 0 || (size_t)z >= cap) {
    free(b);
    return af(o, "No se pudo crear el inventario.");
  }
  pos = (size_t)z;
  for (i = 0; i < v->count; i++) {
    ah(v->entries[i].fingerprint, hash);
    z = snprintf(b + pos, cap - pos, "%c|%s|%s\n",
                 v->entries[i].kind == GA_HOME_THEME ? 'T' : 'S',
                 v->entries[i].title_id, hash);
    if (z < 0 || (size_t)z >= cap - pos) {
      free(b);
      return af(o, "Inventario demasiado grande.");
    }
    pos += (size_t)z;
  }
  gc_sha256_buffer(b, pos, digest);
  ah(digest, hash);
  z = snprintf(b + pos, cap - pos, "sha256=%s\n", hash);
  if (z < 0 || (size_t)z >= cap - pos) {
    free(b);
    return af(o, "No se pudo sellar el inventario.");
  }
  pos += (size_t)z;
  *out = b;
  *n = pos;
  return 0;
}
static char *aline(char **cursor) {
  char *p = *cursor, *q;
  if (!p || !*p)
    return NULL;
  q = strchr(p, '\n');
  if (!q)
    return NULL;
  *q = 0;
  *cursor = q + 1;
  return p;
}
static int parse_inventory(ao *o, char *b, size_t n, const char *magic,
                           inventory *v) {
  char *seal, *cur, *line;
  unsigned char wanted[32], actual[32], cid[16];
  unsigned i;
  seal = strstr(b, "sha256=");
  if (!seal || strlen(seal) != 72 || seal[71] != '\n' ||
      au(seal + 7, 32, wanted))
    return af(o, "El inventario o plan esta incompleto.");
  gc_sha256_buffer(b, (size_t)(seal - b), actual);
  if (memcmp(actual, wanted, 32))
    return af(o, "El inventario o plan esta danado.");
  (void)n;
  *seal = 0;
  cur = b;
  line = aline(&cur);
  if (!line || strcmp(line, magic))
    return af(o, "Formato de inventario no reconocido.");
  line = aline(&cur);
  if (!line || strncmp(line, "identity=", 9) || strlen(line + 9) != 32 ||
      au(line + 9, 16, cid))
    return af(o, "Identidad de inventario invalida.");
  snprintf(v->identity, sizeof v->identity, "%s", line + 9);
  lower(v->identity);
  line = aline(&cur);
  if (!line || strncmp(line, "backup=", 7) || strlen(line + 7) != 64 ||
      au(line + 7, 32, wanted))
    return af(o, "Respaldo de inventario invalido.");
  snprintf(v->backup, sizeof v->backup, "%s", line + 7);
  lower(v->backup);
  while (*cur) {
    ga_candidate *e;
    line = aline(&cur);
    if (!line || strlen(line) != 83 || (line[0] != 'S' && line[0] != 'T') ||
        line[1] != '|' || line[18] != '|' || v->count >= A_MAX)
      return af(o, "Lista de complementos invalida.");
    line[18] = 0;
    if (!aid(line + 2))
      return af(o, "Identificador de complemento invalido.");
    if (line[0] == 'T' && strcmp(line + 2, THEME_ID))
      return af(o, "Solo se permite aislar el tema de HOME.");
    e = &v->entries[v->count++];
    memset(e, 0, sizeof *e);
    e->kind = line[0] == 'T' ? GA_HOME_THEME : GA_BOOT_MODULE;
    snprintf(e->title_id, sizeof e->title_id, "%s", line + 2);
    lower(e->title_id);
    if (au(line + 19, 32, e->fingerprint))
      return af(o, "Huella de complemento invalida.");
    for (i = 0; i + 1 < v->count; i++)
      if (v->entries[i].kind == e->kind &&
          !strcmp(v->entries[i].title_id, e->title_id))
        return af(o, "El inventario contiene duplicados.");
  }
  return 0;
}
static int read_inventory(ao *o, const char *p, const char *magic,
                          inventory *v) {
  char *b = NULL;
  size_t n;
  int r;
  if (aread(o, p, &b, &n))
    return -1;
  r = parse_inventory(o, b, n, magic, v);
  free(b);
  return r;
}
static int atom_new(ao *o, const char *p, const void *b, size_t n) {
  char temp[512];
  gc_stat st;
  void *f;
  int r, bad = 0;
  snprintf(temp, sizeof temp, "%s.tmp", p);
  r = as(o, temp, &st);
  if (r < 0)
    return -1;
  if (r &&
      (st.type != GC_FILE || o->c->fs.remove(o->c->fs.user, temp, GC_FILE)))
    return af(o, "No se pudo retirar un registro temporal.");
  if (aparents(o, p))
    return -1;
  f = o->c->fs.open(o->c->fs.user, temp, 'x');
  if (!f)
    return af(o, "No se pudo crear un registro nuevo.");
  if (o->c->fs.write(o->c->fs.user, f, b, n) ||
      o->c->fs.file_sync(o->c->fs.user, f))
    bad = 1;
  if (o->c->fs.close(o->c->fs.user, f))
    bad = 1;
  if (bad || async(o))
    return af(o, "El registro no se guardo completamente.");
  if (o->c->fs.rename(o->c->fs.user, temp, p) || async(o))
    return af(o, "No se pudo confirmar el registro nuevo.");
  return 0;
}
static ga_result aresult(ao *o, int ok, int pending, unsigned changed,
                         const char *s) {
  ga_result r;
  memset(&r, 0, sizeof r);
  r.ok = ok;
  r.recovery_required = pending;
  r.changed = changed;
  snprintf(r.message, sizeof r.message, "%s", o->error[0] ? o->error : s);
  free(o->buffer);
  return r;
}
ga_result ga_capture(gc_context *c) {
  ao o;
  inventory *v = NULL;
  char *b = NULL;
  size_t n;
  gc_stat st;
  int ok = 0, r;
  if (ai(&o, c) || awrite_ready(&o))
    goto end;
  r = as(&o, QPLAN, &st);
  if (r != 0) {
    af(&o, "Hay complementos apartados. Reactivalos antes de fijar un "
           "inventario nuevo.");
    goto end;
  }
  v = calloc(1, sizeof *v);
  if (!v) {
    af(&o, "Memoria insuficiente para el inventario.");
    goto end;
  }
  if (backup_info(&o, v, 1) || inventory_scan(&o, v) ||
      serialize_inventory(&o, v, "NMGADDONS1", &b, &n))
    goto end;
  r = as(&o, INVENTORY, &st);
  if (r < 0)
    goto end;
  if (r) {
    if (st.type != GC_FILE) {
      af(&o, "El inventario existente no es un archivo regular; se conserva "
             "sin mover.");
      goto end;
    }
    int old = as(&o, INVENTORY ".old", &st);
    if (old < 0)
      goto end;
    if (old && (st.type != GC_FILE ||
                c->fs.remove(c->fs.user, INVENTORY ".old", GC_FILE))) {
      af(&o, "No se pudo conservar el inventario anterior.");
      goto end;
    }
    if (c->fs.rename(c->fs.user, INVENTORY, INVENTORY ".old") || async(&o)) {
      af(&o, "No se pudo conservar el inventario anterior.");
      goto end;
    }
  }
  if (atom_new(&o, INVENTORY, b, n))
    goto end;
  if (as(&o, INVENTORY ".old", &st) == 1 &&
      (st.type != GC_FILE ||
       c->fs.remove(c->fs.user, INVENTORY ".old", GC_FILE) || async(&o))) {
    af(&o, "El inventario nuevo esta guardado, pero no se pudo limpiar el "
           "anterior.");
    goto end;
  }
  ok = 1;
end:
  free(b);
  free(v);
  return aresult(&o, ok, 0, 0,
                 "Huellas de complementos guardadas sin copiar sus archivos.");
}
ga_review ga_inspect(gc_context *c) {
  ao o;
  inventory *now = NULL, *prior = NULL;
  ga_review r;
  gc_stat st;
  unsigned i, j;
  memset(&r, 0, sizeof r);
  if (ai(&o, c))
    goto end;
  now = calloc(1, sizeof *now);
  prior = calloc(1, sizeof *prior);
  if (!now || !prior) {
    af(&o, "Memoria insuficiente para comparar complementos.");
    goto end;
  }
  if (backup_info(&o, now, 0))
    goto end;
  snprintf(r.backup_sha256, sizeof r.backup_sha256, "%s", now->backup);
  if (read_inventory(&o, INVENTORY, "NMGADDONS1", prior))
    goto end;
  if (strcmp(now->backup, prior->backup) ||
      !sameid(now->identity, prior->identity)) {
    af(&o, "El inventario no corresponde al respaldo actual. Crea un respaldo "
           "nuevo para fijar una referencia fiable.");
    goto end;
  }
  if (inventory_scan(&o, now))
    goto end;
  for (i = 0; i < now->count; i++) {
    for (j = 0; j < prior->count; j++)
      if (now->entries[i].kind == prior->entries[j].kind &&
          !strcmp(now->entries[i].title_id, prior->entries[j].title_id))
        break;
    if (j < prior->count &&
        !memcmp(now->entries[i].fingerprint, prior->entries[j].fingerprint, 32))
      continue;
    if (r.count >= GA_MAX_CANDIDATES) {
      af(&o, "Hay mas de 32 complementos cambiados; se requiere revisar la "
             "tarjeta sin aplicar un aislamiento parcial.");
      goto end;
    }
    r.candidates[r.count] = now->entries[i];
    r.candidates[r.count++].newly_added = j == prior->count;
  }
  r.ok = 1;
end:
  if (c && c->fs.stat && c->fs.stat(c->fs.user, QPLAN, &st) == 1)
    r.quarantine_pending = 1;
  snprintf(r.message, sizeof r.message, "%s",
           o.error[0] ? o.error
           : r.count  ? "Estos complementos son nuevos o cambiaron desde el "
                        "respaldo. No demuestra que causen el fallo."
                      : "No se detectaron cambios en los complementos de "
                        "arranque revisados.");
  free(now);
  free(prior);
  free(o.buffer);
  return r;
}
static void entry_paths(const ga_candidate *e, char src[256], char dest[512]) {
  snprintf(src, 256, "atmosphere/contents/%s/%s", e->title_id,
           e->kind == GA_HOME_THEME ? "romfs" : "flags/boot2.flag");
  snprintf(dest, 512, QROOT "/data/%s/%s", e->title_id,
           e->kind == GA_HOME_THEME ? "romfs" : "flags/boot2.flag");
}
static int operate_plan(ao *o, inventory *v, int undo, unsigned *changed) {
  unsigned i;
  char src[256], dest[512];
  gc_stat a, b;
  int ra, rb;
  if (!sameid(o->c->identity, v->identity))
    return af(o, "El plan de aislamiento pertenece a otra microSD.");
  for (i = 0; i < v->count; i++) {
    ga_candidate *e = &v->entries[i];
    gc_type wanted = e->kind == GA_HOME_THEME ? GC_DIR : GC_FILE;
    entry_paths(e, src, dest);
    ra = as(o, src, &a);
    rb = as(o, dest, &b);
    if (ra < 0 || rb < 0)
      return af(o, "No se pudo comprobar una entrada del aislamiento.");
    if ((ra && a.type != wanted) || (rb && b.type != wanted))
      return af(o, "Una entrada de aislamiento no tiene el tipo esperado.");
    if (ra && rb)
      return af(o, "Hay archivos en ambos destinos; se conservan sin "
                   "sobrescribir. Revisa el aislamiento.");
    if (!ra && !rb)
      return af(o, "Falta una entrada del aislamiento; el plan se conserva "
                   "para revisarla.");
    {
      unsigned char hash[32];
      if (e->kind == GA_HOME_THEME) {
        if (fingerprint_theme_at(o, ra ? src : dest, hash))
          return -1;
      } else if (fingerprint_module_at(o, e->title_id, ra ? NULL : dest, hash))
        return -1;
      if (memcmp(hash, e->fingerprint, 32))
        return af(o, undo ? "El complemento apartado o su ejecutable cambio; "
                            "se conserva sin reactivarlo."
                          : "El complemento cambio despues de confirmarlo; no "
                            "se apartara sin revisarlo otra vez.");
    }
    if ((undo && ra) || (!undo && rb))
      continue;
    if (aparents(o, undo ? src : dest) ||
        o->c->fs.rename(o->c->fs.user, undo ? dest : src, undo ? src : dest) ||
        async(o))
      return af(o, "No se pudo mover el complemento; se conserva el plan para "
                   "continuar.");
    (*changed)++;
    if (acp(o, undo ? "addons.restored" : "addons.quarantined", src))
      return -1;
  }
  return 0;
}
ga_result ga_quarantine(gc_context *c, const ga_review *review) {
  ao o;
  inventory *v = NULL;
  ga_review *fresh = NULL;
  gc_stat st;
  char *b = NULL;
  size_t n;
  unsigned i, j, changed = 0;
  int ok = 0, pending = 0, exists;
  if (ai(&o, c) || awrite_ready(&o))
    goto end;
  v = calloc(1, sizeof *v);
  if (!v) {
    af(&o, "Memoria insuficiente para preparar el aislamiento.");
    goto end;
  }
  if (backup_info(&o, v, 1))
    goto end;
  exists = as(&o, QPLAN, &st);
  if (exists < 0)
    goto end;
  if (exists) {
    inventory *saved = calloc(1, sizeof *saved);
    if (!saved) {
      af(&o, "Memoria insuficiente para leer el plan.");
      goto end;
    }
    if (read_inventory(&o, QPLAN, "NMGQUARANTINE1", saved) ||
        !sameid(v->identity, saved->identity)) {
      free(saved);
      af(&o, "El plan de aislamiento no es valido para esta tarjeta.");
      goto end;
    }
    *v = *saved;
    free(saved);
    pending = 1;
  } else {
    if (!review || !review->ok || !review->count ||
        review->count > GA_MAX_CANDIDATES ||
        strcmp(v->backup, review->backup_sha256)) {
      af(&o, "Hace falta confirmar una revision actual de complementos.");
      goto end;
    }
    fresh = malloc(sizeof *fresh);
    if (!fresh) {
      af(&o, "Memoria insuficiente para confirmar la revision.");
      goto end;
    }
    *fresh = ga_inspect(c);
    if (!fresh->ok || strcmp(fresh->backup_sha256, review->backup_sha256)) {
      af(&o, "La revision cambio; vuelve a revisarla antes de apartar nada.");
      goto end;
    }
    for (i = 0; i < review->count; i++) {
      for (j = 0; j < fresh->count; j++)
        if (review->candidates[i].kind == fresh->candidates[j].kind &&
            !strcmp(review->candidates[i].title_id,
                    fresh->candidates[j].title_id) &&
            !memcmp(review->candidates[i].fingerprint,
                    fresh->candidates[j].fingerprint, 32))
          break;
      if (j == fresh->count) {
        af(&o, "La seleccion no coincide con los complementos revisados.");
        goto end;
      }
      for (j = 0; j < i; j++)
        if (review->candidates[j].kind == review->candidates[i].kind &&
            !strcmp(review->candidates[j].title_id,
                    review->candidates[i].title_id)) {
          af(&o, "La seleccion contiene duplicados.");
          goto end;
        }
      v->entries[v->count++] = review->candidates[i];
    }
    if (serialize_inventory(&o, v, "NMGQUARANTINE1", &b, &n) ||
        atom_new(&o, QPLAN, b, n))
      goto end;
    pending = 1;
  }
  if (operate_plan(&o, v, 0, &changed))
    goto end;
  ok = 1;
end:
  free(v);
  free(fresh);
  free(b);
  return aresult(
      &o, ok, pending && !ok, changed,
      "Complementos seleccionados apartados. Sus archivos y los mods de juegos "
      "se conservan. Puedes reactivarlos desde Guardian.");
}
ga_result ga_restore_quarantine(gc_context *c) {
  ao o;
  inventory *v = NULL;
  unsigned changed = 0;
  int ok = 0, pending = 0;
  gc_stat st;
  if (ai(&o, c) || awrite_ready(&o))
    goto end;
  if (as(&o, QPLAN, &st) != 1) {
    af(&o, "No hay un aislamiento guardado para reactivar.");
    goto end;
  }
  pending = 1;
  v = calloc(1, sizeof *v);
  if (!v) {
    af(&o, "Memoria insuficiente para reactivar complementos.");
    goto end;
  }
  if (read_inventory(&o, QPLAN, "NMGQUARANTINE1", v) ||
      operate_plan(&o, v, 1, &changed))
    goto end;
  if (c->fs.remove(c->fs.user, QPLAN, GC_FILE) || async(&o)) {
    af(&o, "Los complementos volvieron a su destino, pero falta confirmar el "
           "cierre del plan.");
    goto end;
  }
  pending = 0;
  ok = 1;
end:
  free(v);
  return aresult(&o, ok, pending && !ok, changed,
                 "Complementos reactivados sin sustituir archivos nuevos. Los "
                 "cambios se aplicaran al reiniciar.");
}
