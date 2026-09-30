/* a8r_menu_audio.c -- the start screen's music and button sounds.
 *
 * As the mod's MainActivity: its music is one of five of the APK's
 * assets/m_*.mp3 at random, another when it ends (playRandomMusic); its
 * sounds are assets/sfx_menu_{confirm,back,denied,woosh}.mp3. Both read from
 * the user's A8R.apk (a8r_menu.c hands the bytes over), decoded with minimp3
 * and mixed here into the port's audout (opensles.c, the one the game plays
 * through afterwards): a thread of its own mixes 1024-frame buffers at the
 * output rate, and audout's queue paces it. MIT.
 */
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include "thirdparty/minimp3.h"

#include "a8r.h"
#include "a8r_menu_audio.h"
#include "util.h"

typedef struct {
  int16_t *pcm; /* stereo */
  int frames, rate;
} Clip;

static Clip g_sfx[SFX_COUNT];
static struct {
  const Clip *clip;
  double pos;
} g_voice[4];

/* the music: decoded a frame at a time into a small queue */
static const uint8_t *g_mp3;
static size_t g_mp3_len, g_mp3_pos;
static mp3dec_t g_dec;
static int16_t g_mq[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
static int g_mq_n, g_mq_rate = 44100;
static double g_mq_pos;         /* in g_mq; -1..0: between g_prev and g_mq[0] */
static int16_t g_prev[2];       /* the last frame of the previous batch */
static int g_music_on, g_effects_on;
static uint8_t *(*g_next_track)(size_t *len);

static Mutex g_mx;
static Thread g_thread;
static volatile int g_run, g_up;

static int decode_all(const uint8_t *mp3, size_t len, Clip *out) {
  mp3dec_t d;
  mp3dec_init(&d);
  mp3dec_frame_info_t fi;
  int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
  int cap = 0, n = 0;
  int16_t *buf = NULL;
  size_t pos = 0;
  out->rate = 44100;
  while (pos < len) {
    int s = mp3dec_decode_frame(&d, mp3 + pos, (int)(len - pos), pcm, &fi);
    if (!fi.frame_bytes)
      break;
    pos += (size_t)fi.frame_bytes;
    if (!s)
      continue;
    out->rate = fi.hz;
    if (n + s > cap) {
      cap = (n + s) * 2;
      int16_t *nb = realloc(buf, (size_t)cap * 4);
      if (!nb)
        break;
      buf = nb;
    }
    for (int i = 0; i < s; i++) {
      buf[(n + i) * 2] = pcm[i * fi.channels];
      buf[(n + i) * 2 + 1] = pcm[i * fi.channels + (fi.channels > 1)];
    }
    n += s;
  }
  out->pcm = buf;
  out->frames = n;
  return n > 0 ? 0 : -1;
}

/* next music frame into g_mq; 0 at the end of the track */
static int music_refill(void) {
  mp3dec_frame_info_t fi;
  int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
  while (g_mp3 && g_mp3_pos < g_mp3_len) {
    int s = mp3dec_decode_frame(&g_dec, g_mp3 + g_mp3_pos, (int)(g_mp3_len - g_mp3_pos), pcm, &fi);
    if (!fi.frame_bytes)
      break;
    g_mp3_pos += (size_t)fi.frame_bytes;
    if (!s)
      continue;
    for (int i = 0; i < s; i++) {
      g_mq[i * 2] = pcm[i * fi.channels];
      g_mq[i * 2 + 1] = pcm[i * fi.channels + (fi.channels > 1)];
    }
    g_mq_n = s;
    g_mq_rate = fi.hz;
    return 1;
  }
  return 0;
}

static void next_track(void) {
  free((void *)g_mp3);
  g_mp3 = NULL;
  g_mq_n = 0;
  g_mq_pos = 0;
  if (!g_next_track)
    return;
  size_t len = 0;
  g_mp3 = g_next_track(&len);
  g_mp3_len = len;
  g_mp3_pos = 0;
  mp3dec_init(&g_dec);
}

static void mix(int32_t *acc, int frames, unsigned rate) {
  mutexLock(&g_mx);
  if (g_music_on) {
    for (int f = 0; f < frames; f++) {
      if (!g_mp3)
        next_track();
      if (!g_mp3)
        break;
      while (g_mq_pos >= g_mq_n - 1) {
        if (g_mq_n > 0) {
          g_prev[0] = g_mq[(g_mq_n - 1) * 2];
          g_prev[1] = g_mq[(g_mq_n - 1) * 2 + 1];
          g_mq_pos -= g_mq_n;
        }
        if (!music_refill()) {
          next_track(); /* the mod's playRandomMusic on completion */
          if (!g_mp3 || !music_refill())
            break;
        }
      }
      if (!g_mp3 || g_mq_n < 2)
        break;
      int i = g_mq_pos < 0 ? -1 : (int)g_mq_pos;
      double t = g_mq_pos - i;
      for (int c = 0; c < 2; c++) {
        double a = i < 0 ? g_prev[c] : g_mq[i * 2 + c];
        double v = a + (g_mq[(i + 1) * 2 + c] - a) * t;
        acc[f * 2 + c] += (int32_t)(v * 0.55); /* under the button sounds */
      }
      g_mq_pos += (double)g_mq_rate / rate;
    }
  }
  for (unsigned v = 0; v < sizeof g_voice / sizeof g_voice[0]; v++) {
    const Clip *c = g_voice[v].clip;
    if (!c)
      continue;
    double step = (double)c->rate / rate;
    for (int f = 0; f < frames; f++) {
      int i = (int)g_voice[v].pos;
      if (i + 1 >= c->frames) {
        g_voice[v].clip = NULL;
        break;
      }
      double t = g_voice[v].pos - i;
      for (int ch = 0; ch < 2; ch++)
        acc[f * 2 + ch] += (int32_t)(c->pcm[i * 2 + ch] + (c->pcm[(i + 1) * 2 + ch] - c->pcm[i * 2 + ch]) * t);
      g_voice[v].pos += step;
    }
  }
  mutexUnlock(&g_mx);
}

static void audio_thread(void *arg) {
  static int32_t acc[DCR_AUDIO_FRAMES * 2];
  static int16_t out[DCR_AUDIO_FRAMES * 2];
  unsigned rate = dcr_audio_out_rate();
  while (g_run) {
    memset(acc, 0, sizeof acc);
    mix(acc, DCR_AUDIO_FRAMES, rate);
    for (int i = 0; i < DCR_AUDIO_FRAMES * 2; i++)
      out[i] = (int16_t)(acc[i] > 32767 ? 32767 : acc[i] < -32768 ? -32768 : acc[i]);
    dcr_audio_out_submit(out); /* blocks while audout's queue is full: the pacing */
  }
  g_up = 0;
}

int menu_audio_start(const uint8_t *const sfx[SFX_COUNT], const size_t sfx_len[SFX_COUNT],
                     uint8_t *(*next_track_fn)(size_t *len), int effects, int music) {
  mutexInit(&g_mx);
  if (dcr_audio_out_open() != 0)
    return -1;
  for (int i = 0; i < SFX_COUNT; i++)
    if (sfx[i])
      decode_all(sfx[i], sfx_len[i], &g_sfx[i]);
  g_next_track = next_track_fn;
  g_effects_on = effects;
  g_music_on = music;
  g_run = 1;
  if (R_FAILED(threadCreate(&g_thread, audio_thread, NULL, NULL, 0x8000, 0x2B, 2))) {
    g_run = 0;
    return -1;
  }
  g_up = 1;
  threadStart(&g_thread);
  return 0;
}

void menu_audio_sfx(int which) {
  if (!g_up || !g_effects_on || which < 0 || which >= SFX_COUNT || !g_sfx[which].pcm)
    return;
  mutexLock(&g_mx);
  unsigned best = 0;
  for (unsigned v = 0; v < sizeof g_voice / sizeof g_voice[0]; v++)
    if (!g_voice[v].clip || g_voice[v].pos > g_voice[best].pos)
      best = v;
  g_voice[best].clip = &g_sfx[which];
  g_voice[best].pos = 0;
  mutexUnlock(&g_mx);
}

void menu_audio_set(int effects, int music) {
  mutexLock(&g_mx);
  g_effects_on = effects;
  if (!music && g_music_on) {
    free((void *)g_mp3);
    g_mp3 = NULL;
  }
  g_music_on = music;
  mutexUnlock(&g_mx);
}

void menu_audio_stop(void) {
  if (g_run) {
    g_run = 0;
    threadWaitForExit(&g_thread);
    threadClose(&g_thread);
  }
  free((void *)g_mp3);
  g_mp3 = NULL;
  for (int i = 0; i < SFX_COUNT; i++) {
    free(g_sfx[i].pcm);
    g_sfx[i].pcm = NULL;
  }
  memset(g_voice, 0, sizeof g_voice);
}
