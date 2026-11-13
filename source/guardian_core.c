#include "guardian_core.h"
#include "guardian_sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The engine knows SD files only. It never opens a partition, NAND or save. */
#define MAX_FILE (128ull * 1024 * 1024)
#define MAX_TOTAL (8ull * 1024 * 1024 * 1024)
#define IO_SIZE 65536u
#define MANIFEST_MAX (2u * 1024 * 1024)
#define ROOT GC_BACKUP_ROOT
#define CURRENT ROOT "/current"
#define STAGING ROOT "/staging"
#define PREVIOUS ROOT "/previous"
#define PROMOTION ROOT "/promotion.pending"
#define RESTORE "config/NinMagicGuardian/restore"
typedef struct {
  char path[GC_PATH_MAX + 1];
  uint64_t size;
  unsigned char hash[32];
} entry;
typedef struct {
  gc_backup_info info;
  entry *e;
  unsigned char digest[32];
} manifest;
typedef struct {
  gc_context *c;
  char error[512];
  unsigned char *buf;
  uint64_t done, total;
} operation;

static int fail(operation *o, const char *text) {
  if (!o->error[0])
    snprintf(o->error, sizeof o->error, "%s", text);
  return -1;
}
static int path_error(operation *o, const char *text, const char *path) {
  if (!o->error[0])
    snprintf(o->error, sizeof o->error, "%s: %.320s", text, path);
  return -1;
}
static int eqi(const char *a, const char *b) {
  while (*a && *b) {
    unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
    if (x >= 'A' && x <= 'Z')
      x += 32;
    if (y >= 'A' && y <= 'Z')
      y += 32;
    if (x != y)
      return 0;
  }
  return !*a && !*b;
}
static int prefixi(const char *a, const char *b) {
  while (*b) {
    unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
    if (x >= 'A' && x <= 'Z')
      x += 32;
    if (y >= 'A' && y <= 'Z')
      y += 32;
    if (!x || x != y)
      return 0;
  }
  return 1;
}
static int suffixi(const char *p, const char *s) {
  size_t n = strlen(p), m = strlen(s);
  return n >= m && eqi(p + n - m, s);
}
int gc_safe_path(const char *p) {
  const char *start = p;
  size_t n;
  if (!p || !*p || *p == '/')
    return 0;
  n = strlen(p);
  if (n > GC_PATH_MAX || p[n - 1] == '/')
    return 0;
  while (*p) {
    unsigned char c = (unsigned char)*p;
    if (c < 32 || c > 126 || c == '\\' || c == ':' || c == '|' || c == '*' ||
        c == '?' || c == '"' || c == '<' || c == '>')
      return 0;
    if (c == '/') {
      size_t len = (size_t)(p - start);
      if (!len || p[-1] == '.' || p[-1] == ' ' ||
          (len == 1 && start[0] == '.') ||
          (len == 2 && start[0] == '.' && start[1] == '.'))
        return 0;
      start = p + 1;
    }
    p++;
  }
  n = (size_t)(p - start);
  return n && p[-1] != '.' && p[-1] != ' ' && !(n == 1 && start[0] == '.') &&
         !(n == 2 && start[0] == '.' && start[1] == '.');
}
int gc_managed_path(const char *p, unsigned options) {
  const char *q;
  if (!gc_safe_path(p) || options > 3)
    return 0;
  if (eqi(p, "atmosphere/package3") ||
      eqi(p, "atmosphere/fusee-secondary.bin") ||
      eqi(p, "atmosphere/fusee-mtc.bin") ||
      eqi(p, "atmosphere/reboot_payload.bin") ||
      eqi(p, "atmosphere/secmon.bin") || eqi(p, "atmosphere/warmboot.bin") ||
      eqi(p, "atmosphere/fatal.bin") || eqi(p, "exosphere.ini") ||
      eqi(p, "emuMMC/emummc.ini"))
    return 1;
  if (eqi(p, "bootloader/hekate_ipl.ini") || eqi(p, "bootloader/nyx.ini") ||
      eqi(p, "bootloader/patches.ini") || eqi(p, "bootloader/update.bin"))
    return 1;
  if (eqi(p, "config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin") ||
      eqi(p, "config/NinMagicGuardian/rescue/libsys_minerva.bso"))
    return 1;
  if ((prefixi(p, "atmosphere/config/") ||
       prefixi(p, "atmosphere/config_templates/") ||
       prefixi(p, "bootloader/ini/")) &&
      suffixi(p, ".ini"))
    return 1;
  if (prefixi(p, "atmosphere/hosts/") && suffixi(p, ".txt"))
    return 1;
  if (prefixi(p, "bootloader/sys/") &&
      (suffixi(p, ".bso") || suffixi(p, ".bin") || suffixi(p, ".pak") ||
       suffixi(p, ".kipm")))
    return 1;
  if (prefixi(p, "atmosphere/kips/") &&
      (suffixi(p, ".kip") || suffixi(p, ".kipm")))
    return 1;
  /* Referenced boot payloads are essential; unreferenced payloads are opt-in.
   */
  if (prefixi(p, "bootloader/payloads/") && suffixi(p, ".bin"))
    return 1;
  if (!strchr(p, '/') &&
      (eqi(p, "payload.bin") || eqi(p, "fusee.bin") || eqi(p, "hekate.bin") ||
       (prefixi(p, "hekate_ctcaer_") && suffixi(p, ".bin"))))
    return 1;
  q = "bootloader/res/";
  if ((options & GC_OPT_VISUALS) &&
      ((prefixi(p, q) &&
        (suffixi(p, ".bmp") || suffixi(p, ".jpg") || suffixi(p, ".png"))) ||
       eqi(p, "bootloader/bootlogo.bmp")))
    return 1;
  return 0;
}
static int identity_valid(const char *s) {
  unsigned i, nonzero = 0;
  if (strlen(s) != 32)
    return 0;
  for (i = 0; i < 32; i++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F')))
      return 0;
    if (c != '0')
      nonzero = 1;
  }
  return nonzero;
}
static int metadata_valid(const char *s) {
  unsigned n = 0;
  while (*s) {
    unsigned c = (unsigned char)*s++;
    if (c < 32 || c > 126 || c == '|')
      return 0;
    if (++n > 63)
      return 0;
  }
  return 1;
}
static int setup(operation *o, gc_context *c, int writing) {
  memset(o, 0, sizeof *o);
  o->c = c;
  if (!c)
    return fail(o, "Falta el contexto de la tarjeta SD.");
  if (!c->fs.stat || !c->fs.list || !c->fs.open || !c->fs.read || !c->fs.close)
    return fail(o, "El lector de archivos no esta disponible.");
  if (writing &&
      (!c->fs.write || !c->fs.file_sync || !c->fs.mkdir || !c->fs.rename ||
       !c->fs.remove || !c->fs.sync || !c->fs.free_space))
    return fail(o, "El escritor de archivos no esta disponible.");
  if (writing && !identity_valid(c->identity))
    return fail(o, "No se pudo identificar esta microSD; no se modificara "
                   "ningun archivo.");
  o->buf = (unsigned char *)malloc(IO_SIZE);
  if (!o->buf)
    return fail(o, "Memoria insuficiente para el respaldo.");
  return 0;
}
static void destroy(operation *o) {
  free(o->buf);
  o->buf = NULL;
}
static int parents_safe(operation *o, const char *p) {
  char b[512];
  size_t n = strlen(p), i;
  gc_stat s;
  int r;
  if (n >= sizeof b)
    return path_error(o, "Ruta demasiado larga", p);
  memcpy(b, p, n + 1);
  for (i = 0; i < n; i++)
    if (b[i] == '/') {
      b[i] = 0;
      r = o->c->fs.stat(o->c->fs.user, b, &s);
      b[i] = '/';
      if (r < 0)
        return path_error(o, "No se pudo comprobar la carpeta", p);
      if (!r)
        return 0;
      if (s.type != GC_DIR)
        return path_error(
            o, "Ruta no segura; una carpeta es un enlace u otro archivo", p);
    }
  return 0;
}
static int stat_path(operation *o, const char *p, gc_stat *s) {
  if (parents_safe(o, p))
    return -1;
  return o->c->fs.stat(o->c->fs.user, p, s);
}
static int exists(operation *o, const char *p, gc_type type) {
  gc_stat s;
  int r = stat_path(o, p, &s);
  if (r < 0)
    return fail(o, "No se pudo comprobar un archivo de la SD.");
  if (r && s.type != type)
    return path_error(o, "Tipo de archivo inesperado", p);
  return r;
}
static int mkdirs(operation *o, const char *p) {
  char b[512];
  size_t i, n = strlen(p);
  gc_stat s;
  int r;
  if (n >= sizeof b)
    return fail(o, "Ruta interna demasiado larga.");
  memcpy(b, p, n + 1);
  for (i = 0; i <= n; i++)
    if (b[i] == '/' || !b[i]) {
      char old = b[i];
      b[i] = 0;
      r = stat_path(o, b, &s);
      if (r < 0)
        return path_error(o, "No se pudo comprobar la carpeta", b);
      if (!r) {
        if (o->c->fs.mkdir(o->c->fs.user, b))
          return path_error(o, "No se pudo crear la carpeta", b);
      } else if (s.type != GC_DIR)
        return path_error(
            o, "Una carpeta contiene un enlace o archivo inesperado", b);
      b[i] = old;
    }
  return 0;
}
static int parent_dirs(operation *o, const char *p) {
  char b[512];
  char *q;
  snprintf(b, sizeof b, "%s", p);
  q = strrchr(b, '/');
  if (!q)
    return 0;
  *q = 0;
  return mkdirs(o, b);
}
static int sync_sd(operation *o) {
  return o->c->fs.sync(o->c->fs.user)
             ? fail(o, "No se pudieron confirmar los cambios en la microSD.")
             : 0;
}
static int checkpoint(operation *o, const char *point, const char *path) {
  if (o->c->fs.checkpoint && o->c->fs.checkpoint(o->c->fs.user, point, path))
    return fail(o, "Operacion interrumpida. Se conserva el respaldo; vuelve a "
                   "recuperar para continuar.");
  return 0;
}
static void progress(operation *o, const char *p, uint64_t n) {
  o->done += n;
  if (o->c->progress)
    o->c->progress(o->c->progress_user, p, o->done, o->total);
}
static int hash_file(operation *o, const char *p, uint64_t size,
                     unsigned char out[32]) {
  gc_stat s;
  gc_sha256 sha;
  void *f;
  uint64_t total = 0;
  size_t got;
  int bad = 0;
  if (stat_path(o, p, &s) != 1 || s.type != GC_FILE || s.size != size)
    return path_error(o, "El archivo falta, cambio o no es regular", p);
  f = o->c->fs.open(o->c->fs.user, p, 'r');
  if (!f)
    return path_error(o, "No se pudo abrir el archivo", p);
  gc_sha256_init(&sha);
  for (;;) {
    if (o->c->fs.read(o->c->fs.user, f, o->buf, IO_SIZE, &got)) {
      bad = 1;
      break;
    }
    if (!got)
      break;
    if (total + got > size) {
      bad = 1;
      break;
    }
    gc_sha256_update(&sha, o->buf, got);
    total += got;
  }
  if (o->c->fs.close(o->c->fs.user, f))
    bad = 1;
  if (bad || total != size)
    return path_error(o, "Lectura incompleta", p);
  gc_sha256_final(&sha, out);
  return 0;
}
static int write_new(operation *o, const char *p, const void *v, size_t n) {
  void *f;
  int bad = 0;
  if (parent_dirs(o, p))
    return -1;
  f = o->c->fs.open(o->c->fs.user, p, 'x');
  if (!f)
    return path_error(o, "No se pudo crear el archivo nuevo", p);
  if (o->c->fs.write(o->c->fs.user, f, v, n) ||
      o->c->fs.file_sync(o->c->fs.user, f))
    bad = 1;
  if (o->c->fs.close(o->c->fs.user, f))
    bad = 1;
  return bad ? path_error(o, "No se pudo guardar y confirmar el archivo", p)
             : 0;
}
/* Only a completely flushed control record becomes visible under its final
 * name. A short write leaves a disposable .tmp, never a partial journal. */
static int write_control(operation *o, const char *p, const void *v, size_t n) {
  char temp[512];
  gc_stat st;
  int r;
  if (snprintf(temp, sizeof temp, "%s.tmp", p) >= (int)sizeof temp)
    return fail(o, "Ruta de control demasiado larga.");
  r = stat_path(o, temp, &st);
  if (r < 0)
    return fail(o, "No se pudo comprobar el registro temporal.");
  if (r &&
      (st.type != GC_FILE || o->c->fs.remove(o->c->fs.user, temp, GC_FILE)))
    return fail(o, "No se pudo retirar un registro temporal incompleto.");
  if (write_new(o, temp, v, n) || sync_sd(o))
    return -1;
  if (o->c->fs.rename(o->c->fs.user, temp, p) || sync_sd(o))
    return fail(o, "No se pudo confirmar el registro de recuperacion.");
  return 0;
}
static int read_small(operation *o, const char *p, char **out, size_t *len,
                      size_t max) {
  gc_stat s;
  void *f;
  char *b;
  size_t pos = 0, got;
  int bad = 0;
  if (stat_path(o, p, &s) != 1 || s.type != GC_FILE || s.size > max)
    return path_error(o, "Falta un archivo de control valido", p);
  b = (char *)malloc((size_t)s.size + 1);
  if (!b)
    return fail(o, "Memoria insuficiente para leer el indice.");
  f = o->c->fs.open(o->c->fs.user, p, 'r');
  if (!f) {
    free(b);
    return path_error(o, "No se pudo abrir el indice", p);
  }
  while (pos < (size_t)s.size) {
    if (o->c->fs.read(o->c->fs.user, f, b + pos, (size_t)s.size - pos, &got) ||
        !got) {
      bad = 1;
      break;
    }
    pos += got;
  }
  if (o->c->fs.close(o->c->fs.user, f))
    bad = 1;
  if (bad) {
    free(b);
    return path_error(o, "Indice incompleto", p);
  }
  b[pos] = 0;
  if (memchr(b, 0, pos)) {
    free(b);
    return fail(o, "El indice contiene datos invalidos.");
  }
  *out = b;
  *len = pos;
  return 0;
}
static void hex(const unsigned char *p, unsigned n, char *out) {
  static const char d[] = "0123456789abcdef";
  unsigned i;
  for (i = 0; i < n; i++) {
    out[i * 2] = d[p[i] >> 4];
    out[i * 2 + 1] = d[p[i] & 15];
  }
  out[n * 2] = 0;
}
static int unhex(const char *s, unsigned n, unsigned char *out) {
  unsigned i;
  for (i = 0; i < n; i++) {
    unsigned a = (unsigned char)s[2 * i], b = (unsigned char)s[2 * i + 1];
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
static int number(const char *s, uint64_t *out) {
  uint64_t v = 0;
  unsigned n = 0;
  if (!*s)
    return -1;
  while (*s) {
    unsigned c = (unsigned char)*s++;
    if (c < '0' || c > '9' || ++n > 20 || v > (UINT64_MAX - (c - '0')) / 10)
      return -1;
    v = v * 10 + c - '0';
  }
  *out = v;
  return 0;
}
static void manifest_free(manifest *m) {
  free(m->e);
  memset(m, 0, sizeof *m);
}
static int manifest_alloc(operation *o, manifest *m) {
  memset(m, 0, sizeof *m);
  m->e = (entry *)malloc(sizeof(entry) * GC_MAX_FILES);
  return m->e ? 0 : fail(o, "Memoria insuficiente para la lista de respaldo.");
}
static int add_entry(operation *o, manifest *m, const char *p) {
  gc_stat s;
  unsigned i;
  int r;
  if (!gc_managed_path(p, m->info.options))
    return path_error(o, "Archivo fuera del respaldo del sistema", p);
  for (i = 0; i < m->info.files; i++)
    if (eqi(m->e[i].path, p))
      return 0;
  r = stat_path(o, p, &s);
  if (r < 0)
    return path_error(o, "No se pudo comprobar el archivo", p);
  if (!r)
    return 0;
  if (s.type != GC_FILE)
    return path_error(o, "No se permiten enlaces ni archivos especiales", p);
  if (s.size > MAX_FILE || m->info.bytes + s.size > MAX_TOTAL ||
      m->info.files >= GC_MAX_FILES)
    return fail(
        o, "El respaldo supera los limites de archivos o espacio del sistema.");
  i = m->info.files++;
  memset(&m->e[i], 0, sizeof(entry));
  snprintf(m->e[i].path, sizeof m->e[i].path, "%s", p);
  m->e[i].size = s.size;
  m->info.bytes += s.size;
  return 0;
}
typedef struct {
  operation *o;
  manifest *m;
  char path[GC_PATH_MAX + 1];
  unsigned depth;
  int deleting;
} walker;
static int collect_tree(operation *, manifest *, const char *, unsigned);
static int collect_visit(void *v, const char *name, gc_type type) {
  walker *w = (walker *)v;
  char p[GC_PATH_MAX + 1];
  int n;
  if (!strcmp(name, ".") || !strcmp(name, ".."))
    return 0;
  if (strchr(name, '/') || strchr(name, '\\'))
    return fail(w->o, "El lector devolvio un nombre de archivo no seguro.");
  n = snprintf(p, sizeof p, "%s/%s", w->path, name);
  if (n < 0 || n > GC_PATH_MAX || !gc_safe_path(p))
    return path_error(w->o, "Nombre de archivo no seguro", name);
  if (type == GC_DIR)
    return collect_tree(w->o, w->m, p, w->depth + 1);
  if (type == GC_OTHER)
    return path_error(w->o, "Se encontro un enlace o archivo especial", p);
  if (gc_managed_path(p, w->m->info.options))
    return add_entry(w->o, w->m, p);
  return 0;
}
static int collect_tree(operation *o, manifest *m, const char *p,
                        unsigned depth) {
  walker w;
  int r = exists(o, p, GC_DIR);
  if (r <= 0)
    return r;
  if (depth > 8)
    return fail(o, "Hay demasiados niveles en una carpeta del sistema.");
  memset(&w, 0, sizeof w);
  w.o = o;
  w.m = m;
  w.depth = depth;
  snprintf(w.path, sizeof w.path, "%s", p);
  if (o->c->fs.list(o->c->fs.user, p, collect_visit, &w))
    return path_error(o, "No se pudo leer completa la carpeta", p);
  return 0;
}
static char *trim(char *s) {
  char *end;
  while (*s == ' ' || *s == '\t' || *s == '\r')
    s++;
  end = s + strlen(s);
  while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
    *--end = 0;
  return s;
}
static int boot_references(operation *o, manifest *m, const char *p) {
  char *b, *cur, *line;
  size_t n;
  int rc = 0;
  if (read_small(o, p, &b, &n, 131072))
    return -1;
  cur = b;
  while (*cur) {
    char *end = strchr(cur, '\n'), *key, *val, *equal;
    if (end)
      *end = 0;
    line = trim(cur);
    if (*line && *line != '#' && *line != ';' && *line != '[') {
      equal = strchr(line, '=');
      if (equal) {
        *equal = 0;
        key = trim(line);
        val = trim(equal + 1);
        if (eqi(key, "payload") || eqi(key, "fss0") || eqi(key, "pkg3") ||
            eqi(key, "warmboot") || eqi(key, "secmon") ||
            eqi(key, "exofatal") || eqi(key, "kip1")) {
          if (eqi(key, "payload")) {
            const char *base = strrchr(val, '/');
            int own = eqi(
                val,
                "config/NinMagicGuardian/rescue/ninmagic-guardian-rescue.bin");
            base = base ? base + 1 : val;
            /* Other boot tools and Android/Linux payloads are not HOS system
             * data. Their INI entries stay intact; optional payload backup is
             * restricted to the explicit payload allowlist. */
            if (!own && (!(m->info.options & GC_OPT_PAYLOADS) &&
                         !prefixi(base, "fusee") && !prefixi(base, "hekate") &&
                         !eqi(val, "payload.bin")))
              goto next_reference;
            if (!own && !gc_managed_path(val, m->info.options))
              goto next_reference;
            if (own) {
              gc_stat module;
              const char *minerva =
                  "config/NinMagicGuardian/rescue/libsys_minerva.bso";
              if (stat_path(o, minerva, &module) != 1 ||
                  module.type != GC_FILE || !module.size ||
                  add_entry(o, m, minerva)) {
                rc = path_error(
                    o, "Falta el modulo de memoria del rescatador Guardian",
                    minerva);
                break;
              }
            }
          }
          if (eqi(key, "kip1") && (eqi(val, "atmosphere/kips/*") ||
                                   eqi(val, "atmosphere/kips/*.kip"))) {
            unsigned before = m->info.files;
            if (collect_tree(o, m, "atmosphere/kips", 0)) {
              rc = -1;
              break;
            }
            unsigned found = 0, k;
            (void)before;
            for (k = 0; k < m->info.files; k++)
              if (prefixi(m->e[k].path, "atmosphere/kips/"))
                found++;
            if (!found) {
              rc = path_error(o, "No se encontro una dependencia KIP valida",
                              val);
              break;
            }
          } else if (!gc_managed_path(val, m->info.options)) {
            rc = path_error(
                o,
                "La configuracion de arranque requiere un archivo fuera del "
                "respaldo permitido; no se creara una copia incompleta",
                val);
            break;
          } else {
            gc_stat st;
            if (stat_path(o, val, &st) != 1 || st.type != GC_FILE || !st.size) {
              rc = path_error(
                  o,
                  "Falta un archivo de arranque indicado en la configuracion",
                  val);
              break;
            }
            if (add_entry(o, m, val)) {
              rc = -1;
              break;
            }
          }
        }
      }
    }
  next_reference:
    if (!end)
      break;
    cur = end + 1;
  }
  free(b);
  return rc;
}
static int scan(operation *o, manifest *m, unsigned options) {
  static const char *files[] = {"atmosphere/package3",
                                "atmosphere/fusee-secondary.bin",
                                "atmosphere/fusee-mtc.bin",
                                "atmosphere/reboot_payload.bin",
                                "bootloader/hekate_ipl.ini",
                                "bootloader/nyx.ini",
                                "bootloader/patches.ini",
                                "bootloader/update.bin",
                                "exosphere.ini",
                                "emuMMC/emummc.ini",
                                "payload.bin",
                                "fusee.bin",
                                "hekate.bin"};
  static const char *dirs[] = {
      "atmosphere/config", "atmosphere/config_templates", "atmosphere/hosts",
      "bootloader/sys", "bootloader/ini"};
  unsigned i, j;
  gc_stat st;
  entry tmp;
  if (options > 3)
    return fail(o, "Opciones de respaldo desconocidas.");
  m->info.options = options;
  snprintf(m->info.identity, sizeof m->info.identity, "%s", o->c->identity);
  snprintf(m->info.firmware, 64, "%s", o->c->firmware);
  snprintf(m->info.model, 64, "%s", o->c->model);
  snprintf(m->info.created, 64, "%s", o->c->created);
  if (!metadata_valid(m->info.firmware) || !metadata_valid(m->info.model) ||
      !metadata_valid(m->info.created))
    return fail(o, "Los datos del respaldo contienen caracteres no admitidos.");
  if (stat_path(o, "atmosphere/package3", &st) != 1 || st.type != GC_FILE ||
      !st.size)
    return fail(
        o, "Falta atmosphere/package3: no se creara un respaldo incompleto.");
  if (stat_path(o, "bootloader/hekate_ipl.ini", &st) != 1 ||
      st.type != GC_FILE || !st.size)
    return fail(o, "Falta bootloader/hekate_ipl.ini: no se creara un respaldo "
                   "incompleto.");
  for (i = 0; i < sizeof files / sizeof files[0]; i++)
    if (add_entry(o, m, files[i]))
      return -1;
  for (i = 0; i < sizeof dirs / sizeof dirs[0]; i++)
    if (collect_tree(o, m, dirs[i], 0))
      return -1;
  if ((options & GC_OPT_VISUALS) &&
      (collect_tree(o, m, "bootloader/res", 0) ||
       add_entry(o, m, "bootloader/bootlogo.bmp")))
    return -1;
  if ((options & GC_OPT_PAYLOADS) &&
      collect_tree(o, m, "bootloader/payloads", 0))
    return -1;
  /* Newly appended dependencies may be INI files too. */
  for (i = 0; i < m->info.files; i++)
    if (prefixi(m->e[i].path, "bootloader/") && suffixi(m->e[i].path, ".ini") &&
        boot_references(o, m, m->e[i].path))
      return -1;
  for (i = 1; i < m->info.files; i++) {
    tmp = m->e[i];
    j = i;
    while (j && strcmp(m->e[j - 1].path, tmp.path) > 0) {
      m->e[j] = m->e[j - 1];
      j--;
    }
    m->e[j] = tmp;
  }
  m->info.present = 1;
  return 0;
}
static int serialize(operation *o, manifest *m, char **out, size_t *len) {
  size_t cap = 512 + (size_t)m->info.files * 340, pos = 0;
  char *b = (char *)malloc(cap), h[65];
  unsigned i;
  int n;
  if (!b)
    return fail(o, "Memoria insuficiente para guardar el indice.");
  n = snprintf(b, cap,
               "NMGUARDIAN1\nidentity=%s\nfirmware=%s\nmodel=%s\ncreated=%"
               "s\noptions=%u\ncount=%u\nbytes=%llu\n",
               m->info.identity, m->info.firmware, m->info.model,
               m->info.created, m->info.options, m->info.files,
               (unsigned long long)m->info.bytes);
  if (n < 0 || (size_t)n >= cap) {
    free(b);
    return fail(o, "No se pudo crear el indice.");
  }
  pos = (size_t)n;
  for (i = 0; i < m->info.files; i++) {
    hex(m->e[i].hash, 32, h);
    n = snprintf(b + pos, cap - pos, "F|%llu|%s|%s\n",
                 (unsigned long long)m->e[i].size, h, m->e[i].path);
    if (n < 0 || (size_t)n >= cap - pos) {
      free(b);
      return fail(o, "Indice demasiado grande.");
    }
    pos += (size_t)n;
  }
  *out = b;
  *len = pos;
  return 0;
}
static char *next_line(char **cursor) {
  char *p = *cursor, *end;
  if (!p || !*p)
    return NULL;
  end = strchr(p, '\n');
  if (!end)
    return NULL;
  *end = 0;
  *cursor = end + 1;
  return p;
}
static int parse_manifest(operation *o, char *b, manifest *m) {
  char *cur = b, *line;
  uint64_t v, total = 0;
  unsigned i, j, required = 0;
  if (!(line = next_line(&cur)) || strcmp(line, "NMGUARDIAN1"))
    return fail(o, "Formato de respaldo no reconocido.");
#define FIELD(KEY, DEST)                                                       \
  do {                                                                         \
    line = next_line(&cur);                                                    \
    if (!line || strncmp(line, KEY, strlen(KEY)) ||                            \
        strlen(line + strlen(KEY)) >= sizeof(DEST))                            \
      return fail(o, "El indice esta incompleto o es invalido.");              \
    snprintf(DEST, sizeof(DEST), "%s", line + strlen(KEY));                    \
  } while (0)
  FIELD("identity=", m->info.identity);
  FIELD("firmware=", m->info.firmware);
  FIELD("model=", m->info.model);
  FIELD("created=", m->info.created);
#undef FIELD
  if (!identity_valid(m->info.identity) || !metadata_valid(m->info.firmware) ||
      !metadata_valid(m->info.model) || !metadata_valid(m->info.created))
    return fail(o, "La identidad o los datos del respaldo son invalidos.");
  line = next_line(&cur);
  if (!line || strncmp(line, "options=", 8) || number(line + 8, &v) || v > 3)
    return fail(o, "Opciones invalidas en el respaldo.");
  m->info.options = (unsigned)v;
  line = next_line(&cur);
  if (!line || strncmp(line, "count=", 6) || number(line + 6, &v) || !v ||
      v > GC_MAX_FILES)
    return fail(o, "Numero de archivos invalido.");
  m->info.files = (unsigned)v;
  line = next_line(&cur);
  if (!line || strncmp(line, "bytes=", 6) || number(line + 6, &v) ||
      v > MAX_TOTAL)
    return fail(o, "Tamano total invalido.");
  m->info.bytes = v;
  for (i = 0; i < m->info.files; i++) {
    char *size, *h, *path, *bar;
    line = next_line(&cur);
    if (!line || strncmp(line, "F|", 2))
      return fail(o, "Lista de archivos incompleta.");
    size = line + 2;
    bar = strchr(size, '|');
    if (!bar)
      return fail(o, "Registro de archivo invalido.");
    *bar = 0;
    h = bar + 1;
    bar = strchr(h, '|');
    if (!bar)
      return fail(o, "Registro de archivo invalido.");
    *bar = 0;
    path = bar + 1;
    if (number(size, &v) || v > MAX_FILE || strlen(h) != 64 ||
        unhex(h, 32, m->e[i].hash) || !gc_managed_path(path, m->info.options))
      return path_error(
          o, "El respaldo contiene un archivo no permitido o invalido", path);
    m->e[i].size = v;
    snprintf(m->e[i].path, sizeof m->e[i].path, "%s", path);
    total += v;
    if (total > MAX_TOTAL)
      return fail(o, "El respaldo supera el limite total.");
    if (i && strcmp(m->e[i - 1].path, path) >= 0)
      return fail(o, "El indice no esta ordenado o contiene duplicados.");
    for (j = 0; j < i; j++)
      if (eqi(m->e[j].path, path))
        return fail(o, "El indice contiene rutas que coinciden en FAT.");
    if (v && eqi(path, "atmosphere/package3"))
      required |= 1;
    if (v && eqi(path, "bootloader/hekate_ipl.ini"))
      required |= 2;
  }
  if (*cur || total != m->info.bytes)
    return fail(o, "El indice contiene datos sobrantes o un total incorrecto.");
  if (required != 3)
    return fail(o, "El respaldo no contiene los archivos esenciales de "
                   "Atmosphere y Hekate.");
  m->info.present = 1;
  return 0;
}
static int load(operation *o, const char *dir, manifest *m, int verify) {
  char p[512], *body = NULL, *digest = NULL;
  size_t n = 0, dn = 0;
  unsigned char actual[32], wanted[32];
  unsigned i;
  gc_stat st;
  int r = stat_path(o, dir, &st);
  if (r < 0)
    return path_error(o, "No se pudo comprobar el respaldo", dir);
  if (!r)
    return 0;
  if (st.type != GC_DIR)
    return fail(o, "La carpeta de respaldo no es segura.");
  if (manifest_alloc(o, m))
    return -1;
  snprintf(p, sizeof p, "%s/manifest.txt", dir);
  if (read_small(o, p, &body, &n, MANIFEST_MAX))
    goto bad;
  gc_sha256_buffer(body, n, actual);
  snprintf(p, sizeof p, "%s/complete.sha256", dir);
  if (read_small(o, p, &digest, &dn, 65))
    goto bad;
  if (dn != 65 || digest[64] != '\n') {
    fail(o, "El sello del respaldo esta incompleto.");
    goto bad;
  }
  digest[64] = 0;
  if (unhex(digest, 32, wanted) || memcmp(actual, wanted, 32)) {
    fail(o, "El indice del respaldo esta danado; no se restaurara.");
    goto bad;
  }
  memcpy(m->digest, actual, 32);
  if (parse_manifest(o, body, m))
    goto bad;
  free(body);
  free(digest);
  body = digest = NULL;
  if (verify) {
    o->total = m->info.bytes;
    o->done = 0;
    for (i = 0; i < m->info.files; i++) {
      snprintf(p, sizeof p, "%s/data/%s", dir, m->e[i].path);
      if (hash_file(o, p, m->e[i].size, actual) ||
          memcmp(actual, m->e[i].hash, 32)) {
        path_error(o, "La copia no coincide con su SHA-256", m->e[i].path);
        goto bad;
      }
      progress(o, m->e[i].path, m->e[i].size);
    }
    m->info.verified = 1;
  }
  return 1;
bad:
  free(body);
  free(digest);
  manifest_free(m);
  return -1;
}
static int copy_file(operation *o, const char *src, const char *dst,
                     uint64_t size, unsigned char out[32]) {
  void *in = NULL, *f = NULL;
  gc_sha256 sha;
  gc_stat st;
  uint64_t count = 0;
  size_t got;
  int rc = -1, bad = 0;
  if (stat_path(o, src, &st) != 1 || st.type != GC_FILE || st.size != size)
    return path_error(o, "El archivo cambio antes de copiarlo", src);
  if (parent_dirs(o, dst))
    return -1;
  in = o->c->fs.open(o->c->fs.user, src, 'r');
  if (!in)
    return path_error(o, "No se pudo abrir el archivo original", src);
  f = o->c->fs.open(o->c->fs.user, dst, 'x');
  if (!f) {
    o->c->fs.close(o->c->fs.user, in);
    return path_error(o, "No se pudo crear la copia nueva", dst);
  }
  gc_sha256_init(&sha);
  for (;;) {
    if (o->c->fs.read(o->c->fs.user, in, o->buf, IO_SIZE, &got)) {
      bad = 1;
      break;
    }
    if (!got)
      break;
    if (count + got > size || o->c->fs.write(o->c->fs.user, f, o->buf, got)) {
      bad = 1;
      break;
    }
    gc_sha256_update(&sha, o->buf, got);
    count += got;
    progress(o, src, got);
  }
  if (!bad && count == size && !o->c->fs.file_sync(o->c->fs.user, f)) {
    gc_sha256_final(&sha, out);
    rc = 0;
  }
  if (o->c->fs.close(o->c->fs.user, in))
    rc = -1;
  if (o->c->fs.close(o->c->fs.user, f))
    rc = -1;
  if (rc)
    return path_error(o, "La copia no termino; se conserva la copia anterior",
                      src);
  return 0;
}
typedef struct {
  operation *o;
  char path[512];
  unsigned depth;
} eraser;
static int remove_tree(operation *, const char *, unsigned);
static int erase_visit(void *v, const char *name, gc_type type) {
  eraser *w = (eraser *)v;
  char p[512];
  int n;
  (void)type;
  if (!strcmp(name, ".") || !strcmp(name, ".."))
    return 0;
  if (strchr(name, '/') || strchr(name, '\\'))
    return fail(w->o, "Nombre inesperado en los temporales.");
  n = snprintf(p, sizeof p, "%s/%s", w->path, name);
  if (n < 0 || (size_t)n >= sizeof p)
    return fail(w->o, "Ruta temporal demasiado larga.");
  return remove_tree(w->o, p, w->depth + 1);
}
static int remove_tree(operation *o, const char *p, unsigned depth) {
  gc_stat s;
  int r;
  eraser w;
  if (!(prefixi(p, ROOT "/") || prefixi(p, RESTORE "/") || !strcmp(p, RESTORE)))
    return fail(o, "Se rechazo borrar fuera de los temporales de Guardian.");
  if (depth > 20)
    return fail(o, "Demasiados niveles en los temporales.");
  r = stat_path(o, p, &s);
  if (r <= 0)
    return r;
  if (s.type == GC_OTHER)
    return path_error(o, "No se borrara un enlace o archivo inesperado", p);
  if (s.type == GC_DIR) {
    memset(&w, 0, sizeof w);
    w.o = o;
    w.depth = depth;
    snprintf(w.path, sizeof w.path, "%s", p);
    if (o->c->fs.list(o->c->fs.user, p, erase_visit, &w))
      return path_error(o, "No se pudieron limpiar los temporales", p);
  }
  if (o->c->fs.remove(o->c->fs.user, p, s.type))
    return path_error(o, "No se pudo retirar un temporal", p);
  return 0;
}
static gc_result result(operation *o, manifest *m, int ok, int recover,
                        const char *success) {
  gc_result r;
  gc_stat st;
  memset(&r, 0, sizeof r);
  r.ok = ok;
  r.recovery_required = recover;
  if (m)
    r.info = m->info;
  if (o->c && o->c->fs.stat &&
      o->c->fs.stat(o->c->fs.user, RESTORE "/pending.txt", &st) == 1)
    r.info.restore_pending = 1;
  snprintf(r.message, sizeof r.message, "%s", o->error[0] ? o->error : success);
  return r;
}
static int check_identity(operation *o, const manifest *m) {
  return eqi(o->c->identity, m->info.identity)
             ? 0
             : fail(o, "Este respaldo pertenece a otra microSD. No se "
                       "modificara ningun archivo.");
}
gc_result gc_scan_source(gc_context *c, unsigned options) {
  operation o;
  manifest m;
  gc_result r;
  memset(&m, 0, sizeof m);
  if (setup(&o, c, 0) || manifest_alloc(&o, &m) || scan(&o, &m, options)) {
    r = result(&o, &m, 0, 0, "");
  } else
    r = result(&o, &m, 1, 0,
               "Lista del sistema preparada. No se ha escrito ningun archivo.");
  manifest_free(&m);
  destroy(&o);
  return r;
}
gc_result gc_inspect(gc_context *c, int verify) {
  operation o;
  manifest m;
  gc_result r;
  int n;
  memset(&m, 0, sizeof m);
  if (setup(&o, c, 0)) {
    r = result(&o, NULL, 0, 0, "");
    destroy(&o);
    return r;
  }
  n = load(&o, CURRENT, &m, verify);
  /* Rescue can discover the physical card identity after an interrupted
   * directory promotion, without needing to write or guess a CID. */
  if (n == 0) {
    gc_stat marker;
    if (stat_path(&o, PROMOTION, &marker) == 1 && marker.type == GC_FILE) {
      n = load(&o, PREVIOUS, &m, verify);
      if (n == 0)
        n = load(&o, STAGING, &m, verify);
      if (n == 1)
        m.info.restore_pending = 0;
    }
  }
  r = result(&o, &m, n >= 0, 0,
             n ? "Respaldo del sistema disponible."
               : "Todavia no hay un respaldo del sistema.");
  manifest_free(&m);
  destroy(&o);
  return r;
}

/* A durable promotion intent is created only after staging verifies in full. */
static int recover_promotion(operation *o) {
  manifest cur, stage, prev;
  int have, cs = 0, ss = 0, ps = 0, rolled_back = 0;
  char saved[512];
  char *intent = NULL;
  unsigned char wanted[32];
  size_t intent_size;
  gc_stat st;
  memset(&cur, 0, sizeof cur);
  memset(&stage, 0, sizeof stage);
  memset(&prev, 0, sizeof prev);
  have = stat_path(o, PROMOTION, &st);
  if (have < 0)
    return fail(o, "No se pudo comprobar la sustitucion del respaldo.");
  if (!have)
    return 0;
  if (st.type != GC_FILE)
    return fail(o, "El registro de sustitucion no es valido.");
  if (read_small(o, PROMOTION, &intent, &intent_size, 65))
    return -1;
  if (intent_size != 65 || intent[64] != '\n' || unhex(intent, 32, wanted)) {
    free(intent);
    return fail(o, "El registro de sustitucion esta incompleto; se conservan "
                   "todas las copias.");
  }
  free(intent);
  cs = load(o, CURRENT, &cur, 1);
  if (cs < 0) {
    snprintf(saved, sizeof saved, "%s", o->error);
    o->error[0] = 0;
  } else
    saved[0] = 0;
  ss = load(o, STAGING, &stage, 1);
  if (ss < 0) {
    if (!saved[0])
      snprintf(saved, sizeof saved, "%s", o->error);
    o->error[0] = 0;
  }
  ps = load(o, PREVIOUS, &prev, 1);
  if (ps < 0) {
    if (!saved[0])
      snprintf(saved, sizeof saved, "%s", o->error);
    o->error[0] = 0;
  }
  if ((cs == 1 && check_identity(o, &cur)) ||
      (ss == 1 && check_identity(o, &stage)) ||
      (ps == 1 && check_identity(o, &prev)))
    goto bad;
  if ((ss == 1 && memcmp(stage.digest, wanted, 32)) ||
      (ss != 1 && cs == 1 && memcmp(cur.digest, wanted, 32))) {
    fail(o, "El registro de sustitucion no corresponde al respaldo nuevo; se "
            "conservan todas las copias.");
    goto bad;
  }
  if (ss == 1) {
    if (cs == 1) {
      if (ps != 0) {
        fail(o, "Hay varias copias de una sustitucion interrumpida; se "
                "conservan todas para revisarlas.");
        goto bad;
      }
      if (o->c->fs.rename(o->c->fs.user, CURRENT, PREVIOUS) || sync_sd(o))
        goto io;
      cs = 0;
      ps = 1;
    } else if (cs < 0) {
      fail(o, "La copia actual esta danada; se conserva sin sobrescribir.");
      goto bad;
    }
    if (o->c->fs.rename(o->c->fs.user, STAGING, CURRENT) || sync_sd(o))
      goto io;
    manifest_free(&cur);
    if (load(o, CURRENT, &cur, 1) != 1 || check_identity(o, &cur))
      goto bad;
    cs = 1;
  } else if (cs != 1) {
    if (cs == 0 && ps == 1) {
      if (o->c->fs.rename(o->c->fs.user, PREVIOUS, CURRENT) || sync_sd(o))
        goto io;
      ps = 0;
      manifest_free(&cur);
      if (load(o, CURRENT, &cur, 1) != 1)
        goto bad;
      cs = 1;
      rolled_back = 1;
    } else {
      fail(o, saved[0] ? saved
                       : "No hay una copia completa para terminar la "
                         "sustitucion; se conservan los archivos.");
      goto bad;
    }
  }
  if (cs != 1) {
    fail(o, "No se pudo verificar el respaldo definitivo.");
    goto bad;
  }
  if (ps == 1 && remove_tree(o, PREVIOUS, 0))
    goto bad;
  /* If the new stage was damaged after the intent became durable, recovering
   * the previous verified directory is a safe rollback, not data loss. Its
   * damaged stage is retained until a later explicit backup replaces it. */
  if (ss < 0 && !rolled_back && remove_tree(o, STAGING, 0))
    goto bad;
  if (o->c->fs.remove(o->c->fs.user, PROMOTION, GC_FILE) || sync_sd(o))
    goto io;
  manifest_free(&cur);
  manifest_free(&stage);
  manifest_free(&prev);
  return 0;
io:
  fail(o, "No se pudo finalizar el cambio de respaldo; se conserva la copia "
          "anterior.");
bad:
  manifest_free(&cur);
  manifest_free(&stage);
  manifest_free(&prev);
  return -1;
}
gc_result gc_recover_backup(gc_context *c) {
  operation o;
  manifest m;
  gc_result r;
  memset(&m, 0, sizeof m);
  if (setup(&o, c, 1) || recover_promotion(&o) || load(&o, CURRENT, &m, 1) < 0)
    r = result(&o, &m, 0, 1, "");
  else
    r = result(&o, &m, 1, 0,
               "El respaldo esta listo. Esta operacion no restaura archivos "
               "del sistema.");
  manifest_free(&m);
  destroy(&o);
  return r;
}
gc_result gc_create_backup(gc_context *c, unsigned options) {
  operation o;
  manifest m, old, verified;
  gc_result r;
  gc_stat st;
  char p[512], *body = NULL, digest[66];
  size_t n;
  uint64_t available;
  unsigned i;
  int pending = 0, ok = 0;
  memset(&m, 0, sizeof m);
  memset(&old, 0, sizeof old);
  memset(&verified, 0, sizeof verified);
  if (setup(&o, c, 1))
    goto end;
  if (stat_path(&o, RESTORE "/pending.txt", &st) != 0) {
    fail(&o, "Hay una restauracion pendiente. Terminala antes de sustituir el "
             "respaldo.");
    goto end;
  }
  if (stat_path(&o, "config/NinMagicGuardian/quarantine/active/plan.txt",
                &st) != 0) {
    fail(&o, "Hay complementos apartados. Reactivalos antes de sustituir el "
             "respaldo.");
    goto end;
  }
  if (recover_promotion(&o))
    goto end;
  if (load(&o, CURRENT, &old, 1) < 0)
    goto end;
  if (old.info.present && check_identity(&o, &old))
    goto end;
  if (manifest_alloc(&o, &m) || scan(&o, &m, options))
    goto end;
  if (o.c->fs.free_space(o.c->fs.user, &available) ||
      available < m.info.bytes + 4u * 1024 * 1024) {
    fail(&o, "No hay espacio suficiente para crear y verificar el respaldo "
             "nuevo sin borrar el anterior.");
    goto end;
  }
  if (remove_tree(&o, STAGING, 0) || mkdirs(&o, STAGING "/data"))
    goto end;
  o.total = m.info.bytes;
  o.done = 0;
  for (i = 0; i < m.info.files; i++) {
    snprintf(p, sizeof p, STAGING "/data/%s", m.e[i].path);
    if (copy_file(&o, m.e[i].path, p, m.e[i].size, m.e[i].hash))
      goto end;
  }
  if (serialize(&o, &m, &body, &n))
    goto end;
  gc_sha256_buffer(body, n, m.digest);
  if (write_new(&o, STAGING "/manifest.txt", body, n))
    goto end;
  free(body);
  body = NULL;
  hex(m.digest, 32, digest);
  digest[64] = '\n';
  digest[65] = 0;
  if (write_new(&o, STAGING "/complete.sha256", digest, 65) || sync_sd(&o) ||
      load(&o, STAGING, &verified, 1) != 1 ||
      checkpoint(&o, "backup.staged", STAGING))
    goto end;
  if (write_control(&o, PROMOTION, digest, 65))
    goto end;
  pending = 1;
  if (old.info.present &&
      (o.c->fs.rename(o.c->fs.user, CURRENT, PREVIOUS) || sync_sd(&o))) {
    fail(&o, "No se pudo conservar el respaldo anterior.");
    goto end;
  }
  if (checkpoint(&o, "backup.previous", PREVIOUS))
    goto end;
  if (o.c->fs.rename(o.c->fs.user, STAGING, CURRENT) || sync_sd(&o)) {
    fail(&o,
         "No se pudo activar el respaldo nuevo; el anterior sigue conservado.");
    goto end;
  }
  if (checkpoint(&o, "backup.promoted", CURRENT))
    goto end;
  if (recover_promotion(&o))
    goto end;
  m.info.verified = 1;
  ok = 1;
end:
  r = result(&o, &m, ok, pending && !ok,
             "Respaldo nuevo verificado. Se conserva una unica copia completa "
             "del sistema.");
  free(body);
  manifest_free(&m);
  manifest_free(&old);
  manifest_free(&verified);
  destroy(&o);
  return r;
}

static int matches(operation *o, const char *p, const entry *e) {
  gc_stat st;
  unsigned char h[32];
  int r = stat_path(o, p, &st);
  if (r < 0)
    return -1;
  if (!r)
    return 0;
  if (st.type != GC_FILE)
    return path_error(o, "El destino no es un archivo regular", p);
  if (st.size != e->size)
    return 0;
  if (hash_file(o, p, e->size, h))
    return -1;
  return !memcmp(h, e->hash, 32);
}
static int restore_file(operation *o, const entry *e, unsigned index) {
  char src[512], fresh[512], old[512], plan[512], record[192], *prior = NULL;
  unsigned char h[32];
  gc_stat st;
  int planned, had = 0, old_exists, match, rc = -1;
  size_t n = 0;
  snprintf(src, sizeof src, CURRENT "/data/%s", e->path);
  snprintf(fresh, sizeof fresh, RESTORE "/new/%s", e->path);
  snprintf(old, sizeof old, RESTORE "/old/%s", e->path);
  snprintf(plan, sizeof plan, RESTORE "/plans/%04u.txt", index);
  planned = exists(o, plan, GC_FILE);
  if (planned < 0)
    return -1;
  if (!planned) {
    int r = stat_path(o, e->path, &st);
    if (r < 0)
      return path_error(o, "No se pudo comprobar el archivo de destino",
                        e->path);
    had = r == 1;
    if (had) {
      if (st.type != GC_FILE || st.size > MAX_FILE)
        return path_error(
            o, "No se sustituira un destino especial o demasiado grande",
            e->path);
      if (hash_file(o, e->path, st.size, h))
        return -1;
      hex(h, 32, record);
      n = strlen(record);
      snprintf(record + n, sizeof record - n, "|%llu\n",
               (unsigned long long)st.size);
    } else
      snprintf(record, sizeof record, "absent\n");
    if (write_control(o, plan, record, strlen(record)) ||
        checkpoint(o, "restore.before", e->path))
      return -1;
  } else {
    if (read_small(o, plan, &prior, &n, sizeof record - 1))
      return -1;
    snprintf(record, sizeof record, "%s", prior);
    free(prior);
    prior = NULL;
    had = strcmp(record, "absent\n") != 0;
    if (had) {
      char *bar = strchr(record, '|');
      uint64_t original_size;
      char *end;
      if (!bar || bar - record != 64) {
        fail(o, "El registro de recuperacion esta danado.");
        goto end;
      }
      *bar = 0;
      end = strchr(bar + 1, '\n');
      if (!end || end[1] || unhex(record, 32, h)) {
        fail(o, "El registro de recuperacion esta danado.");
        goto end;
      }
      *end = 0;
      if (number(bar + 1, &original_size) || original_size > MAX_FILE) {
        fail(o, "El registro de recuperacion esta danado.");
        goto end;
      }
      *bar = '|';
      *end = '\n';
    }
  }
  old_exists = exists(o, old, GC_FILE);
  if (old_exists < 0)
    goto end;
  match = matches(o, e->path, e);
  if (match < 0)
    goto end;
  if (match == 1) {
    rc = 0;
    goto end;
  }
  if (had && !old_exists) {
    unsigned char actual[32];
    char expected[65];
    char *bar = strchr(record, '|');
    uint64_t size;
    char *nl = strchr(bar + 1, '\n');
    memcpy(expected, record, 64);
    expected[64] = 0;
    *nl = 0;
    if (number(bar + 1, &size) || unhex(expected, 32, h)) {
      fail(o, "Registro de recuperacion invalido.");
      goto end;
    }
    *nl = '\n';
    if (hash_file(o, e->path, size, actual) || memcmp(actual, h, 32)) {
      path_error(o,
                 "El destino cambio desde que se preparo la recuperacion; se "
                 "conserva sin sobrescribir",
                 e->path);
      goto end;
    }
  }
  if (old_exists) { /* Never overwrite an unrelated live destination after an
                       interruption. */
    int dest = stat_path(o, e->path, &st);
    if (dest < 0)
      goto end;
    if (dest) {
      path_error(o,
                 "Hay un destino modificado y su copia de recuperacion; se "
                 "conservan ambos",
                 e->path);
      goto end;
    }
  }
  if (!had) {
    int dest = stat_path(o, e->path, &st);
    if (dest < 0)
      goto end;
    if (dest) {
      path_error(o,
                 "Aparecio un archivo nuevo durante la recuperacion; se "
                 "conserva sin sobrescribir",
                 e->path);
      goto end;
    }
  }
  match = matches(o, fresh, e);
  if (match < 0)
    goto end;
  if (!match) {
    int present = stat_path(o, fresh, &st);
    if (present < 0)
      goto end;
    if (present) {
      if (st.type != GC_FILE ||
          o->c->fs.remove(o->c->fs.user, fresh, GC_FILE)) {
        path_error(o, "No se pudo retirar una copia temporal incompleta",
                   fresh);
        goto end;
      }
    }
    if (copy_file(o, src, fresh, e->size, h) || memcmp(h, e->hash, 32) ||
        sync_sd(o)) {
      fail(o, "No se pudo preparar una copia verificada para restaurar.");
      goto end;
    }
    if (matches(o, fresh, e) != 1) {
      fail(o, "La copia temporal no coincide con el respaldo.");
      goto end;
    }
  }
  if (had && !old_exists) {
    if (parent_dirs(o, old) || o->c->fs.rename(o->c->fs.user, e->path, old) ||
        sync_sd(o)) {
      path_error(o, "No se pudo conservar el archivo anterior", e->path);
      goto end;
    }
  }
  if (checkpoint(o, "restore.retired", e->path))
    goto end;
  if (parent_dirs(o, e->path) ||
      o->c->fs.rename(o->c->fs.user, fresh, e->path) || sync_sd(o)) {
    path_error(o,
               "No se pudo colocar el archivo recuperado; su copia anterior "
               "esta protegida",
               e->path);
    goto end;
  }
  if (checkpoint(o, "restore.applied", e->path))
    goto end;
  if (matches(o, e->path, e) != 1) {
    path_error(o, "El archivo restaurado no se pudo verificar", e->path);
    goto end;
  }
  rc = 0;
end:
  free(prior);
  return rc;
}
gc_result gc_restore(gc_context *c) {
  operation o;
  manifest m;
  gc_result r;
  char digest[66], *saved = NULL;
  size_t n;
  uint64_t available;
  unsigned i;
  int pending = 0, ok = 0;
  memset(&m, 0, sizeof m);
  if (setup(&o, c, 1) || recover_promotion(&o) ||
      load(&o, CURRENT, &m, 1) != 1) {
    if (!o.error[0])
      fail(&o, "No hay un respaldo verificado para restaurar.");
    goto end;
  }
  if (check_identity(&o, &m))
    goto end;
  hex(m.digest, 32, digest);
  digest[64] = '\n';
  digest[65] = 0;
  pending = exists(&o, RESTORE "/pending.txt", GC_FILE);
  if (pending < 0)
    goto end;
  if (pending) {
    if (read_small(&o, RESTORE "/pending.txt", &saved, &n, 65) || n != 65 ||
        memcmp(saved, digest, 65)) {
      fail(&o, "La recuperacion pendiente no corresponde a este respaldo. Se "
               "conservan todos los archivos.");
      goto end;
    }
    free(saved);
    saved = NULL;
  } else {
    if (o.c->fs.free_space(o.c->fs.user, &available) ||
        available < m.info.bytes + 4u * 1024 * 1024) {
      fail(&o, "No hay espacio para preparar la recuperacion sin perder los "
               "archivos anteriores.");
      goto end;
    } /* A stale uncommitted work folder contains no retired system files. */
    if (remove_tree(&o, RESTORE, 0) ||
        write_control(&o, RESTORE "/pending.txt", digest, 65))
      goto end;
    pending = 1;
  }
  o.total = m.info.bytes;
  o.done = 0;
  for (i = 0; i < m.info.files; i++)
    if (restore_file(&o, &m.e[i], i))
      goto end;
  for (i = 0; i < m.info.files; i++)
    if (matches(&o, m.e[i].path, &m.e[i]) != 1) {
      path_error(&o, "Falta verificar un archivo recuperado", m.e[i].path);
      goto end;
    }
  if (checkpoint(&o, "restore.complete", RESTORE))
    goto end; /* Retire old copies only after every destination verifies. */
  if (remove_tree(&o, RESTORE, 0) || sync_sd(&o))
    goto end;
  pending = 0;
  ok = 1;
end:
  r = result(&o, &m, ok, pending && !ok,
             "Sistema de la microSD recuperado y verificado. Los juegos, "
             "partidas y archivos excluidos se han conservado.");
  free(saved);
  manifest_free(&m);
  destroy(&o);
  return r;
}
