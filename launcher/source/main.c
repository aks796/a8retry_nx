/* a8retry_nx.nro -- the launcher: the one file the port ships.
 *
 * The game itself is 32-bit ARM, and a 32-bit program cannot be an NRO
 * (hbloader, which runs NROs, is 64-bit). So the game program -- the wrapper,
 * a8retry_nx.nsp -- rides in this NRO's romfs, and is installed on the console as
 * an Atmosphere ExeFS override for the HOME-menu icon it was launched from:
 *
 *   1. the user makes a sphaira forwarder for this NRO and launches it;
 *   2. this runs inside that forwarder title: it installs the game files from
 *      the A8R mod's zips copied beside it (zips.c), writes
 *      /atmosphere/contents/<the forwarder's title id>/exefs.nsp (the wrapper,
 *      main.npdm retargeted to that title id: source/dcr_exefs.h) and
 *      restarts the title;
 *   3. Atmosphere now starts the wrapper for that icon instead of hbloader.
 *      On its first run the wrapper puts the game's engine together from
 *      the user's A8R.apk (source/a8r_setup.c); later NROs update it in
 *      place.
 *
 * The override is only ever written for a forwarder (title id 05xx...) that
 * this program is running as -- never for a real game or a system title, and
 * never from hbmenu. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>
#include <minizip/unzip.h>

#include "a8r_folder.h"
#include "dcr_exefs.h"
#include "zips.h"

#define APK GAME_DIR "/A8R.apk"
#define OBB GAME_DIR "/gameloft/games/GloftA8HP/main.sa2.Asphalt8.obb"

static PadState g_pad;

static void show(void) { consoleUpdate(NULL); }

/* Waits for + (or the HOME menu closing us). */
static void wait_exit(void) {
  printf("\nPress + to exit.\n");
  while (appletMainLoop()) {
    padUpdate(&g_pad);
    if (padGetButtonsDown(&g_pad) & HidNpadButton_Plus)
      break;
    show();
  }
}

static uint8_t *read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *b = n > 0 ? malloc((size_t)n) : NULL;
  if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
    free(b);
    b = NULL;
  }
  fclose(f);
  *len = b ? (size_t)n : 0;
  return b;
}

static int write_file(const char *path, const uint8_t *d, size_t len) {
  char tmp[160];
  snprintf(tmp, sizeof tmp, "%s.part", path);
  FILE *f = fopen(tmp, "wb");
  if (!f)
    return -1;
  int ok = fwrite(d, 1, len, f) == len;
  if (fclose(f) != 0)
    ok = 0;
  if (ok) {
    remove(path);
    ok = rename(tmp, path) == 0;
  }
  if (!ok)
    remove(tmp);
  return ok ? 0 : -1;
}

/* path is the A8R mod's APK, whatever its name (a8r_zips.h) */
static int is_mod_apk(const char *path) {
  unzFile uf = unzOpen64(path);
  if (!uf)
    return 0;
  A8rApkScan s = {0};
  char name[512];
  unz_file_info64 fi;
  for (int rc = unzGoToFirstFile(uf); rc == UNZ_OK; rc = unzGoToNextFile(uf))
    if (unzGetCurrentFileInfo64(uf, &fi, name, sizeof name, NULL, 0, NULL, 0) == UNZ_OK)
      a8r_apk_scan(&s, name);
  unzClose(uf);
  return a8r_apk_is_mod(&s);
}

static int install(uint64_t tid) {
  size_t nsp_len = 0, out_len = 0, cur_len = 0;
  uint8_t *nsp = read_file("romfs:/a8retry_nx.nsp", &nsp_len), *out = NULL;
  if (!nsp || exefs_build_override(nsp, nsp_len, tid, &out, &out_len)) {
    launcher_bar_off();
    printf("This launcher's copy of the game program is missing or damaged.\n"
           "Download a8retry_nx.nro again.\n");
    free(nsp);
    return -1;
  }
  free(nsp);

  char dir[96], path[128];
  snprintf(dir, sizeof dir, "sdmc:/atmosphere/contents/%016lX", tid);
  snprintf(path, sizeof path, "%s/exefs.nsp", dir);
  uint8_t *cur = read_file(path, &cur_len);
  int same = cur && cur_len == out_len && !memcmp(cur, out, out_len);
  free(cur);
  if (same) {
    /* Already installed, yet this launcher ran instead of the game. */
    launcher_bar_off();
    printf("The game program is installed for this icon (%s),\n"
           "but Atmosphere started this launcher instead of it.\n\n"
           "Update Atmosphere, then launch the icon again.\n", path);
    free(out);
    return -1;
  }
  mkdir("sdmc:/atmosphere", 0777);
  mkdir("sdmc:/atmosphere/contents", 0777);
  mkdir(dir, 0777);
  int rc = write_file(path, out, out_len);
  free(out);
  if (rc) {
    launcher_bar_off();
    printf("Could not write %s.\nIs the SD card full or read-only?\n", path);
    return -1;
  }
  if (!launcher_bar_on())
    printf("Installed the game program for this icon:\n  %s\n", path);
  return 0;
}

int main(int argc, char **argv) {
  consoleInit(NULL);
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&g_pad);
  Result rrc = romfsInit();

  printf("Asphalt 8: Airborne Retry for Nintendo Switch -- launcher\n"
         "=========================================================\n\n");
  size_t blen = 0;
  uint8_t *bnum = R_SUCCEEDED(rrc) ? read_file("romfs:/a8retry_nx.build", &blen) : NULL;
  printf("Game program build: %.*s\n", bnum ? (int)(blen && bnum[blen - 1] == '\n' ? blen - 1 : blen) : 7,
         bnum ? (const char *)bnum : "missing");
  free(bnum);

  const char *self = argc > 0 && argv[0] ? argv[0] : "";
  if (*self && !strstr(self, "/switch/a8retry_nx/"))
    printf("\nNote: this NRO is at %s.\n"
           "The game files belong in /switch/a8retry_nx; keeping the NRO there\n"
           "too lets the game update itself when you replace it.\n", self);

  u64 tid = 0;
  svcGetInfo(&tid, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
  int forwarder = appletGetAppletType() == AppletType_Application && exefs_is_forwarder_tid(tid);
  /* From its icon this runs only when there is work (a first launch, new
   * zips): the progress bar, and text again for anything to read. */
  if (forwarder)
    launcher_bar("Checking the game files", 0);

  /* the first builds' folder, /switch/a8retry, moved into this one (listed
   * in migrated.txt there) */
  static char msg[800];
  int moved = a8r_migrate(GAME_DIR, msg, sizeof msg);
  if (moved < 0)
    launcher_bar_off();
  if (moved && !launcher_bar_on())
    printf("\n%s\n", msg);

  /* the mod's zips, copied into the game folder as they are */
  if (zips_install() != 0) {
    wait_exit();
    romfsExit();
    consoleExit(NULL);
    return 0;
  }

  /* the APK itself, copied in under any name */
  int adopted = a8r_adopt_apk(GAME_DIR, is_mod_apk, msg, sizeof msg);
  if (adopted < 0)
    launcher_bar_off();
  if ((adopted || msg[0]) && !launcher_bar_on())
    printf("\n%s\n", msg);

  struct stat st;
  int have_apk = stat(APK, &st) == 0 && st.st_size > 0;
  int have_obb = stat(OBB, &st) == 0 && st.st_size > 0;
  if (!have_apk || !have_obb)
    launcher_bar_off();
  if (!launcher_bar_on()) {
    printf("\nA8R.apk:   %s\n", have_apk ? "found" : "MISSING");
    printf("game data: %s\n", have_obb ? "found" : "MISSING");
  }

  if (!forwarder) {
    printf("\nStart this from its own HOME-menu icon:\n");
    if (have_apk && have_obb)
      printf("  1. (your game files are in place)\n");
    else
      printf("  1. copy the A8R mod's three zips into /switch/a8retry_nx as they are:\n"
             "     A8R APK December.zip, A8R DATA.zip, A8R DATA December.zip\n");
    printf("  2. in sphaira: Homebrew > Asphalt 8: Airborne Retry > Install Forwarder\n"
           "  3. launch the new Asphalt 8: Airborne Retry icon on the HOME menu.\n"
           "The first launch installs the zips and the game program for that\n"
           "icon, then starts the game.\n");
    wait_exit();
  } else if (!have_apk || !have_obb) {
    if (!have_apk)
      printf("\nA8R.apk is missing: copy A8R APK December.zip (the December\n"
             "build) into %s as it is, or the APK itself (any name).\n", GAME_DIR);
    if (!have_obb)
      printf("\nThe game data is missing: copy A8R DATA.zip and A8R DATA\n"
             "December.zip into %s as they are.\n", GAME_DIR);
    printf("\nThen launch this icon again. (Files extracted on a computer work\n"
           "too: the APK and the gameloft folder, in %s.)\n", GAME_DIR);
    wait_exit();
  } else if (launcher_bar("Installing the game program for this icon", 1000), install(tid) != 0) {
    wait_exit();
  } else {
    launcher_bar("Starting the game", 1000);
    svcSleepThread(1500000000ll);
    romfsExit();
    Result rc = appletRestartProgram(NULL, 0);
    launcher_bar_off();
    printf("\nRestarting did not work (0x%x): close this and launch\n"
           "Asphalt 8: Airborne Retry again.\n", rc);
    wait_exit();
    consoleExit(NULL);
    return 0;
  }
  romfsExit();
  consoleExit(NULL);
  return 0;
}
