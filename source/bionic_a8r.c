/* bionic_a8r.c -- the imports Asphalt 8 adds to what the other ports needed.
 *
 *   C++ runtime   Android's minimal libstdc++.so: the thread-safe static
 *                 initialisation guards (__cxa_guard_*: boost, rapidjson,
 *                 Bullet, the game loop -- some 900 call sites), pure virtual
 *                 calls, and the nothrow operator new / delete.
 *   stdio         __srget (the getc() macro's refill: Lua's luaL_loadfile),
 *                 freopen, tmpfile, tmpnam.
 *   the rest      pread, times, getgid / getegid, gethostbyaddr (offline).
 * MIT.
 */
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "bionic.h"
#include "bionic_io.h"
#include "dcr_path.h"
#include "dcr_time.h"
#include "error.h"
#include "util.h"

/* ============================================================ C++ runtime */
/* ARM C++ ABI (3.2.3): the guard is a 32-bit word whose bit 0 says "done";
 * the compiler tests that bit inline and calls acquire only while it is 0.
 * 0x100 marks an initialisation in progress (as bionic's does), and the
 * waiters for it sleep on one process-wide condition variable. */
#define GUARD_DONE 1u
#define GUARD_PENDING 0x100u

static Mutex g_guard_lock;
static CondVar g_guard_cv;

int b___cxa_guard_acquire(volatile uint32_t *g) {
  if (__atomic_load_n(g, __ATOMIC_ACQUIRE) & GUARD_DONE)
    return 0;
  mutexLock(&g_guard_lock);
  for (;;) {
    uint32_t v = *g;
    if (v & GUARD_DONE) {
      mutexUnlock(&g_guard_lock);
      return 0;
    }
    if (!(v & GUARD_PENDING)) {
      *g = v | GUARD_PENDING;
      mutexUnlock(&g_guard_lock);
      return 1; /* this thread initialises */
    }
    condvarWait(&g_guard_cv, &g_guard_lock);
  }
}

void b___cxa_guard_release(volatile uint32_t *g) {
  mutexLock(&g_guard_lock);
  __atomic_store_n(g, GUARD_DONE, __ATOMIC_RELEASE);
  condvarWakeAll(&g_guard_cv);
  mutexUnlock(&g_guard_lock);
}

void b___cxa_guard_abort(volatile uint32_t *g) {
  mutexLock(&g_guard_lock);
  __atomic_store_n(g, 0, __ATOMIC_RELEASE);
  condvarWakeAll(&g_guard_cv);
  mutexUnlock(&g_guard_lock);
}

void b___cxa_pure_virtual(void) {
  fatal_error("The game called a pure virtual function (from %p).", __builtin_return_address(0));
}

/* std::nothrow (an empty object: only its address is used) */
const uint8_t b__ZSt7nothrow[4];

void *b__ZnwjRKSt9nothrow_t(size_t n, const void *nt) { return malloc(n ? n : 1); }
void *b__ZnajRKSt9nothrow_t(size_t n, const void *nt) { return malloc(n ? n : 1); }
void b__ZdlPvRKSt9nothrow_t(void *p, const void *nt) { free(p); }

/* ================================================================== stdio */
extern unsigned char b___sF[];

static int is_std_stream(const void *fp) {
  return (const unsigned char *)fp >= b___sF && (const unsigned char *)fp < b___sF + 3 * B_FILE_SIZE;
}

/* The engine's inlined getc(): --fp->_r < 0 ? __srget(fp) : *fp->_p++. A
 * FILE* from fopen() is newlib's, whose leading fields (_p, _r) are laid out
 * as bionic's (both are BSD stdio), so the macro reads it correctly and only
 * the refill comes here. bionic's stdin reads as empty. */
int b___srget(void *fp) {
  if (is_std_stream(fp))
    return EOF;
  return __srget_r(_REENT, (FILE *)fp);
}

void *b_fopen(const char *path, const char *mode); /* bionic_stdio.c */
int b_fclose(void *fp);

void *b_freopen(const char *path, const char *mode, void *fp) {
  /* Redirecting stdout / stderr (to a log file): they already go to debug.log. */
  if (is_std_stream(fp)) {
    debugPrintf("[stdio] freopen(%s) of a standard stream: kept on debug.log\n", path ? path : "(null)");
    return fp;
  }
  if (fp)
    b_fclose(fp);
  return path ? b_fopen(path, mode) : NULL;
}

static int g_tmp_seq;

char *b_tmpnam(char *buf) {
  static char own[96];
  char *out = buf ? buf : own;
  snprintf(out, 96, DCR_ANDROID_CACHE "/tmp%08x", (unsigned)__atomic_add_fetch(&g_tmp_seq, 1, __ATOMIC_RELAXED));
  return out;
}

void *b_tmpfile(void) {
  char name[96];
  b_tmpnam(name);
  return b_fopen(name, "w+b");
}

/* ============================================================== the rest */
static Mutex g_pread_lock;

/* One lock around seek + read + seek back: pread is how two threads read one
 * descriptor at different places without disturbing each other. */
ssize_t b_pread(int fd, void *buf, size_t n, b_off_t off) {
  if (off < 0) {
    b_set_errno(L_EINVAL);
    return -1;
  }
  mutexLock(&g_pread_lock);
  size_t got = b_pread_all(fd, buf, n, (b_off64_t)off);
  mutexUnlock(&g_pread_lock);
  return (ssize_t)got;
}

struct b_tms {
  b_clock_t tms_utime, tms_stime, tms_cutime, tms_cstime;
};

/* In clock ticks of sysconf(_SC_CLK_TCK) = 100, as bionic's. */
b_clock_t b_times(struct b_tms *t) {
  b_clock_t ticks = (b_clock_t)(dcr_monotonic_ns() / 10000000ull);
  if (t) {
    t->tms_utime = ticks;
    t->tms_stime = 0;
    t->tms_cutime = t->tms_cstime = 0;
  }
  return ticks;
}

#define A8R_FAKE_GID 10123 /* an app's gid on Android (u0_a123) */
b_gid_t b_getgid(void) { return A8R_FAKE_GID; }
b_gid_t b_getegid(void) { return A8R_FAKE_GID; }

int *b___get_h_errno(void); /* bionic_core.c */

void *b_gethostbyaddr(const void *addr, int len, int type) {
  *b___get_h_errno() = 1; /* HOST_NOT_FOUND: offline */
  return NULL;
}
