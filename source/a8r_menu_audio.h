/* a8r_menu_audio.h -- the start screen's music and button sounds. */
#ifndef A8R_MENU_AUDIO_H
#define A8R_MENU_AUDIO_H

#include <stddef.h>
#include <stdint.h>

enum { SFX_CONFIRM, SFX_BACK, SFX_DENIED, SFX_WOOSH, SFX_COUNT };

/* sfx: the four MP3s (kept by the caller until menu_audio_stop); next_track:
 * a new random music MP3 (malloc'd; this frees it). 0 = playing. */
int menu_audio_start(const uint8_t *const sfx[SFX_COUNT], const size_t sfx_len[SFX_COUNT],
                     uint8_t *(*next_track)(size_t *len), int effects, int music);
void menu_audio_sfx(int which);
void menu_audio_set(int effects, int music);
void menu_audio_stop(void);

#endif
