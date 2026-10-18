#ifndef ASTRAOS_VFS_H
#define ASTRAOS_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    ASTRA_VFS_MAX_NODES = 64,
    ASTRA_VFS_NAME_CAP = 32,
    ASTRA_VFS_CONTENT_CAP = 512,
    ASTRA_VFS_PATH_CAP = 256,
    ASTRA_VFS_ROOT_UID = 0,
    ASTRA_VFS_ASTRA_UID = 1000
};

typedef enum AstraVfsResult {
    ASTRA_VFS_OK = 0,
    ASTRA_VFS_INVALID_ARGUMENT,
    ASTRA_VFS_PATH_TOO_LONG,
    ASTRA_VFS_NAME_TOO_LONG,
    ASTRA_VFS_INVALID_NAME,
    ASTRA_VFS_NOT_FOUND,
    ASTRA_VFS_NOT_DIRECTORY,
    ASTRA_VFS_IS_DIRECTORY,
    ASTRA_VFS_PERMISSION_DENIED,
    ASTRA_VFS_ALREADY_EXISTS,
    ASTRA_VFS_DIRECTORY_NOT_EMPTY,
    ASTRA_VFS_NO_SPACE,
    ASTRA_VFS_CONTENT_TOO_LARGE,
    ASTRA_VFS_BUFFER_TOO_SMALL
} AstraVfsResult;

/* Fixed-size RAM filesystem. The kernel owns this storage; no heap is used. */
typedef struct AstraVfsNode {
    bool used;
    bool is_directory;
    uint32_t owner_uid;
    uint16_t mode;
    int parent;
    char name[ASTRA_VFS_NAME_CAP];
    char content[ASTRA_VFS_CONTENT_CAP];
    size_t content_len;
} AstraVfsNode;

typedef struct AstraVfs {
    AstraVfsNode nodes[ASTRA_VFS_MAX_NODES];
    int cwd;
    uint32_t uid;
} AstraVfs;

typedef struct AstraVfsEntry {
    char name[ASTRA_VFS_NAME_CAP];
    bool is_directory;
    uint32_t owner_uid;
    uint16_t mode;
    size_t size;
} AstraVfsEntry;

/* Initializes caller-owned storage; no memory is allocated. */
AstraVfsResult astra_vfs_init(AstraVfs *fs);

/* Changes the active demo identity. Authentication is not implemented yet. */
AstraVfsResult astra_vfs_set_uid(AstraVfs *fs, uint32_t uid);
AstraVfsResult astra_vfs_get_uid(const AstraVfs *fs, uint32_t *uid_out);

/* Paths are ASCII byte spans, not necessarily NUL-terminated. */
AstraVfsResult astra_vfs_chdir(AstraVfs *fs, const char *path, size_t path_len);
AstraVfsResult astra_vfs_pwd(
    const AstraVfs *fs,
    char *output,
    size_t output_capacity,
    size_t *written_out
);
AstraVfsResult astra_vfs_stat(
    const AstraVfs *fs,
    const char *path,
    size_t path_len,
    AstraVfsEntry *entry_out
);
AstraVfsResult astra_vfs_list(
    const AstraVfs *fs,
    const char *path,
    size_t path_len,
    AstraVfsEntry *entries,
    size_t entries_capacity,
    size_t *count_out
);
AstraVfsResult astra_vfs_read(
    const AstraVfs *fs,
    const char *path,
    size_t path_len,
    char *output,
    size_t output_capacity,
    size_t *read_out
);
AstraVfsResult astra_vfs_create(
    AstraVfs *fs,
    const char *path,
    size_t path_len,
    bool is_directory
);
AstraVfsResult astra_vfs_write(
    AstraVfs *fs,
    const char *path,
    size_t path_len,
    const char *content,
    size_t content_len
);
AstraVfsResult astra_vfs_remove(AstraVfs *fs, const char *path, size_t path_len);
AstraVfsResult astra_vfs_chmod(
    AstraVfs *fs,
    const char *path,
    size_t path_len,
    uint16_t mode
);

/* Formats Unix rwx/special-mode bits into 10 characters plus NUL.
 * output is caller-owned and must have at least 11 bytes. Returns false
 * without changing output for a null/short buffer or a mode above 07777. */
bool astra_vfs_format_mode(uint16_t mode, bool is_directory, char *output,
    size_t output_capacity);

const char *astra_vfs_result_name(AstraVfsResult result);

#endif
