#include "vfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            (void)fprintf(stderr, "%s:%d: CHECK failed: %s\n", \
                __FILE__, __LINE__, #expression); \
            g_failures++; \
        } \
    } while (0)

static AstraVfsResult create_path(AstraVfs *fs, const char *path, bool is_directory) {
    return astra_vfs_create(fs, path, strlen(path), is_directory);
}

static AstraVfsResult remove_path(AstraVfs *fs, const char *path) {
    return astra_vfs_remove(fs, path, strlen(path));
}

static AstraVfsResult chmod_path(AstraVfs *fs, const char *path, uint16_t mode) {
    return astra_vfs_chmod(fs, path, strlen(path), mode);
}

static AstraVfsResult read_path(
    const AstraVfs *fs,
    const char *path,
    char *buffer,
    size_t capacity,
    size_t *read_out
) {
    return astra_vfs_read(fs, path, strlen(path), buffer, capacity, read_out);
}

static void test_initial_tree_and_pwd(void) {
    AstraVfs fs;
    char pwd[ASTRA_VFS_PATH_CAP];
    size_t written = 0;
    AstraVfsEntry entry;

    CHECK(astra_vfs_init(&fs) == ASTRA_VFS_OK);
    CHECK(astra_vfs_pwd(&fs, pwd, sizeof pwd, &written) == ASTRA_VFS_OK);
    CHECK(strcmp(pwd, "/") == 0);
    CHECK(written == 1u);
    CHECK(astra_vfs_stat(&fs, "/etc/motd", 9u, &entry) == ASTRA_VFS_OK);
    CHECK(strcmp(entry.name, "motd") == 0);
    CHECK(entry.owner_uid == (uint32_t)ASTRA_VFS_ROOT_UID);
    CHECK(!entry.is_directory);
    CHECK(astra_vfs_stat(&fs, "/home/astra/notes.txt", 21u, &entry) == ASTRA_VFS_OK);
    CHECK(strcmp(entry.name, "notes.txt") == 0);
    CHECK(entry.owner_uid == (uint32_t)ASTRA_VFS_ASTRA_UID);

    CHECK(astra_vfs_chdir(&fs, "/home/astra", 11u) == ASTRA_VFS_OK);
    CHECK(astra_vfs_pwd(&fs, pwd, sizeof pwd, &written) == ASTRA_VFS_OK);
    CHECK(strcmp(pwd, "/home/astra") == 0);
    CHECK(written == strlen("/home/astra"));
    CHECK(astra_vfs_chdir(&fs, "..", 2u) == ASTRA_VFS_OK);
    CHECK(astra_vfs_pwd(&fs, pwd, sizeof pwd, &written) == ASTRA_VFS_OK);
    CHECK(strcmp(pwd, "/home") == 0);
}

static void test_read_write_and_capacity(void) {
    AstraVfs fs;
    char content[ASTRA_VFS_CONTENT_CAP];
    char original[ASTRA_VFS_CONTENT_CAP];
    char replacement[ASTRA_VFS_CONTENT_CAP];
    size_t read_len = 0;

    CHECK(astra_vfs_init(&fs) == ASTRA_VFS_OK);
    CHECK(read_path(&fs, "/README.txt", content, sizeof content, &read_len) == ASTRA_VFS_OK);
    CHECK(read_len > 0u);
    CHECK(memcmp(content, "Welcome to AstraOS.", strlen("Welcome to AstraOS.")) == 0);

    for (size_t i = 0; i < sizeof replacement; i++) {
        replacement[i] = (char)('a' + (int)(i % 26u));
    }
    CHECK(create_path(&fs, "/tmp/payload", false) == ASTRA_VFS_OK);
    CHECK(astra_vfs_write(&fs, "/tmp/payload", strlen("/tmp/payload"),
        replacement, sizeof replacement) == ASTRA_VFS_OK);
    CHECK(read_path(&fs, "/tmp/payload", content, sizeof content, &read_len) == ASTRA_VFS_OK);
    CHECK(read_len == sizeof replacement);
    CHECK(memcmp(content, replacement, sizeof replacement) == 0);

    CHECK(read_path(&fs, "/tmp/payload", content, sizeof content - 1u,
        &read_len) == ASTRA_VFS_BUFFER_TOO_SMALL);
    CHECK(read_len == sizeof replacement);

    CHECK(read_path(&fs, "/tmp/payload", original, sizeof original, &read_len) == ASTRA_VFS_OK);
    CHECK(astra_vfs_write(&fs, "/tmp/payload", strlen("/tmp/payload"),
        replacement, sizeof replacement + 1u) == ASTRA_VFS_CONTENT_TOO_LARGE);
    CHECK(read_path(&fs, "/tmp/payload", content, sizeof content, &read_len) == ASTRA_VFS_OK);
    CHECK(memcmp(content, original, sizeof original) == 0);
}

static void test_permissions_and_owner_chmod(void) {
    AstraVfs fs;
    char buffer[ASTRA_VFS_CONTENT_CAP];
    size_t read_len = 0;

    CHECK(astra_vfs_init(&fs) == ASTRA_VFS_OK);
    CHECK(chmod_path(&fs, "/etc/motd", 0000u) == ASTRA_VFS_OK);
    CHECK(astra_vfs_set_uid(&fs, (uint32_t)ASTRA_VFS_ASTRA_UID) == ASTRA_VFS_OK);
    CHECK(read_path(&fs, "/etc/motd", buffer, sizeof buffer, &read_len) ==
        ASTRA_VFS_PERMISSION_DENIED);
    CHECK(chmod_path(&fs, "/etc/motd", 0644u) == ASTRA_VFS_PERMISSION_DENIED);

    CHECK(astra_vfs_init(&fs) == ASTRA_VFS_OK);
    CHECK(chmod_path(&fs, "/home", 0700u) == ASTRA_VFS_OK);
    CHECK(astra_vfs_set_uid(&fs, (uint32_t)ASTRA_VFS_ASTRA_UID) == ASTRA_VFS_OK);
    CHECK(astra_vfs_stat(&fs, "/home/astra/notes.txt", 21u,
        &(AstraVfsEntry){0}) == ASTRA_VFS_PERMISSION_DENIED);
    CHECK(astra_vfs_chdir(&fs, "/home", 5u) == ASTRA_VFS_PERMISSION_DENIED);
}

static void test_sticky_directory(void) {
    AstraVfs fs;

    CHECK(astra_vfs_init(&fs) == ASTRA_VFS_OK);
    CHECK(create_path(&fs, "/tmp/root-owned", false) == ASTRA_VFS_OK);
    CHECK(astra_vfs_set_uid(&fs, (uint32_t)ASTRA_VFS_ASTRA_UID) == ASTRA_VFS_OK);
    CHECK(remove_path(&fs, "/tmp/root-owned") == ASTRA_VFS_PERMISSION_DENIED);
    CHECK(create_path(&fs, "/tmp/astra-owned", false) == ASTRA_VFS_OK);
    CHECK(remove_path(&fs, "/tmp/astra-owned") == ASTRA_VFS_OK);

    CHECK(astra_vfs_set_uid(&fs, (uint32_t)ASTRA_VFS_ROOT_UID) == ASTRA_VFS_OK);
    CHECK(remove_path(&fs, "/tmp/root-owned") == ASTRA_VFS_OK);
}

static void test_invalid_names_paths_and_capacity(void) {
    AstraVfs fs;
    char long_name[ASTRA_VFS_NAME_CAP + 8u];
    char overlong_path[ASTRA_VFS_PATH_CAP + 1u];
    AstraVfsEntry entries[2];
    AstraVfsEntry sentinel = {0};
    size_t count = 0;

    CHECK(astra_vfs_init(&fs) == ASTRA_VFS_OK);
    CHECK(create_path(&fs, "/tmp/existing", false) == ASTRA_VFS_OK);
    CHECK(create_path(&fs, "/tmp/existing", false) == ASTRA_VFS_ALREADY_EXISTS);
    CHECK(create_path(&fs, "/tmp/name/", false) == ASTRA_VFS_INVALID_NAME);
    CHECK(astra_vfs_stat(&fs, "/README.txt/", 12u, &(AstraVfsEntry){0}) ==
        ASTRA_VFS_NOT_DIRECTORY);

    for (size_t i = 0; i < sizeof long_name; i++) {
        long_name[i] = 'z';
    }
    CHECK(astra_vfs_create(&fs, long_name, sizeof long_name, false) ==
        ASTRA_VFS_NAME_TOO_LONG);

    for (size_t i = 0; i < sizeof overlong_path; i++) {
        overlong_path[i] = 'x';
    }
    CHECK(astra_vfs_stat(&fs, overlong_path, sizeof overlong_path,
        &(AstraVfsEntry){0}) == ASTRA_VFS_PATH_TOO_LONG);
    CHECK(astra_vfs_stat(&fs, "a\0b", 3u, &(AstraVfsEntry){0}) ==
        ASTRA_VFS_INVALID_ARGUMENT);

    sentinel.name[0] = '!';
    sentinel.name[1] = '\0';
    sentinel.is_directory = true;
    sentinel.owner_uid = UINT32_MAX;
    sentinel.mode = UINT16_MAX;
    sentinel.size = SIZE_MAX;
    entries[0] = sentinel;
    entries[1] = sentinel;
    CHECK(astra_vfs_list(&fs, "/", 1u, entries, 2u, &count) ==
        ASTRA_VFS_BUFFER_TOO_SMALL);
    CHECK(count == 4u);
    CHECK(entries[0].name[0] == '!');
    CHECK(entries[1].name[0] == '!');
}

static void test_empty_files_and_directory_removal(void) {
    AstraVfs fs;
    size_t read_len = 99u;

    CHECK(astra_vfs_init(&fs) == ASTRA_VFS_OK);
    CHECK(create_path(&fs, "/tmp/empty", false) == ASTRA_VFS_OK);
    CHECK(astra_vfs_read(&fs, "/tmp/empty", strlen("/tmp/empty"),
        NULL, 0u, &read_len) == ASTRA_VFS_OK);
    CHECK(read_len == 0u);
    CHECK(create_path(&fs, "/tmp/dir", true) == ASTRA_VFS_OK);
    CHECK(create_path(&fs, "/tmp/dir/file", false) == ASTRA_VFS_OK);
    CHECK(remove_path(&fs, "/tmp/dir") == ASTRA_VFS_DIRECTORY_NOT_EMPTY);
    CHECK(remove_path(&fs, "/tmp/dir/file") == ASTRA_VFS_OK);
    CHECK(remove_path(&fs, "/tmp/dir") == ASTRA_VFS_OK);
    CHECK(remove_path(&fs, "/") == ASTRA_VFS_PERMISSION_DENIED);
}

static void test_buffer_too_small_and_invalid_arguments(void) {
    AstraVfs fs;
    char small[2];
    size_t written = 0;

    CHECK(astra_vfs_init(&fs) == ASTRA_VFS_OK);
    CHECK(astra_vfs_pwd(&fs, small, sizeof small, &written) == ASTRA_VFS_OK);
    CHECK(strcmp(small, "/") == 0);
    CHECK(astra_vfs_pwd(&fs, small, 1u, &written) == ASTRA_VFS_BUFFER_TOO_SMALL);
    CHECK(written == 1u);
    CHECK(astra_vfs_init(NULL) == ASTRA_VFS_INVALID_ARGUMENT);
    CHECK(astra_vfs_set_uid(NULL, 0u) == ASTRA_VFS_INVALID_ARGUMENT);
    CHECK(astra_vfs_chdir(&fs, NULL, 1u) == ASTRA_VFS_INVALID_ARGUMENT);
    CHECK(astra_vfs_write(&fs, "/tmp/x", 6u, NULL, 1u) == ASTRA_VFS_INVALID_ARGUMENT);
    CHECK(chmod_path(&fs, "/tmp/x", 010000u) == ASTRA_VFS_INVALID_ARGUMENT);
}

static void test_mode_formatting(void) {
    char mode[11] = {0};
    char small[10] = {0};
    const char sentinel = '!';

    CHECK(astra_vfs_format_mode(01777u, true, mode, sizeof mode));
    CHECK(strcmp(mode, "drwxrwxrwt") == 0);

    CHECK(astra_vfs_format_mode(04755u, false, mode, sizeof mode));
    CHECK(strcmp(mode, "-rwsr-xr-x") == 0);

    CHECK(astra_vfs_format_mode(04644u, false, mode, sizeof mode));
    CHECK(strcmp(mode, "-rwSr--r--") == 0);

    CHECK(astra_vfs_format_mode(02750u, true, mode, sizeof mode));
    CHECK(strcmp(mode, "drwxr-s---") == 0);

    mode[0] = sentinel;
    CHECK(!astra_vfs_format_mode(0644u, false, small, sizeof small));
    CHECK(small[0] == '\0');
    CHECK(!astra_vfs_format_mode(010000u, false, mode, sizeof mode));
    CHECK(mode[0] == sentinel);
    CHECK(!astra_vfs_format_mode(0644u, false, NULL, sizeof mode));
}

static void test_create_enforces_absolute_path_limit(void) {
    AstraVfs fs;
    char component[ASTRA_VFS_NAME_CAP] = {0};
    char current_path[ASTRA_VFS_PATH_CAP] = {0};
    size_t written = 0u;

    for (size_t i = 0; i + 1u < sizeof component; i++) {
        component[i] = 'n';
    }

    CHECK(astra_vfs_init(&fs) == ASTRA_VFS_OK);
    for (size_t i = 0; i < 7u; i++) {
        CHECK(astra_vfs_create(&fs, component, sizeof component - 1u, true)
            == ASTRA_VFS_OK);
        CHECK(astra_vfs_chdir(&fs, component, sizeof component - 1u)
            == ASTRA_VFS_OK);
    }

    CHECK(astra_vfs_pwd(&fs, current_path, sizeof current_path, &written)
        == ASTRA_VFS_OK);
    CHECK(written == 224u);
    CHECK(astra_vfs_create(&fs, component, sizeof component - 1u, true)
        == ASTRA_VFS_PATH_TOO_LONG);
    CHECK(astra_vfs_stat(&fs, component, sizeof component - 1u,
        &(AstraVfsEntry){0}) == ASTRA_VFS_NOT_FOUND);
}

int main(void) {
    test_mode_formatting();
    test_create_enforces_absolute_path_limit();
    test_initial_tree_and_pwd();
    test_read_write_and_capacity();
    test_permissions_and_owner_chmod();
    test_sticky_directory();
    test_invalid_names_paths_and_capacity();
    test_empty_files_and_directory_removal();
    test_buffer_too_small_and_invalid_arguments();

    if (g_failures != 0) {
        (void)fprintf(stderr, "%d AstraOS VFS test(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)puts("All AstraOS VFS tests passed.");
    return EXIT_SUCCESS;
}
