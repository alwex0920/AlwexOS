#ifndef FS_H
#define FS_H

#include "stddef.h"
#include "stdint.h"

#define MAX_NAME_LEN 32
#define MAX_CHILDREN 16
#define MAX_NODES 64
#define MAX_FILE_SIZE 256
#define MAX_PATH_LEN 128

extern int use_ahci;
extern int use_ramdisk;

typedef enum {
    FS_FILE_TYPE,
    FS_DIR_TYPE
} fs_node_type;

#pragma pack(push, 1)
typedef struct fs_node {
    char name[32];
    uint8_t type;
    struct fs_node *parent;
    struct fs_node *children[16];
    uint8_t child_count;
    uint32_t size;
    char content[256];
} fs_node;
#pragma pack(pop)

extern fs_node *current_dir;

void fs_init(uint32_t lba);
void fs_init_ramdisk(void);
void fs_save(void);
void fs_load(void);
void print_tree(fs_node* node, int depth);
void fs_tree(void);
int create_file(const char* name);
int delete_file(const char* name);
int create_dir(const char* name);
int delete_dir(const char* name);
void list_files(void);
fs_node *find_node(const char *path);
int chdir(const char *path);
const char *getcwd(void);
int fs_write(const char *filename, const void *data, size_t size);
int fs_read(const char *filename, void *buf, size_t size);

#endif
