/* a8r_perf.c -- the frame rate: where each frame's 16.7 ms go, and the clocks.
 *
 * The third hardware run (2026-09-25) raced at 31-50 fps with the GL thread
 * on the CPU only 60-75% of the time: the rest it waited, most likely for a
 * free display buffer -- Mesa's Switch platform dequeues without a fence, so
 * libnx waits there for the GPU to finish with it -- i.e. the GPU took longer
 * than the frame. To tell the CPU's share from the GPU's for certain, every
 * frame is measured and each 10 s report says ([perf] lines):
 *   cpu    the engine's frame on the GL thread (step(): game, scene, GL calls,
 *          Mesa), minus the time it spent blocked for a buffer;
 *   wait   blocked in nwindowDequeueBuffer (the linker wraps it): the GPU, or
 *          the display, holding every buffer;
 *   gpu    the GPU's own time for the frame: a GL_TIME_ELAPSED query around
 *          it (EXT_disjoint_timer_query), read three frames later so nothing
 *          waits for it;
 * with the share of frames over 16.7 ms (a missed 60 fps refresh) and over
 * 33.3 ms.
 *
 * The clocks: in handheld mode the Switch runs its GPU at 384 MHz; games may
 * ask for 460.8 MHz (performance configuration 0x92220007: CPU 1020, GPU
 * 460.8, memory 1600 MHz). [performance] gpu_boost_handheld asks for it at
 * start; the report says what the system actually gave. Docked, the GPU is
 * at 768 MHz already.
 *
 * The CPU (fourth hardware run, 2026-09-25): with the GPU at 460.8 the GPU's
 * frame was 5-8 ms, the wait for a buffer 2-4 ms -- the GPU was done -- and
 * the GL thread's own frame 15-23 ms: 55 fps in light stretches, 37-45 in
 * busy ones, the engine's CPU work (scene, draw submission, Mesa) at the
 * Switch's 1020 MHz, where the TV boxes and phones it was made for ran at 2
 * GHz. No performance configuration pairs a faster CPU with a usable GPU (the
 * boost mode's 1785 MHz comes with a 76.8 MHz GPU), but the clock driver
 * itself (clkrst, pcv before 8.0.0) sets the CPU alone: [performance]
 * cpu_clock, from PLAY on. The system puts its own clock back on a dock
 * change, after sleep or a boost-mode change, so a thread checks it every
 * 250 ms and sets it again; the HOME menu gets the normal clock. With the
 * clock driver, loading frames (dcr_boost.c) are boosted with the CPU clock
 * too, the GPU untouched.
 *
 * Overclocking tools (sys-clk, hoc-clk and the like) win. Only a change back
 * to the normal 1020 MHz is taken for the system's own reset and undone;
 * any other clock that is not the one this program set last is a tool's, and
 * from then on this program leaves the clock alone for the rest of the run
 * (no restoring, no loading boost, no clock for the HOME menu or at exit).
 * So does 1020 put back three times within 30 s (a tool holding 1020), a
 * clock that is neither 1020 nor cpu_clock when the game starts, and
 * cpu_clock = system. MIT.
 */
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "a8r.h"
#include "a8r_prof.h"
#include "bionic_pthread.h"
#include "dcr_config.h"
#include "dcr_sched.h"
#include "util.h"

/* ---------------------------------------------------------------- the CPU clock */
#define CPU_NORMAL_HZ 1020000000u
#define CPU_BOOST_HZ 1785000000u

static int g_clk_kind;            /* 0 none, 1 clkrst, 2 pcv */
static ClkrstSession g_clk;
static Mutex g_clk_mx;
static Thread g_clk_thread;
static volatile int g_clk_stop, g_clk_focus = 1, g_clk_boost;
static int g_clk_thread_up;
static u32 g_clk_want;            /* cpu_clock, in Hz */
static u32 g_clk_resets;          /* times the system had put its own clock back */
static u32 g_clk_last;            /* the clock this program set last (0: none yet) */
static volatile int g_clk_yield;  /* the clock is the system's or a tool's: never set it again */
static u64 g_clk_reset_at[3];     /* when the last three resets to 1020 came */

static int near_hz(u32 a, u32 b) { return a + 2000000u >= b && a <= b + 2000000u; }

/* From here on the clock is not this program's (said once). */
static void cpu_yield(const char *why, u32 now) {
  if (g_clk_yield)
    return;
  g_clk_yield = 1;
  debugPrintf("[perf] CPU clock left alone from here on (%u MHz): %s\n", (unsigned)(now / 1000000u), why);
}

static Result cpu_get(u32 *hz) {
  return g_clk_kind == 1 ? clkrstGetClockRate(&g_clk, hz) : pcvGetClockRate(PcvModule_CpuBus, hz);
}
static Result cpu_set(u32 hz) {
  return g_clk_kind == 1 ? clkrstSetClockRate(&g_clk, hz) : pcvSetClockRate(PcvModule_CpuBus, hz);
}

/* the clock this moment calls for, set if it is not what the CPU runs at --
 * unless someone else has changed it since this program last set it */
static void cpu_apply(void) {
  mutexLock(&g_clk_mx);
  u32 want = !g_clk_focus ? CPU_NORMAL_HZ : g_clk_boost ? CPU_BOOST_HZ : g_clk_want, now = 0;
  if (g_clk_kind && !g_clk_yield && R_SUCCEEDED(cpu_get(&now))) {
    if (g_clk_last && !near_hz(now, g_clk_last)) {
      if (!near_hz(now, CPU_NORMAL_HZ)) {
        cpu_yield("another program changed it (an overclocking tool?)", now);
      } else { /* the system's own reset: a dock change, sleep, boost mode */
        u64 t = armGetSystemTick();
        g_clk_reset_at[g_clk_resets % 3] = t;
        g_clk_resets++;
        if (g_clk_resets >= 3 && armTicksToNs(t - g_clk_reset_at[g_clk_resets % 3]) < 30000000000ull)
          cpu_yield("put back to 1020 MHz three times within 30 s (a tool holding it?)", now);
      }
    }
    if (!g_clk_yield) {
      if (!near_hz(now, want))
        cpu_set(want);
      g_clk_last = want;
    }
  }
  mutexUnlock(&g_clk_mx);
}

static void cpu_watch(void *arg) {
  while (!g_clk_stop) {
    svcSleepThread(250000000ll);
    if (g_clk_focus)
      cpu_apply();
  }
}

int a8r_cpu_managed(void) { return g_clk_kind != 0 && !g_clk_yield; }
int a8r_cpu_hands_off(void) { return g_clk_yield; }

void a8r_cpu_boost(int on) {
  if (!g_clk_kind || g_clk_yield || g_clk_boost == on)
    return;
  g_clk_boost = on;
  cpu_apply();
}

/* on_applet (a8r_boot.c): the HOME menu or sleep gets the normal clock */
void a8r_perf_focus(int focused) {
  if (!g_clk_kind || g_clk_yield || g_clk_focus == focused)
    return;
  g_clk_focus = focused;
  cpu_apply();
}

static void cpu_start(void) {
  if (!dcr_config()->cpu_clock) { /* cpu_clock = system */
    g_clk_yield = 1;
    debugPrintf("[perf] CPU clock: the system's (cpu_clock = system): not touched, no loading boost\n");
    return;
  }
  if (hosversionAtLeast(8, 0, 0)) {
    if (R_SUCCEEDED(clkrstInitialize())) {
      if (R_SUCCEEDED(clkrstOpenSession(&g_clk, PcvModuleId_CpuBus, 3)))
        g_clk_kind = 1;
      else
        clkrstExit();
    }
  } else if (R_SUCCEEDED(pcvInitialize())) {
    g_clk_kind = 2;
  }
  u32 was = 0;
  if (!g_clk_kind || R_FAILED(cpu_get(&was)) || was < 100000000u) {
    debugPrintf("[perf] no access to the CPU clock (%s): it stays the system's; loading is boosted "
                "the system's way\n", hosversionAtLeast(8, 0, 0) ? "clkrst" : "pcv");
    if (g_clk_kind == 1)
      clkrstCloseSession(&g_clk), clkrstExit();
    else if (g_clk_kind == 2)
      pcvExit();
    g_clk_kind = 0;
    return;
  }
  mutexInit(&g_clk_mx);
  g_clk_want = (u32)dcr_config()->cpu_clock * 1000000u;
  if (!near_hz(was, CPU_NORMAL_HZ) && !near_hz(was, g_clk_want))
    cpu_yield("it was not the normal clock when the game started (an overclocking tool?)", was);
  else if (near_hz(was, g_clk_want))
    g_clk_last = g_clk_want;
  cpu_apply();
  g_clk_resets = 0;
  u32 now = 0;
  cpu_get(&now);
  debugPrintf("[perf] CPU clock %u MHz (%s): was %u MHz, now %u MHz\n", (unsigned)(g_clk_want / 1000000u),
              g_clk_kind == 1 ? "clkrst" : "pcv", (unsigned)(was / 1000000u), (unsigned)(now / 1000000u));
  if (R_SUCCEEDED(threadCreate(&g_clk_thread, cpu_watch, NULL, NULL, 0x4000, 0x2C, -2)))
    g_clk_thread_up = R_SUCCEEDED(threadStart(&g_clk_thread));
  atexit(a8r_perf_exit);
}

/* the end of the game, a fatal error (atexit), the exit guard: once */
void a8r_perf_exit(void) {
  static int done;
  if (!g_clk_kind || __atomic_exchange_n(&done, 1, __ATOMIC_SEQ_CST))
    return;
  g_clk_stop = 1;
  if (g_clk_thread_up) {
    threadWaitForExit(&g_clk_thread);
    threadClose(&g_clk_thread);
  }
  mutexLock(&g_clk_mx);
  if (!g_clk_yield) /* a tool's clock, or the system's, stays */
    cpu_set(CPU_NORMAL_HZ);
  mutexUnlock(&g_clk_mx);
}

static u32 cpu_mhz_now(void) {
  u32 hz = 0;
  if (g_clk_kind) {
    mutexLock(&g_clk_mx);
    cpu_get(&hz);
    mutexUnlock(&g_clk_mx);
  }
  return hz / 1000000u;
}

/* ---------------------------------------------------------------- the GL thread */
__thread int g_a8r_on_gl_thread;
static Handle g_gl_h = INVALID_HANDLE;

/* Guest threads share priority 59, where the kernel time-slices them every
 * 10 ms (dcr_sched.c): any busy one on the GL thread's core -- the sound
 * engine's, a loader, the network's -- could take a 10 ms slice out of a
 * 16.7 ms frame. One step above them the GL thread keeps its core while it
 * has work; the others move to the other two (the kernel migrates them), and
 * a lock it waits for lends its priority to the holder (ArbitrateLock). It is
 * created so (a8r_boot.c) -- raising itself, it could wait behind a busy
 * thread for its first time slice -- with core 1 for home: core 0 has the
 * vsync and watchdog threads, core 2 audio. This marks it for the report. */
void a8r_perf_gl_thread(void) {
  g_a8r_on_gl_thread = 1;
  g_gl_h = threadGetCurHandle();
  s32 prio = -1, ideal = -1;
  u64 mask = 0;
  svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
  dcr_thread_get_cores(CUR_THREAD_HANDLE, &ideal, &mask);
  debugPrintf("[sched] GL thread: priority %d, cores 0x%llx (home %d)\n", (int)prio, (unsigned long long)mask,
              (int)ideal);
}

/* ---------------------------------------------------------------- clocks */
#define CFG_HANDHELD_GPU_460 0x92220007u

static int g_apm;

void a8r_perf_clocks(void) {
  static int done;
  if (done++)
    return;
  if (R_FAILED(apmInitialize())) {
    debugPrintf("[perf] apm unavailable: the clocks stay the system's\n");
  } else {
    g_apm = 1;
    if (dcr_config()->gpu_boost) {
      Result rc = apmSetPerformanceConfiguration(ApmPerformanceMode_Normal, CFG_HANDHELD_GPU_460);
      u32 got = 0;
      apmGetPerformanceConfiguration(ApmPerformanceMode_Normal, &got);
      debugPrintf("[perf] handheld GPU clock 460.8 MHz (configuration 0x%08x): %s (handheld now 0x%08x)\n",
                  CFG_HANDHELD_GPU_460, R_SUCCEEDED(rc) ? "granted" : "refused", (unsigned)got);
    }
  }
  cpu_start(); /* after the configuration, which sets the CPU's clock too */
}

static const char *clocks_now(void) {
  static char s[64];
  ApmPerformanceMode mode = ApmPerformanceMode_Invalid;
  u32 cfg = 0;
  if (!g_apm || R_FAILED(apmGetPerformanceMode(&mode)) || R_FAILED(apmGetPerformanceConfiguration(mode, &cfg)))
    return "clocks ?";
  const char *gpu = cfg == 0x00010000u ? "GPU 384" : cfg == 0x00010001u ? "GPU 768" : cfg == CFG_HANDHELD_GPU_460 ? "GPU 460.8"
                    : cfg == 0x92220009u || cfg == 0x9222000Au ? "CPU boost, GPU 76.8" : "";
  snprintf(s, sizeof s, "%s, configuration 0x%08x%s%s", mode == ApmPerformanceMode_Boost ? "docked" : "handheld",
           (unsigned)cfg, *gpu ? " = " : "", gpu);
  return s;
}

/* ---------------------------------------------------------------- frame times */
/* Per frame, each thread's own CPU time (the kernel's count of its ticks on a
 * core: no waits, no time slices lost to others): the GL thread's, and with
 * glthread the worker's, which runs the driver beside it. And where each one
 * waited for a free display buffer. */
static Handle g_wk_h = INVALID_HANDLE;
static volatile int g_wk_up;
static u64 g_n, g_step, g_swap, g_over16, g_over33, g_worst, g_last_end, g_report_tick;
static u64 g_glc, g_glc_max, g_glc_over, g_glc_last;
static u64 g_wkc, g_wkc_max, g_wkc_last;
static u64 g_wait_gl, g_wait_wk;
static u64 g_wait_now_gl, g_wait_now_wk; /* blocked in dequeue in the current frame */

static u64 thread_ticks(Handle h) {
  u64 v = 0;
  if (h == INVALID_HANDLE)
    return 0;
  if (R_FAILED(svcGetInfo(&v, InfoType_ThreadTickCount, h, TickCountInfo_Total)))
    svcGetInfo(&v, InfoType_ThreadTickCountDeprecated, h, TickCountInfo_Total);
  return v;
}

Result __real_nwindowDequeueBuffer(NWindow *nw, s32 *slot, NvMultiFence *fence);
Result __wrap_nwindowDequeueBuffer(NWindow *nw, s32 *slot, NvMultiFence *fence) {
  u64 t0 = armGetSystemTick();
  Result rc = __real_nwindowDequeueBuffer(nw, slot, fence);
  __atomic_fetch_add(g_a8r_on_gl_thread ? &g_wait_now_gl : &g_wait_now_wk, armGetSystemTick() - t0,
                     __ATOMIC_RELAXED);
  return rc;
}

/* a8r_boot.c's frame loop: the ticks of step() and of the swap */
void a8r_perf_frame(u64 step_ticks, u64 swap_ticks) {
  u64 now = armGetSystemTick();
  g_n++;
  g_step += step_ticks;
  g_swap += swap_ticks;
  g_wait_gl += __atomic_exchange_n(&g_wait_now_gl, 0, __ATOMIC_RELAXED);
  g_wait_wk += __atomic_exchange_n(&g_wait_now_wk, 0, __ATOMIC_RELAXED);
  u64 c = thread_ticks(g_gl_h);
  if (g_glc_last && c >= g_glc_last) {
    u64 d = c - g_glc_last;
    g_glc += d;
    if (d > g_glc_max)
      g_glc_max = d;
    g_glc_over += d > armNsToTicks(16700000);
  }
  g_glc_last = c;
  if (g_wk_up) {
    c = thread_ticks(g_wk_h);
    if (g_wkc_last && c >= g_wkc_last) {
      u64 d = c - g_wkc_last;
      g_wkc += d;
      if (d > g_wkc_max)
        g_wkc_max = d;
    }
    g_wkc_last = c;
  }
  if (g_last_end) {
    u64 ft = now - g_last_end;
    g_over16 += ft > armNsToTicks(17500000);
    g_over33 += ft > armNsToTicks(34200000);
    if (ft > g_worst)
      g_worst = ft;
  }
  g_last_end = now;
}

/* ---------------------------------------------------------------- glthread */
/* Called by Mesa on its glthread worker as it starts (egl_switch.c, mesa32's
 * 972de9c1). libnx made it at priority 59 among
 * the guest threads: one step above them, like the GL thread, and off the GL
 * thread's core 1 (cores 0 and 2), so the two run side by side. */
void dcr_prof_gl_worker(int tid); /* a8r_prof.c */
void switch_egl_glthread_hook(void);
void switch_egl_glthread_hook(void) {
  g_wk_h = threadGetCurHandle();
  BThread *t = b_thread_self(); /* in the thread list: sampled by the profiler */
  Result rp = svcSetThreadPriority(CUR_THREAD_HANDLE, DCR_GUEST_PRIO - 1);
  Result rc = dcr_thread_set_cores(CUR_THREAD_HANDLE, 0, 0x5);
  debugPrintf("[sched] glthread worker: priority %d%s, cores 0x5 (home 0)%s\n", DCR_GUEST_PRIO - 1,
              R_FAILED(rp) ? " (refused)" : "", R_FAILED(rc) ? " (refused)" : "");
  if (t)
    dcr_prof_gl_worker(t->tid);
  g_wk_up = 1;
}

int a8r_glthread_up(void) { return g_wk_up; }

/* Where the GL thread had to wait for the worker to catch up: a call that
 * needs the driver's answer (glGetError, a query) or data glthread does not
 * copy (client-memory vertices, a texture upload). Counted per call name. */
#define NSYNC 12
static struct {
  const char *name;
  u32 n;
  u64 ticks;
} g_sync[NSYNC];
static u64 g_sync_n, g_sync_ticks;

void __real__mesa_glthread_finish_before(void *ctx, const char *func);
void __wrap__mesa_glthread_finish_before(void *ctx, const char *func);
void __wrap__mesa_glthread_finish_before(void *ctx, const char *func) {
  u64 t0 = armGetSystemTick();
  __real__mesa_glthread_finish_before(ctx, func);
  u64 d = armGetSystemTick() - t0;
  g_sync_n++;
  g_sync_ticks += d;
  for (int i = 0; i < NSYNC; i++)
    if (g_sync[i].name == func || !g_sync[i].name) {
      g_sync[i].name = func;
      g_sync[i].n++;
      g_sync[i].ticks += d;
      break;
    }
}

/* ---------------------------------------------------------------- GPU time */
#define NQ 4
static PFNGLGENQUERIESEXTPROC q_gen;
static PFNGLBEGINQUERYEXTPROC q_begin;
static PFNGLENDQUERYEXTPROC q_end;
static PFNGLGETQUERYOBJECTUIVEXTPROC q_getu;
static PFNGLGETQUERYOBJECTUI64VEXTPROC q_get64;
static GLuint g_q[NQ];
static int g_qstate = 0; /* 0 untried, 1 on, -1 unavailable */
static int g_qactive = -1;
static unsigned g_qframe;
static int g_qpending[NQ];
static u64 g_gpu_ns, g_gpu_n, g_gpu_max;

static int queries_up(void) {
  if (g_qstate)
    return g_qstate > 0;
  g_qstate = -1;
  const char *ext = (const char *)glGetString(GL_EXTENSIONS);
  if (!ext || !strstr(ext, "GL_EXT_disjoint_timer_query")) {
    debugPrintf("[perf] no GL_EXT_disjoint_timer_query: GPU time not measured\n");
    return 0;
  }
  q_gen = (PFNGLGENQUERIESEXTPROC)eglGetProcAddress("glGenQueriesEXT");
  q_begin = (PFNGLBEGINQUERYEXTPROC)eglGetProcAddress("glBeginQueryEXT");
  q_end = (PFNGLENDQUERYEXTPROC)eglGetProcAddress("glEndQueryEXT");
  q_getu = (PFNGLGETQUERYOBJECTUIVEXTPROC)eglGetProcAddress("glGetQueryObjectuivEXT");
  q_get64 = (PFNGLGETQUERYOBJECTUI64VEXTPROC)eglGetProcAddress("glGetQueryObjectui64vEXT");
  if (!q_gen || !q_begin || !q_end || !q_getu || !q_get64)
    return 0;
  q_gen(NQ, g_q);
  g_qstate = 1;
  return 1;
}

/* gl_mesa.c, on the GL thread with the game's context current: the frame is
 * about to be presented (end its query) / was presented (start the next). */
void a8r_perf_gpu_end(void) {
  if (!queries_up() || g_qactive < 0)
    return;
  q_end(GL_TIME_ELAPSED_EXT);
  g_qpending[g_qactive] = 1;
  g_qactive = -1;
  /* the oldest one, if its result is in (never waited for) */
  int old = (int)((g_qframe + 1) % NQ);
  if (g_qpending[old]) {
    GLuint ready = 0;
    q_getu(g_q[old], GL_QUERY_RESULT_AVAILABLE_EXT, &ready);
    if (ready) {
      GLuint64 ns = 0;
      q_get64(g_q[old], GL_QUERY_RESULT_EXT, &ns);
      GLint disjoint = 0;
      glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
      if (!disjoint && ns < 1000000000ull) {
        g_gpu_ns += ns;
        g_gpu_n++;
        if (ns > g_gpu_max)
          g_gpu_max = ns;
      }
      g_qpending[old] = 0;
    }
  }
}

void a8r_perf_gpu_begin(void) {
  if (!queries_up())
    return;
  unsigned i = ++g_qframe % NQ;
  if (g_qpending[i]) /* its result never came: drop it */
    g_qpending[i] = 0;
  q_begin(GL_TIME_ELAPSED_EXT, g_q[i]);
  g_qactive = (int)i;
}

/* ---------------------------------------------------------------- the report */
void dcr_io_gl_stats(uint64_t *opens, uint64_t *reads, uint64_t *ticks); /* bionic_io.c */

void a8r_perf_report(void) {
  if (!g_n)
    return;
  double ms = 1000.0 / 19200000.0; /* ticks: 19.2 MHz */
  u64 now = armGetSystemTick();
  /* refreshes the screen showed an old frame again (60 Hz) */
  u64 missed = 0;
  if (g_report_tick) {
    u64 refreshes = (armTicksToNs(now - g_report_tick) * 60 + 500000000ull) / 1000000000ull;
    missed = refreshes > g_n ? refreshes - g_n : 0;
  }
  g_report_tick = now;
  char gpu[96] = "gpu ? (no timer queries)";
  if (g_gpu_n)
    snprintf(gpu, sizeof gpu, "gpu %.1f ms (max %.1f)", (double)g_gpu_ns / g_gpu_n / 1e6, (double)g_gpu_max / 1e6);
  char cpu_clk[64] = "";
  if (g_clk_kind)
    snprintf(cpu_clk, sizeof cpu_clk, ", CPU %u MHz%s", (unsigned)cpu_mhz_now(), g_clk_resets ? " (put back " : "");
  if (g_clk_kind && g_clk_resets)
    snprintf(cpu_clk + strlen(cpu_clk), sizeof cpu_clk - strlen(cpu_clk), "%u times)", (unsigned)g_clk_resets);
  char wk[96] = "";
  if (g_wk_up)
    snprintf(wk, sizeof wk, "glthread worker CPU %.1f ms (max %.1f), ", g_wkc * ms / g_n, g_wkc_max * ms);
  debugPrintf("[perf] %llu frames, %llu refreshes missed: GL thread CPU %.1f ms a frame (max %.1f, over 16.7 ms "
              "%llu%%; step %.1f, swap %.1f), %swait for a buffer %.1f ms%s, %s; frame intervals over 16.7 ms "
              "%llu%%, over 33 ms %llu%%, worst %.0f ms; %s%s\n",
              (unsigned long long)g_n, (unsigned long long)missed, g_glc * ms / g_n, g_glc_max * ms,
              (unsigned long long)(g_glc_over * 100 / g_n), g_step * ms / g_n, g_swap * ms / g_n, wk,
              (g_wait_gl + g_wait_wk) * ms / g_n, g_wait_wk > g_wait_gl ? " (in the worker)" : "", gpu,
              (unsigned long long)(g_over16 * 100 / g_n), (unsigned long long)(g_over33 * 100 / g_n), g_worst * ms,
              clocks_now(), cpu_clk);
  if (g_wk_up && g_sync_n) {
    char line[400];
    int n = snprintf(line, sizeof line, "[perf]   glthread syncs: %llu (%.1f ms a frame):", (unsigned long long)g_sync_n,
                     g_sync_ticks * ms / g_n);
    for (int k = 0; k < 6 && n < (int)sizeof line - 48; k++) {
      int best = -1;
      for (int i = 0; i < NSYNC; i++)
        if (g_sync[i].n && (best < 0 || g_sync[i].n > g_sync[best].n))
          best = i;
      if (best < 0)
        break;
      n += snprintf(line + n, sizeof line - n, " %s %u (%.1f ms),", g_sync[best].name ? g_sync[best].name : "?",
                    (unsigned)g_sync[best].n, g_sync[best].ticks * ms);
      g_sync[best].n = 0;
    }
    debugPrintf("%s\n", line);
    memset(g_sync, 0, sizeof g_sync);
    g_sync_n = g_sync_ticks = 0;
  }
  /* what else the GL thread did in these frames: file work, uploads, compiles */
  static DcrPc pc0[PC_N];
  static uint64_t io0[3];
  static int have0;
  uint64_t io[3];
  dcr_io_gl_stats(&io[0], &io[1], &io[2]);
  char pc[400];
  dcr_pc_format(pc, sizeof pc, have0 ? pc0 : NULL);
  debugPrintf("[perf]   file work on the GL thread: %llu opens, %llu reads (%llu ms)%s%s\n",
              (unsigned long long)(io[0] - io0[0]), (unsigned long long)(io[1] - io0[1]),
              (unsigned long long)(armTicksToNs(io[2] - io0[2]) / 1000000ull),
              pc[0] ? "; on any thread" : "", pc);
  dcr_pc_snapshot(pc0);
  memcpy(io0, io, sizeof io0);
  have0 = 1;
  g_n = g_step = g_swap = g_over16 = g_over33 = g_worst = 0;
  g_glc = g_glc_max = g_glc_over = g_wkc = g_wkc_max = g_wait_gl = g_wait_wk = 0;
  g_gpu_ns = g_gpu_n = g_gpu_max = 0;
}
