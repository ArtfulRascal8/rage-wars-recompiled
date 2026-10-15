#include "rage_wars_generic_combiner.hpp"
#include <cmath>
#include <cstdio>

using namespace xr64::rage_wars::recomp;
namespace {
int failures = 0;
void check(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAIL %s\n", what); ++failures; } }
bool near4(const std::array<float, 4>& a, const std::array<float, 4>& b) {
    for (int i = 0; i < 4; ++i) if (std::abs(a[i] - b[i]) > 1e-5F) return false;
    return true;
}
// Encode one cycle pair from selectors (inverse of decode_combiner).
std::pair<std::uint32_t, std::uint32_t> encode(const CombinerCycle& c0, const CombinerCycle& c1) {
    const std::uint32_t w0 = 0xFC000000U | (std::uint32_t(c0.a) << 20) | (std::uint32_t(c0.c) << 15) |
        (std::uint32_t(c0.aa) << 12) | (std::uint32_t(c0.ac) << 9) | (std::uint32_t(c1.a) << 5) | std::uint32_t(c1.c);
    const std::uint32_t w1 = (std::uint32_t(c0.b) << 28) | (std::uint32_t(c1.b) << 24) | (std::uint32_t(c1.aa) << 21) |
        (std::uint32_t(c1.ac) << 18) | (std::uint32_t(c0.d) << 15) | (std::uint32_t(c0.ab) << 12) |
        (std::uint32_t(c0.ad) << 9) | (std::uint32_t(c1.d) << 6) | (std::uint32_t(c1.ab) << 3) | std::uint32_t(c1.ad);
    return {w0, w1};
}
}

int main() {
    const CombinerInputs in{{0.8F, 0.6F, 0.4F, 0.5F}, {0.5F, 0.25F, 1.0F, 0.75F}, {0.1F, 0.2F, 0.9F, 0.6F}, {1.0F, 0.5F, 0.0F, 0.4F}};
    // Existing texture-only material: RGB = TEXEL0; alpha = TEXEL0_A * PRIM_A, then * ENV_A.
    {
        const auto s = decode_combiner(0xFCFF97FFU, 0xFF14FE3FU);
        check(s.cycle[0].d == 1 && s.cycle[0].aa == 1 && s.cycle[0].ac == 3 && s.cycle[1].d == 0 && s.cycle[1].ac == 5,
              "decode matches documented texture-only material");
        check(near4(combiner_reference(s, true, in), {0.8F, 0.6F, 0.4F, 0.5F * 0.6F * 0.4F}), "two-cycle texture-only material");
    }
    // G_CC_MODULATERGBA in both cycles: TEXEL0 * SHADE (old GL_MODULATE result).
    {
        const CombinerCycle m{1, 15, 4, 7, 1, 7, 4, 7};
        const auto [w0, w1] = encode(m, m);
        check(near4(combiner_reference(decode_combiner(w0, w1), false, in), {0.4F, 0.15F, 0.4F, 0.375F}), "modulate equals texture x shade");
    }
    // Primitive tint (glow/reticle): RGB = PRIM * TEXEL0, alpha = TEXEL0_A * PRIM_A.
    {
        const CombinerCycle t{3, 15, 1, 7, 1, 7, 3, 7};
        const auto [w0, w1] = encode(t, t);
        check(near4(combiner_reference(decode_combiner(w0, w1), false, in), {0.08F, 0.12F, 0.36F, 0.3F}), "primitive tint");
    }
    // Shadow decal: RGB = 0 (black), alpha = TEXEL0_A * PRIM_A.
    {
        const CombinerCycle sh{15, 15, 31, 7, 1, 7, 3, 7};
        const auto [w0, w1] = encode(sh, sh);
        check(near4(combiner_reference(decode_combiner(w0, w1), false, in), {0.0F, 0.0F, 0.0F, 0.3F}), "black shadow with texture alpha");
    }
    // Environment lerp: RGB = (PRIM - ENV) * TEXEL0 + ENV.
    {
        const CombinerCycle l{3, 5, 1, 5, 7, 7, 7, 6};
        const auto [w0, w1] = encode(l, l);
        const auto r = combiner_reference(decode_combiner(w0, w1), false, in);
        check(std::abs(r[0] - ((0.1F - 1.0F) * 0.8F + 1.0F)) < 1e-5F && r[3] == 1.0F, "primitive/environment lerp");
    }
    // One-cycle mode uses the second selector set only.
    {
        const CombinerCycle black{15, 15, 31, 7, 7, 7, 7, 7}, tex{15, 15, 31, 1, 7, 7, 7, 1};
        const auto [w0, w1] = encode(black, tex);
        check(near4(combiner_reference(decode_combiner(w0, w1), false, in), {0.8F, 0.6F, 0.4F, 0.5F}), "one-cycle uses cycle 1");
    }
    // Two-colour text (kill log, HUD fonts): lerp between the primitive and environment colours by the
    // glyph. The captured font material FC511BFF/3FFDFE38 is lerp(PRIM, ENV, TEXEL1), alpha TEXEL0_A * ENV_A.
    {
        const auto font = combiner_rect_lerp(decode_combiner(0xFC511BFFU, 0x3FFDFE38U), true);
        check(font.valid && font.from_prim && font.alpha_source == 2, "captured two-colour font material is recognised");
        // Same colour equation in one-cycle mode uses cycle 1 and has no pass-through requirement.
        const CombinerCycle lerp_env_prim{5, 3, 1, 3, 1, 7, 3, 7};
        const CombinerCycle zero{15, 15, 31, 7, 7, 7, 7, 7};
        const auto [l0, l1] = encode(zero, lerp_env_prim);
        const auto one = combiner_rect_lerp(decode_combiner(l0, l1), false);
        check(one.valid && one.from_prim && one.alpha_source == 1, "one-cycle lerp(PRIM, ENV, TEXEL0) uses cycle 1 and primitive alpha");
        const CombinerCycle lerp_prim_env{3, 5, 1, 5, 1, 7, 5, 7};
        const auto [r0, r1] = encode(zero, lerp_prim_env);
        const auto reversed = combiner_rect_lerp(decode_combiner(r0, r1), false);
        check(reversed.valid && !reversed.from_prim && reversed.alpha_source == 2, "lerp(ENV, PRIM, TEXEL0) starts from environment");
        // Not text lerps: plain tint, a lerp by a register, and a two-cycle mode that does not pass through.
        const CombinerCycle tint{1, 15, 3, 7, 1, 7, 3, 7};
        const auto [t0, t1] = encode(tint, tint);
        check(!combiner_rect_lerp(decode_combiner(t0, t1), false).valid, "TEXEL0 x PRIM is not a lerp");
        const CombinerCycle by_prim{5, 3, 3, 3, 1, 7, 3, 7};
        const auto [b0, b1] = encode(by_prim, by_prim);
        check(!combiner_rect_lerp(decode_combiner(b0, b1), false).valid, "lerp by PRIM (not a texel) is not text");
        const CombinerCycle second{1, 15, 3, 7, 1, 7, 3, 7};
        const auto [n0, n1] = encode(lerp_env_prim, second);
        check(!combiner_rect_lerp(decode_combiner(n0, n1), true).valid, "two-cycle lerp needs a pass-through second cycle");
        check(!combiner_rect_lerp(decode_combiner(0xFC12ABFFU, 0xFFFFFE38U), true).valid, "environment HUD material is not a lerp");
    }
    if (failures) { std::fprintf(stderr, "%d generic combiner checks failed\n", failures); return 1; }
    std::puts("PASS generic combiner: decode, texture-only, modulate, primitive tint, shadow, lerp, one-cycle selection, two-colour text lerp");
    return 0;
}
