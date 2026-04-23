#include <stdio.h>
#include <string.h>

void get_swapname(const char *b_fname, char *swapname) {
    char *slash = strrchr(b_fname, '/');
    size_t written = 0;

    if (slash) {
        /* Copy path up to and including the separator */
        written = snprintf(swapname, 256, "%.*s", (int)(slash - b_fname + 1), b_fname);
        if (written < 256) {
            /* Append the rest of the filename + ".swp" */
            written += snprintf(swapname + written, 256 - written, ".%s.swp", slash + 1);
        }
    } else {
        /* Append "." + filename + ".swp" */
        written = snprintf(swapname, 256, ".%s.swp", b_fname);
    }

    /* Check for truncation */
    if (written >= 256) {
        /* Handle error or warning if swapname construction truncated */
        /* For now, just ensure it's null-terminated if truncated */
        swapname[255] = '\0';
        /* Optionally, mlwrite("(Warning: Swap file name truncated)"); */
    }
}

int main() {
    char out[256];
    get_swapname("file.txt", out);
    printf("%s\n", out);
    get_swapname("/home/user/file.txt", out);
    printf("%s\n", out);
    return 0;
}
