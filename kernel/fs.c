#include "include/fs.h"
#include "include/lib.h"
#include "include/ata.h"
#include "include/stddef.h"

#define MAX_NODES 64
static fs_node node_pool[MAX_NODES];
static int node_count = 0;

fs_node fs_root;
fs_node *current_dir;

static char current_path[MAX_PATH_LEN] = "/";
#define FS_SIGNATURE 0x4F53574C

static uint32_t fs_start_sector = 0;

void fs_set_start_sector(uint32_t sector) {
    fs_start_sector = sector;
}

fs_node *find_node(const char *path) {
    if (strcmp(path, "/") == 0) {
        return &fs_root;
    }

    char temp_path[MAX_PATH_LEN];
    strlcpy(temp_path, path, sizeof(temp_path));

    fs_node *current = (path[0] == '/') ? &fs_root : current_dir;
    char *component = strtok(temp_path, "/");
    
    while (component != NULL) {
        int found = 0;

        for (int i = 0; i < current->child_count; i++) {
            fs_node *child = current->children[i];
            if (child != NULL && strcmp(child->name, component) == 0) {
                if (child->type == FS_DIR_TYPE) {
                    current = child;
                    found = 1;
                    break;
                }
            }
        }
        
        if (!found) {
            return NULL;
        }
        
        component = strtok(NULL, "/");
    }
    
    return current;
}

int chdir(const char *path) {
    fs_node *node = find_node(path);
    if (node == NULL || node->type != FS_DIR_TYPE) {
        return -1;
    }

    if (path[0] == '/') {
        strlcpy(current_path, path, sizeof(current_path));
    } else {
        if (strcmp(current_path, "/") != 0) {
            strlcat(current_path, "/", sizeof(current_path));
        }
        strlcat(current_path, path, sizeof(current_path));
    }

    char normalized[MAX_PATH_LEN];
    char *dst = normalized;
    const char *src = current_path;
    char prev = '\0';
    
    while (*src) {
        if (*src == '/') {
            if (prev != '/') {
                *dst++ = *src;
            }
        } else {
            *dst++ = *src;
        }
        prev = *src;
        src++;
    }
    *dst = '\0';

    if (normalized[0] == '\0') {
        strlcpy(normalized, "/", sizeof(normalized));
    }
    
    strlcpy(current_path, normalized, sizeof(current_path));
    current_dir = node;
    return 0;
}

const char *getcwd(void) {
    return current_path;
}

void fs_init() {
    fs_load();

    if (node_count == 0) {
        node_count = 0;
        memset(node_pool, 0, sizeof(node_pool));

        strlcpy(fs_root.name, "/", sizeof(fs_root.name));
        fs_root.type = FS_DIR_TYPE;
        fs_root.parent = NULL;
        fs_root.child_count = 0;
        fs_root.size = 0;

        for (int i = 0; i < MAX_CHILDREN; i++) {
            fs_root.children[i] = NULL;
        }
        
        current_dir = &fs_root;
        node_count = 1;
        strlcpy(current_path, "/", sizeof(current_path));
    }
}

void fs_save(void) {
    uint8_t buffer[512];
    memset(buffer, 0, sizeof(buffer));
    
    *(uint32_t*)(buffer) = FS_SIGNATURE;
    *(uint32_t*)(buffer + 4) = node_count;

    ata_write_sectors(0, 1, buffer);

    for (int i = 0; i < node_count; i++) {
        memset(buffer, 0, sizeof(buffer));

        fs_node node_copy = node_pool[i];

        if (node_copy.parent) {
            node_copy.parent = (fs_node*)(uintptr_t)(node_copy.parent - node_pool);
        }

        for (int j = 0; j < node_copy.child_count; j++) {
            if (node_copy.children[j]) {
                node_copy.children[j] = (fs_node*)(uintptr_t)(node_copy.children[j] - node_pool);
            }
        }

        memcpy(buffer, &node_copy, sizeof(fs_node));

        ata_write_sectors(1 + i, 1, buffer);
    }
}

int fs_write(const char *filename, const void *data, size_t size) {
    fs_node *file = NULL;
    for (int i = 0; i < current_dir->child_count; i++) {
        fs_node *child = current_dir->children[i];
        if (child && child->type == FS_FILE_TYPE && 
            strcmp(child->name, filename) == 0) {
            file = child;
            break;
        }
    }

    if (!file) {
        if (create_file(filename) != 0) {
            return -1;
        }

        for (int i = 0; i < current_dir->child_count; i++) {
            if (strcmp(current_dir->children[i]->name, filename) == 0) {
                file = current_dir->children[i];
                break;
            }
        }
        if (!file) return -1;
    }

    size_t to_copy = size;
    if (to_copy > MAX_FILE_SIZE) {
        to_copy = MAX_FILE_SIZE;
    }
    
    memcpy(file->content, data, to_copy);
    file->size = to_copy;
    
    return to_copy;
}

int fs_read(const char *filename, void *buf, size_t size) {
    fs_node *file = NULL;
    for (int i = 0; i < current_dir->child_count; i++) {
        fs_node *child = current_dir->children[i];
        if (child && child->type == FS_FILE_TYPE && 
            strcmp(child->name, filename) == 0) {
            file = child;
            break;
        }
    }

    if (!file) {
        return -1;
    }

    size_t to_copy = (size < file->size) ? size : file->size;
    memcpy(buf, file->content, to_copy);
    return to_copy;
}

void fs_load(void) {
    uint8_t buffer[512];

    uint32_t fs_start = find_fs_partition();
    ata_read_sectors(fs_start, 1, buffer);

    if (*(uint32_t*)buffer != FS_SIGNATURE) {
        print("No valid FS found. Creating new.\n");
        node_count = 0;
        return;
    }

    node_count = *(uint32_t*)(buffer + 4);
    
    if (node_count > MAX_NODES) {
        print("FS corrupted: too many nodes\n");
        node_count = 0;
        return;
    }

    for (int i = 0; i < node_count; i++) {
        ata_read_sectors(1 + i, 1, buffer);
        memcpy(&node_pool[i], buffer, sizeof(fs_node));
    }

    for (int i = 0; i < node_count; i++) {
        fs_node *node = &node_pool[i];

        if (node->parent) {
            uintptr_t parent_index = (uintptr_t)node->parent;
            if (parent_index < node_count) {
                node->parent = &node_pool[parent_index];
            } else {
                node->parent = NULL;
            }
        }

        for (int j = 0; j < node->child_count; j++) {
            if (node->children[j]) {
                uintptr_t child_index = (uintptr_t)node->children[j];
                if (child_index < node_count) {
                    node->children[j] = &node_pool[child_index];
                } else {
                    node->children[j] = NULL;
                }
            }
        }
    }

    fs_root = node_pool[0];
    current_dir = &fs_root;
    
    print("FS loaded successfully. Nodes: ");
    char num_buf[12];
    itoa(node_count, num_buf, 10);
    print(num_buf);
    print("\n");
}

void print_tree(fs_node* node, int depth) {
    if (node == NULL) return;

    for (int i = 0; i < depth; i++) {
        print("  ");
    }

    print(node->name);

    if (node->type == FS_DIR_TYPE) {
        print("/");
    }
    print("\n");

    if (node->type == FS_DIR_TYPE) {
        for (int i = 0; i < node->child_count; i++) {
            if (node->children[i] != NULL) {
                print_tree(node->children[i], depth + 1);
            }
        }
    }
}

void fs_tree() {
    print_tree(&fs_root, 0);
}

int create_file(const char* name) {
    if (current_dir->child_count >= MAX_CHILDREN) {
        return -1;
    }

    if (node_count >= MAX_NODES) {
        return -2;
    }

    fs_node **new_file_ptr = &current_dir->children[current_dir->child_count];
    *new_file_ptr = &node_pool[node_count++];

    strncpy((*new_file_ptr)->name, name, MAX_NAME_LEN - 1);
    (*new_file_ptr)->name[MAX_NAME_LEN - 1] = '\0';
    (*new_file_ptr)->type = FS_FILE_TYPE;
    (*new_file_ptr)->parent = current_dir;
    (*new_file_ptr)->child_count = 0;
    
    current_dir->child_count++;
    return 0;
}

int delete_file(const char* name) {
    for (int i = 0; i < current_dir->child_count; i++) {
        fs_node *child = current_dir->children[i];
        if (child != NULL && 
            strcmp(child->name, name) == 0 && 
            child->type == FS_FILE_TYPE) {

            for (int j = i; j < current_dir->child_count - 1; j++) {
                current_dir->children[j] = current_dir->children[j + 1];
            }
            
            current_dir->child_count--;
            current_dir->children[current_dir->child_count] = NULL;
            return 0;
        }
    }
    return -1;
}

int create_dir(const char* name) {
    if (current_dir->child_count >= MAX_CHILDREN) {
        return -1;
    }

    if (node_count >= MAX_NODES) {
        return -2;
    }

    fs_node **new_dir_ptr = &current_dir->children[current_dir->child_count];
    *new_dir_ptr = &node_pool[node_count++];

    strncpy((*new_dir_ptr)->name, name, MAX_NAME_LEN - 1);
    (*new_dir_ptr)->name[MAX_NAME_LEN - 1] = '\0';
    (*new_dir_ptr)->type = FS_DIR_TYPE;
    (*new_dir_ptr)->parent = current_dir;
    (*new_dir_ptr)->child_count = 0;

    for (int i = 0; i < MAX_CHILDREN; i++) {
        (*new_dir_ptr)->children[i] = NULL;
    }
    
    current_dir->child_count++;
    return 0;
}

int delete_dir(const char* name) {
    for (int i = 0; i < current_dir->child_count; i++) {
        fs_node *child = current_dir->children[i];
        if (child != NULL && 
            strcmp(child->name, name) == 0 && 
            child->type == FS_DIR_TYPE) {

            if (child->child_count > 0) {
                return -2;
            }

            for (int j = i; j < current_dir->child_count - 1; j++) {
                current_dir->children[j] = current_dir->children[j + 1];
            }
            
            current_dir->child_count--;
            current_dir->children[current_dir->child_count] = NULL;
            return 0;
        }
    }
    return -1;
}

void list_files() {
    if (current_dir->child_count == 0) {
        print("The catalog is empty\n");
        return;
    }
    
    print("Contents of the catalog:\n");
    for (int i = 0; i < current_dir->child_count; i++) {
        fs_node *child = current_dir->children[i];
        if (child != NULL) {
            print(child->name);
            if (child->type == FS_DIR_TYPE) {
                print("/");
            }
            print("\n");
        }
    }
}