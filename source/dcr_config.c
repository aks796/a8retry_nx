/* dcr_config.c -- <game folder>/config.ini, the user's settings: the
 * options, on the runtime's INI engine (runtime/source/rt_cfg.c).
 *
 * Written with every option, its default and a line of explanation on the
 * first start; an existing file is appended to (options a newer build adds,
 * at the end, with their defaults), so edits and comments survive updates.
 * Plain INI: [section], key = value, # comments; booleans take true/false,
 * yes/no, on/off, 1/0. Read once at start-up: changes apply the next time the
 * game starts.
 *
 * [mod], [quick_race] and [graphics] are the A8R start screen's OPTIONS
 * button: the same switches, the same defaults (the mod's saveloadOptions), and
 * its own labels in quotes. The start screen itself is not run here: its PLAY
 * button's work (the engine put together with the chosen pieces, the profile
 * and data files edited) is done by a8r_setup.c whenever these change.
 * [performance], [display], [controls] and [debug] are the start screen's
 * fourth options page, SWITCH (a8r_menu.c). The start screen edits them with
 * the runtime's dcr_config_get / _choices / _set / _save. MIT.
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "rt_cfg.h"
#include "util.h"

static DcrConfig g_cfg;

const DcrConfig *dcr_config(void) { return &g_cfg; }

#define M(i) &g_cfg.mod[i]
#define G(i) &g_cfg.gfx[i]

static const CfgOpt k_opts[] = {
    /* ---- the start screen's OPTIONS > MISC ---- */
    {"mod", "retry_difficulty", "true",
     "\"Retry difficulty\": higher difficulty, with tougher bots and drift target scores.",
     CFG_BOOL, NULL, M(MOD_DIFFICULTY)},
    {"mod", "retry_camera", "true", "\"Retry camera\": the mod's reworked in-race camera.", CFG_BOOL,
     NULL, M(MOD_CAMERA)},
    {"mod", "knockdown_camera", "true", "\"Knockdown camera\": the slow-motion camera on knockdowns.",
     CFG_BOOL, NULL, M(MOD_KNOCKDOWN_CAMERA)},
    {"mod", "traffic", "default", "\"Traffic\": default, none or all (traffic in every event).",
     CFG_CHOICE, "default,none,all", &g_cfg.traffic},
    {"mod", "infinite_gears", "true",
     "\"Infinite gears\": the gear count never runs out (the car's performance is\n"
     "# not affected).",
     CFG_BOOL, NULL, M(MOD_INFINITE_GEARS)},
    {"mod", "gameplay_hud", "true", "\"Gameplay HUD\": the in-race HUD.", CFG_BOOL, NULL, M(MOD_HUD)},
    {"mod", "bottom_minimap", "false",
     "\"Bottom minimap\": the minimap at the bottom of the screen (needs the HUD).", CFG_BOOL, NULL,
     M(MOD_BOTTOM_MINIMAP)},
    {"mod", "multiplayer_cinematic", "false",
     "\"Multiplayer cinematic\": the multiplayer intro before single-player races.", CFG_BOOL, NULL,
     M(MOD_CINEMATIC)},
    {"mod", "fake_speed", "false",
     "\"Fake speed\": a higher speedometer reading (the car's performance is not\n# affected).",
     CFG_BOOL, NULL, M(MOD_FAKE_SPEED)},
    {"mod", "attract_mode", "false",
     "\"Attract Mode\": Quick Race becomes Attract Mode, for screenshots and\n"
     "# recordings (- goes on to the results).",
     CFG_BOOL, NULL, M(MOD_ATTRACT)},
    {"mod", "next_button", "true",
     "\"NEXT button\" on the post-race screen (without it, - goes on to the results).", CFG_BOOL,
     NULL, M(MOD_NEXT_BUTTON)},
    {"mod", "skip_tutorial", "false", "Skip the car tips shown when the game starts.", CFG_BOOL, NULL,
     M(MOD_SKIP_TUTORIAL)},
    {"mod", "nickname", "Driver",
     "\"Nickname\": the profile name in the game (A-Z, a-z, 0-9, up to 16).", CFG_TEXT, NULL, NULL},
    /* ---- Quick Race ---- */
    {"quick_race", "racers", "8", "Racers: 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20, 24, 28 or 32.", CFG_INT,
     "4,5,6,7,8,9,10,12,14,16,18,20,24,28,32", &g_cfg.racers},
    {"quick_race", "laps", "2", "Laps: 1 to 10, 12, 14, 16, 18 or 20.", CFG_INT,
     "1,2,3,4,5,6,7,8,9,10,12,14,16,18,20", &g_cfg.laps},
    {"quick_race", "elimination_time", "30", "Elimination time in seconds: 10, 15, 20, 25 or 30.",
     CFG_INT, "10,15,20,25,30", &g_cfg.elimination_time},
    {"quick_race", "knockdown_limit", "25", "Knockdown limit: 15, 20, 25, 30, 35, 40, 45 or 50.",
     CFG_INT, "15,20,25,30,35,40,45,50", &g_cfg.knockdown_limit},
    /* ---- the start screen's OPTIONS > GRAPHICS ---- */
    {"graphics", "60fps", "true", "\"60 FPS\": the high frame rate.", CFG_BOOL, NULL, M(MOD_60FPS)},
    {"graphics", "high_quality", "true", "\"High quality\": the high-quality car models and textures.",
     CFG_BOOL, NULL, M(MOD_HIGH_QUALITY)},
    {"graphics", "brake_calipers", "false", "\"Brake calipers\" (works partially).", CFG_BOOL, NULL,
     M(MOD_CALIPERS)},
    {"graphics", "anti_aliasing", "light", "\"Anti-aliasing\": none, light or heavy.", CFG_CHOICE,
     "none,light,heavy", &g_cfg.aa},
    {"graphics", "anisotropic_filtering", "16", "\"Anisotropic filtering\": 0, 2, 4, 8 or 16.", CFG_INT,
     "0,2,4,8,16", &g_cfg.anisotropy},
    {"graphics", "texture_filtering", "trilinear", "\"Texture filtering\": trilinear, bilinear or none.",
     CFG_CHOICE, "trilinear,bilinear,none", &g_cfg.texture_filtering},
    {"graphics", "resolution", "100",
     "\"Resolution\": the game's 3D resolution in percent of the window: 50, 60,\n"
     "# 70, 80, 90 or 100.",
     CFG_INT, "50,60,70,80,90,100", &g_cfg.resolution_pct},
    {"graphics", "motion_blur", "true", "\"Motion blur\".", CFG_BOOL, NULL, G(GFX_MOTION_BLUR)},
    {"graphics", "shadows", "true", "\"Shadows\": projected shadows.", CFG_BOOL, NULL, G(GFX_SHADOWS)},
    {"graphics", "breakables", "true", "\"Breakables\": fences, cones and the like.", CFG_BOOL, NULL,
     G(GFX_BREAKABLES)},
    {"graphics", "glass_crack", "true", "\"Glass crack effect\" on knockdowns.", CFG_BOOL, NULL,
     G(GFX_GLASS_CRACK)},
    {"graphics", "particles", "true", "\"Particles\".", CFG_BOOL, NULL, G(GFX_PARTICLES)},
    {"graphics", "metal_sparks", "false", "\"Metal spark particles\" (works partially; needs particles).",
     CFG_BOOL, NULL, M(MOD_METAL_SPARKS)},
    {"graphics", "ai_particles", "true", "\"AI particles\": particles from the other racers' cars.",
     CFG_BOOL, NULL, G(GFX_AI_PARTICLES)},
    {"graphics", "skid_marks", "true", "\"Skid marks\".", CFG_BOOL, NULL, G(GFX_SKID_MARKS)},
    {"graphics", "road_reflection", "true", "\"Road reflection\" of the surroundings.", CFG_BOOL, NULL,
     G(GFX_ROAD_REFLECTION)},
    {"graphics", "car_reflection", "true", "\"Car reflection\" of the surroundings.", CFG_BOOL, NULL,
     G(GFX_CAR_REFLECTION)},
    {"graphics", "road_specular", "true", "\"Road specular\": light reflected by the road.", CFG_BOOL,
     NULL, G(GFX_ROAD_SPECULAR)},
    {"graphics", "car_specular", "true", "\"Car specular\": light reflected by the cars.", CFG_BOOL, NULL,
     G(GFX_CAR_SPECULAR)},
    {"graphics", "lens_flare", "true", "\"Lens flare\".", CFG_BOOL, NULL, G(GFX_LENS_FLARE)},
    {"graphics", "anamorphic_glows", "true", "\"Anamorphic glows\".", CFG_BOOL, NULL,
     G(GFX_ANAMORPHIC_GLOWS)},
    {"graphics", "car_dirt", "true", "\"Car dirt\".", CFG_BOOL, NULL, G(GFX_CAR_DIRT)},
    {"graphics", "bokeh", "true", "\"Bokeh\": the camera's bokeh effect.", CFG_BOOL, NULL, G(GFX_BOKEH)},
    {"graphics", "3d_letters", "true", "\"3D letters\" such as the countdown.", CFG_BOOL, NULL,
     G(GFX_3D_LETTERS)},
    {"graphics", "classic_license_plate", "false",
     "\"Classic license plate\" of the game's release version.", CFG_BOOL, NULL, G(GFX_CLASSIC_PLATE)},
    {"graphics", "natural_colors", "false", "\"Natural colors\": an alternative color grading.", CFG_BOOL,
     NULL, G(GFX_NATURAL_COLORS)},
    {"graphics", "original_soundtrack", "true",
     "\"OST\": the original soundtrack. False: only your own songs play (see the\n"
     "# README: music/menu, music/rock, ... in the data folder).",
     CFG_BOOL, NULL, G(GFX_OST)},
    /* ---- the Switch side ---- */
    {"display", "resolution", "auto",
     "The window: 720, 1080 or auto (1080 if docked when the game starts, 720 in\n"
     "# handheld). The Switch scales it to the screen.",
     CFG_CHOICE, "auto,720,1080", NULL},
    {"display", "opengl_es", "2",
     "2 or 3: the OpenGL ES version offered to the game (it takes 3 where a phone\n"
     "# has it, for some effects). 2 is the tried path.",
     CFG_INT, "2,3", &g_cfg.gles},
    {"display", "loading_contexts", "2",
     "GL contexts the game may load textures on in the background (0 to 4; it\n"
     "# asks for its own number). 0 loads everything on the rendering thread.",
     CFG_INT, "0,1,2,3,4", &g_cfg.loader_contexts},
    /* ---- the start screen's own OPTIONS > START SCREEN ---- */
    {"start_screen", "sound_effects", "true", "The start screen's button sounds.", CFG_BOOL, NULL,
     &g_cfg.ss_effects},
    {"start_screen", "music", "true", "Music on the start screen.", CFG_BOOL, NULL, &g_cfg.ss_music},
    {"start_screen", "auto_backup", "false",
     "\"Auto backup\": your progress is copied to gameloft/games/GloftA8HP/save\n"
     "# every 30 s while playing (the start screen's RESTORE brings it back).",
     CFG_BOOL, NULL, &g_cfg.auto_backup},
    {"start_screen", "skip_start_screen", "false",
     "\"Skip start screen\": straight into the game. Hold - while the game starts\n"
     "# to see the start screen anyway.",
     CFG_BOOL, NULL, &g_cfg.skip_start},
    {"start_screen", "menu_size", "100", "\"Menu size\": 70, 80, 90, 100, 110 or 120 (%).", CFG_INT,
     "70,80,90,100,110,120", &g_cfg.menu_size},
    {"start_screen", "language", "en", "\"Language\" of the start screen: en, es, fr, pt, ru, vi or pl.",
     CFG_CHOICE, "en,es,fr,pt,ru,vi,pl", &g_cfg.ss_lang},
    {"start_screen", "whats_new_seen", "false", "What's New has been shown (it opens by itself once).",
     CFG_BOOL, NULL, &g_cfg.whats_new_seen},
    {"controls", "swap_a_b", "false",
     "false: A confirms, B goes back (by label). true: by position -- the bottom\n"
     "# button (B) confirms, as on an Android controller.",
     CFG_BOOL, NULL, &g_cfg.swap_ab},
    {"controls", "race_scheme", "switch",
     "The buttons in a race. switch: A accelerate, B brake, ZR or R drift (brake),\n"
     "# ZL, L or Y nitro, X camera, D-pad up respawn, + pause. shield: the game's\n"
     "# NVIDIA SHIELD layout (its controller card: B nitro, X / L brake, R2 / R1\n"
     "# accelerate). Menus are the same either way.",
     CFG_CHOICE, "switch,shield", &g_cfg.race_scheme},
    {"controls", "touchscreen", "true", "The handheld touchscreen works as the phone's screen.", CFG_BOOL,
     NULL, &g_cfg.touch},
    {"performance", "boost_cpu_when_loading", "true",
     "CPU at 1785 MHz while the game starts (until its first picture) and\n"
     "# inside loading frames (those over 50 ms), cpu_clock otherwise.",
     CFG_BOOL, NULL, &g_cfg.boost},
    {"performance", "gpu_boost_handheld", "true",
     "In handheld mode, the GPU at 460.8 MHz instead of 384 (a clock the system\n"
     "# offers games; more battery). Docked it runs at 768 MHz either way.",
     CFG_BOOL, NULL, &g_cfg.gpu_boost},
    {"performance", "cpu_clock", "1785",
     "The CPU clock in MHz from PLAY on: 1785 (the highest the Switch uses\n"
     "# itself, for its loading screens), 1581, 1428, 1224 or 1020 (the normal\n"
     "# clock), or system: never touched. The game needs 1785 for a steady 60 fps\n"
     "# in races; lower ones use less battery. The GPU clock is not affected. The\n"
     "# HOME menu and the start screen run at the normal clock. Overclocking tools\n"
     "# (sys-clk and the like) win: once one changes the clock, the game leaves\n"
     "# it alone until it is closed.",
     CFG_INT, "system,1020,1224,1428,1581,1785", &g_cfg.cpu_clock},
    {"performance", "gl_thread", "true",
     "The graphics driver's work on a second CPU core (Mesa's glthread), beside\n"
     "# the game's own, so the rendering thread has less to do.\n"
     "# false if you see graphics glitches.",
     CFG_BOOL, NULL, &g_cfg.gl_thread},
    {"debug", "gl_selftest", "false", "Graphics self-test picture at start-up.", CFG_BOOL, NULL,
     &g_cfg.gl_selftest},
    {"debug", "boot_log_on_screen", "false",
     "Show the start-up log on screen at every launch. Off: the log appears only\n"
     "# while something is being set up (first launch, a new A8R.apk, NRO or\n"
     "# settings that change the engine).",
     CFG_BOOL, NULL, &g_cfg.boot_log},
    {"debug", "log_java_calls", "false",
     "Write every Java method the game calls to debug.log (slow; for bug reports).", CFG_BOOL, NULL,
     &g_cfg.log_jni},
    {"debug", "capture_every_seconds", "0",
     "Save the picture the game draws as capture-NNN.bmp in this folder every so\n"
     "# many seconds (0: never; - is not needed). For bug reports.",
     CFG_INT, "0,5,10,15,20,30,45,60,120", &g_cfg.capture_secs},
    {"debug", "profile_loading", "false",
     "Write what every thread does while the game loads (start-up, races) to\n"
     "# debug.log, for making loading faster. Costs a few percent of loading speed.",
     CFG_BOOL, NULL, &g_cfg.profile},
};
static const CfgSection k_sections[] = {
    {"mod", "# The A8R start screen's OPTIONS, with its labels in quotes and its\n"
            "# defaults. Changing one puts the game's engine together again on the next\n"
            "# start (a few seconds).\n"},
    {"quick_race", "# The start screen's Quick Race settings.\n"},
};

/* Version 1 files came with profile_loading = true: turned off once, the
 * other values kept. */
static const CfgMigrate k_migrate[] = {
    {"debug", "profile_loading", NULL, "false", 0},
};

static void apply(void);

static const CfgTable k_table = {
    .opts = k_opts,
    .nopts = CFG_COUNT(k_opts),
    .sections = k_sections,
    .nsections = CFG_COUNT(k_sections),
    .migrate = k_migrate,
    .nmigrate = CFG_COUNT(k_migrate),
    .version = 2,
    .save_header = "# Asphalt 8: Airborne Retry for Switch -- settings (also set on the start\n"
                   "# screen: OPTIONS). Changes apply the next time the game starts. Delete this\n"
                   "# file to get the defaults back.\n",
    .apply = apply,
};

void dcr_config_load(void) { rt_config_load(&k_table); }

/* what the rows' dst do not cover, and the summary line */
static void apply(void) {
  /* the nickname: A-Z, a-z, 0-9, up to 16 (the start screen's rule) */
  const char *nick = rt_config_get("mod", "nickname");
  int n = 0;
  for (const char *p = nick; *p && n < 16; p++)
    if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9'))
      g_cfg.nickname[n++] = *p;
  g_cfg.nickname[n] = 0;
  if (!n)
    snprintf(g_cfg.nickname, sizeof g_cfg.nickname, "Driver");

  /* the window: the runtime's, from [display] resolution */
  g_cfg.res_w = rt_config()->res_w;
  g_cfg.res_h = rt_config()->res_h;

  const char *r = rt_config_get("display", "resolution");
  int docked = appletGetOperationMode() == AppletOperationMode_Console;
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
