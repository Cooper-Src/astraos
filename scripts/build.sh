#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
ISO_ROOT="$BUILD/iso-root"
OBJ_DIR="$BUILD/obj"
LIMINE_VERSION="12.9.3"
LIMINE_SHA256="9f42fe9ea2e84d71056529969b1d5c24ba9a2287ffcc01bcce58b0ef8aa98b1d"
LIMINE_URL="https://github.com/Limine-Bootloader/Limine/releases/download/v${LIMINE_VERSION}/limine-binary.tar.gz"

for tool in clang ld.lld xorriso mkfs.vfat mmd mcopy dd curl tar sha256sum file; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing required build tool: $tool" >&2
        exit 1
    fi
done

rm -rf "$BUILD"
mkdir -p "$OBJ_DIR" "$BUILD/limine" "$ISO_ROOT/boot/limine" "$ISO_ROOT/EFI/BOOT"

echo "==> Testing RAM filesystem with ASan and UBSan"
cc -std=c17 -g -O0 \
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wformat=2 \
    -Wstrict-prototypes -Wmissing-prototypes -Wwrite-strings -Wcast-qual -Wundef -Wvla -Werror \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
    -I"$ROOT/include" \
    "$ROOT/tests/vfs_test.c" "$ROOT/src/kernel/vfs.c" \
    -o "$BUILD/vfs_test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 "$BUILD/vfs_test"

KERNEL_FLAGS=(
    --target=aarch64-none-elf
    -std=c17 -O2 -g
    -ffreestanding -fno-builtin -fno-stack-protector
    -fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables
    -ffunction-sections -fdata-sections -mgeneral-regs-only -mcmodel=large
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wformat=2
    -Wstrict-prototypes -Wmissing-prototypes -Wwrite-strings -Wcast-qual -Wundef -Wvla -Werror
    -I"$ROOT/include" -I"$ROOT/third_party/limine" -I"$ROOT/third_party" -I"$ROOT/src/kernel"
)

SOURCES=(
    "$ROOT/src/main.c"
    "$ROOT/src/kernel/console.c"
    "$ROOT/src/kernel/vfs.c"
    "$ROOT/src/kernel/shell.c"
)
OBJECTS=()
for source in "${SOURCES[@]}"; do
    object="$OBJ_DIR/$(basename "${source%.c}").o"
    echo "==> Compiling ${source#"$ROOT/"}"
    clang "${KERNEL_FLAGS[@]}" -c "$source" -o "$object"
    OBJECTS+=("$object")
done

echo "==> Linking freestanding AArch64 kernel"
ld.lld -m aarch64elf -T "$ROOT/linker.ld" --gc-sections -nostdlib \
    -o "$BUILD/kernel.elf" "${OBJECTS[@]}"
file "$BUILD/kernel.elf" | tee "$BUILD/kernel-file.txt"
grep -Eiq 'ELF 64-bit.*ARM aarch64|ELF 64-bit.*AArch64' "$BUILD/kernel-file.txt" || {
    echo "Kernel output is not an AArch64 ELF executable." >&2
    exit 1
}

echo "==> Downloading pinned Limine UEFI bootloader"
curl -fsSL --retry 3 "$LIMINE_URL" -o "$BUILD/limine-binary.tar.gz"
printf '%s  %s\n' "$LIMINE_SHA256" "$BUILD/limine-binary.tar.gz" | sha256sum -c -
tar -xzf "$BUILD/limine-binary.tar.gz" -C "$BUILD/limine"

LIMINE_EFI="$(find "$BUILD/limine" -name BOOTAA64.EFI -type f -print -quit)"
LIMINE_CD="$(find "$BUILD/limine" -name limine-uefi-cd.bin -type f -print -quit)"
if [[ -z "$LIMINE_EFI" || -z "$LIMINE_CD" ]]; then
    echo "Pinned Limine archive is missing its AArch64 UEFI boot files." >&2
    exit 1
fi

cp "$BUILD/kernel.elf" "$ISO_ROOT/boot/kernel.elf"
cp "$ROOT/limine.conf" "$ISO_ROOT/limine.conf"
cp "$LIMINE_CD" "$ISO_ROOT/boot/limine/limine-uefi-cd.bin"
cp "$LIMINE_EFI" "$ISO_ROOT/EFI/BOOT/BOOTAA64.EFI"

echo "==> Creating hybrid UEFI ARM64 ISO"
xorriso -as mkisofs -R -r -J -V ASTRAOS \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    -efi-boot-part --efi-boot-image --protective-msdos-label \
    "$ISO_ROOT" -o "$BUILD/astraos-aarch64.iso"

echo "==> Creating a raw FAT EFI system partition image"
dd if=/dev/zero of="$BUILD/astraos-aarch64-esp.img" bs=1M count=64 status=none
mkfs.vfat -F 32 -n ASTRAOS "$BUILD/astraos-aarch64-esp.img" >/dev/null
mmd -i "$BUILD/astraos-aarch64-esp.img" ::/EFI
mmd -i "$BUILD/astraos-aarch64-esp.img" ::/EFI/BOOT
mmd -i "$BUILD/astraos-aarch64-esp.img" ::/boot
mcopy -i "$BUILD/astraos-aarch64-esp.img" "$LIMINE_EFI" ::/EFI/BOOT/BOOTAA64.EFI
mcopy -i "$BUILD/astraos-aarch64-esp.img" "$BUILD/kernel.elf" ::/boot/kernel.elf
mcopy -i "$BUILD/astraos-aarch64-esp.img" "$ROOT/limine.conf" ::/limine.conf

(
    cd "$BUILD"
    sha256sum astraos-aarch64.iso astraos-aarch64-esp.img > SHA256SUMS
)

echo "Built: $BUILD/astraos-aarch64.iso"
echo "Built: $BUILD/astraos-aarch64-esp.img"
echo "Checksums: $BUILD/SHA256SUMS"
