/* a8r_gl.c -- the port's part of the GL layer (the runtime's gl_mesa.c calls
 * it): the loading counters' wrappers around uploads, shader builds and
 * mipmaps (a8r_prof.c), the game's controller card replaced as it is uploaded
 * (a8r_card.c), and each frame's GPU time and profile around the swap
 * (a8r_perf.c, a8r_prof.c). The GL error checks and .dump_textures are the
 * runtime's (RT_GL_CHECK), around these. MIT.
 */
#include <GLES2/gl2.h>
#include <stdint.h>
#include <string.h>

#include <miniz/miniz.h>

#include "a8r.h"
#include "a8r_prof.h"
#include "gl_layer.h"

/* bytes per pixel of an uncompressed upload, for the loading counters */
static unsigned px_bytes(GLenum fmt, GLenum type) {
  if (type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1 || type == GL_UNSIGNED_SHORT_5_6_5)
    return 2;
  unsigned c = fmt == GL_RGBA || fmt == 0x80E1 /* BGRA */ ? 4 : fmt == GL_RGB ? 3 : fmt == GL_LUMINANCE_ALPHA ? 2 : 1;
  return type == GL_FLOAT ? c * 4 : type == GL_UNSIGNED_SHORT || type == 0x8D61 /* HALF_FLOAT_OES */ ? c * 2 : c;
}

static void (*r_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
static void (*r_glCompressedTexImage2D)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *);
static void (*r_glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
static void (*r_glCompressedTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void *);
static void (*r_glCompileShader)(GLuint);
static void (*r_glLinkProgram)(GLuint);
static void (*r_glGenerateMipmap)(GLenum);

static void w_glTexImage2D(GLenum t, GLint lvl, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum fmt,
                           GLenum type, const void *px) {
  uint64_t t0 = dcr_pc_begin(), bytes = px ? (uint64_t)w * h * px_bytes(fmt, type) : 0;
  r_glTexImage2D(t, lvl, ifmt, w, h, border, fmt, type, px);
  dcr_pc_end(PC_TEX, t0, bytes);
  dcr_pc_tex_format((fmt & 0x7fff) << 16 | (type & 0xffff), bytes);
}

/* The game's controller card, with a Pro Controller (a8r_card.c): its two
 * textures are known by their first block, then their CRC. */
const uint8_t *a8r_card_replace(uint32_t crc); /* a8r_card.c */
static const uint8_t *card_pixels(GLenum ifmt, GLsizei w, GLsizei h, GLsizei size, const void *d) {
  static const uint8_t k_rgb[8] = {0x00, 0x00, 0x00, 0x02, 0xff, 0xff, 0xff, 0xff};
  static const uint8_t k_alpha[8] = {0x94, 0x94, 0x94, 0xfc, 0xee, 0xe0, 0xff, 0xff};
  if (ifmt != 0x8D64 || w != 1024 || h != 1024 || size != 524288 || !d ||
      (memcmp(d, k_rgb, 8) && memcmp(d, k_alpha, 8)))
    return NULL;
  return a8r_card_replace((uint32_t)mz_crc32(MZ_CRC32_INIT, d, (size_t)size));
}

static void w_glCompressedTexImage2D(GLenum t, GLint lvl, GLenum ifmt, GLsizei w, GLsizei h, GLint border,
                                     GLsizei size, const void *d) {
  const uint8_t *card = lvl == 0 ? card_pixels(ifmt, w, h, size, d) : NULL;
  if (card) {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(t, 0, GL_RGB, 1024, 1024, 0, GL_RGB, GL_UNSIGNED_BYTE, card);
    return;
  }
  uint64_t t0 = dcr_pc_begin();
  r_glCompressedTexImage2D(t, lvl, ifmt, w, h, border, size, d);
  dcr_pc_end(PC_CTEX, t0, (uint64_t)size);
  dcr_pc_tex_format(0x80000000u | (ifmt & 0x7fff) << 16, (uint64_t)size);
}

/* ---- timed only (the loading counters, a8r_prof.c) ---- */
static void w_glTexSubImage2D(GLenum t, GLint lvl, GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type,
                              const void *px) {
  uint64_t t0 = dcr_pc_begin();
  r_glTexSubImage2D(t, lvl, x, y, w, h, fmt, type, px);
  dcr_pc_end(PC_TEX, t0, px ? (uint64_t)w * h * px_bytes(fmt, type) : 0);
}
static void w_glCompressedTexSubImage2D(GLenum t, GLint lvl, GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt,
                                        GLsizei size, const void *d) {
  uint64_t t0 = dcr_pc_begin();
  r_glCompressedTexSubImage2D(t, lvl, x, y, w, h, fmt, size, d);
  dcr_pc_end(PC_CTEX, t0, (uint64_t)size);
}
static void w_glCompileShader(GLuint s) {
  uint64_t t0 = dcr_pc_begin();
  r_glCompileShader(s);
  dcr_pc_end(PC_COMPILE, t0, 0);
}
static void w_glLinkProgram(GLuint p) {
  uint64_t t0 = dcr_pc_begin();
  r_glLinkProgram(p);
  dcr_pc_end(PC_LINK, t0, 0);
}
static void w_glGenerateMipmap(GLenum t) {
  uint64_t t0 = dcr_pc_begin();
  r_glGenerateMipmap(t);
  dcr_pc_end(PC_MIPMAP, t0, 0);
}

/* Every gl* function the engine looks up: ours for those, else 0 (the
 * real one). */
uintptr_t port_gl_wrap(const char *name, uintptr_t real) {
#define W(fn)                            \
  if (!strcmp(name, #fn)) {              \
    r_##fn = (void *)real;               \
    return real ? (uintptr_t)w_##fn : 0; \
  }
  W(glTexImage2D) W(glCompressedTexImage2D) W(glTexSubImage2D) W(glCompressedTexSubImage2D)
  W(glCompileShader) W(glLinkProgram) W(glGenerateMipmap)
#undef W
  return 0;
}

/* A presented frame is where the engine's frame ends (its loop is in
 * libasphalt8: the render, then the swap), and the next one begins: the GPU
 * time around the swap (a8r_perf.c), the frame's profile after it
 * (a8r_prof.c) -- its next frame begins in the frame hook, after the boost's
 * frame end, as before. */
void port_gl_before_swap(void) { a8r_perf_gpu_end(); }

void port_gl_after_swap(uint32_t frame) {
  a8r_perf_gpu_begin();
  dcr_prof_frame_end(frame);
}

static void frame_hook(void) { dcr_prof_frame_begin(); }

/* port_run(), before the engine's first frame */
void a8r_gl_hooks(void) { dcr_frame_hook = frame_hook; }
