/* a8r_ui.c -- a small 2D renderer for the start screen (a8r_menu.c).
 *
 * EGL + GLES 2 through this program's Mesa, on the default window, before the
 * game has one: coloured and gradient rectangles, PNG images (stb_image),
 * text (stb_truetype: a glyph atlas baked from the mod's own fonts, read from
 * A8R.apk), scissor clipping for scrolled lists. Everything is batched into
 * one vertex array and drawn when the texture changes or the frame ends.
 *
 * The window: the boot console gives it up for good first (log_console_close:
 * the console cannot have it back once Mesa has used it), and ui_close leaves
 * nothing behind -- surface destroyed (Mesa's Switch platform releases the
 * window's buffers with it), EGL terminated -- so the game's own EGL start
 * is a first start, as the GL self-test's teardown already shows. MIT.
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "a8r_ui.h"
#include "dcr_config.h"
#include "util.h"

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG /* the start screen's background may be the user's photo */
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_IMPLEMENTATION
#include "thirdparty/stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "thirdparty/stb_truetype.h"

void dcr_window_prepare(void); /* android_ndk.c */

static EGLDisplay g_d = EGL_NO_DISPLAY;
static EGLSurface g_s = EGL_NO_SURFACE;
static EGLContext g_c = EGL_NO_CONTEXT;
static int g_w, g_h;
static GLuint g_prog, g_white, g_atlas;
static GLint g_u_scr, g_u_alpha;

int ui_w(void) { return g_w; }
int ui_h(void) { return g_h; }

/* ------------------------------------------------------------- batching */
typedef struct {
  float x, y, u, v;
  uint32_t c;
} Vtx;

#define MAX_V (6 * 4096)
static Vtx g_v[MAX_V];
static int g_nv;
static GLuint g_tex;
static int g_alpha_tex;

static uint32_t rgba(uint32_t argb) { /* 0xAARRGGBB -> bytes R G B A */
  return (argb >> 16 & 0xFF) | (argb & 0xFF00) | (argb & 0xFF) << 16 | (argb & 0xFF000000u);
}

static void flush(void) {
  if (!g_nv)
    return;
  glBindTexture(GL_TEXTURE_2D, g_tex);
  glUniform1f(g_u_alpha, g_alpha_tex ? 1.0f : 0.0f);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), &g_v[0].x);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), &g_v[0].u);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vtx), &g_v[0].c);
  glDrawArrays(GL_TRIANGLES, 0, g_nv);
  g_nv = 0;
}

static void quad(GLuint tex, int alpha, float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                 float v1, uint32_t top, uint32_t bottom) {
  if (tex != g_tex || alpha != g_alpha_tex || g_nv + 6 > MAX_V) {
    flush();
    g_tex = tex;
    g_alpha_tex = alpha;
  }
  uint32_t ct = rgba(top), cb = rgba(bottom);
  Vtx *v = &g_v[g_nv];
  v[0] = (Vtx){x0, y0, u0, v0, ct};
  v[1] = (Vtx){x1, y0, u1, v0, ct};
  v[2] = (Vtx){x0, y1, u0, v1, cb};
  v[3] = (Vtx){x1, y0, u1, v0, ct};
  v[4] = (Vtx){x1, y1, u1, v1, cb};
  v[5] = (Vtx){x0, y1, u0, v1, cb};
  g_nv += 6;
}

void ui_rect(float x, float y, float w, float h, uint32_t argb) {
  quad(g_white, 0, x, y, x + w, y + h, 0, 0, 1, 1, argb, argb);
}

void ui_vgrad(float x, float y, float w, float h, uint32_t top, uint32_t bottom) {
  quad(g_white, 0, x, y, x + w, y + h, 0, 0, 1, 1, top, bottom);
}

void ui_hgrad(float x, float y, float w, float h, uint32_t left, uint32_t right) {
  if (g_white != g_tex || g_alpha_tex || g_nv + 6 > MAX_V) {
    flush();
    g_tex = g_white;
    g_alpha_tex = 0;
  }
  uint32_t cl = rgba(left), cr = rgba(right);
  float x1 = x + w, y1 = y + h;
  Vtx *v = &g_v[g_nv];
  v[0] = (Vtx){x, y, 0, 0, cl};
  v[1] = (Vtx){x1, y, 1, 0, cr};
  v[2] = (Vtx){x, y1, 0, 1, cl};
  v[3] = (Vtx){x1, y, 1, 0, cr};
  v[4] = (Vtx){x1, y1, 1, 1, cr};
  v[5] = (Vtx){x, y1, 0, 1, cl};
  g_nv += 6;
}

void ui_image(const UiImage *im, float x, float y, float w, float h, uint32_t tint) {
  if (im && im->tex)
    quad(im->tex, 0, x, y, x + w, y + h, 0, 0, 1, 1, tint, tint);
}

void ui_clip(float x, float y, float w, float h) {
  flush();
  glEnable(GL_SCISSOR_TEST);
  int x0 = (int)floorf(x), y0 = (int)floorf(y), x1 = (int)ceilf(x + w), y1 = (int)ceilf(y + h);
  glScissor(x0, g_h - y1, x1 - x0, y1 - y0);
}

void ui_unclip(void) {
  flush();
  glDisable(GL_SCISSOR_TEST);
}

static GLuint make_texture(const void *px, int w, int h, GLenum fmt) {
  GLuint t = 0;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, px);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  g_tex = 0; /* the batch's binding is stale now */
  return t;
}

int ui_image_png_fx(UiImage *im, const uint8_t *png, size_t len, void (*fx)(uint8_t *rgba, int w, int h)) {
  memset(im, 0, sizeof *im);
  int w, h, n;
  uint8_t *px = png ? stbi_load_from_memory(png, (int)len, &w, &h, &n, 4) : NULL;
  if (!px)
    return -1;
  if (fx)
    fx(px, w, h);
  flush();
  im->tex = make_texture(px, w, h, GL_RGBA);
  im->w = w, im->h = h;
  stbi_image_free(px);
  return 0;
}

int ui_image_png(UiImage *im, const uint8_t *png, size_t len) { return ui_image_png_fx(im, png, len, NULL); }

void ui_image_free(UiImage *im) {
  if (im->tex) {
    flush();
    glDeleteTextures(1, &im->tex);
  }
  memset(im, 0, sizeof *im);
}

/* ---------------------------------------------------------------- text */
#define ATLAS 2048
static const uint32_t k_ranges[][2] = {
    {0x20, 0x7E},     {0xA0, 0x17F},    {0x400, 0x45F}, /* Latin, Latin-1, Latin Ext-A, Cyrillic */
    {0x1EA0, 0x1EF9}, {0x2010, 0x2026}, {0x20AC, 0x20AC}, {0x2122, 0x2122},
};
static uint32_t *g_cp;    /* the code points, sorted */
static int g_ncp;
static stbtt_packedchar *g_pc[UI_FONT_SIZES]; /* per size, per code point */
static uint8_t *g_have[UI_FONT_SIZES];        /* baked? */
static float g_em[UI_FONT_SIZES], g_ascent[UI_FONT_SIZES], g_lineh[UI_FONT_SIZES];

uint32_t ui_utf8(const char **ps) {
  const uint8_t *s = (const uint8_t *)*ps;
  uint32_t c = *s++;
  if (c >= 0xC0) {
    int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
    c &= 0x3F >> n;
    for (; n && (*s & 0xC0) == 0x80; n--)
      c = c << 6 | (*s++ & 0x3F);
  }
  *ps = (const char *)s;
  return c;
}

static int cp_index(uint32_t c) {
  int lo = 0, hi = g_ncp - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (g_cp[mid] == c)
      return mid;
    if (g_cp[mid] < c)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return -1;
}

int ui_fonts(const uint8_t *main_ttf, const uint8_t *fallback_ttf, const float em_px[UI_FONT_SIZES]) {
  stbtt_fontinfo fm, ff;
  if (!main_ttf || !stbtt_InitFont(&fm, main_ttf, 0))
    return -1;
  int have_fb = fallback_ttf && stbtt_InitFont(&ff, fallback_ttf, 0);
  g_ncp = 0;
  for (unsigned r = 0; r < sizeof k_ranges / sizeof k_ranges[0]; r++)
    g_ncp += (int)(k_ranges[r][1] - k_ranges[r][0] + 1);
  g_cp = malloc(g_ncp * sizeof *g_cp);
  int *in_main = malloc(g_ncp * sizeof(int)), *in_fb = malloc(g_ncp * sizeof(int));
  int *list_m = malloc(g_ncp * sizeof(int)), *list_f = malloc(g_ncp * sizeof(int));
  uint8_t *atlas = calloc(ATLAS, ATLAS);
  if (!g_cp || !in_main || !in_fb || !list_m || !list_f || !atlas)
    return -1;
  int n = 0, nm = 0, nf = 0;
  for (unsigned r = 0; r < sizeof k_ranges / sizeof k_ranges[0]; r++)
    for (uint32_t c = k_ranges[r][0]; c <= k_ranges[r][1]; c++)
      g_cp[n++] = c;
  for (int i = 0; i < g_ncp; i++) {
    in_main[i] = stbtt_FindGlyphIndex(&fm, (int)g_cp[i]) != 0;
    in_fb[i] = !in_main[i] && have_fb && stbtt_FindGlyphIndex(&ff, (int)g_cp[i]) != 0;
    if (in_main[i])
      list_m[nm++] = (int)g_cp[i];
    else if (in_fb[i])
      list_f[nf++] = (int)g_cp[i];
  }
  stbtt_pack_context pk;
  stbtt_PackBegin(&pk, atlas, ATLAS, ATLAS, 0, 1, NULL);
  stbtt_PackSetOversampling(&pk, 1, 1);
  stbtt_packedchar *pm = malloc(nm * sizeof *pm), *pf = malloc((nf ? nf : 1) * sizeof *pf);
  int ok = pm && pf;
  for (int s = 0; s < UI_FONT_SIZES && ok; s++) {
    g_pc[s] = calloc(g_ncp, sizeof(stbtt_packedchar));
    g_have[s] = calloc(g_ncp, 1);
    if (!g_pc[s] || !g_have[s]) {
      ok = 0;
      break;
    }
    stbtt_pack_range rm = {STBTT_POINT_SIZE(em_px[s]), 0, list_m, nm, pm, 0, 0};
    if (!stbtt_PackFontRanges(&pk, main_ttf, 0, &rm, 1))
      debugPrintf("[menu] font atlas full at size %d\n", s);
    if (nf) {
      stbtt_pack_range rf = {STBTT_POINT_SIZE(em_px[s]), 0, list_f, nf, pf, 0, 0};
      stbtt_PackFontRanges(&pk, fallback_ttf, 0, &rf, 1);
    }
    for (int k = 0; k < nm; k++) {
      int i = cp_index((uint32_t)list_m[k]);
      g_pc[s][i] = pm[k];
      g_have[s][i] = 1;
    }
    for (int k = 0; k < nf; k++) {
      int i = cp_index((uint32_t)list_f[k]);
      g_pc[s][i] = pf[k];
      g_have[s][i] = 1;
    }
    float sc = stbtt_ScaleForMappingEmToPixels(&fm, em_px[s]);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&fm, &asc, &desc, &gap);
    g_em[s] = em_px[s];
    g_ascent[s] = (float)asc * sc;
    g_lineh[s] = (float)(asc - desc + gap) * sc;
  }
  stbtt_PackEnd(&pk);
  if (ok) {
    flush();
    g_atlas = make_texture(atlas, ATLAS, ATLAS, GL_ALPHA);
  }
  debugPrintf("[menu] fonts: %d glyphs from the main font, %d from the fallback, %d sizes\n", nm, nf,
              UI_FONT_SIZES);
  free(atlas), free(pm), free(pf), free(in_main), free(in_fb), free(list_m), free(list_f);
  return ok ? 0 : -1;
}

/* the bake for a size: the smallest at least as large, else the largest */
static int bake_for(float px) {
  int best = UI_FONT_SIZES - 1;
  for (int s = UI_FONT_SIZES - 1; s >= 0; s--)
    if (g_em[s] >= px - 0.5f)
      best = s;
  return best;
}

float ui_line_h(float px) {
  int s = bake_for(px);
  return g_em[s] ? g_lineh[s] * px / g_em[s] : px * 1.2f;
}

static float advance(int s, uint32_t c, float k) {
  int i = cp_index(c);
  if (i < 0 || !g_have[s][i])
    i = cp_index('?');
  return i < 0 ? 0.0f : g_pc[s][i].xadvance * k;
}

float ui_text_w(float px, const char *s, int n, float spacing) {
  if (!g_ncp)
    return 0;
  int b = bake_for(px);
  float k = px / g_em[b], w = 0;
  const char *end = n < 0 ? s + strlen(s) : s + n;
  int glyphs = 0;
  while (s < end) {
    w += advance(b, ui_utf8(&s), k);
    glyphs++;
  }
  return w + (glyphs > 1 ? (glyphs - 1) * spacing * px : 0);
}

void ui_text(float px, float x, float top, const char *s, int n, uint32_t argb, float spacing) {
  if (!g_ncp || !g_atlas)
    return;
  int b = bake_for(px);
  float k = px / g_em[b], base = top + g_ascent[b] * k;
  const char *end = n < 0 ? s + strlen(s) : s + n;
  while (s < end) {
    uint32_t c = ui_utf8(&s);
    int i = cp_index(c);
    if (i < 0 || !g_have[b][i])
      i = cp_index('?');
    if (i < 0)
      continue;
    const stbtt_packedchar *p = &g_pc[b][i];
    if (c != ' ')
      quad(g_atlas, 1, x + p->xoff * k, base + p->yoff * k, x + p->xoff2 * k, base + p->yoff2 * k,
           p->x0 / (float)ATLAS, p->y0 / (float)ATLAS, p->x1 / (float)ATLAS, p->y1 / (float)ATLAS, argb, argb);
    x += p->xadvance * k + spacing * px;
  }
}

int ui_wrap(float px, const char *s, float width, float spacing, const char **next) {
  const char *p = s, *brk = NULL, *brk_next = NULL;
  float w = 0;
  int b = bake_for(px);
  float k = g_em[b] ? px / g_em[b] : 1.0f;
  while (*p && *p != '\n') {
    const char *at = p;
    uint32_t c = ui_utf8(&p);
    float a = advance(b, c, k) + spacing * px;
    if (c == ' ') {
      brk = at, brk_next = p;
    } else if (w + a > width && at > s) {
      if (brk) {
        *next = brk_next;
        return (int)(brk - s);
      }
      *next = at; /* one word longer than the line: cut it */
      return (int)(at - s);
    }
    w += a;
  }
  *next = *p == '\n' ? p + 1 : p;
  return (int)(p - s);
}

/* ----------------------------------------------------------------- EGL */
static GLuint shader(GLenum type, const char *src) {
  GLuint sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, NULL);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[400] = "";
    glGetShaderInfoLog(sh, sizeof log, NULL, log);
    debugPrintf("[menu] shader: %s\n", log);
  }
  return sh;
}

int ui_open(void) {
  log_console_close(); /* the window goes to EGL for good (util.c) */
  dcr_window_prepare();
  g_d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint n = 0;
  if (!eglInitialize(g_d, NULL, NULL)) {
    debugPrintf("[menu] eglInitialize failed 0x%x\n", eglGetError());
    return -1;
  }
  eglBindAPI(EGL_OPENGL_ES_API);
  static const EGLint cfg_attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE,
                                     8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
  EGLConfig cfg;
  static const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  if (!eglChooseConfig(g_d, cfg_attrs, &cfg, 1, &n) || n < 1 ||
      !(g_s = eglCreateWindowSurface(g_d, cfg, (EGLNativeWindowType)nwindowGetDefault(), NULL)) ||
      !(g_c = eglCreateContext(g_d, cfg, EGL_NO_CONTEXT, ctx_attrs)) || !eglMakeCurrent(g_d, g_s, g_s, g_c)) {
    debugPrintf("[menu] EGL setup failed 0x%x\n", eglGetError());
    ui_close();
    return -1;
  }
  eglSwapInterval(g_d, 1);
  EGLint w = 0, h = 0;
  eglQuerySurface(g_d, g_s, EGL_WIDTH, &w);
  eglQuerySurface(g_d, g_s, EGL_HEIGHT, &h);
  g_w = w > 0 ? w : dcr_config()->res_w;
  g_h = h > 0 ? h : dcr_config()->res_h;

  g_prog = glCreateProgram();
  glAttachShader(g_prog, shader(GL_VERTEX_SHADER, "attribute vec2 p; attribute vec2 t; attribute vec4 c;"
                                                  "uniform vec2 scr; varying vec2 vt; varying vec4 vc;"
                                                  "void main() { gl_Position = vec4(p.x / scr.x * 2.0 - 1.0,"
                                                  " 1.0 - p.y / scr.y * 2.0, 0.0, 1.0); vt = t; vc = c; }"));
  glAttachShader(g_prog, shader(GL_FRAGMENT_SHADER,
                                "precision mediump float; uniform sampler2D tex; uniform float a;"
                                "varying vec2 vt; varying vec4 vc;"
                                "void main() { vec4 s = texture2D(tex, vt);"
                                " gl_FragColor = a > 0.5 ? vec4(vc.rgb, vc.a * s.a) : vc * s; }"));
  glBindAttribLocation(g_prog, 0, "p");
  glBindAttribLocation(g_prog, 1, "t");
  glBindAttribLocation(g_prog, 2, "c");
  glLinkProgram(g_prog);
  glUseProgram(g_prog);
  g_u_scr = glGetUniformLocation(g_prog, "scr");
  g_u_alpha = glGetUniformLocation(g_prog, "a");
  glUniform1i(glGetUniformLocation(g_prog, "tex"), 0);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);
  static const uint32_t white = 0xFFFFFFFFu;
  g_white = make_texture(&white, 1, 1, GL_RGBA);
  debugPrintf("[menu] EGL up: %dx%d, %s\n", g_w, g_h, (const char *)glGetString(GL_RENDERER));
  return 0;
}

void ui_begin(uint32_t clear_argb) {
  glViewport(0, 0, g_w, g_h);
  glDisable(GL_SCISSOR_TEST);
  glClearColor((clear_argb >> 16 & 0xFF) / 255.0f, (clear_argb >> 8 & 0xFF) / 255.0f, (clear_argb & 0xFF) / 255.0f,
               1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(g_prog);
  glUniform2f(g_u_scr, (float)g_w, (float)g_h);
  glActiveTexture(GL_TEXTURE0);
}

void ui_flush(void) { flush(); }

void ui_present(void) {
  flush();
  eglSwapBuffers(g_d, g_s);
}

void ui_close(void) {
  if (g_d == EGL_NO_DISPLAY)
    return;
  if (g_c != EGL_NO_CONTEXT) {
    g_nv = 0;
    if (g_atlas)
      glDeleteTextures(1, &g_atlas);
    if (g_white)
      glDeleteTextures(1, &g_white);
    if (g_prog)
      glDeleteProgram(g_prog);
    glFinish();
  }
  eglMakeCurrent(g_d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (g_c != EGL_NO_CONTEXT)
    eglDestroyContext(g_d, g_c);
  if (g_s != EGL_NO_SURFACE)
    eglDestroySurface(g_d, g_s);
  /* The display stays initialised: the game's eglInitialize finds it up, and
   * its surface and contexts are made on the same Mesa screen, as when no
   * start screen came first (hardware 2026-09-25: races ran ~55-60 fps before
   * the start screen was added, 31-37 fps after, on a second screen). */
  g_d = EGL_NO_DISPLAY, g_s = EGL_NO_SURFACE, g_c = EGL_NO_CONTEXT;
  g_atlas = g_white = g_prog = 0;
  for (int s = 0; s < UI_FONT_SIZES; s++) {
    free(g_pc[s]), free(g_have[s]);
    g_pc[s] = NULL, g_have[s] = NULL;
  }
  free(g_cp);
  g_cp = NULL, g_ncp = 0;
}
