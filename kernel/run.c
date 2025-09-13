#include "include/fs.h"
#include "include/lib.h"
#include "include/run.h"
#include "include/stddef.h"
#include "include/ai.h"

double eval_expression(const char* expr);
int eval_condition(const char* cond);
void replace_text_operators(char* token);

int isdigit(int c) {
    return c >= '0' && c <= '9';
}

int isalpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}

double str_to_double(const char* s) {
    double res = 0.0;
    double fact = 1.0;
    int point_seen = 0;
    int negative = 0;

    if (*s == '-') {
        negative = 1;
        s++;
    }

    for (; *s; s++) {
        if (*s == '.') {
            point_seen = 1;
            continue;
        }
        int d = *s - '0';
        if (d >= 0 && d <= 9) {
            if (point_seen) {
                fact /= 10.0;
                res = res + (double)d * fact;
            } else {
                res = res * 10.0 + (double)d;
            }
        }
    }
    return negative ? -res : res;
}

void print_double(double n) {
    char buffer[32];
    int integer_part = (int)n;

    itoa(integer_part, buffer, 10);
    print(buffer);
    print(".");

    double fractional = n - integer_part;
    if (fractional < 0) fractional = -fractional;
    
    int fractional_part = (int)(fractional * 10000);
    if (fractional_part < 10) print("000");
    else if (fractional_part < 100) print("00");
    else if (fractional_part < 1000) print("0");
    
    itoa(fractional_part, buffer, 10);
    print(buffer);
}

static int my_isspace(int c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

#define MAX_VARS 50
struct Variable {
    char name[50];
    double value;
    char* str_value;
};

static struct Variable variables[MAX_VARS];
static int var_count = 0;

#define MAX_STRINGS 20
#define STRING_SIZE 50
static char string_pool[MAX_STRINGS][STRING_SIZE];
static int string_count = 0;

#define MAX_FUNCTIONS 10
#define MAX_FUNC_BODY_SIZE 512

struct Function {
    char name[50];
    char body[MAX_FUNC_BODY_SIZE];
};

static struct Function functions[MAX_FUNCTIONS];
static int function_count = 0;

struct Variable* find_variable(const char* name) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(variables[i].name, name) == 0) {
            return &variables[i];
        }
    }
    return NULL;
}

struct Function* find_function(const char* name) {
    for (int i = 0; i < function_count; i++) {
        if (strcmp(functions[i].name, name) == 0) {
            return &functions[i];
        }
    }
    return NULL;
}

void replace_text_operators(char* token) {
    char* op_ptr = token;
    while ((op_ptr = strstr(op_ptr, "plus"))) {
        *op_ptr = '+';
        memmove(op_ptr + 1, op_ptr + 4, strlen(op_ptr + 4) + 1);
    }
    op_ptr = token;
    while ((op_ptr = strstr(op_ptr, "minus"))) {
        *op_ptr = '-';
        memmove(op_ptr + 1, op_ptr + 5, strlen(op_ptr + 5) + 1);
    }
    op_ptr = token;
    while ((op_ptr = strstr(op_ptr, "mul"))) {
        *op_ptr = '*';
        memmove(op_ptr + 1, op_ptr + 3, strlen(op_ptr + 3) + 1);
    }
    op_ptr = token;
    while ((op_ptr = strstr(op_ptr, "div"))) {
        *op_ptr = '/';
        memmove(op_ptr + 1, op_ptr + 3, strlen(op_ptr + 3) + 1);
    }
    op_ptr = token;
    while ((op_ptr = strstr(op_ptr, "inc"))) {
        *op_ptr = '+';
        *(op_ptr + 1) = '+';
        memmove(op_ptr + 2, op_ptr + 3, strlen(op_ptr + 3) + 1);
    }
    op_ptr = token;
    while ((op_ptr = strstr(op_ptr, "dec"))) {
        *op_ptr = '-';
        *(op_ptr + 1) = '-';
        memmove(op_ptr + 2, op_ptr + 3, strlen(op_ptr + 3) + 1);
    }
}

double eval_expression(const char* expr) {
    double result = 0;
    double current = 0;
    char op = '+';
    int has_value = 0;
    
    const char* p = expr;
    while (*p) {
        if (my_isspace(*p)) {
            p++;
            continue;
        }

        if (isdigit(*p) || *p == '.') {
            current = str_to_double(p);
            has_value = 1;
            while (isdigit(*p) || *p == '.') p++;
            continue;
        }

        if (isalpha(*p)) {
            char var_name[50] = {0};
            int i = 0;

            while (isalnum(*p) || *p == '_') {
                if ((size_t)i < sizeof(var_name) - 1) {
                    var_name[i++] = *p;
                }
                p++;
            }
            var_name[i] = '\0';

            struct Variable* v = find_variable(var_name);
            if (v) {
                if (v->str_value) {
                    current = 0;
                } else {
                    current = v->value;
                }
            } else {
                current = 0;
            }
            has_value = 1;
            continue;
        }

        if (*p == '+' || *p == '-' || *p == '*' || *p == '/') {
            if (has_value) {
                switch (op) {
                    case '+': result += current; break;
                    case '-': result -= current; break;
                    case '*': result *= current; break;
                    case '/': 
                        if (current != 0) result /= current;
                        else print("Error: division by zero\n");
                        break;
                }
                op = *p;
                has_value = 0;
            }
            p++;
            continue;
        }

        if (*p == '+' && *(p+1) == '+') {
            p += 2;
            continue;
        }
        if (*p == '-' && *(p+1) == '-') {
            p += 2;
            continue;
        }

        p++;
    }

    if (has_value) {
        switch (op) {
            case '+': result += current; break;
            case '-': result -= current; break;
            case '*': result *= current; break;
            case '/': 
                if (current != 0) result /= current;
                else print("Error: division by zero\n");
                break;
        }
    }
    
    return result;
}

int eval_condition(const char* cond) {
    const char* p = cond;
    char left[50] = {0};
    char right[50] = {0};
    char op[3] = {0};
    int op_pos = 0;

    while (*p) {
        if (strchr("=><!&|", *p)) {
            if (op_pos < 2) {
                op[op_pos++] = *p;
            }
        } else {
            if (op_pos == 0) {
                strncat(left, p, 1);
            } else {
                strncat(right, p, 1);
            }
        }
        p++;
    }

    char* ltrim = left;
    while (my_isspace(*ltrim)) ltrim++;
    char* rtrim = right;
    while (my_isspace(*rtrim)) rtrim++;
    
    double left_val = str_to_double(ltrim);
    double right_val = str_to_double(rtrim);

    if (strcmp(op, "==") == 0) return left_val == right_val;
    if (strcmp(op, "!=") == 0) return left_val != right_val;
    if (strcmp(op, ">") == 0) return left_val > right_val;
    if (strcmp(op, "<") == 0) return left_val < right_val;
    if (strcmp(op, ">=") == 0) return left_val >= right_val;
    if (strcmp(op, "<=") == 0) return left_val <= right_val;
    
    return 0;
}

int system_command(const char *command) {
    if (strcmp(command, "clr") == 0) {
        clear_screen();
        return 0;
    } else if (strcmp(command, "poweroff") == 0) {
        poweroff();
    }
    else if (strcmp(command, "reboot") == 0) {
        reboot();
    }
    else if (strncmp(command, "create-file ", 12) == 0) {
        const char* name = command + 12;
        if (create_file(name)) {
            print("File creation error\n");
        } else {
            print("The file has been created\n");
        }
    }
    else if (strncmp(command, "delete-file ", 12) == 0) {
        const char* name = command + 12;
        if (delete_file(name)) {
            print("File deletion error\n");
        } else {
            print("The file has been deleted\n");
        }
    }
    else if (strncmp(command, "create-dir ", 11) == 0) {
        const char* name = command + 11;
        if (create_dir(name)) {
            print("Directory creation error\n");
        } else {
            print("The directory has been created\n");
        }
    }
    else if (strncmp(command, "delete-dir ", 11) == 0) {
        const char* name = command + 11;
        if (delete_dir(name)) {
            print("Error deleting a directory\n");
        } else {
            print("The directory has been deleted\n");
        }
    }
    else if (strncmp(command, "cd ", 3) == 0) {
        const char *path = command + 3;
        if (chdir(path) != 0) {
            print("Directory not found: ");
            print(path);
            print("\n");
        }
    } 
    else if (strcmp(command, "cd") == 0) {
        chdir("/");
    }
    else if (strncmp(command, "run ", 4) == 0) {
        const char* filename = command + 4;
        run_program(filename);
    }
    else if (strcmp(command, "list") == 0) {
        list_files();
    }
    else if (strcmp(command, "tree") == 0) {
        fs_tree();
    }
    else if (strncmp(command, "ai ", 3) == 0) {
        ai_handle(command + 3);
    }
    else {
        print("Unknown command: ");
        print(command);
        print("\n");
        return -1;
    }
}

int file_operations(const char *operation, const char *filename, const char *content) {
    if (strcmp(operation, "write") == 0) {
        int result = fs_write(filename, content, strlen(content));
        if (result < 0) {
            print("Error writing to file: ");
            print(filename);
            print("\n");
            return -1;
        }
        print("File written: ");
        print(filename);
        print("\n");
        return 0;
    } else if (strcmp(operation, "read") == 0) {
        char buffer[256];
        int size = fs_read(filename, buffer, sizeof(buffer)-1);
        if (size < 0) {
            print("Error reading file: ");
            print(filename);
            print("\n");
            return -1;
        }
        buffer[size] = '\0';
        print(buffer);
        print("\n");
        return 0;
    } else if (strcmp(operation, "append") == 0) {
        char old_content[512];
        int old_size = fs_read(filename, old_content, sizeof(old_content)-1);
        if (old_size < 0) {
            old_size = 0;
            old_content[0] = '\0';
        } else {
            old_content[old_size] = '\0';
        }

        if (old_size + strlen(content) >= sizeof(old_content)) {
            print("Error: file too big to append\n");
            return -1;
        }

        strcat(old_content, content);

        int result = fs_write(filename, old_content, strlen(old_content));
        if (result < 0) {
            print("Error appending to file: ");
            print(filename);
            print("\n");
            return -1;
        }
        print("Content appended to: ");
        print(filename);
        print("\n");
        return 0;
    } else if (strcmp(operation, "exists") == 0) {
        char buffer[1];
        int size = fs_read(filename, buffer, 1);
        if (size < 0) {
            print("File does not exist: ");
            print(filename);
            print("\n");
            return 0;
        } else {
            print("File exists: ");
            print(filename);
            print("\n");
            return 1;
        }
    }
    
    print("Unknown file operation: ");
    print(operation);
    print("\n");
    return -1;
}

void execute(const char* code) {
    char line[256];
    const char* p = code;

    var_count = 0;
    string_count = 0;
    function_count = 0;

    int skip_block = 0;
    int condition_met = 0;
    int in_loop = 0;
    const char* loop_start = NULL;
    char loop_condition[100] = {0};

    while (*p) {
        int line_len = 0;
        while (p[line_len] != '\n' && p[line_len] != '\0') {
            line_len++;
        }

        if (line_len >= (int)sizeof(line)) line_len = sizeof(line) - 1;
        memcpy(line, p, line_len);
        line[line_len] = '\0';
        p += line_len;
        if (*p == '\n') p++;

        char* token = line;
        while (my_isspace(*token)) token++;

        if (*token == '\0' || *token == '#') {
            continue;
        }

        if (skip_block) {
            if (strncmp(token, "end", 3) == 0) {
                skip_block = 0;
                condition_met = 0;
            }
            else if (strncmp(token, "endloop", 7) == 0) {
                skip_block = 0;
                in_loop = 0;
            }
            continue;
        }

        replace_text_operators(token);

        if (in_loop) {
            if (strncmp(token, "endloop", 7) == 0) {
                if (eval_condition(loop_condition)) {
                    p = loop_start;
                    skip_block = 0;
                    condition_met = 0;
                    continue;
                } else {
                    in_loop = 0;
                }
            }
        }
        else if (strncmp(token, "while ", 6) == 0) {
            char* cond = token + 6;
            if (eval_condition(cond)) {
                in_loop = 1;
                strlcpy(loop_condition, cond, sizeof(loop_condition));
                loop_start = p;
            } else {
                skip_block = 1;
            }
            continue;
        }

        if (strncmp(token, "if ", 3) == 0) {
            char* cond = token + 3;
            if (eval_condition(cond)) {
                condition_met = 1;
            } else {
                skip_block = 1;
            }
            continue;
        }
        else if (strncmp(token, "else if ", 8) == 0) {
            if (condition_met) {
                skip_block = 1;
            } else {
                char* cond = token + 8;
                if (eval_condition(cond)) {
                    condition_met = 1;
                    skip_block = 0;
                } else {
                    skip_block = 1;
                }
            }
            continue;
        }
        else if (strncmp(token, "else", 4) == 0) {
            if (condition_met) {
                skip_block = 1;
            } else {
                skip_block = 0;
            }
            continue;
        }
        else if (strncmp(token, "end", 3) == 0) {
            condition_met = 0;
            continue;
        }

        else if (strncmp(token, "exec ", 5) == 0) {
            char* command = token + 5;
            while (my_isspace(*command)) command++;
            system_command(command);
            continue;
        }

        else if (strncmp(token, "file_write ", 11) == 0) {
            char* args = token + 11;
            while (my_isspace(*args)) args++;

            char filename[50];
            char content[256];

            char* space = strchr(args, ' ');
            if (space == NULL) {
                print("Error: file_write requires filename and content\n");
                continue;
            }

            int filename_len = space - args;
            if (filename_len >= (int)sizeof(filename)) filename_len = sizeof(filename) - 1;
            memcpy(filename, args, filename_len);
            filename[filename_len] = '\0';

            char* content_start = space + 1;
            while (my_isspace(*content_start)) content_start++;

            if (*content_start == '\'') {
                char* end_quote = strchr(content_start + 1, '\'');
                if (end_quote) {
                    *end_quote = '\0';
                    strlcpy(content, content_start + 1, sizeof(content));
                } else {
                    print("Error: unclosed string in file_write\n");
                    continue;
                }
            } else {
                strlcpy(content, content_start, sizeof(content));
            }
            
            file_operations("write", filename, content);
            continue;
        }
        else if (strncmp(token, "file_read ", 10) == 0) {
            char* filename = token + 10;
            while (my_isspace(*filename)) filename++;
            file_operations("read", filename, NULL);
            continue;
        }
        else if (strncmp(token, "file_append ", 12) == 0) {
            char* args = token + 12;
            while (my_isspace(*args)) args++;

            char filename[50];
            char content[256];

            char* space = strchr(args, ' ');
            if (space == NULL) {
                print("Error: file_append requires filename and content\n");
                continue;
            }

            int filename_len = space - args;
            if (filename_len >= (int)sizeof(filename)) filename_len = sizeof(filename) - 1;
            memcpy(filename, args, filename_len);
            filename[filename_len] = '\0';

            char* content_start = space + 1;
            while (my_isspace(*content_start)) content_start++;

            if (*content_start == '\'') {
                char* end_quote = strchr(content_start + 1, '\'');
                if (end_quote) {
                    *end_quote = '\0';
                    strlcpy(content, content_start + 1, sizeof(content));
                } else {
                    print("Error: unclosed string in file_append\n");
                    continue;
                }
            } else {
                strlcpy(content, content_start, sizeof(content));
            }
            
            file_operations("append", filename, content);
            continue;
        }

        else if (strncmp(token, "file_exists ", 12) == 0) {
            char* filename = token + 12;
            while (my_isspace(*filename)) filename++;
            file_operations("exists", filename, NULL);
            continue;
        }

        if (strstr(token, "++") || strstr(token, "--")) {
            char var_name[50] = {0};
            char* op_ptr = token;
            while (*op_ptr && !isalnum(*op_ptr)) op_ptr++;

            int i = 0;
            while (isalnum(*op_ptr) || *op_ptr == '_') {
                if ((size_t)i < sizeof(var_name) - 1) {
                    var_name[i++] = *op_ptr;
                }
                op_ptr++;
            }
            
            struct Variable* v = find_variable(var_name);
            if (v) {
                if (strstr(token, "++")) {
                    v->value += 1;
                } else {
                    v->value -= 1;
                }
            }
            continue;
        }

        if (strncmp(token, "print ", 6) == 0) {
            char* arg = token + 6;
            while (my_isspace(*arg)) arg++;

            if (*arg == '\'') {
                char* end_quote = strchr(arg + 1, '\'');
                if (end_quote) {
                    *end_quote = '\0';
                    print(arg + 1);
                    print("\n");
                } else {
                    print("Error: unclosed string\n");
                }
            } else {
                struct Variable* v = find_variable(arg);
                if (v) {
                    if (v->str_value) {
                        print(v->str_value);
                        print("\n");
                    } else {
                        print_double(v->value);
                        print("\n");
                    }
                } else {
                    print("Error: variable '");
                    print(arg);
                    print("' not found\n");
                }
            }
        }

        else if (strncmp(token, "wait", 4) == 0) {
            char* sec_str = token + 4;
            while (*sec_str == ' ' || *sec_str == '\t') sec_str++;
            
            if (*sec_str == '\0') {
                print("Error: wait command requires argument\n");
            } else {
                int seconds = atoi(sec_str);
                if (seconds > 0) {
                    sleep(seconds);
                } else {
                    print("Error: invalid time value\n");
                }
            }
        }

        else if (strncmp(token, "let ", 4) == 0) {
            char var_name[50];
            char* eq = strchr(token, '=');
            if (eq) {
                char* name_start = token + 4;
                while (my_isspace(*name_start)) name_start++;
                
                char* name_end = eq;
                while (name_end > name_start && my_isspace(*(name_end - 1))) name_end--;
                
                int name_len = name_end - name_start;
                if (name_len >= (int)sizeof(var_name)) name_len = sizeof(var_name) - 1;
                
                memcpy(var_name, name_start, name_len);
                var_name[name_len] = '\0';

                char* value_str = eq + 1;
                while (my_isspace(*value_str)) value_str++;

                struct Variable* v = find_variable(var_name);
                if (!v && var_count < MAX_VARS) {
                    v = &variables[var_count++];
                    strncpy(v->name, var_name, sizeof(v->name));
                    v->name[sizeof(v->name) - 1] = '\0';
                }

                if (v) {
                    if (*value_str == '\'') {
                        char* end_quote = strchr(value_str + 1, '\'');
                        if (end_quote) {
                            *end_quote = '\0';
                            if (string_count < MAX_STRINGS) {
                                strncpy(string_pool[string_count], value_str + 1, STRING_SIZE - 1);
                                string_pool[string_count][STRING_SIZE - 1] = '\0';
                                v->str_value = string_pool[string_count];
                                string_count++;
                            } else {
                                print("Error: string pool full\n");
                            }
                            v->value = 0;
                        } else {
                            print("Error: unclosed string\n");
                        }
                    } else {
                        v->value = eval_expression(value_str);
                        v->str_value = NULL;
                    }
                }
            } else {
                print("Error: expected '=' in let statement\n");
            }
        }
        else if (strncmp(token, "inp ", 4) == 0) {
            char* args = token + 4;
            char type[16];
            char var_name[50];
            char prompt[100] = "";

            char* p = strtok(args, " ");
            if (p) strlcpy(type, p, sizeof(type));
            
            p = strtok(NULL, " ");
            if (p) strlcpy(var_name, p, sizeof(var_name));

            char* quote_start = strchr(token, '\'');
            if (quote_start) {
                char* quote_end = strrchr(token, '\'');
                if (quote_end && quote_end > quote_start) {
                    *quote_end = '\0';
                    strlcpy(prompt, quote_start + 1, sizeof(prompt));
                }
            }

            if (prompt[0] != '\0') {
                print(prompt);
            }

            char input_buf[64];
            safe_readline(input_buf, sizeof(input_buf));

            struct Variable* var = find_variable(var_name);
            if (!var) {
                if (var_count >= MAX_VARS) {
                    print("Error: too many variables\n");
                    continue;
                }
                var = &variables[var_count++];
                strncpy(var->name, var_name, sizeof(var->name));
                var->name[sizeof(var->name)-1] = '\0';
            }

            if (strcmp(type, "int") == 0) {
                var->value = atoi(input_buf);
                var->str_value = NULL;
            }
            else if (strcmp(type, "float") == 0) {
                var->value = str_to_double(input_buf);
                var->str_value = NULL;
            }
            else if (strcmp(type, "string") == 0) {
                if (string_count < MAX_STRINGS) {
                    strncpy(string_pool[string_count], input_buf, STRING_SIZE);
                    string_pool[string_count][STRING_SIZE-1] = '\0';
                    var->str_value = string_pool[string_count];
                    string_count++;
                    var->value = 0;
                } else {
                    print("Error: string pool full\n");
                }
            }
        }
        else if (strncmp(token, "func ", 5) == 0) {
            char* name = token + 5;
            if (function_count >= MAX_FUNCTIONS) {
                print("Error: too many functions\n");
                continue;
            }

            struct Function* func = &functions[function_count++];
            strncpy(func->name, name, sizeof(func->name));

            const char* end_ptr = strstr(p, "end");
            if (!end_ptr) {
                print("Error: function missing end\n");
                continue;
            }

            int body_size = end_ptr - p;
            if (body_size >= MAX_FUNC_BODY_SIZE) body_size = MAX_FUNC_BODY_SIZE - 1;
            
            memcpy(func->body, p, body_size);
            func->body[body_size] = '\0';

            p = end_ptr + 3;
        }
        else if (strncmp(token, "call ", 5) == 0) {
            char* name = token + 5;
            struct Function* func = find_function(name);
            if (func) {
                execute(func->body);
            } else {
                print("Error: function not found: ");
                print(name);
                print("\n");
            }
        }
    }
}

void run_program(const char* filename) {
    static char code_buffer[1024];
    memset(code_buffer, 0, sizeof(code_buffer));

    int size = fs_read(filename, code_buffer, sizeof(code_buffer) - 1);
    
    if (size <= 0) {
        print("Error: could not read file '");
        print(filename);
        print("'\n");
        return;
    }
    
    code_buffer[size] = '\0';
    execute(code_buffer);
}
