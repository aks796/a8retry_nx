/* dcr_config.c -- <game folder>/config.ini, the user's settings.
 *
 * Written with every option, its default and a line of explanation on the
 * first start; an existing file is appended to (options a newer build adds,
 * at the end, with their defaults), so edits and comments survive updates.
 * Plain INI: [section], key = value, # comments; booleans take true/false,
 * yes/no, on/off, 1/0. Read once at start-up: changes apply the next time the
 * game starts. (The machinery is the Crossy Road port's.)
 *
 * [mod], [quick_race] and [graphics] are the A8R start screen's OPTIONS
 * button: the same switches, the same defaults (the mod's saveloadOptions), and
 * its own labels in quotes. The start screen itself is not run here: its PLAY
 * button's work (the engine put together with the chosen pieces, the profile
 * and data files edited) is done by a8r_setup.c whenever these change.
 * [performance], [display], [controls] and [debug] are the start screen's
 * fourth options page, SWITCH (a8r_menu.c). MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <switch.h>

#include "dcr_build.h"
#include "dcr_config.h"
#include "util.h"

const char *dcr_game_root(void);        /* main.c */
void dcr_window_set_size(int w, int h); /* android_ndk.c */

static DcrConfig g_cfg;

const DcrConfig *dcr_config(void) { return &g_cfg; }

enum { K_BOOL, K_CHOICE, K_INT, K_TEXT };

typedef struct {
  const char *section, *key, *def, *help;
  int kind;
  const char *choices; /* K_CHOICE: comma-separated, index = value; K_INT: allowed values */
  int *dst;            /* K_BOOL / K_CHOICE / K_INT */
} Opt;

#define M(i) &g_cfg.mod[i]
#define G(i) &g_cfg.gfx[i]

static const Opt k_opts[] = {
    /* ---- the start screen's OPTIONS > MISC ---- */
    {"mod", "retry_difficulty", "true",
     "\"Retry difficulty\": higher difficulty, with tougher bots and drift target scores.",
     K_BOOL, NULL, M(MOD_DIFFICULTY)},
    {"mod", "retry_camera", "true", "\"Retry camera\": the mod's reworked in-race camera.", K_BOOL,
     NULL, M(MOD_CAMERA)},
    {"mod", "knockdown_camera", "true", "\"Knockdown camera\": the slow-motion camera on knockdowns.",
     K_BOOL, NULL, M(MOD_KNOCKDOWN_CAMERA)},
    {"mod", "traffic", "default", "\"Traffic\": default, none or all (traffic in every event).",
     K_CHOICE, "default,none,all", &g_cfg.traffic},
    {"mod", "infinite_gears", "true",
     "\"Infinite gears\": the gear count never runs out (the car's performance is\n"
     "# not affected).",
     K_BOOL, NULL, M(MOD_INFINITE_GEARS)},
    {"mod", "gameplay_hud", "true", "\"Gameplay HUD\": the in-race HUD.", K_BOOL, NULL, M(MOD_HUD)},
    {"mod", "bottom_minimap", "false",
     "\"Bottom minimap\": the minimap at the bottom of the screen (needs the HUD).", K_BOOL, NULL,
     M(MOD_BOTTOM_MINIMAP)},
    {"mod", "multiplayer_cinematic", "false",
     "\"Multiplayer cinematic\": the multiplayer intro before single-player races.", K_BOOL, NULL,
     M(MOD_CINEMATIC)},
    {"mod", "fake_speed", "false",
     "\"Fake speed\": a higher speedometer reading (the car's performance is not\n# affected).",
     K_BOOL, NULL, M(MOD_FAKE_SPEED)},
    {"mod", "attract_mode", "false",
     "\"Attract Mode\": Quick Race becomes Attract Mode, for screenshots and\n"
     "# recordings (- goes on to the results).",
     K_BOOL, NULL, M(MOD_ATTRACT)},
    {"mod", "next_button", "true",
     "\"NEXT button\" on the post-race screen (without it, - goes on to the results).", K_BOOL,
     NULL, M(MOD_NEXT_BUTTON)},
    {"mod", "skip_tutorial", "false", "Skip the car tips shown when the game starts.", K_BOOL, NULL,
     M(MOD_SKIP_TUTORIAL)},
    {"mod", "nickname", "Driver",
     "\"Nickname\": the profile name in the game (A-Z, a-z, 0-9, up to 16).", K_TEXT, NULL, NULL},
    /* ---- Quick Race ---- */
    {"quick_race", "racers", "8", "Racers: 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20, 24, 28 or 32.", K_INT,
     "4,5,6,7,8,9,10,12,14,16,18,20,24,28,32", &g_cfg.racers},
    {"quick_race", "laps", "2", "Laps: 1 to 10, 12, 14, 16, 18 or 20.", K_INT,
     "1,2,3,4,5,6,7,8,9,10,12,14,16,18,20", &g_cfg.laps},
    {"quick_race", "elimination_time", "30", "Elimination time in seconds: 10, 15, 20, 25 or 30.",
     K_INT, "10,15,20,25,30", &g_cfg.elimination_time},
    {"quick_race", "knockdown_limit", "25", "Knockdown limit: 15, 20, 25, 30, 35, 40, 45 or 50.",
     K_INT, "15,20,25,30,35,40,45,50", &g_cfg.knockdown_limit},
    /* ---- the start screen's OPTIONS > GRAPHICS ---- */
    {"graphics", "60fps", "true", "\"60 FPS\": the high frame rate.", K_BOOL, NULL, M(MOD_60FPS)},
    {"graphics", "high_quality", "true", "\"High quality\": the high-quality car models and textures.",
     K_BOOL, NULL, M(MOD_HIGH_QUALITY)},
    {"graphics", "brake_calipers", "false", "\"Brake calipers\" (works partially).", K_BOOL, NULL,
     M(MOD_CALIPERS)},
    {"graphics", "anti_aliasing", "light", "\"Anti-aliasing\": none, light or heavy.", K_CHOICE,
     "none,light,heavy", &g_cfg.aa},
    {"graphics", "anisotropic_filtering", "16", "\"Anisotropic filtering\": 0, 2, 4, 8 or 16.", K_INT,
     "0,2,4,8,16", &g_cfg.anisotropy},
    {"graphics", "texture_filtering", "trilinear", "\"Texture filtering\": trilinear, bilinear or none.",
     K_CHOICE, "trilinear,bilinear,none", &g_cfg.texture_filtering},
    {"graphics", "resolution", "100",
     "\"Resolution\": the game's 3D resolution in percent of the window: 50, 60,\n"
     "# 70, 80, 90 or 100.",
     K_INT, "50,60,70,80,90,100", &g_cfg.resolution_pct},
    {"graphics", "motion_blur", "true", "\"Motion blur\".", K_BOOL, NULL, G(GFX_MOTION_BLUR)},
    {"graphics", "shadows", "true", "\"Shadows\": projected shadows.", K_BOOL, NULL, G(GFX_SHADOWS)},
    {"graphics", "breakables", "true", "\"Breakables\": fences, cones and the like.", K_BOOL, NULL,
     G(GFX_BREAKABLES)},
    {"graphics", "glass_crack", "true", "\"Glass crack effect\" on knockdowns.", K_BOOL, NULL,
     G(GFX_GLASS_CRACK)},
    {"graphics", "particles", "true", "\"Particles\".", K_BOOL, NULL, G(GFX_PARTICLES)},
    {"graphics", "metal_sparks", "false", "\"Metal spark particles\" (works partially; needs particles).",
     K_BOOL, NULL, M(MOD_METAL_SPARKS)},
    {"graphics", "ai_particles", "true", "\"AI particles\": particles from the other racers' cars.",
     K_BOOL, NULL, G(GFX_AI_PARTICLES)},
    {"graphics", "skid_marks", "true", "\"Skid marks\".", K_BOOL, NULL, G(GFX_SKID_MARKS)},
    {"graphics", "road_reflection", "true", "\"Road reflection\" of the surroundings.", K_BOOL, NULL,
     G(GFX_ROAD_REFLECTION)},
    {"graphics", "car_reflection", "true", "\"Car reflection\" of the surroundings.", K_BOOL, NULL,
     G(GFX_CAR_REFLECTION)},
    {"graphics", "road_specular", "true", "\"Road specular\": light reflected by the road.", K_BOOL,
     NULL, G(GFX_ROAD_SPECULAR)},
    {"graphics", "car_specular", "true", "\"Car specular\": light reflected by the cars.", K_BOOL, NULL,
     G(GFX_CAR_SPECULAR)},
    {"graphics", "lens_flare", "true", "\"Lens flare\".", K_BOOL, NULL, G(GFX_LENS_FLARE)},
    {"graphics", "anamorphic_glows", "true", "\"Anamorphic glows\".", K_BOOL, NULL,
     G(GFX_ANAMORPHIC_GLOWS)},
    {"graphics", "car_dirt", "true", "\"Car dirt\".", K_BOOL, NULL, G(GFX_CAR_DIRT)},
    {"graphics", "bokeh", "true", "\"Bokeh\": the camera's bokeh effect.", K_BOOL, NULL, G(GFX_BOKEH)},
    {"graphics", "3d_letters", "true", "\"3D letters\" such as the countdown.", K_BOOL, NULL,
     G(GFX_3D_LETTERS)},
    {"graphics", "classic_license_plate", "false",
     "\"Classic license plate\" of the game's release version.", K_BOOL, NULL, G(GFX_CLASSIC_PLATE)},
    {"graphics", "natural_colors", "false", "\"Natural colors\": an alternative color grading.", K_BOOL,
     NULL, G(GFX_NATURAL_COLORS)},
    {"graphics", "original_soundtrack", "true",
     "\"OST\": the original soundtrack. False: only your own songs play (see the\n"
     "# README: music/menu, music/rock, ... in the data folder).",
     K_BOOL, NULL, G(GFX_OST)},
    /* ---- the Switch side ---- */
    {"display", "resolution", "auto",
     "The window: 720, 1080 or auto (1080 if docked when the game starts, 720 in\n"
     "# handheld). The Switch scales it to the screen.",
     K_CHOICE, "auto,720,1080", NULL},
    {"display", "opengl_es", "2",
     "2 or 3: the OpenGL ES version offered to the game (it takes 3 where a phone\n"
     "# has it, for some effects). 2 is the tried path.",
     K_INT, "2,3", &g_cfg.gles},
    {"display", "loading_contexts", "2",
     "GL contexts the game may load textures on in the background (0 to 4; it\n"
     "# asks for its own number). 0 loads everything on the rendering thread.",
     K_INT, "0,1,2,3,4", &g_cfg.loader_contexts},
    /* ---- the start screen's own OPTIONS > START SCREEN ---- */
    {"start_screen", "sound_effects", "true", "The start screen's button sounds.", K_BOOL, NULL,
     &g_cfg.ss_effects},
    {"start_screen", "music", "true", "Music on the start screen.", K_BOOL, NULL, &g_cfg.ss_music},
    {"start_screen", "auto_backup", "false",
     "\"Auto backup\": your progress is copied to gameloft/games/GloftA8HP/save\n"
     "# every 30 s while playing (the start screen's RESTORE brings it back).",
     K_BOOL, NULL, &g_cfg.auto_backup},
    {"start_screen", "skip_start_screen", "false",
     "\"Skip start screen\": straight into the game. Hold - while the game starts\n"
     "# to see the start screen anyway.",
     K_BOOL, NULL, &g_cfg.skip_start},
    {"start_screen", "menu_size", "100", "\"Menu size\": 70, 80, 90, 100, 110 or 120 (%).", K_INT,
     "70,80,90,100,110,120", &g_cfg.menu_size},
    {"start_screen", "language", "en", "\"Language\" of the start screen: en, es, fr, pt, ru, vi or pl.",
     K_CHOICE, "en,es,fr,pt,ru,vi,pl", &g_cfg.ss_lang},
    {"start_screen", "whats_new_seen", "false", "What's New has been shown (it opens by itself once).",
     K_BOOL, NULL, &g_cfg.whats_new_seen},
    {"controls", "swap_a_b", "false",
     "false: A confirms, B goes back (by label). true: by position -- the bottom\n"
     "# button (B) confirms, as on an Android controller.",
     K_BOOL, NULL, &g_cfg.swap_ab},
    {"controls", "race_scheme", "switch",
     "The buttons in a race. switch: A accelerate, B brake, ZR or R drift (brake),\n"
     "# ZL, L or Y nitro, X camera, D-pad up respawn, + pause. shield: the game's\n"
     "# NVIDIA SHIELD layout (its controller card: B nitro, X / L brake, R2 / R1\n"
     "# accelerate). Menus are the same either way.",
     K_CHOICE, "switch,shield", &g_cfg.race_scheme},
    {"controls", "touchscreen", "true", "The handheld touchscreen works as the phone's screen.", K_BOOL,
     NULL, &g_cfg.touch},
    {"performance", "boost_cpu_when_loading", "true",
     "CPU at 1785 MHz while the game starts (until its first picture) and\n"
     "# inside loading frames (those over 50 ms), cpu_clock otherwise.",
     K_BOOL, NULL, &g_cfg.boost},
    {"performance", "gpu_boost_handheld", "true",
     "In handheld mode, the GPU at 460.8 MHz instead of 384 (a clock the system\n"
     "# offers games; more battery). Docked it runs at 768 MHz either way.",
     K_BOOL, NULL, &g_cfg.gpu_boost},
    {"performance", "cpu_clock", "1785",
     "The CPU clock in MHz from PLAY on: 1785 (the highest the Switch uses\n"
     "# itself, for its loading screens), 1581, 1428, 1224 or 1020 (the normal\n"
     "# clock). The game needs 1785 for a steady 60 fps in races; lower ones use\n"
     "# less battery. The GPU clock is not affected. The HOME menu and the start\n"
     "# screen run at the normal clock.",
     K_INT, "1020,1224,1428,1581,1785", &g_cfg.cpu_clock},
    {"performance", "gl_thread", "true",
     "The graphics driver's work on a second CPU core (Mesa's glthread), beside\n"
     "# the game's own, so the rendering thread has less to do.\n"
     "# false if you see graphics glitches.",
     K_BOOL, NULL, &g_cfg.gl_thread},
    {"debug", "gl_selftest", "false", "Graphics self-test picture at start-up.", K_BOOL, NULL,
     &g_cfg.gl_selftest},
    {"debug", "boot_log_on_screen", "false",
     "Show the start-up log on screen at every launch. Off: the log appears only\n"
     "# while something is being set up (first launch, a new A8R.apk, NRO or\n"
     "# settings that change the engine).",
     K_BOOL, NULL, &g_cfg.boot_log},
    {"debug", "log_java_calls", "false",
     "Write every Java method the game calls to debug.log (slow; for bug reports).", K_BOOL, NULL,
     &g_cfg.log_jni},
    {"debug", "capture_every_seconds", "0",
     "Save the picture the game draws as capture-NNN.bmp in this folder every so\n"
     "# many seconds (0: never; - is not needed). For bug reports.",
     K_INT, "0,5,10,15,20,30,45,60,120", &g_cfg.capture_secs},
    {"debug", "profile_loading", "true",
     "Write what every thread does while the game loads (start-up, races) to\n"
     "# debug.log, for making loading faster. Costs a few percent of loading speed.",
     K_BOOL, NULL, &g_cfg.profile},
    {"config", "version", "1", "Settings file format; leave as it is.", K_TEXT, NULL, NULL},
};
#define O_COUNT ((int)(sizeof k_opts / sizeof k_opts[0]))

static char g_val[O_COUNT][40];
static int g_have[O_COUNT];

static int opt_index(const char *section, const char *key) {
  for (int i = 0; i < O_COUNT; i++)
    if (!strcmp(k_opts[i].section, section) && !strcmp(k_opts[i].key, key))
      return i;
  return -1;
}

static char *trim(char *s) {
  while (*s == ' ' || *s == '\t')
    s++;
  char *e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
    *--e = 0;
  return s;
}

static void parse(FILE *f) {
  char line[256], section[32] = "";
  while (fgets(line, sizeof line, f)) {
    char *s = trim(line);
    if (!*s || *s == '#' || *s == ';')
      continue;
    if (*s == '[') {
      char *e = strchr(s, ']');
      if (e) {
        *e = 0;
        snprintf(section, sizeof section, "%s", trim(s + 1));
      }
      continue;
    }
    char *eq = strchr(s, '=');
    if (!eq)
      continue;
    *eq = 0;
    char *key = trim(s), *val = trim(eq + 1);
    char *hash = strpbrk(val, "#;");
    if (hash) {
      *hash = 0;
      val = trim(val);
    }
    for (int i = 0; i < O_COUNT; i++)
      if (!strcasecmp(section, k_opts[i].section) && !strcasecmp(key, k_opts[i].key)) {
        snprintf(g_val[i], sizeof g_val[i], "%s", val);
        g_have[i] = 1;
      }
  }
}

static void write_opts(FILE *f, int only_missing) {
  const char *last = NULL;
  for (int i = 0; i < O_COUNT; i++) {
    if (only_missing && g_have[i])
      continue;
    if (!last || strcmp(last, k_opts[i].section)) {
      fprintf(f, "\n[%s]\n", k_opts[i].section);
      if (!strcmp(k_opts[i].section, "mod"))
        fputs("# The A8R start screen's OPTIONS, with its labels in quotes and its\n"
              "# defaults. Changing one puts the game's engine together again on the next\n"
              "# start (a few seconds).\n",
              f);
      else if (!strcmp(k_opts[i].section, "quick_race"))
        fputs("# The start screen's Quick Race settings.\n", f);
    }
    last = k_opts[i].section;
    if (k_opts[i].help)
      fprintf(f, "# %s\n", k_opts[i].help);
    fprintf(f, "%s = %s\n", k_opts[i].key, g_val[i]);
  }
}

static int as_bool(int i) {
  const char *v = g_val[i];
  if (!strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on") || !strcmp(v, "1"))
    return 1;
  if (!strcasecmp(v, "false") || !strcasecmp(v, "no") || !strcasecmp(v, "off") || !strcmp(v, "0"))
    return 0;
  debugPrintf("[config] %s = %s: not true/false, using %s\n", k_opts[i].key, v, k_opts[i].def);
  return !strcmp(k_opts[i].def, "true");
}

/* index of the value in the option's choice list; the default's if unknown */
static int choice_of(const char *choices, const char *v) {
  const char *c = choices;
  for (int idx = 0; c && *c; idx++) {
    const char *e = strchr(c, ',');
    size_t n = e ? (size_t)(e - c) : strlen(c);
    if (strlen(v) == n && !strncasecmp(v, c, n))
      return idx;
    if (!e)
      break;
    c = e + 1;
  }
  return -1;
}

static int as_choice(int i) {
  int r = choice_of(k_opts[i].choices, g_val[i]);
  if (r >= 0)
    return r;
  debugPrintf("[config] %s = %s: not one of %s, using %s\n", k_opts[i].key, g_val[i],
              k_opts[i].choices, k_opts[i].def);
  r = choice_of(k_opts[i].choices, k_opts[i].def);
  return r >= 0 ? r : 0;
}

static int as_int(int i) {
  if (choice_of(k_opts[i].choices, g_val[i]) >= 0)
    return atoi(g_val[i]);
  debugPrintf("[config] %s = %s: not one of %s, using %s\n", k_opts[i].key, g_val[i],
              k_opts[i].choices, k_opts[i].def);
  return atoi(k_opts[i].def);
}

static void apply(void);

void dcr_config_load(void) {
  for (int i = 0; i < O_COUNT; i++)
    snprintf(g_val[i], sizeof g_val[i], "%s", k_opts[i].def);
  char path[300];
  snprintf(path, sizeof path, "%s/config.ini", dcr_game_root());
  FILE *f = fopen(path, "r");
  if (f) {
    parse(f);
    fclose(f);
    int missing = 0;
    for (int i = 0; i < O_COUNT; i++)
      missing += !g_have[i];
    if (missing && (f = fopen(path, "a"))) {
      fprintf(f, "\n# Added by build %llu (new options, at their defaults):\n",
              (unsigned long long)DCR_BUILD);
      write_opts(f, 1);
      fclose(f);
      debugPrintf("[config] added %d new option%s to config.ini\n", missing, missing > 1 ? "s" : "");
    }
  } else if ((f = fopen(path, "w"))) {
    fputs("# Asphalt 8: Airborne Retry for Switch -- settings.\n"
          "# Changes apply the next time the game starts. Delete this file to get\n"
          "# the defaults back.\n",
          f);
    write_opts(f, 0);
    fclose(f);
    debugPrintf("[config] wrote config.ini with the defaults\n");
  }

  apply();
}

/* g_val -> g_cfg */
static void apply(void) {
  for (int i = 0; i < O_COUNT; i++) {
    const Opt *o = &k_opts[i];
    if (!o->dst)
      continue;
    *o->dst = o->kind == K_BOOL ? as_bool(i) : o->kind == K_CHOICE ? as_choice(i) : as_int(i);
  }

  /* the nickname: A-Z, a-z, 0-9, up to 16 (the start screen's rule) */
  const char *nick = g_val[opt_index("mod", "nickname")];
  int n = 0;
  for (const char *p = nick; *p && n < 16; p++)
    if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9'))
      g_cfg.nickname[n++] = *p;
  g_cfg.nickname[n] = 0;
  if (!n)
    snprintf(g_cfg.nickname, sizeof g_cfg.nickname, "Driver");

  const char *r = g_val[opt_index("display", "resolution")];
  int docked = appletGetOperationMode() == AppletOperationMode_Console;
  int h = !strcmp(r, "720") ? 720 : !strcmp(r, "1080") ? 1080 : !strcasecmp(r, "auto") ? (docked ? 1080 : 720) : 0;
  if (!h) {
    debugPrintf("[config] resolution = %s: not 720, 1080 or auto, using auto\n", r);
    h = docked ? 1080 : 720;
  }
  g_cfg.res_h = h;
  g_cfg.res_w = h * 16 / 9;
  dcr_window_set_size(g_cfg.res_w, g_cfg.res_h);

  char sig[256];
  dcr_config_signature(sig, sizeof sig);
  debugPrintf("[config] window %dx%d (%s, %s), OpenGL ES %d, %d loading context%s, A/B %s, "
              "touch %s, CPU boost %s\n",
              g_cfg.res_w, g_cfg.res_h, r, docked ? "docked" : "handheld", g_cfg.gles,
              g_cfg.loader_contexts, g_cfg.loader_contexts == 1 ? "" : "s",
              g_cfg.swap_ab ? "by position" : "by label", g_cfg.touch ? "on" : "off",
              g_cfg.boost ? "on" : "off");
  debugPrintf("[config] mod: %s\n", sig);
}

/* Everything that changes libasphalt8.so or the files a8r_setup.c writes. */
void dcr_config_signature(char *out, unsigned cap) {
  int n = snprintf(out, cap, "m");
  for (int i = 0; i < MOD_COUNT && n < (int)cap; i++)
    n += snprintf(out + n, cap - n, "%d", g_cfg.mod[i]);
  if (n < (int)cap)
    n += snprintf(out + n, cap - n, " g");
  for (int i = 0; i < GFX_COUNT && n < (int)cap; i++)
    n += snprintf(out + n, cap - n, "%d", g_cfg.gfx[i]);
  if (n < (int)cap)
    snprintf(out + n, cap - n, " t%d r%d l%d e%d k%d aa%d af%d tf%d res%d n=%s", g_cfg.traffic,
             g_cfg.racers, g_cfg.laps, g_cfg.elimination_time, g_cfg.knockdown_limit, g_cfg.aa,
             g_cfg.anisotropy, g_cfg.texture_filtering, g_cfg.resolution_pct, g_cfg.nickname);
}

/* ------------------------------------------------ the start screen's edits */
const char *dcr_config_get(const char *section, const char *key) {
  int i = opt_index(section, key);
  return i < 0 ? "" : g_val[i];
}

const char *dcr_config_choices(const char *section, const char *key) {
  int i = opt_index(section, key);
  return i < 0 ? NULL : k_opts[i].choices;
}

int dcr_config_set(const char *section, const char *key, const char *value) {
  int i = opt_index(section, key);
  if (i < 0 || !value)
    return -1;
  const Opt *o = &k_opts[i];
  if (o->kind == K_BOOL && strcmp(value, "true") && strcmp(value, "false"))
    return -1;
  if ((o->kind == K_CHOICE || o->kind == K_INT) && o->choices && choice_of(o->choices, value) < 0)
    return -1;
  snprintf(g_val[i], sizeof g_val[i], "%s", value);
  g_have[i] = 1;
  return 0;
}

int dcr_config_save(void) {
  char path[300], tmp[310];
  snprintf(path, sizeof path, "%s/config.ini", dcr_game_root());
  snprintf(tmp, sizeof tmp, "%s.part", path);
  FILE *f = fopen(tmp, "w");
  if (!f)
    return -1;
  fputs("# Asphalt 8: Airborne Retry for Switch -- settings (also set on the start\n"
        "# screen: OPTIONS). Changes apply the next time the game starts. Delete this\n"
        "# file to get the defaults back.\n",
        f);
  write_opts(f, 0);
  int ok = fclose(f) == 0;
  if (ok) {
    remove(path);
    ok = rename(tmp, path) == 0;
  }
  if (!ok) {
    remove(tmp);
    debugPrintf("[config] could not write %s\n", path);
    return -1;
  }
  apply();
  return 0;
}
