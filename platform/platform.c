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
    if (cap == 0) return; // Cannot write to a zero-sized buffer
    out[0] = '\0'; // Initialize out to an empty string

    // Safely copy 'a'
    size_t written = snprintf(out, cap, "%s", a);

    // Check for truncation during copy of 'a'
    if (written >= cap) {
        // Buffer too small, out is already null-terminated by snprintf
        return;
    }

    // Determine if a separator is needed
    // Check the actual last character written to 'out'
    bool a_ends_sep = (written > 0 && out[written - 1] == PATH_SEP);
    bool b_starts_sep = (b != NULL && *b != '\0' && b[0] == PATH_SEP); // Check b is not null and has content

    // Append separator if needed and there's space
    if (!a_ends_sep && !b_starts_sep) {
        if (cap - written > 1) { // Ensure space for separator + null terminator
            written += snprintf(out + written, cap - written, "%c", PATH_SEP);
            if (written >= cap) {
                // Buffer too small after adding separator
                return;
            }
        } else {
            // Not enough space for separator and null terminator
            return;
        }
    } else if (a_ends_sep && b_starts_sep) {
        // If 'a' ends with separator and 'b' starts with one, skip 'b's separator
        // This condition might need refinement depending on exact requirements
        // For now, assume we skip the first char of 'b' if it's a separator
        b++; // Move pointer to skip the leading separator in b
    }
    // else if (!a_ends_sep && b_starts_sep) or (a_ends_sep && !b_starts_sep), no extra separator needed

    // Append 'b'
    if (b != NULL && *b != '\0') { // Ensure b is not NULL and not empty after potential skip
        if (cap - written > strlen(b)) { // Check if there's enough space for 'b' + null terminator
            written += snprintf(out + written, cap - written, "%s", b);
            if (written >= cap) {
                // Buffer too small after adding b
                return;
            }
        } else {
            // Not enough space for b and null terminator
            return;
        }
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
