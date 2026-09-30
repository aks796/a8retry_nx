/* a8r_folder.h -- two jobs on the game folder, shared by the launcher
 * (launcher/source/main.c) and the game program (main.c), whichever runs
 * first:
 *
 *   a8r_migrate()    the folder's move from /switch/a8retry (the port's
 *                    first builds) to /switch/a8retry_nx: every file and
 *                    folder in the old one is renamed into the new one (the
 *                    same SD card, so the 1.5 GB OBB moves at once), except
 *                    .nro files (the old launcher) and names the new folder
 *                    already has. Nothing is copied or deleted; the old
 *                    folder is removed only when that leaves it empty. What
 *                    moved goes into migrated.txt in the new folder.
 *
 *   a8r_adopt_apk()  an APK of any name: the A8R APK copied into the folder
 *                    under another name (a browser's download name, a
 *                    version in it) becomes A8R.apk. Told apart by what it
 *                    holds (a8r_zips.h: a8r_apk_scan), through the caller's
 *                    zip reader. The newest (by date) is taken; A8R.apk, if
 *                    there was one, becomes A8R.apk.previous, and other A8R
 *                    APKs *.unused so they are not taken later. An APK that
 *                    is not the mod's is left alone.
 *
 * Both write what they did into msg (for the screen or debug.log). Plain
 * newlib (dirent, stdio, stat): no libnx, so the 64-bit launcher and the
 * 32-bit program build the same code. MIT.
 */
#ifndef A8R_FOLDER_H
#define A8R_FOLDER_H

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "a8r_zips.h"

#define A8R_OLD_DIR "sdmc:/switch/a8retry"
#define A8R_FOLDER_MAX 96 /* entries looked at in the folder's top level */

typedef struct {
  char name[A8R_FOLDER_MAX][128];
  int n;
} A8rNames;

/* The top-level names of dir (not . and ..; not macOS ._ files), read in
 * full before anything is renamed: FAT directories change under readdir. */
static inline int a8r_list(const char *dir, A8rNames *out) {
  out->n = 0;
  DIR *d = opendir(dir);
  if (!d)
    return -1;
  struct dirent *e;
  while ((e = readdir(d)) && out->n < A8R_FOLDER_MAX) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || !strncmp(e->d_name, "._", 2) ||
        strlen(e->d_name) >= sizeof out->name[0])
      continue;
    snprintf(out->name[out->n++], sizeof out->name[0], "%s", e->d_name);
  }
  closedir(d);
  return 0;
}

static inline int a8r_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

/* >0: entries moved; 0: nothing to do; -1: some could not be moved; -2: both
 * folders hold a game install, so neither was touched. */
static inline int a8r_migrate(const char *new_dir, char *msg, size_t cap) {
  static A8rNames names;
  char from[400], to[400];
  msg[0] = 0;
  if (!a8r_exists(A8R_OLD_DIR) || a8r_list(A8R_OLD_DIR, &names) != 0)
    return 0;
  snprintf(to, sizeof to, "%s/A8R.apk", new_dir);
  int new_has_game = a8r_exists(to);
  snprintf(to, sizeof to, "%s/gameloft", new_dir);
  new_has_game |= a8r_exists(to);
  if (new_has_game) {
    snprintf(from, sizeof from, A8R_OLD_DIR "/A8R.apk");
    int old_has_game = a8r_exists(from);
    snprintf(from, sizeof from, A8R_OLD_DIR "/gameloft");
    old_has_game |= a8r_exists(from);
    if (!old_has_game)
      return 0;
    snprintf(msg, cap, "Both %s and %s hold the game: the old folder was left as it is (delete it once "
             "the new one works).", A8R_OLD_DIR, new_dir);
    return -2;
  }
  mkdir(new_dir, 0777);
  int moved = 0, failed = 0, kept = 0;
  char list[600] = "", left[300] = "";
  size_t ln = 0, lf = 0;
  for (int i = 0; i < names.n; i++) {
    const char *nm = names.name[i];
    snprintf(from, sizeof from, A8R_OLD_DIR "/%s", nm);
    snprintf(to, sizeof to, "%s/%s", new_dir, nm);
    if (a8r_zip_ends(nm, ".nro") || a8r_exists(to)) {
      kept++;
      if (lf < sizeof left - 1)
        lf += (size_t)snprintf(left + lf, sizeof left - lf, "%s%s", lf ? ", " : "", nm);
      continue;
    }
    if (rename(from, to) != 0) {
      failed++;
      if (lf < sizeof left - 1)
        lf += (size_t)snprintf(left + lf, sizeof left - lf, "%s%s (could not move)", lf ? ", " : "", nm);
      continue;
    }
    moved++;
    if (ln < sizeof list - 1)
      ln += (size_t)snprintf(list + ln, sizeof list - ln, "%s%s", ln ? ", " : "", nm);
  }
  if (ln >= sizeof list)
    ln = sizeof list - 1;
  if (lf >= sizeof left)
    lf = sizeof left - 1;
  if (!moved && !failed)
    return 0;
  if (!kept && !failed)
    rmdir(A8R_OLD_DIR);
  snprintf(msg, cap, "Moved %d item%s from %s to %s: %s.%s%s%s", moved, moved == 1 ? "" : "s", A8R_OLD_DIR,
           new_dir, list, lf ? " Left in the old folder: " : "", left, lf ? "." : "");
  snprintf(to, sizeof to, "%s/migrated.txt", new_dir);
  FILE *f = fopen(to, "a");
  if (f) {
    fprintf(f, "%s\n", msg);
    fclose(f);
  }
  return failed ? -1 : moved;
}

/* 1: an APK became A8R.apk; 0: nothing to do; -1: it could not be renamed. */
static inline int a8r_adopt_apk(const char *dir, int (*is_mod_apk)(const char *path), char *msg, size_t cap) {
  static A8rNames names;
  char p[400], q[420];
  msg[0] = 0;
  if (a8r_list(dir, &names) != 0)
    return 0;
  int best = -1;
  time_t best_t = 0;
  static int ok[A8R_FOLDER_MAX];
  for (int i = 0; i < names.n; i++) {
    const char *nm = names.name[i];
    ok[i] = 0;
    if (!a8r_zip_ends(nm, ".apk") || !strcasecmp(nm, "A8R.apk"))
      continue;
    snprintf(p, sizeof p, "%.250s/%.127s", dir, nm);
    struct stat st;
    if (stat(p, &st) != 0 || !S_ISREG(st.st_mode))
      continue;
    if (!is_mod_apk(p)) {
      size_t n = strlen(msg);
      snprintf(msg + n, cap - n, "%s is not the A8R mod's APK: left alone. ", nm);
      continue;
    }
    ok[i] = 1;
    if (best < 0 || st.st_mtime > best_t)
      best = i, best_t = st.st_mtime;
  }
  if (best < 0)
    return 0;
  char dst[400];
  snprintf(dst, sizeof dst, "%s/A8R.apk", dir);
  int had = a8r_exists(dst);
  if (had) {
    snprintf(q, sizeof q, "%s.previous", dst);
    remove(q);
    if (rename(dst, q) != 0) {
      size_t n = strlen(msg);
      snprintf(msg + n, cap - n, "Could not rename A8R.apk to make way for %s.", names.name[best]);
      return -1;
    }
  }
  snprintf(p, sizeof p, "%.250s/%.127s", dir, names.name[best]);
  if (rename(p, dst) != 0) {
    size_t n = strlen(msg);
    snprintf(msg + n, cap - n, "Could not rename %s to A8R.apk.", names.name[best]);
    return -1;
  }
  size_t n = strlen(msg);
  snprintf(msg + n, cap - n, "%s is the A8R APK: now A8R.apk%s.", names.name[best],
           had ? " (the one before is A8R.apk.previous)" : "");
  for (int i = 0; i < names.n; i++)
    if (ok[i] && i != best) {
      snprintf(p, sizeof p, "%.250s/%.127s", dir, names.name[i]);
      snprintf(q, sizeof q, "%s.unused", p);
      remove(q);
      if (rename(p, q) == 0) {
        n = strlen(msg);
        snprintf(msg + n, cap - n, " %s is older: renamed %s.unused.", names.name[i], names.name[i]);
      }
    }
  return 1;
}

#endif
