/* a8r.h -- what the Asphalt 8: Retry files share with each other. MIT. */
#ifndef A8R_H
#define A8R_H
#include <stdint.h>

#include "bionic_io.h"
#include "jni.h"
#include "so_util.h"

/* ------------------------------------------------------------- classes */
#define A8R_JPKG "com/gameloft/android/HEP/GloftA8HP"
#define C_GAME       A8R_JPKG "/Game"
#define C_GL2JNILIB  A8R_JPKG "/GL2JNILib"
#define C_SUTILS     A8R_JPKG "/GLUtils/SUtils"
#define C_DEVICE     A8R_JPKG "/GLUtils/Device"
#define C_DATASHARE  A8R_JPKG "/DataSharing"
#define C_INSTALLER  A8R_JPKG "/installer/GameInstaller"
#define C_SENDINFO   A8R_JPKG "/SendInfo"
#define C_PUSH       A8R_JPKG "/PushNotification/SimplifiedAndroidUtils"
#define C_IAB        A8R_JPKG "/iab/InAppBilling"
#define C_HID        A8R_JPKG "/GLUtils/controller/NativeBridgeHIDControllers"

/* ------------------------------------------------------------ loader */
extern so_module g_mod_game;               /* libasphalt8.so (a8r_loader.c) */
int a8r_load_engine(void);                 /* 0 on success */
void a8r_run_constructors(void);
int a8r_boot_run(void);                    /* a8r_boot.c: the activity, the GL thread, the UI loop */
void *a8r_native(const char *symbol);      /* an engine export, or NULL */

/* -------------------------------------------------------------- setup */
void a8r_setup(const char *apk);           /* a8r_setup.c: the engine, the data */
/* New A8R zips in the game folder: back to the launcher, which installs them. */
void dcr_setup_zips_to_launcher(void);
const char *a8r_apk_path(void);
/* Contents of an asset of A8R.apk (malloc'd, *len set), or NULL. */
uint8_t *a8r_apk_asset(const char *name, size_t *len);
/* The APK's signing certificate as Signature.hashCode() sees it (0 if none). */
int32_t a8r_signature_hash(void);

/* --------------------------------------------------------------- Java */
extern JObj *g_activity;                   /* the Game activity (a Context) */
void a8r_java_init(void);
/* A Java class object, for the natives' jclass argument. */
void *a8r_class(const char *name);
/* The render size the game was told (GetPhoneWidth/Height, resize). */
int a8r_screen_w(void);
int a8r_screen_h(void);

/* ----------------------------------------------------- the GL thread */
/* GL2JNILib.createView: the activity asked for its GL2JNIView. */
void a8r_view_requested(void);
/* GL2JNILib.setViewSettings: the engine's pixel format (color bits, depth,
 * stencil, samples, coverage AA). */
void a8r_set_view_settings(int color, int depth, int stencil, int samples, int coverage);
/* GL2JNILib.setCurrentContext(i): context i current on the calling thread
 * (-1: none). */
int a8r_set_current_context(int i);
/* Queue a touch (GL2JNIView's h runnable): runs on the GL thread before the
 * next frame. type: 1 down, 0 up, 2 move. */
void a8r_post_touch(int type, int x, int y, int id);
int a8r_gl_running(void);                  /* frames are being stepped */
void a8r_request_exit(void);               /* GL2JNILib.Exit / RestartGame */

/* -------------------------------------------------------------- performance */
void a8r_perf_clocks(void);       /* a8r_perf.c: the clocks, frame times */
void a8r_perf_exit(void);         /* the CPU back to its normal clock */
void a8r_perf_focus(int focused); /* the HOME menu, sleep: the normal CPU clock */
int a8r_cpu_managed(void);        /* the CPU clock is this program's (clkrst / pcv) */
int a8r_cpu_hands_off(void);      /* the clock is the system's or a tool's: no boost of any kind */
void a8r_cpu_boost(int on);       /* loading: the CPU at 1785 MHz, the GPU untouched */
void a8r_perf_gl_thread(void);    /* the GL thread, once: above the other guest threads */
int a8r_glthread_up(void);        /* Mesa's glthread worker is running */
/* the GL thread's file work is counted apart (bionic_io.c: dcr_io_gl_stats) */
#define g_a8r_on_gl_thread dcr_io_tagged_thread
void a8r_perf_frame(u64 step_ticks, u64 swap_ticks);
void a8r_perf_report(void);
void a8r_perf_gpu_begin(void);
void a8r_perf_gpu_end(void);
void a8r_gl_hooks(void);          /* a8r_gl.c: the frame hook, before the first frame */

/* -------------------------------------------------------------- input */
void a8r_input_hw_init(void);   /* controllers + touch, before the engine */
int a8r_input_minus_held(void);
void a8r_input_init(void);
void a8r_input_poll(void);                 /* the UI thread, every ~8 ms */
void a8r_input_game_ready(void);           /* announce the controller (Game.D && Game.E) */

/* -------------------------------------------------------------- audio */
uint32_t dcr_audio_writes(void);           /* opensles.c */
void dcr_audio_pause(int paused);
void dcr_audio_close(void);
#define DCR_AUDIO_FRAMES 1024               /* opensles.c FRAMES_PER_BUF */
int dcr_audio_out_open(void);               /* the start screen's sound */
unsigned dcr_audio_out_rate(void);
void dcr_audio_out_submit(const int16_t *frames);

#endif
