/* dcr_prefs.h -- SharedPreferences (Unity PlayerPrefs) store; see dcr_prefs.c. */
#ifndef DCR_PREFS_H
#define DCR_PREFS_H

typedef struct DcrPrefs DcrPrefs;
typedef struct DcrPrefsEdit DcrPrefsEdit;

DcrPrefs *dcr_prefs_open(const char *name);
/* Copy of the value text (free() it) and its type ('s','i','l','f','b'), or
 * NULL if absent. */
char *dcr_prefs_get(DcrPrefs *p, const char *key, char *type_out);
int dcr_prefs_count(DcrPrefs *p);
int dcr_prefs_entry(DcrPrefs *p, int i, const char **key, const char **val, char *type);

DcrPrefsEdit *dcr_prefs_edit(DcrPrefs *p);
void dcr_prefs_put(DcrPrefsEdit *e, char type, const char *key, const char *val);
void dcr_prefs_remove(DcrPrefsEdit *e, const char *key);
void dcr_prefs_clear(DcrPrefsEdit *e);
int dcr_prefs_commit(DcrPrefsEdit *e);   /* apply + write to the SD card */
void dcr_prefs_edit_free(DcrPrefsEdit *e);

#endif
