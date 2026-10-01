/* zips.h -- installing the game's files from the A8R zips (zips.c), the
 * launcher's port_launcher_prepare (a8r_launcher.c). */
#ifndef LAUNCHER_ZIPS_H
#define LAUNCHER_ZIPS_H

#include "rt_settings.h"

#define GAME_DIR "sdmc:" PORT_ROOT_PATH

/* Installs the files of the A8R zips copied into GAME_DIR, printing what it
 * does (the shared launcher's progress screen while it extracts). 0 when
 * there were none or all went in; -1 when something went wrong (said on
 * screen: the zips not yet installed are kept). */
int zips_install(void);

#endif
