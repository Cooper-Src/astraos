#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
STAGING="$BUILD/staging"
EFI_APP="$BUILD/EFI/BOOT/BOOTAA64.EFI"
WARN_FLAGS=(
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wconversion
  -Wformat=2
  -Wstrict-prototypes
  -Wmissing-prototypes
  -Wwrite-strings
  -Wcast-qual
  -Wundef
  -Wvla
  -Werror
)

for tool in clang lld-link ld.lld nm xorriso mkfs.vfat mmd mcopy dd file sha256sum stat; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Missing required build tool: $tool" >&2
    exit 1
  fi
done

rm -rf "$BUILD"
mkdir -p "$BUILD/EFI/BOOT" "$STAGING/EFI/BOOT" "$BUILD/objects"

echo "[1/5] Compile the UEFI kernel loader"
clang \
  --target=aarch64-pc-windows-msvc \
  -std=c17 -fshort-wchar -ffreestanding -fno-builtin \
  -fno-stack-protector -fno-exceptions \
  -fno-unwind-tables -fno-asynchronous-unwind-tables \
  "${WARN_FLAGS[@]}" \
  -c "$ROOT/src/boot/efi_main.c" -o "$BUILD/objects/efi_main.obj"

lld-link \
  /subsystem:efi_application /entry:efi_main /machine:arm64 /nodefaultlib \
  /out:"$EFI_APP" "$BUILD/objects/efi_main.obj"

EFI_DESCRIPTION="$(file "$EFI_APP")"
printf '%s\n' "$EFI_DESCRIPTION"
if ! grep -Eiq 'PE32\+ executable.*EFI.*application|PE32\+ executable.*application.*EFI' \
    <<< "$EFI_DESCRIPTION"; then
  echo "The loader output is not a PE/COFF EFI application." >&2
  exit 1
fi
if ! grep -Eiq 'ARM64|AArch64' <<< "$EFI_DESCRIPTION"; then
  echo "The loader output is not an AArch64 EFI application." >&2
  exit 1
fi

echo "[2/5] Compile the freestanding AArch64 kernel"
KERNEL_FLAGS=(
  --target=aarch64-none-elf
  -std=c17
  -O2
  -ffreestanding
  -fno-builtin
  -fno-stack-protector
  -fno-pic
  -fno-pie
  -fno-unwind-tables
  -fno-asynchronous-unwind-tables
  -mgeneral-regs-only
  -mcpu=cortex-a72
  -Ikernel
  "${WARN_FLAGS[@]}"
)

clang "${KERNEL_FLAGS[@]}" -c "$ROOT/kernel/main.c" -o "$BUILD/objects/kernel_main.o"
clang "${KERNEL_FLAGS[@]}" -c "$ROOT/kernel/vfs.c" -o "$BUILD/objects/vfs.o"
clang "${KERNEL_FLAGS[@]}" -c "$ROOT/kernel/memory.c" -o "$BUILD/objects/memory.o"
clang "${KERNEL_FLAGS[@]}" -c "$ROOT/kernel/uart.c" -o "$BUILD/objects/uart.o"
clang "${KERNEL_FLAGS[@]}" -c "$ROOT/kernel/framebuffer.c" -o "$BUILD/objects/framebuffer.o"
clang --target=aarch64-none-elf -ffreestanding -fno-pic -fno-pie \
  -mcpu=cortex-a72 -c "$ROOT/kernel/entry.S" -o "$BUILD/objects/entry.o"

KERNEL_OBJECTS=(
  "$BUILD/objects/entry.o"
  "$BUILD/objects/kernel_main.o"
  "$BUILD/objects/vfs.o"
  "$BUILD/objects/memory.o"
  "$BUILD/objects/uart.o"
  "$BUILD/objects/framebuffer.o"
)

ld.lld -T "$ROOT/kernel/linker.ld" -o "$BUILD/kernel.elf" "${KERNEL_OBJECTS[@]}"
if nm -u "$BUILD/kernel.elf" | grep -q .; then
  echo "Kernel has undefined symbols; a freestanding image must not need a hosted runtime." >&2
  nm -u "$BUILD/kernel.elf" >&2
  exit 1
fi
if ! file "$BUILD/kernel.elf" | grep -Eiq 'ARM aarch64|AArch64'; then
  echo "Kernel ELF is not AArch64." >&2
  file "$BUILD/kernel.elf" >&2
  exit 1
fi

# Emit a flat image that the UEFI loader copies to the linker-script load address.
ld.lld -T "$ROOT/kernel/linker.ld" --oformat=binary \
  -o "$BUILD/kernel.bin" "${KERNEL_OBJECTS[@]}"
KERNEL_BYTES="$(stat -c '%s' "$BUILD/kernel.bin")"
if [[ "$KERNEL_BYTES" -eq 0 || "$KERNEL_BYTES" -gt 1048576 ]]; then
  echo "Kernel flat image size is invalid or exceeds the 1 MiB loader limit: $KERNEL_BYTES" >&2
  exit 1
fi
echo "Kernel ELF: $BUILD/kernel.elf"
echo "Kernel flat image: $KERNEL_BYTES bytes"

echo "[3/5] Build the FAT EFI System Partition"
dd if=/dev/zero of="$BUILD/efiboot.img" bs=1M count=64 status=none
mkfs.vfat -F 32 -n ASTRAOS "$BUILD/efiboot.img" >/dev/null
mmd -i "$BUILD/efiboot.img" ::/EFI
mmd -i "$BUILD/efiboot.img" ::/EFI/BOOT
mcopy -i "$BUILD/efiboot.img" "$EFI_APP" ::/EFI/BOOT/BOOTAA64.EFI
mcopy -i "$BUILD/efiboot.img" "$BUILD/kernel.bin" ::/kernel.bin

cp "$EFI_APP" "$STAGING/EFI/BOOT/BOOTAA64.EFI"
cp "$BUILD/efiboot.img" "$STAGING/efiboot.img"
cp "$BUILD/kernel.bin" "$STAGING/kernel.bin"
cp "$ROOT/README.md" "$STAGING/README.md"

echo "[4/5] Create a UEFI-bootable ARM64 ISO"
(
  cd "$STAGING"
  xorriso -as mkisofs -r -J -V ASTRAOS \
    -o "$BUILD/astraos-aarch64.iso" \
    -e efiboot.img -no-emul-boot \
    -append_partition 2 0xef efiboot.img .
)

cp "$BUILD/efiboot.img" "$BUILD/astraos-aarch64-esp.img"
echo "[5/5] Generate checksums"
sha256sum \
  "$BUILD/astraos-aarch64.iso" \
  "$BUILD/astraos-aarch64-esp.img" \
  "$BUILD/kernel.elf" \
  "$BUILD/kernel.bin" > "$BUILD/SHA256SUMS"

echo "Built: $BUILD/astraos-aarch64.iso"
echo "Built: $BUILD/astraos-aarch64-esp.img"
echo "Built: $BUILD/kernel.elf"
echo "Built: $BUILD/kernel.bin"
