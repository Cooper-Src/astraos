# AstraOS

AstraOS is an experimental from-scratch ARM64 operating-system project. The current development target is **AArch64 UEFI on QEMU's `virt` machine**. The boot image contains a small UEFI loader and a separately linked freestanding kernel.

> **Alpha status:** AstraOS has its own AArch64 kernel entry, UEFI `ExitBootServices` handoff, aligned EL1/EL2 exception vectors with fatal exception diagnostics, fixed-size RAM filesystem, physical page allocator, serial console, and optional UEFI GOP framebuffer text console. It is not yet a general-purpose or secure multi-user OS. Serial discovery prefers a validated FDT `/chosen/stdout-path` and `/aliases`; if no supported FDT PL011 is found, the kernel validates the UEFI ACPI RSDP, root-table and SPCR checksums and accepts only a memory-mapped ARM PL011 description. The ACPI SPCR path preserves the firmware-configured baud divisor because SPCR does not describe the UART input clock. Other UART types and nested FDT bus `ranges` are not supported. The generic live image includes a UEFI keyboard compatibility path: when `keyboard-console.cfg` is present, the loader passes UEFI Simple Text Input and `WaitForEvent` to the kernel and deliberately retains Boot Services so firmware can keep handling keyboard events. This depends on working firmware text input and is not a native USB/HID driver. The compatibility path disables AstraOS's physical page allocator while firmware remains active, so it has less memory-management functionality than the normal serial path. Images without a supported input source still fall back to display-only diagnostics. Native USB/HID, timers, interrupt controllers, persistent storage and many device drivers remain incomplete. Do not install AstraOS over your existing OS or use it to protect data.

## Current architecture

- `EFI/BOOT/BOOTAA64.EFI`: AArch64 PE/COFF UEFI loader.
- `kernel.bin`: flat freestanding AArch64 kernel linked at `0x40200000` for stable symbols; the UEFI loader can load it at a different page-aligned physical address.
- `kernel.elf`: ELF kernel build for symbol inspection and debugging.
- UEFI loader reads `kernel.bin` from the FAT ESP, allocates a 2 MiB region at a firmware-selected physical address, copies/zeros kernel memory, captures the UEFI memory map and optional GOP framebuffer metadata, and captures the standard UEFI device-tree configuration table when present. It validates the FDT header, block ranges, and reservation map before calling `ExitBootServices`. The loader also hands off the ACPI RSDP pointer from the UEFI configuration table; the kernel validates RSDP, XSDT/RSDT and SPCR checksums against ACPI-reclaim/NVS ranges in the final memory map. The kernel entry computes its initial stack using PC-relative instructions so a page-aligned load away from the link address is supported.
- Kernel entry masks interrupts, explicitly selects `SP_ELx`, installs its own stack, and points VBAR_EL1 or VBAR_EL2 at a 2 KiB-aligned exception table. FDT- and ACPI-described PL011 spans are rejected if they overlap RAM or a conflicting memory-map descriptor; when firmware does provide a descriptor, it must be MMIO/reserved and cover the full span. Some firmware omits device windows from the map, so a non-overlapping FDT/ACPI-described window is also permitted. Fatal exceptions report the vector slot, CurrentEL, ESR class, saved exception PC, fault address, and stack pointer before halting. The kernel owns a bounded shell, an in-memory VFS, a physical-page allocator, a polling PL011 driver discovered from validated FDT metadata or ACPI SPCR, and a software-rendered text console for supported GOP framebuffer formats. On the ACPI path, it preserves firmware-programmed baud divisors instead of guessing the UART clock.
- The fixed-size VFS has bounded path/content interfaces and owner/mode checks. The early page allocator parses UEFI descriptors, tracks conventional 4 KiB pages with bitmaps, rejects overlapping memory ranges, reserves page zero, and currently manages physical memory below 4 GiB.
- `kernel/fdt.[ch]` validates FDT v17-compatible headers, size/offset arithmetic, non-overlapping blocks, and the reservation-map terminator. The kernel revalidates the passed blob and can safely read the root `model` property. Sanitizer tests cover malformed headers, block ranges, and property bounds. CI also runs a bounded libFuzzer session with AddressSanitizer and UndefinedBehaviorSanitizer using a valid device-tree seed fixture.
- If firmware does not publish the standard DTB configuration table, the loader accepts an optional board-matched `platform.dtb` at the root of the EFI System Partition (maximum file size 1 MiB). It validates the blob before copying it to resident EFI loader memory. The `platform` command reports the root model/compatible strings and `/chosen/stdout-path`; reporting a UART path does not mean that UART driver is implemented. The DTB must match the exact device and board.
- These checks still run in a single kernel address space. There is no userspace isolation, syscall boundary, scheduler, password authentication, or persistent storage.

## Build

On Debian/Ubuntu, install the dependencies:

    sudo apt update
    sudo apt install clang llvm lld xorriso dosfstools mtools qemu-system-arm qemu-efi-aarch64 file python3

Build from the repository root:

    bash ./scripts/build.sh

The build runs the VFS, FDT, physical-memory, MMIO, ACPI, platform, and framebuffer test suites with AddressSanitizer and UndefinedBehaviorSanitizer, then runs a bounded libFuzzer session against the FDT parser. The kernel and UEFI loader compile with strict C17/freestanding warnings treated as errors. CI also requires a QEMU UEFI boot with the kernel loaded away from its link address. Outputs are:

- `build/astraos-aarch64.iso` — UEFI-bootable optical-disc image.
- `build/astraos-aarch64-esp.img` — generic FAT EFI system partition image.
- `build/astraos-qemu-virt.iso` — QEMU `virt` image with a test device tree and the normal PL011/ExitBootServices path.
- `build/astraos-qemu-virt-esp.img` — matching QEMU serial-test ESP image.
- `build/astraos-qemu-keyboard.iso` — QEMU `virt` image with a test device tree and the UEFI keyboard compatibility path.
- `build/astraos-qemu-keyboard-esp.img` — matching QEMU keyboard-test ESP image.
- `build/kernel.elf` and `build/kernel.bin` — kernel debug and flat images.
- `build/SHA256SUMS` — portable SHA-256 checksums. Verify them with `cd build && sha256sum -c SHA256SUMS`.

## Boot and interact in QEMU

Install QEMU ARM64 and an AArch64 UEFI firmware package (for example, `qemu-efi-aarch64`). Use `astraos-qemu-virt.iso` for interactive QEMU testing; it includes a test-only DTB that identifies QEMU's PL011 UART. The generic image omits that fixture and prefers the device tree or ACPI description provided by the target firmware. With a graphical display, run:

    CODE="$(find /usr/share -name AAVMF_CODE.fd -print -quit)"
    VARS_TEMPLATE="$(find /usr/share -name AAVMF_VARS.fd -print -quit)"
    cp "$VARS_TEMPLATE" /tmp/astraos-vars.fd
    qemu-system-aarch64 \
      -machine virt -cpu cortex-a72 -m 1024 \
      -device ramfb \
      -drive if=pflash,format=raw,unit=0,readonly=on,file="$CODE" \
      -drive if=pflash,format=raw,unit=1,file=/tmp/astraos-vars.fd \
      -cdrom build/astraos-qemu-virt.iso -boot order=d \
      -display default -serial stdio -monitor none

The kernel draws to the framebuffer when UEFI supplies a supported GOP mode and also mirrors text to serial. In this image, **type commands in the terminal running QEMU**, because input comes from the emulated PL011 UART.

To test the UEFI keyboard compatibility path, use `build/astraos-qemu-keyboard.iso` with a graphical QEMU display and a USB keyboard. The loader reads `keyboard-console.cfg` from the EFI partition to opt in to firmware input. This lets UEFI continue servicing keyboard events after the kernel starts; it is a compatibility layer, not native USB/HID support. If firmware does not expose working text input, the kernel will still fall back to display-only diagnostics.

To use a headless serial-only session, replace `-display default` with `-display none`. Commands include:

    help
    about
    uname
    platform
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
    exception-test

`exception-test` intentionally executes an AArch64 `BRK` instruction to verify the fatal exception report, then halts that boot session. The `palloc` command reserves one 4 KiB physical page; `pfree HEX_ADDRESS` returns an allocated page. Use only addresses actually returned by `palloc`. File data and allocator state are reset on reboot. `su astra` is a development identity switch, not authentication.

## Security and hardware limits

The kernel is freestanding, but not hardened for multi-user use. UID and mode checks are enforced inside one address space and can be bypassed by kernel code. No process isolation, syscall boundary, scheduler, or credentials store exist yet.

The normal serial image uses GOP framebuffer metadata after `ExitBootServices` and does not call firmware services while the kernel runs. The opt-in keyboard image is different: it deliberately retains UEFI Boot Services and temporarily restores firmware exception handling while waiting for keys. Its page allocator is disabled because the firmware still owns conventional memory. The `platform` command reports validated device-tree properties and a supported ACPI SPCR PL011 base when one is present. Native USB/HID, timers, interrupt controllers, memory-mapping work and platform-specific drivers are still needed for a self-contained OS input stack. A successful QEMU boot is not proof of a successful boot on a laptop or phone.

## Roadmap

1. Validate GOP framebuffer output and the UEFI keyboard fallback across firmware implementations; then connect it to a platform-neutral input layer for native drivers.
2. Exception vectors and fatal diagnostics are implemented and QEMU-tested; add a real timer/interrupt controller path, page-table management, and user/kernel isolation.
3. Add storage discovery and a persistent filesystem with crash/error tests.
4. Implement USB host-controller and HID support. Android USB tethering comes later and requires USB enumeration, CDC-ECM/NCM/RNDIS support, and a network stack.
5. Add target-specific enablement for physical ARM64 devices and keep reproducible hardware tests.

## License

MIT; see `LICENSE`.
