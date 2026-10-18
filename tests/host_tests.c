#include <stdio.h>
#include <stdlib.h>

#define ASTRAOS_TEST 1
#include "../src/main.c"

enum { TEST_OUTPUT_CAP = 8192 };

static CHAR16 test_output_buffer[TEST_OUTPUT_CAP] = {0};
static UINTN test_output_length = 0;
static int test_failures = 0;

static EFI_STATUS EFIAPI test_output(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self,
    CHAR16 *text);
static EFI_STATUS EFIAPI test_clear(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self);

static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL test_console = {
    .OutputString = test_output,
    .ClearScreen = test_clear
};

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #condition);                                                \
            test_failures++;                                                    \
        }                                                                       \
    } while (0)

static EFI_STATUS EFIAPI test_output(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self,
    CHAR16 *text)
{
    UINTN i = 0;

    (void)self;
    if (text == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    while (text[i] != 0) {
        if (test_output_length + 1u >= (UINTN)TEST_OUTPUT_CAP) {
            return EFI_OUT_OF_RESOURCES;
        }
        test_output_buffer[test_output_length] = text[i];
        test_output_length++;
        i++;
    }
    test_output_buffer[test_output_length] = 0;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI test_clear(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *self)
{
    (void)self;
    test_output_length = 0;
    test_output_buffer[0] = 0;
    return EFI_SUCCESS;
}

static void reset_test_output(void)
{
    test_output_length = 0;
    test_output_buffer[0] = 0;
    gOutputError = EFI_SUCCESS;
}

static BOOLEAN text_contains(const CHAR16 *text, const CHAR16 *needle)
{
    UINTN text_length = 0;
    UINTN needle_length = 0;
    UINTN i = 0;
    UINTN j = 0;

    if (text == NULL || needle == NULL) {
        return FALSE;
    }
    text_length = s_len(text);
    needle_length = s_len(needle);
    if (needle_length > text_length) {
        return FALSE;
    }

    for (i = 0; i <= text_length - needle_length; i++) {
        j = 0;
        while (j < needle_length && text[i + j] == needle[j]) {
            j++;
        }
        if (j == needle_length) {
            return TRUE;
        }
    }
    return FALSE;
}

static void run_command(const CHAR16 *command)
{
    reset_test_output();
    s_copy(gLine, command, LINE_CAP);
    dispatch(gLine);
    CHECK(gOutputError == EFI_SUCCESS);
}

static UINTN count_used_nodes(void)
{
    UINTN count = 0;
    UINTN i = 0;

    for (i = 0; i < MAX_NODES; i++) {
        if (gNodes[i].used) {
            count++;
        }
    }
    return count;
}

static void test_root_execute_policy(void)
{
    int readme = -1;

    CHECK(fs_init());
    gUid = ROOT_UID;
    readme = resolve_path(L"/README.txt");
    CHECK(readme > 0);
    if (readme > 0) {
        CHECK(can_access(&gNodes[readme], 4u));
        CHECK(can_access(&gNodes[readme], 2u));
        CHECK(!can_access(&gNodes[readme], 1u));
        gNodes[readme].mode = 0744u;
        CHECK(can_access(&gNodes[readme], 1u));
    }

    gUid = ASTRA_UID;
    readme = resolve_path(L"/README.txt");
    CHECK(readme > 0);
    if (readme > 0) {
        CHECK(can_access(&gNodes[readme], 4u));
        CHECK(!can_access(&gNodes[readme], 2u));
    }
}

static void test_parent_directory_permissions(void)
{
    int locked = -1;

    CHECK(fs_init());
    run_command(L"mkdir /locked");
    locked = resolve_path(L"/locked");
    CHECK(locked > 0);
    run_command(L"chmod 555 /locked");

    gUid = ASTRA_UID;
    run_command(L"touch /locked/blocked.txt");
    CHECK(text_contains(test_output_buffer, L"permission denied"));
    CHECK(resolve_path(L"/locked/blocked.txt") < 0);
    CHECK(locked > 0);
    if (locked > 0) {
        CHECK(!can_access(&gNodes[locked], 3u));
    }
}

static void test_sticky_directory_removal(void)
{
    int root_file = -1;
    int astra_file = -1;

    CHECK(fs_init());
    run_command(L"touch /tmp/root-owned.txt");
    root_file = resolve_path(L"/tmp/root-owned.txt");
    CHECK(root_file > 0);

    gUid = ASTRA_UID;
    run_command(L"rm /tmp/root-owned.txt");
    CHECK(text_contains(test_output_buffer, L"sticky directory"));
    CHECK(resolve_path(L"/tmp/root-owned.txt") == root_file);

    run_command(L"touch /tmp/astra-owned.txt");
    astra_file = resolve_path(L"/tmp/astra-owned.txt");
    CHECK(astra_file > 0);
    run_command(L"rm /tmp/astra-owned.txt");
    CHECK(text_contains(test_output_buffer, L"removed"));
    CHECK(resolve_path(L"/tmp/astra-owned.txt") < 0);
}

static void test_name_and_path_boundaries(void)
{
    CHECK(fs_init());

    run_command(L"touch 12345678901234567890123456789012");
    CHECK(text_contains(test_output_buffer, L"name too long"));
    CHECK(resolve_path(L"/12345678901234567890123456789012") < 0);

    run_command(L"touch valid.txt");
    CHECK(resolve_path(L"/valid.txt") > 0);
    CHECK(resolve_path(L"") == gCwd);
    CHECK(resolve_path(L"/") == 0);
    CHECK(resolve_path(L"/README.txt/") < 0);
}

static void test_octal_parser_boundaries(void)
{
    CHECK(parse_octal(L"0") == 0);
    CHECK(parse_octal(L"644") == 0644);
    CHECK(parse_octal(L"7777") == 07777);
    CHECK(parse_octal(L"") < 0);
    CHECK(parse_octal(L"888") < 0);
    CHECK(parse_octal(L"10000") < 0);
    CHECK(parse_octal(L"777777777777777777777777777777777777") < 0);
}

static void test_reject_oversized_write_without_modifying_file(void)
{
    CHAR16 args[CONTENT_CAP + NAME_CAP + 4] = {0};
    CHAR16 original[CONTENT_CAP] = {0};
    const CHAR16 prefix[] = L"README.txt ";
    UINTN prefix_length = s_len(prefix);
    UINTN i = 0;
    int readme = -1;

    CHECK(fs_init());
    readme = resolve_path(L"/README.txt");
    CHECK(readme > 0);
    if (readme < 0) {
        return;
    }
    s_copy(original, gNodes[readme].content, CONTENT_CAP);
    s_copy(args, prefix, (UINTN)ARRAY_COUNT(prefix));
    for (i = 0; i < (UINTN)CONTENT_CAP; i++) {
        args[prefix_length + i] = 'x';
    }
    args[prefix_length + (UINTN)CONTENT_CAP] = 0;

    reset_test_output();
    cmd_write(args);
    CHECK(text_contains(test_output_buffer, L"content too long"));
    CHECK(s_cmp(original, gNodes[readme].content) == 0);
}

static void test_filesystem_capacity_and_invalid_parent(void)
{
    CHAR16 name[NAME_CAP] = L"f00";
    UINTN created = 0;
    UINTN i = 0;
    int result = -1;

    CHECK(fs_init());
    CHECK(node_add(-1, L"invalid", FALSE, ROOT_UID, 0644u, L"") < 0);
    CHECK(node_add((int)MAX_NODES, L"invalid", FALSE, ROOT_UID, 0644u, L"") < 0);

    for (i = 0; i < MAX_NODES; i++) {
        name[1] = (CHAR16)('0' + (i / 10u));
        name[2] = (CHAR16)('0' + (i % 10u));
        result = node_add(0, name, FALSE, ROOT_UID, 0644u, L"");
        if (result < 0) {
            break;
        }
        created++;
    }

    CHECK(created == (UINTN)(MAX_NODES - 8));
    CHECK(result < 0);
    CHECK(count_used_nodes() == (UINTN)MAX_NODES);
}

int main(void)
{
    gOut = &test_console;
    gIn = NULL;
    gOutputError = EFI_SUCCESS;

    test_root_execute_policy();
    test_parent_directory_permissions();
    test_sticky_directory_removal();
    test_name_and_path_boundaries();
    test_octal_parser_boundaries();
    test_reject_oversized_write_without_modifying_file();
    test_filesystem_capacity_and_invalid_parent();

    if (test_failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", test_failures);
        return EXIT_FAILURE;
    }

    puts("AstraOS host regression tests passed.");
    return EXIT_SUCCESS;
}
