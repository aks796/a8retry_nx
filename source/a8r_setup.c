/* a8r_setup.c -- a start from nothing but the user's A8R.apk and data folder.
 *
 * The game folder (/switch/a8retry_nx) holds:
 *   A8R.apk            the user's own APK of the mod (December build)
 *   gameloft/          the user's own data folder, as the mod's instructions
 *                      lay it out on a phone's internal storage: A8R DATA.zip's
 *                      "gameloft" folder, A8R DATA December.zip extracted over
 *                      gameloft/games/GloftA8HP (the OBB, the mod's loose files)
 *   a8retry_nx.nro     the launcher (launcher/)
 * and everything else is made here, before anything is loaded.
 *
 * THE ENGINE. The mod ships libasphalt8.so in pieces: five of its assets named
 * like music (m_*.mp3) are zips stored byte-reversed with every byte pair
 * swapped. Their entries are the 71 pieces of the library (names whose digits
 * give the order) and, under 19-character names, alternatives for the small
 * even-numbered pieces: 1-20 byte patch sites. The mod's start screen
 * (com.saveload.A8R.MainActivity) puts the library together at every PLAY:
 * the pieces in order, the alternatives of the options that are on copied
 * over theirs, and single bytes for the Quick Race numbers. Done here the same
 * way, from config.ini [mod]/[graphics]/[quick_race] with the mod's defaults,
 * and again only when the APK or those options change (.setup).
 *
 * THE DATA. PLAY also prepares the data folder: text/en.texts from the APK
 * (with the nickname written in), update.zip when the texts are older than
 * the APK, the soundpack and its song lists, gameprofiles.txt (the profile the
 * engine reads: every graphics switch is an edit of it), the texture, label
 * and colour-grading overrides the switches add or remove. Also done here,
 * the same way, at every start (only small files are written unless an
 * option changes).
 *
 * Also: classes.txt (the Java class names of the APK's dex, for FindClass),
 * and the APK's signing certificate (SUtils.retrieveBarrels hands its hash to
 * the engine).
 *
 * UPDATES FROM THE NRO are the runtime's (dcr_setup.c); new zips copied into
 * the game folder later are handed to the launcher from here. MIT.
 */
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <switch.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz/miniz.h>

#include "a8r.h"
#include "a8r_menu.h"
#include "a8r_zips.h"
#include "config.h"
#include "dcr_config.h"
#include "dcr_exefs.h"
#include "dcr_formats.h"
#include "dcr_path.h"
#include "dcr_setup.h"
#include "error.h"
#include "util.h"

static void root_path(char *out, size_t cap, const char *name) {
  snprintf(out, cap, "%s/%s", dcr_game_root(), name);
}

/* <root>/gameloft/games/GloftA8HP/<rel> */
static void data_path(char *out, size_t cap, const char *rel) {
  snprintf(out, cap, "%s/" A8R_DATA_DIR "%s%s", dcr_game_root(), rel && *rel ? "/" : "", rel ? rel : "");
}

/* <root>/data/<rel>: /data/data/<package>/<rel> */
static void int_path(char *out, size_t cap, const char *rel) {
  snprintf(out, cap, "%s/data/%s", dcr_game_root(), rel);
}

/* Setup's progress, only when there is real work (a first launch, a new APK
 * or options, a new build): the runtime's bar (dcr_setup_progress), on the
 * boot console, or on the start screen's renderer once that has the window
 * (a8r_menu.c registers it). The whole launch, in permille:
 *     0- 700  the engine put together (its five hidden assets, then written)
 *   700- 850  the Java class list
 *   850- 950  the data folder's texts updated
 *        1000 the game starts
 * An update from the NRO and new zips for the launcher show one step each,
 * before the restart. */

static long file_size(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode) ? (long)st.st_size : -1;
}

static void mkdirs(const char *dir) {
  char p[512];
  snprintf(p, sizeof p, "%s", dir);
  for (char *s = strchr(p + 6, '/'); s; s = strchr(s + 1, '/')) {
    *s = 0;
    mkdir(p, 0777);
    *s = '/';
  }
  mkdir(p, 0777);
}

static void mkdirs_for(const char *file) {
  char d[512];
  snprintf(d, sizeof d, "%s", file);
  char *s = strrchr(d, '/');
  if (s) {
    *s = 0;
    mkdirs(d);
  }
}

static uint8_t *read_whole(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *b = n >= 0 ? malloc((size_t)n + 1) : NULL;
  if (b && n > 0 && fread(b, 1, (size_t)n, f) != (size_t)n) {
    free(b);
    b = NULL;
  }
  fclose(f);
  if (b)
    b[n] = 0; /* text files are used as strings */
  *len = b ? (size_t)n : 0;
  return b;
}

/* Write to <dst>.part, then put it in place: a half-written file is never
 * mistaken for a whole one. */
static int write_atomic(const char *dst, const void *buf, size_t len) {
  char tmp[520];
  snprintf(tmp, sizeof tmp, "%s.part", dst);
  mkdirs_for(dst);
  FILE *f = fopen(tmp, "wb");
  if (!f)
    return 0;
  int ok = !len || fwrite(buf, 1, len, f) == len;
  if (fclose(f) != 0)
    ok = 0;
  if (ok) {
    unlink(dst);
    ok = rename(tmp, dst) == 0;
  }
  if (!ok)
    unlink(tmp);
  return ok;
}

/* The file already holds exactly these bytes. */
static int same_file(const char *path, const void *buf, size_t len) {
  if (file_size(path) != (long)len)
    return 0;
  size_t n;
  uint8_t *cur = read_whole(path, &n);
  int same = cur && n == len && !memcmp(cur, buf, len);
  free(cur);
  return same;
}

static int write_if_changed(const char *dst, const void *buf, size_t len) {
  if (same_file(dst, buf, len))
    return 0;
  if (!write_atomic(dst, buf, len)) {
    debugPrintf("[setup] could not write %s\n", dst);
    return 0;
  }
  return 1;
}

/* ---------------------------------------------------------------- .setup */
/* One line per made thing: "<name> <crc of what it was made from> <size>". */
#define MAX_STAMP 8
typedef struct {
  char name[32];
  unsigned long crc, size;
} Stamp;
static Stamp g_stamp[MAX_STAMP];
static int g_nstamp, g_stamp_dirty;

static void stamp_load(void) {
  char path[300];
  root_path(path, sizeof path, ".setup");
  FILE *f = fopen(path, "r");
  if (!f)
    return;
  while (g_nstamp < MAX_STAMP && fscanf(f, "%31s %lx %lu", g_stamp[g_nstamp].name,
                                        &g_stamp[g_nstamp].crc, &g_stamp[g_nstamp].size) == 3)
    g_nstamp++;
  fclose(f);
}

static Stamp *stamp_get(const char *name) {
  for (int i = 0; i < g_nstamp; i++)
    if (!strcmp(g_stamp[i].name, name))
      return &g_stamp[i];
  return NULL;
}

static void stamp_set(const char *name, unsigned long crc, unsigned long size) {
  Stamp *s = stamp_get(name);
  if (!s && g_nstamp < MAX_STAMP) {
    s = &g_stamp[g_nstamp++];
    snprintf(s->name, sizeof s->name, "%s", name);
  }
  if (s && (s->crc != crc || s->size != size)) {
    s->crc = crc;
    s->size = size;
    g_stamp_dirty = 1;
  }
}

static void stamp_save(void) {
  if (!g_stamp_dirty)
    return;
  char path[300];
  root_path(path, sizeof path, ".setup");
  FILE *f = fopen(path, "w");
  if (!f)
    return;
  for (int i = 0; i < g_nstamp; i++)
    fprintf(f, "%s %08lx %lu\n", g_stamp[i].name, g_stamp[i].crc, g_stamp[i].size);
  fclose(f);
}

/* ================================================================ the APK */
static mz_zip_archive g_apk;
static int g_apk_open;
static Mutex g_apk_lock;
static char g_apk_path[512];

const char *a8r_apk_path(void) { return g_apk_path; }

static int asset_index(const char *name) {
  char arc[256];
  snprintf(arc, sizeof arc, "assets/%s", name[0] == '/' ? name + 1 : name);
  return mz_zip_reader_locate_file(&g_apk, arc, NULL, 0);
}

uint8_t *a8r_apk_asset(const char *name, size_t *len) {
  *len = 0;
  if (!name)
    return NULL;
  mutexLock(&g_apk_lock);
  uint8_t *d = NULL;
  if (g_apk_open) {
    int i = asset_index(name);
    size_t n = 0;
    if (i >= 0 && (d = mz_zip_reader_extract_to_heap(&g_apk, (mz_uint)i, &n, 0)))
      *len = n;
  }
  mutexUnlock(&g_apk_lock);
  return d;
}

static unsigned long asset_crc(const char *name) {
  int i = asset_index(name);
  mz_zip_archive_file_stat st;
  if (i < 0 || !mz_zip_reader_file_stat(&g_apk, (mz_uint)i, &st))
    return 0;
  return (unsigned long)st.m_crc32;
}

/* ============================================================ the engine */
#define NPARTS 5 /* its assets the pieces are hidden in: a8r_apk_part() (a8r_zips.h) */
#define NPIECES 71

typedef struct {
  uint8_t *d;
  size_t n;
} Blob;

/* The option -> the pieces its alternatives replace (MainActivity.modify*). */
static const struct {
  int opt;
  const char *pieces;
} k_mod_pieces[] = {
    {MOD_DIFFICULTY, "2 20"},      {MOD_CAMERA, "4"},          {MOD_INFINITE_GEARS, "6 8"},
    {MOD_FAKE_SPEED, "10 16 18"},  {MOD_CALIPERS, "12"},       {MOD_CINEMATIC, "14"},
    {MOD_60FPS, "22 24 26"},       {MOD_METAL_SPARKS, "28 30"}, {MOD_ATTRACT, "32 38 40 42 46 58"},
    {MOD_BOTTOM_MINIMAP, "35"},    {MOD_HUD, "37"},            {MOD_SKIP_TUTORIAL, "56"},
    {MOD_KNOCKDOWN_CAMERA, "62 64 66"}, {MOD_HIGH_QUALITY, "68 70"},
};

/* Which pieces take their alternative (1..NPIECES), and the single bytes the
 * Quick Race numbers are written as (piece -> byte, -1 = none). */
static void engine_plan(int use_alt[NPIECES + 1], int byte_of[NPIECES + 1]) {
  const DcrConfig *c = dcr_config();
  for (int i = 0; i <= NPIECES; i++)
    use_alt[i] = 0, byte_of[i] = -1;
  for (unsigned k = 0; k < sizeof k_mod_pieces / sizeof k_mod_pieces[0]; k++) {
    if (!c->mod[k_mod_pieces[k].opt])
      continue;
    for (const char *p = k_mod_pieces[k].pieces; *p;) {
      int n = atoi(p);
      if (n >= 1 && n <= NPIECES)
        use_alt[n] = 1;
      while (*p && *p != ' ')
        p++;
      while (*p == ' ')
        p++;
    }
  }
  if (!c->mod[MOD_NEXT_BUTTON])
    use_alt[33] = 1;           /* verifyNextButton: '0' -> modifyNextButton */
  if (c->traffic == TRAFFIC_ALL)
    use_alt[60] = 1;           /* modifyUseTrafficCars2 */
  byte_of[44] = c->laps;       /* modifyLaps */
  byte_of[48] = c->racers;     /* modifyRacers: both */
  byte_of[50] = c->racers;
  byte_of[52] = c->knockdown_limit;  /* modifyKnockdownLimit */
  byte_of[54] = c->elimination_time; /* modifyEliminationTime */
}

/* An asset back into the zip it is: byte order reversed, then every pair of
 * bytes swapped (MainActivity.reverseFileBytes, decodeFileBytes). */
static uint8_t *decode_part(const char *name, size_t *len) {
  uint8_t *d = a8r_apk_asset(name, len);
  if (!d)
    return NULL;
  size_t n = *len;
  for (size_t i = 0, j = n ? n - 1 : 0; i < j; i++, j--) {
    uint8_t t = d[i];
    d[i] = d[j];
    d[j] = t;
  }
  for (size_t i = 0; i + 1 < n; i += 2) {
    uint8_t t = d[i];
    d[i] = d[i + 1];
    d[i + 1] = t;
  }
  return d;
}

/* The digits of a piece's name are its number (renameSplitFiles). */
static int piece_number(const char *name) {
  int v = 0, any = 0;
  for (const char *p = name; *p; p++)
    if (*p >= '0' && *p <= '9') {
      v = v * 10 + (*p - '0');
      any = 1;
      if (v > 1000)
        return -1;
    }
  return any ? v : -1;
}

static void ensure_engine(void) {
  int use_alt[NPIECES + 1], byte_of[NPIECES + 1];
  engine_plan(use_alt, byte_of);

  /* What the library is made from: the five assets and the plan. */
  mz_ulong crc = mz_crc32(0, NULL, 0);
  for (int k = 0; k < NPARTS; k++) {
    uint32_t c = (uint32_t)asset_crc(a8r_apk_part(k));
    if (!c)
      fatal_error("%s has no assets/%s.\n\n"
                  "This port needs the APK of Asphalt 8: Airborne Retry (the December\n"
                  "build, A8R APK December.zip): copy its A8R.apk to %s.",
                  g_apk_path, a8r_apk_part(k), dcr_game_root());
    crc = mz_crc32(crc, (const unsigned char *)&c, sizeof c);
  }
  crc = mz_crc32(crc, (const unsigned char *)use_alt, sizeof use_alt);
  crc = mz_crc32(crc, (const unsigned char *)byte_of, sizeof byte_of);

  char dst[300];
  root_path(dst, sizeof dst, A8R_LIB_GAME);
  Stamp *s = stamp_get(A8R_LIB_GAME);
  if (s && s->crc == (unsigned long)crc && file_size(dst) == (long)s->size)
    return; /* made from this APK with these options before */

  dcr_setup_progress("Putting the game's engine together", 0);
  debugPrintf("[setup] putting the game's engine together from A8R.apk (%s)...\n",
              s ? "the options or the APK changed" : "first launch");
  log_console_update();
  u64 t0 = armGetSystemTick();

  static Blob piece[NPIECES + 1], alt[NPIECES + 1];
  memset(piece, 0, sizeof piece);
  memset(alt, 0, sizeof alt);
  for (int k = 0; k < NPARTS; k++) {
    size_t len = 0;
    uint8_t *z = decode_part(a8r_apk_part(k), &len);
    mz_zip_archive zip;
    memset(&zip, 0, sizeof zip);
    if (!z || !mz_zip_reader_init_mem(&zip, z, len, 0))
      fatal_error("assets/%s of %s is not the mod's archive (damaged, or another build).", a8r_apk_part(k),
                  g_apk_path);
    mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; i++) {
      mz_zip_archive_file_stat st;
      if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory)
        continue;
      int num = piece_number(st.m_filename);
      int is_alt = strlen(st.m_filename) == 19; /* moveTempFiles: name length 19 */
      Blob *slot = num >= 1 && num <= NPIECES ? (is_alt ? &alt[num] : &piece[num]) : NULL;
      if (!slot) {
        debugPrintf("[setup] %s: unexpected entry %s, ignored\n", a8r_apk_part(k), st.m_filename);
        continue;
      }
      size_t sz = 0;
      slot->d = mz_zip_reader_extract_to_heap(&zip, i, &sz, 0);
      slot->n = sz;
      if (!slot->d)
        fatal_error("assets/%s of %s: entry %s is damaged.", a8r_apk_part(k), g_apk_path, st.m_filename);
    }
    mz_zip_reader_end(&zip);
    free(z);
    dcr_setup_progress("Putting the game's engine together", (k + 1) * 550 / NPARTS);
  }

  size_t total = 0;
  int alts_used = 0;
  for (int i = 1; i <= NPIECES; i++) {
    if (!piece[i].d)
      fatal_error("Piece %d of the engine is missing from %s: is it the December build of\n"
                  "Asphalt 8: Airborne Retry?", i, g_apk_path);
    if (use_alt[i]) {
      if (!alt[i].d || alt[i].n != piece[i].n)
        fatal_error("The mod's alternative for piece %d is missing from %s.", i, g_apk_path);
      free(piece[i].d);
      piece[i] = alt[i];
      alt[i].d = NULL;
      alts_used++;
    }
    if (byte_of[i] >= 0) {
      if (piece[i].n != 1)
        fatal_error("Piece %d of the engine is not the one-byte setting the mod writes.", i);
      piece[i].d[0] = (uint8_t)byte_of[i];
    }
    total += piece[i].n;
  }
  uint8_t *lib = malloc(total);
  if (!lib)
    fatal_error("Out of memory putting the engine together (%u KB).", (unsigned)(total >> 10));
  size_t off = 0;
  for (int i = 1; i <= NPIECES; i++) {
    memcpy(lib + off, piece[i].d, piece[i].n);
    off += piece[i].n;
    free(piece[i].d);
    free(alt[i].d);
    piece[i].d = alt[i].d = NULL;
  }
  if (total < 0x34 || memcmp(lib, "\177ELF", 4))
    fatal_error("The engine put together from %s is not a library (damaged APK?).", g_apk_path);
  dcr_setup_progress("Writing the game's engine", 600);
  if (!write_atomic(dst, lib, total))
    fatal_error("Could not write %s (%u KB).\n\nIs the SD card full or read-only?", dst,
                (unsigned)(total >> 10));
  free(lib);
  stamp_set(A8R_LIB_GAME, (unsigned long)crc, (unsigned long)total);
  debugPrintf("[setup] %s: %u KB, %d of the mod's alternative pieces, in %llu ms\n", A8R_LIB_GAME,
              (unsigned)(total >> 10), alts_used,
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
}

/* ============================================================ classes.txt */
static int cmp_str(const void *a, const void *b) {
  return strcmp(*(char *const *)a, *(char *const *)b);
}

static void ensure_classes(void) {
  int idx[32], n = 0;
  mz_ulong crc = mz_crc32(0, NULL, 0);
  for (int k = 1; k <= 32 && n < 32; k++) {
    char nm[32];
    if (k == 1)
      snprintf(nm, sizeof nm, "classes.dex");
    else
      snprintf(nm, sizeof nm, "classes%d.dex", k);
    int i = mz_zip_reader_locate_file(&g_apk, nm, NULL, 0);
    mz_zip_archive_file_stat st;
    if (i < 0 || !mz_zip_reader_file_stat(&g_apk, (mz_uint)i, &st))
      break;
    idx[n++] = i;
    uint32_t c = st.m_crc32;
    crc = mz_crc32(crc, (const unsigned char *)&c, sizeof c);
  }
  char dst[300];
  root_path(dst, sizeof dst, "classes.txt");
  Stamp *s = stamp_get("classes.txt");
  if (!n || (s && s->crc == (unsigned long)crc && s->size == (unsigned long)n && file_size(dst) > 0))
    return;

  dcr_setup_progress("Reading the game's Java classes", 700);
  debugPrintf("[setup] listing the Java classes of A8R.apk (%d dex file%s)...\n", n, n > 1 ? "s" : "");
  log_console_update();
  Names ns = {0};
  for (int k = 0; k < n; k++) {
    size_t len = 0;
    void *d = mz_zip_reader_extract_to_heap(&g_apk, (mz_uint)idx[k], &len, 0);
    if (d) {
      dex_names(d, len, &ns);
      free(d);
    }
  }
  if (ns.n) {
    qsort(ns.v, (size_t)ns.n, sizeof *ns.v, cmp_str);
    char tmp[320];
    snprintf(tmp, sizeof tmp, "%s.part", dst);
    FILE *f = fopen(tmp, "w");
    int written = 0;
    if (f) {
      fputs("# Java classes defined by the game's APK (names only). The wrapper's JNI\n"
            "# FindClass/Class.forName report exactly these, plus the Android framework.\n", f);
      for (int i = 0; i < ns.n; i++)
        if (i == 0 || strcmp(ns.v[i], ns.v[i - 1])) {
          fputs(ns.v[i], f);
          fputc('\n', f);
          written++;
        }
      if (fclose(f) == 0) {
        unlink(dst);
        if (rename(tmp, dst) == 0) {
          stamp_set("classes.txt", (unsigned long)crc, (unsigned long)n);
          debugPrintf("[setup] classes.txt: %d Java class names\n", written);
        }
      }
    }
  }
  for (int i = 0; i < ns.n; i++)
    free(ns.v[i]);
  free(ns.v);
}

/* ========================================================= the signature */
/* PackageInfo.signatures[0] is the signing certificate's DER encoding, and
 * Signature.hashCode() is Arrays.hashCode of those bytes. The certificate is
 * the first of SignedData.certificates in the META-INF .RSA file (PKCS #7). */
static int32_t g_sig_hash;

int32_t a8r_signature_hash(void) { return g_sig_hash; }

/* One DER TLV at p (end e): *tag, content start and length; the TLV's end. */
static const uint8_t *der(const uint8_t *p, const uint8_t *e, uint8_t *tag, const uint8_t **c,
                          size_t *clen) {
  if (!p || p + 2 > e)
    return NULL;
  *tag = p[0];
  size_t n = p[1], hl = 2;
  if (n & 0x80) {
    int k = n & 0x7f;
    if (k < 1 || k > 4 || p + 2 + k > e)
      return NULL;
    n = 0;
    for (int i = 0; i < k; i++)
      n = n << 8 | p[2 + i];
    hl += (size_t)k;
  }
  if (p + hl + n > e)
    return NULL;
  *c = p + hl;
  *clen = n;
  return p + hl + n;
}

static void read_signature(void) {
  mz_uint n = mz_zip_reader_get_num_files(&g_apk);
  for (mz_uint i = 0; i < n; i++) {
    char nm[256];
    mz_zip_reader_get_filename(&g_apk, i, nm, sizeof nm);
    size_t l = strlen(nm);
    if (strncmp(nm, "META-INF/", 9) || l < 4 ||
        (strcasecmp(nm + l - 4, ".RSA") && strcasecmp(nm + l - 4, ".DSA") && strcasecmp(nm + l - 3, ".EC")))
      continue;
    size_t len = 0;
    uint8_t *d = mz_zip_reader_extract_to_heap(&g_apk, i, &len, 0);
    if (!d)
      continue;
    const uint8_t *e = d + len, *c, *sd, *cert;
    size_t cl, sdl, certl;
    uint8_t t;
    /* ContentInfo { OID, [0] { SignedData { version, digestAlgs, contentInfo, [0] certs } } } */
    if (der(d, e, &t, &c, &cl) && t == 0x30) {
      const uint8_t *ce = c + cl, *q = c;
      const uint8_t *x;
      size_t xl;
      q = der(q, ce, &t, &x, &xl);                          /* contentType */
      if (q && der(q, ce, &t, &x, &xl) && t == 0xA0 &&      /* [0] EXPLICIT */
          der(x, x + xl, &t, &sd, &sdl) && t == 0x30) {     /* SignedData */
        const uint8_t *se = sd + sdl, *r = sd;
        for (int k = 0; k < 3 && r; k++)
          r = der(r, se, &t, &x, &xl);                      /* version, digestAlgs, contentInfo */
        if (r && der(r, se, &t, &x, &xl) && t == 0xA0) {    /* [0] IMPLICIT certificates */
          const uint8_t *end = der(x, x + xl, &t, &cert, &certl);
          if (end && t == 0x30) {
            int32_t h = 1;
            for (const uint8_t *b = x; b < end; b++)
              h = 31 * h + (int8_t)*b;
            g_sig_hash = h;
            debugPrintf("[setup] %s: signing certificate %u bytes, hash 0x%08x\n", nm,
                        (unsigned)(end - x), (unsigned)h);
          }
        }
      }
    }
    free(d);
    if (g_sig_hash)
      return;
  }
  debugPrintf("[setup] no signing certificate found in A8R.apk\n");
}

/* ============================================================== the data */
/* Copy an asset of the APK to a file, if the file differs. 1 if written. */
static int copy_asset(const char *asset, const char *dst) {
  size_t len;
  uint8_t *d = a8r_apk_asset(asset, &len);
  if (!d) {
    debugPrintf("[setup] A8R.apk has no assets/%s\n", asset);
    return 0;
  }
  int w = write_if_changed(dst, d, len);
  free(d);
  return w;
}

/* Delete the files of dir whose names start with prefix (the mod's delete()
 * over listFiles). Returns how many. */
static int delete_prefixed(const char *dir, const char *prefix) {
  DIR *dp = opendir(dir);
  if (!dp)
    return 0;
  char names[64][256];
  int n = 0, removed = 0;
  struct dirent *e;
  while ((e = readdir(dp)) && n < 64)
    if (!strncmp(e->d_name, prefix, strlen(prefix)))
      snprintf(names[n++], sizeof names[0], "%s", e->d_name);
  closedir(dp);
  for (int i = 0; i < n; i++) {
    char p[800];
    snprintf(p, sizeof p, "%s/%s", dir, names[i]);
    removed += unlink(p) == 0;
  }
  return removed;
}

static int has_prefixed(const char *dir, const char *prefix) {
  DIR *dp = opendir(dir);
  if (!dp)
    return 0;
  struct dirent *e;
  int found = 0;
  while (!found && (e = readdir(dp)))
    found = !strncmp(e->d_name, prefix, strlen(prefix));
  closedir(dp);
  return found;
}

/* Every file of a zip asset into dir (MainActivity.extract). */
static int extract_asset_zip(const char *asset, const char *dir) {
  size_t len;
  uint8_t *z = a8r_apk_asset(asset, &len);
  mz_zip_archive zip;
  memset(&zip, 0, sizeof zip);
  if (!z || !mz_zip_reader_init_mem(&zip, z, len, 0)) {
    free(z);
    debugPrintf("[setup] assets/%s: not a zip\n", asset);
    return 0;
  }
  int files = 0;
  mz_uint n = mz_zip_reader_get_num_files(&zip);
  for (mz_uint i = 0; i < n; i++) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, i, &st) || strstr(st.m_filename, ".."))
      continue;
    char p[800];
    snprintf(p, sizeof p, "%s/%s", dir, st.m_filename);
    if (st.m_is_directory) {
      mkdirs(p);
      continue;
    }
    size_t sz = 0;
    void *d = mz_zip_reader_extract_to_heap(&zip, i, &sz, 0);
    if (d && write_atomic(p, d, sz))
      files++;
    free(d);
  }
  mz_zip_reader_end(&zip);
  free(z);
  return files;
}

/* Replace every occurrence of `from` in a malloc'd string (*s). 1 if changed. */
static int str_replace(char **s, const char *from, const char *to) {
  size_t fl = strlen(from), tl = strlen(to);
  int count = 0;
  for (char *p = strstr(*s, from); p; p = strstr(p + fl, from))
    count++;
  if (!count)
    return 0;
  size_t n = strlen(*s) + (size_t)count * (tl > fl ? tl - fl : 0) + 1;
  char *out = malloc(n), *o = out;
  if (!out)
    return 0;
  for (const char *p = *s;;) {
    const char *m = strstr(p, from);
    if (!m) {
      strcpy(o, p);
      break;
    }
    memcpy(o, p, (size_t)(m - p));
    o += m - p;
    memcpy(o, to, tl);
    o += tl;
    p = m + fl;
  }
  free(*s);
  *s = out;
  return 1;
}

/* MainActivity.readFile joins lines without their line breaks; the mod's
 * files are single-line JSON/XML, but do the same. */
static char *read_text(const char *path) {
  size_t n;
  char *t = (char *)read_whole(path, &n);
  if (!t)
    return NULL;
  char *o = t;
  for (size_t i = 0; i < n; i++)
    if (t[i] != '\n' && t[i] != '\r')
      *o++ = t[i];
  *o = 0;
  return t;
}

/* --- the profile: gameprofiles.txt, edited as the start screen does ---- */
static int prepare_profile(void) {
  const DcrConfig *c = dcr_config();
  size_t len;
  char *t = (char *)a8r_apk_asset("gameprofiles.txt", &len);
  if (!t) {
    debugPrintf("[setup] A8R.apk has no gameprofiles.txt\n");
    return 0;
  }
  char *s = malloc(len + 1);
  memcpy(s, t, len);
  s[len] = 0;
  free(t);
  /* getGameProfiles copies it; the verify* steps edit it, in PLAY's order */
  if (c->aa == AA_NONE || c->aa == AA_HEAVY)
    str_replace(&s, "\"scaleAABuffer\": 1", "\"scaleAABuffer\": 0");
  if (c->aa == AA_HEAVY) {
    str_replace(&s, "\"useAAInGameplay\": false,", "\"useAAInGameplay\": true,");
    str_replace(&s, "\"useAAInMenu\": false,", "\"useAAInMenu\": true,");
  }
  char v[48];
  snprintf(v, sizeof v, "\"roadTextureAnisotropy\": %d", c->anisotropy);
  str_replace(&s, "\"roadTextureAnisotropy\": 16", v);
  snprintf(v, sizeof v, "\"scaleDisplay\": %d", c->resolution_pct);
  str_replace(&s, "\"scaleDisplay\": 100", v);
  static const struct { int gfx; const char *key; } offs[] = {
      {GFX_BREAKABLES, "useBreakables"},     {GFX_SHADOWS, "useShadows"},
      {GFX_ROAD_REFLECTION, "useRoadReflection"}, {GFX_AI_PARTICLES, "useAICarParticles"},
      {GFX_PARTICLES, "useCarParticles"},    {GFX_MOTION_BLUR, "useMotionBlur"},
      {GFX_GLASS_CRACK, "useGlassCrackPFX"}, {GFX_SKID_MARKS, "useSkidMarks"},
      {GFX_ROAD_SPECULAR, "useRoadSpecular"}, {GFX_CAR_REFLECTION, "useParaboloidReflection"},
      {GFX_LENS_FLARE, "useLensflare"},      {GFX_CAR_DIRT, "useCarDirt"},
      {GFX_ANAMORPHIC_GLOWS, "useAnamorphicGlows"}, {GFX_CAR_SPECULAR, "useCarSpecular"},
  };
  for (unsigned i = 0; i < sizeof offs / sizeof offs[0]; i++)
    if (!c->gfx[offs[i].gfx]) {
      char on[64], off[64];
      snprintf(on, sizeof on, "\"%s\": true", offs[i].key);
      snprintf(off, sizeof off, "\"%s\": false", offs[i].key);
      str_replace(&s, on, off);
    }
  if (c->texture_filtering == TEX_NONE)
    str_replace(&s, "\"defaultTextureFiltering\": 2", "\"defaultTextureFiltering\": 0");
  else if (c->texture_filtering == TEX_BILINEAR)
    str_replace(&s, "\"defaultTextureFiltering\": 2", "\"defaultTextureFiltering\": 1");
  if (c->traffic == TRAFFIC_NONE)
    str_replace(&s, "\"useTrafficCars\": true", "\"useTrafficCars\": false");
  char dst[300];
  int_path(dst, sizeof dst, "files/gameprofiles.txt");
  int w = write_if_changed(dst, s, strlen(s));
  free(s);
  return w;
}

/* --- the soundpack: the OST's song lists (disableOSTAll / enableOSTAll) -- */
static const char *const k_ost_base[] = {
    "m_deadmau5_professional_griefers,m_gemini_fire_inside",
    "m_bloc_party_ares_edit,m_mutemath_blood_pressure",
    "m_monsta_holdin_on,m_nero_etude_edit",
    "m_breton_the_commission,m_celldweller_pulsar",
};
static const char *const k_ost_more[] = {
    ",m_the_bloody_beetroots_rocksteady,m_the_crystal_method_play_for_real_edit,m_vitalic_stamina,"
    "m_hadouken_mic_check_camo_krooked_remix,m_mord_fustang_windwaker,m_Krubb_Wenkroist_Bad_Weekend,"
    "m_the_crystal_method_over_it,m_martin_garrix_animals",
    ",m_awolnation_burn_it_down,m_kasabian_underdog,m_trentemoller_silver_surfer_ghost_rider_go,"
    "m_queen_of_the_stone_age_go_with_the_flow,m_silversun_pickups_cannibal,"
    "m_band_of_skulls_asleep_at_the_wheel",
    ",m_svidden_we_are,m_mind_vortex_arc,m_far_too_loud_lightbringer,m_the_qemists_be_electric,"
    "m_dj_gontran_burning,m_dj_gontran_outrun",
    ",m_celldweller_through_the_gates,m_Krubb_Wenkroist_bleach,m_dj_Gontran_Chemistry,"
    "m_DJ_Dubai_Vodka_Aspirin,m_dj_gontran_moby_glitch,m_dj_gontran_phantasmagorical",
};

static int prepare_soundpack(void) {
  char path[300];
  data_path(path, sizeof path, "soundpack/soundpack");
  int w = 0;
  if (file_size(path) <= 0) /* verifySoundpack -> getSoundpack */
    w |= copy_asset("soundpack", path);
  char *s = read_text(path);
  if (!s)
    return w;
  size_t before = strlen(s);
  char *orig = strdup(s);
  for (int i = 0; i < 4; i++) {
    char a[512], b[512];
    if (dcr_config()->gfx[GFX_OST]) { /* enableOSTAll */
      snprintf(a, sizeof a, "%s\"/>", k_ost_base[i]);
      snprintf(b, sizeof b, "%s%s\"/>", k_ost_base[i], k_ost_more[i]);
    } else { /* disableOSTAll */
      snprintf(a, sizeof a, "%s\"/>", k_ost_more[i]);
      snprintf(b, sizeof b, "\"/>");
    }
    str_replace(&s, a, b);
  }
  if (strcmp(s, orig) || strlen(s) != before)
    w |= write_if_changed(path, s, strlen(s));
  free(orig);
  free(s);
  return w;
}

/* --- the texts: en.texts from the APK, with the nickname (getNickname,
 * setNickname), after update.zip when the folder's texts are older --- */
static int prepare_texts(void) {
  char path[300], dir[300];
  data_path(path, sizeof path, "text/en.texts");
  data_path(dir, sizeof dir, "");
  int w = 0;
  size_t n = 0;
  uint8_t *cur = read_whole(path, &n);
  int current = cur && memmem(cur, n, "2025M4D25", 9) != NULL;
  free(cur);
  if (!current) { /* updatePatch */
    dcr_setup_progress("Updating the game's texts", 850);
    debugPrintf("[setup] the data folder's texts are older than the APK: applying its update.zip\n");
    log_console_update();
    w |= extract_asset_zip("update.zip", dir) > 0;
  }
  size_t len;
  uint8_t *t = a8r_apk_asset("en.texts", &len);
  if (!t)
    return w;
  static const char kDriver[16] = "Driver\0\0\0\0\0\0\0\0\0";
  uint8_t *p = memmem(t, len, kDriver, sizeof kDriver);
  if (p) {
    char name[16] = {0};
    memcpy(name, dcr_config()->nickname, strnlen(dcr_config()->nickname, 16));
    memcpy(p, name, 16);
  }
  w |= write_if_changed(path, t, len);
  free(t);
  return w;
}

/* --- textures, labels, music: the switches that add or remove files --- */
static void prepare_overrides(void) {
  const DcrConfig *c = dcr_config();
  char tex[300], models[300], sd[300], p[400];
  data_path(tex, sizeof tex, "textures_android");
  data_path(models, sizeof models, "models");
  data_path(sd, sizeof sd, "");
  mkdirs(tex);
  mkdirs(models);

  /* verifyCarReflection: off -> the black paraboloid texture */
  snprintf(p, sizeof p, "%s/menu_paraboloid_glass.tga", tex);
  if (!c->gfx[GFX_CAR_REFLECTION])
    copy_asset("menu_paraboloid_glass.tga", p);
  else
    unlink(p);

  /* verifyClassicLicensePlate */
  static const char *const plates_new[] = {"car_license_plates_new.tga", "car_license_plates_new_mk.tga"};
  static const char *const plates[] = {"car_license_plates.tga", "car_license_plates_mk.tga"};
  const char *const *add = c->gfx[GFX_CLASSIC_PLATE] ? plates_new : plates;
  const char *const *del = c->gfx[GFX_CLASSIC_PLATE] ? plates : plates_new;
  for (int i = 0; i < 2; i++) {
    snprintf(p, sizeof p, "%s/%s", tex, add[i]);
    copy_asset(add[i], p);
    snprintf(p, sizeof p, "%s/%s", tex, del[i]);
    unlink(p);
  }

  /* verifyLUT ("Natural colors"), verifyBokeh, verifyLabels ("3D letters"),
   * verifyOST: a zip of override files in, or those files out */
  if (c->gfx[GFX_NATURAL_COLORS]) {
    if (!has_prefixed(tex, "fx_lut"))
      extract_asset_zip("fx_lut_new.zip", tex);
  } else {
    delete_prefixed(tex, "fx_lut");
  }
  if (!c->gfx[GFX_BOKEH]) {
    if (!has_prefixed(tex, "fx_special_bokeh"))
      extract_asset_zip("fx_special_bokeh.zip", tex);
  } else {
    delete_prefixed(tex, "fx_special_bokeh");
  }
  if (!c->gfx[GFX_3D_LETTERS]) {
    if (!has_prefixed(models, "label"))
      extract_asset_zip("labels.zip", models);
  } else {
    delete_prefixed(models, "label");
  }
  if (!c->gfx[GFX_OST]) {
    if (!has_prefixed(sd, "m_"))
      extract_asset_zip("music.zip", sd);
  } else {
    delete_prefixed(sd, "m_");
  }
}

static void ensure_data(void) {
  char obb[300], dir[300];
  data_path(obb, sizeof obb, A8R_OBB_NAME);
  data_path(dir, sizeof dir, "");
  long sz = file_size(obb);
  if (sz <= 0)
    fatal_error("The game's data is missing: %s\n\n"
                "Copy A8R DATA.zip and A8R DATA December.zip into %s\n"
                "as they are, then start the game again: the launcher installs them.",
                obb, dcr_game_root());
  debugPrintf("[setup] data folder %s: OBB %ld MB\n", dir, sz >> 20);

  /* createSubdirectories, and the Android app's own folders */
  static const char *const sub[] = {"music/dubstep", "music/electro", "music/menu", "music/outro",
                                    "music/rock", "save"};
  for (unsigned i = 0; i < sizeof sub / sizeof sub[0]; i++) {
    char p[400];
    snprintf(p, sizeof p, "%s/%s", dir, sub[i]);
    mkdirs(p);
  }

  u64 t0 = armGetSystemTick();
  int w = 0;
  w |= prepare_texts();
  w |= prepare_soundpack();
  w |= prepare_profile();
  prepare_overrides();

  /* getGameloftSharing; Game.ExtractAssets (assets/extract/ into the folder) */
  char p[400];
  int_path(p, sizeof p, "databases/gameloft_sharing");
  if (file_size(p) <= 0)
    copy_asset("gameloft_sharing", p);
  static const char *const extract[] = {"events.json", "initialfeed.dat"};
  for (unsigned i = 0; i < sizeof extract / sizeof extract[0]; i++) {
    char a[64];
    snprintf(a, sizeof a, "extract/%s", extract[i]);
    snprintf(p, sizeof p, "%s/%s", dir, extract[i]);
    if (file_size(p) <= 0)
      copy_asset(a, p);
  }
  debugPrintf("[setup] data prepared%s (%llu ms)\n", w ? " (files updated)" : "",
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
}

/* ================================================================ entry */
void a8r_setup(const char *apk) {
  mutexInit(&g_apk_lock);
  snprintf(g_apk_path, sizeof g_apk_path, "%s", apk);
  memset(&g_apk, 0, sizeof g_apk);
  if (!mz_zip_reader_init_file(&g_apk, apk, 0))
    fatal_error("%s is missing or unreadable.\n\n"
                "Copy your A8R.apk (from A8R APK December.zip) there: the game's engine\n"
                "is put together from it on the first launch.",
                apk);
  g_apk_open = 1; /* kept open: the Java side reads its assets during play */
  stamp_load();
  ensure_engine();
  ensure_classes();
  read_signature();
  stamp_save();
  ensure_data();
  rt_setup_finish(); /* "Starting the game", if anything was shown (dcr_setup.c) */
}

/* ------------------------------------------------ zips for the launcher */
/* The launcher installs the A8R zips copied into the game folder
 * (launcher/source/zips.c), but once this icon starts the game program
 * instead of the launcher, zips copied there later would go unnoticed. When
 * there are any, this steps aside -- removes its own override -- and
 * restarts: the icon starts the launcher again, which installs them and then
 * this. The launcher removes the zips it installs, so one still here after
 * being handed over was not taken, and is not handed over again (.zips). */
static int is_mod_zip(const char *path) {
  mz_zip_archive z;
  memset(&z, 0, sizeof z);
  if (!mz_zip_reader_init_file(&z, path, 0))
    return 0;
  A8rZipScan s = {0};
  char name[512];
  for (mz_uint i = 0, n = mz_zip_reader_get_num_files(&z); i < n; i++)
    if (mz_zip_reader_get_filename(&z, i, name, sizeof name) < sizeof name) {
      a8r_zip_normalize(name);
      a8r_zip_scan(&s, name);
    }
  mz_zip_reader_end(&z);
  return a8r_zip_kind(&s) != A8R_ZIP_OTHER;
}

void dcr_setup_zips_to_launcher(void) {
  u64 tid = 0;
  if (R_FAILED(svcGetInfo(&tid, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0)) || !exefs_is_forwarder_tid(tid))
    return;
  char ovr[128], marker[300], key[300] = "";
  snprintf(ovr, sizeof ovr, "sdmc:/atmosphere/contents/%016llX/exefs.nsp", (unsigned long long)tid);
  root_path(marker, sizeof marker, ".zips");
  if (file_size(ovr) <= 0)
    return;

  DIR *d = opendir(dcr_game_root());
  struct dirent *e;
  while (d && !key[0] && (e = readdir(d))) {
    size_t n = strlen(e->d_name);
    if (n < 5 || strcasecmp(e->d_name + n - 4, ".zip") || !strncmp(e->d_name, "._", 2))
      continue;
    char p[320];
    root_path(p, sizeof p, e->d_name);
    if (is_mod_zip(p))
      snprintf(key, sizeof key, "%s %ld", e->d_name, file_size(p));
  }
  if (d)
    closedir(d);
  if (!key[0]) {
    unlink(marker);
    return;
  }
  char last[300] = "";
  FILE *mf = fopen(marker, "r");
  if (mf) {
    if (!fgets(last, sizeof last, mf))
      last[0] = 0;
    last[strcspn(last, "\n")] = 0;
    fclose(mf);
  }
  if (!strcmp(last, key)) {
    debugPrintf("[setup] zip %s: handed to the launcher already, still here -- not again (delete %s to "
                "retry)\n", key, marker);
    return;
  }
  if ((mf = fopen(marker, "w"))) {
    fprintf(mf, "%s\n", key);
    fclose(mf);
  }
  debugPrintf("[setup] zip %s: restarting into the launcher to install it...\n", key);
  rt_setup_restart_into_launcher("New game files: restarting to install them"); /* dcr_setup.c */
}
