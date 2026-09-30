/* main.c -- boot sequence for the Asphalt 8: Airborne Retry wrapper (32-bit).
 *
 * The order here matters; each step says why it is where it is. MIT.
 */
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>

#include "a8r.h"
#include "a8r_folder.h"
#include "a8r_menu.h"
#include "config.h"
#include "dcr_config.h"
#include "dcr_manifest.h"
#include "dcr_path.h"
#include "dcr_sched.h"
#include "dcr_time.h"
#include "error.h"
#include "nx_init.h"
#include "selfproc.h"
#include "so_util.h"
#include "util.h"

int a8r_boot_run(void);

static char g_root[256] = "sdmc:" A8R_ROOT_PATH;
const char *dcr_game_root(void) { return g_root; }

extern volatile uint32_t __dcr_reloc_path __attribute__((visibility("hidden")));

static void report_boot(void) {
  const u64 MB = 1024 * 1024;
  debugPrintf("[boot] === a8retry_nx: Asphalt 8: Airborne Retry (Gameloft jet/glf engine + the A8R mod, "
              "armeabi-v7a) ===\n");
  static const char *const paths[] = {"none needed", "patched through a writable alias (hardware)",
                                      "direct writes (emulator: pseudo-handle refused)"};
  debugPrintf("[boot] text relocations: %s\n",
              __dcr_reloc_path < 3 ? paths[__dcr_reloc_path] : "?");
  debugPrintf("[heap] total %u MB, used %u MB at start, heap region %u MB, heap %u MB @ %p\n",
              (unsigned)(g_nxinit.total / MB), (unsigned)(g_nxinit.used / MB),
              (unsigned)(g_nxinit.heap_region / MB), (unsigned)(g_nxinit.heap / MB),
              (void *)g_nxinit.heap_base);
  debugPrintf("[svc] sm=%x applet=%x hid=%x time=%x fs=%x sdmc=%x\n", g_nxinit.rc_sm,
              g_nxinit.rc_applet, g_nxinit.rc_hid, g_nxinit.rc_time, g_nxinit.rc_fs,
              g_nxinit.rc_sdmc);
  if (R_FAILED(g_nxinit.rc_time))
    debugPrintf("[svc] time service unavailable: clocks fall back to the system tick\n");
}

int main(int argc, char *argv[]) {
  /* The game folder was /switch/a8retry in the first builds: moved into this
   * one before anything is written here (a8r_folder.h), logged below. */
  static char moved[800];
  int migrated = a8r_migrate(g_root, moved, sizeof moved);
  mkdir(g_root, 0777);
  log_init(g_root);
  if (dcr_is_emulator()) {
    int dcr_emu_fix_self(void); /* emu_fixups.c: before any of Mesa runs */
    dcr_emu_fix_self();
  }
  log_console_open(); /* blank: text only when asked or for setup work */
  report_boot();
  if (migrated)
    debugPrintf("[setup] %s\n", moved);

  if (chdir(g_root) != 0)
    debugPrintf("[boot] WARNING: chdir(%s) failed\n", g_root);
  dcr_config_load(); /* config.ini: the mod's options, the window, controls */
  if (dcr_config()->boot_log)
    log_console_show_text();
  dcr_time_init();
  dcr_path_prepare_dirs();

  /* New A8R zips copied into the game folder: the launcher installs them,
   * so restart into it. Else a newer build of this program in the launcher
   * NRO: install it and restart into it before anything else happens
   * (a8r_setup.c). */
  dcr_setup_zips_to_launcher();
  dcr_setup_update_from_nro();

  /* The A8R APK copied in under another name becomes A8R.apk (a8r_folder.h) */
  char apk[512];
  if (a8r_adopt_apk(g_root, a8r_is_mod_apk, moved, sizeof moved) != 0 || moved[0])
    debugPrintf("[setup] %s\n", moved);
  snprintf(apk, sizeof apk, "%s/" DCR_APK_NAME, g_root);
  void dcr_apkcache_set_path(const char *real);
  dcr_apkcache_set_path(apk);
  if (dcr_manifest_load(apk) != 0)
    fatal_error("%s is missing or unreadable.\n\n"
                "Copy A8R APK December.zip (your own Asphalt 8: Airborne Retry, December\n"
                "build) into that folder as it is, or the APK itself under any name,\n"
                "then start the game again.",
                apk);
  if (strcmp(dcr_manifest_package(), A8R_PACKAGE))
    debugPrintf("[boot] WARNING: A8R.apk is %s, not %s\n", dcr_manifest_package(), A8R_PACKAGE);
  else
    debugPrintf("[boot] A8R.apk: %s %s (%d)\n", dcr_manifest_package(), dcr_manifest_version_name(),
                dcr_manifest_version_code());

  /* The mod's start screen (a8r_menu.c): its options go into config.ini,
   * which the setup below builds the engine from. Skipped when the user
   * chose to, unless - is held. */
  if (!dcr_config()->skip_start || a8r_input_minus_held()) {
    if (a8r_menu_run(apk) == 0) {
      debugPrintf("[boot] exit from the start screen\n");
      log_flush_ring();
      return 0;
    }
  }
  void dcr_boost_launch_begin(void);
  dcr_boost_launch_begin(); /* CPU at 1785 MHz from PLAY to the first picture (dcr_boost.c) */

  if (dcr_self_process() == INVALID_HANDLE)
    fatal_error("Could not obtain a handle to this process.\n"
                "The loader needs it to map the game's code.");

  /* libasphalt8.so from the APK's hidden pieces and the options, classes.txt,
   * and the data folder as the mod's PLAY button leaves it */
  a8r_setup(apk);
  a8r_menu_done(); /* the start screen, kept for setup's progress, gives the window up */
  if (a8r_load_engine() != 0)
    fatal_error("Could not load the game's engine from %s.\n\n"
                "It is put together from A8R.apk on launch: delete " A8R_LIB_GAME " and\n"
                ".setup there to make it again.",
                g_root);

  /* The main thread becomes a guest thread like the game's own: priority
   * 59 on cores 0-2, where the kernel time-slices (dcr_sched.c). */
  dcr_sched_init();
  {
    void dcr_audio_selftest(void);
    dcr_audio_selftest();
    void dcr_pthread_selftest(void);
    dcr_pthread_selftest();
    void dcr_io_selftest(void);
    dcr_io_selftest();
  }
#if DCR_GL_MESA
  if (dcr_is_emulator() || dcr_config()->gl_selftest) {
    int dcr_gl_selftest(void);
    dcr_gl_selftest();
  }
#endif

  /* What loading spends its time on, from here to the first picture and in
   * every long frame after it (a8r_prof.c, [debug] profile_loading). */
  void dcr_prof_start(void);
  dcr_prof_start();

  /* Android runs a library's constructors inside System.load (GameInstaller
   * loads libasphalt8.so), before JNI_OnLoad. */
  a8r_run_constructors();

  a8r_boot_run();
  debugPrintf("[boot] exiting\n");
  log_flush_ring();
  return 0;
}
