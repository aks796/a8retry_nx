/* a8r_loader.c -- loads libasphalt8.so, the engine.
 *
 * One module: the mod's own build of Gameloft's Asphalt 8 engine, put together
 * by a8r_setup.c from the pieces the mod hides in its APK. It is ARM (A32)
 * code almost throughout (54,182 functions; 69 are Thumb), links nothing but
 * Android's system libraries (libGLESv2, libEGL, libOpenSLES, libz, libandroid,
 * liblog, libdl, libstdc++, libm, libc), and exports ~66,000 named symbols.
 *
 *   so_load / so_relocate / so_resolve   the shared loader (so_util.c): the
 *                                        import table (imports.c), gl* through
 *                                        Mesa (gl_mesa.c)
 *   kernel user helpers                  libgcc's __sync_* and boost's
 *                                        call_once call the Linux kuser page
 *                                        (0xffff0fc0 cmpxchg, 0xffff0fa0
 *                                        barrier; 44 + 9 literals). Horizon has
 *                                        nothing there: the literals are
 *                                        pointed at kuser.S (PvZ found this on
 *                                        hardware)
 *   so_finalize                          RX text, RW data
 *   constructors                         244 of them, when Android's
 *                                        System.load would run them
 *
 * The engine never writes its own code, so the code-space hooks the memory
 * shims ask (codespace.h: PvZ's mod patches its engine at run time) are
 * answered with "not ours" here. MIT.
 */
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "a8r.h"
#include "codespace.h"
#include "config.h"
#include "imports.h"
#include "util.h"

static void patch_steering(so_module *m); /* below */

so_module g_mod_game;

const char *dcr_game_root(void); /* main.c */

/* ----------------------------------------------------- code space: none */
volatile int g_cs_armed;
void *cs_mmap(size_t len, int prot, const void *caller) { return NULL; }
int cs_munmap(void *addr, size_t len) { return 0; }
int cs_mprotect(void *addr, size_t len, int prot, const void *caller) { return 0; }
int cs_write(void *dst, const void *src, size_t n, int c, int kind) { return 0; }

/* ----------------------------------------------------- kernel helpers */
void dcr_kuser_cmpxchg(void);
void dcr_kuser_memory_barrier(void);

static void fix_kuser_helpers(so_module *m) {
  int cmpxchg = 0, barrier = 0, other = 0;
  for (int i = 0; i < m->phnum; i++) {
    const Elf32_Phdr *ph = &m->phdr[i];
    if (ph->p_type != PT_LOAD || !(ph->p_flags & PF_X))
      continue;
    uint32_t *w = (uint32_t *)((uintptr_t)((uint8_t *)m->load_base + ph->p_vaddr + 3) & ~3u);
    size_t nw = ph->p_filesz / 4;
    for (size_t k = 0; k < nw; k++) {
      if ((w[k] & 0xfffff000u) != 0xffff0000u || (w[k] & 0xfff) < 0xf60)
        continue;
      if (w[k] == 0xffff0fc0u) {
        w[k] = (uint32_t)(uintptr_t)dcr_kuser_cmpxchg;
        cmpxchg++;
      } else if (w[k] == 0xffff0fa0u) {
        w[k] = (uint32_t)(uintptr_t)dcr_kuser_memory_barrier;
        barrier++;
      } else if (w[k] == 0xffff0f60u || w[k] == 0xffff0fe0u || w[k] == 0xffff0ffcu) {
        other++;
      }
    }
  }
  debugPrintf("[boot] %s: kernel user helpers -> kuser.S (%d cmpxchg, %d barrier%s)\n", m->base_name,
              cmpxchg, barrier, other ? "; others look like data, left alone" : "");
}

/* ------------------------------------------------------------- loading */
int dcr_emu_fix_vcvt(so_module *m, uint32_t *pool, size_t pool_words); /* emu_fixups.c */

#define EMU_POOL_BYTES 0x10000u

int a8r_load_engine(void) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s", dcr_game_root(), A8R_LIB_GAME);
  u64 t0 = armGetSystemTick();
  /* Under an emulator the image runs where it is staged: stage it with room
   * behind it for the instruction stubs (emu_fixups.c), within branch range. */
  void *base = dcr_is_emulator() ? memalign(0x1000, SO_REGION_BYTES) : NULL;
  int rc = so_load(&g_mod_game, path, base, base ? SO_REGION_BYTES - EMU_POOL_BYTES : SO_REGION_BYTES);
  if (rc < 0) {
    const char *why = rc == -1 ? "cannot open it, or it is not a 32-bit ARM ELF"
                    : rc == -2 ? "out of memory"
                    : rc == -3 ? "larger than SO_REGION_BYTES"
                    : rc == -4 ? "too many program headers" : "?";
    debugPrintf("[boot] so_load(%s) failed rc=%d: %s\n", path, rc, why);
    return -1;
  }
  so_relocate(&g_mod_game);
  int missing = so_resolve(&g_mod_game, dcr_imports, dcr_imports_count, 1);
  fix_kuser_helpers(&g_mod_game);
  patch_steering(&g_mod_game);
  if (base) {
    uint32_t *pool = (uint32_t *)((uint8_t *)base + g_mod_game.load_size);
    memset(pool, 0, EMU_POOL_BYTES);
    dcr_emu_fix_vcvt(&g_mod_game, pool, EMU_POOL_BYTES / 4);
    /* Gameloft's analytics (glotv3) start 4-5 boost::thread workers, whose
     * entry (boost's thread_proxy) is Thumb code Ryujinx's decoder fails on
     * ("Can't split at right block address"). Offline they have nothing to
     * send: under the emulator the tracker gets no workers. Hardware runs
     * them as the game does. */
    static const char *const conc[] = {"_ZN6glotv37Porting29GetMinimumRequiredConcurrencyEv",
                                       "_ZN6glotv37Porting29GetMaximumRequiredConcurrencyEv"};
    for (int i = 0; i < 2; i++) {
      uint32_t off = 0;
      for (int k = 0; k < g_mod_game.num_syms && !off; k++)
        if (g_mod_game.syms[k].st_shndx != SHN_UNDEF &&
            !strcmp(g_mod_game.dynstrtab + g_mod_game.syms[k].st_name, conc[i]))
          off = g_mod_game.syms[k].st_value;
      if (off && !(off & 1)) {
        *(uint32_t *)((uint8_t *)g_mod_game.load_base + off) = 0xE3A00000u; /* mov r0, #0 */
        debugPrintf("[emu] %s -> 0 (no analytics workers under the emulator)\n", conc[i] + 15);
      }
    }
  }
  so_finalize(&g_mod_game);
  so_flush_caches(&g_mod_game);
  debugPrintf("[boot] %s %u KB at %p (%d unresolved imports) in %llu ms\n", A8R_LIB_GAME,
              (unsigned)(g_mod_game.load_size >> 10), g_mod_game.load_virtbase, missing,
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
  return 0;
}

/* The engine's function `name`: its offset in the image, 0 if not there (or Thumb). */
static uint32_t func_off(so_module *m, const char *name) {
  for (int k = 0; k < m->num_syms; k++)
    if (m->syms[k].st_shndx != SHN_UNDEF && !strcmp(m->dynstrtab + m->syms[k].st_name, name))
      return (m->syms[k].st_value & 1) ? 0 : m->syms[k].st_value;
  return 0;
}

static int words_are(so_module *m, uint32_t off, const uint32_t (*want)[2], int n) {
  for (int i = 0; i < n; i++)
    if (*(const uint32_t *)((const uint8_t *)m->load_base + off + want[i][0]) != want[i][1])
      return 0;
  return 1;
}

/* Analog steering with a controller (a8r_input.c). The engine steers from the
 * left stick's value only for a racing wheel -- PlayerInputController::
 * UpdateSteering: if IsPowerAConnected() == 10 and the stick moved since the
 * last key, steering = 2 * x. For every other controller, the SHIELD kind (5)
 * this port reports included, SetPowerALeftJoystick turns the stick into
 * digital left/right, and only at exactly +-1.0, which a Switch stick seldom
 * reports (hardware 2026-09-25: only the D-pad steered). So the SHIELD kind
 * takes the wheel's path, at 1x (the input side scales the stick). The flag
 * "the stick moved" is the engine's: a8r_input.c sets it only while the stick
 * is out of its centre, so the D-pad steers whenever the stick is let go.
 * Every word is checked first: a mod option that changed this code (none
 * does) would leave it as it is. */
volatile uint8_t *g_a8r_stick_moved;
/* The engine's KeyboardControl (the controller's player input): its byte at
 * +4 is KeyboardControl::IsRacing() -- the race controls (a8r_input.c) apply
 * only while it is set. Found as SetPowerALeftJoystick loads it. */
void *volatile *g_a8r_kbctl;

static void patch_steering(so_module *m) {
  uint32_t u = func_off(m, "_ZN21PlayerInputController14UpdateSteeringEjRf");
  uint32_t s = func_off(m, "_Z21SetPowerALeftJoystickff");
  static const uint32_t upd[][2] = {
      {0x20, 0xE350000Au}, /* cmp r0, #10            IsPowerAConnected() == wheel */
      {0x24, 0x0A00009Eu}, /* beq .analog */
      {0x2c0, 0xEDD37A00u}, /* vldr s15, [r3]        the stick's x */
      {0x2c4, 0xEE777AA7u}, /* vadd.f32 s15, s15, s15  x 2 */
      {0x2c8, 0xEDC67A00u}, /* vstr s15, [r6]        steering */
  };
  static const uint32_t set[][2] = {
      {0x0c, 0xE59F4340u}, /* ldr r4, [pc, #0x340] */
      {0x14, 0xE08F4004u}, /* add r4, pc, r4        the GOT */
      {0x24, 0xE59FC338u}, /* ldr ip, [pc, #0x338]  GOT offset of the flag */
      {0x40, 0xE794100Cu}, /* ldr r1, [r4, ip] */
      {0x44, 0xE5C12000u}, /* strb r2, [r1]         flag = 1 */
      {0x30, 0xE59F3330u}, /* ldr r3, [pc, #0x330]  GOT offset of KeyboardControl* */
      {0x48, 0xE7943003u}, /* ldr r3, [r4, r3] */
      {0x4c, 0xE5933000u}, /* ldr r3, [r3]          the instance */
  };
  if (!u || !s || !words_are(m, u, upd, 5) || !words_are(m, s, set, 8)) {
    debugPrintf("[input] the engine's steering code is not the expected one: the stick steers only "
                "at full deflection\n");
    return;
  }
  uint8_t *b = (uint8_t *)m->load_base;
  uint32_t got = s + 0x14 + 8 + *(uint32_t *)(b + s + 0x0c + 8 + 0x340);
  uint32_t slot = got + *(uint32_t *)(b + s + 0x24 + 8 + 0x338);
  if (slot + 4 > m->load_size) {
    debugPrintf("[input] steering: the flag's GOT slot is outside the image -- not patched\n");
    return;
  }
  g_a8r_stick_moved = (volatile uint8_t *)(uintptr_t) * (uint32_t *)(b + slot); /* relocated */
  uint32_t kslot = got + *(uint32_t *)(b + s + 0x30 + 8 + 0x330);
  uint32_t r = func_off(m, "_ZN15KeyboardControl8IsRacingEv");
  if (kslot + 4 <= m->load_size && r && *(uint32_t *)(b + r) == 0xE5D00004u) /* ldrb r0, [r0, #4] */
    g_a8r_kbctl = (void *volatile *)(uintptr_t) * (uint32_t *)(b + kslot);
  *(uint32_t *)(b + u + 0x20) = 0xE3500005u; /* cmp r0, #5 */
  *(uint32_t *)(b + u + 0x2c4) = 0xE320F000u; /* nop: steering = x */
  debugPrintf("[input] analog steering with the left stick (engine patched; flag at %p)\n",
              (void *)g_a8r_stick_moved);
}

void a8r_run_constructors(void) {
  u64 t0 = armGetSystemTick();
  so_execute_init_array(&g_mod_game);
  /* The 28 MB file image: only its section headers were still in use (they
   * locate .init_array); the mapped copy is what runs. */
  so_free_temp(&g_mod_game);
  debugPrintf("[boot] %s constructors done (%llu ms)\n", A8R_LIB_GAME,
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
}

void *a8r_native(const char *symbol) {
  return (void *)so_try_find_addr_rx(&g_mod_game, symbol);
}
