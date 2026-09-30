/* a8r_ui.h -- a small 2D renderer for the start screen (a8r_ui.c). */
#ifndef A8R_UI_H
#define A8R_UI_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  unsigned tex;
  int w, h;
} UiImage;

/* EGL and GLES 2 on the default window, at the game's resolution; 0 = up.
 * ui_close gives the window back whole (the game makes its own EGL). */
int ui_open(void);
void ui_close(void);
int ui_w(void);
int ui_h(void);
void ui_begin(uint32_t clear_argb);
void ui_present(void);
void ui_flush(void); /* draw what is batched (before reading the frame back) */

/* Pixels, top-left origin; colours 0xAARRGGBB. */
void ui_rect(float x, float y, float w, float h, uint32_t argb);
void ui_vgrad(float x, float y, float w, float h, uint32_t top, uint32_t bottom);
void ui_hgrad(float x, float y, float w, float h, uint32_t left, uint32_t right);
/* a PNG or JPEG; _fx: its pixels (RGBA, top row first) edited by fx before they are uploaded */
int ui_image_png(UiImage *im, const uint8_t *png, size_t len);
int ui_image_png_fx(UiImage *im, const uint8_t *png, size_t len, void (*fx)(uint8_t *rgba, int w, int h));
void ui_image_free(UiImage *im);
void ui_image(const UiImage *im, float x, float y, float w, float h, uint32_t tint);
void ui_clip(float x, float y, float w, float h);
void ui_unclip(void);

/* Text: the main font, per glyph the fallback where it has none. Baked at the
 * given pixel sizes (em); drawn at any size from the nearest bake. */
#define UI_FONT_SIZES 3
int ui_fonts(const uint8_t *main_ttf, const uint8_t *fallback_ttf, const float em_px[UI_FONT_SIZES]);
/* n < 0: the whole string. spacing: extra advance per glyph, in ems. */
float ui_text_w(float px, const char *s, int n, float spacing);
void ui_text(float px, float x, float top, const char *s, int n, uint32_t argb, float spacing);
float ui_line_h(float px);
/* The byte length of the first line of s that fits `width` (words kept
 * whole where they fit a line); *next: where the next line starts. */
int ui_wrap(float px, const char *s, float width, float spacing, const char **next);
/* Next code point of UTF-8 s; advances *s. */
uint32_t ui_utf8(const char **s);

#endif
