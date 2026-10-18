# AstraOS

AstraOS is an experimental from-scratch ARM64 operating-system project. The current development target is **AArch64 UEFI on QEMU's virt machine**. The boot image contains a small UEFI loader and a separately linked freestanding kernel.

> **Alpha status:** the kernel can own the CPU after ExitBootServices and start its own UART shell and RAM filesystem. This is not yet a general-purpose OS. The current interactive driver is specifically the PL011 UART at QEMU virt's 0x09000000 base. There is no framebuffer, USB/HID keyboard, network driver, storage driver, scheduler, userspace process isolation, or persistent filesystem yet. On most real ARM computers—including Snapdragon X-series laptops—the current kernel will not yet provide a usable screen and keyboard session. Do not install it over your existing OS or use it to protect data.

## Current architecture

- EFI/BOOT/BOOTAA64.EFI: AArch64 PE/COFF UEFI loader.
- kernel.bin: flat freestanding AArch64 kernel image linked at 0x40200000.
- kernel.elf: ELF kernel build used for symbol checks and debugging.
- UEFI loader reads kernel.bin from the FAT ESP, reserves a fixed 2 MiB load region, copies/zeros the kernel memory, captures the UEFI memory map, calls ExitBootServices, and jumps to the kernel entry.
- Kernel entry installs its own stack and masks interrupts. The kernel currently owns a polling PL011 console, a bounded command loop, and a fixed-size RAM VFS.
- VFS uses caller-owned/static storage only: no heap and no hosted C runtime. Paths are bounded byte spans, file reads/writes are size-checked, and owner/mode bits plus sticky-directory behavior are covered by sanitizer tests.

## Build

On Debian/Ubuntu, install the dependencies:

    sudo apt update
    sudo apt install clang lld xorriso dosfstools mtools qemu-system-arm qemu-efi-aarch64 file

Build from the repository root:

    bash ./scripts/build.sh

The build uses strict compiler warnings and fails on undefined kernel symbols. Outputs are:

- build/astraos-aarch64.iso — UEFI bootable optical-disc image.
- build/astraos-aarch64-esp.img — FAT EFI system partition image.
- build/kernel.elf and build/kernel.bin — kernel debug and flat images.
- build/SHA256SUMS — SHA-256 checksums.

## Boot and interact in QEMU

With QEMU ARM64 and AAVMF firmware installed, run:

    CODE="$(find /usr/share -name AAVMF_CODE.fd -print -quit)"
    VARS_TEMPLATE="$(find /usr/share -name AAVMF_VARS.fd -print -quit)"
    cp "$VARS_TEMPLATE" /tmp/astraos-vars.fd
    qemu-system-aarch64 \
      -machine virt -cpu cortex-a72 -m 1024 \
      -drive if=pflash,format=raw,unit=0,readonly=on,file="$CODE" \
      -drive if=pflash,format=raw,unit=1,file=/tmp/astraos-vars.fd \
      -cdrom build/astraos-aarch64.iso -boot order=d \
      -display none -serial stdio -monitor none

The kernel shell appears on the serial terminal. Commands currently include:

    help
    about
    uname
    mem
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

For example, after boot, run ls /, cat /README.txt, mkdir /tmp/demo, touch /tmp/demo/test, and write /tmp/demo/test hello. All file changes are lost on reboot. su astra is a development/demo identity switch, not a security boundary; authentication and process isolation do not exist yet.

## Security and hardware limits

The kernel is freestanding, but it is not secure for multi-user use. UID and mode checks are enforced within one kernel address space and can be bypassed by kernel code. There is no virtual memory isolation, syscall boundary, scheduler, privilege-separated userspace, capability model, or password authentication.

The serial driver is only intended for QEMU virt. Real hardware requires platform support: boot-time ACPI/device-tree discovery, GOP/framebuffer output, input-controller drivers, clock/interrupt handling, and device-specific memory mappings. Booting a UEFI image on an ARM64 device is not evidence that the device has a working display, keyboard, USB, network, or storage path.

## Roadmap

1. Harden the loader/kernel handoff and add a kernel memory-map parser plus physical-page allocator.
2. Add an EFI GOP framebuffer console and boot-time graphics metadata so hardware can show kernel output after boot services end.
3. Add architecture exceptions, interrupt controller/timer setup, then a scheduler and user/kernel isolation.
4. Implement storage discovery and a persistent on-disk filesystem with tested recovery/error handling.
5. Add USB host-controller and HID support; only then consider Android USB tethering (USB enumeration, CDC-ECM/NCM/RNDIS variants, and network stack work are required).
6. Add target-specific enablement for real devices and validate using serial logs and reproducible hardware tests.

## License

MIT; see LICENSE.
