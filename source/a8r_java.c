/* a8r_java.c -- the Java side of Asphalt 8, answered in C.
 *
 * The game's Java (com.gameloft.android.HEP.GloftA8HP: Game, GL2JNILib,
 * GL2JNIView, GLUtils.SUtils / Device, DataSharing, SendInfo, the social and
 * store SDKs) does not run here. The engine calls about 200 static methods on
 * those classes through JNI; the ones that matter have a handler below doing
 * what the decompiled Java does (or what it does on a device with no network,
 * no telephony, no store and no web browser). Unhandled calls return 0 / false
 * / "" and are logged once (jni_core.c).
 *
 * Classes the engine gets a jclass for: the ones its natives are called on
 * (Game, GL2JNILib, SUtils, Device, DataSharing, SendInfo, ...: a8r_boot.c
 * passes their class objects) and a few it FindClass()es by name (SUtils,
 * Device, installer/GameInstaller, SplashScreenActivity, InGameBrowser, the
 * HID controller bridge; android/os/Bundle, android/os/Process,
 * android/media/AudioTrack). All are in classes.txt, the APK's own list.
 *
 * What the Java keeps is kept the Android way: SharedPreferences
 * (SUtils.nativeSetPreference / nativeGetPreference, the Bundle protocol:
 * npDataType, npKey, npPrefName, npData, npDefaultValue, npResult) are
 * <root>/data/shared_prefs/<name>.xml (dcr_prefs.c), as on a phone. MIT.
 */
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <unistd.h>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz/miniz.h>

#include "a8r.h"
#include "config.h"
#include "dcr_config.h"
#include "dcr_manifest.h"
#include "dcr_path.h"
#include "dcr_prefs.h"
#include "jni.h"
#include "util.h"

#define PKG A8R_PACKAGE
#define A_FILES "/data/data/" PKG "/files"
#define A_CACHE "/data/data/" PKG "/cache"
#define A_APK "/data/app/" PKG "-1/base.apk"
#define S "Ljava/lang/String;"

#define H(fn) static jvalue fn(JObj *self, const jvalue *a, const JMethod *m)

const char *dcr_game_root(void); /* main.c */

JObj *g_activity;

void *a8r_class(const char *name) { return jni_class(name)->obj; }

/* ================================================================= misc */
H(h_void) { return jv_none(); }
H(h_false) { return jv_z(0); }
H(h_true) { return jv_z(1); }
H(h_zero) { return jv_i(0); }
H(h_empty) { return jv_l(jni_str("")); }

static const char *arg_str(const jvalue *a, int i) { return jni_utf(a[i].l); }

/* ------------------------------------------------------------------ MD5 */
typedef struct {
  uint32_t s[4];
  uint64_t n;
  uint8_t buf[64];
} Md5;

/* Scalar on purpose: GCC's NEON form of the rotates (vsri) is one Ryujinx's
 * A32 decoder lacks, and this runs once. */
__attribute__((optimize("no-tree-vectorize", "no-tree-slp-vectorize")))
static void md5_block(Md5 *c, const uint8_t *p) {
  static const uint32_t K[64] = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
      0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
      0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
      0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
      0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
      0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
      0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
      0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
  static const uint8_t R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
  uint32_t w[16];
  for (int i = 0; i < 16; i++)
    w[i] = (uint32_t)p[i * 4] | (uint32_t)p[i * 4 + 1] << 8 | (uint32_t)p[i * 4 + 2] << 16 |
           (uint32_t)p[i * 4 + 3] << 24;
  uint32_t A = c->s[0], B = c->s[1], C = c->s[2], D = c->s[3];
  for (int i = 0; i < 64; i++) {
    uint32_t f;
    int g;
    if (i < 16) f = (B & C) | (~B & D), g = i;
    else if (i < 32) f = (D & B) | (~D & C), g = (5 * i + 1) & 15;
    else if (i < 48) f = B ^ C ^ D, g = (3 * i + 5) & 15;
    else f = C ^ (B | ~D), g = (7 * i) & 15;
    uint32_t t = D;
    D = C;
    C = B;
    uint32_t x = A + f + K[i] + w[g];
    B = B + ((x << R[i]) | (x >> (32 - R[i])));
    A = t;
  }
  c->s[0] += A, c->s[1] += B, c->s[2] += C, c->s[3] += D;
}

static void md5(const void *data, size_t len, uint8_t out[16]) {
  Md5 c = {{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476}, 0, {0}};
  const uint8_t *p = data;
  size_t full = len & ~(size_t)63;
  for (size_t i = 0; i < full; i += 64)
    md5_block(&c, p + i);
  uint8_t tail[128] = {0};
  size_t rest = len - full;
  memcpy(tail, p + full, rest);
  tail[rest] = 0x80;
  size_t tl = rest < 56 ? 64 : 128;
  uint64_t bits = (uint64_t)len * 8;
  for (int i = 0; i < 8; i++)
    tail[tl - 8 + i] = (uint8_t)(bits >> (8 * i));
  md5_block(&c, tail);
  if (tl == 128)
    md5_block(&c, tail + 64);
  for (int i = 0; i < 4; i++)
    for (int k = 0; k < 4; k++)
      out[i * 4 + k] = (uint8_t)(c.s[i] >> (8 * k));
}

/* SUtils.getInt: big-endian */
static int32_t be32(const uint8_t *b) {
  return (int32_t)((uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3]);
}

static JObj *md5_ints(const char *text) {
  uint8_t d[16];
  md5(text, strlen(text), d);
  JObj *arr = jni_array('I', 4);
  for (int i = 0; i < 4; i++)
    ((jint *)arr->a.data)[i] = be32(d + 4 * i);
  return arr;
}

/* ============================================================= identity */
/* A device id made once and kept, as a phone's would be (HDIDFV is
 * Gameloft's own id: a UUID shared between its games). */
static char g_uuid[40], g_android_id[20];

static void identity_init(void) {
  DcrPrefs *p = dcr_prefs_open("a8r_device");
  char t;
  char *u = dcr_prefs_get(p, "hdidfv", &t), *id = dcr_prefs_get(p, "android_id", &t);
  if (u && id && strlen(u) == 36 && strlen(id) == 16) {
    snprintf(g_uuid, sizeof g_uuid, "%s", u);
    snprintf(g_android_id, sizeof g_android_id, "%s", id);
  } else {
    uint8_t r[24];
    randomGet(r, sizeof r);
    r[6] = (uint8_t)((r[6] & 0x0f) | 0x40); /* version 4 */
    r[8] = (uint8_t)((r[8] & 0x3f) | 0x80);
    snprintf(g_uuid, sizeof g_uuid,
             "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", r[0], r[1],
             r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12], r[13], r[14], r[15]);
    snprintf(g_android_id, sizeof g_android_id, "%02x%02x%02x%02x%02x%02x%02x%02x", r[16], r[17],
             r[18], r[19], r[20], r[21], r[22], r[23]);
    DcrPrefsEdit *e = dcr_prefs_edit(p);
    dcr_prefs_put(e, 's', "hdidfv", g_uuid);
    dcr_prefs_put(e, 's', "android_id", g_android_id);
    dcr_prefs_commit(e);
    dcr_prefs_edit_free(e);
    debugPrintf("[java] new device identity %s\n", g_uuid);
  }
  free(u);
  free(id);
}

#define DEVICE_MANUFACTURER "Nintendo"
#define DEVICE_MODEL "Switch"
#define USER_AGENT "Mozilla/5.0 (Linux; Android 9; " DEVICE_MODEL ") AppleWebKit/537.36 (KHTML, like Gecko) Mobile Safari/537.36"

/* ======================================================= SharedPreferences */
static const char *pref_file(const char *name) { return name && *name ? name : "GamePrefs"; }

static char *pref_get(const char *file, const char *key, char *type) {
  return dcr_prefs_get(dcr_prefs_open(pref_file(file)), key, type);
}

static void pref_put(const char *file, char type, const char *key, const char *val) {
  DcrPrefsEdit *e = dcr_prefs_edit(dcr_prefs_open(pref_file(file)));
  dcr_prefs_put(e, type, key, val);
  dcr_prefs_commit(e);
  dcr_prefs_edit_free(e);
}

static const char *sd_folder(void) {
  static char s[256];
  char t;
  char *v = pref_get("GamePrefs", "SDFolder", &t);
  snprintf(s, sizeof s, "%s", v && *v ? v : A8R_ANDROID_DATA);
  free(v);
  return s;
}

/* ================================================================ Bundle */
typedef struct BEntry {
  struct BEntry *next;
  char key[64];
  char type; /* s i l z b */
  jlong num;
  char *str;
  JObj *bytes;
} BEntry;

static void bundle_finalize(JObj *o) {
  for (BEntry *e = o->p, *n; e; e = n) {
    n = e->next;
    free(e->str);
    jni_release(e->bytes);
    free(e);
  }
  o->p = NULL;
}

static BEntry *bundle_find(JObj *o, const char *key, int create) {
  if (!o)
    return NULL;
  for (BEntry *e = o->p; e; e = e->next)
    if (!strcmp(e->key, key))
      return e;
  if (!create)
    return NULL;
  BEntry *e = calloc(1, sizeof *e);
  if (!e)
    return NULL;
  snprintf(e->key, sizeof e->key, "%s", key);
  e->next = o->p;
  o->p = e;
  o->finalize = bundle_finalize;
  return e;
}

static void bundle_clear_value(BEntry *e) {
  free(e->str);
  e->str = NULL;
  jni_release(e->bytes);
  e->bytes = NULL;
  e->num = 0;
}

H(h_bundle_init) {
  self->finalize = bundle_finalize;
  return jv_none();
}
H(h_bundle_putString) {
  BEntry *e = bundle_find(self, arg_str(a, 0), 1);
  if (e) {
    bundle_clear_value(e);
    e->type = 's';
    e->str = a[1].l ? strdup(jni_utf(a[1].l)) : NULL;
  }
  return jv_none();
}
H(h_bundle_getString) {
  BEntry *e = bundle_find(self, arg_str(a, 0), 0);
  return jv_l(e && e->type == 's' && e->str ? jni_str(e->str) : NULL);
}
static void bundle_put_num(JObj *self, const char *key, char type, jlong v) {
  BEntry *e = bundle_find(self, key, 1);
  if (e) {
    bundle_clear_value(e);
    e->type = type;
    e->num = v;
  }
}
static jlong bundle_get_num(JObj *self, const char *key) {
  BEntry *e = bundle_find(self, key, 0);
  return e && e->type != 's' && e->type != 'b' ? e->num : 0;
}
H(h_bundle_putInt) { bundle_put_num(self, arg_str(a, 0), 'i', a[1].i); return jv_none(); }
H(h_bundle_getInt) { return jv_i((jint)bundle_get_num(self, arg_str(a, 0))); }
H(h_bundle_putLong) { bundle_put_num(self, arg_str(a, 0), 'l', a[1].j); return jv_none(); }
H(h_bundle_getLong) { return jv_j(bundle_get_num(self, arg_str(a, 0))); }
H(h_bundle_putBoolean) { bundle_put_num(self, arg_str(a, 0), 'z', a[1].z != 0); return jv_none(); }
H(h_bundle_getBoolean) { return jv_z(bundle_get_num(self, arg_str(a, 0)) != 0); }
H(h_bundle_containsKey) { return jv_z(bundle_find(self, arg_str(a, 0), 0) != NULL); }
H(h_bundle_clear) {
  bundle_finalize(self);
  return jv_none();
}
H(h_bundle_putByteArray) {
  BEntry *e = bundle_find(self, arg_str(a, 0), 1);
  if (e) {
    bundle_clear_value(e);
    e->type = 'b';
    e->bytes = jni_retain(a[1].l);
  }
  return jv_none();
}
H(h_bundle_getByteArray) {
  BEntry *e = bundle_find(self, arg_str(a, 0), 0);
  return jv_l(e && e->type == 'b' ? jni_retain(e->bytes) : NULL);
}

/* SUtils.nativeGetPreference / nativeSetPreference: npDataType 0 int, 1 long,
 * 2 boolean, 3 String; npKey, npPrefName; npData / npDefaultValue -> npResult. */
H(h_nativeGetPreference) {
  JObj *b = a[0].l;
  if (!b)
    return jv_l(NULL);
  int type = (int)bundle_get_num(b, "npDataType");
  BEntry *ke = bundle_find(b, "npKey", 0), *fe = bundle_find(b, "npPrefName", 0);
  const char *key = ke && ke->str ? ke->str : "", *file = fe && fe->str ? fe->str : "";
  char t = 0;
  char *v = pref_get(file, key, &t);
  switch (type) {
  case 0:
    bundle_put_num(b, "npResult", 'i', v ? (jint)strtol(v, NULL, 10) : (jint)bundle_get_num(b, "npDefaultValue"));
    break;
  case 1:
    bundle_put_num(b, "npResult", 'l', v ? (jlong)strtoll(v, NULL, 10) : bundle_get_num(b, "npDefaultValue"));
    break;
  case 2:
    bundle_put_num(b, "npResult", 'z', v ? !strcmp(v, "true") : bundle_get_num(b, "npDefaultValue") != 0);
    break;
  case 3: {
    BEntry *d = bundle_find(b, "npDefaultValue", 0);
    BEntry *r = bundle_find(b, "npResult", 1);
    if (r) {
      bundle_clear_value(r);
      r->type = 's';
      r->str = strdup(v ? v : d && d->str ? d->str : "");
    }
    break;
  }
  }
  free(v);
  return jv_l(jni_retain(b));
}

H(h_nativeSetPreference) {
  JObj *b = a[0].l;
  if (!b)
    return jv_none();
  int type = (int)bundle_get_num(b, "npDataType");
  BEntry *ke = bundle_find(b, "npKey", 0), *fe = bundle_find(b, "npPrefName", 0);
  const char *key = ke && ke->str ? ke->str : "", *file = fe && fe->str ? fe->str : "";
  char v[64];
  switch (type) {
  case 0:
    snprintf(v, sizeof v, "%d", (int)bundle_get_num(b, "npData"));
    pref_put(file, 'i', key, v);
    break;
  case 1:
    snprintf(v, sizeof v, "%lld", (long long)bundle_get_num(b, "npData"));
    pref_put(file, 'l', key, v);
    break;
  case 2:
    pref_put(file, 'b', key, bundle_get_num(b, "npData") ? "true" : "false");
    break;
  case 3: {
    BEntry *d = bundle_find(b, "npData", 0);
    pref_put(file, 's', key, d && d->str ? d->str : "");
    break;
  }
  }
  return jv_none();
}

/* AndroidOS::Init: SUtils.getPreferenceString(key, prefName) */
H(h_getPreferenceString) {
  char t;
  char *v = pref_get(arg_str(a, 1), arg_str(a, 0), &t);
  JObj *r = jni_str(v ? v : "");
  free(v);
  return jv_l(r);
}

/* ========================================================== GL2JNILib */
typedef void (*fn_setPaths)(void *env, void *cls, void *sd, void *files, void *cache);

H(h_setupPaths) {
  fn_setPaths f = (fn_setPaths)a8r_native("Java_com_gameloft_android_HEP_GloftA8HP_GL2JNILib_setPaths");
  if (!f)
    return jv_none();
  JObj *sd = jni_str(sd_folder()), *files = jni_str(A_FILES), *cache = jni_str(A_CACHE);
  debugPrintf("[java] setupPaths -> setPaths(%s, %s, %s)\n", jni_utf(sd), A_FILES, A_CACHE);
  f(g_jni_env, a8r_class(C_GL2JNILIB), sd, files, cache);
  jni_release(sd);
  jni_release(files);
  jni_release(cache);
  return jv_none();
}

H(h_createView) {
  a8r_view_requested();
  return jv_none();
}
H(h_setViewSettings) {
  a8r_set_view_settings(a[0].i, a[1].i, a[2].i, a[3].i, a[4].i);
  return jv_none();
}
H(h_setCurrentContext) { return jv_z(a8r_set_current_context(a[0].i)); }

H(h_GetPhoneWidth) { return jv_i(a8r_screen_w()); }
H(h_GetPhoneHeight) { return jv_i(a8r_screen_h()); }
/* The screen's diagonal in inches (GetPhoneDII: the resolution profile's
 * minDII; the mod's profiles have no bounds) -- the Switch's 6.2". */
H(h_GetPhoneDII) { return jv_d(6.2); }
H(h_GetPhoneMemory) { return jv_j(3072); } /* MB: /proc/meminfo's MemTotal / 1024 */
H(h_GetMaxCPUSpeed) { return jv_f(1.785f); } /* GHz (cpuinfo_max_freq / 1e6) */
H(h_GetCPUPartInfo) { return jv_l(jni_str("0xd07")); } /* Cortex-A57 */
H(h_GetResProfileName) { return jv_l(jni_str("RES_0")); } /* Game.a(): no profile has bounds */
H(h_getPVScaleRate) { return jv_f((float)dcr_config()->resolution_pct / 100.0f); }
H(h_getOBBFullPath) { return jv_l(jni_str_fmt("%s/" A8R_OBB_NAME, sd_folder())); }
H(h_getRegionFormat) { return jv_l(jni_str("en_US")); }
H(h_GetDeviceFirmware) { return jv_l(jni_str("9")); }
H(h_GetDeviceIdentifier) { return jv_l(jni_str(g_android_id)); }
H(h_getDeviceUserAgent) { return jv_l(jni_str(USER_AGENT)); }
H(h_GetPhoneIP) { return jv_l(jni_str("0.0.0.0")); }
H(h_GetFreeSpaceInKBytes) {
  struct statvfs vs;
  if (statvfs("sdmc:/", &vs) != 0)
    return jv_i(1 << 20);
  unsigned long long kb = (unsigned long long)vs.f_bavail * vs.f_frsize / 1024;
  return jv_i(kb > 0x7fffffff ? 0x7fffffff : (jint)kb);
}
/* GL2JNILib.GetProfilesStr: gameprofiles.txt as read by Game.a() -- the copy
 * the mod's PLAY button makes and edits (a8r_setup.c). */
H(h_GetProfilesStr) {
  char path[300];
  snprintf(path, sizeof path, "%s/data/files/gameprofiles.txt", dcr_game_root());
  FILE *f = fopen(path, "rb");
  if (!f)
    return jv_l(jni_str(""));
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *s = n > 0 ? malloc((size_t)n + 1) : NULL;
  if (s && fread(s, 1, (size_t)n, f) == (size_t)n)
    s[n] = 0;
  else if (s)
    s[0] = 0;
  fclose(f);
  JObj *r = jni_str(s ? s : "");
  free(s);
  return jv_l(r);
}
H(h_GetGameLanguage) {
  debugPrintf("[java] game language: %s\n", arg_str(a, 0));
  return jv_none();
}
H(h_Exit) {
  debugPrintf("[java] GL2JNILib.Exit(%d)\n", a[0].z);
  if (!a[0].z) /* true = SendAppToBackground */
    a8r_request_exit();
  return jv_none();
}
H(h_RestartGame) {
  debugPrintf("[java] GL2JNILib.RestartGame: closing (launch the game again)\n");
  a8r_request_exit();
  return jv_none();
}
H(h_log_url) {
  debugPrintf("[java] %s.%s(%s): no browser here\n", m->cls->name, m->name, arg_str(a, 0));
  return jv_none();
}

/* ============================================================== SUtils */
H(h_getSDFolder) { return jv_l(jni_str(sd_folder())); }
H(h_getSaveFolder) { return jv_l(jni_str(A_FILES)); }
H(h_getPackage) { return jv_l(jni_str(PKG)); }
H(h_getGameName) { return jv_l(jni_str(PKG + 21)); } /* "HEP.GloftA8HP" */
H(h_getContext) { return jv_l(g_activity); }
H(h_GetApkPath) { return jv_l(jni_str(A_APK)); }
H(h_getUserAgent) { return jv_l(jni_str(USER_AGENT)); }
H(h_getMetaDataValue) {
  const DcrMeta *md = dcr_manifest_meta(arg_str(a, 0));
  if (!md)
    return jv_l(jni_str(""));
  if (md->type == DCR_META_STRING)
    return jv_l(jni_str(md->s));
  if (md->type == DCR_META_FLOAT)
    return jv_l(jni_str_fmt("%g", (double)md->f));
  if (md->type == DCR_META_BOOL)
    return jv_l(jni_str(md->i ? "true" : "false"));
  return jv_l(jni_str_fmt("%d", (int)md->i));
}
H(h_getAssetAsString) {
  const char *name = arg_str(a, 0);
  if (name[0] == '.')
    name++;
  size_t len = 0;
  uint8_t *d = a8r_apk_asset(name, &len);
  if (!d) {
    debugPrintf("[java] getAssetAsString(%s): not in the APK\n", arg_str(a, 0));
    return jv_l(NULL);
  }
  JObj *arr = jni_array('B', (jsize)len);
  memcpy(arr->a.data, d, len);
  free(d);
  return jv_l(arr);
}
/* getGLUID(String): MD5 of the string + the game's package, as 4 ints */
H(h_getGLUID_str) {
  char text[512];
  snprintf(text, sizeof text, "%s%s", arg_str(a, 0), PKG);
  return jv_l(md5_ints(text));
}
/* SendInfo.getGLUID(): the same over its GLDID and SendInfo's package */
H(h_sendinfo_getGLUID) {
  char text[256];
  snprintf(text, sizeof text, "%s%s", g_android_id, PKG);
  return jv_l(md5_ints(text));
}
/* retrieveBarrels: the hashCode()s of the APK's signatures (one) */
H(h_retrieveBarrels) {
  JObj *arr = jni_array('I', 4);
  ((jint *)arr->a.data)[0] = a8r_signature_hash();
  return jv_l(arr);
}

static void to_real(const char *android, char *out, size_t cap) {
  const char *r = dcr_translate_path(android, out, cap);
  if (r != out)
    snprintf(out, cap, "%s", r);
}

H(h_deleteFile) {
  char p[DCR_PATH_MAX];
  to_real(arg_str(a, 0), p, sizeof p);
  unlink(p);
  return jv_none();
}

static int remove_tree(const char *p) {
  DIR *d = opendir(p);
  if (!d)
    return unlink(p) == 0;
  struct dirent *e;
  while ((e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
      continue;
    char c[DCR_PATH_MAX];
    snprintf(c, sizeof c, "%s/%s", p, e->d_name);
    remove_tree(c);
  }
  closedir(d);
  return rmdir(p) == 0;
}

H(h_removeDirectoryRecursively) {
  char p[DCR_PATH_MAX];
  to_real(arg_str(a, 0), p, sizeof p);
  return jv_z(remove_tree(p));
}

H(h_genericUnzipArchive) {
  char src[DCR_PATH_MAX], dst[DCR_PATH_MAX];
  to_real(arg_str(a, 0), src, sizeof src);
  to_real(arg_str(a, 1), dst, sizeof dst);
  mz_zip_archive z;
  memset(&z, 0, sizeof z);
  if (!mz_zip_reader_init_file(&z, src, 0))
    return jv_z(0);
  int ok = 1;
  mz_uint n = mz_zip_reader_get_num_files(&z);
  for (mz_uint i = 0; i < n && ok; i++) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&z, i, &st) || strstr(st.m_filename, ".."))
      continue;
    char out[DCR_PATH_MAX + 512];
    snprintf(out, sizeof out, "%s/%s", dst, st.m_filename);
    for (char *s = strchr(out + 6, '/'); s; s = strchr(s + 1, '/')) {
      *s = 0;
      mkdir(out, 0777);
      *s = '/';
    }
    if (!st.m_is_directory)
      ok = mz_zip_reader_extract_to_file(&z, i, out, 0);
  }
  mz_zip_reader_end(&z);
  debugPrintf("[java] genericUnzipArchive(%s -> %s): %s\n", arg_str(a, 0), arg_str(a, 1), ok ? "ok" : "failed");
  return jv_z(ok);
}

/* The virtual keyboard: not wired to the Switch keyboard yet (the one place
 * the game asks for text is its profile name, which config.ini sets). */
H(h_ShowKeyboard) {
  debugPrintf("[java] ShowKeyboard(%s): the Switch keyboard is not wired up\n", arg_str(a, 0));
  return jv_none();
}

/* ============================================================== Device */
H(h_androidId) { return jv_l(jni_str(g_android_id)); }
H(h_hdidfv) { return jv_l(jni_str(g_uuid)); }
H(h_hdidfvVersion) { return jv_l(jni_str("1")); }
H(h_gldid) {
  return jv_l(jni_str_fmt("hdidfv=%s imei= mac=02:00:00:00:00:00 aid=%s serialNo=%s ", g_uuid,
                          g_android_id, g_android_id));
}
H(h_serial) { return jv_l(jni_str(g_android_id)); }
H(h_mac) { return jv_l(jni_str("02:00:00:00:00:00")); }
H(h_manufacturer) { return jv_l(jni_str(DEVICE_MANUFACTURER)); }
H(h_model) { return jv_l(jni_str(DEVICE_MODEL)); }
H(h_deviceName) { return jv_l(jni_str(DEVICE_MANUFACTURER " " DEVICE_MODEL)); }
H(h_manufacturerModel) { return jv_l(jni_str(DEVICE_MANUFACTURER "_" DEVICE_MODEL)); }
H(h_country) { return jv_l(jni_str("US")); }
H(h_country_lower) { return jv_l(jni_str("us")); }
H(h_language) { return jv_l(jni_str("en")); }
H(h_locale) { return jv_l(jni_str("en_US")); }
H(h_product) { return jv_l(jni_str("nx")); }

/* ========================================================= DataSharing */
/* Values Gameloft games share on a device (a content provider): kept in the
 * "gameloft_sharing" preferences here. */
H(h_ds_set) {
  pref_put("gameloft_sharing", 's', arg_str(a, 0), arg_str(a, 1));
  return jv_none();
}
H(h_ds_get) {
  char t;
  char *v = pref_get("gameloft_sharing", arg_str(a, 0), &t);
  JObj *r = jni_str(v ? v : "");
  free(v);
  return jv_l(r);
}
H(h_ds_delete) {
  DcrPrefsEdit *e = dcr_prefs_edit(dcr_prefs_open("gameloft_sharing"));
  dcr_prefs_remove(e, arg_str(a, 0));
  dcr_prefs_commit(e);
  dcr_prefs_edit_free(e);
  return jv_none();
}
H(h_ds_is) {
  char t;
  char *v = pref_get("gameloft_sharing", arg_str(a, 0), &t);
  int r = v != NULL;
  free(v);
  return jv_z(r);
}

/* ============================================================== tables */
#define GL C_GL2JNILIB
#define SU C_SUTILS
#define DV C_DEVICE
#define SI C_SENDINFO

const JMethodDef jni_method_defs[] = {
    /* ---- GL2JNILib (resolved in GL2JNILib.init) ---- */
    {GL, "setupPaths", "()V", h_setupPaths},
    {GL, "createView", "()V", h_createView},
    {GL, "setViewSettings", "(IIIII)V", h_setViewSettings},
    {GL, "setCurrentContext", "(I)Z", h_setCurrentContext},
    {GL, "enableAccelerometer", "(ZF)V", h_void},
    {GL, "CollectDataIGB", NULL, h_void},
    {GL, "getDeviceUserAgent", "()" S, h_getDeviceUserAgent},
    {GL, "GetPhoneMemory", "()J", h_GetPhoneMemory},
    {GL, "GetPhoneHeight", "()I", h_GetPhoneHeight},
    {GL, "GetPhoneWidth", "()I", h_GetPhoneWidth},
    {GL, "GetPhoneDII", "()D", h_GetPhoneDII},
    {GL, "GetMaxCPUSpeed", "()F", h_GetMaxCPUSpeed},
    {GL, "GetMaxCPUCore", "()I", h_zero},
    {GL, "GetCPUPartInfo", "()" S, h_GetCPUPartInfo},
    {GL, "GetResProfileName", "()" S, h_GetResProfileName},
    {GL, "RestartGame", "()V", h_RestartGame},
    {GL, "IsMobileConnection", "()Z", h_false},
    {GL, "ExecuteTrackHits", "(" S ")V", h_void},
    {GL, "getOBBFullPath", "()" S, h_getOBBFullPath},
    {GL, "getRegionFormat", "()" S, h_getRegionFormat},
    {GL, "getGameAPIAchivementID", "(I)" S, h_empty},
    {GL, "getGameAPILeaderboardID", "()" S, h_empty},
    {GL, "getPVScaleRate", "()F", h_getPVScaleRate},
    {GL, "GetPhoneIP", "()" S, h_GetPhoneIP},
    {GL, "HasConnectivity", "(Z)I", h_zero},
    {GL, "SendAppToBackground", "()V", h_void},
    {GL, "Exit", "(Z)V", h_Exit},
    {GL, "NoBackWarning", "()V", h_void},
    {GL, "LockSensor", "(Z)V", h_void},
    {GL, "GetGameLanguage", "(" S ")V", h_GetGameLanguage},
    {GL, "OpenBrowser", "(" S ")V", h_log_url},
    {GL, "OpenCustomerCare", "(I)V", h_void},
    {GL, "OpenshowInGameBrowserWithUrl", "(" S ")V", h_log_url},
    {GL, "EnterForum", "()V", h_void},
    {GL, "EnterNews", "()V", h_void},
    {GL, "ComputeNumUnreadNews", "()I", h_zero},
    {GL, "GetSimCountryCode", "()" S, h_empty},
    {GL, "SetRestarting", "(I)V", h_void},
    {GL, "LaunchIGP", "(I)V", h_void},
    {GL, "GetDeviceFirmware", "()" S, h_GetDeviceFirmware},
    {GL, "GetDeviceIdentifier", "()" S, h_GetDeviceIdentifier},
    {GL, "GetFreeSpaceInKBytes", "()I", h_GetFreeSpaceInKBytes},
    {GL, "BeginWelcomeScreen", "(I)V", h_void},
    {GL, "PresentWelcomeScreen", "(I)V", h_void},
    {GL, "FinishLoadWS", "()Z", h_false},
    {GL, "GetProfilesStr", "()" S, h_GetProfilesStr},
    {C_GAME, "KeepScreenOn", "(Z)V", h_void},

    /* ---- SUtils: AndroidOS::Init, GameUtils::init ---- */
    {SU, "getPreferenceString", "(" S S ")" S, h_getPreferenceString},
    {SU, "getPackage", "()" S, h_getPackage},
    {SU, "getSaveFolder", "()" S, h_getSaveFolder},
    {SU, "getContext", "()Landroid/content/Context;", h_getContext},
    {SU, "getUserAgent", "()" S, h_getUserAgent},
    {SU, "shareInfo", NULL, h_void},
    {SU, "playVideo", "(" S "Z)Z", h_false},
    {SU, "stopVideo", "()V", h_void},
    {SU, "inGameVideoSetSkipEnabled", "(Z)V", h_void},
    {SU, "getAssetAsString", "(" S ")[B", h_getAssetAsString},
    {SU, "getGameName", "()" S, h_getGameName},
    {SU, "getInjectedIGP", "()" S, h_empty},
    {SU, "getInjectedSerialKey", "()" S, h_empty},
    {SU, "showCantGoBackPopup", "(I)V", h_void},
    {SU, "getSDFolder", "()" S, h_getSDFolder},
    {SU, "retrieveBarrels", "()[I", h_retrieveBarrels},
    {SU, "getGLUID", "(" S ")[I", h_getGLUID_str},
    {SU, "getMetaDataValue", "(" S ")" S, h_getMetaDataValue},
    {SU, "GetApkPath", "()" S, h_GetApkPath},
    {SU, "initCheckConnectionType", "()I", h_zero},
    {SU, "nativeSetPreference", "(Landroid/os/Bundle;)V", h_nativeSetPreference},
    {SU, "nativeGetPreference", "(Landroid/os/Bundle;)Landroid/os/Bundle;", h_nativeGetPreference},
    {SU, "genericUnzipArchive", "(" S S ")Z", h_genericUnzipArchive},
    {SU, "deleteFile", "(" S ")V", h_deleteFile},
    {SU, "removeDirectoryRecursively", "(" S ")Z", h_removeDirectoryRecursively},
    {SU, "ShowKeyboard", "(" S ")V", h_ShowKeyboard},
    {SU, "HideKeyboard", "()V", h_void},
    {SU, "IsKeyboardVisible", "()Z", h_false},
    {SU, "GetVirtualKeyboardText", "()" S, h_empty},

    /* ---- Device: DeviceUtils::init ---- */
    {DV, "d1", "()" S, h_androidId},
    {DV, "getAndroidId", "()" S, h_androidId},
    {DV, "getSerial", "()" S, h_serial},
    {DV, "getSerialNo", "()" S, h_serial},
    {DV, "getDeviceFirmware", "()" S, h_GetDeviceFirmware},
    {DV, "getMacAddress", "()" S, h_mac},
    {DV, "getDeviceIMEI", "()" S, h_empty},
    {DV, "getHDIDFV", "()" S, h_hdidfv},
    {DV, "getHDIDFVVersion", "()" S, h_hdidfvVersion},
    {DV, "getGoogleAdId", "()" S, h_empty},
    {DV, "getGoogleAdIdStatus", "()I", h_zero},
    {DV, "getGLDID", "()" S, h_gldid},
    {DV, "getDeviceName", "()" S, h_deviceName},
    {DV, "getPhoneManufacturer", "()" S, h_manufacturer},
    {DV, "getPhoneModel", "()" S, h_model},
    {DV, "retrieveDeviceCarrier", "()" S, h_empty},
    {DV, "retrieveDeviceCountry", "()" S, h_country},
    {DV, "retrieveDeviceRegion", "()" S, h_country},
    {DV, "retrieveDeviceLanguage", "()" S, h_language},
    {DV, "retrieveCPUSerial", "()" S, h_serial},
    {DV, "getPhoneDevice", "()" S, h_product},
    {DV, "getPhoneProduct", "()" S, h_product},

    /* ---- DataSharing ---- */
    {C_DATASHARE, "setSharedValue", "(" S S ")V", h_ds_set},
    {C_DATASHARE, "getSharedValue", "(" S ")" S, h_ds_get},
    {C_DATASHARE, "deleteSharedValue", "(" S ")V", h_ds_delete},
    {C_DATASHARE, "isSharedValue", "(" S ")Z", h_ds_is},

    /* ---- SendInfo (initMethods) ---- */
    {SI, "getSaveFolder", "()" S, h_getSaveFolder},
    /* The social framework's cache folder is this + "/sf_cache" (getSD_path):
     * unanswered, it was "/sf_cache", at the SD card's root. */
    {SI, "getSDFolder", "()" S, h_getSDFolder},
    {SI, "getGLUID", "()[I", h_sendinfo_getGLUID},
    {SI, "getGLDID", "()" S, h_androidId},
    {SI, "getPhoneCarrier", "()" S, h_empty},
    {SI, "getLocaleCountry", "()" S, h_country_lower},
    {SI, "getLocaleLanguage", "()" S, h_locale},
    {SI, "getManufacturerModel", "()" S, h_manufacturerModel},
    {SI, "getMacAddress", "()" S, h_mac},

    /* ---- push notifications (SimplifiedPN::init): none ---- */
    {C_PUSH, "GetDeviceToken", "(I)I", h_zero},
    {C_PUSH, "IsEnable", "()Z", h_false},
    {C_PUSH, "IsAppLaunchedFromPN", "()" S, h_empty},
    {C_PUSH, "SendMessage", "(Landroid/os/Bundle;" S "I)I", h_zero},
    {C_PUSH, "DeleteMessageGroup", "(I)I", h_zero},

    /* ---- android/os/Bundle ---- */
    {"android/os/Bundle", "<init>", "()V", h_bundle_init},
    {"android/os/Bundle", "putString", "(" S S ")V", h_bundle_putString},
    {"android/os/Bundle", "getString", "(" S ")" S, h_bundle_getString},
    {"android/os/Bundle", "getInt", "(" S ")I", h_bundle_getInt},
    {"android/os/Bundle", "putInt", "(" S "I)V", h_bundle_putInt},
    {"android/os/Bundle", "getLong", "(" S ")J", h_bundle_getLong},
    {"android/os/Bundle", "putLong", "(" S "J)V", h_bundle_putLong},
    {"android/os/Bundle", "getBoolean", "(" S ")Z", h_bundle_getBoolean},
    {"android/os/Bundle", "putBoolean", "(" S "Z)V", h_bundle_putBoolean},
    {"android/os/Bundle", "containsKey", "(" S ")Z", h_bundle_containsKey},
    {"android/os/Bundle", "clear", "()V", h_bundle_clear},
    {"android/os/Bundle", "getByteArray", "(" S ")[B", h_bundle_getByteArray},
    {"android/os/Bundle", "putByteArray", "(" S "[B)V", h_bundle_putByteArray},

    /* glf::Thread::Impl::SetPriority: the port schedules its threads itself */
    {"android/os/Process", "setThreadPriority", "(I)V", h_void},
    {NULL, NULL, NULL, NULL},
};

/* AndroidOS::GetSDFolder reads GameInstaller.mPreferencesName */
static jvalue f_prefs_name(JObj *self, const JField *f) { return jv_l(jni_str("GamePrefs")); }

const JFieldDef jni_field_defs[] = {
    {C_INSTALLER, "mPreferencesName", f_prefs_name, 0, NULL},
    {NULL, NULL, NULL, 0, NULL},
};

const char *const jni_class_supers[][2] = {
    {C_GAME, "android/app/Activity"},
    {"android/app/Activity", "android/view/ContextThemeWrapper"},
    {"android/view/ContextThemeWrapper", "android/content/ContextWrapper"},
    {"android/content/ContextWrapper", "android/content/Context"},
    {"java/nio/DirectByteBuffer", "java/nio/ByteBuffer"},
    {NULL, NULL},
};

/* Only used when classes.txt (the APK's class list) is missing. */
const char *const jni_missing_classes[] = {NULL};

/* ================================================================ setup */
void a8r_java_init(void) {
  jni_init();
  g_jni_log = dcr_config()->log_jni;
  g_activity = jni_singleton(C_GAME);
  identity_init();
  (void)h_true;
  debugPrintf("[java] objects ready: activity %p; SD folder %s\n", (void *)g_activity, sd_folder());
}
