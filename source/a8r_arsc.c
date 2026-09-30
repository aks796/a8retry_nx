/* a8r_arsc.c -- the start screen's texts, from the user's A8R.apk.
 *
 * The mod's start screen (a8r_menu.c) is an Android activity whose words --
 * every button, option, help page and the What's New list, in English and
 * the mod's six translations -- are string resources in the APK's
 * resources.arsc. They are read from there at run time (nothing of the mod
 * ships with this program): the table's "string" type is walked once and
 * every saveload_* string is kept, per language, as UTF-8.
 *
 * Format (Android's ResourceTypes.h): a RES_TABLE chunk holding the global
 * string pool (the values) and a package chunk; the package holds its type
 * and key name pools and, per type and configuration, a TYPE chunk: entry
 * offsets, then entries (key index, then a Res_value; for a string,
 * dataType 3 and the global pool index). The configuration's locale is
 * bytes 8-11 of ResTable_config (language, country; zeros: the default). MIT.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "a8r_arsc.h"
#include "util.h"

#define T_STRING_POOL 0x0001
#define T_TABLE 0x0002
#define T_PACKAGE 0x0200
#define T_TYPE 0x0201

typedef struct {
  const uint8_t *base;
  uint32_t count, flags;
  const uint32_t *offsets;
  const uint8_t *strings;
  const uint8_t *end;
} Pool;

typedef struct {
  char name[64];
  char lang[3];
  char *text;
} Str;

static Str *g_str;
static int g_n, g_cap;
static char g_lang[3] = "en";

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int pool_open(Pool *pl, const uint8_t *chunk, const uint8_t *limit) {
  if (chunk + 28 > limit || u16(chunk) != T_STRING_POOL)
    return -1;
  uint32_t hsize = u16(chunk + 2), size = u32(chunk + 4);
  if (chunk + size > limit || hsize < 28)
    return -1;
  pl->base = chunk;
  pl->count = u32(chunk + 8);
  pl->flags = u32(chunk + 16);
  pl->offsets = (const uint32_t *)(chunk + hsize);
  pl->strings = chunk + u32(chunk + 20);
  pl->end = chunk + size;
  if ((const uint8_t *)(pl->offsets + pl->count) > pl->end)
    return -1;
  return 0;
}

static void put_utf8(char **o, char *end, uint32_t c) {
  char *p = *o;
  if (c < 0x80 && p + 1 < end)
    *p++ = (char)c;
  else if (c < 0x800 && p + 2 < end)
    *p++ = (char)(0xC0 | c >> 6), *p++ = (char)(0x80 | (c & 0x3F));
  else if (c < 0x10000 && p + 3 < end)
    *p++ = (char)(0xE0 | c >> 12), *p++ = (char)(0x80 | ((c >> 6) & 0x3F)), *p++ = (char)(0x80 | (c & 0x3F));
  else if (p + 4 < end)
    *p++ = (char)(0xF0 | c >> 18), *p++ = (char)(0x80 | ((c >> 12) & 0x3F)),
    *p++ = (char)(0x80 | ((c >> 6) & 0x3F)), *p++ = (char)(0x80 | (c & 0x3F));
  *o = p;
}

/* String i of the pool as a new UTF-8 string (NULL if out of range). */
static char *pool_get(const Pool *pl, uint32_t i) {
  if (i >= pl->count)
    return NULL;
  const uint8_t *p = pl->strings + pl->offsets[i];
  if (p >= pl->end)
    return NULL;
  if (pl->flags & 0x100) { /* UTF-8: char count, byte count (1 or 2 bytes each), bytes */
    p += (p[0] & 0x80) ? 2 : 1;
    uint32_t n = p[0];
    if (n & 0x80)
      n = (n & 0x7F) << 8 | p[1], p += 2;
    else
      p += 1;
    if (p + n > pl->end)
      return NULL;
    char *s = malloc(n + 1);
    if (s) {
      memcpy(s, p, n);
      s[n] = 0;
    }
    return s;
  }
  uint32_t n = u16(p); /* UTF-16 */
  p += 2;
  if (n & 0x8000)
    n = (n & 0x7FFF) << 16 | u16(p), p += 2;
  if (p + 2 * n > pl->end)
    return NULL;
  char *s = malloc(n * 3 + 1), *o = s;
  if (!s)
    return NULL;
  for (uint32_t k = 0; k < n; k++) {
    uint32_t c = u16(p + 2 * k);
    if (c >= 0xD800 && c < 0xDC00 && k + 1 < n) {
      uint32_t lo = u16(p + 2 * (k + 1));
      c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
      k++;
    }
    put_utf8(&o, s + n * 3 + 1, c);
  }
  *o = 0;
  return s;
}

static void keep(const char *name, const char *lang, char *text) {
  if (g_n == g_cap) {
    int cap = g_cap ? g_cap * 2 : 512;
    Str *n = realloc(g_str, cap * sizeof *n);
    if (!n) {
      free(text);
      return;
    }
    g_str = n, g_cap = cap;
  }
  Str *s = &g_str[g_n++];
  snprintf(s->name, sizeof s->name, "%s", name);
  memcpy(s->lang, lang, 3);
  s->text = text;
}

static void walk_type(const uint8_t *t, const uint8_t *end, const Pool *values, const Pool *keys) {
  uint32_t hsize = u16(t + 2), count = u32(t + 12), start = u32(t + 16);
  uint8_t flags = t[9];
  const uint8_t *cfg = t + 20;
  char lang[3] = {0};
  if (u32(cfg) >= 12 && cfg[8] >= 'a' && cfg[8] <= 'z')
    lang[0] = (char)cfg[8], lang[1] = (char)cfg[9];
  if (u32(cfg) >= 12 && cfg[10]) /* a country too (en-rGB...): the language's plain form is enough */
    return;
  const uint8_t *offs = t + hsize, *entries = t + start;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t off;
    if (flags & 0x01) /* sparse: (index, offset / 4) pairs */
      off = (uint32_t)u16(offs + 4 * i + 2) * 4;
    else if (flags & 0x02) { /* 16-bit offsets / 4 */
      uint16_t o = u16(offs + 2 * i);
      if (o == 0xFFFF)
        continue;
      off = (uint32_t)o * 4;
    } else {
      off = u32(offs + 4 * i);
      if (off == 0xFFFFFFFFu)
        continue;
    }
    const uint8_t *e = entries + off;
    if (e + 16 > end)
      continue;
    uint16_t esize = u16(e), eflags = u16(e + 2);
    if (eflags & 0x0009) /* complex (a map) or compact: not a plain string */
      continue;
    const uint8_t *v = e + esize;
    if (v + 8 > end || v[3] != 0x03) /* TYPE_STRING */
      continue;
    char *key = pool_get(keys, u32(e + 4));
    if (key && !strncmp(key, "saveload_", 9)) {
      char *text = pool_get(values, u32(v + 4));
      if (text)
        keep(key, lang[0] ? lang : "", text);
    }
    free(key);
  }
}

int a8r_arsc_load(const uint8_t *d, size_t len) {
  const uint8_t *end = d + len;
  if (len < 12 || u16(d) != T_TABLE)
    return 0;
  Pool values;
  const uint8_t *c = d + u16(d + 2);
  if (pool_open(&values, c, end))
    return 0;
  for (c = values.end; c + 8 <= end;) {
    uint32_t size = u32(c + 4);
    if (size < 8 || c + size > end)
      break;
    if (u16(c) == T_PACKAGE) {
      const uint8_t *pkg = c, *pend = c + size;
      Pool types, keys;
      if (pool_open(&types, pkg + u32(pkg + 268), pend) || pool_open(&keys, pkg + u32(pkg + 276), pend))
        break;
      for (const uint8_t *t = pkg + u16(pkg + 2); t + 8 <= pend;) {
        uint32_t ts = u32(t + 4);
        if (ts < 8 || t + ts > pend)
          break;
        if (u16(t) == T_TYPE) {
          char *tname = pool_get(&types, (uint32_t)t[8] - 1);
          if (tname && !strcmp(tname, "string"))
            walk_type(t, t + ts, &values, &keys);
          free(tname);
        }
        t += ts;
      }
    }
    c += size;
  }
  debugPrintf("[menu] %d start screen texts from resources.arsc\n", g_n);
  return g_n;
}

void a8r_arsc_set_lang(const char *lang) { snprintf(g_lang, sizeof g_lang, "%s", lang && *lang ? lang : "en"); }

const char *a8r_str(const char *name) {
  const char *def = NULL;
  for (int i = 0; i < g_n; i++)
    if (!strcmp(g_str[i].name, name)) {
      if (!strcmp(g_str[i].lang, g_lang))
        return g_str[i].text;
      if (!g_str[i].lang[0])
        def = g_str[i].text;
    }
  return def ? def : "";
}
