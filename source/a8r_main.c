/* a8r_main.c -- the port's part of the boot. The runtime's main.c does the
 * rest, in this order: the old folder moved in, the log, config.ini, the
 * clock, focus messages, new zips handed to the launcher (port_before_update),
 * the NRO self-update, the APK found by what is in it (A8R.apk), its manifest
 * -- then port_load(), the guest scheduling and the self-tests, port_run().
 * MIT.
 */
#include <stdio.h>
#include <switch.h>

#include "a8r.h"
#include "a8r_menu.h"
#include "a8r_prof.h"
#include "config.h"
#include "dcr_boost.h"
#include "dcr_config.h"
#include "dcr_path.h"
#include "error.h"
#include "rt_boot.h"
#include "util.h"

/* From the APK to the engine's first code. */
int port_load(const char *apk) {
  /* The mod's start screen (a8r_menu.c): its options go into config.ini,
   * which the setup below builds the engine from. Skipped when the user
   * chose to, unless - is held. */
  if (!dcr_config()->skip_start || a8r_input_minus_held()) {
    if (a8r_menu_run(apk) == 0) {
      debugPrintf("[boot] exit from the start screen\n");
      return 1;
    }
  }
  dcr_boost_launch_begin(); /* CPU at 1785 MHz from PLAY to the first picture (dcr_boost.c) */

  /* libasphalt8.so from the APK's hidden pieces and the options, classes.txt,
   * and the data folder as the mod's PLAY button leaves it */
  a8r_setup(apk);
  a8r_menu_done(); /* the start screen, kept for setup's progress, gives the window up */
  if (a8r_load_engine() != 0)
    fatal_error("Could not load the game's engine from %s.\n\n"
                "It is put together from A8R.apk on launch: delete " A8R_LIB_GAME " and\n"
                ".setup there to make it again.",
                dcr_game_root());
  return 0;
}

/* The constructors, then the game. */
void port_run(void) {
  /* What loading spends its time on, from here to the first picture and in
   * every long frame after it (a8r_prof.c, [debug] profile_loading). */
  dcr_prof_start();
  a8r_gl_hooks(); /* the frame profile's next frame begins after the boost's frame end */
  /* Android runs a library's constructors inside System.load (GameInstaller
   * loads libasphalt8.so), before JNI_OnLoad. */
  a8r_run_constructors();
  a8r_boot_run();
}

/* New A8R zips copied into the game folder: the launcher installs them, so
 * restart into it (a8r_setup.c), before the NRO self-update. */
void port_before_update(void) { dcr_setup_zips_to_launcher(); }

const char *port_apk_help(void) {
  return "Copy A8R APK December.zip (your own Asphalt 8: Airborne Retry, December\n"
         "build) into that folder as it is, or the APK itself under any name,\n"
         "then start the game again.";
}
