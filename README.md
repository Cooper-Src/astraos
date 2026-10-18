# AstraOS

AstraOS is an experimental ARM64 operating-system project. The current milestone is a bootable **AArch64 UEFI live shell**: its EFI application is compiled from the C source in this repository and launched directly by UEFI removable-media boot. The shell implements a small in-memory filesystem and Unix-style owner/mode checks without linking a hosted C runtime.

> **Alpha status:** this is a first bootable prototype, not yet a standalone general-purpose OS. UEFI currently handles keyboard and text-console I/O. The filesystem is RAM-only; the permission checks are enforced by the shell inside one address space and are not a security boundary. There is no scheduler, userspace process isolation, persistent disk filesystem, network stack, or general device-driver layer yet. Do not use it to protect data or as a replacement for your existing OS.

## Current features

- Native AArch64 PE/COFF EFI application at `EFI/BOOT/BOOTAA64.EFI`.
- UEFI bootable ISO and a raw FAT EFI System Partition image.
- AstraOS-owned command interpreter, RAM VFS, file and directory creation/removal, and octal file modes.
- Basic users `root` and `astra`; `su astra` switches to the unprivileged demo account. Returning to root is intentionally not implemented because authentication is not implemented.
- Automated CI build, QEMU UEFI startup smoke test, downloadable artifacts, and rolling development prerelease.

## Build locally

On Debian/Ubuntu, install the build dependencies:

```sh
sudo apt update
sudo apt install clang lld xorriso dosfstools mtools file
```

Then run from the repository root:

```sh
./scripts/build.sh
```

Outputs:

- `build/astraos-aarch64.iso` — UEFI optical-disc ISO.
- `build/astraos-aarch64-esp.img` — FAT EFI System Partition image, useful for UEFI USB boot workflows.
- `build/SHA256SUMS` — SHA-256 checksums.

The compiler must support `--target=aarch64-pc-windows-msvc`; the script checks that the result is an ARM64 EFI application before making the image.

## Boot in QEMU

Install `qemu-system-arm` and an AArch64 UEFI firmware package (often `qemu-efi-aarch64`). On Ubuntu the firmware files are commonly under `/usr/share/AAVMF/`. Use a writable copy of the firmware variable store:

```sh
CODE=/usr/share/AAVMF/AAVMF_CODE.fd
cp /usr/share/AAVMF/AAVMF_VARS.fd /tmp/astraos-vars.fd
qemu-system-aarch64 \
  -machine virt -cpu cortex-a72 -m 1024 \
  -drive if=pflash,format=raw,readonly=on,file="$CODE" \
  -drive if=pflash,format=raw,file=/tmp/astraos-vars.fd \
  -cdrom build/astraos-aarch64.iso -boot order=d \
  -display none -serial stdio -monitor none
```

Firmware package paths differ by distribution; adjust `CODE` and the variable-store path as needed. The console shell stays running until `exit` is entered.

## Try the shell

```text
help
ls
cat README.txt
mkdir /home/astra/demo
cd /home/astra/demo
touch hello.txt
write hello.txt hello from AstraOS
cat hello.txt
chmod 600 hello.txt
su astra
whoami
```

The RAM filesystem resets on every reboot. This is intentional for the initial live prototype.

## Real hardware notes

The ISO targets **ARM64 UEFI firmware with removable-media boot enabled**. That does not mean it will boot on every ARM64 computer or phone. Secure Boot policy, vendor firmware, ACPI/device-tree handoff, display and input interfaces vary by device. The CI smoke test targets QEMU's `virt` machine; successful QEMU boot is not proof that drivers exist for a particular physical device. Qualcomm Snapdragon X-series systems may require platform-specific enablement before they are usable.

## Roadmap

1. Replace the firmware-hosted shell with a separately linked kernel entry point and explicit boot handoff.
2. Add a boot information contract (memory map, framebuffer, ACPI/device tree) and physical page allocator.
3. Build kernel-owned serial/framebuffer drivers and interrupt handling.
4. Add a VFS interface and persistent filesystem driver.
5. Add user/kernel privilege separation, syscall boundary, process scheduler, and a real userspace init/shell.
6. Expand hardware support and add reproducible real-device testing.

## License

MIT; see [LICENSE](LICENSE).
