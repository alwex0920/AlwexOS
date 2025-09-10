#include "include/fs.h"
#include "include/lib.h"
#include "include/keyboard.h"
#include "include/editor.h"
#include "include/ahci.h"
#include "include/run.h"
#include "include/ai.h"

void shell_main() {
    char input[64];
    keyboard_init();
    print("AlwexOS\n");

    while (1) {
        print("[");
        print(getcwd());
        print("] > ");

        int len = safe_readline(input, sizeof(input));

        if (len == 0) {
            continue;
        }
        
        if (strcmp(input, "help") == 0) {
            print("Cmds:\n");
            print("cat [name]: show the text in the file\n");
            print("edit [name]: edit file\n");
            print("run [name]: run a program file\n");
            print("clr: clear the screen\n");
            print("echo [text]: output text\n");
            print("poweroff: turn off the system\n");
            print("reboot: restart the system\n");
            print("create-file [name]: create a file\n");
            print("delete-file [name]: delete file\n");
            print("create-dir [name]: create a directory\n");
            print("delete-dir [name]: delete directory\n");
            print("cd [path]: change directory\n");
            print("list: list of files\n");
            print("tree: show the file system tree\n");
        }
        else if (strcmp(input, "clr") == 0) {
            clear_screen();
        }
        else if (strncmp(input, "echo ", 5) == 0) {
            print(input + 5);
            print("\n");
        }
        else if (strcmp(input, "poweroff") == 0) {
            poweroff();
        }
        else if (strcmp(input, "reboot") == 0) {
            reboot();
        }
        else if (len > 12 && strncmp(input, "create-file ", 12) == 0) {
            const char* name = input + 12;
            if (create_file(name)) {
                print("File creation error\n");
            } else {
                print("The file has been created\n");
            }
        }
        else if (strncmp(input, "delete-file ", 12) == 0) {
            const char* name = input + 12;
            if (delete_file(name)) {
                print("File deletion error\n");
            } else {
                print("The file has been deleted\n");
            }
        }
        else if (strncmp(input, "create-dir ", 11) == 0) {
            const char* name = input + 11;
            if (create_dir(name)) {
                print("Directory creation error\n");
            } else {
                print("The directory has been created\n");
            }
        }
        else if (strncmp(input, "delete-dir ", 11) == 0) {
            const char* name = input + 11;
            if (delete_dir(name)) {
                print("Error deleting a directory\n");
            } else {
                print("The directory has been deleted\n");
            }
        }
        else if (strncmp(input, "cd ", 3) == 0) {
            const char *path = input + 3;
            if (chdir(path) != 0) {
                print("Directory not found: ");
                print(path);
                print("\n");
            }
        } 
        else if (strcmp(input, "cd") == 0) {
            chdir("/");
        }
        else if (strncmp(input, "edit ", 5) == 0) {
            edit_file(input + 5);
        }
        else if (strncmp(input, "cat ", 4) == 0) {
            const char* name = input + 4;
            char buffer[1024];
            int size = fs_read(name, buffer, sizeof(buffer) - 1);
            if (size <= 0) {
                print("Error reading file\n");
            } else {
                buffer[size] = '\0';
                print(buffer);
                print("\n");
            }
        }
        else if (strncmp(input, "run ", 4) == 0) {
            const char* filename = input + 4;
            run_program(filename);
        }
        else if (strcmp(input, "list") == 0) {
            list_files();
        }
        else if (strcmp(input, "tree") == 0) {
            fs_tree();
        }
        else if (strncmp(input, "ai ", 3) == 0) {
            ai_handle(input + 3);
        }
        else {
            print("Unknown team. Enter 'help' for help.\n");
        }
    }
}
