#include "fdt.h"

#include <stddef.h>
#include <stdint.h>

/* Required by libFuzzer; malformed data is expected and must not crash. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    AstraFdtView view = {0};
    AstraFdtResult result = ASTRA_FDT_INVALID_ARGUMENT;
    const uint8_t *value = NULL;
    size_t value_length = 0u;

    result = astra_fdt_open(data, size, &view);
    if (result != ASTRA_FDT_OK) {
        return 0;
    }

    (void)astra_fdt_root_property(
        &view, "model", sizeof("model") - 1u, &value, &value_length
    );
    value = NULL;
    value_length = 0u;

    (void)astra_fdt_root_property(
        &view, "compatible", sizeof("compatible") - 1u, &value, &value_length
    );
    value = NULL;
    value_length = 0u;

    (void)astra_fdt_node_property(
        &view,
        "/chosen",
        sizeof("/chosen") - 1u,
        "stdout-path",
        sizeof("stdout-path") - 1u,
        &value,
        &value_length
    );
    value = NULL;
    value_length = 0u;

    (void)astra_fdt_node_property(
        &view,
        "/aliases",
        sizeof("/aliases") - 1u,
        "serial0",
        sizeof("serial0") - 1u,
        &value,
        &value_length
    );

    return 0;
}
