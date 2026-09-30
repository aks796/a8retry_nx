/* zips.c -- installs the game's files from the zips the A8R mod ships in.
 *
 * Copy the mod's three zips (../../source/a8r_zips.h) into /switch/a8retry_nx as
 * they are: this extracts them there -- the APK and the data first, then the
 * December update over the data -- and removes each zip once all of it is in
 * place, so its 2 GB are not on the card twice. With several APKs (an older
 * build's zip next to the December one) the newest is installed and the
 * others are renamed *.unused, so that they are not installed later. Zips
 * that are not the mod's are left alone.
 *
 * Every file is written as <name>.part and renamed when it is complete, so an
 * interrupted install never leaves a truncated file under the real name; the
 * zip it came from stays, and the next launch installs it again. MIT.
 */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <switch.h>
#include <minizip/unzip.h>

#include "a8r_zips.h"
#include "zips.h"

#define MAX_ZIPS 16
#define CHUNK (1u << 20)
#define KIND_UNREADABLE (-1)

typedef struct {
  char name[256];  /* in GAME_DIR */
  int kind;        /* A8R_ZIP_*, KIND_UNREADABLE */
  A8rZipScan scan;
  char apk[512];   /* its APK entry (A8R.apk when it has several) */
  uLong apk_date;  /* that entry's DOS date and time: the newest APK is used */
  uint64_t bytes;  /* what installing it writes */
  int unused;      /* its APK is older than another zip's */
} Zip;

typedef struct {
  uint64_t done, total; /* over all the zips being installed */
  u64 last;
  const char *zip;
} Progress;

static Zip g_zip[MAX_ZIPS];
static int g_nzip;
static uint8_t *g_buf;
static char g_why[600]; /* what went wrong */

static void in_game_dir(char *out, size_t cap, const char *name) { snprintf(out, cap, GAME_DIR "/%s", name); }

static const char *base_name(const char *p) {
  const char *s = strrchr(p, '/');
  return s ? s + 1 : p;
}

/* minizip reads through stdio 16 KB at a time: a bigger buffer makes fewer,
 * larger reads of the SD card. */
static open64_file_func g_open64;
static voidpf ZCALLBACK open_buffered(voidpf opaque, const void *name, int mode) {
  FILE *f = g_open64(opaque, name, mode);
  if (f)
    setvbuf(f, NULL, _IOFBF, 256 * 1024);
  return f;
}

static unzFile open_zip(const char *name) {
  char p[320];
  in_game_dir(p, sizeof p, name);
  zlib_filefunc64_def ff;
  fill_fopen64_filefunc(&ff);
  g_open64 = ff.zopen64_file;
  ff.zopen64_file = open_buffered;
  return unzOpen2_64(p, &ff);
}

/* The next entry's name, normalized; 0 at the end (or on damage: rc says). */
static int next_entry(unzFile uf, int *rc, int first, char *name, size_t cap, unz_file_info64 *fi) {
  for (;;) {
    *rc = first ? unzGoToFirstFile(uf) : unzGoToNextFile(uf);
    first = 0;
    if (*rc != UNZ_OK)
      return 0;
    if ((*rc = unzGetCurrentFileInfo64(uf, fi, name, cap, NULL, 0, NULL, 0)) != UNZ_OK)
      return 0;
    if (fi->size_filename < cap) {
      a8r_zip_normalize(name);
      return 1;
    }
  }
}

/* Where entry `name` of z goes, relative to GAME_DIR (ending in '/' for a
 * folder); 0 when it is left out. */
static int dest(const Zip *z, const char *name, char *out, size_t cap) {
  switch (a8r_zip_entry(name)) {
  case A8R_ENTRY_JUNK:
    return 0;
  case A8R_ENTRY_APK:
    if (strcmp(name, z->apk))
      return 0;
    snprintf(out, cap, "A8R.apk");
    return 1;
  case A8R_ENTRY_GAMELOFT:
    snprintf(out, cap, "%s", a8r_zip_component(name, "gameloft"));
    return 1;
  default: { /* beside a gameloft/ folder: for the update, the data folder's */
    const char *in = a8r_zip_in_data(name);
    if (z->kind != A8R_ZIP_UPDATE || !*in)
      return 0;
    snprintf(out, cap, A8R_ZIP_DATA_DIR "/%s", in);
    return 1;
  }
  }
}

static void scan(Zip *z) {
  unzFile uf = open_zip(z->name);
  if (!uf) {
    z->kind = KIND_UNREADABLE;
    return;
  }
  char name[512];
  unz_file_info64 fi;
  uint64_t apk_bytes = 0, gameloft_bytes = 0, other_bytes = 0;
  int rc;
  for (int first = 1; next_entry(uf, &rc, first, name, sizeof name, &fi); first = 0) {
    switch (a8r_zip_scan(&z->scan, name)) {
    case A8R_ENTRY_APK:
      if (!z->apk[0] || !strcasecmp(base_name(name), "A8R.apk")) {
        snprintf(z->apk, sizeof z->apk, "%s", name);
        z->apk_date = fi.dosDate;
        apk_bytes = fi.uncompressed_size;
      }
      break;
    case A8R_ENTRY_GAMELOFT:
      gameloft_bytes += fi.uncompressed_size;
      break;
    case A8R_ENTRY_JUNK:
      break;
    default:
      other_bytes += fi.uncompressed_size;
    }
  }
  unzClose(uf);
  z->kind = rc == UNZ_END_OF_LIST_OF_FILE ? a8r_zip_kind(&z->scan) : KIND_UNREADABLE;
  z->bytes = z->kind == A8R_ZIP_BASE ? apk_bytes + gameloft_bytes : z->kind == A8R_ZIP_UPDATE ? other_bytes : 0;
}

static int installs(const Zip *z) { return (z->kind == A8R_ZIP_BASE || z->kind == A8R_ZIP_UPDATE) && !z->unused; }

static int64_t sd_free(void) {
  FsFileSystem *fs = fsdevGetDeviceFileSystem("sdmc");
  s64 n = -1;
  if (!fs || R_FAILED(fsFsGetFreeSpace(fs, "/", &n)))
    return -1;
  return n;
}

static void mkdirs(char *path) {
  for (char *p = path + sizeof "sdmc:/" - 1; *p; p++)
    if (*p == '/') {
      *p = 0;
      mkdir(path, 0777);
      *p = '/';
    }
  mkdir(path, 0777);
}

static int g_bar_on;

void launcher_bar(const char *what, int permille) {
  static char last[96];
  static int last_pct = -1;
  permille = permille < 0 ? 0 : permille > 1000 ? 1000 : permille;
  int pct = permille / 10;
  if (g_bar_on && pct == last_pct && !strncmp(what, last, sizeof last - 1))
    return;
  g_bar_on = 1;
  last_pct = pct;
  snprintf(last, sizeof last, "%s", what);
  enum { COLS = 80, BAR = 56 };
  int fill = permille * BAR / 1000;
  char bar[BAR + 1];
  for (int i = 0; i < BAR; i++)
    bar[i] = i < fill ? '#' : '-';
  bar[BAR] = 0;
  static const char title[] = "Asphalt 8: Airborne Retry";
  static const char note[] = "Getting the game ready (after an install or an update)";
  printf("\x1b[2J");
  printf("\x1b[18;%dH\x1b[32;1m%s\x1b[0m", (COLS - (int)sizeof title + 1) / 2 + 1, title);
  printf("\x1b[20;%dH%s", (COLS - (int)sizeof note + 1) / 2 + 1, note);
  printf("\x1b[23;%dH[\x1b[32m%s\x1b[0m] %3d%%", (COLS - BAR - 7) / 2 + 1, bar, pct);
  int wl = (int)strlen(last);
  printf("\x1b[25;%dH%s", wl < COLS ? (COLS - wl) / 2 + 1 : 1, last);
  fflush(stdout);
  consoleUpdate(NULL);
}

void launcher_bar_off(void) {
  if (!g_bar_on)
    return;
  g_bar_on = 0;
  printf("\x1b[2J\x1b[1;1HAsphalt 8: Airborne Retry for Nintendo Switch -- launcher\n\n");
  consoleUpdate(NULL);
}

int launcher_bar_on(void) { return g_bar_on; }

static void progress(Progress *p, int force) {
  u64 now = armGetSystemTick();
  if (!force && armTicksToNs(now - p->last) < 200000000ull)
    return;
  p->last = now;
  char what[96];
  snprintf(what, sizeof what, "Extracting the game data: %.40s (%llu / %llu MB)", p->zip ? p->zip : "",
           (unsigned long long)(p->done >> 20), (unsigned long long)(p->total >> 20));
  launcher_bar(what, p->total ? (int)(p->done * 1000 / p->total) : 1000);
}

/* The current entry of uf, as full (written as full.part, then renamed). */
static int extract_one(unzFile uf, const Zip *z, const char *entry, const char *full, Progress *pg) {
  char part[720];
  snprintf(part, sizeof part, "%s.part", full);
  if (unzOpenCurrentFile(uf) != UNZ_OK) {
    snprintf(g_why, sizeof g_why, "%s is damaged (at %s): download it again.", z->name, entry);
    return -1;
  }
  FILE *f = fopen(part, "wb");
  int n = 0, ok = f != NULL, quit = 0;
  while (ok && (n = unzReadCurrentFile(uf, g_buf, CHUNK)) > 0) {
    ok = fwrite(g_buf, 1, (size_t)n, f) == (size_t)n;
    pg->done += (unsigned)n;
    progress(pg, 0);
    if (!appletMainLoop())
      quit = 1, ok = 0;
  }
  int damaged = ok && n < 0;
  if (unzCloseCurrentFile(uf) == UNZ_CRCERROR && ok)
    damaged = 1;
  if (f && fclose(f) != 0)
    ok = 0;
  if (ok && !damaged) {
    remove(full);
    if (rename(part, full) == 0)
      return 0;
  }
  remove(part);
  if (quit)
    snprintf(g_why, sizeof g_why, "Stopped.");
  else if (damaged)
    snprintf(g_why, sizeof g_why, "%s is damaged (at %s): download it again.", z->name, entry);
  else
    snprintf(g_why, sizeof g_why, "Could not write %s.\nIs the SD card full or read-only?", full);
  return -1;
}

static int extract(const Zip *z, Progress *all) {
  unzFile uf = open_zip(z->name);
  if (!uf) {
    snprintf(g_why, sizeof g_why, "Could not open %s.", z->name);
    return -1;
  }
  Progress pg = *all;
  uint64_t start = pg.done;
  pg.zip = z->name;
  char name[512], rel[600], full[700];
  unz_file_info64 fi;
  int rc, err = 0;
  for (int first = 1; !err && next_entry(uf, &rc, first, name, sizeof name, &fi); first = 0) {
    if (!dest(z, name, rel, sizeof rel))
      continue;
    snprintf(full, sizeof full, GAME_DIR "/%s", rel);
    size_t n = strlen(full);
    if (full[n - 1] == '/') {
      full[n - 1] = 0;
      mkdirs(full);
      continue;
    }
    char *slash = strrchr(full, '/');
    *slash = 0;
    mkdirs(full);
    *slash = '/';
    err = extract_one(uf, z, name, full, &pg);
  }
  unzClose(uf);
  if (!err && rc != UNZ_END_OF_LIST_OF_FILE) {
    snprintf(g_why, sizeof g_why, "%s is damaged: download it again.", z->name);
    err = -1;
  }
  if (!err)
    pg.done = start + z->bytes;
  progress(&pg, 1);
  all->done = pg.done;
  all->last = pg.last;
  return err;
}

/* old -> old<suffix>, replacing one left by an earlier launch. */
static int rename_to(const char *name, const char *suffix) {
  char from[320], to[340];
  in_game_dir(from, sizeof from, name);
  snprintf(to, sizeof to, "%s%s", from, suffix);
  remove(to);
  return rename(from, to);
}

int zips_install(void) {
  DIR *d = opendir(GAME_DIR);
  if (!d)
    return 0;
  struct dirent *e;
  g_nzip = 0;
  while ((e = readdir(d)) && g_nzip < MAX_ZIPS) {
    if (!strncmp(e->d_name, "._", 2) || !a8r_zip_ends(e->d_name, ".zip") ||
        strlen(e->d_name) >= sizeof g_zip[0].name)
      continue;
    memset(&g_zip[g_nzip], 0, sizeof g_zip[0]);
    snprintf(g_zip[g_nzip].name, sizeof g_zip[0].name, "%s", e->d_name);
    g_nzip++;
  }
  closedir(d);
  if (!g_nzip)
    return 0;

  printf("\nZips in %s:\n", GAME_DIR);
  consoleUpdate(NULL);
  int newest = -1;
  for (int i = 0; i < g_nzip; i++) {
    scan(&g_zip[i]);
    if (g_zip[i].kind == A8R_ZIP_BASE && g_zip[i].apk[0] &&
        (newest < 0 || g_zip[i].apk_date > g_zip[newest].apk_date))
      newest = i;
  }
  uint64_t total = 0;
  int count = 0;
  for (int i = 0; i < g_nzip; i++) {
    Zip *z = &g_zip[i];
    z->unused = z->kind == A8R_ZIP_BASE && z->apk[0] && i != newest;
    const char *what = z->kind == KIND_UNREADABLE ? "cannot be read (damaged, or not a zip): left alone"
                       : z->kind == A8R_ZIP_OTHER ? "not part of the A8R mod: left alone"
                       : z->unused                ? "an older A8R.apk than the other one: not used"
                       : z->kind == A8R_ZIP_UPDATE ? "update for the game data"
                       : z->apk[0] && z->scan.gameloft ? "A8R.apk and game data"
                       : z->apk[0]                ? "A8R.apk"
                                                  : "game data";
    printf("  %-34s %s", z->name, what);
    if (installs(z)) {
      printf(" (%llu MB)", (unsigned long long)(z->bytes >> 20));
      total += z->bytes;
      count++;
    }
    printf("\n");
  }
  if (!count)
    return 0;
  for (int i = 0; i < g_nzip; i++)
    if (g_zip[i].unused && rename_to(g_zip[i].name, ".unused") != 0) {
      printf("\nCould not rename %s.\nIs the SD card read-only?\n", g_zip[i].name);
      return -1;
    }

  int64_t fr = sd_free();
  if (fr >= 0 && (uint64_t)fr < total + (64ull << 20)) {
    printf("\nNot enough space on the SD card: installing needs %llu MB, and\n"
           "%llu MB are free. (Reinstalling? Deleting the old A8R.apk and\n"
           "gameloft folder in %s first frees space.)\n",
           (unsigned long long)((total >> 20) + 64), (unsigned long long)(fr >> 20), GAME_DIR);
    return -1;
  }

  /* the APK and the data before the update, which replaces some of the data */
  int order[MAX_ZIPS], n = 0;
  for (int kind = A8R_ZIP_BASE; kind <= A8R_ZIP_UPDATE; kind++) {
    int from = n;
    for (int i = 0; i < g_nzip; i++)
      if (installs(&g_zip[i]) && g_zip[i].kind == kind) {
        int j = n++;
        for (; j > from && strcasecmp(g_zip[order[j - 1]].name, g_zip[i].name) > 0; j--)
          order[j] = order[j - 1];
        order[j] = i;
      }
  }

  if (!(g_buf = malloc(CHUNK))) {
    printf("\nOut of memory.\n");
    return -1;
  }
  appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
  appletSetMediaPlaybackState(true); /* no auto-sleep meanwhile */
  Progress all = {0, total, 0, NULL};
  launcher_bar("Extracting the game data (a few minutes)", 0);
  int rc = 0;
  for (int k = 0; k < n && !rc; k++) {
    const Zip *z = &g_zip[order[k]];
    char p[320];
    in_game_dir(p, sizeof p, z->name);
    if (extract(z, &all) != 0)
      rc = -1;
    else if (remove(p) != 0 && rename_to(z->name, ".installed") != 0) {
      snprintf(g_why, sizeof g_why, "Installed %s, but could not remove it.\nRemove it yourself.", z->name);
      rc = -1;
    }
  }
  appletSetMediaPlaybackState(false);
  appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
  free(g_buf);
  g_buf = NULL;
  if (rc) {
    launcher_bar_off();
    printf("%s\nThe zips not installed yet are still there: launch again to retry.\n", g_why);
    return -1;
  }
  launcher_bar("The game data is in place", 1000);
  return 0;
}
