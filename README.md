# AstraOS

AstraOS is an experimental from-scratch ARM64 operating-system project. The current development target is **AArch64 UEFI on QEMU's `virt` machine**. The boot image contains a small UEFI loader and a separately linked freestanding kernel.

> **Alpha status:** AstraOS now has its own kernel entry, UEFI `ExitBootServices` handoff, fixed-size RAM filesystem, physical page allocator, serial console, and an optional UEFI GOP framebuffer text console. It is not yet a general-purpose or secure multi-user OS. The interactive input driver still polls the PL011 UART at QEMU virt's `0x09000000` base; a framebuffer can draw text but does not provide keyboard input. Most physical ARM laptops and phones therefore still need device-specific input, interrupt, and platform support before they are usable. Do not install AstraOS over your existing OS or use it to protect data.

## Current architecture

- `EFI/BOOT/BOOTAA64.EFI`: AArch64 PE/COFF UEFI loader.
- `kernel.bin`: flat freestanding AArch64 kernel linked at `0x40200000` for stable symbols; the UEFI loader can load it at a different page-aligned physical address.
- `kernel.elf`: ELF kernel build for symbol inspection and debugging.
- UEFI loader reads `kernel.bin` from the FAT ESP, allocates a 2 MiB region at a firmware-selected physical address, copies/zeros kernel memory, captures the UEFI memory map and optional GOP framebuffer metadata, and captures the standard UEFI device-tree configuration table when present. It validates the FDT header, block ranges, and reservation map before calling `ExitBootServices`. The kernel entry computes its initial stack using PC-relative instructions so a page-aligned load away from the link address is supported.
- Kernel entry installs its own stack and masks interrupts. It owns a bounded shell, an in-memory VFS, a physical-page allocator, a polling PL011 serial driver, and a software-rendered text console for supported GOP framebuffer formats.
- The fixed-size VFS has bounded path/content interfaces and owner/mode checks. The early page allocator parses UEFI descriptors, tracks conventional 4 KiB pages with bitmaps, rejects overlapping memory ranges, reserves page zero, and currently manages physical memory below 4 GiB.
- `kernel/fdt.[ch]` validates FDT v17-compatible headers, size/offset arithmetic, non-overlapping blocks, and the reservation-map terminator. The kernel revalidates the passed blob and can safely read the root `model` property. Sanitizer tests cover malformed headers, block ranges, and property bounds.
- These checks still run in a single kernel address space. There is no userspace isolation, syscall boundary, scheduler, password authentication, or persistent storage.

## Build

On Debian/Ubuntu, install the dependencies:

    sudo apt update
    sudo apt install clang lld xorriso dosfstools mtools qemu-system-arm qemu-efi-aarch64 file

Build from the repository root:

    bash ./scripts/build.sh

The build uses strict compiler warnings and rejects undefined kernel symbols. CI also requires a QEMU boot with the kernel loaded away from its link address. Outputs are:

- `build/astraos-aarch64.iso` — UEFI-bootable optical-disc image.
- `build/astraos-aarch64-esp.img` — FAT EFI system partition image.
- `build/kernel.elf` and `build/kernel.bin` — kernel debug and flat images.
- `build/SHA256SUMS` — SHA-256 checksums.

## Boot and interact in QEMU

Install QEMU ARM64 and an AArch64 UEFI firmware package (for example, `qemu-efi-aarch64`). With a graphical display, run:

    CODE="$(find /usr/share -name AAVMF_CODE.fd -print -quit)"
    VARS_TEMPLATE="$(find /usr/share -name AAVMF_VARS.fd -print -quit)"
    cp "$VARS_TEMPLATE" /tmp/astraos-vars.fd
    qemu-system-aarch64 \
      -machine virt -cpu cortex-a72 -m 1024 \
      -device ramfb \
      -drive if=pflash,format=raw,unit=0,readonly=on,file="$CODE" \
      -drive if=pflash,format=raw,unit=1,file=/tmp/astraos-vars.fd \
      -cdrom build/astraos-aarch64.iso -boot order=d \
      -display default -serial stdio -monitor none

The kernel draws to the framebuffer when UEFI supplies a supported GOP mode and also mirrors text to serial. **Type commands in the terminal running QEMU**, because input still comes from the emulated PL011 UART; a framebuffer window is output only.

To use a headless serial-only session, replace `-display default` with `-display none`. Commands include:

    help
    about
    uname
    mem
    palloc
    pfree 0x40001000
    pwd
    ls [PATH]
    cd PATH
    cat PATH
    touch PATH
    mkdir PATH
    write PATH TEXT
    rm PATH
    chmod OCTAL PATH
    whoami
    id
    su astra
    echo TEXT
    clear
    halt

The `palloc` command reserves one 4 KiB physical page; `pfree HEX_ADDRESS` returns an allocated page. Use only addresses actually returned by `palloc`. File data and allocator state are reset on reboot. `su astra` is a development identity switch, not authentication.

## Security and hardware limits

The kernel is freestanding, but not hardened for multi-user use. UID and mode checks are enforced inside one address space and can be bypassed by kernel code. No process isolation, syscall boundary, scheduler, or credentials store exist yet.

The GOP console uses firmware-provided linear framebuffer metadata after `ExitBootServices`; it does not call firmware services while the kernel runs. Whether that framebuffer is safe to use depends on the firmware's handoff and memory mapping. The kernel can accept the FDT configuration table defined by UEFI, but it does not yet select among multiple vendor-packed DTBs or enumerate/initialize devices from the tree. The input and PL011 UART path remain QEMU-virt-specific. Real hardware needs boot-time ACPI/device-tree discovery, physical mapping validation, timers/interrupt controllers, native UART or USB HID input, and platform-specific drivers. A successful QEMU boot is not proof of a successful boot on a laptop or phone.

## Roadmap

1. Validate GOP framebuffer output and physical-page allocator handoff across UEFI configurations, then add a platform-neutral input layer.
2. Add exception vectors, a real timer/interrupt controller path, page-table management, and user/kernel isolation.
3. Add storage discovery and a persistent filesystem with crash/error tests.
4. Implement USB host-controller and HID support. Android USB tethering comes later and requires USB enumeration, CDC-ECM/NCM/RNDIS support, and a network stack.
5. Add target-specific enablement for physical ARM64 devices and keep reproducible hardware tests.

## License

MIT; see `LICENSE`.
