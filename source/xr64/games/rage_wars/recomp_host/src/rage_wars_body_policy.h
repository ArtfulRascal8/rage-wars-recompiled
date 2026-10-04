#pragma once
#include <stdint.h>
#include <string.h>

/* Render-only local-view predicate. RDRAM is the canonical 8 MiB host-word-swapped image. */
static inline uint32_t xr64_body_word(const uint8_t* ram, uint32_t offset) {
    uint32_t value; memcpy(&value, ram + offset, sizeof(value)); return value;
}
static inline int xr64_body_local_view(const uint8_t* ram, uint32_t actor, uint32_t view, int enabled) {
    if (!enabled || !ram || actor < 0x80000000U || actor > 0x807FE918U ||
        view < 0x80000000U || view > 0x807FF000U) return 0;
    const uint32_t a = actor - 0x80000000U;
    const uint32_t v = view - 0x80000000U;
    return ram[0x140225U ^ 3U] == 1 &&
        xr64_body_word(ram, a + 0x5DC) == view &&
        xr64_body_word(ram, a + 0x5D4) == 0 &&
        (int32_t)xr64_body_word(ram, a + 0x5E4) > 0 &&
        xr64_body_word(ram, v + 0x24) == 0;
}
