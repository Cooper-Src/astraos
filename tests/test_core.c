#include <stdio.h>
#include <stdlib.h>

#include "../src/main.c"

static int test_failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            (void)fprintf(stderr, "%s:%d: CHECK failed: %s\\n", __FILE__,   \
                           __LINE__, #condition);                               \
            test_failures++;                                                    \
        }                                                                       \
    } while (0)

static void test_octal_parser(void) {
    CHAR16 long_mode[LINE_CAP] = {0};
    size_t i = 0;

    CHECK(parse_octal(L"0") == 0);
    CHECK(parse_octal(L"755") == 0755);
    CHECK(parse_octal(L"1777") == 01777);
    CHECK(parse_octal(L"7777") == 07777);
    CHECK(parse_octal(L"") == -1);
    CHECK(parse_octal(L"8") == -1);
    CHECK(parse_octal(L"12x") == -1);
    CHECK(parse_octal(L"77777") == -1);

    for (i = 0; i + 1u < ARRAY_COUNT(long_mode); i++) {
        long_mode[i] = (CHAR16)'7';
    }
    long_mode[ARRAY_COUNT(long_mode) - 1u] = 0;
    CHECK(parse_octal(long_mode) == -1);
}

static void test_paths_and_permissions(void) {
    int readme = -1;
    int notes = -1;
    int astra_home = -1;

    CHECK(fs_init());
    CHECK(resolve_path(L"/") == 0);
    CHECK(resolve_path(L"/../../etc/motd") >= 0);
    CHECK(resolve_path(L"/README.txt/") == -1);
    CHECK(resolve_path(L"/tmp/12345678901234567890123456789012") == -1);

    readme = resolve_path(L"/README.txt");
    notes = resolve_path(L"/home/astra/notes.txt");
    astra_home = resolve_path(L"/home/astra");
    CHECK(readme >= 0);
    CHECK(notes >= 0);
    CHECK(astra_home >= 0);
    if (readme < 0 || notes < 0 || astra_home < 0) {
        return;
    }

    gUid = ASTRA_UID;
    CHECK(can_access(&gNodes[readme], 4u));
    CHECK(!can_access(&gNodes[readme], 2u));
    CHECK(can_access(&gNodes[notes], 4u));
    CHECK(can_access(&gNodes[notes], 2u));

    gNodes[astra_home].mode = 0400u;
    CHECK(can_access(&gNodes[astra_home], 4u));
    CHECK(!can_access(&gNodes[astra_home], 5u));
    gNodes[astra_home].mode = 0750u;

    {
        CHAR16 chmod_args[] = L"000 /README.txt";
        cmd_chmod(chmod_args);
    }
    CHECK(gNodes[readme].mode == 0644u);

    gUid = ROOT_UID;
    CHECK(can_access(&gNodes[readme], 7u));
}

static void test_names_cwd_and_sticky_directory(void) {
    const CHAR16 prefix[] = L"/tmp/";
    CHAR16 too_long_name[LINE_CAP] = {0};
    size_t prefix_len = ARRAY_COUNT(prefix) - 1u;
    size_t i = 0;
    int tmp = -1;
    int cwd_node = -1;
    int root_owned = -1;
    int astra_owned = -1;

    CHECK(fs_init());
    tmp = resolve_path(L"/tmp");
    CHECK(tmp >= 0);
    if (tmp < 0) {
        return;
    }

    for (i = 0; i < prefix_len; i++) {
        too_long_name[i] = prefix[i];
    }
    for (i = 0; i < NAME_CAP; i++) {
        too_long_name[prefix_len + i] = (CHAR16)'x';
    }
    too_long_name[prefix_len + NAME_CAP] = 0;
    cmd_create(too_long_name, FALSE);
    CHECK(child_find(tmp, too_long_name + prefix_len) == -1);

    cmd_create(L"/tmp/cwd-guard", TRUE);
    cwd_node = resolve_path(L"/tmp/cwd-guard");
    CHECK(cwd_node >= 0);
    if (cwd_node >= 0) {
        gCwd = cwd_node;
        cmd_rm(L"/tmp/cwd-guard");
        CHECK(gNodes[cwd_node].used);
        gCwd = 0;
        cmd_rm(L"/tmp/cwd-guard");
        CHECK(!gNodes[cwd_node].used);
    }

    cmd_create(L"/tmp/root-owned", FALSE);
    root_owned = resolve_path(L"/tmp/root-owned");
    CHECK(root_owned >= 0);
    if (root_owned < 0) {
        return;
    }

    gUid = ASTRA_UID;
    cmd_rm(L"/tmp/root-owned");
    CHECK(gNodes[root_owned].used);

    cmd_create(L"/tmp/astra-owned", FALSE);
    astra_owned = resolve_path(L"/tmp/astra-owned");
    CHECK(astra_owned >= 0);
    if (astra_owned >= 0) {
        CHECK(gNodes[astra_owned].owner == ASTRA_UID);
        cmd_rm(L"/tmp/astra-owned");
        CHECK(!gNodes[astra_owned].used);
    }
}

int main(void) {
    test_octal_parser();
    test_paths_and_permissions();
    test_names_cwd_and_sticky_directory();

    if (test_failures != 0) {
        (void)fprintf(stderr, "%d test(s) failed\\n", test_failures);
        return EXIT_FAILURE;
    }

    (void)puts("AstraOS core tests passed.");
    return EXIT_SUCCESS;
}
