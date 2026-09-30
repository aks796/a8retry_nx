/* a8r_zips.h -- the zips the A8R mod ships in, told apart by what they hold.
 *
 * The mod comes as three zips made for an Android phone:
 *
 *   A8R APK December.zip    A8R.apk
 *   A8R DATA.zip            gameloft/games/GloftA8HP/... for the phone's
 *                           storage root: the OBB, texts, empty folders
 *   A8R DATA December.zip   files for inside gameloft/games/GloftA8HP: the
 *                           December update (textures, collisions, texts)
 *
 * Their names are not relied on (browsers and users rename downloads): an
 * APK, or anything under a "gameloft/" folder, makes a zip one to install
 * first; game files without that folder make it an update for the data
 * folder, installed after. Anything else is not the mod's.
 *
 * Shared by the launcher, which installs them (launcher/source/zips.c), and
 * the game program, which hands zips found later to it (a8r_setup.c), so the
 * two always agree. No library use: entry names come from minizip in one and
 * miniz in the other. MIT.
 */
#ifndef A8R_ZIPS_H
#define A8R_ZIPS_H

#include <string.h>
#include <strings.h>

#define A8R_ZIP_DATA_DIR "gameloft/games/GloftA8HP"

enum { A8R_ZIP_OTHER, A8R_ZIP_BASE, A8R_ZIP_UPDATE };
enum { A8R_ENTRY_JUNK, A8R_ENTRY_DIR, A8R_ENTRY_APK, A8R_ENTRY_GAMELOFT, A8R_ENTRY_GAME_FILE, A8R_ENTRY_OTHER };

typedef struct {
  unsigned apk, gameloft, game_files;
} A8rZipScan;

/* Windows zips separate with '\'. */
static inline void a8r_zip_normalize(char *name) {
  for (char *p = name; *p; p++)
    if (*p == '\\')
      *p = '/';
}

static inline int a8r_zip_ends(const char *s, const char *suffix) {
  size_t n = strlen(s), k = strlen(suffix);
  return n >= k && !strcasecmp(s + n - k, suffix);
}

/* Where the path component `dir`/ starts in name, or NULL. */
static inline const char *a8r_zip_component(const char *name, const char *dir) {
  size_t k = strlen(dir);
  for (const char *p = name; *p; p = strchr(p, '/') ? strchr(p, '/') + 1 : p + strlen(p))
    if (!strncasecmp(p, dir, k) && p[k] == '/')
      return p;
  return NULL;
}

/* A path this must never write (outside the game folder), or macOS and
 * Windows leftovers (__MACOSX/, ._name, .DS_Store, Thumbs.db). */
static inline int a8r_zip_junk(const char *name) {
  if (!*name || *name == '/' || strchr(name, ':'))
    return 1;
  for (const char *p = name; p; p = strchr(p, '/') ? strchr(p, '/') + 1 : NULL) {
    if (!strncmp(p, "../", 3) || !strcmp(p, "..") || !strncmp(p, "._", 2) || !strncasecmp(p, "__MACOSX/", 9) ||
        !strcasecmp(p, ".DS_Store") || !strcasecmp(p, "Thumbs.db"))
      return 1;
  }
  return 0;
}

/* The files of the data folder, by extension (the December update's are
 * textures, collision shapes and texts): only ones no other zip would hold. */
static inline int a8r_zip_game_file(const char *name) {
  static const char *const ext[] = {".tga", ".texts", ".shapedef", ".shapedefx", ".pig", ".bdae", ".mpc", ".obb"};
  for (unsigned i = 0; i < sizeof ext / sizeof ext[0]; i++)
    if (a8r_zip_ends(name, ext[i]))
      return 1;
  return 0;
}

static inline int a8r_zip_entry(const char *name) {
  if (a8r_zip_junk(name))
    return A8R_ENTRY_JUNK;
  if (a8r_zip_component(name, "gameloft"))
    return A8R_ENTRY_GAMELOFT;
  if (name[strlen(name) - 1] == '/')
    return A8R_ENTRY_DIR;
  if (a8r_zip_ends(name, ".apk"))
    return A8R_ENTRY_APK;
  return a8r_zip_game_file(name) ? A8R_ENTRY_GAME_FILE : A8R_ENTRY_OTHER;
}

/* Counts one (normalized) entry; returns its A8R_ENTRY_*. */
static inline int a8r_zip_scan(A8rZipScan *s, const char *name) {
  int e = a8r_zip_entry(name);
  s->apk += e == A8R_ENTRY_APK;
  s->gameloft += e == A8R_ENTRY_GAMELOFT;
  s->game_files += e == A8R_ENTRY_GAME_FILE;
  return e;
}

static inline int a8r_zip_kind(const A8rZipScan *s) {
  if (s->apk || s->gameloft)
    return A8R_ZIP_BASE;
  return s->game_files ? A8R_ZIP_UPDATE : A8R_ZIP_OTHER;
}

/* An APK file of any name is the mod's when it holds AndroidManifest.xml and
 * the five assets the mod hides its engine in (a8r_setup.c). The stock game's
 * APK has neither those assets nor the name, so it is never mistaken. */
typedef struct {
  unsigned manifest, parts;
} A8rApkScan;

static inline const char *a8r_apk_part(int i) {
  static const char *const parts[] = {
      "m_bgm_menu_halloween_djgontran_i_see_you.mp3", "m_breton_the_commission.mp3",
      "m_celldweller_pulsar.mp3", "m_celldweller_through_the_gates.mp3",
      "m_dj_gontran_down_to_earth.mp3",
  };
  return i >= 0 && i < 5 ? parts[i] : NULL;
}

static inline void a8r_apk_scan(A8rApkScan *s, const char *name) {
  if (!strcmp(name, "AndroidManifest.xml"))
    s->manifest++;
  else if (!strncmp(name, "assets/", 7))
    for (int i = 0; a8r_apk_part(i); i++)
      if (!strcmp(name + 7, a8r_apk_part(i)))
        s->parts++;
}

static inline int a8r_apk_is_mod(const A8rApkScan *s) { return s->manifest && s->parts >= 5; }

/* An update entry's path inside the data folder: past a GloftA8HP/ folder
 * when the zip has one. */
static inline const char *a8r_zip_in_data(const char *name) {
  const char *g = a8r_zip_component(name, "GloftA8HP");
  return g ? g + 10 : name;
}

#endif
