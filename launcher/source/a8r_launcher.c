/* a8r_launcher.c -- the A8R mod's part of the shared launcher
 * (runtime/launcher/source/main.c calls these hooks).
 *
 *   prepare        the mod's three zips copied into the game folder, unpacked
 *                  there (zips.c) before the APK check
 *   check          the game data: the OBB in gameloft/games/GloftA8HP, from
 *                  A8R DATA.zip
 *   apk_help       no APK: its zip, copied as it is
 *   instructions   step 1 off the forwarder icon: the zips
 *
 * MIT.
 */
#include <stdio.h>
#include <sys/stat.h>

#include "launcher.h"
#include "rt_settings.h"
#include "zips.h"

#define OBB GAME_DIR "/gameloft/games/GloftA8HP/main.sa2.Asphalt8.obb"

int port_launcher_prepare(void) { return zips_install(); }

int port_launcher_check(char *status, size_t scap, char *help, size_t hcap) {
  struct stat st;
  int have_obb = stat(OBB, &st) == 0 && st.st_size > 0;
  snprintf(status, scap, "game data: %s", have_obb ? "found" : "MISSING");
  if (have_obb)
    return 0;
  snprintf(help, hcap,
           "The game data is missing: copy A8R DATA.zip and A8R DATA\n"
           "December.zip into %s as they are.\n"
           "(Files extracted on a computer work too: the APK and the\n"
           "gameloft folder, in %s.)",
           GAME_DIR, GAME_DIR);
  return 1;
}

void port_launcher_apk_help(void) {
  printf("\nA8R.apk is missing: copy A8R APK December.zip (the December\n"
         "build) into %s as it is, or the APK itself (any name).\n",
         GAME_DIR);
}

void port_launcher_instructions(void) {
  printf("  1. copy the A8R mod's three zips into " PORT_ROOT_PATH " as they are:\n"
         "     A8R APK December.zip, A8R DATA.zip, A8R DATA December.zip\n");
}
