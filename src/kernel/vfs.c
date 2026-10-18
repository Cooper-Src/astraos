#include "astra/vfs.h"

enum {
    PERM_EXECUTE = 1,
    PERM_WRITE = 2,
    PERM_READ = 4,
    MODE_DEFAULT_DIRECTORY = 0755,
    MODE_DEFAULT_FILE = 0644,
    MODE_TEMP_DIRECTORY = 01777,
    MODE_PERMISSION_MASK = 07777
};

typedef struct {
    bool used;
    bool directory;
    uint32_t owner;
    uint16_t mode;
    size_t parent;
    char name[VFS_NAME_MAX + 1];
    char contents[VFS_CONTENT_MAX + 1];
    size_t size;
} VfsNode;

static VfsNode g_nodes[VFS_MAX_NODES] = {0};
static size_t g_working_directory = 0;
static uint32_t g_uid = VFS_UID_ROOT;

/* Copies a bounded NUL-terminated string; on truncation, clears the destination. */
static bool text_copy(char *destination, size_t capacity, const char *source) {
    size_t length = 0;

    if (destination == NULL || source == NULL || capacity == 0) {
        return false;
    }
    while (length < capacity && source[length] != '\0') {
        length++;
    }
    if (length == capacity) {
        destination[0] = '\0';
        return false;
    }
    for (size_t i = 0; i <= length; i++) {
        destination[i] = source[i];
    }
    return true;
}

/* Reads a NUL-terminated string length without inspecting more than maximum bytes. */
static bool text_length(const char *text, size_t maximum, size_t *length) {
    size_t i = 0;

    if (text == NULL || length == NULL) {
        return false;
    }
    while (i < maximum && text[i] != '\0') {
        i++;
    }
    if (i == maximum) {
        return false;
    }
    *length = i;
    return true;
}

static bool text_equal(const char *left, const char *right) {
    size_t i = 0;

    if (left == NULL || right == NULL) {
        return false;
    }
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) {
            return false;
        }
        i++;
    }
    return left[i] == right[i];
}

static void node_initialize(size_t index, size_t parent, const char *name, bool directory,
                            uint32_t owner, uint16_t mode, const char *contents) {
    VfsNode *node = &g_nodes[index];
    size_t length = 0;

    node->used = true;
    node->directory = directory;
    node->owner = owner;
    node->mode = mode;
    node->parent = parent;
    (void)text_copy(node->name, sizeof node->name, name);
    node->contents[0] = '\0';
    node->size = 0;

    if (contents != NULL && text_length(contents, sizeof node->contents, &length)) {
        (void)text_copy(node->contents, sizeof node->contents, contents);
        node->size = length;
    }
}

static bool node_can_access(const VfsNode *node, unsigned int requested) {
    unsigned int available = 0;

    if (node == NULL || !node->used) {
        return false;
    }
    if (g_uid == VFS_UID_ROOT) {
        if ((requested & PERM_EXECUTE) != 0u && (node->mode & 0111u) == 0u) {
            return false;
        }
        return true;
    }

    if (g_uid == node->owner) {
        available = (unsigned int)((node->mode >> 6u) & 7u);
    } else {
        available = (unsigned int)(node->mode & 7u);
    }
    return (available & requested) == requested;
}

static int child_find(size_t parent, const char *name) {
    for (size_t i = 1; i < VFS_MAX_NODES; i++) {
        if (g_nodes[i].used && g_nodes[i].parent == parent &&
            text_equal(g_nodes[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

static VfsStatus path_resolve(const char *path, size_t *out_index) {
    char component[VFS_NAME_MAX + 1] = {0};
    size_t path_length = 0;
    size_t position = 0;
    size_t current = 0;

    if (path == NULL || out_index == NULL) {
        return VFS_ERR_INVALID_ARGUMENT;
    }
    if (!text_length(path, VFS_PATH_MAX + 1u, &path_length)) {
        return VFS_ERR_PATH_TOO_LONG;
    }
    if (path_length == 0) {
        return VFS_ERR_INVALID_ARGUMENT;
    }
    current = path[0] == '/' ? 0 : g_working_directory;

    while (position < path_length) {
        size_t component_length = 0;
        int child = -1;

        while (position < path_length && path[position] == '/') {
            position++;
        }
        if (position >= path_length) {
            break;
        }
        while (position < path_length && path[position] != '/') {
            if (component_length >= VFS_NAME_MAX) {
                return VFS_ERR_NAME_TOO_LONG;
            }
            component[component_length++] = path[position++];
        }
        component[component_length] = '\0';

        if (text_equal(component, ".")) {
            continue;
        }
        if (!node_can_access(&g_nodes[current], PERM_EXECUTE)) {
            return VFS_ERR_PERMISSION_DENIED;
        }
        if (text_equal(component, "..")) {
            current = g_nodes[current].parent;
            continue;
        }
        if (!g_nodes[current].directory) {
            return VFS_ERR_NOT_DIRECTORY;
        }
        child = child_find(current, component);
        if (child < 0) {
            return VFS_ERR_NOT_FOUND;
        }
        current = (size_t)child;
    }

    if (!g_nodes[current].used) {
        return VFS_ERR_NOT_FOUND;
    }
    *out_index = current;
    return VFS_OK;
}

static VfsStatus split_parent(const char *path, char *parent_path, size_t parent_capacity,
                              char *name, size_t name_capacity) {
    size_t length = 0;
    size_t name_start = 0;
    size_t slash = SIZE_MAX;
    size_t name_length = 0;

    if (path == NULL || parent_path == NULL || name == NULL) {
        return VFS_ERR_INVALID_ARGUMENT;
    }
    if (!text_length(path, VFS_PATH_MAX + 1u, &length)) {
        return VFS_ERR_PATH_TOO_LONG;
    }
    while (length > 1u && path[length - 1u] == '/') {
        length--;
    }
    if (length == 0 || (length == 1u && path[0] == '/')) {
        return VFS_ERR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < length; i++) {
        if (path[i] == '/') {
            slash = i;
        }
    }
    name_start = slash == SIZE_MAX ? 0 : slash + 1u;
    if (name_start >= length) {
        return VFS_ERR_INVALID_ARGUMENT;
    }
    name_length = length - name_start;
    if (name_length > VFS_NAME_MAX || name_length >= name_capacity) {
        return VFS_ERR_NAME_TOO_LONG;
    }
    for (size_t i = 0; i < name_length; i++) {
        name[i] = path[name_start + i];
    }
    name[name_length] = '\0';
    if (text_equal(name, ".") || text_equal(name, "..")) {
        return VFS_ERR_INVALID_ARGUMENT;
    }

    if (slash == SIZE_MAX) {
        if (!text_copy(parent_path, parent_capacity, ".")) {
            return VFS_ERR_PATH_TOO_LONG;
        }
    } else if (slash == 0u) {
        if (!text_copy(parent_path, parent_capacity, "/")) {
            return VFS_ERR_PATH_TOO_LONG;
        }
    } else {
        if (slash >= parent_capacity) {
            return VFS_ERR_PATH_TOO_LONG;
        }
        for (size_t i = 0; i < slash; i++) {
            parent_path[i] = path[i];
        }
        parent_path[slash] = '\0';
    }
    return VFS_OK;
}

static bool is_working_directory_or_ancestor(size_t node_index) {
    size_t current = g_working_directory;

    for (size_t i = 0; i < VFS_MAX_NODES; i++) {
        if (current == node_index) {
            return true;
        }
        if (current == 0) {
            return false;
        }
        if (current >= VFS_MAX_NODES || !g_nodes[current].used) {
            return true;
        }
        current = g_nodes[current].parent;
    }
    return true;
}

void vfs_init(void) {
    for (size_t i = 0; i < VFS_MAX_NODES; i++) {
        g_nodes[i] = (VfsNode){0};
    }
    node_initialize(0, 0, "/", true, VFS_UID_ROOT, MODE_DEFAULT_DIRECTORY, NULL);
    node_initialize(1, 0, "etc", true, VFS_UID_ROOT, MODE_DEFAULT_DIRECTORY, NULL);
    node_initialize(2, 0, "home", true, VFS_UID_ROOT, MODE_DEFAULT_DIRECTORY, NULL);
    node_initialize(3, 0, "tmp", true, VFS_UID_ROOT, MODE_TEMP_DIRECTORY, NULL);
    node_initialize(4, 0, "README.txt", false, VFS_UID_ROOT, MODE_DEFAULT_FILE,
                    "Welcome to AstraOS, an experimental AArch64 kernel.\n"
                    "This is a RAM-only live filesystem; changes reset on reboot.\n"
                    "Type 'help' for available commands.\n");
    node_initialize(5, 1, "motd", false, VFS_UID_ROOT, MODE_DEFAULT_FILE,
                    "AstraOS kernel prototype. Keep backups of important data.\n");
    node_initialize(6, 2, "astra", true, VFS_UID_ASTRA, 0750, NULL);
    node_initialize(7, 6, "notes.txt", false, VFS_UID_ASTRA, 0640,
                    "This sample file belongs to the astra demo account.\n");
    g_working_directory = 0;
    g_uid = VFS_UID_ROOT;
}

uint32_t vfs_get_uid(void) {
    return g_uid;
}

VfsStatus vfs_switch_user(uint32_t uid) {
    if (uid != VFS_UID_ROOT && uid != VFS_UID_ASTRA) {
        return VFS_ERR_INVALID_ARGUMENT;
    }
    g_uid = uid;
    return VFS_OK;
}

const char *vfs_username(uint32_t uid) {
    if (uid == VFS_UID_ROOT) {
        return "root";
    }
    if (uid == VFS_UID_ASTRA) {
        return "astra";
    }
    return "unknown";
}

VfsStatus vfs_list(const char *path, VfsEntry *entries, size_t capacity, size_t *out_count) {
    size_t directory_index = 0;
    size_t count = 0;
    VfsStatus status = VFS_OK;

    if (out_count == NULL || (capacity > 0 && entries == NULL)) {
        return VFS_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0;
    status = path_resolve(path, &directory_index);
    if (status != VFS_OK) {
        return status;
    }
    if (!g_nodes[directory_index].directory) {
        return VFS_ERR_NOT_DIRECTORY;
    }
    if (!node_can_access(&g_nodes[directory_index], PERM_READ | PERM_EXECUTE)) {
        return VFS_ERR_PERMISSION_DENIED;
    }
    for (size_t i = 1; i < VFS_MAX_NODES; i++) {
        if (g_nodes[i].used && g_nodes[i].parent == directory_index) {
            count++;
        }
    }
    *out_count = count;
    if (count > capacity) {
        return VFS_ERR_BUFFER_TOO_SMALL;
    }
    count = 0;
    for (size_t i = 1; i < VFS_MAX_NODES; i++) {
        if (g_nodes[i].used && g_nodes[i].parent == directory_index) {
            VfsEntry *entry = &entries[count++];
            (void)text_copy(entry->name, sizeof entry->name, g_nodes[i].name);
            entry->directory = g_nodes[i].directory;
            entry->owner = g_nodes[i].owner;
            entry->mode = g_nodes[i].mode;
            entry->size = g_nodes[i].size;
        }
    }
    return VFS_OK;
}

VfsStatus vfs_read(const char *path, char *buffer, size_t capacity, size_t *out_length) {
    size_t index = 0;
    VfsStatus status = VFS_OK;

    if (buffer == NULL || capacity == 0 || out_length == NULL) {
        return VFS_ERR_INVALID_ARGUMENT;
    }
    *out_length = 0;
    buffer[0] = '\0';
    status = path_resolve(path, &index);
    if (status != VFS_OK) {
        return status;
    }
    if (g_nodes[index].directory) {
        return VFS_ERR_IS_DIRECTORY;
    }
    if (!node_can_access(&g_nodes[index], PERM_READ)) {
        return VFS_ERR_PERMISSION_DENIED;
    }
    *out_length = g_nodes[index].size;
    if (g_nodes[index].size >= capacity) {
        return VFS_ERR_BUFFER_TOO_SMALL;
    }
    for (size_t i = 0; i <= g_nodes[index].size; i++) {
        buffer[i] = g_nodes[index].contents[i];
    }
    return VFS_OK;
}

VfsStatus vfs_create(const char *path, bool directory) {
    char parent_path[VFS_PATH_MAX + 1] = {0};
    char name[VFS_NAME_MAX + 1] = {0};
    size_t parent_index = 0;
    VfsStatus status = split_parent(path, parent_path, sizeof parent_path, name, sizeof name);

    if (status != VFS_OK) {
        return status;
    }
    status = path_resolve(parent_path, &parent_index);
    if (status != VFS_OK) {
        return status;
    }
    if (!g_nodes[parent_index].directory) {
        return VFS_ERR_NOT_DIRECTORY;
    }
    if (!node_can_access(&g_nodes[parent_index], PERM_WRITE | PERM_EXECUTE)) {
        return VFS_ERR_PERMISSION_DENIED;
    }
    if (child_find(parent_index, name) >= 0) {
        return VFS_ERR_EXISTS;
    }
    for (size_t i = 1; i < VFS_MAX_NODES; i++) {
        if (!g_nodes[i].used) {
            node_initialize(i, parent_index, name, directory, g_uid,
                            directory ? MODE_DEFAULT_DIRECTORY : MODE_DEFAULT_FILE, NULL);
            return VFS_OK;
        }
    }
    return VFS_ERR_NO_SPACE;
}

VfsStatus vfs_write(const char *path, const char *contents) {
    size_t index = 0;
    size_t length = 0;
    VfsStatus status = VFS_OK;

    if (contents == NULL) {
        return VFS_ERR_INVALID_ARGUMENT;
    }
    if (!text_length(contents, VFS_CONTENT_MAX + 1u, &length)) {
        return VFS_ERR_BUFFER_TOO_SMALL;
    }
    status = path_resolve(path, &index);
    if (status != VFS_OK) {
        return status;
    }
    if (g_nodes[index].directory) {
        return VFS_ERR_IS_DIRECTORY;
    }
    if (!node_can_access(&g_nodes[index], PERM_WRITE)) {
        return VFS_ERR_PERMISSION_DENIED;
    }
    if (!text_copy(g_nodes[index].contents, sizeof g_nodes[index].contents, contents)) {
        return VFS_ERR_BUFFER_TOO_SMALL;
    }
    g_nodes[index].size = length;
    return VFS_OK;
}

VfsStatus vfs_remove(const char *path) {
    size_t index = 0;
    size_t parent_index = 0;
    VfsStatus status = path_resolve(path, &index);

    if (status != VFS_OK) {
        return status;
    }
    if (index == 0) {
        return VFS_ERR_BUSY;
    }
    if (is_working_directory_or_ancestor(index)) {
        return VFS_ERR_BUSY;
    }
    parent_index = g_nodes[index].parent;
    if (parent_index >= VFS_MAX_NODES || !g_nodes[parent_index].used) {
        return VFS_ERR_NOT_FOUND;
    }
    if (!node_can_access(&g_nodes[parent_index], PERM_WRITE | PERM_EXECUTE)) {
        return VFS_ERR_PERMISSION_DENIED;
    }
    if ((g_nodes[parent_index].mode & 01000u) != 0u && g_uid != VFS_UID_ROOT &&
        g_uid != g_nodes[parent_index].owner && g_uid != g_nodes[index].owner) {
        return VFS_ERR_PERMISSION_DENIED;
    }
    if (g_nodes[index].directory) {
        for (size_t i = 1; i < VFS_MAX_NODES; i++) {
            if (g_nodes[i].used && g_nodes[i].parent == index) {
                return VFS_ERR_NOT_EMPTY;
            }
        }
    }
    g_nodes[index] = (VfsNode){0};
    return VFS_OK;
}

VfsStatus vfs_chmod(const char *path, uint16_t mode) {
    size_t index = 0;
    VfsStatus status = VFS_OK;

    if ((mode & (uint16_t)~MODE_PERMISSION_MASK) != 0u) {
        return VFS_ERR_INVALID_MODE;
    }
    status = path_resolve(path, &index);
    if (status != VFS_OK) {
        return status;
    }
    if (g_uid != VFS_UID_ROOT && g_uid != g_nodes[index].owner) {
        return VFS_ERR_PERMISSION_DENIED;
    }
    g_nodes[index].mode = mode;
    return VFS_OK;
}

VfsStatus vfs_chdir(const char *path) {
    size_t index = 0;
    VfsStatus status = path_resolve(path, &index);

    if (status != VFS_OK) {
        return status;
    }
    if (!g_nodes[index].directory) {
        return VFS_ERR_NOT_DIRECTORY;
    }
    if (!node_can_access(&g_nodes[index], PERM_EXECUTE)) {
        return VFS_ERR_PERMISSION_DENIED;
    }
    g_working_directory = index;
    return VFS_OK;
}

VfsStatus vfs_getcwd(char *buffer, size_t capacity) {
    size_t chain[VFS_MAX_NODES] = {0};
    size_t count = 0;
    size_t needed = 1;
    size_t position = 0;
    size_t current = g_working_directory;

    if (buffer == NULL || capacity == 0) {
        return VFS_ERR_INVALID_ARGUMENT;
    }
    buffer[0] = '\0';
    while (current != 0) {
        size_t name_length = 0;

        if (current >= VFS_MAX_NODES || !g_nodes[current].used || count >= VFS_MAX_NODES) {
            return VFS_ERR_NOT_FOUND;
        }
        chain[count++] = current;
        if (!text_length(g_nodes[current].name, sizeof g_nodes[current].name, &name_length)) {
            return VFS_ERR_NOT_FOUND;
        }
        if (needed > SIZE_MAX - name_length) {
            return VFS_ERR_PATH_TOO_LONG;
        }
        needed += name_length;
        current = g_nodes[current].parent;
    }
    if (count > 1u) {
        if (needed > SIZE_MAX - (count - 1u)) {
            return VFS_ERR_PATH_TOO_LONG;
        }
        needed += count - 1u;
    }
    if (needed >= capacity) {
        return VFS_ERR_BUFFER_TOO_SMALL;
    }
    buffer[position++] = '/';
    for (size_t i = count; i-- > 0;) {
        size_t name_length = 0;

        (void)text_length(g_nodes[chain[i]].name, sizeof g_nodes[chain[i]].name, &name_length);
        if (position > 1u) {
            buffer[position++] = '/';
        }
        for (size_t j = 0; j < name_length; j++) {
            buffer[position++] = g_nodes[chain[i]].name[j];
        }
    }
    buffer[position] = '\0';
    return VFS_OK;
}

const char *vfs_status_string(VfsStatus status) {
    static const char *const messages[] = {
        "success",
        "invalid argument",
        "path too long",
        "name too long",
        "path not found",
        "already exists",
        "not a directory",
        "is a directory",
        "permission denied",
        "RAM filesystem full",
        "directory not empty",
        "output buffer too small",
        "invalid permission mode",
        "operation would invalidate the working directory"
    };

    if ((unsigned int)status >= (unsigned int)(sizeof messages / sizeof messages[0])) {
        return "unknown filesystem error";
    }
    return messages[(size_t)status];
}
