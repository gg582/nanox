#ifndef PLATFORM_H_
#define PLATFORM_H_

#include <stddef.h>
#include <stdbool.h>

void nanox_get_user_data_dir(char *out, size_t cap);
void nanox_get_user_config_dir(char *out, size_t cap);
void nanox_path_join(char *out, size_t cap, const char *a, const char *b);
bool nanox_file_exists(const char *path);
void nanox_normalize_path(char *path);
const char *nanox_getenv(const char *name);

#endif /* PLATFORM_H_ */
