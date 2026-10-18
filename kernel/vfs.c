#include "vfs.h"

static const char g_readme[] =
    "Welcome to AstraOS.\n"
    "This kernel-owned RAM filesystem is temporary and resets after reboot.\n"
    "Type 'help' to see commands.\n";
static const char g_motd[] = "AstraOS alpha - welcome.\n";
static const char g_notes[] = "This file belongs to astra.\n";

static size_t text_length(const char *text) {
    size_t length = 0;

    if (text == NULL) {
        return 0;
    }
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static void copy_bytes(char *destination, const char *source, size_t length) {
    for (size_t i = 0; i < length; i++) {
        destination[i] = source[i];
    }
}

static void copy_name(char *destination, const char *source, size_t length) {
    copy_bytes(destination, source, length);
    destination[length] = '\0';
}

static bool span_equals(const char *span, size_t span_len, const char *text) {
    size_t i = 0;

    while (i < span_len && text[i] != '\0' && span[i] == text[i]) {
        i++;
    }
    return i == span_len && text[i] == '\0';
}

static bool valid_component(const char *name, size_t length) {
    if (name == NULL || length == 0) {
        return false;
    }
    if (length >= (size_t)ASTRA_VFS_NAME_CAP) {
        return false;
    }
    if ((length == 1u && name[0] == '.') ||
        (length == 2u && name[0] == '.' && name[1] == '.')) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        unsigned char byte = (unsigned char)name[i];
        if (byte < 0x21u || byte > 0x7eu || byte == '/') {
            return false;
        }
    }
    return true;
}

static bool can_access(const AstraVfs *fs, const AstraVfsNode *node, uint16_t access) {
    uint16_t available = 0;

    if (fs->uid == (uint32_t)ASTRA_VFS_ROOT_UID) {
        return true;
    }
    if (fs->uid == node->owner_uid) {
        available = (uint16_t)((node->mode >> 6) & 7u);
    } else {
        available = (uint16_t)(node->mode & 7u);
    }
    return (available & access) == access;
}

static int find_child(
    const AstraVfs *fs,
    int parent,
    const char *name,
    size_t name_len
) {
    if (parent < 0 || parent >= ASTRA_VFS_MAX_NODES) {
        return -1;
    }

    for (size_t i = 1; i < (size_t)ASTRA_VFS_MAX_NODES; i++) {
        const AstraVfsNode *node = &fs->nodes[i];
        if (node->used && node->parent == parent &&
            span_equals(name, name_len, node->name)) {
            return (int)i;
        }
    }
    return -1;
}

static AstraVfsResult resolve_path(
    const AstraVfs *fs,
    const char *path,
    size_t path_len,
    int *node_id_out
) {
    char component[ASTRA_VFS_NAME_CAP];
    size_t position = 0;
    int current = 0;

    if (fs == NULL || path == NULL || node_id_out == NULL || path_len == 0) {
        return ASTRA_VFS_INVALID_ARGUMENT;
    }
    if (path_len >= (size_t)ASTRA_VFS_PATH_CAP) {
        return ASTRA_VFS_PATH_TOO_LONG;
    }
    for (size_t i = 0; i < path_len; i++) {
        if (path[i] == '\0') {
            return ASTRA_VFS_INVALID_ARGUMENT;
        }
    }

    current = path[0] == '/' ? 0 : fs->cwd;
    while (position < path_len) {
        size_t start = 0;
        size_t component_len = 0;

        while (position < path_len && path[position] == '/') {
            position++;
        }
        if (position == path_len) {
            break;
        }

        if (current < 0 || current >= ASTRA_VFS_MAX_NODES ||
            !fs->nodes[current].used) {
            return ASTRA_VFS_NOT_FOUND;
        }
        if (!fs->nodes[current].is_directory) {
            return ASTRA_VFS_NOT_DIRECTORY;
        }
        if (!can_access(fs, &fs->nodes[current], 1u)) {
            return ASTRA_VFS_PERMISSION_DENIED;
        }

        start = position;
        while (position < path_len && path[position] != '/') {
            position++;
        }
        component_len = position - start;
        if (component_len >= (size_t)ASTRA_VFS_NAME_CAP) {
            return ASTRA_VFS_NAME_TOO_LONG;
        }
        copy_name(component, &path[start], component_len);

        if (span_equals(component, component_len, ".")) {
            continue;
        }
        if (span_equals(component, component_len, "..")) {
            if (current != 0) {
                current = fs->nodes[current].parent;
            }
            continue;
        }
        if (!valid_component(component, component_len)) {
            return ASTRA_VFS_INVALID_NAME;
        }

        current = find_child(fs, current, component, component_len);
        if (current < 0) {
            return ASTRA_VFS_NOT_FOUND;
        }
    }

    if (path[path_len - 1u] == '/' && !fs->nodes[current].is_directory) {
        return ASTRA_VFS_NOT_DIRECTORY;
    }
    *node_id_out = current;
    return ASTRA_VFS_OK;
}

static AstraVfsResult add_node(
    AstraVfs *fs,
    int parent,
    const char *name,
    size_t name_len,
    bool is_directory,
    uint32_t owner_uid,
    uint16_t mode,
    const char *content,
    size_t content_len,
    int *node_id_out
) {
    size_t free_index = 0;
    AstraVfsNode *node = NULL;

    if (parent < 0 || parent >= ASTRA_VFS_MAX_NODES ||
        !fs->nodes[parent].used || !fs->nodes[parent].is_directory ||
        !valid_component(name, name_len) ||
        content_len > (size_t)ASTRA_VFS_CONTENT_CAP ||
        (content_len > 0 && content == NULL) ||
        node_id_out == NULL) {
        return ASTRA_VFS_INVALID_ARGUMENT;
    }

    for (free_index = 1; free_index < (size_t)ASTRA_VFS_MAX_NODES; free_index++) {
        if (!fs->nodes[free_index].used) {
            break;
        }
    }
    if (free_index == (size_t)ASTRA_VFS_MAX_NODES) {
        return ASTRA_VFS_NO_SPACE;
    }

    node = &fs->nodes[free_index];
    node->is_directory = is_directory;
    node->owner_uid = owner_uid;
    node->mode = mode;
    node->parent = parent;
    copy_name(node->name, name, name_len);
    if (content_len > 0) {
        copy_bytes(node->content, content, content_len);
    }
    node->content_len = content_len;
    node->used = true;
    *node_id_out = (int)free_index;
    return ASTRA_VFS_OK;
}

static void entry_from_node(const AstraVfsNode *node, AstraVfsEntry *entry) {
    size_t name_len = text_length(node->name);

    copy_name(entry->name, node->name, name_len);
    entry->is_directory = node->is_directory;
    entry->owner_uid = node->owner_uid;
    entry->mode = node->mode;
    entry->size = node->content_len;
}

AstraVfsResult astra_vfs_init(AstraVfs *fs) {
    int etc_id = -1;
    int home_id = -1;
    int tmp_id = -1;
    int readme_id = -1;
    int motd_id = -1;
    int astra_id = -1;
    int notes_id = -1;
    AstraVfsResult result = ASTRA_VFS_OK;

    if (fs == NULL) {
        return ASTRA_VFS_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < (size_t)ASTRA_VFS_MAX_NODES; i++) {
        fs->nodes[i].used = false;
        fs->nodes[i].is_directory = false;
        fs->nodes[i].owner_uid = 0;
        fs->nodes[i].mode = 0;
        fs->nodes[i].parent = -1;
        fs->nodes[i].name[0] = '\0';
        fs->nodes[i].content_len = 0;
    }

    fs->cwd = 0;
    fs->uid = (uint32_t)ASTRA_VFS_ROOT_UID;
    fs->nodes[0].is_directory = true;
    fs->nodes[0].owner_uid = (uint32_t)ASTRA_VFS_ROOT_UID;
    fs->nodes[0].mode = 0755u;
    fs->nodes[0].parent = 0;
    fs->nodes[0].name[0] = '/';
    fs->nodes[0].name[1] = '\0';
    fs->nodes[0].used = true;

    result = add_node(fs, 0, "etc", 3u, true, 0u, 0755u, NULL, 0, &etc_id);
    if (result != ASTRA_VFS_OK) {
        return result;
    }
    result = add_node(fs, 0, "home", 4u, true, 0u, 0755u, NULL, 0, &home_id);
    if (result != ASTRA_VFS_OK) {
        return result;
    }
    result = add_node(fs, 0, "tmp", 3u, true, 0u, 01777u, NULL, 0, &tmp_id);
    if (result != ASTRA_VFS_OK) {
        return result;
    }
    result = add_node(fs, 0, "README.txt", 10u, false, 0u, 0644u,
        g_readme, sizeof g_readme - 1u, &readme_id);
    if (result != ASTRA_VFS_OK) {
        return result;
    }
    result = add_node(fs, etc_id, "motd", 4u, false, 0u, 0644u,
        g_motd, sizeof g_motd - 1u, &motd_id);
    if (result != ASTRA_VFS_OK) {
        return result;
    }
    result = add_node(fs, home_id, "astra", 5u, true,
        (uint32_t)ASTRA_VFS_ASTRA_UID, 0750u, NULL, 0, &astra_id);
    if (result != ASTRA_VFS_OK) {
        return result;
    }
    result = add_node(fs, astra_id, "notes.txt", 9u, false,
        (uint32_t)ASTRA_VFS_ASTRA_UID, 0640u,
        g_notes, sizeof g_notes - 1u, &notes_id);
    if (result != ASTRA_VFS_OK) {
        return result;
    }

    (void)tmp_id;
    (void)readme_id;
    (void)motd_id;
    (void)notes_id;
    return ASTRA_VFS_OK;
}

AstraVfsResult astra_vfs_set_uid(AstraVfs *fs, uint32_t uid) {
    if (fs == NULL) {
        return ASTRA_VFS_INVALID_ARGUMENT;
    }
    fs->uid = uid;
    return ASTRA_VFS_OK;
}

AstraVfsResult astra_vfs_get_uid(const AstraVfs *fs, uint32_t *uid_out) {
    if (fs == NULL || uid_out == NULL) {
        return ASTRA_VFS_INVALID_ARGUMENT;
    }
    *uid_out = fs->uid;
    return ASTRA_VFS_OK;
}

AstraVfsResult astra_vfs_chdir(AstraVfs *fs, const char *path, size_t path_len) {
    int id = -1;
    AstraVfsResult result = resolve_path(fs, path, path_len, &id);

    if (result != ASTRA_VFS_OK) {
        return result;
    }
    if (!fs->nodes[id].is_directory) {
        return ASTRA_VFS_NOT_DIRECTORY;
    }
    if (!can_access(fs, &fs->nodes[id], 1u)) {
        return ASTRA_VFS_PERMISSION_DENIED;
    }
    fs->cwd = id;
    return ASTRA_VFS_OK;
}

AstraVfsResult astra_vfs_pwd(
    const AstraVfs *fs,
    char *output,
    size_t output_capacity,
    size_t *written_out
) {
    int chain[ASTRA_VFS_MAX_NODES];
    size_t count = 0;
    size_t needed = 1u;
    size_t used = 0;
    int current = 0;

    if (fs == NULL || output == NULL || written_out == NULL) {
        return ASTRA_VFS_INVALID_ARGUMENT;
    }

    current = fs->cwd;
    while (current > 0 && count < (size_t)ASTRA_VFS_MAX_NODES) {
        if (current >= ASTRA_VFS_MAX_NODES || !fs->nodes[current].used) {
            return ASTRA_VFS_NOT_FOUND;
        }
        chain[count] = current;
        count++;
        current = fs->nodes[current].parent;
    }
    if (current != 0) {
        return ASTRA_VFS_NOT_FOUND;
    }
    if (count == 0) {
        needed = 2u;
    } else {
        for (size_t i = 0; i < count; i++) {
            needed += 1u + text_length(fs->nodes[chain[i]].name);
        }
    }
    if (output_capacity < needed) {
        *written_out = needed - 1u;
        return ASTRA_VFS_BUFFER_TOO_SMALL;
    }

    if (count == 0) {
        output[0] = '/';
        output[1] = '\0';
        *written_out = 1u;
        return ASTRA_VFS_OK;
    }

    for (size_t i = count; i-- > 0;) {
        const AstraVfsNode *node = &fs->nodes[chain[i]];
        size_t name_len = text_length(node->name);
        output[used] = '/';
        used++;
        copy_bytes(&output[used], node->name, name_len);
        used += name_len;
    }
    output[used] = '\0';
    *written_out = used;
    return ASTRA_VFS_OK;
}

AstraVfsResult astra_vfs_stat(
    const AstraVfs *fs,
    const char *path,
    size_t path_len,
    AstraVfsEntry *entry_out
) {
    int id = -1;
    AstraVfsResult result = ASTRA_VFS_OK;

    if (entry_out == NULL) {
        return ASTRA_VFS_INVALID_ARGUMENT;
    }
    result = resolve_path(fs, path, path_len, &id);
    if (result != ASTRA_VFS_OK) {
        return result;
    }
    entry_from_node(&fs->nodes[id], entry_out);
    return ASTRA_VFS_OK;
}

AstraVfsResult astra_vfs_list(
    const AstraVfs *fs,
    const char *path,
    size_t path_len,
    AstraVfsEntry *entries,
    size_t entries_capacity,
    size_t *count_out
) {
    int id = -1;
    size_t required = 0;
    AstraVfsResult result = ASTRA_VFS_OK;

    if (fs == NULL || count_out == NULL ||
        (entries_capacity > 0 && entries == NULL)) {
        return ASTRA_VFS_INVALID_ARGUMENT;
    }
    result = resolve_path(fs, path, path_len, &id);
    if (result != ASTRA_VFS_OK) {
        return result;
    }
    if (!fs->nodes[id].is_directory) {
        return ASTRA_VFS_NOT_DIRECTORY;
    }
    if (!can_access(fs, &fs->nodes[id], 5u)) {
        return ASTRA_VFS_PERMISSION_DENIED;
    }

    for (size_t i = 1; i < (size_t)ASTRA_VFS_MAX_NODES; i++) {
        if (fs->nodes[i].used && fs->nodes[i].parent == id) {
            required++;
        }
    }
    *count_out = required;
    if (required > entries_capacity) {
        return ASTRA_VFS_BUFFER_TOO_SMALL;
    }

    required = 0;
    for (size_t i = 1; i < (size_t)ASTRA_VFS_MAX_NODES; i++) {
        if (fs->nodes[i].used && fs->nodes[i].parent == id) {
            entry_from_node(&fs->nodes[i], &entries[required]);
            required++;
        }
    }
    return ASTRA_VFS_OK;
}

AstraVfsResult astra_vfs_read(
    const AstraVfs *fs,
    const char *path,
    size_t path_len,
    char *output,
    size_t output_capacity,
    size_t *read_out
) {
    int id = -1;
    const AstraVfsNode *node = NULL;
    AstraVfsResult result = ASTRA_VFS_OK;

    if (read_out == NULL) {
        return ASTRA_VFS_INVALID_ARGUMENT;
    }
    result = resolve_path(fs, path, path_len, &id);
    if (result != ASTRA_VFS_OK) {
        return result;
    }
    node = &fs->nodes[id];
    if (node->is_directory) {
        return ASTRA_VFS_IS_DIRECTORY;
    }
    if (!can_access(fs, node, 4u)) {
        return ASTRA_VFS_PERMISSION_DENIED;
    }
    *read_out = node->content_len;
    if (output_capacity < node->content_len) {
        return ASTRA_VFS_BUFFER_TOO_SMALL;
    }
    if (node->content_len > 0 && output == NULL) {
        return ASTRA_VFS_INVALID_ARGUMENT;
    }
    if (node->content_len > 0) {
        copy_bytes(output, node->content, node->content_len);
    }
    return ASTRA_VFS_OK;
}

/* Validates the parent chain and computes the absolute path's byte length. */
static bool node_absolute_path_length(
    const AstraVfs *fs,
    int node_id,
    size_t *length_out
) {
    size_t length = 1u;
    size_t depth = 0u;
    int current = node_id;

    if (fs == NULL || length_out == NULL ||
        current < 0 || current >= ASTRA_VFS_MAX_NODES ||
        !fs->nodes[0].used || !fs->nodes[0].is_directory ||
        fs->nodes[0].parent != 0) {
        return false;
    }

    while (current != 0) {
        const AstraVfsNode *node = NULL;
        size_t name_length = 0u;
        size_t addition = 0u;

        if (current < 0 || current >= ASTRA_VFS_MAX_NODES ||
            depth >= (size_t)ASTRA_VFS_MAX_NODES) {
            return false;
        }
        node = &fs->nodes[current];
        if (!node->used) {
            return false;
        }
        while (name_length < sizeof node->name &&
            node->name[name_length] != '\0') {
            name_length++;
        }
        if (name_length == 0u || name_length >= sizeof node->name) {
            return false;
        }

        addition = name_length;
        if (depth > 0u) {
            if (addition == SIZE_MAX) {
                return false;
            }
            addition++;
        }
        if (length > SIZE_MAX - addition) {
            return false;
        }
        length += addition;

        current = node->parent;
        depth++;
    }

    *length_out = length;
    return true;
}

AstraVfsResult astra_vfs_create(
    AstraVfs *fs,
    const char *path,
    size_t path_len,
    bool is_directory
) {
    char parent_path[ASTRA_VFS_PATH_CAP];
    size_t last_slash = (size_t)-1;
    size_t name_start = 0;
    size_t name_len = 0;
    int parent_id = -1;
    int existing_id = -1;
    int created_id = -1;
    AstraVfsResult result = ASTRA_VFS_OK;

    if (fs == NULL || path == NULL || path_len == 0) {
        return ASTRA_VFS_INVALID_ARGUMENT;
    }
    if (path_len >= (size_t)ASTRA_VFS_PATH_CAP) {
        return ASTRA_VFS_PATH_TOO_LONG;
    }
    for (size_t i = 0; i < path_len; i++) {
        if (path[i] == '\0') {
            return ASTRA_VFS_INVALID_ARGUMENT;
        }
        if (path[i] == '/') {
            last_slash = i;
        }
    }
    if (path[path_len - 1u] == '/') {
        return ASTRA_VFS_INVALID_NAME;
    }

    if (last_slash == (size_t)-1) {
        parent_id = fs->cwd;
        name_start = 0;
    } else if (last_slash == 0) {
        parent_id = 0;
        name_start = 1u;
    } else {
        copy_bytes(parent_path, path, last_slash);
        parent_path[last_slash] = '\0';
        result = resolve_path(fs, parent_path, last_slash, &parent_id);
        if (result != ASTRA_VFS_OK) {
            return result;
        }
        name_start = last_slash + 1u;
    }

    name_len = path_len - name_start;
    if (name_len == 0) {
        return ASTRA_VFS_INVALID_NAME;
    }
    if (name_len >= (size_t)ASTRA_VFS_NAME_CAP) {
        return ASTRA_VFS_NAME_TOO_LONG;
    }
    if (!valid_component(&path[name_start], name_len)) {
        return ASTRA_VFS_INVALID_NAME;
    }
    if (parent_id < 0 || parent_id >= ASTRA_VFS_MAX_NODES ||
        !fs->nodes[parent_id].used) {
        return ASTRA_VFS_NOT_FOUND;
    }
    if (!fs->nodes[parent_id].is_directory) {
        return ASTRA_VFS_NOT_DIRECTORY;
    }
    if (!can_access(fs, &fs->nodes[parent_id], 3u)) {
        return ASTRA_VFS_PERMISSION_DENIED;
    }
    existing_id = find_child(fs, parent_id, &path[name_start], name_len);
    if (existing_id >= 0) {
        return ASTRA_VFS_ALREADY_EXISTS;
    }

    {
        size_t parent_path_length = 0u;
        size_t suffix_length = name_len;
        size_t new_path_length = 0u;

        if (!node_absolute_path_length(fs, parent_id, &parent_path_length)) {
            return ASTRA_VFS_NOT_FOUND;
        }
        if (parent_id != 0) {
            if (suffix_length == SIZE_MAX) {
                return ASTRA_VFS_PATH_TOO_LONG;
            }
            suffix_length++;
        }
        if (parent_path_length > SIZE_MAX - suffix_length) {
            return ASTRA_VFS_PATH_TOO_LONG;
        }
        new_path_length = parent_path_length + suffix_length;
        if (new_path_length >= (size_t)ASTRA_VFS_PATH_CAP) {
            return ASTRA_VFS_PATH_TOO_LONG;
        }
    }

    return add_node(
        fs,
        parent_id,
        &path[name_start],
        name_len,
        is_directory,
        fs->uid,
        is_directory ? 0755u : 0644u,
        NULL,
        0,
        &created_id
    );
}

AstraVfsResult astra_vfs_write(
    AstraVfs *fs,
    const char *path,
    size_t path_len,
    const char *content,
    size_t content_len
) {
    int id = -1;
    AstraVfsNode *node = NULL;
    AstraVfsResult result = ASTRA_VFS_OK;

    if (fs == NULL || (content_len > 0 && content == NULL)) {
        return ASTRA_VFS_INVALID_ARGUMENT;
    }
    if (content_len > (size_t)ASTRA_VFS_CONTENT_CAP) {
        return ASTRA_VFS_CONTENT_TOO_LARGE;
    }
    result = resolve_path(fs, path, path_len, &id);
    if (result != ASTRA_VFS_OK) {
        return result;
    }
    node = &fs->nodes[id];
    if (node->is_directory) {
        return ASTRA_VFS_IS_DIRECTORY;
    }
    if (!can_access(fs, node, 2u)) {
        return ASTRA_VFS_PERMISSION_DENIED;
    }
    if (content_len > 0) {
        copy_bytes(node->content, content, content_len);
    }
    node->content_len = content_len;
    return ASTRA_VFS_OK;
}

AstraVfsResult astra_vfs_remove(AstraVfs *fs, const char *path, size_t path_len) {
    int id = -1;
    int parent = -1;
    AstraVfsNode *node = NULL;
    AstraVfsNode *parent_node = NULL;
    AstraVfsResult result = ASTRA_VFS_OK;

    result = resolve_path(fs, path, path_len, &id);
    if (result != ASTRA_VFS_OK) {
        return result;
    }
    if (id == 0) {
        return ASTRA_VFS_PERMISSION_DENIED;
    }

    node = &fs->nodes[id];
    parent = node->parent;
    if (parent < 0 || parent >= ASTRA_VFS_MAX_NODES ||
        !fs->nodes[parent].used || !fs->nodes[parent].is_directory) {
        return ASTRA_VFS_NOT_FOUND;
    }
    parent_node = &fs->nodes[parent];
    if (!can_access(fs, parent_node, 3u)) {
        return ASTRA_VFS_PERMISSION_DENIED;
    }
    if ((parent_node->mode & 01000u) != 0 &&
        fs->uid != (uint32_t)ASTRA_VFS_ROOT_UID &&
        fs->uid != parent_node->owner_uid && fs->uid != node->owner_uid) {
        return ASTRA_VFS_PERMISSION_DENIED;
    }
    if (node->is_directory && id == fs->cwd) {
        return ASTRA_VFS_PERMISSION_DENIED;
    }
    if (node->is_directory) {
        for (size_t i = 1; i < (size_t)ASTRA_VFS_MAX_NODES; i++) {
            if (fs->nodes[i].used && fs->nodes[i].parent == id) {
                return ASTRA_VFS_DIRECTORY_NOT_EMPTY;
            }
        }
    }

    node->used = false;
    return ASTRA_VFS_OK;
}

AstraVfsResult astra_vfs_chmod(
    AstraVfs *fs,
    const char *path,
    size_t path_len,
    uint16_t mode
) {
    int id = -1;
    AstraVfsResult result = ASTRA_VFS_OK;

    if (fs == NULL || mode > 07777u) {
        return ASTRA_VFS_INVALID_ARGUMENT;
    }
    result = resolve_path(fs, path, path_len, &id);
    if (result != ASTRA_VFS_OK) {
        return result;
    }
    if (fs->uid != (uint32_t)ASTRA_VFS_ROOT_UID &&
        fs->uid != fs->nodes[id].owner_uid) {
        return ASTRA_VFS_PERMISSION_DENIED;
    }
    fs->nodes[id].mode = mode;
    return ASTRA_VFS_OK;
}

bool astra_vfs_format_mode(
    uint16_t mode,
    bool is_directory,
    char *output,
    size_t output_capacity
) {
    char permissions[11] = {0};

    if (output == NULL || output_capacity < sizeof permissions || mode > 07777u) {
        return false;
    }

    permissions[0] = is_directory ? 'd' : '-';
    permissions[1] = (mode & 0400u) != 0u ? 'r' : '-';
    permissions[2] = (mode & 0200u) != 0u ? 'w' : '-';
    if ((mode & 04000u) != 0u) {
        permissions[3] = (mode & 0100u) != 0u ? 's' : 'S';
    } else {
        permissions[3] = (mode & 0100u) != 0u ? 'x' : '-';
    }

    permissions[4] = (mode & 0040u) != 0u ? 'r' : '-';
    permissions[5] = (mode & 0020u) != 0u ? 'w' : '-';
    if ((mode & 02000u) != 0u) {
        permissions[6] = (mode & 0010u) != 0u ? 's' : 'S';
    } else {
        permissions[6] = (mode & 0010u) != 0u ? 'x' : '-';
    }

    permissions[7] = (mode & 0004u) != 0u ? 'r' : '-';
    permissions[8] = (mode & 0002u) != 0u ? 'w' : '-';
    if ((mode & 01000u) != 0u) {
        permissions[9] = (mode & 0001u) != 0u ? 't' : 'T';
    } else {
        permissions[9] = (mode & 0001u) != 0u ? 'x' : '-';
    }
    permissions[10] = '\0';

    for (size_t i = 0; i < sizeof permissions; i++) {
        output[i] = permissions[i];
    }
    return true;
}

const char *astra_vfs_result_name(AstraVfsResult result) {
    switch (result) {
        case ASTRA_VFS_OK:
            return "success";
        case ASTRA_VFS_INVALID_ARGUMENT:
            return "invalid argument";
        case ASTRA_VFS_PATH_TOO_LONG:
            return "path too long";
        case ASTRA_VFS_NAME_TOO_LONG:
            return "name too long";
        case ASTRA_VFS_INVALID_NAME:
            return "invalid name";
        case ASTRA_VFS_NOT_FOUND:
            return "not found";
        case ASTRA_VFS_NOT_DIRECTORY:
            return "not a directory";
        case ASTRA_VFS_IS_DIRECTORY:
            return "is a directory";
        case ASTRA_VFS_PERMISSION_DENIED:
            return "permission denied";
        case ASTRA_VFS_ALREADY_EXISTS:
            return "already exists";
        case ASTRA_VFS_DIRECTORY_NOT_EMPTY:
            return "directory not empty";
        case ASTRA_VFS_NO_SPACE:
            return "filesystem full";
        case ASTRA_VFS_CONTENT_TOO_LARGE:
            return "content too large";
        case ASTRA_VFS_BUFFER_TOO_SMALL:
            return "buffer too small";
    }
    return "unknown VFS error";
}
