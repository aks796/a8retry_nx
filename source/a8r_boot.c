/* a8r_boot.c -- plays the game's Android activity and its GL view.
 *
 * On a phone the mod's start screen starts the activity with role "entry",
 * com.gameloft.android.HEP.GloftA8HP.Game. What it does (decompiled), and what
 * this file does for it:
 *
 *   Game.onCreate    GameInstaller (for result): System.load(libasphalt8.so)
 *                    -> constructors (main.c) and JNI_OnLoad. Then Game.h():
 *                      SUtils.init()             SUtils.nativeInit
 *                      Device.init()             Device.nativeInit
 *                      DataSharing.doNativeInit  DataSharing.nativeInit
 *                      nativeSetXperiaPlay(false)
 *                      GL2JNILib.init()          -> the engine resolves 47 Java
 *                                                   methods and calls createView
 *                      SendInfo.setContext       SendInfo.initMethods
 *                      SimplifiedAndroidUtils    nativeInit
 *                      InAppBilling.init         nativeInit(context)
 *                      PlatformAndroid, Facebook-, GameAPIAndroidGLSocialLib nativeInit
 *                      Game.nativeInit
 *   onStart          GL2JNILib.onResume
 *   the view         GL2JNIView (its own GLSurfaceView), on its GL thread:
 *                      ConfigChooser: initGL, getViewSettings (the engine
 *                        makes its glf::App and calls setViewSettings back)
 *                      ContextFactory: a GLES 3 or 2 context,
 *                        Game.setOpenGlesVersion, getNumExtraContext shared
 *                        contexts for the engine's loading threads (on 1x1
 *                        pbuffers there; surfaceless here: Mesa's Switch EGL
 *                        has window surfaces only), setNumExtraContext if fewer
 *                      onSurfaceCreated -> stateChanged(true)
 *                      onSurfaceChanged -> resize(w, h)
 *                      onDrawFrame      -> step(), then the swap
 *                      touch events are queued to this thread (touchEvent)
 *   onPause / focus  Game.Pause: stateChanged(false) run on the GL thread,
 *                    waited for; Game.Resume: stateChanged(true)
 *
 * After start-up the main thread is the UI thread: it reads the controllers
 * and the touchscreen (a8r_input.c) and turns the Switch's focus changes into
 * onPause / onResume. The game runs on the GL thread (its frame loop is in
 * step()) and its own worker threads. MIT.
 */
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <switch.h>

#include "a8r.h"
#include "bionic_pthread.h"
#include "a8r_menu.h"
#include "bionic.h"
#include "config.h"
#include "dcr_config.h"
#include "dcr_sched.h"
#include "dcr_time.h"
#include "error.h"
#include "gl_layer.h"
#include "util.h"

void dcr_watchdog_start(void);
void dcr_boost_poll(void);
void dcr_boost_report(void);
void dcr_boost_launch_end(void);
void dcr_apkcache_report(void);
void dcr_window_prepare(void);          /* android_ndk.c */
void dcr_window_size(int *w, int *h);
void dcr_config_locale(const char *lang, const char *country, int density);
int b_pthread_create(b_pthread_t *out, const b_pthread_attr_t *attr, void *(*start)(void *), void *arg);
int b_pthread_create_on(b_pthread_t *out, const b_pthread_attr_t *attr, void *(*start)(void *), void *arg,
                        int prio, int core);

/* ------------------------------------------------------------ EGL (the
 * GL layer's egl* entry points: gl_mesa.c / gl_null.c) */
typedef void *EGLDisplay, *EGLConfig, *EGLContext, *EGLSurface;
typedef int32_t EGLint;
typedef uint32_t EGLBoolean;
#define EGL_NONE 0x3038
#define EGL_ALPHA_SIZE 0x3021
#define EGL_BLUE_SIZE 0x3022
#define EGL_GREEN_SIZE 0x3023
#define EGL_RED_SIZE 0x3024
#define EGL_DEPTH_SIZE 0x3025
#define EGL_STENCIL_SIZE 0x3026
#define EGL_SURFACE_TYPE 0x3033
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_EXTENSIONS 0x3055
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_WINDOW_BIT 0x0004
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_OPENGL_ES3_BIT 0x0040
EGLDisplay b_eglGetDisplay(void *d);
EGLBoolean b_eglInitialize(EGLDisplay d, EGLint *maj, EGLint *min);
EGLint b_eglGetError(void);
const char *b_eglQueryString(EGLDisplay d, EGLint name);
EGLBoolean b_eglChooseConfig(EGLDisplay d, const EGLint *attrs, EGLConfig *cfgs, EGLint cap, EGLint *num);
EGLContext b_eglCreateContext(EGLDisplay d, EGLConfig c, EGLContext share, const EGLint *attrs);
EGLBoolean b_eglDestroyContext(EGLDisplay d, EGLContext c);
EGLSurface b_eglCreateWindowSurface(EGLDisplay d, EGLConfig c, void *w, const EGLint *attrs);
EGLBoolean b_eglMakeCurrent(EGLDisplay d, EGLSurface dr, EGLSurface rd, EGLContext c);
EGLBoolean b_eglSwapBuffers(EGLDisplay d, EGLSurface s);
EGLBoolean b_eglSwapInterval(EGLDisplay d, EGLint i);

/* ------------------------------------------------------------ natives */
typedef jint (*fn_onload)(void *vm, void *reserved);
typedef void (*fn_v)(void *env, void *cls);
typedef void (*fn_z)(void *env, void *cls, jboolean z);
typedef void (*fn_i)(void *env, void *cls, jint i);
typedef void (*fn_ii)(void *env, void *cls, jint a, jint b);
typedef void (*fn_iiii)(void *env, void *cls, jint a, jint b, jint c, jint d);
typedef void (*fn_o)(void *env, void *cls, void *o);
typedef jint (*fn_ri)(void *env, void *cls);

#define NATIVE(cls, name) "Java_com_gameloft_android_HEP_GloftA8HP_" cls "_" name
#define GLN(name) NATIVE("GL2JNILib", name)

static void *need(const char *symbol) {
  void *p = a8r_native(symbol);
  if (!p)
    debugPrintf("[boot] MISSING native %s\n", symbol);
  return p;
}

static void call_v(const char *symbol, const char *cls) {
  fn_v f = (fn_v)need(symbol);
  if (f) {
    debugPrintf("[boot] %s\n", strstr(symbol, "GloftA8HP_") ? strstr(symbol, "GloftA8HP_") + 10 : symbol);
    f(g_jni_env, a8r_class(cls));
  }
}

/* ============================================================ the GL view */
#define MAX_EXTRA 4

static struct {
  int color, depth, stencil, samples, coverage; /* setViewSettings */
} g_vs = {32, 16, 0, 0, 0};

static EGLDisplay g_dpy;
static EGLConfig g_cfg;
static EGLContext g_ctx[1 + MAX_EXTRA];
static EGLSurface g_surf;
static int g_nextra, g_gles = 2;
static int g_w = 1280, g_h = 720;

static volatile int g_view_requested, g_gl_up, g_gl_running;
static volatile int g_exit, g_paused, g_focused = 1, g_focus_changed, g_started;
static uint64_t g_steps;

/* The UI thread -> GL thread queue: touches (GL2JNIView's h runnables) and
 * the pause / resume / exit requests (Game.Pause's IntSender). */
enum { EV_TOUCH, EV_PAUSE, EV_RESUME, EV_EXIT };
typedef struct {
  int kind, a, b, c, d;
} GlEvent;
#define QLEN 256
static GlEvent g_q[QLEN];
static int g_qhead, g_qcount;
static Mutex g_qlock;
static CondVar g_qcv;
static volatile uint32_t g_acks; /* EV_PAUSE / EV_RESUME / EV_EXIT done */

static void post(int kind, int a, int b, int c, int d) {
  mutexLock(&g_qlock);
  if (g_qcount < QLEN) {
    g_q[(g_qhead + g_qcount) % QLEN] = (GlEvent){kind, a, b, c, d};
    g_qcount++;
  }
  condvarWakeAll(&g_qcv);
  mutexUnlock(&g_qlock);
}

/* Post and wait until the GL thread has run it (at most `ms`). */
static void post_wait(int kind, int ms) {
  uint32_t before = g_acks;
  post(kind, 0, 0, 0, 0);
  u64 t0 = armGetSystemTick();
  while (g_acks == before && g_gl_up && armTicksToNs(armGetSystemTick() - t0) < (u64)ms * 1000000ull)
    svcSleepThread(2000000ll);
}

void a8r_post_touch(int type, int x, int y, int id) {
  if (g_gl_running)
    post(EV_TOUCH, type, x, y, id);
}

int a8r_gl_running(void) { return g_gl_running; }
void a8r_request_exit(void) { g_exit = 1; }

int a8r_screen_w(void) { return g_w; }
int a8r_screen_h(void) { return g_h; }

void a8r_view_requested(void) {
  debugPrintf("[boot] GL2JNILib.createView: the GL view is made once the activity is up\n");
  g_view_requested = 1;
}

void a8r_set_view_settings(int color, int depth, int stencil, int samples, int coverage) {
  g_vs.color = color, g_vs.depth = depth, g_vs.stencil = stencil;
  g_vs.samples = samples, g_vs.coverage = coverage;
  debugPrintf("[boot] setViewSettings: %d-bit colour, depth %d, stencil %d, %d samples, coverage %d\n",
              color, depth, stencil, samples, coverage);
}

/* GL2JNIView.a(i): context i current on the calling thread (the engine's
 * glf::App::ReserveContext / SetAsMainThread), -1 none. */
int a8r_set_current_context(int i) {
  if (!g_dpy)
    return 0;
  EGLBoolean r;
  if (i < 0)
    r = b_eglMakeCurrent(g_dpy, NULL, NULL, NULL);
  else if (i == 0)
    r = b_eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx[0]);
  else if (i <= g_nextra)
    r = b_eglMakeCurrent(g_dpy, NULL, NULL, g_ctx[i]); /* surfaceless */
  else
    r = 0;
  static int logged;
  if (logged++ < 16 || !r)
    debugPrintf("[egl] setCurrentContext(%d) on thread %p -> %d\n", i, (void *)threadGetCurHandle(), (int)r);
  return r != 0;
}

static EGLConfig choose_config(void) {
  const int want_es3 = g_gles >= 3;
  const int rgb = g_vs.color == 16 ? 5 : 8;
  for (int attempt = 0; attempt < 4; attempt++) {
    /* 0: as asked; 1: RGBA8888 (the Switch platform's window configs);
     * 2: without stencil; 3: anything ES2-renderable */
    EGLint attrs[24];
    int n = 0;
    attrs[n++] = EGL_SURFACE_TYPE, attrs[n++] = EGL_WINDOW_BIT;
    attrs[n++] = EGL_RENDERABLE_TYPE, attrs[n++] = want_es3 && attempt < 3 ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_ES2_BIT;
    if (attempt < 3) {
      int r = attempt == 0 ? rgb : 8;
      attrs[n++] = EGL_RED_SIZE, attrs[n++] = r;
      attrs[n++] = EGL_GREEN_SIZE, attrs[n++] = r == 5 ? 6 : 8;
      attrs[n++] = EGL_BLUE_SIZE, attrs[n++] = r;
      if (attempt > 0 || g_vs.color == 32)
        attrs[n++] = EGL_ALPHA_SIZE, attrs[n++] = 8;
      attrs[n++] = EGL_DEPTH_SIZE, attrs[n++] = g_vs.depth > 0 ? g_vs.depth : 16;
      if (attempt < 2)
        attrs[n++] = EGL_STENCIL_SIZE, attrs[n++] = g_vs.stencil;
    }
    attrs[n] = EGL_NONE;
    EGLConfig cfg = NULL;
    EGLint num = 0;
    if (b_eglChooseConfig(g_dpy, attrs, &cfg, 1, &num) && num > 0) {
      if (attempt)
        debugPrintf("[egl] config: the engine's own request matched nothing; took fallback %d\n", attempt);
      return cfg;
    }
  }
  return NULL;
}

static void egl_up(void) {
  g_dpy = b_eglGetDisplay(NULL);
  EGLint maj = 0, min = 0;
  if (!b_eglInitialize(g_dpy, &maj, &min))
    fatal_error("eglInitialize failed (0x%x).", (unsigned)b_eglGetError());
  const char *ext = b_eglQueryString(g_dpy, EGL_EXTENSIONS);
  const int surfaceless = ext && strstr(ext, "EGL_KHR_surfaceless_context");
  debugPrintf("[egl] EGL %d.%d; surfaceless contexts %s\n", (int)maj, (int)min,
              surfaceless ? "supported" : "NOT supported");

  g_gles = dcr_config()->gles;
  g_cfg = choose_config();
  if (!g_cfg)
    fatal_error("No EGL configuration for the game's %d-bit / depth %d request.", g_vs.color, g_vs.depth);

  /* GL2JNIView$g: GLES 3 where offered, else 2; then Game.setOpenGlesVersion */
  EGLint cattr[] = {EGL_CONTEXT_CLIENT_VERSION, g_gles, EGL_NONE};
  g_ctx[0] = b_eglCreateContext(g_dpy, g_cfg, NULL, cattr);
  if (!g_ctx[0] && g_gles == 3) {
    debugPrintf("[egl] no GLES 3 context: trying 2, as GL2JNIView does\n");
    g_gles = cattr[1] = 2;
    g_ctx[0] = b_eglCreateContext(g_dpy, g_cfg, NULL, cattr);
  }
  if (!g_ctx[0])
    fatal_error("Could not create a GLES %d context (0x%x).", g_gles, (unsigned)b_eglGetError());
  fn_i set_ver = (fn_i)need(NATIVE("Game", "setOpenGlesVersion"));
  if (set_ver)
    set_ver(g_jni_env, a8r_class(C_GAME), g_gles);

  /* the loading threads' shared contexts */
  fn_ri get_extra = (fn_ri)need(GLN("getNumExtraContext"));
  int want = get_extra ? get_extra(g_jni_env, a8r_class(C_GL2JNILIB)) : 0;
  int allow = want < dcr_config()->loader_contexts ? want : dcr_config()->loader_contexts;
  if (allow > MAX_EXTRA)
    allow = MAX_EXTRA;
  g_nextra = 0;
  for (int i = 1; i <= allow && surfaceless; i++) {
    EGLContext c = b_eglCreateContext(g_dpy, g_cfg, g_ctx[0], cattr);
    if (!c)
      break;
    if (!b_eglMakeCurrent(g_dpy, NULL, NULL, c)) { /* the surfaceless test */
      debugPrintf("[egl] extra context %d cannot be made current without a surface (0x%x)\n", i,
                  (unsigned)b_eglGetError());
      b_eglDestroyContext(g_dpy, c);
      break;
    }
    g_ctx[++g_nextra] = c;
  }
  b_eglMakeCurrent(g_dpy, NULL, NULL, NULL);
  debugPrintf("[egl] GLES %d, %d of the %d loading context%s the engine asked for\n", g_gles, g_nextra,
              want, want == 1 ? "" : "s");
  if (g_nextra != want) {
    fn_i set_extra = (fn_i)need(GLN("setNumExtraContext"));
    if (set_extra)
      set_extra(g_jni_env, a8r_class(C_GL2JNILIB), g_nextra);
  }

  /* the window surface (the GL layer hands the screen over from the boot log) */
  dcr_window_size(&g_w, &g_h);
  g_surf = b_eglCreateWindowSurface(g_dpy, g_cfg, nwindowGetDefault(), NULL);
  if (!g_surf)
    fatal_error("Could not create the window surface (0x%x).", (unsigned)b_eglGetError());
  if (dcr_config()->gl_thread) { /* the driver's work on a second core (a8r_perf.c) */
    int b_egl_start_glthread(EGLDisplay d, EGLContext c); /* gl_mesa.c */
    int ok = b_egl_start_glthread(g_dpy, g_ctx[0]);
    debugPrintf("[egl] glthread for the game's context: %s\n",
                ok && a8r_glthread_up() ? "on" : "could not start (the driver runs on this thread)");
  }
  if (!b_eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx[0]))
    fatal_error("Could not make the GL context current (0x%x).", (unsigned)b_eglGetError());
  b_eglSwapInterval(g_dpy, 1);
}

/* the GL thread's CPU time, for the 10 s report: close to 100% of the
 * wall-clock time, and the frame rate is bound by the CPU; well under, it
 * waits (for the GPU, or vsync) */
static Handle g_gl_handle = INVALID_HANDLE;

static u64 thread_ticks(Handle h) {
  u64 v = 0;
  if (R_FAILED(svcGetInfo(&v, InfoType_ThreadTickCount, h, TickCountInfo_Total)))
    svcGetInfo(&v, InfoType_ThreadTickCountDeprecated, h, TickCountInfo_Total);
  return v;
}

static void *gl_thread(void *arg) {
  (void)arg;
  a8r_perf_gl_thread(); /* above the other guest threads (a8r_perf.c) */
  BThread *self = b_thread_self();
  if (self) {
    g_gl_handle = self->handle;
    void dcr_prof_gl_thread(int tid); /* a8r_prof.c: the steady windows' thread */
    dcr_prof_gl_thread(self->tid);
  }
  void *gl = a8r_class(C_GL2JNILIB);
  /* GL2JNIView.ConfigChooser */
  fn_v init_gl = (fn_v)need(GLN("initGL")), view_settings = (fn_v)need(GLN("getViewSettings"));
  if (init_gl)
    init_gl(g_jni_env, gl);
  if (view_settings)
    view_settings(g_jni_env, gl);
  egl_up();

  fn_z state = (fn_z)need(GLN("stateChanged"));
  fn_ii resize = (fn_ii)need(GLN("resize"));
  fn_v step = (fn_v)need(GLN("step"));
  fn_iiii touch = (fn_iiii)need(GLN("touchEvent"));
  if (!state || !resize || !step)
    fatal_error("libasphalt8.so lacks GL2JNILib's natives: is it the December build of A8R?");

  debugPrintf("[boot] onSurfaceCreated -> stateChanged(true)\n");
  state(g_jni_env, gl, 1);
  debugPrintf("[boot] onSurfaceChanged -> resize(%d, %d)\n", g_w, g_h);
  resize(g_jni_env, gl, g_w > g_h ? g_w : g_h, g_w > g_h ? g_h : g_w);
  g_gl_up = 1;
  g_gl_running = 1;
  log_flush_ring();

  for (;;) {
    /* the queued runnables, in order */
    for (;;) {
      mutexLock(&g_qlock);
      if (!g_qcount) {
        mutexUnlock(&g_qlock);
        break;
      }
      GlEvent e = g_q[g_qhead];
      g_qhead = (g_qhead + 1) % QLEN;
      g_qcount--;
      mutexUnlock(&g_qlock);
      switch (e.kind) {
      case EV_TOUCH:
        if (touch && !g_paused)
          touch(g_jni_env, gl, e.a, e.b, e.c, e.d);
        break;
      case EV_PAUSE:
        if (!g_paused) {
          state(g_jni_env, gl, 0); /* Game.Pause: the game saves and stops */
          g_paused = 1;
        }
        g_acks++;
        break;
      case EV_RESUME:
        if (g_paused) {
          state(g_jni_env, gl, 1);
          g_paused = 0;
        }
        g_acks++;
        break;
      case EV_EXIT:
        if (!g_paused)
          state(g_jni_env, gl, 0);
        g_gl_running = 0;
        g_acks++;
        debugPrintf("[boot] GL thread done\n");
        return NULL;
      }
    }
    if (g_paused) {
      mutexLock(&g_qlock);
      if (!g_qcount)
        condvarWaitTimeout(&g_qcv, &g_qlock, 50000000ll);
      mutexUnlock(&g_qlock);
      continue;
    }
    u64 t0 = armGetSystemTick();
    step(g_jni_env, gl); /* GL2JNIView$i.a(): the engine's whole frame */
    g_steps++;
    u64 t1 = armGetSystemTick();
    b_eglSwapBuffers(g_dpy, g_surf);
    a8r_perf_frame(t1 - t0, armGetSystemTick() - t1); /* a8r_perf.c */
  }
}

/* ---------------------------------------------------------- the watchdog */
uint64_t dcr_boot_frames(void) { return dcr_gl_frames(); }
int dcr_boot_in_focus(void) { return g_focused && g_started && !g_paused; }

/* ============================================================== lifecycle */
static AppletHookCookie g_hook;

static void on_applet(AppletHookType type, void *param) {
  static const char *const names[] = {"focus state", "operation mode", "performance mode",
                                      "EXIT REQUEST", "resume", "capture button",
                                      "screenshot taken", "request to display"};
  if (type == AppletHookType_OnExitRequest)
    debugPrintf("[applet] the system asked the game to close\n");
  else if ((unsigned)type < sizeof names / sizeof names[0])
    debugPrintf("[applet] %s (focus %d, mode %d)\n", names[type], (int)appletGetFocusState(),
                (int)appletGetOperationMode());
  if (type == AppletHookType_OnFocusState || type == AppletHookType_OnOperationMode) {
    int focused = appletGetFocusState() == AppletFocusState_InFocus;
    if (focused != g_focused) {
      g_focused = focused;
      g_focus_changed = 1;
    }
  }
}

static void apply_focus(void) {
  if (!g_focus_changed || !g_started)
    return;
  g_focus_changed = 0;
  if (!g_focused) {
    debugPrintf("[boot] focus lost: onPause\n");
    a8r_perf_focus(0); /* the normal CPU clock for the HOME menu */
    post_wait(EV_PAUSE, 3000);
    dcr_audio_pause(1);
    log_flush_ring();
    dcr_time_suspend();
  } else {
    a8r_perf_focus(1);
    dcr_time_resume();
    dcr_audio_pause(0);
    call_v(GLN("onResume"), C_GL2JNILIB); /* onStart */
    post_wait(EV_RESUME, 3000);           /* onResume: Game.Resume */
    debugPrintf("[boot] focus regained: onResume\n");
  }
}

static void exit_guard(void *arg) {
  (void)arg;
  svcSleepThread(5000000000ll);
  debugPrintf("[boot] the game did not close within 5 s: ending the process\n");
  a8r_perf_exit();
  log_flush_ring();
  svcExitProcess();
}

/* Test aid: with a file ".touch_script" in the game folder, taps are played
 * on the touchscreen after the first frame -- a line "<seconds> <x> <y>"
 * (x, y in thousandths of the screen) taps there; "<seconds> cap" saves a
 * capture (gl_mesa.c). For driving the menus into a race where no controller
 * is at hand (an emulator). */
const char *dcr_game_root(void); /* main.c */
#define TS_MAX 64
static struct {
  u64 at; /* ns after the first frame */
  int x, y, cap;
} g_ts[TS_MAX];
static int g_ts_n, g_ts_next, g_ts_up = -1;
static u64 g_ts_up_at;

static void touch_script_load(void) {
  char p[300];
  snprintf(p, sizeof p, "%s/.touch_script", dcr_game_root());
  FILE *f = fopen(p, "r");
  if (!f)
    return;
  char line[128];
  while (g_ts_n < TS_MAX && fgets(line, sizeof line, f)) {
    double t;
    int x, y;
    char w[8];
    if (sscanf(line, "%lf %d %d", &t, &x, &y) == 3)
      g_ts[g_ts_n].at = (u64)(t * 1e9), g_ts[g_ts_n].x = x, g_ts[g_ts_n].y = y, g_ts[g_ts_n++].cap = 0;
    else if (sscanf(line, "%lf %7s", &t, w) == 2 && !strcmp(w, "cap"))
      g_ts[g_ts_n].at = (u64)(t * 1e9), g_ts[g_ts_n++].cap = 1;
  }
  fclose(f);
  debugPrintf("[test] .touch_script: %d steps\n", g_ts_n);
}

static void touch_script_poll(u64 since_first_ns) {
  if (g_ts_up >= 0 && armGetSystemTick() >= g_ts_up_at) {
    a8r_post_touch(0, g_ts[g_ts_up].x * g_w / 1000, g_ts[g_ts_up].y * g_h / 1000, 0);
    g_ts_up = -1;
  }
  if (g_ts_up >= 0 || g_ts_next >= g_ts_n || since_first_ns < g_ts[g_ts_next].at)
    return;
  int i = g_ts_next++;
  if (g_ts[i].cap) {
    void dcr_gl_request_capture(void); /* gl_mesa.c */
    dcr_gl_request_capture();
    return;
  }
  debugPrintf("[test] tap %d/%d at %d,%d\n", i + 1, g_ts_n, g_ts[i].x, g_ts[i].y);
  a8r_post_touch(1, g_ts[i].x * g_w / 1000, g_ts[i].y * g_h / 1000, 0);
  g_ts_up = i;
  g_ts_up_at = armGetSystemTick() + armNsToTicks(100000000ull);
}

static void report(void) {
  static u64 last_tick;
  static unsigned long last_presented;
  u64 tick = armGetSystemTick();
  unsigned long presented = (unsigned long)dcr_gl_frames();
  double fps = last_tick ? (double)(presented - last_presented) * 1e9 / (double)armTicksToNs(tick - last_tick) : 0.0;
  u64 span = last_tick ? tick - last_tick : 0;
  last_tick = tick;
  last_presented = presented;
  static u64 last_gl;
  u64 gl = g_gl_handle != INVALID_HANDLE ? thread_ticks(g_gl_handle) : 0;
  unsigned gl_pct = last_gl && span ? (unsigned)((gl - last_gl) * 100 / span) : 0;
  last_gl = gl;
  struct mallinfo mi = mallinfo();
  u64 b_mmap_bytes(void); /* bionic_mem.c */
  debugPrintf("[boot] %lu frames presented (%.1f fps; GL thread on the CPU %u%%), %lu audio buffers, %d Java "
              "objects; heap: %u MB in use, %u MB free in the arena (%u MB), mmap %u MB\n",
              presented, fps, gl_pct, (unsigned long)dcr_audio_writes(), jni_live_objects(),
              (unsigned)(mi.uordblks >> 20), (unsigned)(mi.fordblks >> 20), (unsigned)(mi.arena >> 20),
              (unsigned)(b_mmap_bytes() >> 20));
  dcr_boost_report();
  dcr_apkcache_report();
  a8r_perf_report();
}

int a8r_boot_run(void) {
  a8r_perf_clocks(); /* the clocks (a8r_perf.c; normally set at PLAY already) */
  mutexInit(&g_qlock);
  condvarInit(&g_qcv);
  a8r_java_init();
  dcr_config_locale("en", "US", dcr_config()->res_h >= 1080 ? 320 : 213);
  dcr_watchdog_start();
  a8r_input_init();
  dcr_window_size(&g_w, &g_h);

  /* ---- GameInstaller: System.load -> JNI_OnLoad ---- */
  fn_onload onload = (fn_onload)need("JNI_OnLoad");
  if (onload)
    debugPrintf("[boot] JNI_OnLoad -> 0x%lx\n", (unsigned long)onload(g_jni_vm, NULL));

  /* ---- Game.h() ---- */
  call_v(NATIVE("GLUtils_SUtils", "nativeInit"), C_SUTILS);
  call_v(NATIVE("GLUtils_Device", "nativeInit"), C_DEVICE);
  call_v(NATIVE("DataSharing", "nativeInit"), C_DATASHARE);
  fn_z xperia = (fn_z)need(GLN("nativeSetXperiaPlay"));
  if (xperia)
    xperia(g_jni_env, a8r_class(C_GL2JNILIB), 0);
  call_v(GLN("init"), C_GL2JNILIB);
  call_v(NATIVE("SendInfo", "initMethods"), C_SENDINFO);
  call_v(NATIVE("PushNotification_SimplifiedAndroidUtils", "nativeInit"), C_PUSH);
  fn_o iab = (fn_o)need(NATIVE("iab_InAppBilling", "nativeInit"));
  if (iab) {
    debugPrintf("[boot] iab_InAppBilling_nativeInit\n");
    iab(g_jni_env, a8r_class(C_IAB), g_activity);
  }
  call_v("Java_com_gameloft_GLSocialLib_PlatformAndroid_nativeInit", "com/gameloft/GLSocialLib/PlatformAndroid");
  call_v("Java_com_gameloft_GLSocialLib_facebook_FacebookAndroidGLSocialLib_nativeInit",
         "com/gameloft/GLSocialLib/facebook/FacebookAndroidGLSocialLib");
  call_v("Java_com_gameloft_GLSocialLib_GameAPI_GameAPIAndroidGLSocialLib_nativeInit",
         "com/gameloft/GLSocialLib/GameAPI/GameAPIAndroidGLSocialLib");
  call_v(NATIVE("Game", "nativeInit"), C_GAME);

  /* ---- onStart, onResume ---- */
  call_v(GLN("onResume"), C_GL2JNILIB);
  appletHook(&g_hook, on_applet, NULL);
  if (!g_view_requested)
    debugPrintf("[boot] WARNING: the engine never asked for its view (createView); making it anyway\n");

  /* ---- the GL view's thread (a guest thread: the engine runs on it) ---- */
  b_pthread_attr_t attr = {0};
  attr.stack_size = 2u << 20;
  b_pthread_t th;
  /* one step above the other guest threads, from its first instruction
   * (a8r_perf.c: a8r_perf_gl_thread) */
  if (b_pthread_create_on(&th, &attr, gl_thread, NULL, DCR_GUEST_PRIO - 1, 1) != 0)
    fatal_error("Could not start the rendering thread.");
  g_started = 1;
  debugPrintf("[boot] activity up; this thread is the UI thread now\n");
  log_flush_ring();

  /* ---- the UI thread ---- */
  u64 last_input = 0, last_report = armGetSystemTick(), first_frame_at = 0;
  int launch_done = 0, announced = 0;
  unsigned long quiet_at = 0;
  const u64 input_period = armNsToTicks(8000000ull); /* 8 ms: twice per display frame */
  while (!g_exit && appletMainLoop()) {
    apply_focus();
    if (!g_focused || !g_gl_up) {
      svcSleepThread(20000000ll);
      continue;
    }
    u64 now = armGetSystemTick();
    if (now - last_input >= input_period) {
      last_input = now;
      a8r_input_poll();
    } else {
      svcSleepThread(2000000ll);
    }
    dcr_boost_poll();
    unsigned long frames = (unsigned long)dcr_gl_frames();
    if (!launch_done && frames > 0) {
      launch_done = 1;
      first_frame_at = now;
      dcr_boost_launch_end();
      debugPrintf("[boot] first frame presented\n");
      quiet_at = frames + 180;
      touch_script_load();
    }
    if (launch_done && g_ts_n)
      touch_script_poll(armTicksToNs(now - first_frame_at));
    /* Game.D (the 5 s splash timer) && Game.E (resumed): the controller
     * timer's tracking and connection calls start then. */
    if (launch_done && !announced && armTicksToNs(now - first_frame_at) >= 5000000000ull) {
      announced = 1;
      a8r_input_game_ready();
    }
    if (launch_done && dcr_config()->capture_secs) {
      static u64 last_capture;
      if (!last_capture)
        last_capture = now;
      if (armTicksToNs(now - last_capture) >= (u64)dcr_config()->capture_secs * 1000000000ull) {
        last_capture = now;
        void dcr_gl_request_capture(void); /* gl_mesa.c */
        dcr_gl_request_capture();
      }
    }
    if (quiet_at && frames >= quiet_at) {
      quiet_at = 0;
      log_set_quiet(1);
    }
    /* the start screen's "Auto backup" (the mod's AB service, every 10 s
     * there): the progress copied to gameloft/games/GloftA8HP/save */
    if (launch_done && dcr_config()->auto_backup) {
      static u64 last_backup;
      if (!last_backup)
        last_backup = now;
      if (armTicksToNs(now - last_backup) >= 30000000000ull) {
        last_backup = now;
        a8r_backup_progress();
      }
    }
    if (armTicksToNs(now - last_report) >= 10000000000ull) {
      last_report = now;
      report();
      log_flush_ring();
      log_console_update();
    }
  }

  /* Android's way out: onPause (the game saves), then the process ends. */
  debugPrintf("[boot] leaving: onPause, onStop, onDestroy\n");
  log_set_quiet(0);
  appletUnhook(&g_hook);
  if (!g_focused)
    dcr_time_resume(); /* frozen clocks would stall any timed wait in the shutdown */
  static Thread guard;
  if (R_SUCCEEDED(threadCreate(&guard, exit_guard, NULL, NULL, 0x4000, 0x2B, -2)))
    threadStart(&guard);
  post_wait(EV_EXIT, 4000);
  if (dcr_config()->auto_backup)
    a8r_backup_progress(); /* what the game saved on its way out */
  dcr_audio_close();
  a8r_perf_exit(); /* the CPU back to its normal clock */
  debugPrintf("[boot] the game has closed\n");
  log_flush_ring();
  return 0;
}
