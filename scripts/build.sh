#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
OBJ_DIR="$BUILD/obj"
TEST_DIR="$BUILD/tests"
EFI_APP="$BUILD/EFI/BOOT/BOOTAA64.EFI"
HOST_CC="${CC:-cc}"

for tool in "$HOST_CC" clang ld.lld lld-link llvm-objcopy xorriso mkfs.vfat mmd mcopy dd file sha256sum python3 timeout; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing required build tool: $tool" >&2
        exit 1
    fi
done

rm -rf "$BUILD"
mkdir -p "$OBJ_DIR/kernel" "$OBJ_DIR/boot" "$TEST_DIR" "$BUILD/EFI/BOOT"

HOST_FLAGS=(
    -std=c17 -g -O0
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wformat=2
    -Wstrict-prototypes -Wmissing-prototypes -Wwrite-strings -Wcast-qual -Wundef -Wvla -Werror
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
)

run_host_test() {
    local name="$1"
    shift
    echo "==> Running sanitized ${name} tests"
    "$HOST_CC" "${HOST_FLAGS[@]}" -I"$ROOT/kernel" "$@" -o "$TEST_DIR/$name"
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 "$TEST_DIR/$name"
}

run_host_test vfs_tests \
    "$ROOT/tests/vfs_tests.c" "$ROOT/kernel/vfs.c"
run_host_test fdt_tests \
    "$ROOT/tests/fdt_tests.c" "$ROOT/kernel/fdt.c"
run_host_test memory_tests \
    "$ROOT/tests/memory_tests.c" "$ROOT/kernel/memory.c"
run_host_test mmio_tests \
    "$ROOT/tests/mmio_tests.c" "$ROOT/kernel/mmio.c"
run_host_test acpi_tests \
    "$ROOT/tests/acpi_tests.c" "$ROOT/kernel/acpi.c" "$ROOT/kernel/mmio.c"
run_host_test platform_tests \
    "$ROOT/tests/platform_tests.c" "$ROOT/kernel/platform.c" \
    "$ROOT/kernel/fdt.c" "$ROOT/kernel/acpi.c" "$ROOT/kernel/mmio.c"
run_host_test input_tests \
    "$ROOT/tests/input_tests.c" "$ROOT/kernel/input.c"
run_host_test framebuffer_tests \
    "$ROOT/tests/framebuffer_tests.c" "$ROOT/kernel/framebuffer.c"

echo "==> Fuzzing FDT parser with ASan and UBSan"
clang -std=c17 -g -O1 -Wall -Wextra -Wpedantic -Werror \
    -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined \
    -fno-omit-frame-pointer -I"$ROOT/kernel" \
    "$ROOT/tests/fuzz_fdt.c" "$ROOT/kernel/fdt.c" -o "$TEST_DIR/fuzz_fdt"
mkdir -p "$BUILD/fdt-corpus"
python3 "$ROOT/tests/make_fdt_fixture.py" "$BUILD/fdt-corpus/seed.dtb"
"$TEST_DIR/fuzz_fdt" -max_total_time=3 -max_len=65536 -print_final_stats=1 \
    "$BUILD/fdt-corpus" >"$BUILD/fuzz-fdt.log" 2>&1 || {
        cat "$BUILD/fuzz-fdt.log" >&2
        exit 1
    }
tail -n 12 "$BUILD/fuzz-fdt.log"

KERNEL_FLAGS=(
    --target=aarch64-none-elf
    -std=c17 -O2 -g
    -ffreestanding -fno-builtin -fno-stack-protector
    -fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables
    -ffunction-sections -fdata-sections -mgeneral-regs-only -mcmodel=small
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wformat=2
    -Wstrict-prototypes -Wmissing-prototypes -Wwrite-strings -Wcast-qual -Wundef -Wvla -Werror
    -I"$ROOT/kernel"
)

KERNEL_SOURCES=(
    "$ROOT/kernel/main.c"
    "$ROOT/kernel/display.c"
    "$ROOT/kernel/exceptions.c"
    "$ROOT/kernel/fdt.c"
    "$ROOT/kernel/acpi.c"
    "$ROOT/kernel/framebuffer.c"
    "$ROOT/kernel/memory.c"
    "$ROOT/kernel/mmio.c"
    "$ROOT/kernel/platform.c"
    "$ROOT/kernel/input.c"
    "$ROOT/kernel/uart.c"
    "$ROOT/kernel/vfs.c"
)
KERNEL_OBJECTS=()
for source in "${KERNEL_SOURCES[@]}"; do
    object="$OBJ_DIR/kernel/$(basename "${source%.c}").o"
    echo "==> Compiling ${source#"$ROOT/"}"
    clang "${KERNEL_FLAGS[@]}" -c "$source" -o "$object"
    KERNEL_OBJECTS+=("$object")
done

clang --target=aarch64-none-elf -g -ffreestanding -c \
    "$ROOT/kernel/entry.S" -o "$OBJ_DIR/kernel/entry_asm.o"
clang --target=aarch64-none-elf -g -ffreestanding -c \
    "$ROOT/kernel/exceptions.S" -o "$OBJ_DIR/kernel/exceptions_asm.o"
KERNEL_OBJECTS+=("$OBJ_DIR/kernel/entry_asm.o" "$OBJ_DIR/kernel/exceptions_asm.o")

echo "==> Linking freestanding AArch64 kernel"
ld.lld -m aarch64elf -T "$ROOT/kernel/linker.ld" --gc-sections \
    --no-undefined --fatal-warnings -nostdlib \
    -o "$BUILD/kernel.elf" "${KERNEL_OBJECTS[@]}"
llvm-objcopy -O binary "$BUILD/kernel.elf" "$BUILD/kernel.bin"
file "$BUILD/kernel.elf" | tee "$BUILD/kernel-file.txt"
grep -Eiq 'ELF 64-bit.*(ARM aarch64|AArch64)' "$BUILD/kernel-file.txt" || {
    echo "Kernel output is not an AArch64 ELF executable." >&2
    exit 1
}
test -s "$BUILD/kernel.bin"
test "$(stat -c '%s' "$BUILD/kernel.bin")" -le 1048576 || {
    echo "Flat kernel image exceeds the loader's 1 MiB staging buffer." >&2
    exit 1
}

EFI_FLAGS=(
    --target=aarch64-pc-windows-msvc
    -std=c17 -O2 -g
    -ffreestanding -fshort-wchar -fno-builtin -fno-stack-protector
    -fno-pic -fno-pie
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wformat=2
    -Wstrict-prototypes -Wmissing-prototypes -Wwrite-strings -Wcast-qual -Wundef -Wvla -Werror
    -I"$ROOT/kernel"
)

echo "==> Compiling native AArch64 UEFI loader"
clang "${EFI_FLAGS[@]}" -c "$ROOT/src/boot/efi_main.c" -o "$OBJ_DIR/boot/efi_main.obj"
clang "${EFI_FLAGS[@]}" -c "$ROOT/kernel/fdt.c" -o "$OBJ_DIR/boot/fdt.obj"
lld-link /subsystem:efi_application /entry:efi_main /machine:arm64 /nodefaultlib \
    /out:"$EFI_APP" "$OBJ_DIR/boot/efi_main.obj" "$OBJ_DIR/boot/fdt.obj"
file "$EFI_APP" | tee "$BUILD/efi-file.txt"
grep -Eiq 'PE32\+ executable.*(Aarch64|ARM64)' "$BUILD/efi-file.txt" || {
    echo "Loader output is not an AArch64 EFI application." >&2
    exit 1
}

create_esp() {
    local output="$1"
    local volume_label="$2"
    local dtb_path="$3"
    local console_mode="${4:-serial}"
    local desktop_mode="${5:-none}"

    dd if=/dev/zero of="$output" bs=1M count=64 status=none
    mkfs.vfat -F 32 -n "$volume_label" "$output" >/dev/null
    mmd -i "$output" ::/EFI
    mmd -i "$output" ::/EFI/BOOT
    mcopy -i "$output" "$EFI_APP" ::/EFI/BOOT/BOOTAA64.EFI
    mcopy -i "$output" "$BUILD/kernel.bin" ::/kernel.bin
    if [[ -n "$dtb_path" ]]; then
        mcopy -i "$output" "$dtb_path" ::/platform.dtb
    fi
    if [[ "$console_mode" == "uefi-keyboard" ]]; then
        mcopy -i "$output" "$BUILD/keyboard-console.cfg" ::/keyboard-console.cfg
    fi
    if [[ "$desktop_mode" == "desktop" ]]; then
        mcopy -i "$output" "$BUILD/desktop-mode.cfg" ::/desktop-mode.cfg
    fi
    mdir -i "$output" ::/ >/dev/null
}

create_iso() {
    local output="$1"
    local esp_image="$2"
    local volume_label="$3"
    local staging="$BUILD/stage-$(basename "${output%.iso}")"

    mkdir -p "$staging"
    cp "$esp_image" "$staging/efiboot.img"
    cp "$ROOT/README.md" "$staging/README.md"
    (
        cd "$staging"
        xorriso -as mkisofs -r -J -V "$volume_label" \
            -o "$output" \
            -e efiboot.img -no-emul-boot \
            -append_partition 2 0xef efiboot.img .
    )
}

printf '%s\n' 'ASTRAOS_UEFI_KEYBOARD=1' > "$BUILD/keyboard-console.cfg"
printf '%s\n' 'ASTRAOS_DESKTOP=1' > "$BUILD/desktop-mode.cfg"

echo "==> Creating generic ARM64 EFI System Partition"
create_esp "$BUILD/astraos-aarch64-esp.img" "ASTRAOS" "" "uefi-keyboard"

echo "==> Creating QEMU serial test device tree"
python3 "$ROOT/tests/make_fdt_fixture.py" "$BUILD/platform.dtb"

echo "==> Creating QEMU serial EFI System Partition"
create_esp "$BUILD/astraos-qemu-virt-esp.img" "ASTRAQEMU" "$BUILD/platform.dtb"

echo "==> Creating QEMU keyboard EFI System Partition"
create_esp "$BUILD/astraos-qemu-keyboard-esp.img" "ASTRAKEYB" "$BUILD/platform.dtb" "uefi-keyboard"

echo "==> Creating QEMU desktop EFI System Partition"
create_esp "$BUILD/astraos-qemu-desktop-esp.img" "ASTRADESK" "$BUILD/platform.dtb" "uefi-keyboard" "desktop"

echo "==> Creating UEFI-bootable ARM64 ISO images"
create_iso "$BUILD/astraos-aarch64.iso" "$BUILD/astraos-aarch64-esp.img" "ASTRAOS"
create_iso "$BUILD/astraos-qemu-virt.iso" "$BUILD/astraos-qemu-virt-esp.img" "ASTRAQEMU"
create_iso "$BUILD/astraos-qemu-keyboard.iso" "$BUILD/astraos-qemu-keyboard-esp.img" "ASTRAKEYB"
create_iso "$BUILD/astraos-qemu-desktop.iso" "$BUILD/astraos-qemu-desktop-esp.img" "ASTRADESK"

(
    cd "$BUILD"
    sha256sum \
        astraos-aarch64.iso astraos-aarch64-esp.img \
        astraos-qemu-virt.iso astraos-qemu-virt-esp.img \
        astraos-qemu-keyboard.iso astraos-qemu-keyboard-esp.img \
        astraos-qemu-desktop.iso astraos-qemu-desktop-esp.img > SHA256SUMS
    sha256sum -c SHA256SUMS
)

echo "Built: $BUILD/astraos-aarch64.iso"
echo "Built: $BUILD/astraos-aarch64-esp.img"
echo "Built: $BUILD/astraos-qemu-virt.iso"
echo "Built: $BUILD/astraos-qemu-virt-esp.img"
echo "Built: $BUILD/astraos-qemu-keyboard.iso"
echo "Built: $BUILD/astraos-qemu-keyboard-esp.img"
echo "Built: $BUILD/astraos-qemu-desktop.iso"
echo "Built: $BUILD/astraos-qemu-desktop-esp.img"
echo "Kernel: $BUILD/kernel.elf (ELF), $BUILD/kernel.bin (flat image)"
echo "Checksums: $BUILD/SHA256SUMS"
