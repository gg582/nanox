#ifndef SCRAPER_H
#define SCRAPER_H

typedef enum {
    SCRAPER_LANG_PYTHON = 0,
    SCRAPER_LANG_NODE,
    SCRAPER_LANG_COUNT
} scraper_lang_t;

typedef void (*scraper_symbol_cb)(const char *symbol, void *userdata);

void scraper_init(void);
void scraper_cleanup(void);
int scraper_iterate_symbols(scraper_lang_t lang, const char *module,
                            scraper_symbol_cb cb, void *userdata);

#endif /* SCRAPER_H */
