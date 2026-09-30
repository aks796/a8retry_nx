/* a8r_prof.c -- where loading time goes.
 *
 * The first hardware run (2026-09-25) took 30 s to its first picture, ~30 s
 * more to the menu and ~25 s into a race, with the CPU boosted throughout
 * (dcr_boost.c) and file reads only ~6% of the long frames. So two measures,
 * both written to debug.log:
 *
 * COUNTERS (always on, a system-tick read on each side of a call): the time
 * and bytes of the things a load is made of -- inflating the OBB's entries
 * (bionic_zlib.c), texture uploads, shader compiles and links, draws (nouveau
 * compiles a program's GPU code on its first draw), mipmap generation, buffer
 * uploads (gl_mesa.c) -- added to each long frame's line and to the start-up
 * line (dcr_boost.c). Calls on any thread count: the loading contexts too.
 *
 * SAMPLER ([debug] profile_loading, from the Crossy Road port's dcr_prof.c):
 * during the start-up and in frames that pass 100 ms, every 10 ms a thread of
 * its own pauses each game thread for a moment (as the watchdog does, under
 * b_pause_lock), classifies it by the syscall it sits in (none: running;
 * SendSyncRequest: file/IPC; anything else: waiting) and, when it is not
 * waiting, reads its pc and the code addresses on the mapped top of its stack.
 * And every 30 s of play, a 3 s window of ordinary frames is sampled ("steady"):
 * the GL thread alone (cheap), and where it waits too -- the syscall and the
 * code on its stack -- which tells a wait for the GPU (a free display buffer,
 * a fence) from a lock or a sleep.
 * When the start-up, a window or a frame of 500 ms or more ends, the log
 * gets, for the busiest threads: run/io/wait shares; "self", the busiest code (64-byte
 * buckets); "incl", the call sites most often on the stack. Engine addresses
 * are named from its symbols; this program's (Mesa, zlib, newlib) are
 * a8retry_nx+offset, named offline against a8retry_nx.elf. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "a8r_prof.h"
#include "bionic_pthread.h"
#include "dcr_config.h"
#include "util.h"

const char *dcr_addr_name(uint32_t a, char *buf, size_t cap); /* exc_handler.c */
int dcr_is_code_addr(uint32_t a);                               /* exc_handler.c */
size_t dcr_readable(uint32_t p, size_t want);                   /* exc_handler.c */

/* ================================================================ counters */
DcrPc g_dcr_pc[PC_N];

static const char *const k_pc_name[PC_N] = {"inflate", "tex", "ctex", "compile", "link", "draw", "mipmap", "buffers"};

void dcr_pc_snapshot(DcrPc out[PC_N]) {
  for (int k = 0; k < PC_N; k++) {
    out[k].calls = __atomic_load_n(&g_dcr_pc[k].calls, __ATOMIC_RELAXED);
    out[k].ticks = __atomic_load_n(&g_dcr_pc[k].ticks, __ATOMIC_RELAXED);
    out[k].bytes = __atomic_load_n(&g_dcr_pc[k].bytes, __ATOMIC_RELAXED);
  }
}

/* "; inflate 812 ms (4100, 96 MB), compile 3120 ms (412), ..." since `from`
 * (NULL: since the start), categories of 5 ms or more. */
void dcr_pc_format(char *out, size_t cap, const DcrPc *from) {
  static const char k_bytes[PC_N] = {1, 1, 1, 0, 0, 0, 0, 1};
  DcrPc now[PC_N];
  dcr_pc_snapshot(now);
  int n = 0;
  out[0] = 0;
  for (int k = 0; k < PC_N && n < (int)cap - 48; k++) {
    unsigned long long calls = now[k].calls - (from ? from[k].calls : 0);
    unsigned long long ms = armTicksToNs(now[k].ticks - (from ? from[k].ticks : 0)) / 1000000ull;
    unsigned long long mb = (now[k].bytes - (from ? from[k].bytes : 0)) >> 20;
    if (ms < 5)
      continue;
    if (k_bytes[k])
      n += snprintf(out + n, cap - n, "%s %s %llu ms (%llu, %llu MB)", n ? "," : ";", k_pc_name[k], ms, calls, mb);
    else
      n += snprintf(out + n, cap - n, "%s %s %llu ms (%llu)", n ? "," : ";", k_pc_name[k], ms, calls);
  }
}

/* texture upload formats (glTexImage2D format<<16|type; compressed: the
 * internal format), for the report */
#define NFMT 24
static struct {
  uint32_t key, count;
  uint64_t bytes;
} g_fmt[NFMT];
static Mutex g_fmt_mx;

void dcr_pc_tex_format(uint32_t key, uint64_t bytes) {
  mutexLock(&g_fmt_mx);
  for (int i = 0; i < NFMT; i++)
    if (!g_fmt[i].count || g_fmt[i].key == key) {
      g_fmt[i].key = key;
      g_fmt[i].count++;
      g_fmt[i].bytes += bytes;
      break;
    }
  mutexUnlock(&g_fmt_mx);
}

void dcr_pc_report_formats(void) {
  char line[700];
  int n = snprintf(line, sizeof line, "[prof] texture uploads by format:");
  mutexLock(&g_fmt_mx);
  for (int i = 0; i < NFMT && g_fmt[i].count && n < (int)sizeof line - 48; i++)
    n += snprintf(line + n, sizeof line - n, " %s%x/%x %u (%llu MB)", g_fmt[i].key >> 31 ? "c:" : "",
                  (g_fmt[i].key >> 16) & 0x7fff, g_fmt[i].key & 0xffff, (unsigned)g_fmt[i].count,
                  (unsigned long long)(g_fmt[i].bytes >> 20));
  mutexUnlock(&g_fmt_mx);
  debugPrintf("%s\n", line);
}

/* ================================================================= sampler */
#define START_MS 100
#define REPORT_MS 500
#define PERIOD_NS 10000000ll
#define MAX_SAMPLES 65536
#define MAX_THREADS 64
#define DEPTH 24
#define SHOW_THREADS 3

typedef struct {
  uint64_t r[29];
  uint64_t fp, lr, sp, pc;
  uint32_t psr, _pad;
  uint8_t v[32][16];
  uint32_t fpcr, fpsr;
  uint64_t tpidr;
} KCtx;

static Result get_ctx(KCtx *ctx, Handle h) {
  register uint32_t r0 __asm__("r0") = (uint32_t)(uintptr_t)ctx;
  register uint32_t r1 __asm__("r1") = h;
  __asm__ volatile("svc 0x33" : "+r"(r0), "+r"(r1) : : "r2", "r3", "r12", "lr", "memory");
  return r0;
}

enum { ST_RUN, ST_IO, ST_WAIT };

typedef struct {
  uint32_t pc;
  uint8_t thread, state, n, _pad;
  uint32_t ret[DEPTH];
} Sample;

typedef struct {
  BThread *t;
  int tid, main;
  char name[16];
  uint32_t n[3];
} Seen;

static Sample *g_s;
static volatile uint32_t g_n;
static Seen g_seen[MAX_THREADS];
static int g_nseen;
static volatile uint64_t g_frame_start; /* system tick; 0 between frames */
static volatile int g_busy;
static Handle g_main_handle;
static Thread g_thread;
static int g_startup = 1; /* the frame being sampled is the start-up */
static volatile int g_gl_tid = -1; /* the GL thread: the steady windows' one */

static volatile int g_wk_tid = -1; /* Mesa's glthread worker, if any: sampled too */
void dcr_prof_gl_thread(int tid) { g_gl_tid = tid; }
void dcr_prof_gl_worker(int tid) { g_wk_tid = tid; }
#define WINDOW_NS 3000000000ull
#define WINDOW_EVERY_NS 30000000000ull
static volatile u64 g_win_end; /* a steady window: sampling until this tick */
static u64 g_next_win;         /* when the next one starts (0: after the start-up) */
static void report(const char *what, uint64_t ms);

/* The syscall a paused thread sits in: a thread blocked in the kernel is
 * reported with its pc ON the svc instruction; the one before is checked too,
 * for a thread just back from one. */
static uint32_t svc_at(uint32_t a, int thumb) {
  if (!dcr_is_code_addr(a & ~1u))
    return ~0u;
  if (thumb) { /* svc #imm8 = 0xDFxx */
    uint16_t op = *(const volatile uint16_t *)(uintptr_t)(a & ~1u);
    return (op & 0xFF00) == 0xDF00 ? (uint32_t)(op & 0xFF) : ~0u;
  }
  if (a & 3)
    return ~0u;
  uint32_t op = *(const volatile uint32_t *)(uintptr_t)a; /* svc #imm24 */
  return (op & 0x0F000000) == 0x0F000000 ? (op & 0xFFFFFF) : ~0u;
}

static int state_of(const KCtx *c, uint32_t *svc_out) {
  uint32_t pc = (uint32_t)c->pc;
  int thumb = (c->psr & 0x20) != 0;
  uint32_t svc = svc_at(pc, thumb);
  if (svc == ~0u)
    svc = svc_at(pc - (thumb ? 2 : 4), thumb);
  *svc_out = svc;
  if (svc == ~0u)
    return ST_RUN;
  if (svc == 0x21 || svc == 0x22) /* SendSyncRequest(WithUserBuffer): fs and other IPC */
    return ST_IO;
  return ST_WAIT;
}

static int seen_index(BThread *t) {
  for (int i = 0; i < g_nseen; i++)
    if (g_seen[i].t == t && g_seen[i].tid == t->tid)
      return i;
  if (g_nseen == MAX_THREADS)
    return -1;
  Seen *e = &g_seen[g_nseen];
  memset(e, 0, sizeof *e);
  e->t = t;
  e->tid = t->tid;
  e->main = t->handle == g_main_handle;
  memcpy(e->name, t->name, sizeof e->name);
  e->name[sizeof e->name - 1] = 0;
  return g_nseen++;
}

static void sample_one(BThread *t, void *arg) {
  if (t->handle == INVALID_HANDLE || t->finished || g_n >= MAX_SAMPLES || t->handle == g_thread.handle)
    return;
  if (g_win_end && t->tid != g_gl_tid && t->tid != g_wk_tid)
    return;
  int ti = seen_index(t);
  if (ti < 0)
    return;
  /* Paused only for its context and a copy of its stack top: the code
   * addresses are picked out of the copy after it runs again (the GL thread
   * is sampled 100 times a second in a steady window). */
  static uint32_t stack[0x4000 / 4];
  size_t words = 0;
  b_pause_lock();
  int paused = R_SUCCEEDED(svcSetThreadActivity(t->handle, ThreadActivity_Paused));
  KCtx ctx;
  int got = paused && R_SUCCEEDED(get_ctx(&ctx, t->handle));
  uint32_t svc = 0;
  int st = ST_RUN;
  if (got) {
    st = state_of(&ctx, &svc);
    if (st != ST_WAIT || g_win_end) {
      uint32_t sp = (uint32_t)ctx.r[13] & ~3u;
      uintptr_t lo = (uintptr_t)t->stack_base, hi = lo + t->stack_size;
      if (lo && sp >= lo && sp < hi) {
        size_t n = dcr_readable(sp, 0x4000); /* mapped only */
        if (n > hi - sp)
          n = hi - sp;
        words = n / 4;
        memcpy(stack, (const void *)(uintptr_t)sp, words * 4);
      }
    }
  }
  if (paused)
    svcSetThreadActivity(t->handle, ThreadActivity_Runnable);
  b_pause_unlock();
  if (!got)
    return;
  g_seen[ti].n[st]++;
  if (st == ST_WAIT && !g_win_end)
    return;
  Sample *s = &g_s[g_n];
  s->pc = (uint32_t)ctx.pc;
  s->thread = (uint8_t)ti;
  s->state = (uint8_t)st;
  s->_pad = (uint8_t)svc;
  s->n = 0;
  if ((uint32_t)ctx.r[14] && dcr_is_code_addr((uint32_t)ctx.r[14] & ~1u))
    s->ret[s->n++] = (uint32_t)ctx.r[14];
  for (size_t k = 0; k < words && s->n < DEPTH; k++)
    if (dcr_is_code_addr(stack[k] & ~1u))
      s->ret[s->n++] = stack[k];
  g_n++;
}

static void sampler(void *arg) {
  for (;;) {
    __atomic_store_n(&g_busy, 1, __ATOMIC_SEQ_CST);
    u64 now = armGetSystemTick();
    uint64_t st = __atomic_load_n(&g_frame_start, __ATOMIC_SEQ_CST);
    int long_frame = st && armTicksToNs(now - st) >= START_MS * 1000000ull;
    if (!g_win_end && !g_startup && g_next_win && now >= g_next_win && !long_frame) {
      g_n = 0; /* a steady window starts: every frame from here */
      g_nseen = 0;
      g_win_end = now + armNsToTicks(WINDOW_NS);
    }
    int window = g_win_end != 0;
    if (window || long_frame)
      b_thread_foreach(sample_one, NULL);
    if (window && now >= g_win_end) {
      report("steady 3 s window", WINDOW_NS / 1000000ull);
      g_win_end = 0;
      g_next_win = now + armNsToTicks(WINDOW_EVERY_NS);
      g_n = 0;
      g_nseen = 0;
    }
    __atomic_store_n(&g_busy, 0, __ATOMIC_SEQ_CST);
    svcSleepThread(window || long_frame ? PERIOD_NS : 5000000ll);
  }
}

/* main(), before the engine's constructors: the start-up is sampled as one
 * long frame, up to the first picture. */
void dcr_prof_start(void) {
  if (!dcr_config()->profile)
    return;
  g_main_handle = envGetMainThreadHandle();
  g_s = malloc(sizeof(Sample) * MAX_SAMPLES);
  if (!g_s)
    return;
  __atomic_store_n(&g_frame_start, armGetSystemTick(), __ATOMIC_SEQ_CST);
  if (R_SUCCEEDED(threadCreate(&g_thread, sampler, NULL, NULL, 0x4000, 0x2C, -2)))
    threadStart(&g_thread);
  debugPrintf("[prof] loading is sampled: every thread every %lld ms, in the start-up and in frames "
              "over %d ms; frames of %d ms or more are reported ([debug] profile_loading)\n",
              PERIOD_NS / 1000000, START_MS, REPORT_MS);
}

void dcr_prof_frame_begin(void) {
  if (!g_s || g_win_end)
    return;
  g_n = 0;
  g_nseen = 0;
  __atomic_store_n(&g_frame_start, armGetSystemTick(), __ATOMIC_SEQ_CST);
}

/* ---- aggregation: small open-addressing tables ---- */
#define TAB 2048
typedef struct {
  uint32_t key, count;
} Ent;

static void add(Ent *t, uint32_t key) {
  for (uint32_t h = (key * 2654435761u) % TAB, k = 0; k < TAB; k++, h = (h + 1) % TAB) {
    if (t[h].count && t[h].key != key)
      continue;
    t[h].key = key;
    t[h].count++;
    return;
  }
}

static void top_line(const char *label, Ent *t, uint32_t total, int want) {
  char line[1400];
  int n = snprintf(line, sizeof line, "[prof]     %s:", label);
  int any = 0;
  for (int k = 0; k < want; k++) {
    int best = -1;
    for (int i = 0; i < TAB; i++)
      if (t[i].count && (best < 0 || t[i].count > t[best].count))
        best = i;
    if (best < 0 || t[best].count * 100 < total || n > (int)sizeof line - 140)
      break;
    char a[160];
    n += snprintf(line + n, sizeof line - n, " %s %u%%;", dcr_addr_name(t[best].key, a, sizeof a),
                  t[best].count * 100 / total);
    t[best].count = 0;
    any = 1;
  }
  if (any)
    debugPrintf("%s\n", line);
}

/* Every bucket, compactly (E: libasphalt8.so+, A: a8retry_nx+), for naming and
 * summing by function and library offline: in a steady window the GL
 * thread's frame is spread so thin that no bucket reaches 1%. */
static void raw_line(const char *label, const Ent *t) {
  char line[1200];
  int n = snprintf(line, sizeof line, "[prof]     %s:", label), any = 0;
  for (int i = 0; i < TAB; i++) {
    if (!t[i].count)
      continue;
    char a[160], c[24];
    const char *nm = dcr_addr_name(t[i].key, a, sizeof a);
    if (!strncmp(nm, "libasphalt8.so+0x", 17))
      snprintf(c, sizeof c, "E%.*s", (int)strcspn(nm + 17, " "), nm + 17);
    else if (!strncmp(nm, "a8retry_nx+0x", 13))
      snprintf(c, sizeof c, "A%.*s", (int)strcspn(nm + 13, " "), nm + 13);
    else
      snprintf(c, sizeof c, "X%x", (unsigned)t[i].key);
    n += snprintf(line + n, sizeof line - n, " %s*%u", c, (unsigned)t[i].count);
    any = 1;
    if (n > (int)sizeof line - 40) {
      debugPrintf("%s\n", line);
      n = snprintf(line, sizeof line, "[prof]     %s:", label);
      any = 0;
    }
  }
  if (any)
    debugPrintf("%s\n", line);
}

static const char *svc_name(unsigned svc) {
  switch (svc) {
  case 0x0B: return "SleepThread";
  case 0x18: return "WaitSynchronization";
  case 0x1A: return "ArbitrateLock";
  case 0x1C: return "WaitProcessWideKeyAtomic";
  case 0x21: case 0x22: return "SendSyncRequest";
  case 0x34: return "WaitForAddress";
  default: return "?";
  }
}

static void report_thread(int ti, uint32_t ticks) {
  static Ent self[TAB], incl[TAB], winc[TAB];
  uint32_t svcs[256] = {0}, waits = 0;
  memset(self, 0, sizeof self);
  memset(incl, 0, sizeof incl);
  memset(winc, 0, sizeof winc);
  const Seen *e = &g_seen[ti];
  uint32_t n = g_n;
  for (uint32_t i = 0; i < n; i++) {
    const Sample *s = &g_s[i];
    if (s->thread != ti)
      continue;
    int wait = s->state == ST_WAIT;
    if (wait) {
      svcs[s->_pad]++;
      waits++;
    } else {
      add(self, s->pc & ~63u);
    }
    for (int k = 0; k < s->n; k++) {
      uint32_t a = s->ret[k] & ~1u;
      int dup = 0;
      for (int j = 0; j < k && !dup; j++)
        dup = (s->ret[j] & ~1u) == a;
      if (!dup)
        add(wait ? winc : incl, a);
    }
  }
  debugPrintf("[prof]   %s \"%s\" (tid %d): running %u%%, file/IPC %u%%, waiting %u%%\n",
              e->main ? "main thread" : "thread", e->name[0] ? e->name : "?", e->tid,
              e->n[ST_RUN] * 100 / ticks, e->n[ST_IO] * 100 / ticks, e->n[ST_WAIT] * 100 / ticks);
  if (g_win_end)
    raw_line("self, all", self);
  top_line("self", self, ticks, 16);
  top_line("incl", incl, ticks, 24);
  if (waits) {
    char line[400];
    int len = snprintf(line, sizeof line, "[prof]     waiting in:");
    for (int k = 0; k < 4; k++) {
      int best = -1;
      for (int v = 0; v < 256; v++)
        if (svcs[v] && (best < 0 || svcs[v] > svcs[best]))
          best = v;
      if (best < 0)
        break;
      len += snprintf(line + len, sizeof line - len, " svc 0x%x %s %u%%;", best, svc_name(best), svcs[best] * 100 / ticks);
      svcs[best] = 0;
    }
    debugPrintf("%s\n", line);
    top_line("waiting, incl", winc, ticks, 24);
  }
}

void dcr_prof_frame_end(uint64_t frame) {
  if (!g_s || g_win_end)
    return;
  uint64_t st = g_frame_start;
  __atomic_store_n(&g_frame_start, 0, __ATOMIC_SEQ_CST);
  while (__atomic_load_n(&g_busy, __ATOMIC_SEQ_CST))
    svcSleepThread(100000ll);
  int startup = g_startup;
  g_startup = 0;
  if (startup)
    g_next_win = armGetSystemTick() + armNsToTicks(20000000000ull); /* the first window, in the menus */
  uint64_t ms = st ? armTicksToNs(armGetSystemTick() - st) / 1000000ull : 0;
  if (ms < REPORT_MS || !g_nseen)
    return;
  char what[64];
  if (startup)
    snprintf(what, sizeof what, "start-up (to the first picture)");
  else
    snprintf(what, sizeof what, "frame %llu", (unsigned long long)frame);
  report(what, ms);
  if (startup)
    dcr_pc_report_formats();
}

static void report(const char *what, uint64_t ms) {
  uint32_t ticks = 0; /* the most samples any one thread got */
  for (int i = 0; i < g_nseen; i++) {
    uint32_t t = g_seen[i].n[0] + g_seen[i].n[1] + g_seen[i].n[2];
    if (t > ticks)
      ticks = t;
  }
  if (ticks < 5)
    return;
  debugPrintf("[prof] %s: %llu ms, %lu ticks, %d threads%s\n", what, (unsigned long long)ms,
              (unsigned long)ticks, g_nseen, g_n >= MAX_SAMPLES ? " (sample buffer full)" : "");
  int shown[MAX_THREADS] = {0};
  for (int k = 0; k < SHOW_THREADS; k++) {
    int best = -1;
    for (int i = 0; i < g_nseen; i++) {
      uint32_t busy = g_seen[i].n[ST_RUN] + g_seen[i].n[ST_IO];
      if (!shown[i] && busy * 20 >= ticks &&
          (best < 0 || busy > g_seen[best].n[ST_RUN] + g_seen[best].n[ST_IO]))
        best = i;
    }
    if (best < 0)
      break;
    shown[best] = 1;
    report_thread(best, ticks);
  }
}
