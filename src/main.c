#include <stddef.h>

/* AstraOS: an AArch64 UEFI-native OS prototype.
 * Firmware supplies device I/O; the shell, RAM filesystem and permission
 * checks are AstraOS code. This alpha is not yet a protected-mode kernel.
 */
typedef unsigned char  UINT8;
typedef unsigned short UINT16;
typedef unsigned int   UINT32;
typedef unsigned long long UINT64;
typedef UINT64 EFI_STATUS;
typedef void *EFI_HANDLE;
typedef UINT16 CHAR16;
typedef UINT8 BOOLEAN;
typedef unsigned long long UINTN;
#define EFIAPI
#define EFI_SUCCESS 0
#define EFI_INVALID_PARAMETER 0x8000000000000002ULL
#define EFI_NOT_READY 0x8000000000000006ULL
#define TRUE 1
#define FALSE 0
#define ARRAY_COUNT(x) (sizeof(x) / sizeof((x)[0]))

typedef struct { UINT16 ScanCode; CHAR16 UnicodeChar; } EFI_INPUT_KEY;
typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *, BOOLEAN);
    EFI_STATUS (EFIAPI *ReadKeyStroke)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *, EFI_INPUT_KEY *);
    void *WaitForKey;
};
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, BOOLEAN);
    EFI_STATUS (EFIAPI *OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, CHAR16 *);
    void *TestString;
    void *QueryMode;
    void *SetMode;
    EFI_STATUS (EFIAPI *SetAttribute)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, UINTN);
    EFI_STATUS (EFIAPI *ClearScreen)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *);
    void *SetCursorPosition;
    void *EnableCursor;
    void *Mode;
};
typedef struct { UINT64 Signature; UINT32 Revision; UINT32 HeaderSize; UINT32 CRC32; UINT32 Reserved; } EFI_TABLE_HEADER;
typedef struct EFI_BOOT_SERVICES EFI_BOOT_SERVICES;
typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    UINT32 _pad;
    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *gOut;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL *gIn;

#define MAX_NODES 64
#define NAME_CAP 32
#define CONTENT_CAP 512
#define LINE_CAP 256
#define ROOT_UID 0U
#define ASTRA_UID 1000U

typedef struct {
    BOOLEAN used;
    BOOLEAN directory;
    UINT32 owner;
    UINT16 mode;
    int parent;
    CHAR16 name[NAME_CAP];
    CHAR16 content[CONTENT_CAP];
} VNode;
static VNode gNodes[MAX_NODES];
static int gCwd = 0;
static UINT32 gUid = ROOT_UID;
static CHAR16 gLine[LINE_CAP];

static UINTN s_len(const CHAR16 *s) { UINTN n=0; if(!s)return 0; while(s[n])n++; return n; }
static int s_cmp(const CHAR16 *a,const CHAR16 *b) {
    UINTN i=0; while(a[i] && b[i] && a[i]==b[i]) i++;
    return (int)a[i]-(int)b[i];
}
static void s_copy(CHAR16 *dst, const CHAR16 *src, UINTN cap) {
    UINTN i=0; if(cap==0)return; if(!src){dst[0]=0;return;}
    while(src[i] && i+1<cap){dst[i]=src[i];i++;} dst[i]=0;
}
static EFI_STATUS gOutputError = EFI_SUCCESS;

static void out(const CHAR16 *s) {
    enum { OUTPUT_CHUNK_CAP = 128 };
    CHAR16 chunk[OUTPUT_CHUNK_CAP] = {0};
    UINTN i = 0;
    EFI_STATUS status = EFI_SUCCESS;

    if (s == NULL || gOut == NULL || gOut->OutputString == NULL ||
        gOutputError != EFI_SUCCESS) {
        return;
    }

    while (*s != 0) {
        i = 0;
        while (*s != 0 && i + 1u < OUTPUT_CHUNK_CAP) {
            chunk[i] = *s;
            i++;
            s++;
        }
        chunk[i] = 0;
        status = gOut->OutputString(gOut, chunk);
        if (status != EFI_SUCCESS) {
            gOutputError = status;
            return;
        }
    }
}
static void out_ch(CHAR16 c) { CHAR16 s[2]; s[0]=c;s[1]=0;out(s); }
static void out_u32(UINT32 v) {
    CHAR16 b[16]; UINTN n=0; if(v==0){out_ch('0');return;}
    while(v && n<ARRAY_COUNT(b)){b[n++]=(CHAR16)('0'+(v%10));v/=10;}
    while(n)out_ch(b[--n]);
}
static void newline(void) { out(L"\r\n"); }
static void error(const CHAR16 *s) { out(L"error: ");out(s);newline(); }
static BOOLEAN is_space(CHAR16 c) { return c==' ' || c=='\t'; }
static CHAR16 *skip_spaces(CHAR16 *s) { while(*s && is_space(*s))s++;return s; }
static BOOLEAN is_root(void) { return gUid==ROOT_UID; }
static const CHAR16 *username(UINT32 uid) { return uid==ROOT_UID?L"root":(uid==ASTRA_UID?L"astra":L"unknown"); }


static BOOLEAN can_access(const VNode *n, UINT16 bit) {
    UINT16 bits = 0;

    if (n == NULL || !n->used) {
        return FALSE;
    }
    if (gUid == ROOT_UID) {
        return TRUE;
    }

    bits = (gUid == n->owner) ? (UINT16)((n->mode >> 6) & 7u)
                              : (UINT16)(n->mode & 7u);
    return (bits & bit) == bit;
}
static int node_add(int parent,const CHAR16 *name,BOOLEAN directory,UINT32 owner,UINT16 mode,const CHAR16 *content) {
    UINTN i;
    for(i=1;i<MAX_NODES;i++) if(!gNodes[i].used) {
        VNode *n=&gNodes[i]; n->used=TRUE;n->directory=directory;n->owner=owner;n->mode=mode;n->parent=parent;
        s_copy(n->name,name,NAME_CAP);s_copy(n->content,content,CONTENT_CAP);return (int)i;
    }
    return -1;
}
static int child_find(int parent,const CHAR16 *name) {
    UINTN i; for(i=1;i<MAX_NODES;i++) if(gNodes[i].used && gNodes[i].parent==parent && s_cmp(gNodes[i].name,name)==0)return (int)i;
    return -1;
}

static int resolve_path(const CHAR16 *path) {
    CHAR16 part[NAME_CAP] = {0};
    UINTN j = 0;
    int cur = 0;
    const CHAR16 *p = path;

    if (path == NULL || *path == 0) {
        return gCwd;
    }

    cur = (*p == '/') ? 0 : gCwd;
    while (*p != 0) {
        while (*p == '/') {
            p++;
        }
        if (*p == 0) {
            break;
        }
        if (cur < 0 || cur >= MAX_NODES || !gNodes[cur].used ||
            !gNodes[cur].directory || !can_access(&gNodes[cur], 1u)) {
            return -1;
        }

        j = 0;
        while (p[j] != 0 && p[j] != '/') {
            if (j >= NAME_CAP - 1u) {
                return -1;
            }
            part[j] = p[j];
            j++;
        }
        part[j] = 0;
        p += j;

        if (s_cmp(part, L".") == 0) {
            continue;
        }
        if (s_cmp(part, L"..") == 0) {
            if (cur != 0) {
                cur = gNodes[cur].parent;
            }
            continue;
        }

        cur = child_find(cur, part);
        if (cur < 0) {
            return -1;
        }
    }

    if (s_len(path) > 0 && path[s_len(path) - 1u] == '/' &&
        !gNodes[cur].directory) {
        return -1;
    }
    return cur;
}

static BOOLEAN valid_parent_for_write(int node) {
    int parent = -1;

    if (node <= 0 || node >= MAX_NODES || !gNodes[node].used) {
        return FALSE;
    }
    parent = gNodes[node].parent;
    if (parent < 0 || parent >= MAX_NODES || !gNodes[parent].directory) {
        return FALSE;
    }
    return can_access(&gNodes[parent], 3u);
}
static void fs_init(void) {
    UINTN i; for(i=0;i<MAX_NODES;i++)gNodes[i].used=FALSE;
    gNodes[0].used=TRUE;gNodes[0].directory=TRUE;gNodes[0].owner=ROOT_UID;gNodes[0].mode=0755;gNodes[0].parent=0;s_copy(gNodes[0].name,L"/",NAME_CAP);
    node_add(0,L"etc",TRUE,ROOT_UID,0755,L"");
    node_add(0,L"home",TRUE,ROOT_UID,0755,L"");
    node_add(0,L"tmp",TRUE,ROOT_UID,01777,L"");
    node_add(0,L"README.txt",FALSE,ROOT_UID,0644,L"Welcome to AstraOS. This is an early UEFI-native live prototype.\r\nThe RAM filesystem is temporary and resets after reboot.\r\nType 'help' to see commands.\r\n");
    node_add(1,L"motd",FALSE,ROOT_UID,0644,L"AstraOS alpha - welcome.\r\n");
    node_add(2,L"astra",TRUE,ASTRA_UID,0750,L"");
    node_add(6,L"notes.txt",FALSE,ASTRA_UID,0640,L"This file belongs to astra.\r\n");
    gCwd=0;gUid=ROOT_UID;
}
static void print_mode(const VNode *n) {
    out(n->directory?L"d":L"-");
    out((n->mode&0400)?L"r":L"-");out((n->mode&0200)?L"w":L"-");out((n->mode&0100)?L"x":L"-");
    out((n->mode&0040)?L"r":L"-");out((n->mode&0020)?L"w":L"-");out((n->mode&0010)?L"x":L"-");
    out((n->mode&0004)?L"r":L"-");out((n->mode&0002)?L"w":L"-");out((n->mode&0001)?L"x":L"-");
}
static void cmd_ls(void) {
    UINTN i; VNode *n=&gNodes[gCwd];
    if(!can_access(n,5)){error(L"permission denied");return;}
    out(L"MODE        OWNER   NAME\r\n");
    for(i=1;i<MAX_NODES;i++) if(gNodes[i].used && gNodes[i].parent==gCwd) {
        print_mode(&gNodes[i]);out(L"  ");out(username(gNodes[i].owner));out(L"  ");out(gNodes[i].name);if(gNodes[i].directory)out(L"/");newline();
    }
}
static void print_cwd_path(void) {
    int chain[MAX_NODES],n=0,i;
    if(gCwd==0){out_ch('/');return;}
    i=gCwd;while(i>0 && n<MAX_NODES){chain[n++]=i;i=gNodes[i].parent;}
    for(i=n-1;i>=0;i--){out_ch('/');out(gNodes[chain[i]].name);}
}
static void print_pwd(void) { print_cwd_path();newline(); }
static void cmd_cat(CHAR16 *path) {
    int id=resolve_path(path); if(id<0){error(L"path not found or inaccessible");return;}
    if(gNodes[id].directory){error(L"is a directory");return;}
    if(!can_access(&gNodes[id],4)){error(L"permission denied");return;}
    out(gNodes[id].content);if(s_len(gNodes[id].content) && gNodes[id].content[s_len(gNodes[id].content)-1]!='\n')newline();
}

static void cmd_create(const CHAR16 *path, BOOLEAN directory) {
    int parent = -1;
    int existing = -1;
    CHAR16 *slash = NULL;
    CHAR16 *base_src = NULL;
    UINTN i = 0;
    UINTN n = s_len(path);
    CHAR16 base[NAME_CAP] = {0};
    CHAR16 parent_path[LINE_CAP] = {0};

    if (path == NULL || *path == 0) {
        error(L"path required");
        return;
    }
    if (n >= LINE_CAP) {
        error(L"path too long");
        return;
    }

    s_copy(parent_path, path, LINE_CAP);
    while (n > 1u && parent_path[n - 1u] == '/') {
        parent_path[--n] = 0;
    }
    for (i = 0; i < n; i++) {
        if (parent_path[i] == '/') {
            slash = &parent_path[i];
        }
    }

    if (slash == NULL) {
        base_src = parent_path;
        parent = gCwd;
    } else if (slash == parent_path) {
        base_src = slash + 1;
        parent = 0;
    } else {
        base_src = slash + 1;
        *slash = 0;
        parent = resolve_path(parent_path);
    }

    if (s_len(base_src) >= NAME_CAP) {
        error(L"name too long");
        return;
    }
    s_copy(base, base_src, NAME_CAP);
    if (base[0] == 0 || s_cmp(base, L".") == 0 || s_cmp(base, L"..") == 0) {
        error(L"invalid name");
        return;
    }
    if (parent < 0 || parent >= MAX_NODES || !gNodes[parent].directory) {
        error(L"parent directory not found");
        return;
    }
    if (!can_access(&gNodes[parent], 3u)) {
        error(L"permission denied");
        return;
    }

    existing = child_find(parent, base);
    if (existing >= 0) {
        if (!directory && !gNodes[existing].directory) {
            out(L"already exists\r\n");
            return;
        }
        error(L"name already exists");
        return;
    }
    if (node_add(parent, base, directory, gUid, directory ? 0755u : 0644u, L"") < 0) {
        error(L"RAM filesystem full");
        return;
    }
    out(directory ? L"directory created\r\n" : L"file created\r\n");
}

static void cmd_write(CHAR16 *args) {
    CHAR16 *p = skip_spaces(args);
    CHAR16 *text = NULL;
    int id = -1;

    if (*p == 0) {
        error(L"usage: write PATH TEXT");
        return;
    }
    text = p;
    while (*text != 0 && !is_space(*text)) {
        text++;
    }
    if (*text == 0) {
        error(L"usage: write PATH TEXT");
        return;
    }
    *text++ = 0;
    text = skip_spaces(text);

    id = resolve_path(p);
    if (id < 0) {
        error(L"file not found");
        return;
    }
    if (gNodes[id].directory) {
        error(L"is a directory");
        return;
    }
    if (!can_access(&gNodes[id], 2u)) {
        error(L"permission denied");
        return;
    }
    if (s_len(text) >= CONTENT_CAP) {
        error(L"content too long; file was not changed");
        return;
    }

    s_copy(gNodes[id].content, text, CONTENT_CAP);
    out(L"written\r\n");
}

static void cmd_rm(const CHAR16 *path) {
    int id = -1;
    int parent = -1;
    UINTN i = 0;

    if (path == NULL || *path == 0) {
        error(L"usage: rm PATH");
        return;
    }
    id = resolve_path(path);
    if (id <= 0) {
        error(L"path not found");
        return;
    }
    if (!valid_parent_for_write(id)) {
        error(L"permission denied");
        return;
    }

    parent = gNodes[id].parent;
    if ((gNodes[parent].mode & 01000u) != 0 && !is_root() &&
        gUid != gNodes[parent].owner && gUid != gNodes[id].owner) {
        error(L"sticky directory: only its owner, file owner, or root may remove this entry");
        return;
    }

    if (gNodes[id].directory) {
        for (i = 1; i < MAX_NODES; i++) {
            if (gNodes[i].used && gNodes[i].parent == id) {
                error(L"directory not empty");
                return;
            }
        }
    }
    gNodes[id].used = FALSE;
    out(L"removed\r\n");
}

static int parse_octal(const CHAR16 *s) {
    int value = 0;
    UINTN i = 0;

    if (s == NULL || *s == 0) {
        return -1;
    }
    while (s[i] != 0) {
        int digit = (int)(s[i] - '0');
        if (digit < 0 || digit > 7) {
            return -1;
        }
        if (value > (07777 - digit) / 8) {
            return -1;
        }
        value = (value * 8) + digit;
        i++;
    }
    return value;
}
static void cmd_chmod(CHAR16 *args) {
    CHAR16 *mode=skip_spaces(args),*path;int m,id;
    while(*mode&&!is_space(*mode))mode++;if(!*mode){error(L"usage: chmod OCTAL PATH");return;}*mode++=0;path=skip_spaces(mode);
    m=parse_octal(args);if(m<0 || !*path){error(L"usage: chmod OCTAL PATH");return;}
    id=resolve_path(path);if(id<0){error(L"path not found");return;}
    if(!is_root() && gUid!=gNodes[id].owner){error(L"only root or the owner may chmod");return;}
    gNodes[id].mode=(UINT16)m;out(L"permissions updated\r\n");
}
static void dispatch(CHAR16 *line) {
    CHAR16 *cmd=skip_spaces(line),*args,*p;int id;UINTN i;BOOLEAN found;
    if(!*cmd)return;
    p=cmd;while(*p&&!is_space(*p))p++;if(*p)*p++=0;args=skip_spaces(p);
    if(s_cmp(cmd,L"help")==0){out(L"AstraOS alpha commands:\r\n  help, about, uname, clear, exit\r\n  pwd, ls, cd PATH, cat PATH\r\n  touch PATH, mkdir PATH, write PATH TEXT, rm PATH\r\n  chmod OCTAL PATH, whoami, id, su astra\r\n  Note: RAM filesystem resets on reboot.\r\n");}
    else if(s_cmp(cmd,L"about")==0){out(L"AstraOS alpha 0.1 (AArch64 UEFI)\r\nAstraOS-owned shell, RAM VFS and Unix-style permission checks.\r\nUEFI currently provides firmware console I/O; process isolation is not implemented.\r\n");}
    else if(s_cmp(cmd,L"uname")==0){out(L"AstraOS astra-aarch64 0.1 UEFI-native\r\n");}
    else if(s_cmp(cmd,L"clear")==0){if(gOut->ClearScreen)gOut->ClearScreen(gOut);}
    else if(s_cmp(cmd,L"exit")==0){out(L"Leaving AstraOS shell.\r\n");gLine[0]=0;s_copy(gLine,L"__exit__",LINE_CAP);}
    else if(s_cmp(cmd,L"pwd")==0)print_pwd();
    else if(s_cmp(cmd,L"ls")==0){if(*args){id=resolve_path(args);if(id<0){error(L"path not found");return;}if(!gNodes[id].directory){out(gNodes[id].name);newline();return;}if(!can_access(&gNodes[id],5)){error(L"permission denied");return;}for(i=1;i<MAX_NODES;i++)if(gNodes[i].used&&gNodes[i].parent==id){out(gNodes[i].name);if(gNodes[i].directory)out(L"/");newline();}}else cmd_ls();}
    else if(s_cmp(cmd,L"cd")==0){id=resolve_path(*args?args:L"/");if(id<0||!gNodes[id].directory){error(L"directory not found or inaccessible");return;}if(!can_access(&gNodes[id],1)){error(L"permission denied");return;}gCwd=id;}
    else if(s_cmp(cmd,L"cat")==0)cmd_cat(args);
    else if(s_cmp(cmd,L"touch")==0)cmd_create(args,FALSE);
    else if(s_cmp(cmd,L"mkdir")==0)cmd_create(args,TRUE);
    else if(s_cmp(cmd,L"write")==0)cmd_write(args);
    else if(s_cmp(cmd,L"rm")==0)cmd_rm(args);
    else if(s_cmp(cmd,L"chmod")==0)cmd_chmod(args);
    else if(s_cmp(cmd,L"whoami")==0){out(username(gUid));newline();}
    else if(s_cmp(cmd,L"id")==0){out(L"uid=");out_u32(gUid);out(L"(");out(username(gUid));out(L") gid=");out_u32(gUid);out(L"(");out(username(gUid));out(L")\r\n");}
    else if(s_cmp(cmd,L"su")==0){if(s_cmp(args,L"astra")==0){gUid=ASTRA_UID;out(L"switched to unprivileged user astra\r\n");}else if(s_cmp(args,L"root")==0){error(L"authentication is not implemented; restart to restore root demo session");}else error(L"usage: su astra");}
    else if(s_cmp(cmd,L"echo")==0){out(args);newline();}
    else if(s_cmp(cmd,L"__exit__")==0){}
    else {found=FALSE;error(L"unknown command (type help)");(void)found;}
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *systemTable);

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *systemTable) {
    EFI_INPUT_KEY key = {0};
    EFI_STATUS status = EFI_SUCCESS;
    UINTN len = 0;
    (void)image;

    if (systemTable == NULL || systemTable->ConOut == NULL ||
        systemTable->ConIn == NULL ||
        systemTable->ConOut->OutputString == NULL ||
        systemTable->ConIn->ReadKeyStroke == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    gOut = systemTable->ConOut;
    gIn = systemTable->ConIn;
    gOutputError = EFI_SUCCESS;
    fs_init();

    if (gOut->ClearScreen != NULL) {
        status = gOut->ClearScreen(gOut);
        if (status != EFI_SUCCESS) {
            return status;
        }
    }
    out(L"\r\n==============================\r\n          ASTRAOS\r\n==============================\r\n\r\n");
    out(L"AstraOS alpha 0.1 | AArch64 UEFI live prototype\r\nType 'help' for commands. Changes are kept in RAM only.\r\n\r\n");
    if (gOutputError != EFI_SUCCESS) {
        return gOutputError;
    }

    for (;;) {
        out(username(gUid));
        out(L"@astra:");
        print_cwd_path();
        out(L"# ");
        if (gOutputError != EFI_SUCCESS) {
            return gOutputError;
        }

        for (;;) {
            status = gIn->ReadKeyStroke(gIn, &key);
            if (status == EFI_NOT_READY) {
                continue;
            }
            if (status != EFI_SUCCESS) {
                return status;
            }
            if (key.UnicodeChar == 0x000D) {
                newline();
                gLine[len] = 0;
                dispatch(gLine);
                if (s_cmp(gLine, L"__exit__") == 0) {
                    return EFI_SUCCESS;
                }
                if (gOutputError != EFI_SUCCESS) {
                    return gOutputError;
                }
                len = 0;
                gLine[0] = 0;
                break;
            }
            if (key.UnicodeChar == 0x0008) {
                if (len > 0) {
                    len--;
                    gLine[len] = 0;
                    out(L"\b \b");
                }
                if (gOutputError != EFI_SUCCESS) {
                    return gOutputError;
                }
                continue;
            }
            if (key.UnicodeChar >= 32 && key.UnicodeChar < 127 &&
                len + 1u < (UINTN)LINE_CAP) {
                gLine[len] = key.UnicodeChar;
                len++;
                gLine[len] = 0;
                out_ch(key.UnicodeChar);
                if (gOutputError != EFI_SUCCESS) {
                    return gOutputError;
                }
            }
        }
    }
}
