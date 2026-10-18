#ifndef ASTRA_VFS_H
#define ASTRA_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    VFS_NAME_MAX = 31,
    VFS_PATH_MAX = 255,
    VFS_CONTENT_MAX = 511,
    VFS_MAX_NODES = 64,
    VFS_UID_ROOT = 0,
    VFS_UID_ASTRA = 1000
};

typedef enum {
    VFS_OK = 0,
    VFS_ERR_INVALID_ARGUMENT,
    VFS_ERR_PATH_TOO_LONG,
    VFS_ERR_NAME_TOO_LONG,
    VFS_ERR_NOT_FOUND,
    VFS_ERR_EXISTS,
    VFS_ERR_NOT_DIRECTORY,
    VFS_ERR_IS_DIRECTORY,
    VFS_ERR_PERMISSION_DENIED,
    VFS_ERR_NO_SPACE,
    VFS_ERR_NOT_EMPTY,
    VFS_ERR_BUFFER_TOO_SMALL,
    VFS_ERR_INVALID_MODE,
    VFS_ERR_BUSY
} VfsStatus;

typedef struct {
    char name[VFS_NAME_MAX + 1];
    bool directory;
    uint32_t owner;
    uint16_t mode;
    size_t size;
} VfsEntry;

/* Initializes the fixed-capacity RAM filesystem; all contents reset on reboot. */
void vfs_init(void);

/* Returns the current demo UID. */
uint32_t vfs_get_uid(void);

/* Changes the demo identity to a known account; this is not an authentication API. */
VfsStatus vfs_switch_user(uint32_t uid);

/* Returns a stable, static username string for a known UID. */
const char *vfs_username(uint32_t uid);

/* Lists direct children. On BUFFER_TOO_SMALL, *out_count is the required capacity. */
VfsStatus vfs_list(const char *path, VfsEntry *entries, size_t capacity, size_t *out_count);

/* Reads a file as text. out_length receives the data length, excluding its NUL terminator. */
VfsStatus vfs_read(const char *path, char *buffer, size_t capacity, size_t *out_length);

/* Creates a regular file or directory in the current RAM filesystem. */
VfsStatus vfs_create(const char *path, bool directory);

/* Replaces a text file's contents; data is bounded by VFS_CONTENT_MAX. */
VfsStatus vfs_write(const char *path, const char *contents);

/* Removes a file or an empty directory, but never the current directory or its ancestors. */
VfsStatus vfs_remove(const char *path);

/* Sets Unix-style permission bits. Only root or the owner may change a mode. */
VfsStatus vfs_chmod(const char *path, uint16_t mode);

/* Changes the working directory after checking search permission. */
VfsStatus vfs_chdir(const char *path);

/* Copies the canonical working directory path into the caller's buffer. */
VfsStatus vfs_getcwd(char *buffer, size_t capacity);

/* Returns a stable, static explanation for a VfsStatus value. */
const char *vfs_status_string(VfsStatus status);

#endif
