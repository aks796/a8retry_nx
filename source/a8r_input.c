/* a8r_input.c -- the Switch's controllers and touchscreen, as the game's
 * Android activity reports a gamepad and a touchscreen.
 *
 * Controllers. Game.java recognises a gamepad by its device name once a
 * second (the "ab" timer) and tells the engine its kind with
 * nativeSetPowerAConnected: 5 = "NVIDIA Corporation NVIDIA Controller" (the
 * SHIELD controller, whose layout the Switch Pro Controller shares). For that
 * kind onGenericMotionEvent passes:
 *   left stick (AXIS_X, AXIS_Y)       -> nativeSetPowerALeftJoystick(x, y)
 *   right stick (AXIS_Z, AXIS_RZ)     -> nativeSetPowerARightJoystick(x, y)
 *   triggers (AXIS_LTRIGGER, _RTRIGGER) -> nativeSetPowerAL2R2MODEB(l, r)
 * and onKeyDown / onKeyUp pass the Android key codes to GL2JNILib.keyEvent
 * (0 = down, 1 = up). The engine's own gamepad support (GamepadNavigation in
 * the menus, PlayerInputConfig in races) does the rest. Once the game is up
 * (Game.D: its 5 s splash timer), nativeSendControllerTracking announces the
 * controller, as the timer does.
 *
 *   A B X Y     BUTTON_A B X Y (by label; config.ini [controls] swap_a_b: by
 *               position, as on an Android controller)
 *   L R         BUTTON_L1 R1      ZL ZR  BUTTON_L2 R2 + the trigger axes
 *   + -         BUTTON_START, BACK (the mod's "BACK virtual button")
 *   D-pad       DPAD_*            stick clicks  BUTTON_THUMBL / THUMBR
 *
 * STICKS. The engine reads a stick as digital -- left/right, up/down -- at
 * exactly +-1.0 (SetPowerALeftJoystick; Android reports 1.0 at the edge of a
 * stick's throw), so the throw is rescaled: nothing inside 12%, 1.0 from 85%
 * on. In races the left stick steers in proportion (the engine's wheel path,
 * a8r_loader.c patch_steering); the engine's "stick moved" flag, which picks
 * that path over the keys, is set only while the stick is out of its centre,
 * so the D-pad steers whenever the stick is let go.
 *
 * RACE CONTROLS ([controls] race_scheme). In a race (the engine's
 * KeyboardControl::IsRacing, a8r_loader.c) the buttons are the Switch's own
 * racing layout by default ("switch"): A accelerates, B brakes, ZR / R brake
 * to drift, ZL / L and Y fire the nitro, X changes the camera -- as the
 * engine's SHIELD scheme A keys (BUTTON_A accelerate, BUTTON_L1 brake,
 * BUTTON_B nitro, BUTTON_Y camera), with the trigger axes held at 0 so that
 * ZR is not also the SHIELD's R2 (accelerate). "shield" keeps the SHIELD
 * layout in races too (the game's controller card). Menus are always the
 * layout above. A button's release goes out as the key its press did, when
 * a race starts or ends in between.
 *
 * WIRELESS CONTROLLERS: which player slots may take a controller is the
 * application's call (hid SetSupportedNpadIdType); a controller with no slot
 * to go to -- Joy-Cons taken off the console, a Pro Controller -- keeps
 * searching. padConfigureInput() sends that list, but libnx32 is built for
 * arm-none-eabi, whose enums are as small as their values allow: HidNpadIdType
 * is one byte, so the list went as bytes that hid reads as 32-bit IDs of no
 * real slot, and only the attached Joy-Cons (the handheld slot) worked (found
 * in the Crossy Road port, hardware 2026-09-25). set_supported_npad_ids()
 * sends it again as 32-bit IDs; any player's controller drives the game.
 *
 * Touchscreen: GL2JNIView.onTouchEvent -> touchEvent(1 down / 2 move / 0 up,
 * x, y, pointer id), queued to the GL thread, in render pixels. MIT.
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "a8r.h"
#include "dcr_config.h"
#include "util.h"

const char *dcr_game_root(void); /* main.c */

#define NATIVE(name) "Java_com_gameloft_android_HEP_GloftA8HP_GL2JNILib_" name

typedef void (*fn_ii)(void *env, void *cls, jint a, jint b);
typedef void (*fn_i)(void *env, void *cls, jint a);
typedef void (*fn_ff)(void *env, void *cls, jfloat a, jfloat b);
typedef void (*fn_track)(void *env, void *cls, jboolean on, void *name, jint count);

static PadState g_pad;
static fn_ii n_key;
static fn_i n_connected;
static fn_ff n_left, n_right, n_l2r2;
static fn_track n_track;
static int g_ready, g_connected;

/* Android key codes */
enum {
  K_BACK = 4, K_DPAD_UP = 19, K_DPAD_DOWN = 20, K_DPAD_LEFT = 21, K_DPAD_RIGHT = 22,
  K_BUTTON_A = 96, K_BUTTON_B = 97, K_BUTTON_X = 99, K_BUTTON_Y = 100, K_BUTTON_L1 = 102,
  K_BUTTON_R1 = 103, K_BUTTON_L2 = 104, K_BUTTON_R2 = 105, K_BUTTON_THUMBL = 106,
  K_BUTTON_THUMBR = 107, K_BUTTON_START = 108,
};

typedef struct {
  u64 bit;
  int key;
} KeyMap;

static KeyMap g_map[] = {
    {HidNpadButton_A, K_BUTTON_A},       {HidNpadButton_B, K_BUTTON_B},
    {HidNpadButton_X, K_BUTTON_X},       {HidNpadButton_Y, K_BUTTON_Y},
    {HidNpadButton_L, K_BUTTON_L1},      {HidNpadButton_R, K_BUTTON_R1},
    {HidNpadButton_ZL, K_BUTTON_L2},     {HidNpadButton_ZR, K_BUTTON_R2},
    {HidNpadButton_Plus, K_BUTTON_START}, {HidNpadButton_Minus, K_BACK},
    {HidNpadButton_Up, K_DPAD_UP},       {HidNpadButton_Down, K_DPAD_DOWN},
    {HidNpadButton_Left, K_DPAD_LEFT},   {HidNpadButton_Right, K_DPAD_RIGHT},
    {HidNpadButton_StickL, K_BUTTON_THUMBL}, {HidNpadButton_StickR, K_BUTTON_THUMBR},
};

extern volatile uint8_t *g_a8r_stick_moved; /* a8r_loader.c */

/* hid SetSupportedNpadIdType (command 102), with 32-bit IDs: players 1-8 and
 * handheld (see the notes at the top). */
static Result set_supported_npad_ids(void) {
  static const u32 ids[] = {0, 1, 2, 3, 4, 5, 6, 7, 0x20};
  u64 aruid = appletGetAppletResourceUserId();
  return serviceDispatchIn(hidGetServiceSession(), 102, aruid,
                           .buffer_attrs = {SfBufferAttr_HipcPointer | SfBufferAttr_In},
                           .buffers = {{ids, sizeof ids}}, .in_send_pid = true);
}

/* The controllers and the touchscreen, once: the start screen (a8r_menu.c)
 * uses them before the game does. */
void a8r_input_hw_init(void) {
  static int done;
  if (done)
    return;
  done = 1;
  padConfigureInput(8, HidNpadStyleSet_NpadStandard);
  Result rc = set_supported_npad_ids();
  hidSetNpadJoyHoldType(HidNpadJoyHoldType_Horizontal); /* a single Joy-Con, held sideways */
  debugPrintf("[input] controllers allowed: players 1-8 and handheld, any style (%s)\n",
              R_SUCCEEDED(rc) ? "ok" : "hid refused it: only the attached Joy-Cons will work");
  hidInitializeTouchScreen();
}

/* - held while the game starts: the start screen even when it is skipped */
int a8r_input_minus_held(void) {
  a8r_input_hw_init();
  PadState p;
  padInitializeAny(&p);
  int held = 0;
  for (int i = 0; i < 8 && !held; i++) {
    padUpdate(&p);
    held = (padGetButtons(&p) & HidNpadButton_Minus) != 0;
    svcSleepThread(16000000ll);
  }
  return held;
}

extern void *volatile *g_a8r_kbctl; /* a8r_loader.c */

static int racing(void) {
  void *kc = g_a8r_kbctl ? *g_a8r_kbctl : NULL;
  return kc && ((volatile uint8_t *)kc)[4];
}

/* the Switch racing layout: the key a button sends in a race (0: as in menus) */
static int race_key(u64 bit) {
  switch (bit) {
  case HidNpadButton_A: return K_BUTTON_A;  /* accelerate */
  case HidNpadButton_B: return K_BUTTON_X;  /* brake */
  case HidNpadButton_X: return K_BUTTON_Y;  /* camera */
  case HidNpadButton_Y: return K_BUTTON_B;  /* nitro */
  case HidNpadButton_ZL: return K_BUTTON_B; /* nitro */
  case HidNpadButton_L: return K_BUTTON_B;  /* nitro */
  case HidNpadButton_ZR: return K_BUTTON_L1; /* brake: drift */
  case HidNpadButton_R: return K_BUTTON_L1;  /* brake: drift */
  default: return 0;
  }
}

void a8r_input_init(void) {
  a8r_input_hw_init();
  padInitializeAny(&g_pad);
  n_key = (fn_ii)a8r_native(NATIVE("keyEvent"));
  n_connected = (fn_i)a8r_native(NATIVE("nativeSetPowerAConnected"));
  n_left = (fn_ff)a8r_native(NATIVE("nativeSetPowerALeftJoystick"));
  n_right = (fn_ff)a8r_native(NATIVE("nativeSetPowerARightJoystick"));
  n_l2r2 = (fn_ff)a8r_native(NATIVE("nativeSetPowerAL2R2MODEB"));
  n_track = (fn_track)a8r_native(NATIVE("nativeSendControllerTracking"));
  if (dcr_config()->swap_ab) { /* by position: bottom = BUTTON_A, left = BUTTON_X */
    for (unsigned i = 0; i < sizeof g_map / sizeof g_map[0]; i++) {
      if (g_map[i].bit == HidNpadButton_A) g_map[i].key = K_BUTTON_B;
      else if (g_map[i].bit == HidNpadButton_B) g_map[i].key = K_BUTTON_A;
      else if (g_map[i].bit == HidNpadButton_X) g_map[i].key = K_BUTTON_Y;
      else if (g_map[i].bit == HidNpadButton_Y) g_map[i].key = K_BUTTON_X;
    }
  }
  debugPrintf("[input] controllers as an NVIDIA SHIELD-type gamepad%s; touchscreen %s\n",
              dcr_config()->swap_ab ? " (A/B by position)" : "", dcr_config()->touch ? "on" : "off");
}

void a8r_input_game_ready(void) {
  g_ready = 1;
  if (n_track && padIsConnected(&g_pad)) {
    JObj *name = jni_str("NVIDIA Corporation NVIDIA Controller v01.03");
    n_track(g_jni_env, a8r_class(C_GL2JNILIB), 1, name, 1);
    jni_release(name);
    debugPrintf("[input] controller announced to the game\n");
  }
}

/* -1..1: nothing inside 12% of the throw, 1.0 from 85% on (see the top) */
static float axis(s32 v) {
  float f = (float)v / 32767.0f, a = f < 0.0f ? -f : f;
  if (a < 0.12f)
    return 0.0f;
  a = (a - 0.12f) / (0.85f - 0.12f);
  if (a > 1.0f)
    a = 1.0f;
  return f < 0.0f ? -a : a;
}

/* -------------------------------------------------------------- touch */
#define MAX_TOUCH 10
static struct {
  int used;
  u32 finger;
  int x, y;
} g_touch[MAX_TOUCH];

static void poll_touch(void) {
  HidTouchScreenState st = {0};
  if (!hidGetTouchScreenStates(&st, 1))
    return;
  int w = a8r_screen_w(), h = a8r_screen_h();
  int seen[MAX_TOUCH] = {0};
  for (s32 i = 0; i < st.count && i < 16; i++) {
    const HidTouchState *t = &st.touches[i];
    int x = (int)((u64)t->x * (u64)w / 1280u), y = (int)((u64)t->y * (u64)h / 720u);
    int slot = -1;
    for (int k = 0; k < MAX_TOUCH; k++)
      if (g_touch[k].used && g_touch[k].finger == t->finger_id)
        slot = k;
    if (slot < 0) {
      for (int k = 0; k < MAX_TOUCH && slot < 0; k++)
        if (!g_touch[k].used)
          slot = k;
      if (slot < 0)
        continue;
      g_touch[slot].used = 1;
      g_touch[slot].finger = t->finger_id;
      g_touch[slot].x = x, g_touch[slot].y = y;
      a8r_post_touch(1, x, y, slot);
    } else if (x != g_touch[slot].x || y != g_touch[slot].y) {
      g_touch[slot].x = x, g_touch[slot].y = y;
      a8r_post_touch(2, x, y, slot);
    }
    seen[slot] = 1;
  }
  for (int k = 0; k < MAX_TOUCH; k++)
    if (g_touch[k].used && !seen[k]) {
      a8r_post_touch(0, g_touch[k].x, g_touch[k].y, k);
      g_touch[k].used = 0;
    }
}

/* ---------------------------------------------------------- the poll */
void a8r_input_poll(void) {
  if (!a8r_gl_running())
    return;
  void *cls = a8r_class(C_GL2JNILIB);
  padUpdate(&g_pad);
  int connected = padIsConnected(&g_pad);
  static int force = -1; /* test aid (".dump_textures"): a controller, for the game's controller card */
  if (force < 0) {
    char p[300];
    snprintf(p, sizeof p, "%s/.dump_textures", dcr_game_root());
    FILE *f = fopen(p, "r");
    force = f != NULL;
    if (f)
      fclose(f);
  }
  connected |= force;
  if (connected != g_connected && n_connected) {
    g_connected = connected;
    n_connected(g_jni_env, cls, connected ? 5 : 0); /* Game.z: 5 = NVIDIA Controller */
    debugPrintf("[input] controller %s\n", connected ? "connected (kind 5)" : "gone");
  }

  u64 down = padGetButtonsDown(&g_pad), up = padGetButtonsUp(&g_pad);
  int race = dcr_config()->race_scheme == 0 && racing();
  static int was_race = -1;
  if (race != was_race) {
    if (was_race >= 0)
      debugPrintf("[input] %s\n", race ? "race: the Switch racing layout" : "menus: the menu layout");
    was_race = race;
  }
  static int sent[sizeof g_map / sizeof g_map[0]]; /* the key each held button's press sent */
  for (unsigned i = 0; i < sizeof g_map / sizeof g_map[0] && n_key; i++) {
    if (down & g_map[i].bit) {
      int k = race ? race_key(g_map[i].bit) : 0;
      sent[i] = k ? k : g_map[i].key;
      n_key(g_jni_env, cls, 0, sent[i]);
    }
    if (up & g_map[i].bit)
      n_key(g_jni_env, cls, 1, sent[i] ? sent[i] : g_map[i].key);
  }

  /* the axes, when they change (onGenericMotionEvent fires on change) */
  static float last[6] = {9, 9, 9, 9, 9, 9};
  HidAnalogStickState l = padGetStickPos(&g_pad, 0), r = padGetStickPos(&g_pad, 1);
  u64 held = padGetButtons(&g_pad);
  float v[6] = {axis(l.x), -axis(l.y), axis(r.x), -axis(r.y), (held & HidNpadButton_ZL) && !race ? 1.0f : 0.0f,
                (held & HidNpadButton_ZR) && !race ? 1.0f : 0.0f};
  if ((v[0] != last[0] || v[1] != last[1]) && n_left) {
    n_left(g_jni_env, cls, v[0], v[1]);
    if (g_a8r_stick_moved) /* the stick steers only while it is out of its centre */
      *g_a8r_stick_moved = v[0] != 0.0f || v[1] != 0.0f;
  }
  if ((v[2] != last[2] || v[3] != last[3]) && n_right)
    n_right(g_jni_env, cls, v[2], v[3]);
  if ((v[4] != last[4] || v[5] != last[5]) && n_l2r2)
    n_l2r2(g_jni_env, cls, v[4], v[5]);
  memcpy(last, v, sizeof last);

  if (dcr_config()->touch)
    poll_touch();
  (void)g_ready;
}
