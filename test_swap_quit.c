#include <stdio.h>
#include <string.h>

void get_swapname(const char *b_fname, char *swapname) {
    char *slash = strrchr(b_fname, '/');
    if (slash) {
        strncpy(swapname, b_fname, slash - b_fname + 1);
        swapname[slash - b_fname + 1] = '\0';
        strcat(swapname, ".");
        strcat(swapname, slash + 1);
        strcat(swapname, ".swp");
    } else {
        strcpy(swapname, ".");
        strcat(swapname, b_fname);
        strcat(swapname, ".swp");
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
