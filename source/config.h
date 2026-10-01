/* config.h -- the game's own constants for the Asphalt 8: Airborne Retry
 * port. The runtime's (the folder, the package, the module region...) are in
 * port_config.h.
 *
 * Asphalt 8: Airborne (com.gameloft.android.HEP.GloftA8HP, Gameloft's jet/glf
 * engine, armeabi-v7a) as modified by the "Asphalt 8: Retry" mod (A8R,
 * December build). MIT.
 */
#ifndef A8R_CONFIG_H
#define A8R_CONFIG_H

#include "rt_settings.h"

#define A8R_PACKAGE     PORT_PACKAGE

/* The engine. The mod ships it as 71 pieces hidden in five byte-scrambled zips
 * among its assets; the wrapper puts them together on the first launch
 * (a8r_setup.c), with the pieces of the options the user picked. */
#define A8R_LIB_GAME    "libasphalt8.so"

/* The game data folder, exactly as the mod's instructions lay it out on a phone
 * (/storage/emulated/0/gameloft/games/GloftA8HP): the user copies the
 * "gameloft" folder of A8R DATA.zip, with A8R DATA December.zip extracted over
 * games/GloftA8HP, into the game folder. */
#define A8R_DATA_DIR    "gameloft/games/GloftA8HP"
#define A8R_ANDROID_SD  "/storage/emulated/0"
#define A8R_ANDROID_DATA A8R_ANDROID_SD "/" A8R_DATA_DIR
#define A8R_OBB_NAME    "main.sa2.Asphalt8.obb"

#endif /* A8R_CONFIG_H */
