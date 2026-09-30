/* a8r_arsc.h -- the start screen's texts, from A8R.apk's resources.arsc. */
#ifndef A8R_ARSC_H
#define A8R_ARSC_H

#include <stddef.h>
#include <stdint.h>

/* Keeps every saveload_* string of the table, per language. Returns how many. */
int a8r_arsc_load(const uint8_t *arsc, size_t len);
/* "en" (the default), "es", "fr", "pt", "ru", "vi" or "pl". */
void a8r_arsc_set_lang(const char *lang);
/* The string in the language set (else the default); "" if the APK has none. */
const char *a8r_str(const char *name);

#endif
