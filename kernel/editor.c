#include "include/keyboard.h"
#include "include/fs.h"
#include "include/lib.h"
#include "include/stddef.h"
#include "include/stdint.h"

#define MAX_LINES 23
#define MAX_LINE_LEN 80

static char text[MAX_LINES][MAX_LINE_LEN];
static int line_count = 0;
static int cursor_x = 0;
static int cursor_y = 0;

static void sleep_ticks(int ticks) {
    for (volatile int i = 0; i < ticks * 100000; i++);
}

void editor_load(const char *filename) {
    for (int i = 0; i < MAX_LINES; i++) {
        text[i][0] = '\0';
    }

    char buf[512] = {0};
    int size = fs_read(filename, buf, sizeof(buf) - 1);
    if (size <= 0) {
        line_count = 1;
        text[0][0] = '\0';
        return;
    }

    buf[size] = '\0';
    line_count = 0;

    char *line = buf;
    while (line && line_count < MAX_LINES) {
        char *newline = strchr(line, '\n');
        if (newline) {
            *newline = '\0';
            strlcpy(text[line_count], line, MAX_LINE_LEN);
            line = newline + 1;
        } else {
            strlcpy(text[line_count], line, MAX_LINE_LEN);
            line = NULL;
        }
        line_count++;
    }

    if (line_count == 0) {
        line_count = 1;
        text[0][0] = '\0';
    }
}

void editor_save(const char *filename) {
    char buf[2048] = {0};
    size_t pos = 0;

    for (int i = 0; i < line_count && pos < sizeof(buf) - 1; i++) {
        size_t len = strlen(text[i]);
        if (pos + len + 1 >= sizeof(buf)) break;

        memcpy(buf + pos, text[i], len);
        pos += len;

        buf[pos++] = '\n';
    }

    fs_write(filename, buf, pos);
}

void editor_draw_text() {
    clear_screen();
    
    for (int i = 0; i < line_count; i++) {
        set_cursor_position(0, i);
        print(text[i]);

        int len = strlen(text[i]);
        for (int j = len; j < VGA_WIDTH; j++) {
            print(" ");
        }
    }
}

void editor_draw_ui() {
    set_cursor_position(0, MAX_LINES);
    for (int i = 0; i < VGA_WIDTH; i++) {
        print(" ");
    }

    set_cursor_position(0, MAX_LINES);
    print("F1:Save  ESC:Exit");
}

void editor_show_message(const char *msg) {
    set_cursor_position(0, MAX_LINES + 1);
    for (int i = 0; i < VGA_WIDTH; i++) {
        print(" ");
    }

    set_cursor_position(0, MAX_LINES + 1);
    print(msg);
}

void edit_file(const char *filename) {
    cursor_x = 0;
    cursor_y = 0;
    editor_load(filename);
    int running = 1;

    clear_screen();
    editor_draw_text();
    editor_draw_ui();
    set_cursor_position(cursor_x, cursor_y);

    while (running) {
        int key = keyboard_getkey();
        if (key == -1) continue;

        int text_modified = 0;
        int ui_modified = 0;

        if (key == 27) {
            running = 0;
        } else if (key == 0x3B) {
            editor_save(filename);
            editor_show_message("File saved!");
            ui_modified = 1;
            sleep_ticks(5);
        } else if (key == KEY_LEFT && cursor_x > 0) {
            cursor_x--;
        } else if (key == KEY_RIGHT && cursor_x < (int)strlen(text[cursor_y])) {
            cursor_x++;
        } else if (key == KEY_UP && cursor_y > 0) {
            cursor_y--;
            if (cursor_x > (int)strlen(text[cursor_y])) {
                cursor_x = strlen(text[cursor_y]);
            }
        } else if (key == KEY_DOWN && cursor_y < line_count - 1) {
            cursor_y++;
            if (cursor_x > (int)strlen(text[cursor_y])) {
                cursor_x = strlen(text[cursor_y]);
            }
        } else if (key == '\n') {
            if (line_count < MAX_LINES - 1) {
                if (cursor_x < (int)strlen(text[cursor_y])) {
                    strlcpy(text[cursor_y + 1], text[cursor_y] + cursor_x, MAX_LINE_LEN);
                    text[cursor_y][cursor_x] = '\0';
                }
                cursor_y++;
                cursor_x = 0;
                line_count++;
                text_modified = 1;
            }
        } else if (key == '\b') {
            if (cursor_x > 0) {
                memmove(&text[cursor_y][cursor_x - 1], &text[cursor_y][cursor_x],
                        strlen(&text[cursor_y][cursor_x]) + 1);
                cursor_x--;
                text_modified = 1;
            } else if (cursor_y > 0) {
                int prev_len = strlen(text[cursor_y - 1]);
                if (prev_len + strlen(text[cursor_y]) < MAX_LINE_LEN) {
                    strcat(text[cursor_y - 1], text[cursor_y]);
                    for (int i = cursor_y; i < line_count - 1; i++) {
                        strlcpy(text[i], text[i + 1], MAX_LINE_LEN);
                    }
                    text[line_count - 1][0] = '\0';
                    cursor_y--;
                    cursor_x = prev_len;
                    line_count--;
                    text_modified = 1;
                }
            }
        } else if (key >= 32 && key <= 126) {
            size_t len = strlen(text[cursor_y]);
            if (len < MAX_LINE_LEN - 1) {
                if (cursor_x < (int)len) {
                    memmove(&text[cursor_y][cursor_x + 1], &text[cursor_y][cursor_x],
                            len - cursor_x + 1);
                }
                text[cursor_y][cursor_x] = (char)key;
                cursor_x++;
                text_modified = 1;
            }
        }

        if (text_modified) {
            editor_draw_text();
            editor_draw_ui();
            text_modified = 0;
        }

        if (ui_modified) {
            editor_draw_ui();
            ui_modified = 0;
        }

        set_cursor_position(cursor_x, cursor_y);
    }

    editor_save(filename);
    editor_show_message("Saving file...");
    sleep_ticks(5);
}