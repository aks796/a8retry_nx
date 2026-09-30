/* a8r_card.c -- the game's controller card, with a Switch Pro Controller.
 *
 * With a controller connected the game opens on a card of its buttons (the
 * splash's "PRESS ANY KEY TO CONTINUE"), and its Options have the same: for
 * the NVIDIA SHIELD controller this port reports (a8r_input.c), a drawing of
 * that controller with numbered callouts, and the game's own list of what
 * each number does ("1. BRAKE ... 13. NITRO"). The drawing is one texture
 * pair of the OBB (1024 x 1024 ETC1, colour and alpha; two 600 x 480
 * diagrams, one above the other), recognised by its CRC as the engine uploads
 * it (gl_mesa.c) and replaced by the port's own card: a Pro Controller whose
 * numbers point at the Switch buttons that do what the game's list says in
 * the Switch race layout ([controls] race_scheme; with the SHIELD layout the
 * game's card is right and stays). The card is tools/card/labeledcontroller.png,
 * built in as a8r_card_art.h; nothing of the game's drawing is kept. MIT.
 */
#include <stdlib.h>
#include <string.h>

#include "a8r.h"
#include "dcr_config.h"
#include "thirdparty/stb_image.h" /* the implementation is in a8r_ui.c */
#include "a8r_card_art.h"
#include "util.h"

#define TEX 1024

/* The replacement pair, built once: RGB colour, and the alpha as grey RGB
 * (as the game's ETC1 alpha textures are). */
static uint8_t *g_rgb, *g_alpha;

static int build(void) {
  if (g_rgb)
    return 0;
  int w = 0, h = 0, n = 0;
  uint8_t *art = stbi_load_from_memory(k_card_art_png, sizeof k_card_art_png, &w, &h, &n, 4);
  if (!art || w != A8R_CARD_ART_W || h != A8R_CARD_ART_H) {
    debugPrintf("[card] the built-in card does not decode: the game's own card stays\n");
    if (art)
      stbi_image_free(art);
    return -1;
  }
  g_rgb = calloc(TEX * TEX, 3);
  g_alpha = calloc(TEX * TEX, 3);
  if (!g_rgb || !g_alpha) {
    free(g_rgb), free(g_alpha);
    g_rgb = g_alpha = NULL;
    stbi_image_free(art);
    return -1;
  }
  for (int half = 0; half < 2; half++) /* both diagrams: 15 and 16 callouts */
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        const uint8_t *s = &art[(y * w + x) * 4];
        int i = ((half * 480 + y) * TEX + x) * 3;
        memcpy(&g_rgb[i], s, 3);
        g_alpha[i] = g_alpha[i + 1] = g_alpha[i + 2] = s[3];
      }
  stbi_image_free(art);
  debugPrintf("[card] the controller card: the Pro Controller with the Switch racing layout\n");
  return 0;
}

/* gl_mesa.c: a compressed upload about to happen; the RGB pixels to upload
 * instead (1024 x 1024), or NULL. */
const uint8_t *a8r_card_replace(uint32_t crc) {
  if (crc != 0x86d52dc0u && crc != 0x4f65d6abu)
    return NULL;
  if (dcr_config()->race_scheme != 0) /* the SHIELD layout: the game's own card is right */
    return NULL;
  if (build() != 0)
    return NULL;
  return crc == 0x86d52dc0u ? g_rgb : g_alpha;
}
