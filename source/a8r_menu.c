/* a8r_menu.c -- the A8R start screen, native.
 *
 * On Android the mod opens on its own screen (com.saveload.A8R.MainActivity)
 * before the game: OPTIONS, ABOUT and HELP at the top left, BACKUP, RESTORE
 * and RESET at the bottom left, WHAT'S NEW and PLAY at the bottom right, its
 * title at the top right; options in three pages (start screen, misc,
 * graphics), Quick Race settings, a language choice. The port adds a fourth
 * options page, SWITCH (in English), for its own settings: the clocks,
 * the driver thread, the window, the race controls, the debug switches. It is built of Android
 * views, which this port does not have -- so it is redrawn here, with GLES 2
 * (a8r_ui.c), before the game starts: the same layout (the mod's dp sizes,
 * colours and images), the mod's own words in its seven languages
 * (resources.arsc, a8r_arsc.c), its music and button sounds
 * (a8r_menu_audio.c), all read from the user's A8R.apk. Its choices are the
 * port's config.ini (dcr_config.c), which a8r_setup.c turns into the engine
 * and data files as the mod's PLAY does.
 *
 * Controls: the D-pad or left stick moves between buttons, A presses (B with
 * [controls] swap_a_b), B goes back (the mod's BACK: the exit question on the
 * main screen), + is PLAY; in the options, L / R or left / right turn the
 * pages, and a row with a value (the "<>" rows) is changed by pressing A on
 * it, then left / right, then A again -- never by a stray push of the stick.
 * The right stick scrolls; the touchscreen taps and drags. The nickname is typed on the
 * Switch keyboard. "Skip start screen" skips it; holding - while the game
 * starts shows it anyway. MIT.
 */
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <switch.h>
#include <unistd.h>

#include <miniz/miniz.h>

#include "a8r.h"
#include "a8r_arsc.h"
#include "a8r_menu.h"
#include "a8r_menu_audio.h"
#include "a8r_ui.h"
#include "dcr_config.h"
#include "util.h"

#define ST(n) a8r_str(n)

const char *dcr_game_root(void); /* main.c */

/* colours (the mod's drawables and colour selectors) */
#define C_WHITE 0xFFFFFFFFu
#define C_BLUE 0xFF255182u
#define C_ORANGE 0xFFFF9600u
#define C_DIALOG 0xFF0C0A0Du
#define C_FOCUS_TOP 0xFFFBBC16u
#define C_FOCUS_BOTTOM 0xFFF99807u
#define C_RED_TOP 0xFFCF0505u
#define C_RED_BOTTOM 0xFFB40101u
#define C_RED_F_TOP 0xFFFF4D4Du
#define C_RED_F_BOTTOM 0xFFFF1A1Au
#define SPACING 0.05f /* android:letterSpacing */

/* ================================================================ the APK */
static mz_zip_archive g_zip;
static int g_zip_ok;

static uint8_t *apk_file(const char *name, size_t *len) {
  size_t n = 0;
  void *p = g_zip_ok ? mz_zip_reader_extract_file_to_heap(&g_zip, name, &n, 0) : NULL;
  if (len)
    *len = p ? n : 0;
  return p;
}

/* ================================================================ images */
enum {
  IM_TITLE,
  IM_OPTIONS, IM_OPTIONS_P, IM_ABOUT, IM_ABOUT_P, IM_HELP, IM_HELP_P,
  IM_BACKUP, IM_BACKUP_P, IM_RESTORE, IM_RESTORE_P, IM_RESET, IM_RESET_P,
  IM_CLOSE, IM_NEXT, IM_NEXT_F, IM_PREV, IM_PREV_F,
  IM_CHECKED, IM_CHECKED_F, IM_UNCHECKED, IM_UNCHECKED_F,
  IM_SELECT, IM_SELECT_F, IM_PROFILE, IM_PROFILE_F, IM_DELETE, IM_DELETE_F,
  IM_LANGUAGE, IM_LANGUAGE_F, IM_QUICK, IM_QUICK_F,
  IM_FLAG, /* + 2 * language: normal, pressed */
  IM_COUNT = IM_FLAG + 14
};
static const char *const k_img[IM_FLAG] = {
    "title",
    "options_button", "options_button_pressed", "about_button", "about_button_pressed", "help_button",
    "help_button_pressed", "backup_button", "backup_button_pressed", "restore_button", "restore_button_pressed",
    "reset_button", "reset_button_pressed",
    "close_button", "next_button", "next_button_focused", "previous_button", "previous_button_focused",
    "checkbox_checked", "checkbox_checked_focused", "checkbox_unchecked", "checkbox_unchecked_focused",
    "checkbox_select", "checkbox_select_focused", "checkbox_profile", "checkbox_profile_focused",
    "checkbox_delete", "checkbox_delete_focused", "checkbox_language", "checkbox_language_focused",
    "checkbox_quickrace", "checkbox_quickrace_focused",
};
static const char *const k_lang[7] = {"en", "es", "fr", "pt", "ru", "vi", "pl"};
static UiImage g_im[IM_COUNT];

static void load_images(void) {
  for (int i = 0; i < IM_COUNT; i++) {
    char path[96];
    if (i < IM_FLAG)
      snprintf(path, sizeof path, "res/drawable/saveload_%s.png", k_img[i]);
    else
      snprintf(path, sizeof path, "res/drawable/saveload_%s_button%s.png", k_lang[(i - IM_FLAG) / 2],
               (i - IM_FLAG) & 1 ? "_pressed" : "");
    size_t n;
    uint8_t *png = apk_file(path, &n);
    if (!png || ui_image_png(&g_im[i], png, n))
      debugPrintf("[menu] no image %s\n", path);
    free(png);
  }
}

/* ================================================================ layout units */
static float S;        /* px per dp */
static float g_m = 1;  /* the main screen's "Menu size" */
static float dp(float v) { return v * S; }
static float sp(float v) { return v * S; }

/* ================================================================ input */
static PadState g_pad;
static u64 g_down, g_held;
static int g_confirm_key, g_back_key; /* HidNpadButton_A / _B, swapped by position */
static int g_touch_down, g_touch_tap;
static float g_tx, g_ty, g_tx0, g_ty0, g_drag;
static u64 g_rep_at;
static int g_rep_dir = -1;

enum { D_UP, D_DOWN, D_LEFT, D_RIGHT };

static u64 now_ms(void) { return armTicksToNs(armGetSystemTick()) / 1000000ull; }

/* D-pad / left stick, with repeat: the direction pressed this frame, or -1.
 * The stick counts along one axis: the one it is pushed along when it leaves
 * the centre (sideways only when clearly more sideways than up or down), kept
 * until it comes back -- a push down that wanders sideways stays a push down,
 * and does not turn the options page. */
static int g_stick_dir = -1;
static int nav_dir(void) {
  HidAnalogStickState l = padGetStickPos(&g_pad, 0);
  int ax = abs(l.x), ay = abs(l.y), mag = ax > ay ? ax : ay;
  if (mag < 12000)
    g_stick_dir = -1;
  else if (g_stick_dir < 0 && mag > 20000)
    g_stick_dir = 3 * ay >= 2 * ax ? (l.y > 0 ? D_UP : D_DOWN) : (l.x < 0 ? D_LEFT : D_RIGHT);
  int dir = (g_held & HidNpadButton_Up)     ? D_UP
            : (g_held & HidNpadButton_Down)  ? D_DOWN
            : (g_held & HidNpadButton_Left)  ? D_LEFT
            : (g_held & HidNpadButton_Right) ? D_RIGHT
                                             : g_stick_dir;
  if (dir < 0) {
    g_rep_dir = -1;
    return -1;
  }
  u64 t = now_ms();
  if (dir != g_rep_dir) {
    g_rep_dir = dir;
    g_rep_at = t + 350;
    return dir;
  }
  if (t >= g_rep_at) {
    g_rep_at = t + 90;
    return dir;
  }
  return -1;
}

static void poll_input(void) {
  padUpdate(&g_pad);
  g_down = padGetButtonsDown(&g_pad);
  g_held = padGetButtons(&g_pad);
  g_touch_tap = 0;
  HidTouchScreenState ts = {0};
  int touching = hidGetTouchScreenStates(&ts, 1) && ts.count > 0;
  if (touching) {
    float x = (float)ts.touches[0].x * ui_w() / 1280.0f, y = (float)ts.touches[0].y * ui_h() / 720.0f;
    if (!g_touch_down) {
      g_tx0 = x, g_ty0 = y, g_drag = 0;
    } else {
      g_drag += fabsf(x - g_tx) + fabsf(y - g_ty);
    }
    g_tx = x, g_ty = y;
  } else if (g_touch_down && g_drag < dp(12)) {
    g_touch_tap = 1; /* a tap: released where it went down */
  }
  g_touch_down = touching;
}

/* ================================================================ focusables */
typedef struct {
  int id;
  float x, y, w, h;
  int hidden; /* scrolled out of view: the controller can go there, a tap cannot */
} Hot;
static Hot g_hot[160];
static int g_nhot, g_focus;

static void hot(int id, float x, float y, float w, float h) {
  if (g_nhot < (int)(sizeof g_hot / sizeof g_hot[0]))
    g_hot[g_nhot++] = (Hot){id, x, y, w, h, 0};
}

static const Hot *hot_get(int id) {
  for (int i = 0; i < g_nhot; i++)
    if (g_hot[i].id == id)
      return &g_hot[i];
  return NULL;
}

/* the nearest focusable in a direction, by the centres; 0 if none */
static int hot_step(int from, int dir) {
  const Hot *f = hot_get(from);
  if (!f)
    return g_nhot ? g_hot[0].id : 0;
  float fx = f->x + f->w / 2, fy = f->y + f->h / 2, best = 1e9f;
  int id = 0;
  for (int i = 0; i < g_nhot; i++) {
    const Hot *h = &g_hot[i];
    if (h->id == from)
      continue;
    float dx = h->x + h->w / 2 - fx, dy = h->y + h->h / 2 - fy;
    float along = dir == D_UP ? -dy : dir == D_DOWN ? dy : dir == D_LEFT ? -dx : dx;
    float across = dir == D_UP || dir == D_DOWN ? fabsf(dx) : fabsf(dy);
    if (along < 1.0f)
      continue;
    float score = along + across * 2.5f;
    if (score < best)
      best = score, id = h->id;
  }
  return id;
}

static int hot_at(float x, float y) {
  for (int i = g_nhot - 1; i >= 0; i--) {
    const Hot *h = &g_hot[i];
    if (!h->hidden && x >= h->x && x < h->x + h->w && y >= h->y && y < h->y + h->h)
      return h->id;
  }
  return 0;
}

/* ================================================================ drawing helpers */
static void text_upper(char *out, size_t cap, const char *s) {
  size_t n = 0;
  while (*s && n + 4 < cap) {
    uint32_t c = ui_utf8(&s);
    if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7) || (c >= 0x430 && c <= 0x44F))
      c -= 0x20;
    else if (c >= 0x450 && c <= 0x45F)
      c -= 0x50;
    else if (((c >= 0x100 && c <= 0x17F) || (c >= 0x1EA0 && c <= 0x1EF9)) && (c & 1) &&
             c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F)
      c -= 1;
    if (c < 0x80)
      out[n++] = (char)c;
    else if (c < 0x800)
      out[n++] = (char)(0xC0 | c >> 6), out[n++] = (char)(0x80 | (c & 0x3F));
    else
      out[n++] = (char)(0xE0 | c >> 12), out[n++] = (char)(0x80 | ((c >> 6) & 0x3F)),
      out[n++] = (char)(0x80 | (c & 0x3F));
  }
  out[n] = 0;
}

/* text centred in a box (all caps when asked, as textAllCaps) */
static void text_in(const char *s, float px, float x, float y, float w, float h, uint32_t col, int caps, int centre) {
  char buf[256];
  if (caps) {
    text_upper(buf, sizeof buf, s);
    s = buf;
  }
  float tw = ui_text_w(px, s, -1, SPACING);
  float tx = centre ? x + (w - tw) / 2 : x;
  ui_text(px, tx, y + (h - ui_line_h(px)) / 2, s, -1, col, SPACING);
}

/* a white button; the orange gradient when focused (button_background_selector) */
static void button_bg(float x, float y, float w, float h, int focused) {
  if (focused)
    ui_vgrad(x, y, w, h, C_FOCUS_TOP, C_FOCUS_BOTTOM);
  else
    ui_rect(x, y, w, h, C_WHITE);
}

static void text_button(int id, const char *label, float px, float x, float y, float w, float h) {
  int f = g_focus == id;
  button_bg(x, y, w, h, f);
  text_in(label, px, x, y, w, h, f ? C_WHITE : C_BLUE, 1, 1);
  hot(id, x, y, w, h);
}

static void icon_button(int id, int im, float x, float y, float s) {
  int f = g_focus == id;
  button_bg(x, y, s, s, f);
  ui_image(&g_im[f ? im + 1 : im], x, y, s, s, C_WHITE);
  hot(id, x, y, s, s);
}

/* wrapped text; returns its height */
static float text_block(const char *s, float px, float x, float y, float w, uint32_t col, int centre, int draw) {
  float lh = ui_line_h(px), yy = y;
  if (!*s)
    return 0;
  while (*s) {
    const char *next;
    int n = ui_wrap(px, s, w, SPACING, &next);
    if (draw) {
      float tw = ui_text_w(px, s, n, SPACING);
      ui_text(px, centre ? x + (w - tw) / 2 : x, yy, s, n, col, SPACING);
    }
    yy += lh;
    if (next == s)
      break;
    s = next;
  }
  return yy - y;
}

/* ================================================================ toasts */
static char g_toast[128];
static u64 g_toast_until;

static void toast(const char *s) {
  snprintf(g_toast, sizeof g_toast, "%s", s);
  g_toast_until = now_ms() + 2200;
}

static void draw_toast(void) {
  if (!g_toast[0] || now_ms() > g_toast_until)
    return;
  char up[160];
  text_upper(up, sizeof up, g_toast);
  float px = sp(18), w = ui_text_w(px, up, -1, SPACING) + dp(40), h = dp(40);
  float x = (ui_w() - w) / 2, y = ui_h() - dp(100);
  ui_rect(x, y, w, h, 0xE0303030u);
  text_in(up, px, x, y, w, h, C_WHITE, 0, 1);
}

/* ================================================================ files */
static void root_path(char *out, size_t cap, const char *rel) { snprintf(out, cap, "%s/%s", dcr_game_root(), rel); }

#define DATA_REL "data/files"
#define SAVE_REL "gameloft/games/GloftA8HP/save"

/* the mod's saveLoad: a copy, recursive, of everything but *.txt */
static int copy_tree(const char *from, const char *to) {
  struct stat st;
  if (stat(from, &st) != 0)
    return 0;
  if (S_ISDIR(st.st_mode)) {
    mkdir(to, 0777);
    DIR *d = opendir(from);
    if (!d)
      return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
      size_t l = strlen(e->d_name);
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || (l > 4 && !strcasecmp(e->d_name + l - 4, ".txt")))
        continue;
      char a[512], b[512];
      snprintf(a, sizeof a, "%s/%s", from, e->d_name);
      snprintf(b, sizeof b, "%s/%s", to, e->d_name);
      n += copy_tree(a, b);
    }
    closedir(d);
    return n;
  }
  FILE *in = fopen(from, "rb"), *out = in ? fopen(to, "wb") : NULL;
  int ok = in && out;
  static uint8_t buf[64 * 1024];
  size_t k;
  while (ok && (k = fread(buf, 1, sizeof buf, in)) > 0)
    ok = fwrite(buf, 1, k, out) == k;
  if (in)
    fclose(in);
  if (out && fclose(out) != 0)
    ok = 0;
  return ok;
}

/* the mod's delete(): everything but *.txt */
static void delete_tree(const char *path, int top) {
  struct stat st;
  if (stat(path, &st) != 0)
    return;
  if (S_ISDIR(st.st_mode)) {
    DIR *d = opendir(path);
    struct dirent *e;
    while (d && (e = readdir(d))) {
      size_t l = strlen(e->d_name);
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || (l > 4 && !strcasecmp(e->d_name + l - 4, ".txt")))
        continue;
      char p[512];
      snprintf(p, sizeof p, "%s/%s", path, e->d_name);
      delete_tree(p, 0);
    }
    if (d)
      closedir(d);
    if (!top)
      rmdir(path);
  } else {
    unlink(path);
  }
}

int a8r_backup_progress(void) {
  char a[300], b[300];
  root_path(a, sizeof a, DATA_REL);
  root_path(b, sizeof b, SAVE_REL);
  return copy_tree(a, b);
}

static int restore_progress(void) {
  char a[300], b[300];
  root_path(a, sizeof a, SAVE_REL);
  root_path(b, sizeof b, DATA_REL);
  return copy_tree(a, b);
}

static void reset_progress(void) {
  char a[300];
  root_path(a, sizeof a, DATA_REL);
  delete_tree(a, 1);
}

/* "Remove custom songs": the mod moves the added songs to music/standby */
static int remove_songs(void) {
  static const char *const dirs[] = {"menu", "dubstep", "rock", "electro", "outro"};
  char base[300], standby[320];
  root_path(base, sizeof base, "gameloft/games/GloftA8HP/music");
  snprintf(standby, sizeof standby, "%s/standby", base);
  mkdir(standby, 0777);
  int n = 0;
  for (unsigned i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
    char dir[340];
    snprintf(dir, sizeof dir, "%s/%s", base, dirs[i]);
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d))) {
      if (e->d_name[0] == '.')
        continue;
      char a[600], b[600];
      snprintf(a, sizeof a, "%s/%s", dir, e->d_name);
      snprintf(b, sizeof b, "%s/%s", standby, e->d_name);
      n += rename(a, b) == 0;
    }
    if (d)
      closedir(d);
  }
  return n;
}

/* ================================================================ options */
enum { RK_BOOL, RK_CYCLE, RK_NICK, RK_LANG, RK_SONGS, RK_QUICK, RK_ALWAYS };
enum { F_RAW, F_PCT, F_X, F_SEC, F_WORD, F_MHZ, F_TXT, F_SECOFF };
enum { IC_CHECK, IC_SELECT, IC_PROFILE, IC_DELETE, IC_LANG, IC_QUICK };

typedef struct {
  const char *label, *desc;
  int kind;
  const char *sec, *key;
  int fmt, icon;
} Row;

#define B(l, d, s, k) {l, d, RK_BOOL, s, k, 0, IC_CHECK}
#define CY(l, d, s, k, f) {l, d, RK_CYCLE, s, k, f, IC_SELECT}
static const Row k_page0[] = {
    B("saveload_options_title", "saveload_options_title_message", "start_screen", "sound_effects"),
    B("saveload_options_title2", "saveload_options_title2_message", "start_screen", "music"),
    B("saveload_options_title8", "saveload_options_title8_message", "start_screen", "auto_backup"),
    B("saveload_options_title3", "saveload_options_title3_message", "start_screen", "skip_start_screen"),
    CY("saveload_options_title4", "saveload_options_title4_message", "start_screen", "menu_size", F_PCT),
    {"saveload_options_title5", "saveload_options_title5_message", RK_NICK, "mod", "nickname", F_RAW, IC_PROFILE},
    B("saveload_options_title9", "saveload_options_title9_message", "graphics", "original_soundtrack"),
    {"saveload_options_title6", "saveload_options_title6_message", RK_SONGS, NULL, NULL, 0, IC_DELETE},
    {"saveload_options_title7", "saveload_options_title7_message", RK_LANG, NULL, NULL, 0, IC_LANG},
};
static const Row k_page1[] = {
    {"saveload_options2_title11", "saveload_options2_title11_message", RK_QUICK, NULL, NULL, 0, IC_QUICK},
    B("saveload_options2_title", "saveload_options2_title_message", "mod", "retry_difficulty"),
    B("saveload_options2_title2", "saveload_options2_title2_message", "mod", "retry_camera"),
    B("saveload_options2_title3", "saveload_options2_title3_message", "mod", "knockdown_camera"),
    CY("saveload_options2_title4", "saveload_options2_title4_message", "mod", "traffic", F_WORD),
    B("saveload_options2_title5", "saveload_options2_title5_message", "mod", "infinite_gears"),
    B("saveload_options2_title6", "saveload_options2_title6_message", "mod", "gameplay_hud"),
    B("saveload_options2_title7", "saveload_options2_title7_message", "mod", "bottom_minimap"),
    B("saveload_options2_title8", "saveload_options2_title8_message", "mod", "multiplayer_cinematic"),
    B("saveload_options2_title13", "saveload_options2_title13_message", "mod", "next_button"),
    B("saveload_options2_title14", "saveload_options2_title14_message", "graphics", "3d_letters"),
    B("saveload_options2_title12", "saveload_options2_title12_message", "mod", "attract_mode"),
    B("saveload_options2_title9", "saveload_options2_title9_message", "mod", "fake_speed"),
    /* the port always reports its controller: the box stays ticked */
    {"saveload_options2_title10", "saveload_options2_title10_message", RK_ALWAYS, NULL, NULL, 0, IC_CHECK},
};
static const Row k_page2[] = {
    B("saveload_options3_title1", "saveload_options3_title1_message", "graphics", "60fps"),
    B("saveload_options3_title2", "saveload_options3_title2_message", "graphics", "high_quality"),
    B("saveload_options3_title3", "saveload_options3_title3_message", "graphics", "brake_calipers"),
    CY("saveload_options3_title4", "saveload_options3_title4_message", "graphics", "anti_aliasing", F_WORD),
    CY("saveload_options3_title5", "saveload_options3_title5_message", "graphics", "anisotropic_filtering", F_X),
    CY("saveload_options3_title6", "saveload_options3_title6_message", "graphics", "texture_filtering", F_WORD),
    CY("saveload_options3_title7", "saveload_options3_title7_message", "graphics", "resolution", F_PCT),
    B("saveload_options3_title8", "saveload_options3_title8_message", "graphics", "motion_blur"),
    B("saveload_options3_title9", "saveload_options3_title9_message", "graphics", "shadows"),
    B("saveload_options3_title10", "saveload_options3_title10_message", "graphics", "breakables"),
    B("saveload_options3_title11", "saveload_options3_title11_message", "graphics", "glass_crack"),
    B("saveload_options3_title12", "saveload_options3_title12_message", "graphics", "particles"),
    B("saveload_options3_title13", "saveload_options3_title13_message", "graphics", "metal_sparks"),
    B("saveload_options3_title14", "saveload_options3_title14_message", "graphics", "ai_particles"),
    B("saveload_options3_title15", "saveload_options3_title15_message", "graphics", "skid_marks"),
    B("saveload_options3_title25", "saveload_options3_title25_message", "graphics", "car_dirt"),
    B("saveload_options3_title16", "saveload_options3_title16_message", "graphics", "road_reflection"),
    B("saveload_options3_title17", "saveload_options3_title17_message", "graphics", "car_reflection"),
    B("saveload_options3_title18", "saveload_options3_title18_message", "graphics", "road_specular"),
    B("saveload_options3_title19", "saveload_options3_title19_message", "graphics", "car_specular"),
    B("saveload_options3_title20", "saveload_options3_title20_message", "graphics", "lens_flare"),
    B("saveload_options3_title21", "saveload_options3_title21_message", "graphics", "anamorphic_glows"),
    B("saveload_options3_title24", "saveload_options3_title24_message", "graphics", "bokeh"),
    B("saveload_options3_title22", "saveload_options3_title22_message", "graphics", "classic_license_plate"),
    B("saveload_options3_title23", "saveload_options3_title23_message", "graphics", "natural_colors"),
};
static const Row k_quick[] = {
    {"saveload_quickrace_title2", NULL, RK_CYCLE, "quick_race", "racers", F_RAW, IC_SELECT},
    {"saveload_quickrace_title", NULL, RK_CYCLE, "quick_race", "laps", F_RAW, IC_SELECT},
    {"saveload_quickrace_title3", NULL, RK_CYCLE, "quick_race", "elimination_time", F_SEC, IC_SELECT},
    {"saveload_quickrace_title4", NULL, RK_CYCLE, "quick_race", "knockdown_limit", F_RAW, IC_SELECT},
};
/* The Switch side of config.ini, which the mod's screen has no words for:
 * English, "=" before a text that is not a string resource. */
static const Row k_page3[] = {
    B("=Graphics driver on a second core", "=Runs the graphics driver beside the game on another processor "
      "core, so the game has more of each frame. Turn it off if you see graphics glitches.",
      "performance", "gl_thread"),
    B("=Faster GPU in handheld mode", "=The graphics processor at 460.8 MHz instead of 384 MHz while "
      "handheld (docked it runs at 768 MHz either way). Uses more battery.",
      "performance", "gpu_boost_handheld"),
    CY("=CPU clock: ", "=The processor's speed while the game runs. 1785 MHz holds races at 60 fps; lower "
       "speeds use less battery. System leaves the clock alone, for an overclocking tool such as sys-clk; "
       "a tool that changes the clock during play also takes over. This screen and the HOME menu run at "
       "the normal 1020 MHz.",
       "performance", "cpu_clock", F_MHZ),
    B("=Full CPU speed while loading", "=The processor at 1785 MHz while the game starts and loads, when "
      "the CPU clock above is set lower.", "performance", "boost_cpu_when_loading"),
    CY("=Screen resolution: ", "=The game's window: 1080p, 720p, or Auto -- 1080p when the game starts "
       "docked, 720p in handheld mode. The Switch scales it to the screen.",
       "display", "resolution", F_TXT),
    CY("=OpenGL ES version: ", "=The version offered to the game. 2 is the tested one; with 3 the game "
       "uses a few more effects, as on a phone that has it.", "display", "opengl_es", F_RAW),
    CY("=Loading threads: ", "=Graphics contexts the game may load textures on in the background (it asks "
       "for one). 0 loads everything on the rendering thread.", "display", "loading_contexts", F_RAW),
    CY("=Race controls: ", "=Switch: A accelerate, B brake, ZR or R drift, ZL, L or Y nitro, X camera, "
       "D-pad up respawn, + pause. SHIELD: the game's own NVIDIA SHIELD layout, as its controller card "
       "shows. Menus work the same either way.", "controls", "race_scheme", F_TXT),
    B("=Confirm with the bottom button", "=Off: A confirms and B goes back, by their labels. On: the bottom "
      "button confirms, as on an Android controller.", "controls", "swap_a_b"),
    B("=Touchscreen", "=The handheld touchscreen works as the phone's screen.", "controls", "touchscreen"),
    B("=Profile loading (debug)", "=Writes what the game's threads do while it loads, and in short samples "
      "during play, to debug.log -- for bug reports. Costs a little speed.", "debug", "profile_loading"),
    CY("=Screen capture: ", "=Saves the game's picture as capture-NNN.bmp in the game folder this often, "
       "for bug reports.", "debug", "capture_every_seconds", F_SECOFF),
    B("=Start-up log on screen (debug)", "=Shows the start-up log on screen at every launch.", "debug",
      "boot_log_on_screen"),
    B("=Log Java calls (debug)", "=Writes every Java method the game calls to debug.log. Slow; only for bug "
      "reports.", "debug", "log_java_calls"),
    B("=Graphics self-test (debug)", "=Shows a test picture at start-up.", "debug", "gl_selftest"),
};
#define NPAGES 4
static const struct {
  const Row *rows;
  int n;
  const char *name;
} k_pages[NPAGES] = {
    {k_page0, sizeof k_page0 / sizeof k_page0[0], "saveload_options_start_screen"},
    {k_page1, sizeof k_page1 / sizeof k_page1[0], "saveload_options_misc"},
    {k_page2, sizeof k_page2 / sizeof k_page2[0], "saveload_options_graphics"},
    {k_page3, sizeof k_page3 / sizeof k_page3[0], "=SWITCH"},
};

/* a string resource, or ("=...") the text itself */
static const char *txt(const char *s) { return s && s[0] == '=' ? s + 1 : ST(s); }
static int g_dirty;

/* "Traffic: X" -> "Traffic: Default" (the mod's update*Text); arrows: "Traffic: <  Default  >" */
static void row_label(const Row *r, char *out, size_t cap, int arrows) {
  const char *l = txt(r->label);
  const char *colon = strstr(l, ": ");
  if (!colon || !r->key) {
    snprintf(out, cap, "%s", l);
    return;
  }
  const char *v = dcr_config_get(r->sec, r->key);
  char val[64];
  if (r->fmt == F_WORD) {
    const char *name = !strcasecmp(v, "default") ? "saveload_options_dynamic_title2"
                       : !strcasecmp(v, "all")   ? "saveload_options_dynamic_title3"
                       : !strcasecmp(v, "light") ? "saveload_options_dynamic_title4"
                       : !strcasecmp(v, "heavy") ? "saveload_options_dynamic_title5"
                       : !strcasecmp(v, "trilinear") ? "saveload_options_dynamic_title6"
                       : !strcasecmp(v, "bilinear")  ? "saveload_options_dynamic_title7"
                       : !strcmp(r->key, "traffic")  ? "saveload_options_dynamic_title"
                                                     : "saveload_options_dynamic_title8";
    snprintf(val, sizeof val, "%s", ST(name));
  } else if (r->fmt == F_TXT) {
    static const char *const k_words[][2] = {{"switch", "Switch"}, {"shield", "SHIELD"}, {"auto", "Auto"},
                                             {"720", "720p"},      {"1080", "1080p"}};
    snprintf(val, sizeof val, "%s", v);
    for (unsigned i = 0; i < sizeof k_words / sizeof k_words[0]; i++)
      if (!strcasecmp(v, k_words[i][0]))
        snprintf(val, sizeof val, "%s", k_words[i][1]);
  } else if (r->fmt == F_SECOFF && !strcmp(v, "0")) {
    snprintf(val, sizeof val, "Off");
  } else if (r->fmt == F_MHZ && !strcasecmp(v, "system")) {
    snprintf(val, sizeof val, "System");
  } else {
    snprintf(val, sizeof val, "%s%s", v, r->fmt == F_PCT ? "%" : r->fmt == F_X ? "x"
                                         : r->fmt == F_SEC || r->fmt == F_SECOFF ? " s"
                                         : r->fmt == F_MHZ ? " MHz" : "");
  }
  snprintf(out, cap, arrows ? "%.*s:  <  %s  >" : "%.*s: %s", (int)(colon - l), l, val);
}

static int row_checked(const Row *r) {
  if (r->kind == RK_ALWAYS)
    return 1;
  return r->kind == RK_BOOL && !strcmp(dcr_config_get(r->sec, r->key), "true");
}

static void cycle_by(const Row *r, int step) {
  const char *ch = dcr_config_choices(r->sec, r->key), *v = dcr_config_get(r->sec, r->key);
  if (!ch)
    return;
  char list[160];
  snprintf(list, sizeof list, "%s", ch);
  char *vals[24];
  int n = 0;
  for (char *t = strtok(list, ","); t && n < 24; t = strtok(NULL, ","))
    vals[n++] = t;
  int i = 0;
  while (i < n && strcasecmp(vals[i], v))
    i++;
  if (i >= n)
    i = step > 0 ? n - 1 : 0; /* not one of them: from the first */
  dcr_config_set(r->sec, r->key, vals[(i + step + n) % n]);
  g_dirty = 1;
}

static int g_edit; /* the value row being changed with left / right (its id), or 0 */

static void save_if_dirty(void) {
  if (g_dirty && dcr_config_save() == 0)
    g_dirty = 0;
}

/* ================================================================ dialogs */
enum { IT_END, IT_GAP, IT_TEXT, IT_IMAGE, IT_CENTER = 0x100 };
typedef struct {
  int kind;
  const char *str; /* a string resource; "=text": the text itself */
  int size;        /* sp (IT_GAP: dp) */
  uint32_t col;
} Item;

#include "a8r_menu_texts.h" /* k_whatsnew, k_help, k_about: the mod's layouts, as lists */

enum {
  DLG_NONE, DLG_WHATSNEW, DLG_HELP, DLG_ABOUT, DLG_OPTIONS, DLG_QUICK, DLG_LANGUAGE,
  DLG_BACKUP, DLG_RESTORE, DLG_RESET, DLG_EXIT, DLG_SONGS,
};
static int g_stack[4], g_depth; /* open dialogs, innermost last */
static int g_page;              /* options page */
static float g_scroll, g_content_h, g_view_y, g_view_h;
/* Smooth scrolling: every scroll sets where the list is going (g_scroll_to);
 * each frame it glides a fraction of the way there. A finger drags the list
 * 1:1 and, let go, it coasts on (g_fling, px per frame) and slows down. */
static float g_scroll_to, g_fling;

static void scroll_reset(void) { g_scroll = g_scroll_to = g_fling = 0; }

static void scroll_step(int touching) {
  if (!touching && (g_fling > 0.3f || g_fling < -0.3f)) {
    g_scroll_to += g_fling;
    g_fling *= 0.94f;
  } else if (!touching) {
    g_fling = 0;
  }
  float d = g_scroll_to - g_scroll;
  g_scroll = d > -0.5f && d < 0.5f ? g_scroll_to : g_scroll + d * 0.22f;
}
static int g_result = -1;       /* 1 play, 0 exit */

enum { ID_CLOSE = 100, ID_NO, ID_YES, ID_PREV, ID_NEXT, ID_TAB = 120, ID_ROW = 200, ID_FLAG = 400 };
enum { ID_OPTIONS = 1, ID_ABOUT, ID_HELP, ID_BACKUP, ID_RESTORE, ID_RESET, ID_WHATSNEW, ID_PLAY };

static int top_dialog(void) { return g_depth ? g_stack[g_depth - 1] : DLG_NONE; }

static void open_dialog(int d, int sfx) {
  if (g_depth < 4)
    g_stack[g_depth++] = d;
  g_edit = 0;
  scroll_reset();
  g_focus = d == DLG_OPTIONS || d == DLG_QUICK ? ID_ROW : d == DLG_LANGUAGE ? ID_FLAG
          : d >= DLG_BACKUP ? ID_NO : ID_CLOSE;
  if (sfx) {
    menu_audio_sfx(SFX_CONFIRM);
    menu_audio_sfx(SFX_WOOSH);
  }
}

static void close_dialog(void) {
  int d = top_dialog();
  if (g_depth)
    g_depth--;
  g_edit = 0;
  menu_audio_sfx(SFX_BACK);
  save_if_dirty();
  scroll_reset();
  /* focus back on what opened it */
  g_focus = !g_depth ? (d == DLG_WHATSNEW ? ID_WHATSNEW : d == DLG_HELP ? ID_HELP : d == DLG_ABOUT ? ID_ABOUT
                        : d == DLG_OPTIONS ? ID_OPTIONS : d == DLG_BACKUP ? ID_BACKUP
                        : d == DLG_RESTORE ? ID_RESTORE : d == DLG_RESET ? ID_RESET : ID_PLAY)
                     : ID_ROW;
}

static void nickname(void) {
  SwkbdConfig kbd;
  char out[64] = "";
  if (R_FAILED(swkbdCreate(&kbd, 0)))
    return;
  swkbdConfigMakePresetDefault(&kbd);
  swkbdConfigSetHeaderText(&kbd, ST("saveload_nickname_message"));
  swkbdConfigSetSubText(&kbd, ST("saveload_nickname_message3"));
  swkbdConfigSetInitialText(&kbd, dcr_config_get("mod", "nickname"));
  swkbdConfigSetStringLenMax(&kbd, 16);
  Result rc = swkbdShow(&kbd, out, sizeof out);
  swkbdClose(&kbd);
  if (R_FAILED(rc))
    return;
  char nick[17];
  int n = 0;
  for (const char *p = out; *p && n < 16; p++) /* A-Z, a-z, 0-9 (the mod's rule) */
    if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9'))
      nick[n++] = *p;
  nick[n] = 0;
  if (!n) {
    menu_audio_sfx(SFX_DENIED);
    return;
  }
  dcr_config_set("mod", "nickname", nick);
  g_dirty = 1;
  save_if_dirty();
  char msg[96];
  const char *t = ST("saveload_toast5"), *colon = strstr(t, ": ");
  snprintf(msg, sizeof msg, "%.*s: %s", colon ? (int)(colon - t) : (int)strlen(t), t, nick);
  menu_audio_sfx(SFX_CONFIRM);
  toast(msg);
}

/* a value row's next (step 1) or previous (-1) value */
static void change_value(const Row *r, int step) {
  cycle_by(r, step);
  menu_audio_sfx(SFX_CONFIRM);
  if (!strcmp(r->key, "menu_size")) {
    save_if_dirty();
    g_m = dcr_config()->menu_size / 100.0f;
  }
}

static void row_press(const Row *r) {
  switch (r->kind) {
  case RK_BOOL:
    dcr_config_set(r->sec, r->key, row_checked(r) ? "false" : "true");
    g_dirty = 1;
    menu_audio_sfx(SFX_CONFIRM);
    if (!strcmp(r->key, "sound_effects") || !strcmp(r->key, "music")) {
      save_if_dirty();
      menu_audio_set(dcr_config()->ss_effects, dcr_config()->ss_music);
    }
    break;
  case RK_CYCLE:
    change_value(r, 1);
    break;
  case RK_NICK:
    menu_audio_sfx(SFX_CONFIRM);
    nickname();
    break;
  case RK_LANG:
    open_dialog(DLG_LANGUAGE, 1);
    break;
  case RK_SONGS:
    open_dialog(DLG_SONGS, 1);
    break;
  case RK_QUICK:
    open_dialog(DLG_QUICK, 1);
    break;
  default:
    menu_audio_sfx(SFX_DENIED);
  }
}

/* the frame every dialog has: background, title bar, close button */
static void dialog_frame(const char *title) {
  float W = ui_w();
  ui_rect(0, 0, W, ui_h(), C_DIALOG);
  ui_rect(0, 0, W, dp(35), C_WHITE);
  text_in(title, sp(18), dp(15), 0, W, dp(35), C_BLUE, 1, 0);
  int f = g_focus == ID_CLOSE;
  float x = W - dp(60);
  ui_vgrad(x, 0, dp(60), dp(35), f ? C_RED_F_TOP : C_RED_TOP, f ? C_RED_F_BOTTOM : C_RED_BOTTOM);
  ui_image(&g_im[IM_CLOSE], x, 0, dp(60), dp(35), C_WHITE);
  hot(ID_CLOSE, x, 0, dp(60), dp(35));
}

static void begin_scroll(float top, float bottom) {
  g_view_y = top;
  g_view_h = ui_h() - top - bottom;
  float max = g_content_h - g_view_h;
  if (max < 0)
    max = 0;
  if (g_scroll_to > max)
    g_scroll_to = max, g_fling = 0;
  if (g_scroll_to < 0)
    g_scroll_to = 0, g_fling = 0;
  if (g_scroll > max)
    g_scroll = max;
  if (g_scroll < 0)
    g_scroll = 0;
  ui_clip(0, g_view_y, ui_w(), g_view_h);
}

static void end_scroll(float content_h) {
  ui_unclip();
  g_content_h = content_h;
  if (content_h > g_view_h) { /* a scroll bar */
    float track = g_view_h, bar = track * g_view_h / content_h;
    float pos = (track - bar) * g_scroll / (content_h - g_view_h);
    ui_rect(ui_w() - dp(6), g_view_y + pos, dp(3), bar, 0x80FFFFFFu);
  }
}

static void draw_items(const Item *it) {
  float x = dp(15), w = ui_w() - dp(30), y = g_view_y - g_scroll;
  for (; it->kind != IT_END; it++) {
    int k = it->kind & 0xFF, centre = (it->kind & IT_CENTER) != 0;
    if (k == IT_GAP) {
      y += dp(it->size);
    } else if (k == IT_IMAGE) {
      float h = dp(70), iw = h * g_im[IM_TITLE].w / (g_im[IM_TITLE].h ? g_im[IM_TITLE].h : 1);
      ui_image(&g_im[IM_TITLE], x + (w - iw) / 2, y, iw, h, C_WHITE);
      y += h;
    } else {
      const char *s = it->str[0] == '=' ? it->str + 1 : ST(it->str);
      int vis = y < g_view_y + g_view_h && y + dp(400) > g_view_y;
      y += text_block(s, sp(it->size), x, y, w, it->col, centre, vis);
    }
  }
  end_scroll(y + g_scroll - g_view_y);
}

/* a list of option rows; the focused one kept in view */
static void draw_rows(const Row *rows, int n, int with_desc) {
  float x = dp(15), w = ui_w() - dp(30), y = g_view_y - g_scroll;
  for (int i = 0; i < n; i++) {
    const Row *r = &rows[i];
    int id = ID_ROW + i, f = g_focus == id, e = f && g_edit == id;
    if (e)
      ui_vgrad(0, y, ui_w(), dp(35), C_FOCUS_TOP, C_FOCUS_BOTTOM); /* being changed */
    else if (f)
      ui_rect(0, y, ui_w(), dp(35), 0x1FFFFFFFu); /* the focused row, for the controller */
    static const int k_icon[] = {IM_CHECKED, IM_SELECT, IM_PROFILE, IM_DELETE, IM_LANGUAGE, IM_QUICK};
    int im = r->icon == IC_CHECK ? (row_checked(r) ? IM_CHECKED : IM_UNCHECKED) : k_icon[r->icon];
    ui_image(&g_im[im + f], x + dp(15), y + dp(7.5f), dp(20), dp(20), C_WHITE);
    char label[160];
    row_label(r, label, sizeof label, e);
    text_in(label, sp(18), x + dp(50), y, w, dp(35), e ? C_WHITE : C_ORANGE, 0, 0);
    if (f && r->kind == RK_CYCLE) { /* what the buttons do here, at the right */
      char hint[64];
      const char *a = g_confirm_key == HidNpadButton_A ? "A" : "B";
      if (e)
        snprintf(hint, sizeof hint, "LEFT / RIGHT: CHANGE     %s: DONE", a);
      else
        snprintf(hint, sizeof hint, "%s: CHANGE", a);
      float hw = ui_text_w(sp(14), hint, -1, SPACING);
      text_in(hint, sp(14), x + w - hw - dp(5), y, hw, dp(35), e ? C_WHITE : 0xB0FFFFFFu, 0, 0);
    }
    hot(id, 0, y, ui_w(), dp(35));
    if (g_nhot && (y + dp(35) <= g_view_y || y >= g_view_y + g_view_h))
      g_hot[g_nhot - 1].hidden = 1;
    y += dp(35);
    if (with_desc && r->desc) {
      y += dp(5);
      y += text_block(txt(r->desc), sp(16), x + dp(15), y, w - dp(15), C_WHITE, 0,
                      y < g_view_y + g_view_h && y + dp(200) > g_view_y);
      y += dp(15);
    } else {
      y += dp(5);
    }
  }
  end_scroll(y + g_scroll - g_view_y);
}

/* scroll so that the focused row is in view */
static void keep_in_view(void) {
  const Hot *h = hot_get(g_focus);
  if (!h || g_focus < ID_ROW || g_focus >= ID_FLAG)
    return;
  float top = h->y - g_view_y + g_scroll, bottom = top + h->h, margin = dp(10);
  if (top - margin < g_scroll_to)
    g_scroll_to = top - margin;
  else if (bottom + margin > g_scroll_to + g_view_h)
    g_scroll_to = bottom + margin - g_view_h;
  g_fling = 0;
}

static void two_buttons(const char *no, const char *yes) {
  float y = ui_h() - dp(15) - dp(33);
  text_button(ID_NO, no, sp(18), dp(15), y, dp(135), dp(33));
  text_button(ID_YES, yes, sp(18), ui_w() - dp(15) - dp(135), y, dp(135), dp(33));
}

static void draw_dialog(int d) {
  switch (d) {
  case DLG_WHATSNEW:
  case DLG_HELP:
  case DLG_ABOUT:
    dialog_frame(ST(d == DLG_WHATSNEW ? "saveload_whats_new" : d == DLG_HELP ? "saveload_help" : "saveload_about"));
    begin_scroll(dp(50), dp(15));
    draw_items(d == DLG_WHATSNEW ? k_whatsnew : d == DLG_HELP ? k_help : k_about);
    break;
  case DLG_OPTIONS: {
    /* The pages as tabs under the title, always in view: L / R or left /
     * right turn them (a value row takes left / right only after A), up
     * from the list reaches them, a tap picks one. */
    dialog_frame(ST("saveload_options"));
    float W = ui_w(), ty = dp(35), th = dp(35), hw = dp(40);
    ui_rect(0, ty, W, th, 0xFF1B1E24u);
    float tw = (W - 2 * hw) / NPAGES;
    for (int i = 0; i < NPAGES; i++) {
      float x = hw + i * tw;
      int cur = i == g_page, f = g_focus == ID_TAB + i;
      if (cur)
        ui_vgrad(x, ty, tw, th, C_FOCUS_TOP, C_FOCUS_BOTTOM);
      else if (f)
        ui_rect(x, ty, tw, th, 0x40FFFFFFu);
      if (f && cur)
        ui_rect(x, ty + th - dp(3), tw, dp(3), C_WHITE);
      text_in(txt(k_pages[i].name), sp(16), x, ty, tw, th, cur ? C_WHITE : 0xFFB8BEC8u, 1, 1);
      hot(ID_TAB + i, x, ty, tw, th);
    }
    /* the shoulder buttons that turn them */
    static const char *const k_lr[2] = {"L", "R"};
    for (int i = 0; i < 2; i++) {
      float x = i ? W - hw : 0, s = dp(24);
      ui_rect(x + (hw - s) / 2, ty + (th - s) / 2, s, s, 0xFF3A3F48u);
      text_in(k_lr[i], sp(16), x, ty, hw, th, C_WHITE, 0, 1);
      hot(i ? ID_NEXT : ID_PREV, x, ty, hw, th);
    }
    begin_scroll(ty + th + dp(8), dp(8));
    draw_rows(k_pages[g_page].rows, k_pages[g_page].n, 1);
    break;
  }
  case DLG_QUICK: {
    dialog_frame(ST("saveload_options2_title11"));
    begin_scroll(dp(50), dp(68));
    float y0 = g_view_y - g_scroll;
    float h = text_block(ST("saveload_whats_new_title7_message"), sp(16), dp(15), y0, ui_w() - dp(30), C_WHITE, 0, 1);
    g_view_y += h + dp(15), g_view_h -= h + dp(15);
    draw_rows(k_quick, 4, 0);
    two_buttons(ST("saveload_options_dynamic_title2"), ST("saveload_ok"));
    break;
  }
  case DLG_LANGUAGE: {
    dialog_frame(ST("saveload_options_title7"));
    float cw = dp(72), ch = dp(50), gap = dp(10);
    float gx = (ui_w() - (3 * cw + 2 * gap)) / 2, gy = dp(70);
    for (int i = 0; i < 7; i++) {
      float x = gx + (i % 3) * (cw + gap), y = gy + (i / 3) * (ch + gap);
      int f = g_focus == ID_FLAG + i || dcr_config()->ss_lang == i;
      ui_image(&g_im[IM_FLAG + 2 * i + f], x, y, cw, ch, C_WHITE);
      if (g_focus == ID_FLAG + i)
        ui_rect(x, y + ch - dp(3), cw, dp(3), C_ORANGE);
      hot(ID_FLAG + i, x, y, cw, ch);
    }
    break;
  }
  default: { /* the questions */
    static const char *const k_q[][2] = {
        [DLG_BACKUP] = {"saveload_backup", "saveload_backup_message"},
        [DLG_RESTORE] = {"saveload_restore", "saveload_restore_message"},
        [DLG_RESET] = {"saveload_reset", "saveload_reset_message"},
        [DLG_EXIT] = {"saveload_exit", "saveload_exit_message"},
        [DLG_SONGS] = {"saveload_remove_songs", "saveload_remove_songs_message"},
    };
    dialog_frame(ST(k_q[d][0]));
    begin_scroll(dp(50), dp(60));
    float y = g_view_y - g_scroll;
    y += text_block(ST(k_q[d][1]), sp(16), dp(15), y, ui_w() - dp(30), C_WHITE, 0, 1);
    end_scroll(y + g_scroll - g_view_y);
    two_buttons(ST("saveload_no"), ST("saveload_yes"));
  }
  }
}

/* the options' next / previous page; the focus stays on the tabs if it is there */
static void options_page(int step) {
  int on_tab = g_focus >= ID_TAB && g_focus < ID_TAB + NPAGES;
  g_page = (g_page + step + NPAGES) % NPAGES;
  g_edit = 0;
  scroll_reset();
  g_focus = on_tab ? ID_TAB + g_page : ID_ROW;
  menu_audio_sfx(SFX_WOOSH);
}

static void dialog_press(int d, int id) {
  if (id == ID_CLOSE) {
    close_dialog();
    return;
  }
  switch (d) {
  case DLG_OPTIONS:
    if (id == ID_PREV || id == ID_NEXT) {
      options_page(id == ID_PREV ? -1 : 1);
    } else if (id >= ID_TAB && id < ID_TAB + NPAGES) {
      if (id - ID_TAB == g_page) { /* the page shown: into its list */
        g_focus = ID_ROW;
      } else {
        g_page = id - ID_TAB;
        scroll_reset();
        menu_audio_sfx(SFX_WOOSH);
      }
    } else if (id >= ID_ROW && id < ID_ROW + k_pages[g_page].n) {
      row_press(&k_pages[g_page].rows[id - ID_ROW]);
    }
    break;
  case DLG_QUICK:
    if (id >= ID_ROW && id < ID_ROW + 4) {
      row_press(&k_quick[id - ID_ROW]);
    } else if (id == ID_NO) { /* Default */
      dcr_config_set("quick_race", "racers", "8");
      dcr_config_set("quick_race", "laps", "2");
      dcr_config_set("quick_race", "elimination_time", "30");
      dcr_config_set("quick_race", "knockdown_limit", "25");
      g_dirty = 1;
      menu_audio_sfx(SFX_CONFIRM);
    } else if (id == ID_YES) {
      close_dialog();
    }
    break;
  case DLG_LANGUAGE:
    if (id >= ID_FLAG && id < ID_FLAG + 7) {
      dcr_config_set("start_screen", "language", k_lang[id - ID_FLAG]);
      g_dirty = 1;
      save_if_dirty();
      a8r_arsc_set_lang(k_lang[id - ID_FLAG]);
      close_dialog();
    }
    break;
  case DLG_BACKUP:
  case DLG_RESTORE:
  case DLG_RESET:
  case DLG_EXIT:
  case DLG_SONGS:
    if (id == ID_NO) {
      close_dialog();
    } else if (id == ID_YES) {
      g_depth--; /* the question goes; what it asked about happens */
      if (d == DLG_EXIT) {
        g_result = 0;
      } else if (d == DLG_BACKUP) {
        a8r_backup_progress();
        menu_audio_sfx(SFX_CONFIRM);
        toast(ST("saveload_toast"));
        g_focus = ID_BACKUP;
      } else if (d == DLG_RESTORE) {
        int n = restore_progress();
        menu_audio_sfx(n ? SFX_CONFIRM : SFX_DENIED);
        if (n)
          toast(ST("saveload_toast2"));
        g_focus = ID_RESTORE;
      } else if (d == DLG_RESET) {
        reset_progress();
        menu_audio_sfx(SFX_CONFIRM);
        toast(ST("saveload_toast3"));
        g_focus = ID_RESET;
      } else {
        remove_songs();
        menu_audio_sfx(SFX_CONFIRM);
        toast(ST("saveload_toast4"));
        g_focus = ID_ROW + 7;
      }
    }
    break;
  default:
    break;
  }
}

/* ================================================================ the main screen */
/* The mod plays a video behind this screen (assets/saveload_background.mp4),
 * which this port cannot decode. In its place a picture drifting slowly: the
 * user's own, if the game folder has a background.jpg / .png (any size; it
 * is cropped to fill the screen), else the game's own key art from A8R.apk
 * (the data downloader's background) -- nothing of the game's is in this
 * program. Darkened towards the buttons, as the video is. */
static UiImage g_bg;

/* A box blur along one line of n pixels, pixel stride ps floats (RGB), the
 * ends repeated. */
static void box_line(const float *in, float *out, int n, int ps, int r) {
  float inv = 1.0f / (float)(2 * r + 1);
  for (int c = 0; c < 3; c++) {
    float sum = 0;
    for (int k = -r; k <= r; k++)
      sum += in[(k < 0 ? 0 : k >= n ? n - 1 : k) * ps + c];
    for (int i = 0; i < n; i++) {
      out[i * ps + c] = sum * inv;
      int a = i + r + 1, b = i - r;
      sum += in[(a >= n ? n - 1 : a) * ps + c] - in[(b < 0 ? 0 : b) * ps + c];
    }
  }
}

/* The key art has the game's logo at the bottom right (x 395-775, y 328-405
 * of its 800 x 480), right behind WHAT'S NEW and PLAY. Blurred into the road
 * and darkened, in the picture's own pixels before it is uploaded: a smooth
 * mask (a rounded box, feathered with smoothstep) that drifts with the
 * picture -- no overlay bands to show as lines. */
static void hide_keyart_logo(uint8_t *px, int w, int h) {
  float sx = (float)w / 800.0f, sy = (float)h / 480.0f;
  float cx = 585 * sx, cy = 367 * sy, hx = 200 * sx, hy = 46 * sy, fe = 46 * sx;
  int r = (int)(20 * sx + 0.5f);
  if (r < 1)
    r = 1;
  int y0 = (int)(cy - hy - fe) - 3 * r, y1 = (int)(cy + hy + fe) + 3 * r + 1; /* the rows it reaches */
  y0 = y0 < 0 ? 0 : y0;
  y1 = y1 > h ? h : y1;
  int bh = y1 - y0, n = w > bh ? w : bh;
  if (bh <= 0)
    return;
  float *b = malloc(sizeof(float) * 3 * (size_t)w * bh), *lin = malloc(sizeof(float) * 3 * n),
        *lout = malloc(sizeof(float) * 3 * n);
  if (!b || !lin || !lout) {
    free(b), free(lin), free(lout);
    return;
  }
  for (int y = 0; y < bh; y++)
    for (int x = 0; x < w; x++)
      for (int c = 0; c < 3; c++)
        b[(y * w + x) * 3 + c] = px[((y0 + y) * w + x) * 4 + c];
  for (int pass = 0; pass < 3; pass++) { /* three boxes: about a Gaussian */
    for (int y = 0; y < bh; y++) {
      memcpy(lin, &b[y * w * 3], sizeof(float) * 3 * w);
      box_line(lin, &b[y * w * 3], w, 3, r);
    }
    for (int x = 0; x < w; x++) {
      for (int y = 0; y < bh; y++)
        memcpy(&lin[y * 3], &b[(y * w + x) * 3], sizeof(float) * 3);
      box_line(lin, lout, bh, 3, r);
      for (int y = 0; y < bh; y++)
        memcpy(&b[(y * w + x) * 3], &lout[y * 3], sizeof(float) * 3);
    }
  }
  for (int y = 0; y < bh; y++) {
    float dy = fmaxf(fabsf((float)(y0 + y) - cy) - hy, 0);
    for (int x = 0; x < w; x++) {
      float dx = fmaxf(fabsf((float)x - cx) - hx, 0);
      float t = 1.0f - sqrtf(dx * dx + dy * dy) / fe;
      if (t <= 0)
        continue;
      float m = t * t * (3 - 2 * t), keep = 1.0f - 0.7f * m;
      uint8_t *p = &px[((y0 + y) * w + x) * 4];
      for (int c = 0; c < 3; c++) {
        float v = ((float)p[c] + (b[(y * w + x) * 3 + c] - (float)p[c]) * m) * keep;
        p[c] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
      }
    }
  }
  free(b), free(lin), free(lout);
}

static void load_background(void) {
  static const char *const names[] = {"background.jpg", "background.jpeg", "background.png"};
  for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) {
    char p[300];
    root_path(p, sizeof p, names[i]);
    FILE *f = fopen(p, "rb");
    if (!f)
      continue;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = n > 0 && n < (64l << 20) ? malloc((size_t)n) : NULL;
    int ok = d && fread(d, 1, (size_t)n, f) == (size_t)n && ui_image_png(&g_bg, d, (size_t)n) == 0;
    fclose(f);
    free(d);
    debugPrintf("[menu] background: %s%s\n", names[i], ok ? "" : " (could not read it)");
    if (ok)
      return;
  }
  size_t n;
  uint8_t *png = apk_file("res/drawable/data_downloader_background.9.png", &n);
  if (png && ui_image_png_fx(&g_bg, png, n, hide_keyart_logo) == 0) {
    debugPrintf("[menu] background: the key art in A8R.apk (%dx%d)\n", g_bg.w, g_bg.h);
  }
  free(png);
}

static void draw_background(void) {
  float W = ui_w(), H = ui_h();
  if (!g_bg.tex) { /* a dark backdrop in the mod's colours */
    ui_vgrad(0, 0, W, H * 0.6f, 0xFF10141Cu, 0xFF07080Bu);
    ui_vgrad(0, H * 0.6f, W, H * 0.4f, 0xFF07080Bu, 0xFF1A1208u);
  } else {
    /* cover the screen, zoom 104-110% and pan side to side over a minute */
    float t = (float)(now_ms() % 60000u) / 60000.0f * 6.2831853f;
    float k = 1.07f - 0.03f * cosf(t);
    float s = fmaxf(W / (float)g_bg.w, H / (float)g_bg.h) * k;
    float dw = g_bg.w * s, dh = g_bg.h * s;
    float x = (W - dw) / 2 + sinf(t) * (dw - W) * 0.45f, y = (H - dh) / 2 + sinf(t * 0.5f) * (dh - H) * 0.3f;
    ui_image(&g_bg, x, y, dw, dh, C_WHITE);
    ui_rect(0, 0, W, H, 0x40000000u);                                  /* a little darker overall */
    ui_hgrad(0, 0, W * 0.45f, H, 0xC8000000u, 0x00000000u);            /* the buttons at the left */
    ui_vgrad(0, 0, W, H * 0.25f, 0x70000000u, 0x00000000u);            /* the title and icons */
    ui_vgrad(0, H * 0.5f, W, H * 0.5f, 0x00000000u, 0xE0000000u);      /* PLAY, WHAT'S NEW */
  }
  ui_vgrad(0, H - dp(6), W, dp(6), 0x00F99807u, 0x60F99807u);
}

static void draw_main(void) {
  float W = ui_w(), H = ui_h(), m = g_m;
  draw_background();
  /* the title: a 320 x 180 dp frame at the top right, 30 dp down, 45 dp in */
  if (g_im[IM_TITLE].tex) {
    float maxw = dp(275) * m, maxh = dp(150) * m;
    float iw = maxw, ih = iw * g_im[IM_TITLE].h / g_im[IM_TITLE].w;
    if (ih > maxh)
      ih = maxh, iw = ih * g_im[IM_TITLE].w / g_im[IM_TITLE].h;
    ui_image(&g_im[IM_TITLE], W - dp(45) * m - iw, dp(30) * m, iw, ih, C_WHITE);
  }
  float s = dp(35) * m;
  icon_button(ID_OPTIONS, IM_OPTIONS, dp(45) * m, dp(30) * m, s);
  icon_button(ID_ABOUT, IM_ABOUT, dp(95) * m, dp(30) * m, s);
  icon_button(ID_HELP, IM_HELP, dp(145) * m, dp(30) * m, s);
  float by = H - dp(30) * m - s;
  icon_button(ID_BACKUP, IM_BACKUP, dp(45) * m, by, s);
  icon_button(ID_RESTORE, IM_RESTORE, dp(95) * m, by, s);
  icon_button(ID_RESET, IM_RESET, dp(145) * m, by, s);
  /* AUTO: the auto backup's state (not a button) */
  float ay = H - dp(80) * m - dp(35) * m;
  ui_rect(dp(45) * m, ay, dp(135) * m, dp(35) * m, C_BLUE);
  text_in(ST(dcr_config()->auto_backup ? "saveload_autobackup_notify_on" : "saveload_autobackup_notify_off"),
          sp(18) * m, dp(45) * m, ay, dp(135) * m, dp(35) * m, C_WHITE, 1, 1);
  float bw = dp(205) * m, bh = dp(50) * m, bx = W - dp(45) * m - bw;
  text_button(ID_WHATSNEW, ST("saveload_whats_new"), sp(24) * m, bx, H - dp(95) * m - bh, bw, bh);
  text_button(ID_PLAY, ST("saveload_play"), sp(24) * m, bx, H - dp(30) * m - bh, bw, bh);
}

static void main_press(int id) {
  switch (id) {
  case ID_PLAY:
    menu_audio_sfx(SFX_CONFIRM);
    g_result = 1;
    break;
  case ID_WHATSNEW: open_dialog(DLG_WHATSNEW, 1); break;
  case ID_OPTIONS:
    g_page = 0;
    open_dialog(DLG_OPTIONS, 1);
    break;
  case ID_ABOUT: open_dialog(DLG_ABOUT, 1); break;
  case ID_HELP: open_dialog(DLG_HELP, 1); break;
  case ID_BACKUP: open_dialog(DLG_BACKUP, 1); break;
  case ID_RESTORE: open_dialog(DLG_RESTORE, 1); break;
  case ID_RESET: open_dialog(DLG_RESET, 1); break;
  }
}

/* ================================================================ the loop */
static uint8_t *g_sfx_mp3[SFX_COUNT];
static size_t g_sfx_len[SFX_COUNT];

static uint8_t *random_track(size_t *len) {
  static const char *const k_music[] = {"m_dj_gontran_chemistry.mp3", "m_dj_gontran_moby_glitch.mp3",
                                        "m_dj_gontran_phantasmagorical.mp3", "m_krubb_wenkroist_bleach.mp3",
                                        "m_dj_dubai_vodka_aspirin.mp3"};
  static int last = -1;
  int i = (int)(armGetSystemTick() % 5);
  if (i == last)
    i = (i + 1) % 5;
  last = i;
  char path[80];
  snprintf(path, sizeof path, "assets/%s", k_music[i]);
  return apk_file(path, len);
}

/* Test aid: with a file ".menu_tour" in the game folder, every screen is shown
 * in turn and saved as capture-NNN.bmp there, then PLAY (the hand-over of the
 * screen to the game included). Third column: 1 opened from the options, 2
 * the page's first value row being changed. */
static int g_tour = -1;
static u64 g_tour_at;
static const int k_tour[][3] = {
    {DLG_NONE, 0, 0},    {DLG_WHATSNEW, 0, 0}, {DLG_OPTIONS, 0, 0}, {DLG_OPTIONS, 1, 0},
    {DLG_OPTIONS, 2, 0}, {DLG_OPTIONS, 3, 0}, {DLG_OPTIONS, 3, 2}, {DLG_QUICK, 0, 1},    {DLG_LANGUAGE, 0, 1}, {DLG_HELP, 0, 0},
    {DLG_ABOUT, 0, 0},   {DLG_BACKUP, 0, 0},   {DLG_EXIT, 0, 0},
};
void dcr_gl_capture_now(void); /* gl_mesa.c */

static int tour_step(void) {
  if (g_tour < 0 || now_ms() < g_tour_at)
    return 0;
  if (g_tour >= (int)(sizeof k_tour / sizeof k_tour[0])) {
    g_tour = -1;
    g_result = 1;
    return 0;
  }
  g_depth = 0;
  scroll_reset();
  if (k_tour[g_tour][2] == 1) { /* opened from the options */
    g_stack[g_depth++] = DLG_OPTIONS;
    g_page = 1;
  }
  if (k_tour[g_tour][0] != DLG_NONE)
    g_stack[g_depth++] = k_tour[g_tour][0];
  if (k_tour[g_tour][0] == DLG_OPTIONS)
    g_page = k_tour[g_tour][1];
  g_focus = 0;
  for (int i = 0; k_tour[g_tour][2] == 2 && i < k_pages[g_page].n; i++)
    if (k_pages[g_page].rows[i].kind == RK_CYCLE) {
      g_focus = g_edit = ID_ROW + i;
      break;
    }
  g_tour++;
  g_tour_at = now_ms() + 1500;
  return 1;
}

/* the value row ("<>") with this id in the dialog shown, or NULL */
static const Row *value_row(int d, int id) {
  const Row *rows = d == DLG_OPTIONS ? k_pages[g_page].rows : d == DLG_QUICK ? k_quick : NULL;
  int n = d == DLG_OPTIONS ? k_pages[g_page].n : d == DLG_QUICK ? 4 : 0, i = id - ID_ROW;
  return rows && i >= 0 && i < n && rows[i].kind == RK_CYCLE ? &rows[i] : NULL;
}

static void frame(void) {
  int capture = tour_step();
  int d = top_dialog();
  scroll_step(g_touch_down);
  g_nhot = 0;
  ui_begin(0xFF000000u);
  if (d == DLG_NONE)
    draw_main();
  else
    draw_dialog(d);
  draw_toast();
  if (capture) {
    ui_flush();
    dcr_gl_capture_now();
  }
  ui_present();

  /* input, against what was just drawn */
  if (!g_focus || !hot_get(g_focus))
    g_focus = g_nhot ? (d == DLG_NONE ? ID_PLAY : g_hot[0].id) : 0;
  int dir = nav_dir();
  const Row *vr = value_row(d, g_focus);
  if (g_edit && (g_edit != g_focus || !vr))
    g_edit = 0;
  if (g_edit && (dir == D_LEFT || dir == D_RIGHT)) { /* changing a value */
    change_value(vr, dir == D_LEFT ? -1 : 1);
    dir = -1;
  } else if (g_edit && dir >= 0) { /* up / down: done with it, on to the next */
    g_edit = 0;
  }
  if (d == DLG_OPTIONS && (dir == D_LEFT || dir == D_RIGHT)) { /* the next or previous page */
    options_page(dir == D_LEFT ? -1 : 1);
    dir = -1;
  }
  if (d == DLG_OPTIONS && dir == D_UP && g_focus >= ID_ROW && g_focus < ID_FLAG) {
    int to = hot_step(g_focus, dir);
    if (to >= ID_TAB && to < ID_TAB + NPAGES) { /* up out of the list: the page's own tab */
      g_focus = ID_TAB + g_page;
      dir = -1;
    }
  }
  if (dir >= 0) {
    int to = hot_step(g_focus, dir);
    if (to) {
      g_focus = to;
      keep_in_view(); /* only when the controller moves: scrolling by hand stays put */
    } else if (d != DLG_NONE && (dir == D_UP || dir == D_DOWN)) {
      g_scroll_to += (dir == D_UP ? -1 : 1) * dp(80);
    }
  }
  if (d != DLG_NONE) {
    HidAnalogStickState r = padGetStickPos(&g_pad, 1);
    if (r.y > 8000 || r.y < -8000)
      g_scroll_to -= (float)r.y / 32767.0f * dp(16);
    if (d == DLG_OPTIONS && (g_down & (HidNpadButton_L | HidNpadButton_R | HidNpadButton_ZL | HidNpadButton_ZR)))
      options_page((g_down & (HidNpadButton_L | HidNpadButton_ZL)) ? -1 : 1);
  }
  static float last_ty, vel;
  static int dragging;
  if (g_touch_down) {
    if (d != DLG_NONE && g_drag > dp(12)) {
      float dy = g_ty - last_ty;
      g_scroll_to -= dy;
      g_scroll = g_scroll_to; /* under the finger */
      vel = vel * 0.5f - dy * 0.5f;
      dragging = 1;
    }
    last_ty = g_ty;
  } else if (dragging) {
    dragging = 0;
    g_fling = vel; /* let go: it coasts */
    vel = 0;
  }
  int pressed = 0;
  if (g_down & g_confirm_key) {
    if (g_edit) { /* done changing it */
      g_edit = 0;
      menu_audio_sfx(SFX_CONFIRM);
    } else if (vr) { /* a value row: left / right change it now */
      g_edit = g_focus;
      menu_audio_sfx(SFX_CONFIRM);
    } else {
      pressed = g_focus;
    }
  }
  if (g_touch_tap) {
    int id = hot_at(g_tx, g_ty);
    g_edit = 0;
    if (id)
      g_focus = pressed = id;
  }
  if (pressed) {
    if (d == DLG_NONE)
      main_press(pressed);
    else
      dialog_press(d, pressed);
  } else if ((g_down & g_back_key) && g_edit) {
    g_edit = 0;
    menu_audio_sfx(SFX_BACK);
  } else if (g_down & g_back_key) {
    if (d == DLG_NONE)
      open_dialog(DLG_EXIT, 1);
    else
      close_dialog();
  } else if ((g_down & HidNpadButton_Plus) && d == DLG_NONE) {
    main_press(ID_PLAY);
  }
}

/* After PLAY the screen stays EGL's (the console cannot have the window back)
 * until the setup is done: a8r_menu_progress draws its progress, and
 * a8r_menu_done gives the window up for the game. */
static int g_kept;
static int g_toured; /* the test tour ran (.menu_tour): each progress step is captured too */
static uint8_t *g_font_main, *g_font_fallback;

void a8r_menu_done(void) {
  if (!g_kept)
    return;
  g_kept = 0;
  for (int i = 0; i < IM_COUNT; i++)
    ui_image_free(&g_im[i]);
  ui_image_free(&g_bg);
  ui_close();
  free(g_font_main), g_font_main = NULL;
  free(g_font_fallback), g_font_fallback = NULL;
}

/* The console's progress screen (util.c, log_console_progress), drawn with
 * the start screen's renderer: the game's name in green, why, a green bar
 * and the step. Redrawn only when the step or the whole percent changes. */
void a8r_menu_progress(const char *what, int permille) {
  static char last[96];
  static int last_pct = -1;
  if (!g_kept)
    return;
  permille = permille < 0 ? 0 : permille > 1000 ? 1000 : permille;
  int pct = permille / 10;
  if (pct == last_pct && !strncmp(what, last, sizeof last - 1))
    return;
  last_pct = pct;
  snprintf(last, sizeof last, "%s", what);
  const uint32_t green = 0xFF4CE05Au, track = 0xFF2A2D33u;
  float W = ui_w(), H = ui_h();
  ui_begin(0xFF000000u);
  text_in("Asphalt 8: Airborne Retry", sp(24), 0, H * 0.36f, W, sp(30), green, 0, 1);
  text_in("Getting the game ready (after an install or an update)", sp(16), 0, H * 0.43f, W, sp(22),
          C_WHITE, 0, 1);
  float bw = W * 0.56f, bh = dp(14), bx = (W - bw) / 2 - dp(24), by = H * 0.52f;
  ui_rect(bx, by, bw, bh, track);
  ui_rect(bx, by, bw * permille / 1000.0f, bh, green);
  char p[8];
  snprintf(p, sizeof p, "%d%%", pct);
  text_in(p, sp(16), bx + bw + dp(12), by - dp(4), dp(60), bh + dp(8), C_WHITE, 0, 0);
  text_in(last, sp(16), 0, H * 0.58f, W, sp(22), 0xFFB8BEC8u, 0, 1);
  static char captured[96];
  if (g_toured && strcmp(captured, last)) {
    void dcr_gl_capture_now(void); /* gl_mesa.c */
    snprintf(captured, sizeof captured, "%s", last);
    ui_flush();
    dcr_gl_capture_now();
  }
  ui_present();
}

int a8r_menu_run(const char *apk) {
  u64 t0 = armGetSystemTick();
  memset(&g_zip, 0, sizeof g_zip);
  g_zip_ok = mz_zip_reader_init_file(&g_zip, apk, 0);
  size_t n;
  uint8_t *arsc = apk_file("resources.arsc", &n);
  if (!arsc || !a8r_arsc_load(arsc, n)) {
    debugPrintf("[menu] no texts in %s: the start screen is skipped\n", apk);
    free(arsc);
    if (g_zip_ok)
      mz_zip_reader_end(&g_zip);
    return 1;
  }
  free(arsc);
  a8r_arsc_set_lang(dcr_config_get("start_screen", "language"));

  if (ui_open() != 0) {
    if (g_zip_ok)
      mz_zip_reader_end(&g_zip);
    return 1;
  }
  S = ui_h() / 440.0f;
  g_m = dcr_config()->menu_size / 100.0f;
  size_t f1, f2;
  uint8_t *accid = apk_file("assets/accid.ttf", &f1), *oswald = apk_file("assets/oswald_regular.ttf", &f2);
  g_font_main = accid, g_font_fallback = oswald;
  const float sizes[UI_FONT_SIZES] = {sp(16), sp(18), sp(24)};
  ui_fonts(accid ? accid : oswald, accid ? oswald : NULL, sizes);
  load_images();
  load_background();

  static const char *const k_sfx[SFX_COUNT] = {"assets/sfx_menu_confirm.mp3", "assets/sfx_menu_back.mp3",
                                               "assets/sfx_menu_denied.mp3", "assets/sfx_menu_woosh.mp3"};
  for (int i = 0; i < SFX_COUNT; i++)
    g_sfx_mp3[i] = apk_file(k_sfx[i], &g_sfx_len[i]);
  menu_audio_start((const uint8_t *const *)g_sfx_mp3, g_sfx_len, random_track, dcr_config()->ss_effects,
                   dcr_config()->ss_music);

  a8r_input_hw_init();
  padInitializeAny(&g_pad);
  g_confirm_key = dcr_config()->swap_ab ? HidNpadButton_B : HidNpadButton_A;
  g_back_key = dcr_config()->swap_ab ? HidNpadButton_A : HidNpadButton_B;
  g_focus = ID_PLAY;
  g_result = -1;
  g_depth = 0;
  debugPrintf("[menu] start screen up in %llu ms (%s)\n",
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull),
              dcr_config_get("start_screen", "language"));

  char tour[300];
  root_path(tour, sizeof tour, ".menu_tour");
  if (access(tour, F_OK) == 0) {
    g_tour = 0;
    g_toured = 1;
    g_tour_at = now_ms() + 1000;
  } else if (!dcr_config()->whats_new_seen) { /* the mod's showWhatsNew: once, by itself */
    open_dialog(DLG_WHATSNEW, 0);
    menu_audio_sfx(SFX_WOOSH);
    dcr_config_set("start_screen", "whats_new_seen", "true");
    g_dirty = 1;
  }
  while (g_result < 0) {
    if (!appletMainLoop()) {
      g_result = 0;
      break;
    }
    poll_input();
    frame();
  }
  save_if_dirty();

  /* the last picture stays up while the game gets ready */
  if (g_result == 1) {
    g_nhot = 0;
    ui_begin(0xFF000000u);
    draw_background();
    if (g_im[IM_TITLE].tex) {
      float iw = dp(320), ih = iw * g_im[IM_TITLE].h / g_im[IM_TITLE].w;
      ui_image(&g_im[IM_TITLE], (ui_w() - iw) / 2, (ui_h() - ih) / 2, iw, ih, C_WHITE);
    }
    ui_present();
    svcSleepThread(250000000ll); /* the PLAY sound */
  }
  menu_audio_stop();
  for (int i = 0; i < SFX_COUNT; i++)
    free(g_sfx_mp3[i]), g_sfx_mp3[i] = NULL;
  g_kept = 1; /* PLAY: the screen stays up for the setup's progress */
  if (g_result != 1)
    a8r_menu_done();
  if (g_zip_ok)
    mz_zip_reader_end(&g_zip);
  g_zip_ok = 0;
  debugPrintf("[menu] %s\n", g_result ? "PLAY" : "exit");
  return g_result;
}
