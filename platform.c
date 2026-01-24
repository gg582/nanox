#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef USE_WINDOWS
#include <windows.h>
#include <direct.h>
#define PATH_SEP '\\'
#define PATH_SEP_STR "\\"
#else
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#define PATH_SEP '/'
#define PATH_SEP_STR "/"
#endif

const char *nanox_getenv(const char *name) {
    return getenv(name);
}

void nanox_get_user_data_dir(char *out, size_t cap) {
#ifdef USE_WINDOWS
    const char *local_app_data = getenv("LOCALAPPDATA");
    if (local_app_data) {
        snprintf(out, cap, "%s\\nanox", local_app_data);
    } else {
        const char *app_data = getenv("APPDATA");
        if (app_data) {
            snprintf(out, cap, "%s\\nanox", app_data);
        } else {
            const char *user_profile = getenv("USERPROFILE");
            if (user_profile) {
                snprintf(out, cap, "%s\\.local\\share\\nanox", user_profile);
            } else {
                snprintf(out, cap, "C:\\nanox");
            }
        }
    }
#else
    const char *xdg_data = getenv("XDG_DATA_HOME");
    if (xdg_data && *xdg_data) {
        snprintf(out, cap, "%s/nanox", xdg_data);
    } else {
        const char *home = getenv("HOME");
        if (!home) {
            struct passwd *pw = getpwuid(getuid());
            if (pw) home = pw->pw_dir;
        }
        if (home) {
            snprintf(out, cap, "%s/.local/share/nanox", home);
        } else {
            snprintf(out, cap, "/tmp/nanox");
        }
    }
#endif
}

void nanox_get_user_config_dir(char *out, size_t cap) {
#ifdef USE_WINDOWS
    nanox_get_user_data_dir(out, cap); /* Same for Windows usually */
#else
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config && *xdg_config) {
        snprintf(out, cap, "%s/nanox", xdg_config);
    } else {
        const char *home = getenv("HOME");
        if (!home) {
            struct passwd *pw = getpwuid(getuid());
            if (pw) home = pw->pw_dir;
        }
        if (home) {
            snprintf(out, cap, "%s/.config/nanox", home);
        } else {
            snprintf(out, cap, "/tmp/nanox");
        }
    }
#endif
}

void nanox_path_join(char *out, size_t cap, const char *a, const char *b) {
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    
    if (len_a + len_b + 2 > cap) {
        // Truncate or handle error - for now, just safe copy what fits
        if (cap > 0) out[0] = '\0';
        return;
    }

    if (out != a) {
        strcpy(out, a);
    }
    
    bool a_ends_sep = (len_a > 0 && out[len_a - 1] == PATH_SEP);
    bool b_starts_sep = (len_b > 0 && b[0] == PATH_SEP);

    if (a_ends_sep && b_starts_sep) {
        strcat(out, b + 1);
    } else if (!a_ends_sep && !b_starts_sep) {
        strcat(out, PATH_SEP_STR);
        strcat(out, b);
    } else {
        strcat(out, b);
    }
}

bool nanox_file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

void nanox_normalize_path(char *path) {
    if (!path) return;
    for (char *p = path; *p; p++) {
#ifdef USE_WINDOWS
        if (*p == '/') *p = '\\';
#else
        if (*p == '\\') *p = '/';
#endif
    }
}
