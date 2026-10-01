/* a8r_menu.h -- the A8R start screen (a8r_menu.c). */
#ifndef A8R_MENU_H
#define A8R_MENU_H

/* The start screen, until PLAY (1) or its exit question (0). Its choices are
 * in config.ini (dcr_config_save) when it returns. */
int a8r_menu_run(const char *apk);
/* After PLAY the start screen keeps the window and draws setup's progress
 * screen (the runtime's, log_progress_set_renderer: a8r_setup.c's steps)
 * until a8r_menu_done gives it up for the game. Nothing when the start screen
 * was not shown. */
void a8r_menu_done(void);
/* The mod's BACKUP: data/files (but *.txt) to gameloft/games/GloftA8HP/save;
 * the files copied. Also the auto backup's, while playing (a8r_boot.c). */
int a8r_backup_progress(void);

#endif
