#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../kernel/boot_info.h"

typedef unsigned char EFI_UINT8;
typedef unsigned short EFI_UINT16;
typedef unsigned int EFI_UINT32;
typedef unsigned long long EFI_UINT64;
typedef unsigned long long EFI_UINTN;
typedef EFI_UINT64 EFI_STATUS;
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
    EFI_ALLOCATE_ADDRESS = 2,
    EFI_LOADER_CODE = 1,
    EFI_FILE_MODE_READ = 1
};

enum {
    KERNEL_STAGING_CAP = 1024u * 1024u,
    KERNEL_ALLOCATION_SIZE = 2u * 1024u * 1024u,
    MEMORY_MAP_WORDS = 16384u,
    PAGE_SIZE = 4096u
};

#define KERNEL_LOAD_BASE UINT64_C(0x40200000)

typedef struct EFI_GUID {
    EFI_UINT32 Data1;
    EFI_UINT16 Data2;
    EFI_UINT16 Data3;
    EFI_UINT8 Data4[8];
} EFI_GUID;

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
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(
    EFI_UINTN *,
    void *,
    EFI_UINTN *,
    EFI_UINTN *,
    EFI_UINT32 *
);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE, EFI_UINTN);

struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL;
    void *RestoreTPL;
    EFI_ALLOCATE_PAGES AllocatePages;
    void *FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    void *AllocatePool;
    void *FreePool;
    void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
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
    void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

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
static CHAR16 g_kernel_file_name[] = L"kernel.bin";
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

static EFI_STATUS read_kernel(
    EFI_HANDLE image,
    EFI_SYSTEM_TABLE *system_table,
    size_t *kernel_size_out
) {
    EFI_STATUS status = EFI_SUCCESS;
    EFI_STATUS close_status = EFI_SUCCESS;
    void *interface = NULL;
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *file_system = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    size_t total = 0;

    if (image == NULL || system_table == NULL || system_table->BootServices == NULL ||
        kernel_size_out == NULL) {
        return EFI_INVALID_PARAMETER;
    }

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

    status = root->Open(root, &file, g_kernel_file_name, EFI_FILE_MODE_READ, 0);
    if (status != EFI_SUCCESS || file == NULL) {
        status = status == EFI_SUCCESS ? EFI_NOT_FOUND : status;
        goto cleanup;
    }
    if (file->Read == NULL || file->Close == NULL) {
        status = EFI_UNSUPPORTED;
        goto cleanup;
    }

    for (;;) {
        EFI_UINTN request = 0;

        if (total == (size_t)KERNEL_STAGING_CAP) {
            EFI_UINT8 extra_byte = 0;
            request = 1;
            status = file->Read(file, &request, &extra_byte);
            if (status != EFI_SUCCESS) {
                goto cleanup;
            }
            if (request != 0) {
                status = EFI_BAD_BUFFER_SIZE;
                goto cleanup;
            }
            break;
        }

        request = (EFI_UINTN)((size_t)KERNEL_STAGING_CAP - total);
        status = file->Read(file, &request, &g_kernel_staging[total]);
        if (status != EFI_SUCCESS) {
            goto cleanup;
        }
        if (request > (EFI_UINTN)((size_t)KERNEL_STAGING_CAP - total)) {
            status = EFI_BAD_BUFFER_SIZE;
            goto cleanup;
        }
        if (request == 0) {
            break;
        }
        total += (size_t)request;
    }

    if (total == 0) {
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
        *kernel_size_out = total;
    }
    return status;
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
    EFI_UINT64 kernel_address = KERNEL_LOAD_BASE;
    EFI_UINTN page_count = (EFI_UINTN)(KERNEL_ALLOCATION_SIZE / PAGE_SIZE);
    EFI_UINTN map_size = 0;
    EFI_UINTN map_key = 0;
    EFI_UINTN descriptor_size = 0;
    EFI_UINT32 descriptor_version = 0;
    EFI_BOOT_SERVICES *boot_services = NULL;
    EFI_UINT8 *kernel_destination = NULL;
    typedef void (*KernelEntry)(const AstraBootInfo *);

    if (system_table == NULL || system_table->BootServices == NULL ||
        system_table->ConOut == NULL || system_table->ConOut->OutputString == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    g_active_console = system_table->ConOut;
    loader_out(L"\r\nAstraOS UEFI loader 0.2\r\n");

    boot_services = system_table->BootServices;
    if (boot_services->HandleProtocol == NULL || boot_services->AllocatePages == NULL ||
        boot_services->GetMemoryMap == NULL || boot_services->ExitBootServices == NULL) {
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
        EFI_ALLOCATE_ADDRESS,
        EFI_LOADER_CODE,
        page_count,
        &kernel_address
    );
    if (status != EFI_SUCCESS || kernel_address != KERNEL_LOAD_BASE) {
        loader_out(L"error: kernel load address could not be reserved\r\n");
        return status == EFI_SUCCESS ? EFI_OUT_OF_RESOURCES : status;
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
    g_boot_services_exited = true;

    /* No UEFI protocol or boot-service call is made after this point. */
    {
        KernelEntry entry = (KernelEntry)(uintptr_t)kernel_address;
        entry(&g_boot_info);
    }
    stop_forever();
    return EFI_ABORTED;
}
