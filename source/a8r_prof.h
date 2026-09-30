/* a8r_prof.h -- loading-time counters and the loading sampler (a8r_prof.c). */
#ifndef A8R_PROF_H
#define A8R_PROF_H

#include <stdint.h>
#include <switch.h>

enum { PC_INFLATE, PC_TEX, PC_CTEX, PC_COMPILE, PC_LINK, PC_DRAW, PC_MIPMAP, PC_BUFFER, PC_N };

typedef struct {
  uint64_t calls, ticks, bytes;
} DcrPc;

extern DcrPc g_dcr_pc[PC_N];

static inline uint64_t dcr_pc_begin(void) { return armGetSystemTick(); }

static inline void dcr_pc_end(int k, uint64_t t0, uint64_t bytes) {
  __atomic_fetch_add(&g_dcr_pc[k].calls, 1, __ATOMIC_RELAXED);
  __atomic_fetch_add(&g_dcr_pc[k].ticks, armGetSystemTick() - t0, __ATOMIC_RELAXED);
  if (bytes)
    __atomic_fetch_add(&g_dcr_pc[k].bytes, bytes, __ATOMIC_RELAXED);
}

void dcr_pc_snapshot(DcrPc out[PC_N]);
void dcr_pc_format(char *out, size_t cap, const DcrPc *from);
void dcr_pc_tex_format(uint32_t key, uint64_t bytes);
void dcr_pc_report_formats(void);

void dcr_prof_start(void);
void dcr_prof_frame_begin(void);
void dcr_prof_frame_end(uint64_t frame);

#endif
