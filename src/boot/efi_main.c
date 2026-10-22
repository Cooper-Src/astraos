#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../kernel/boot_info.h"
#include "../../kernel/fdt.h"

typedef uint8_t EFI_UINT8;
typedef uint16_t EFI_UINT16;
typedef uint32_t EFI_UINT32;
typedef uint64_t EFI_UINT64;
typedef uint64_t EFI_UINTN;
typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef EFI_UINT16 CHAR16;

#define EFIAPI
#define EFI_SUCCESS UINT64_C(0)
#define EFI_ERROR_BIT UINT64_C(0x8000000000000000)
#define EFI_LOAD_ERROR (EFI_ERROR_BIT | UINT64_C(1))
#define EFI_INVALID_PARAMETER (EFI_ERROR_BIT | UINT64_C(2))
#define EFI_UNSUPPORTED (EFI_ERROR_BIT | UINT64_C(3))
#define EFI_BAD_BUFFER_SIZE (EFI_ERROR_BIT | UINT64_C(4))
#define EFI_BUFFER_TOO_SMALL (EFI_ERROR_BIT | UINT64_C(5))
#define EFI_NOT_READY (EFI_ERROR_BIT | UINT64_C(6))
#define EFI_DEVICE_ERROR (EFI_ERROR_BIT | UINT64_C(7))
#define EFI_OUT_OF_RESOURCES (EFI_ERROR_BIT | UINT64_C(9))
#define EFI_NOT_FOUND (EFI_ERROR_BIT | UINT64_C(14))
#define EFI_ABORTED (EFI_ERROR_BIT | UINT64_C(21))

enum {
    EFI_ALLOCATE_ANY_PAGES = 0,
    EFI_LOADER_CODE = 1,
    EFI_LOADER_DATA = 2,
    EFI_FILE_MODE_READ = 1
};

enum {
    KERNEL_STAGING_CAP = 1024u * 1024u,
    KERNEL_ALLOCATION_SIZE = 2u * 1024u * 1024u,
    MEMORY_MAP_WORDS = 16384u,
    PAGE_SIZE = 4096u
};

typedef struct EFI_GUID {
    EFI_UINT32 Data1;
    EFI_UINT16 Data2;
    EFI_UINT16 Data3;
    EFI_UINT8 Data4[8];
} EFI_GUID;

typedef struct EFI_CONFIGURATION_TABLE {
    EFI_GUID VendorGuid;
    void *VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct EFI_TABLE_HEADER {
    EFI_UINT64 Signature;
    EFI_UINT32 Revision;
    EFI_UINT32 HeaderSize;
    EFI_UINT32 CRC32;
    EFI_UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, EFI_UINT8);
    EFI_STATUS (EFIAPI *OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, CHAR16 *);
};

typedef AstraFirmwareTextInputProtocol EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

typedef struct EFI_BOOT_SERVICES EFI_BOOT_SERVICES;
typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
typedef struct EFI_LOADED_IMAGE_PROTOCOL EFI_LOADED_IMAGE_PROTOCOL;
typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(
    EFI_HANDLE,
    EFI_GUID *,
    void **
);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(
    EFI_UINT32,
    EFI_UINT32,
    EFI_UINTN,
    EFI_UINT64 *
);
typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(EFI_UINT64, EFI_UINTN);
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(
    EFI_UINTN *,
    void *,
    EFI_UINTN *,
    EFI_UINTN *,
    EFI_UINT32 *
);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE, EFI_UINTN);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(EFI_GUID *, void *, void **);

struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL;
    void *RestoreTPL;
    EFI_ALLOCATE_PAGES AllocatePages;
    EFI_FREE_PAGES FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    void *AllocatePool;
    void *FreePool;
    void *CreateEvent;
    void *SetTimer;
    AstraFirmwareWaitForEvent WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL HandleProtocol;
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
    void *GetNextMonotonicCount;
    void *Stall;
    void *SetWatchdogTimer;
    void *ConnectController;
    void *DisconnectController;
    void *OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;
    void *ProtocolsPerHandle;
    void *LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL LocateProtocol;
};

typedef struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    EFI_UINT32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    EFI_UINTN NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;


enum {
    GOP_PIXEL_RGB_RESERVED = 0,
    GOP_PIXEL_BGR_RESERVED = 1,
    GOP_PIXEL_BITMASK = 2,
    GOP_PIXEL_BLT_ONLY = 3,
    GOP_MAX_DIMENSION = 8192u
};

typedef struct EFI_PIXEL_BITMASK {
    EFI_UINT32 RedMask;
    EFI_UINT32 GreenMask;
    EFI_UINT32 BlueMask;
    EFI_UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct EFI_GOP_MODE_INFO {
    EFI_UINT32 Version;
    EFI_UINT32 HorizontalResolution;
    EFI_UINT32 VerticalResolution;
    EFI_UINT32 PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    EFI_UINT32 PixelsPerScanLine;
} EFI_GOP_MODE_INFO;

typedef struct EFI_GOP_MODE {
    EFI_UINT32 MaxMode;
    EFI_UINT32 Mode;
    EFI_GOP_MODE_INFO *Info;
    EFI_UINTN SizeOfInfo;
    EFI_UINT64 FrameBufferBase;
    EFI_UINTN FrameBufferSize;
} EFI_GOP_MODE;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    void *QueryMode;
    void *SetMode;
    void *Blt;
    EFI_GOP_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

struct EFI_LOADED_IMAGE_PROTOCOL {
    EFI_UINT32 Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
};

struct EFI_FILE_PROTOCOL {
    EFI_UINT64 Revision;
    EFI_STATUS (EFIAPI *Open)(
        EFI_FILE_PROTOCOL *,
        EFI_FILE_PROTOCOL **,
        CHAR16 *,
        EFI_UINT64,
        EFI_UINT64
    );
    EFI_STATUS (EFIAPI *Close)(EFI_FILE_PROTOCOL *);
    EFI_STATUS (EFIAPI *Delete)(EFI_FILE_PROTOCOL *);
    EFI_STATUS (EFIAPI *Read)(EFI_FILE_PROTOCOL *, EFI_UINTN *, void *);
};

struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    EFI_UINT64 Revision;
    EFI_STATUS (EFIAPI *OpenVolume)(
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *,
        EFI_FILE_PROTOCOL **
    );
};

static EFI_GUID g_loaded_image_guid = {
    0x5b1b31a1u, 0x9562u, 0x11d2u,
    {0x8eu, 0x3fu, 0x00u, 0xa0u, 0xc9u, 0x69u, 0x72u, 0x3bu}
};
static EFI_GUID g_simple_file_system_guid = {
    0x964e5b22u, 0x6459u, 0x11d2u,
    {0x8eu, 0x39u, 0x00u, 0xa0u, 0xc9u, 0x69u, 0x72u, 0x3bu}
};

static EFI_GUID g_graphics_output_guid = {
    0x9042a9deu, 0x23dcu, 0x4a38u,
    {0x96u, 0xfbu, 0x7au, 0xdeu, 0xd0u, 0x80u, 0x51u, 0x6au}
};
static EFI_GUID g_simple_pointer_guid = {
    0x31878c87u, 0xb75u, 0x11d5u,
    {0x9au, 0x4fu, 0x00u, 0x90u, 0x27u, 0x3fu, 0xc1u, 0x4du}
};
static EFI_GUID g_absolute_pointer_guid = {
    0x8d59d32bu, 0xc655u, 0x4ae9u,
    {0x9bu, 0x15u, 0xf2u, 0x59u, 0x04u, 0x99u, 0x2au, 0x43u}
};
static const EFI_GUID g_dtb_table_guid = {
    0xb1b621d5u, 0xf19cu, 0x41a5u,
    {0x83u, 0x0bu, 0xd9u, 0x15u, 0x2cu, 0x69u, 0xaau, 0xe0u}
};
static const EFI_GUID g_acpi20_table_guid = {
    0x8868e871u, 0xe4f1u, 0x11d3u,
    {0xbcu, 0x22u, 0x00u, 0x80u, 0xc7u, 0x3cu, 0x88u, 0x81u}
};
static const EFI_GUID g_acpi10_table_guid = {
    0xeb9d2d30u, 0x2d88u, 0x11d3u,
    {0x9au, 0x16u, 0x00u, 0x90u, 0x27u, 0x3fu, 0xc1u, 0x4du}
};
static CHAR16 g_kernel_file_name[] = L"kernel.bin";
static CHAR16 g_platform_dtb_file_name[] = L"platform.dtb";
static CHAR16 g_keyboard_console_file_name[] = L"keyboard-console.cfg";
static EFI_UINT8 g_keyboard_console_marker[64];
static CHAR16 g_desktop_mode_file_name[] = L"desktop-mode.cfg";
static EFI_UINT8 g_desktop_mode_marker[64];
static EFI_UINT8 g_kernel_staging[KERNEL_STAGING_CAP];
static EFI_UINT64 g_memory_map[MEMORY_MAP_WORDS];
static AstraBootInfo g_boot_info;
static EFI_STATUS g_output_error = EFI_SUCCESS;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *g_active_console = NULL;
static bool g_boot_services_exited = false;

static void loader_out(const CHAR16 *text) {
    enum { OUTPUT_CHUNK_CAP = 128 };
    CHAR16 chunk[OUTPUT_CHUNK_CAP];
    EFI_UINTN count = 0;
    EFI_STATUS status = EFI_SUCCESS;

    if (text == NULL || g_output_error != EFI_SUCCESS) {
        return;
    }
    if (g_boot_services_exited) {
        return;
    }

    while (*text != 0) {
        count = 0;
        while (*text != 0 && count + 1u < (EFI_UINTN)OUTPUT_CHUNK_CAP) {
            chunk[count] = *text;
            count++;
            text++;
        }
        chunk[count] = 0;
        if (g_output_error == EFI_SUCCESS) {
            /* The active text protocol is set immediately before the first output. */
            if (g_active_console == NULL || g_active_console->OutputString == NULL) {
                g_output_error = EFI_INVALID_PARAMETER;
                return;
            }
            status = g_active_console->OutputString(g_active_console, chunk);
            if (status != EFI_SUCCESS) {
                g_output_error = status;
                return;
            }
        }
    }
}

static EFI_STATUS read_efi_file(
    EFI_HANDLE image,
    EFI_SYSTEM_TABLE *system_table,
    CHAR16 *file_name,
    EFI_UINT8 *destination,
    size_t destination_capacity,
    size_t *file_size_out
) {
    EFI_STATUS status = EFI_SUCCESS;
    EFI_STATUS close_status = EFI_SUCCESS;
    void *interface = NULL;
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *file_system = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    size_t total = 0u;

    if (image == NULL || system_table == NULL || system_table->BootServices == NULL ||
        file_name == NULL || destination == NULL || destination_capacity == 0u ||
        file_size_out == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *file_size_out = 0u;

    status = system_table->BootServices->HandleProtocol(
        image, &g_loaded_image_guid, &interface
    );
    if (status != EFI_SUCCESS || interface == NULL) {
        return status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
    }
    loaded_image = (EFI_LOADED_IMAGE_PROTOCOL *)interface;
    if (loaded_image->DeviceHandle == NULL) {
        return EFI_NOT_FOUND;
    }

    interface = NULL;
    status = system_table->BootServices->HandleProtocol(
        loaded_image->DeviceHandle, &g_simple_file_system_guid, &interface
    );
    if (status != EFI_SUCCESS || interface == NULL) {
        return status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
    }
    file_system = (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *)interface;
    if (file_system->OpenVolume == NULL) {
        return EFI_UNSUPPORTED;
    }

    status = file_system->OpenVolume(file_system, &root);
    if (status != EFI_SUCCESS || root == NULL) {
        return status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
    }
    if (root->Open == NULL || root->Close == NULL) {
        status = EFI_UNSUPPORTED;
        goto cleanup;
    }

    status = root->Open(root, &file, file_name, EFI_FILE_MODE_READ, 0);
    if (status != EFI_SUCCESS || file == NULL) {
        status = status == EFI_SUCCESS ? EFI_NOT_FOUND : status;
        goto cleanup;
    }
    if (file->Read == NULL || file->Close == NULL) {
        status = EFI_UNSUPPORTED;
        goto cleanup;
    }

    for (;;) {
        EFI_UINTN request = 0u;
        size_t remaining = destination_capacity - total;

        if (remaining == 0u) {
            EFI_UINT8 extra_byte = 0u;
            request = 1u;
            status = file->Read(file, &request, &extra_byte);
            if (status != EFI_SUCCESS) {
                goto cleanup;
            }
            if (request != 0u) {
                status = EFI_BAD_BUFFER_SIZE;
                goto cleanup;
            }
            break;
        }

        request = (EFI_UINTN)remaining;
        status = file->Read(file, &request, &destination[total]);
        if (status != EFI_SUCCESS) {
            goto cleanup;
        }
        if (request > (EFI_UINTN)remaining) {
            status = EFI_BAD_BUFFER_SIZE;
            goto cleanup;
        }
        if (request == 0u) {
            break;
        }
        total += (size_t)request;
    }

    if (total == 0u) {
        status = EFI_LOAD_ERROR;
        goto cleanup;
    }

cleanup:
    if (file != NULL && file->Close != NULL) {
        close_status = file->Close(file);
        if (status == EFI_SUCCESS && close_status != EFI_SUCCESS) {
            status = close_status;
        }
    }
    if (root != NULL && root->Close != NULL) {
        close_status = root->Close(root);
        if (status == EFI_SUCCESS && close_status != EFI_SUCCESS) {
            status = close_status;
        }
    }
    if (status == EFI_SUCCESS) {
        *file_size_out = total;
    }
    return status;
}

static EFI_STATUS read_kernel(
    EFI_HANDLE image,
    EFI_SYSTEM_TABLE *system_table,
    size_t *kernel_size_out
) {
    return read_efi_file(
        image,
        system_table,
        g_kernel_file_name,
        g_kernel_staging,
        sizeof g_kernel_staging,
        kernel_size_out
    );
}

/* The marker is included only in images that request firmware-backed keyboard input. */
static bool keyboard_console_mode_requested(
    EFI_HANDLE image,
    EFI_SYSTEM_TABLE *system_table
) {
    static const EFI_UINT8 expected[] = "ASTRAOS_UEFI_KEYBOARD=1\n";
    EFI_STATUS status = EFI_SUCCESS;
    size_t file_size = 0u;

    status = read_efi_file(
        image,
        system_table,
        g_keyboard_console_file_name,
        g_keyboard_console_marker,
        sizeof g_keyboard_console_marker,
        &file_size
    );
    if (status == EFI_NOT_FOUND) {
        return false;
    }
    if (status != EFI_SUCCESS) {
        loader_out(L"Warning: could not read keyboard-console.cfg.\r\n");
        return false;
    }
    if (file_size != sizeof expected - 1u) {
        loader_out(L"Warning: keyboard-console.cfg has an invalid size.\r\n");
        return false;
    }
    for (size_t i = 0u; i < file_size; i++) {
        if (g_keyboard_console_marker[i] != expected[i]) {
            loader_out(L"Warning: keyboard-console.cfg has invalid contents.\r\n");
            return false;
        }
    }
    return true;
}

/* A separate marker opts this image into starting the graphical desktop. */
static bool desktop_mode_requested(
    EFI_HANDLE image,
    EFI_SYSTEM_TABLE *system_table
) {
    static const EFI_UINT8 expected[] = "ASTRAOS_DESKTOP=1\n";
    EFI_STATUS status = EFI_SUCCESS;
    size_t file_size = 0u;

    status = read_efi_file(
        image,
        system_table,
        g_desktop_mode_file_name,
        g_desktop_mode_marker,
        sizeof g_desktop_mode_marker,
        &file_size
    );
    if (status == EFI_NOT_FOUND) {
        return false;
    }
    if (status != EFI_SUCCESS) {
        loader_out(L"Warning: could not read desktop-mode.cfg.\r\n");
        return false;
    }
    if (file_size != sizeof expected - 1u) {
        loader_out(L"Warning: desktop-mode.cfg has an invalid size.\r\n");
        return false;
    }
    for (size_t i = 0u; i < file_size; i++) {
        if (g_desktop_mode_marker[i] != expected[i]) {
            loader_out(L"Warning: desktop-mode.cfg has invalid contents.\r\n");
            return false;
        }
    }
    return true;
}

/*
 * Captures firmware input and exception state. In this mode, Boot Services stay
 * active intentionally, and the page allocator remains disabled in the kernel.
 */
static bool configure_firmware_keyboard_console(
    AstraBootInfo *boot_info,
    EFI_SYSTEM_TABLE *system_table
) {
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *input = NULL;
    EFI_UINT64 current_el = 0u;
    EFI_UINT64 daif = 0u;
    EFI_UINT64 vector = 0u;
    EFI_UINT64 exception_level = 0u;

    if (boot_info == NULL || system_table == NULL ||
        system_table->BootServices == NULL ||
        system_table->BootServices->WaitForEvent == NULL ||
        system_table->ConIn == NULL) {
        return false;
    }

    input = (EFI_SIMPLE_TEXT_INPUT_PROTOCOL *)system_table->ConIn;
    if (input->read_key_stroke == NULL || input->wait_for_key == NULL) {
        return false;
    }

    __asm__ volatile("mrs %0, CurrentEL" : "=r"(current_el));
    __asm__ volatile("mrs %0, DAIF" : "=r"(daif));
    exception_level = (current_el >> 2u) & UINT64_C(0x3);
    if (exception_level == UINT64_C(1)) {
        __asm__ volatile("mrs %0, VBAR_EL1" : "=r"(vector));
    } else if (exception_level == UINT64_C(2)) {
        __asm__ volatile("mrs %0, VBAR_EL2" : "=r"(vector));
    } else {
        return false;
    }

    if (vector == 0u || (vector & UINT64_C(0x7ff)) != 0u) {
        return false;
    }

    boot_info->flags = ASTRA_BOOT_FLAG_BOOT_SERVICES_ACTIVE |
        ASTRA_BOOT_FLAG_UEFI_TEXT_INPUT;
    boot_info->firmware_input_protocol = (AstraFirmwareTextInputProtocol *)input;
    boot_info->firmware_wait_for_event = system_table->BootServices->WaitForEvent;
    boot_info->firmware_exception_level = exception_level;
    boot_info->firmware_exception_vector = vector;
    /* UEFI input/timer events need IRQ delivery while firmware vectors are active. */
    boot_info->firmware_daif = daif & ~(UINT64_C(1) << 7u);
    return true;
}

static bool guid_equal(const EFI_GUID *left, const EFI_GUID *right) {
    if (left == NULL || right == NULL ||
        left->Data1 != right->Data1 ||
        left->Data2 != right->Data2 ||
        left->Data3 != right->Data3) {
        return false;
    }
    for (size_t i = 0; i < sizeof left->Data4; i++) {
        if (left->Data4[i] != right->Data4[i]) {
            return false;
        }
    }
    return true;
}

enum {
    EFI_MEMORY_DESCRIPTOR_MIN_SIZE = 40u,
    EFI_MEMORY_TYPE_OFFSET = 0u,
    EFI_MEMORY_PHYSICAL_START_OFFSET = 8u,
    EFI_MEMORY_NUMBER_OF_PAGES_OFFSET = 24u,
    EFI_ACPI_RECLAIM_MEMORY = 9u
};

static EFI_UINT32 read_memory_u32_le(const EFI_UINT8 *bytes) {
    return (EFI_UINT32)bytes[0]
        | ((EFI_UINT32)bytes[1] << 8u)
        | ((EFI_UINT32)bytes[2] << 16u)
        | ((EFI_UINT32)bytes[3] << 24u);
}

static EFI_UINT64 read_memory_u64_le(const EFI_UINT8 *bytes) {
    EFI_UINT64 value = 0;

    for (size_t i = 0; i < 8u; i++) {
        value |= (EFI_UINT64)bytes[i] << (i * 8u);
    }
    return value;
}

/* Return only bytes proven to lie inside the DTB's UEFI reclaim-memory descriptor. */
static bool find_dtb_available_size(
    EFI_BOOT_SERVICES *boot_services,
    EFI_UINT64 dtb_address,
    size_t *available_out
) {
    EFI_UINTN map_size = (EFI_UINTN)sizeof g_memory_map;
    EFI_UINTN map_key = 0;
    EFI_UINTN descriptor_size = 0;
    EFI_UINT32 descriptor_version = 0;
    EFI_STATUS status = EFI_UNSUPPORTED;
    const EFI_UINT8 *map = (const EFI_UINT8 *)g_memory_map;

    if (available_out == NULL) {
        return false;
    }
    *available_out = 0u;
    if (boot_services == NULL || boot_services->GetMemoryMap == NULL ||
        dtb_address == 0u) {
        return false;
    }

    status = boot_services->GetMemoryMap(
        &map_size,
        g_memory_map,
        &map_key,
        &descriptor_size,
        &descriptor_version
    );
    if (status != EFI_SUCCESS ||
        map_size > (EFI_UINTN)sizeof g_memory_map ||
        descriptor_size < (EFI_UINTN)EFI_MEMORY_DESCRIPTOR_MIN_SIZE ||
        descriptor_size == 0u ||
        map_size % descriptor_size != 0u) {
        return false;
    }

    for (EFI_UINTN offset = 0; offset < map_size; offset += descriptor_size) {
        const EFI_UINT8 *descriptor = &map[(size_t)offset];
        EFI_UINT32 type = read_memory_u32_le(
            &descriptor[EFI_MEMORY_TYPE_OFFSET]
        );
        EFI_UINT64 base = read_memory_u64_le(
            &descriptor[EFI_MEMORY_PHYSICAL_START_OFFSET]
        );
        EFI_UINT64 pages = read_memory_u64_le(
            &descriptor[EFI_MEMORY_NUMBER_OF_PAGES_OFFSET]
        );
        EFI_UINT64 span = 0;
        EFI_UINT64 end = 0;
        EFI_UINT64 remaining = 0;

        if (type != (EFI_UINT32)EFI_ACPI_RECLAIM_MEMORY || pages == 0u) {
            continue;
        }
        if (pages > (UINT64_MAX - base) / (EFI_UINT64)PAGE_SIZE) {
            continue;
        }

        span = pages * (EFI_UINT64)PAGE_SIZE;
        end = base + span;
        if (dtb_address < base || dtb_address >= end) {
            continue;
        }

        remaining = end - dtb_address;
        if (remaining > (EFI_UINT64)ASTRA_FDT_MAX_SIZE) {
            remaining = (EFI_UINT64)ASTRA_FDT_MAX_SIZE;
        }
        if (remaining < (EFI_UINT64)ASTRA_FDT_HEADER_SIZE) {
            return false;
        }
        *available_out = (size_t)remaining;
        return true;
    }

    return false;
}

/* The kernel validates this pointer against the final UEFI memory map. */
static bool capture_acpi_rsdp_from_guid(
    EFI_SYSTEM_TABLE *system_table,
    const EFI_GUID *guid,
    EFI_UINT64 *address_out
) {
    enum { MAX_CONFIGURATION_TABLES = 4096u };
    EFI_CONFIGURATION_TABLE *tables = NULL;

    if (system_table == NULL || guid == NULL || address_out == NULL ||
        system_table->ConfigurationTable == NULL ||
        system_table->NumberOfTableEntries == 0u ||
        system_table->NumberOfTableEntries > (EFI_UINTN)MAX_CONFIGURATION_TABLES) {
        return false;
    }

    tables = system_table->ConfigurationTable;
    for (EFI_UINTN i = 0u; i < system_table->NumberOfTableEntries; i++) {
        EFI_UINT64 address = 0u;

        if (!guid_equal(&tables[i].VendorGuid, guid) ||
            tables[i].VendorTable == NULL) {
            continue;
        }
        address = (EFI_UINT64)(uintptr_t)tables[i].VendorTable;
        if (address == 0u) {
            continue;
        }
        *address_out = address;
        return true;
    }
    return false;
}

static void capture_acpi_rsdp(
    EFI_SYSTEM_TABLE *system_table,
    AstraBootInfo *boot_info
) {
    if (boot_info == NULL) {
        return;
    }
    boot_info->acpi_rsdp_base = 0u;
    if (capture_acpi_rsdp_from_guid(
            system_table, &g_acpi20_table_guid, &boot_info->acpi_rsdp_base
        )) {
        loader_out(L"ACPI 2.0+ RSDP pointer captured for validated kernel discovery.\r\n");
        return;
    }
    if (capture_acpi_rsdp_from_guid(
            system_table, &g_acpi10_table_guid, &boot_info->acpi_rsdp_base
        )) {
        loader_out(L"ACPI 1.0 RSDP pointer captured for kernel discovery.\r\n");
    }
}

/* The firmware owns the DTB memory; the kernel borrows it after ExitBootServices. */
static bool capture_device_tree_from_firmware(
    EFI_SYSTEM_TABLE *system_table,
    AstraBootInfo *boot_info
) {
    enum { MAX_CONFIGURATION_TABLES = 4096u };
    EFI_CONFIGURATION_TABLE *tables = NULL;
    AstraFdtView view;
    AstraFdtResult result = ASTRA_FDT_OK;
    size_t available = 0u;

    if (system_table == NULL || boot_info == NULL ||
        system_table->ConfigurationTable == NULL ||
        system_table->NumberOfTableEntries == 0u ||
        system_table->NumberOfTableEntries > (EFI_UINTN)MAX_CONFIGURATION_TABLES) {
        return false;
    }

    tables = system_table->ConfigurationTable;
    for (EFI_UINTN i = 0u; i < system_table->NumberOfTableEntries; i++) {
        if (!guid_equal(&tables[i].VendorGuid, &g_dtb_table_guid)) {
            continue;
        }
        if (tables[i].VendorTable == NULL) {
            loader_out(L"Warning: firmware device-tree pointer is null.\r\n");
            continue;
        }
        if (!find_dtb_available_size(
                system_table->BootServices,
                (EFI_UINT64)(uintptr_t)tables[i].VendorTable,
                &available)) {
            loader_out(L"Warning: firmware device tree is outside ACPI reclaim memory.\r\n");
            continue;
        }

        result = astra_fdt_open(tables[i].VendorTable, available, &view);
        if (result != ASTRA_FDT_OK) {
            loader_out(L"Warning: firmware device tree failed validation.\r\n");
            continue;
        }

        boot_info->device_tree_base = (EFI_UINT64)(uintptr_t)view.data;
        boot_info->device_tree_size = (EFI_UINT64)view.total_size;
        loader_out(L"Validated firmware device tree found.\r\n");
        return true;
    }
    return false;
}

/*
 * A valid firmware-provided DTB is preferred. If it is absent or invalid,
 * a board-matched platform.dtb can be placed at the EFI partition root.
 */
static void capture_device_tree(
    EFI_HANDLE image,
    EFI_SYSTEM_TABLE *system_table,
    AstraBootInfo *boot_info
) {
    EFI_STATUS status = EFI_SUCCESS;
    EFI_STATUS free_status = EFI_SUCCESS;
    AstraFdtView view;
    AstraFdtView copied_view;
    AstraFdtResult result = ASTRA_FDT_OK;
    size_t file_size = 0u;
    EFI_UINTN page_count = 0u;
    EFI_UINT64 allocation_address = 0u;
    EFI_UINT64 allocation_size = 0u;
    EFI_UINT8 *destination = NULL;

    if (boot_info == NULL) {
        return;
    }
    boot_info->device_tree_base = 0u;
    boot_info->device_tree_size = 0u;

    if (capture_device_tree_from_firmware(system_table, boot_info)) {
        return;
    }
    if (system_table == NULL || system_table->BootServices == NULL ||
        system_table->BootServices->AllocatePages == NULL ||
        system_table->BootServices->FreePages == NULL) {
        loader_out(L"Warning: platform.dtb fallback cannot be allocated.\r\n");
        return;
    }

    status = read_efi_file(
        image,
        system_table,
        g_platform_dtb_file_name,
        g_kernel_staging,
        sizeof g_kernel_staging,
        &file_size
    );
    if (status == EFI_NOT_FOUND) {
        loader_out(L"No firmware DTB or optional platform.dtb found.\r\n");
        return;
    }
    if (status != EFI_SUCCESS) {
        loader_out(L"Warning: could not read optional platform.dtb.\r\n");
        return;
    }

    result = astra_fdt_open(g_kernel_staging, file_size, &view);
    if (result != ASTRA_FDT_OK) {
        loader_out(L"Warning: optional platform.dtb failed validation.\r\n");
        return;
    }
    if (view.total_size > SIZE_MAX - ((size_t)PAGE_SIZE - 1u)) {
        loader_out(L"Warning: platform.dtb size overflows page rounding.\r\n");
        return;
    }

    page_count = (EFI_UINTN)(
        (view.total_size + (size_t)PAGE_SIZE - 1u) / (size_t)PAGE_SIZE
    );
    if (page_count == 0u ||
        (EFI_UINT64)page_count > UINT64_MAX / (EFI_UINT64)PAGE_SIZE) {
        loader_out(L"Warning: platform.dtb page count is invalid.\r\n");
        return;
    }
    allocation_size = (EFI_UINT64)page_count * (EFI_UINT64)PAGE_SIZE;

    status = system_table->BootServices->AllocatePages(
        EFI_ALLOCATE_ANY_PAGES,
        EFI_LOADER_DATA,
        page_count,
        &allocation_address
    );
    if (status != EFI_SUCCESS) {
        loader_out(L"Warning: could not allocate resident memory for platform.dtb.\r\n");
        return;
    }
    if (allocation_address == 0u ||
        (allocation_address % (EFI_UINT64)PAGE_SIZE) != 0u ||
        allocation_address > (EFI_UINT64)UINTPTR_MAX ||
        allocation_address > UINT64_MAX - allocation_size ||
        allocation_size < (EFI_UINT64)view.total_size) {
        free_status = system_table->BootServices->FreePages(
            allocation_address,
            page_count
        );
        if (free_status != EFI_SUCCESS) {
            loader_out(L"Warning: invalid platform.dtb allocation could not be released.\r\n");
        }
        loader_out(L"Warning: firmware returned an invalid platform.dtb allocation.\r\n");
        return;
    }

    destination = (EFI_UINT8 *)(uintptr_t)allocation_address;
    for (size_t i = 0u; i < view.total_size; i++) {
        destination[i] = g_kernel_staging[i];
    }

    result = astra_fdt_open(destination, view.total_size, &copied_view);
    if (result != ASTRA_FDT_OK) {
        free_status = system_table->BootServices->FreePages(
            allocation_address,
            page_count
        );
        if (free_status != EFI_SUCCESS) {
            loader_out(L"Warning: failed DTB copy could not be released.\r\n");
        }
        loader_out(L"Warning: copied platform.dtb failed validation.\r\n");
        return;
    }

    boot_info->device_tree_base = allocation_address;
    boot_info->device_tree_size = (EFI_UINT64)copied_view.total_size;
    loader_out(L"Loaded validated platform.dtb fallback.\r\n");
}

static void clear_framebuffer_info(AstraBootInfo *boot_info) {
    boot_info->framebuffer_base = 0;
    boot_info->framebuffer_size = 0;
    boot_info->framebuffer_width = 0;
    boot_info->framebuffer_height = 0;
    boot_info->framebuffer_pixels_per_scanline = 0;
    boot_info->framebuffer_pixel_format = GOP_PIXEL_BLT_ONLY;
    boot_info->framebuffer_red_mask = 0;
    boot_info->framebuffer_green_mask = 0;
    boot_info->framebuffer_blue_mask = 0;
    boot_info->framebuffer_reserved_mask = 0;
}

static bool mask_is_contiguous(EFI_UINT32 mask) {
    if (mask == 0u) {
        return false;
    }
    while ((mask & 1u) == 0u) {
        mask >>= 1u;
    }
    return (mask & (mask + 1u)) == 0u;
}

/* Copy validated GOP metadata into the boot contract; no protocol pointer escapes. */
static void capture_framebuffer(EFI_BOOT_SERVICES *boot_services, AstraBootInfo *boot_info) {
    EFI_STATUS status = EFI_UNSUPPORTED;
    void *interface = NULL;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_GOP_MODE *mode = NULL;
    EFI_GOP_MODE_INFO *info = NULL;
    EFI_UINT64 pixels = 0;
    EFI_UINT64 required_bytes = 0;
    EFI_UINT32 mask_union = 0;

    clear_framebuffer_info(boot_info);
    if (boot_services == NULL || boot_services->LocateProtocol == NULL) {
        return;
    }

    status = boot_services->LocateProtocol(&g_graphics_output_guid, NULL, &interface);
    if (status != EFI_SUCCESS || interface == NULL) {
        return;
    }
    gop = (EFI_GRAPHICS_OUTPUT_PROTOCOL *)interface;
    mode = gop->Mode;
    if (mode == NULL || mode->Info == NULL ||
        mode->SizeOfInfo < (EFI_UINTN)sizeof(EFI_GOP_MODE_INFO) ||
        mode->FrameBufferBase == 0 ||
        mode->FrameBufferBase > (EFI_UINT64)UINTPTR_MAX ||
        mode->FrameBufferSize == 0) {
        return;
    }

    info = mode->Info;
    if (info->HorizontalResolution < 8u || info->VerticalResolution < 16u ||
        info->HorizontalResolution > GOP_MAX_DIMENSION ||
        info->VerticalResolution > GOP_MAX_DIMENSION ||
        info->PixelsPerScanLine < info->HorizontalResolution ||
        info->PixelsPerScanLine > GOP_MAX_DIMENSION ||
        info->PixelFormat > GOP_PIXEL_BITMASK ||
        info->PixelFormat == GOP_PIXEL_BLT_ONLY) {
        return;
    }

    pixels = (EFI_UINT64)info->PixelsPerScanLine * (EFI_UINT64)info->VerticalResolution;
    if (pixels > UINT64_MAX / UINT64_C(4)) {
        return;
    }
    required_bytes = pixels * UINT64_C(4);
    if (required_bytes > (EFI_UINT64)mode->FrameBufferSize ||
        mode->FrameBufferBase > UINT64_MAX - required_bytes) {
        return;
    }

    if (info->PixelFormat == GOP_PIXEL_BITMASK) {
        mask_union = info->PixelInformation.RedMask |
            info->PixelInformation.GreenMask |
            info->PixelInformation.BlueMask |
            info->PixelInformation.ReservedMask;
        if (mask_union == 0u ||
            !mask_is_contiguous(info->PixelInformation.RedMask) ||
            !mask_is_contiguous(info->PixelInformation.GreenMask) ||
            !mask_is_contiguous(info->PixelInformation.BlueMask) ||
            (info->PixelInformation.ReservedMask != 0u &&
                !mask_is_contiguous(info->PixelInformation.ReservedMask)) ||
            (info->PixelInformation.RedMask & info->PixelInformation.GreenMask) != 0u ||
            (info->PixelInformation.RedMask & info->PixelInformation.BlueMask) != 0u ||
            (info->PixelInformation.RedMask & info->PixelInformation.ReservedMask) != 0u ||
            (info->PixelInformation.GreenMask & info->PixelInformation.BlueMask) != 0u ||
            (info->PixelInformation.GreenMask & info->PixelInformation.ReservedMask) != 0u ||
            (info->PixelInformation.BlueMask & info->PixelInformation.ReservedMask) != 0u) {
            return;
        }
    }

    boot_info->framebuffer_base = mode->FrameBufferBase;
    boot_info->framebuffer_size = (EFI_UINT64)mode->FrameBufferSize;
    boot_info->framebuffer_width = info->HorizontalResolution;
    boot_info->framebuffer_height = info->VerticalResolution;
    boot_info->framebuffer_pixels_per_scanline = info->PixelsPerScanLine;
    boot_info->framebuffer_pixel_format = info->PixelFormat;
    boot_info->framebuffer_red_mask = info->PixelInformation.RedMask;
    boot_info->framebuffer_green_mask = info->PixelInformation.GreenMask;
    boot_info->framebuffer_blue_mask = info->PixelInformation.BlueMask;
    boot_info->framebuffer_reserved_mask = info->PixelInformation.ReservedMask;
}


/* Capture a firmware-owned pointer protocol only for the Boot-Services desktop path. */

/* Capture firmware-owned pointer protocols for the UEFI desktop session. */
static void capture_firmware_pointer(
    EFI_BOOT_SERVICES *boot_services,
    AstraBootInfo *boot_info
) {
    EFI_STATUS status = EFI_UNSUPPORTED;
    void *interface = NULL;
    bool found_pointer = false;
    AstraFirmwareSimplePointerProtocol *relative_pointer = NULL;
    AstraFirmwareAbsolutePointerProtocol *absolute_pointer = NULL;

    if (boot_services == NULL || boot_info == NULL ||
        boot_services->LocateProtocol == NULL) {
        return;
    }

    status = boot_services->LocateProtocol(&g_absolute_pointer_guid, NULL, &interface);
    if (status == EFI_SUCCESS && interface != NULL) {
        absolute_pointer = (AstraFirmwareAbsolutePointerProtocol *)interface;
        if (absolute_pointer->get_state != NULL &&
            absolute_pointer->wait_for_input != NULL &&
            absolute_pointer->mode != NULL &&
            absolute_pointer->mode->absolute_max_x >
                absolute_pointer->mode->absolute_min_x &&
            absolute_pointer->mode->absolute_max_y >
                absolute_pointer->mode->absolute_min_y &&
            absolute_pointer->mode->absolute_max_x -
                absolute_pointer->mode->absolute_min_x <= UINT32_MAX &&
            absolute_pointer->mode->absolute_max_y -
                absolute_pointer->mode->absolute_min_y <= UINT32_MAX) {
            boot_info->firmware_absolute_pointer_protocol = absolute_pointer;
            boot_info->flags |= ASTRA_BOOT_FLAG_UEFI_ABSOLUTE_POINTER_INPUT;
            found_pointer = true;
            loader_out(L"UEFI Absolute Pointer Protocol found; absolute mouse input is enabled.\r\n");
        }
    }

    interface = NULL;
    status = boot_services->LocateProtocol(&g_simple_pointer_guid, NULL, &interface);
    if (status == EFI_SUCCESS && interface != NULL) {
        relative_pointer = (AstraFirmwareSimplePointerProtocol *)interface;
        if (relative_pointer->get_state != NULL &&
            relative_pointer->wait_for_input != NULL) {
            boot_info->firmware_pointer_protocol = relative_pointer;
            boot_info->flags |= ASTRA_BOOT_FLAG_UEFI_POINTER_INPUT;
            found_pointer = true;
            loader_out(L"UEFI Simple Pointer Protocol found; relative mouse input is enabled.\r\n");
        }
    }

    if (!found_pointer) {
        loader_out(L"No supported UEFI pointer protocol found; mouse input is unavailable.\r\n");
    }
}

static void stop_forever(void) {
    for (;;) {
        __asm__ volatile("wfe");
    }
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table);
EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table) {
    EFI_STATUS status = EFI_SUCCESS;
    size_t kernel_size = 0;
    EFI_UINT64 kernel_address = 0;
    EFI_STATUS free_status = EFI_SUCCESS;
    EFI_UINTN page_count = (EFI_UINTN)(KERNEL_ALLOCATION_SIZE / PAGE_SIZE);
    EFI_UINTN map_size = 0;
    EFI_UINTN map_key = 0;
    EFI_UINTN descriptor_size = 0;
    EFI_UINT32 descriptor_version = 0;
    EFI_BOOT_SERVICES *boot_services = NULL;
    EFI_UINT8 *kernel_destination = NULL;
    bool keep_boot_services_active = false;
    typedef void (*KernelEntry)(const AstraBootInfo *);

    if (system_table == NULL || system_table->BootServices == NULL ||
        system_table->ConOut == NULL || system_table->ConOut->OutputString == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    g_active_console = system_table->ConOut;
    loader_out(L"\r\nAstraOS UEFI loader 0.2\r\n");

    boot_services = system_table->BootServices;
    if (boot_services->HandleProtocol == NULL || boot_services->AllocatePages == NULL ||
        boot_services->FreePages == NULL || boot_services->GetMemoryMap == NULL ||
        boot_services->ExitBootServices == NULL) {
        loader_out(L"error: required UEFI services are unavailable\r\n");
        return EFI_UNSUPPORTED;
    }

    loader_out(L"Loading kernel.bin from the boot filesystem...\r\n");
    status = read_kernel(image, system_table, &kernel_size);
    if (status != EFI_SUCCESS) {
        loader_out(L"error: could not load kernel.bin\r\n");
        return status;
    }
    if (kernel_size > (size_t)KERNEL_ALLOCATION_SIZE) {
        loader_out(L"error: kernel image exceeds reserved memory\r\n");
        return EFI_BAD_BUFFER_SIZE;
    }

    status = boot_services->AllocatePages(
        EFI_ALLOCATE_ANY_PAGES,
        EFI_LOADER_CODE,
        page_count,
        &kernel_address
    );
    if (status != EFI_SUCCESS) {
        loader_out(L"error: could not allocate memory for kernel\r\n");
        return status;
    }
    if (kernel_address == 0 ||
        (kernel_address % (EFI_UINT64)PAGE_SIZE) != 0 ||
        kernel_address > UINT64_MAX - (EFI_UINT64)KERNEL_ALLOCATION_SIZE) {
        free_status = boot_services->FreePages(kernel_address, page_count);
        if (free_status != EFI_SUCCESS) {
            loader_out(L"error: invalid kernel allocation could not be released\r\n");
        }
        loader_out(L"error: firmware returned an invalid kernel memory range\r\n");
        return EFI_LOAD_ERROR;
    }

    kernel_destination = (EFI_UINT8 *)(uintptr_t)kernel_address;
    for (size_t i = 0; i < (size_t)KERNEL_ALLOCATION_SIZE; i++) {
        kernel_destination[i] = 0;
    }
    for (size_t i = 0; i < kernel_size; i++) {
        kernel_destination[i] = g_kernel_staging[i];
    }

    g_boot_info.magic = ASTRA_BOOT_INFO_MAGIC;
    g_boot_info.version = ASTRA_BOOT_INFO_VERSION;
    g_boot_info.memory_map = g_memory_map;
    g_boot_info.memory_map_size = 0;
    g_boot_info.memory_descriptor_size = 0;
    g_boot_info.memory_descriptor_version = 0;
    g_boot_info.kernel_base = kernel_address;
    g_boot_info.kernel_image_size = (EFI_UINT64)kernel_size;
    g_boot_info.kernel_allocation_size = KERNEL_ALLOCATION_SIZE;
    clear_framebuffer_info(&g_boot_info);
    capture_framebuffer(boot_services, &g_boot_info);
    capture_device_tree(image, system_table, &g_boot_info);
    capture_acpi_rsdp(system_table, &g_boot_info);
    g_boot_info.flags = 0u;
    g_boot_info.firmware_input_protocol = NULL;
    g_boot_info.firmware_wait_for_event = NULL;
    g_boot_info.firmware_exception_level = 0u;
    g_boot_info.firmware_exception_vector = 0u;
    g_boot_info.firmware_daif = 0u;
    g_boot_info.firmware_pointer_protocol = NULL;
    g_boot_info.firmware_absolute_pointer_protocol = NULL;

    if (keyboard_console_mode_requested(image, system_table)) {
        if (configure_firmware_keyboard_console(&g_boot_info, system_table)) {
            keep_boot_services_active = true;
            loader_out(
                L"UEFI keyboard console enabled; firmware services will remain active.\r\n"
            );
            if (desktop_mode_requested(image, system_table)) {
                g_boot_info.flags |= ASTRA_BOOT_FLAG_DESKTOP_MODE;
                loader_out(L"Graphical desktop mode enabled.\r\n");
                capture_firmware_pointer(boot_services, &g_boot_info);
            }
        } else {
            loader_out(
                L"Warning: UEFI keyboard input unavailable; using normal kernel handoff.\r\n"
            );
        }
    }

    if (g_boot_info.framebuffer_base != 0) {
        loader_out(L"UEFI GOP framebuffer discovered.\r\n");
    } else {
        loader_out(L"No usable GOP framebuffer; serial console remains available.\r\n");
    }
    loader_out(L"Kernel copied. Capturing the UEFI memory map...\r\n");
    status = EFI_INVALID_PARAMETER;
    for (size_t attempt = 0; attempt < 3u; attempt++) {
        map_size = (EFI_UINTN)sizeof g_memory_map;
        status = boot_services->GetMemoryMap(
            &map_size,
            g_memory_map,
            &map_key,
            &descriptor_size,
            &descriptor_version
        );
        if (status != EFI_SUCCESS) {
            break;
        }

        g_boot_info.memory_map_size = (EFI_UINT64)map_size;
        g_boot_info.memory_descriptor_size = (EFI_UINT64)descriptor_size;
        g_boot_info.memory_descriptor_version = (EFI_UINT64)descriptor_version;

        if (keep_boot_services_active) {
            status = EFI_SUCCESS;
            break;
        }

        status = boot_services->ExitBootServices(image, map_key);
        if (status == EFI_SUCCESS) {
            break;
        }
        if (status != EFI_INVALID_PARAMETER) {
            break;
        }
    }

    if (status != EFI_SUCCESS) {
        /* A failed ExitBootServices can leave firmware partly shut down. */
        stop_forever();
    }
    if (!keep_boot_services_active) {
        g_boot_services_exited = true;
    }

    /* Firmware-backed keyboard mode retains Boot Services by design. */
    {
        KernelEntry entry = (KernelEntry)(uintptr_t)kernel_address;
        entry(&g_boot_info);
    }
    stop_forever();
    return EFI_ABORTED;
}
