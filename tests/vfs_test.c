#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "astra/vfs.h"

static unsigned int g_failures = 0u;

#define CHECK(condition)                                                         \
    do {                                                                         \
        if (!(condition)) {                                                      \
            (void)fprintf(stderr, "%s:%d: CHECK failed: %s\n",                  \
                          __FILE__, __LINE__, #condition);                       \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

static void test_initial_filesystem(void) {
    char path[VFS_PATH_MAX + 1] = {0};
    char contents[VFS_CONTENT_MAX + 1] = {0};
    size_t length = 0;

    vfs_init();
    CHECK(vfs_get_uid() == VFS_UID_ROOT);
    CHECK(vfs_getcwd(path, sizeof path) == VFS_OK);
    CHECK(path[0] == '/' && path[1] == '\0');
    CHECK(vfs_read("/README.txt", contents, sizeof contents, &length) == VFS_OK);
    CHECK(length > 0u);
    CHECK(contents[0] == 'W');
}

static void test_create_read_write_and_capacity(void) {
    char output[16] = {0};
    char tiny[1] = {0};
    size_t length = 0;

    CHECK(vfs_create("/tmp/report.txt", false) == VFS_OK);
    CHECK(vfs_write("/tmp/report.txt", "hello") == VFS_OK);
    CHECK(vfs_read("/tmp/report.txt", output, sizeof output, &length) == VFS_OK);
    CHECK(length == 5u);
    CHECK(output[0] == 'h' && output[4] == 'o' && output[5] == '\0');
    CHECK(vfs_read("/tmp/report.txt", tiny, sizeof tiny, &length) ==
          VFS_ERR_BUFFER_TOO_SMALL);
    CHECK(length == 5u);
    CHECK(tiny[0] == '\0');
    CHECK(vfs_create("/tmp/report.txt", false) == VFS_ERR_EXISTS);
    CHECK(vfs_read("/tmp", output, sizeof output, &length) == VFS_ERR_IS_DIRECTORY);
}

static void test_permissions(void) {
    char output[VFS_CONTENT_MAX + 1] = {0};
    size_t length = 0;

    CHECK(vfs_create("/tmp/root-secret.txt", false) == VFS_OK);
    CHECK(vfs_write("/tmp/root-secret.txt", "private") == VFS_OK);
    CHECK(vfs_chmod("/tmp/root-secret.txt", 0600u) == VFS_OK);
    CHECK(vfs_switch_user(VFS_UID_ASTRA) == VFS_OK);
    CHECK(vfs_read("/tmp/root-secret.txt", output, sizeof output, &length) ==
          VFS_ERR_PERMISSION_DENIED);
    CHECK(vfs_write("/tmp/root-secret.txt", "changed") == VFS_ERR_PERMISSION_DENIED);
    CHECK(vfs_chmod("/tmp/root-secret.txt", 0644u) == VFS_ERR_PERMISSION_DENIED);
    CHECK(vfs_create("/home/astra/user-file.txt", false) == VFS_OK);
    CHECK(vfs_write("/home/astra/user-file.txt", "astra data") == VFS_OK);
    CHECK(vfs_read("/home/astra/user-file.txt", output, sizeof output, &length) == VFS_OK);
    CHECK(length == 10u);
    CHECK(vfs_switch_user(VFS_UID_ROOT) == VFS_OK);
    CHECK(vfs_read("/home/astra/user-file.txt", output, sizeof output, &length) == VFS_OK);
    CHECK(vfs_switch_user(42u) == VFS_ERR_INVALID_ARGUMENT);
}

static void test_paths_removal_and_modes(void) {
    char cwd[VFS_PATH_MAX + 1] = {0};
    char long_path[VFS_PATH_MAX + 2] = {0};
    VfsEntry tiny_entries[1] = {0};
    size_t count = 0;

    vfs_init();
    CHECK(vfs_create("/home/astra/nested", true) == VFS_OK);
    CHECK(vfs_create("/home/astra/nested/one.txt", false) == VFS_OK);
    CHECK(vfs_remove("/home/astra/nested") == VFS_ERR_NOT_EMPTY);
    CHECK(vfs_chdir("/home/astra/nested") == VFS_OK);
    CHECK(vfs_getcwd(cwd, sizeof cwd) == VFS_OK);
    CHECK(cwd[0] == '/' && cwd[1] == 'h');
    CHECK(vfs_remove(".") == VFS_ERR_BUSY);
    CHECK(vfs_remove("/home/astra") == VFS_ERR_BUSY);
    CHECK(vfs_chdir("..") == VFS_OK);
    CHECK(vfs_chdir("/") == VFS_OK);
    CHECK(vfs_remove("/home/astra/nested/one.txt") == VFS_OK);
    CHECK(vfs_remove("/home/astra/nested") == VFS_OK);
    CHECK(vfs_list("/", tiny_entries, 1u, &count) == VFS_ERR_BUFFER_TOO_SMALL);
    CHECK(count > 1u);
    for (size_t i = 0; i < sizeof long_path - 1u; i++) {
        long_path[i] = 'x';
    }
    long_path[sizeof long_path - 1u] = '\0';
    CHECK(vfs_create(long_path, false) == VFS_ERR_PATH_TOO_LONG);
    CHECK(vfs_chmod("/README.txt", 010000u) == VFS_ERR_INVALID_MODE);
    CHECK(vfs_chmod("/README.txt", 0000u) == VFS_OK);
    CHECK(vfs_read("/README.txt", cwd, sizeof cwd, &count) == VFS_ERR_PERMISSION_DENIED);
}

int main(void) {
    test_initial_filesystem();
    test_create_read_write_and_capacity();
    test_permissions();
    test_paths_removal_and_modes();

    if (g_failures != 0u) {
        (void)fprintf(stderr, "%u VFS test(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)puts("All VFS tests passed.");
    return EXIT_SUCCESS;
}
