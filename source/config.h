/* config.h -- build-wide constants for the Asphalt 8: Retry Switch wrapper.
 *
 * Asphalt 8: Airborne (com.gameloft.android.HEP.GloftA8HP, Gameloft's jet/glf
 * engine, armeabi-v7a) as modified by the "Asphalt 8: Retry" mod (A8R,
 * December build). AArch32 host process built against libnx32 (see the
 * Makefile). MIT.
 */
#ifndef DCR_CONFIG_H
#define DCR_CONFIG_H

/* Where on the SD card the game files live. */
#define A8R_ROOT_PATH   "/switch/a8retry_nx"
#define DCR_ROOT_PATH   A8R_ROOT_PATH
#define DCR_APK_NAME    "A8R.apk"   /* the user's own A8R.apk, copied as-is */
#define A8R_PACKAGE     "com.gameloft.android.HEP.GloftA8HP"

/* The engine. The mod ships it as 71 pieces hidden in five byte-scrambled zips
 * among its assets; the wrapper puts them together on the first launch
 * (a8r_setup.c), with the pieces of the options the user picked. */
#define A8R_LIB_GAME    "libasphalt8.so"

/* The game data folder, exactly as the mod's instructions lay it out on a phone
 * (/storage/emulated/0/gameloft/games/GloftA8HP): the user copies the
 * "gameloft" folder of A8R DATA.zip, with A8R DATA December.zip extracted over
 * games/GloftA8HP, into the game folder. */
#define A8R_DATA_DIR    "gameloft/games/GloftA8HP"
#define A8R_ANDROID_SD  "/storage/emulated/0"
#define A8R_ANDROID_DATA A8R_ANDROID_SD "/" A8R_DATA_DIR
#define A8R_OBB_NAME    "main.sa2.Asphalt8.obb"

/* The reserved region each game module is mapped into. libasphalt8.so's
 * highest p_vaddr+p_memsz is 0x1b74e2c (~27.5 MB: 27 MB of code, 0.5 MB of
 * data and bss). A module larger than this is refused by so_load (-3). */
#define SO_REGION_BYTES (40u * 1024 * 1024)

/* Left outside the heap for kernel-side allocations. GPU buffers come from
 * the heap (libdrm_nouveau memaligns them and hands them to nvmap). */
#define GFX_RESERVE_MB  16u

/* The render size the self-test uses (the game's own size comes from
 * config.ini [display], dcr_config.c). */
#define DCR_FORCE_SCREEN_W 1280
#define DCR_FORCE_SCREEN_H 720

#define DEBUG_LOG 1

/* The renderer: 1 = mesa/nouveau (gl_mesa.c, portlibs32/ from
 * mesa32), 0 = null GL (gl_null.c: runs the game, draws
 * nothing). Set by the Makefile. */
#ifndef DCR_GL_MESA
#define DCR_GL_MESA 0
#endif

#endif /* DCR_CONFIG_H */
