/* dcr_config.h -- the user's settings, from <game folder>/config.ini (dcr_config.c). */
#ifndef DCR_USER_CONFIG_H
#define DCR_USER_CONFIG_H

/* The A8R start screen's switches that change the engine itself: each one
 * swaps pieces of libasphalt8.so for the mod's alternatives (a8r_setup.c
 * puts the engine together from them; the mod's MainActivity.verify*). */
enum {
  MOD_HIGH_QUALITY,     /* "High quality" (saveloadUpgrade, pieces 68 70) */
  MOD_60FPS,            /* "60 FPS" (saveloadFramerate, 22 24 26) */
  MOD_DIFFICULTY,       /* "Retry difficulty" (2 20) */
  MOD_CAMERA,           /* "Retry camera" (4) */
  MOD_KNOCKDOWN_CAMERA, /* "Knockdown camera" (62 64 66) */
  MOD_INFINITE_GEARS,   /* "Infinite gears" (6 8) */
  MOD_HUD,              /* "Gameplay HUD" (37) */
  MOD_BOTTOM_MINIMAP,   /* "Bottom minimap" (35) */
  MOD_CINEMATIC,        /* "Multiplayer cinematic" (14) */
  MOD_FAKE_SPEED,       /* "Fake speed" (10 16 18) */
  MOD_CALIPERS,         /* "Brake calipers" (12) */
  MOD_METAL_SPARKS,     /* "Metal spark particles" (28 30) */
  MOD_ATTRACT,          /* "Attract Mode" (32 38 40 42 46 58) */
  MOD_NEXT_BUTTON,      /* "NEXT button" (33 when off) */
  MOD_SKIP_TUTORIAL,    /* the car tip / tutorial skip (saveloadSkipCarTip, 56) */
  MOD_COUNT
};

/* The graphics switches: edits of gameprofiles.txt, the profile the engine
 * reads (GetProfilesStr), or extra files in the data folder. */
enum {
  GFX_MOTION_BLUR, GFX_SHADOWS, GFX_BREAKABLES, GFX_GLASS_CRACK, GFX_PARTICLES,
  GFX_AI_PARTICLES, GFX_SKID_MARKS, GFX_ROAD_REFLECTION, GFX_CAR_REFLECTION,
  GFX_ROAD_SPECULAR, GFX_CAR_SPECULAR, GFX_LENS_FLARE, GFX_ANAMORPHIC_GLOWS, GFX_CAR_DIRT,
  GFX_CLASSIC_PLATE, GFX_NATURAL_COLORS, GFX_BOKEH, GFX_3D_LETTERS, GFX_OST,
  GFX_COUNT
};

enum { TRAFFIC_DEFAULT, TRAFFIC_NONE, TRAFFIC_ALL };
enum { AA_NONE, AA_LIGHT, AA_HEAVY };
enum { TEX_TRILINEAR, TEX_BILINEAR, TEX_NONE };

typedef struct {
  /* [mod] */
  int mod[MOD_COUNT];
  int traffic;             /* TRAFFIC_* */
  int racers, laps, elimination_time, knockdown_limit; /* Quick Race */
  char nickname[17];       /* the profile name (patched into text/en.texts) */
  /* [graphics] */
  int gfx[GFX_COUNT];
  int aa;                  /* AA_* */
  int anisotropy;          /* 0 2 4 8 16 */
  int texture_filtering;   /* TEX_* */
  int resolution_pct;      /* 50..100: the profile's scaleDisplay */
  /* [display] */
  int res_w, res_h;        /* the window (the vi layer scales it to the screen) */
  int gles;                /* 2 or 3: the context version offered to the engine */
  int loader_contexts;     /* the engine's extra GL contexts for loading threads */
  /* [controls] */
  int swap_ab;             /* A/B by position (bottom = confirm) instead of by label */
  int race_scheme;         /* 0 switch (ZR drift, ZL nitro), 1 shield (the game's) */
  int touch;               /* the touchscreen */
  /* [performance], [debug] */
  int boost;
  int gpu_boost;           /* [performance] gpu_boost_handheld: GPU 460.8 MHz in handheld */
  int cpu_clock;           /* [performance] cpu_clock: MHz while the game runs, 0 = system (a8r_perf.c) */
  int gl_thread;           /* [performance] gl_thread: Mesa's glthread for the game's context */
  int gl_selftest;
  int boot_log;
  int log_jni;
  int capture_secs;        /* [debug] capture_every_seconds: frame captures (0 = off) */
  /* [start_screen] (a8r_menu.c) */
  int ss_effects, ss_music; /* its sound effects and music */
  int auto_backup;          /* the progress copied to the backup folder while playing */
  int skip_start;           /* straight into the game (hold - at launch for the screen) */
  int menu_size;            /* 70..120 (%) */
  int ss_lang;              /* index in "en,es,fr,pt,ru,vi,pl" */
  int whats_new_seen;       /* What's New opens by itself once */
  int profile;             /* [debug] profile_loading: the loading sampler (a8r_prof.c) */
} DcrConfig;

/* Read config.ini (writing it with the defaults, or adding missing options,
 * first). Early in main(); the defaults hold until then. */
void dcr_config_load(void);
const DcrConfig *dcr_config(void);

/* A short text naming everything that changes the engine or the data files
 * (a8r_setup.c redoes that work when it changes). */
void dcr_config_signature(char *out, unsigned cap);

/* The start screen's editing (a8r_menu.c): an option's value as written in
 * config.ini, its allowed values (comma-separated; NULL for true/false or
 * free text), setting one (checked against those; 0 on success), and
 * writing the file again -- which also makes dcr_config() the new values. */
const char *dcr_config_get(const char *section, const char *key);
const char *dcr_config_choices(const char *section, const char *key);
int dcr_config_set(const char *section, const char *key, const char *value);
int dcr_config_save(void);

#endif
