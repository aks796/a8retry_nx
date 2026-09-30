/* zips.h -- installing the game's files from the A8R zips (zips.c). */
#ifndef LAUNCHER_ZIPS_H
#define LAUNCHER_ZIPS_H

#define GAME_DIR "sdmc:/switch/a8retry_nx"

/* Installs the files of the A8R zips copied into GAME_DIR, printing what it
 * does. 0 when there were none or all went in; -1 when something went wrong
 * (said on screen: the zips not yet installed are kept). */
int zips_install(void);

/* The progress screen (as the game program's, source/util.c): the game's name,
 * why, a green bar (0..1000) and the step, redrawn when either changes.
 * launcher_bar_off() clears it for text again (errors, instructions). */
void launcher_bar(const char *what, int permille);
void launcher_bar_off(void);
int launcher_bar_on(void);

#endif
