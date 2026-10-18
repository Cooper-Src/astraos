#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
STAGING="$BUILD/staging"
EFI_APP="$BUILD/EFI/BOOT/BOOTAA64.EFI"

for tool in clang lld-link xorriso mkfs.vfat mmd mcopy dd; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "Missing required build tool: $tool" >&2
    exit 1
  }
done

rm -rf "$BUILD"
mkdir -p "$BUILD/EFI/BOOT" "$STAGING/EFI/BOOT"

clang \
  --target=aarch64-pc-windows-msvc \
  -fshort-wchar -ffreestanding -fno-stack-protector -fno-builtin \
  -fno-exceptions -Wall -Wextra -Werror \
  -c "$ROOT/src/main.c" -o "$BUILD/main.obj"

lld-link \
  /subsystem:efi_application /entry:efi_main /machine:arm64 /nodefaultlib \
  /out:"$EFI_APP" "$BUILD/main.obj"

file "$EFI_APP" | grep -q 'PE32+ executable for EFI (application)' || {
  echo "The compiler output is not an AArch64 EFI application." >&2
  file "$EFI_APP" >&2
  exit 1
}
file "$EFI_APP" | grep -Eiq 'ARM64|AArch64' || {
  echo "The EFI application is not AArch64." >&2
  file "$EFI_APP" >&2
  exit 1
}

# FAT EFI System Partition image for UEFI removable-media boot.
dd if=/dev/zero of="$BUILD/efiboot.img" bs=1M count=64 status=none
mkfs.vfat -F 32 -n ASTRAOS "$BUILD/efiboot.img" >/dev/null
mmd -i "$BUILD/efiboot.img" ::/EFI
mmd -i "$BUILD/efiboot.img" ::/EFI/BOOT
mcopy -i "$BUILD/efiboot.img" "$EFI_APP" ::/EFI/BOOT/BOOTAA64.EFI

cp "$EFI_APP" "$STAGING/EFI/BOOT/BOOTAA64.EFI"
cp "$BUILD/efiboot.img" "$STAGING/efiboot.img"
cp "$ROOT/README.md" "$STAGING/README.md"

# UEFI El Torito boot image; xorriso also leaves the app visible in the ISO tree.
(
  cd "$STAGING"
  xorriso -as mkisofs -r -J -V ASTRAOS \
    -o "$BUILD/astraos-aarch64.iso" \
    -e efiboot.img -no-emul-boot \
    -append_partition 2 0xef efiboot.img .
)

cp "$BUILD/efiboot.img" "$BUILD/astraos-aarch64-esp.img"
sha256sum "$BUILD/astraos-aarch64.iso" "$BUILD/astraos-aarch64-esp.img" > "$BUILD/SHA256SUMS"
echo "Built: $BUILD/astraos-aarch64.iso"
echo "Built: $BUILD/astraos-aarch64-esp.img"
