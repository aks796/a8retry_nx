/* port_config.h -- Asphalt 8: Airborne Retry's settings for the android32
 * runtime.
 *
 * Macros only: the runtime's C files, its assembly and the launcher all read
 * this (runtime/source/rt_settings.h). What each setting does is next to its
 * default in the runtime; runtime/docs/ lists them all. MIT.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* ------------------------------------------------------------------ the game */
#define PORT_TITLE   "Asphalt 8: Airborne Retry"
#define PORT_NAME    "a8retry_nx"
#define PORT_PACKAGE "com.gameloft.android.HEP.GloftA8HP"
#define PORT_BANNER  "a8retry_nx: Asphalt 8: Airborne Retry (Gameloft jet/glf engine + the A8R mod, armeabi-v7a)"
/* the first builds used /switch/a8retry */
#define PORT_OLD_ROOT_PATHS "/switch/a8retry"
/* libasphalt8.so's highest p_vaddr + p_memsz is 0x1b74e2c (~27.5 MB) */
#define PORT_SO_REGION_BYTES (40u * 1024 * 1024)

/* The mod's APK, under any name: by its manifest and the five assets the mod
 * hides its engine in (a8r_setup.c). It becomes A8R.apk. */
#define PORT_APK_DEFAULT_NAME "A8R.apk"
#define PORT_APK_DESC "Asphalt 8: Airborne Retry (the A8R mod's December build)"
#define PORT_APK_ROLES                                                                             \
  {.what = "the A8R APK", .name = "A8R.apk",                                                     \
   .need = (const char *const[]){"AndroidManifest.xml",                                          \
                                 "assets/m_bgm_menu_halloween_djgontran_i_see_you.mp3",          \
                                 "assets/m_breton_the_commission.mp3",                           \
                                 "assets/m_celldweller_pulsar.mp3",                              \
                                 "assets/m_celldweller_through_the_gates.mp3",                   \
                                 "assets/m_dj_gontran_down_to_earth.mp3", NULL},                 \
   .flags = RT_APK_ADOPT}

/* ------------------------------------------------------------------ libc */
#define RT_TIME_SHIFT        3 /* REALTIME and time(): the frame timer is gettimeofday */
#define RT_IO_READAHEAD      1 /* the OBB's zip directory comes in 4-byte reads */
#define RT_IO_PATTERN_STATS  1
#define RT_STDIO_READ_BUF    32768
#define RT_LOG_THREAD_CREATE 1

/* ------------------------------------------------------------------ JNI, NDK, sound */
#define RT_OPENSLES 1 /* the vox engine plays through OpenSL ES */

/* ------------------------------------------------------------------ frames */
#define RT_GL_CHECK        1
#define RT_PAD_MAX_PLAYERS 8
#define RT_BOOST_AT_BOOT   0 /* the boost starts at the start screen's PLAY (port_load) */

/* ------------------------------------------------------------------ files */
#define RT_PATH_SD_SUBDIRS  "gameloft"
#define RT_PATH_EXTRA_DIRS  "data/databases", "sdcard", "gameloft", "gameloft/games", "gameloft/games/GloftA8HP"
#define RT_PATH_TRACE_EXTRA ".obb"
#define RT_MIGRATE_BOTH_INSTALLED "A8R.apk", "gameloft"

/* ------------------------------------------------------------------ setup, launcher */
#define RT_SETUP_UPDATE_PERMILLE 500
#define RT_LAUNCHER_BAR 1

#endif
