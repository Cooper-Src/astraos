#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/main.c"

enum { TEST_OUTPUT_CAP = 16384 };

static char gTestOutput[TEST_OUTPUT_CAP] = {0};
static size_t gTestOutputLen = 0;
static int gFailures = 0;

static EFI_STATUS EFIAPI test_output_string(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *protocol,
    CHAR16 *string
) {
    UINTN i = 0;
    (void)protocol;

    while (string[i] != 0) {
        if (gTestOutputLen + 1u >= TEST_OUTPUT_CAP) {
            return 1;
        }
        gTestOutput[gTestOutputLen] =
            string[i] <= 0x7fu ? (char)string[i] : '?';
        gTestOutputLen++;
        i++;
    }
    gTestOutput[gTestOutputLen] = '\0';
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI test_clear_screen(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *protocol
) {
    (void)protocol;
    gTestOutputLen = 0;
    gTestOutput[0] = '\0';
    return EFI_SUCCESS;
}

static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL gTestConsole = {
    .OutputString = test_output_string,
    .ClearScreen = test_clear_screen
};

static void check_result(int condition, const char *expression, int line) {
    if (condition == 0) {
        (void)fprintf(stderr, "line %d: check failed: %s\n", line, expression);
        gFailures++;
    }
}

#define CHECK(expression) check_result((expression), #expression, __LINE__)

static void reset_test_environment(void) {
    fs_init();
    gCwd = 0;
    gUid = ROOT_UID;
    gOut = &gTestConsole;
    gOutputError = EFI_SUCCESS;
    gTestOutputLen = 0;
    gTestOutput[0] = '\0';
}

static void test_path_resolution(void) {
    CHAR16 too_long[LINE_CAP] = {0};

    reset_test_environment();
    CHECK(resolve_path(L"/") == 0);
    CHECK(resolve_path(L"/etc/motd") == 5);
    CHECK(resolve_path(L"/home/astra/notes.txt") == 7);
    CHECK(resolve_path(L"/README.txt/") == -1);

    too_long[0] = '/';
    for (UINTN i = 1; i < NAME_CAP + 1u; i++) {
        too_long[i] = 'x';
    }
    too_long[NAME_CAP + 1u] = 0;
    CHECK(resolve_path(too_long) == -1);

    gUid = ASTRA_UID;
    gNodes[2].mode = 0700;
    CHECK(resolve_path(L"/home/astra/notes.txt") == -1);
}

static void test_creation_rejects_truncated_names(void) {
    CHAR16 path[LINE_CAP] = {0};

    reset_test_environment();
    for (UINTN i = 0; i < NAME_CAP + 8u; i++) {
        path[i] = 'n';
    }
    path[NAME_CAP + 8u] = 0;
    cmd_create(path, FALSE);
    CHECK(strstr(gTestOutput, "name too long") != NULL);
    CHECK(child_find(0, L"nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn") == -1);
}

static void test_octal_parser_limits_values_without_overflow(void) {
    CHAR16 value[260] = {0};

    reset_test_environment();
    CHECK(parse_octal(L"0") == 0);
    CHECK(parse_octal(L"755") == 0755);
    CHECK(parse_octal(L"1777") == 01777);
    CHECK(parse_octal(L"888") == -1);
    CHECK(parse_octal(L"10000") == -1);

    for (UINTN i = 0; i < 250u; i++) {
        value[i] = '7';
    }
    value[250] = 0;
    CHECK(parse_octal(value) == -1);
}

static void test_write_rejects_truncation(void) {
    CHAR16 args[CONTENT_CAP + 64u] = {0};
    CHAR16 previous[CONTENT_CAP] = {0};
    UINTN used = 0;

    reset_test_environment();
    s_copy(previous, gNodes[4].content, CONTENT_CAP);
    s_copy(args, L"README.txt ", ARRAY_COUNT(args));
    used = s_len(args);
    for (UINTN i = 0; i < CONTENT_CAP; i++) {
        args[used + i] = 'x';
    }
    args[used + CONTENT_CAP] = 0;

    cmd_write(args);
    CHECK(strstr(gTestOutput, "content too long") != NULL);
    CHECK(s_cmp(gNodes[4].content, previous) == 0);
}

static void test_sticky_directory_protection(void) {
    int entry = -1;

    reset_test_environment();
    cmd_create(L"/tmp/root-file", FALSE);
    entry = child_find(3, L"root-file");
    CHECK(entry >= 0);

    gUid = ASTRA_UID;
    gTestOutputLen = 0;
    gTestOutput[0] = '\0';
    cmd_rm(L"/tmp/root-file");
    CHECK(strstr(gTestOutput, "sticky directory") != NULL);
    CHECK(child_find(3, L"root-file") == entry);

    gUid = ROOT_UID;
    cmd_rm(L"/tmp/root-file");
    CHECK(child_find(3, L"root-file") == -1);
}

static void test_owner_can_remove_own_sticky_directory_entry(void) {
    reset_test_environment();
    gUid = ASTRA_UID;
    cmd_create(L"/tmp/astra-file", FALSE);
    CHECK(child_find(3, L"astra-file") >= 0);

    cmd_rm(L"/tmp/astra-file");
    CHECK(child_find(3, L"astra-file") == -1);
}

static void test_chmod_rejects_other_users(void) {
    CHAR16 args[] = L"000 /etc/motd";
    UINT16 original_mode = 0;

    reset_test_environment();
    original_mode = gNodes[5].mode;
    gUid = ASTRA_UID;
    cmd_chmod(args);
    CHECK(gNodes[5].mode == original_mode);
    CHECK(strstr(gTestOutput, "only root or the owner may chmod") != NULL);
}

int main(void) {
    test_path_resolution();
    test_creation_rejects_truncated_names();
    test_octal_parser_limits_values_without_overflow();
    test_write_rejects_truncation();
    test_sticky_directory_protection();
    test_owner_can_remove_own_sticky_directory_entry();
    test_chmod_rejects_other_users();

    if (gFailures != 0) {
        (void)fprintf(stderr, "%d AstraOS shell test(s) failed\n", gFailures);
        return EXIT_FAILURE;
    }
    (void)puts("All AstraOS shell tests passed.");
    return EXIT_SUCCESS;
}
