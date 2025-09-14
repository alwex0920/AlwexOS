#include "include/fs.h"
#include "include/lib.h"
#include "include/dialogs.h"

int strcasecmp_custom(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? *s1 + 32 : *s1;
        char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? *s2 + 32 : *s2;
        if (c1 != c2) return c1 - c2;
        s1++; s2++;
    }
    return *s1 - *s2;
}

int strstr_case_insensitive(const char *haystack, const char *needle) {
    int nlen = strlen(needle);
    if (nlen == 0) return 1;

    for (int i = 0; haystack[i]; i++) {
        int j = 0;
        while (haystack[i + j] &&
               ((haystack[i + j] | 32) == (needle[j] | 32))) {
            j++;
            if (needle[j] == 0) return 1;
        }
    }
    return 0;
}

void ai_init() {
    print("AI: dialog base loaded\n");
}

void ai_handle(const char *input) {
    for (int i = 0; i < dialogs_count; i++) {
        if (strcasecmp_custom(input, dialogs[i].question) == 0) {
            print("AI: ");
            print(dialogs[i].answer);
            print("\n");
            return;
        }
    }

    for (int i = 0; i < dialogs_count; i++) {
        if (strstr_case_insensitive(input, dialogs[i].question)) {
            print("AI: ");
            print(dialogs[i].answer);
            print("\n");
            return;
        }
    }

    for (int i = 0; i < dialogs_count; i++) {
        if (strcmp(dialogs[i].question, "default") == 0) {
            print("AI: ");
            print(dialogs[i].answer);
            print("\n");
            return;
        }
    }
}
