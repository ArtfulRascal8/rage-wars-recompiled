#include "rage_wars_native_pc_options.h"
#include "recomp_memory_shim.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "Native focus contract failure at line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

extern "C" std::uint8_t* xr64_recomp_resolve_address(std::uint8_t* rdram, gpr address) {
    const auto physical=static_cast<std::uint32_t>(address)&0x1FFFFFFFU;
    REQUIRE(physical<0x00800000);
    return rdram+physical;
}

static std::uint32_t next_allocation = 0x80010000;
static unsigned allocations;
extern "C" void trace_func_002A1524_r000A2124(std::uint8_t*, recomp_context* ctx) {
    ++allocations;
    // Original frame allocation writes odd FPRs; a cloned context must own them.
    ctx->f_odd[0] = 0x41200000;
    ctx->r2 = static_cast<std::int32_t>(next_allocation);
    next_allocation += (static_cast<std::uint32_t>(ctx->r4) + 7) & ~7U;
}

int main(int argc, char** argv) {
    std::vector<std::uint8_t> memory(0x00800000);
    auto* rdram = memory.data();
    constexpr std::uint32_t manager=0x80001000, stack=0x80001100;
    constexpr std::uint32_t focus=0x80002000, node=0x80002100;
    constexpr std::uint32_t text=0x80003000, fonts=0x80004000;
    MEM_W(0x801407D4, 0)=1;
    MEM_W(0x80144E00, 0)=manager;
    MEM_W(4, manager)=0;
    MEM_W(0xC, manager)=stack;
    MEM_W(4, stack)=0x11;
    MEM_W(8, stack)=focus;
    MEM_H(0xE, focus)=0xB61;
    MEM_H(0x12, focus)=0x5C;
    MEM_W(0, node)=focus;
    MEM_W(0x8014B998, 0)=fonts;
    const char label[]="CONTINUE";
    for (unsigned i=0;i<sizeof(label);++i) MEM_B(i,text)=label[i];
    const auto metric=[&](unsigned font,char character,unsigned width,unsigned height) {
        const auto record=fonts+font*0x49C;
        const auto glyph=static_cast<unsigned>(character-0x20);
        MEM_B(8+glyph*8,record)=width;
        MEM_B(9+glyph*8,record)=height;
    };
    const auto verify_label=[&](std::uint32_t address,char marker) {
        REQUIRE(MEM_BU(0,address)==marker && MEM_BU(1,address)==' ');
        for(unsigned i=0;i<sizeof(label);++i) REQUIRE(MEM_BU(i+2,address)==label[i]);
    };
    recomp_context ctx{};
    ctx.mips3_float_mode=1;
    ctx.f_odd=&ctx.f1.u32l;
    ctx.f1.u32l=0x3F800000;
    for(unsigned i=0;i<3;++i) MEM_B(0x2C+i*2,node)=2;
    // Actual Results font metrics: '>' is 0x0; '*' is 5x5.
    metric(2,'>',0,0);metric(2,'*',5,5);
    const auto marked=xr64_pause_focus_text(rdram,&ctx,node,text);
    REQUIRE(marked!=text);
    verify_label(marked,'*');
    REQUIRE(ctx.f1.u32l==0x3F800000 && ctx.f_odd==&ctx.f1.u32l);
    // Fonts that support the existing arrow retain it, including FF fallback.
    MEM_B(0x2C,node)=0;MEM_B(0x2E,node)=0xFF;MEM_B(0x30,node)=0xFF;
    metric(0,'>',8,7);metric(0,'*',7,5);
    verify_label(xr64_pause_focus_text(rdram,&ctx,node,text),'>');
    // Unknown font coverage preserves the original string without allocation.
    metric(0,'>',0,0);metric(0,'*',0,0);
    const auto before=allocations;
    REQUIRE(xr64_pause_focus_text(rdram,&ctx,node,text)==text);
    REQUIRE(allocations==before);
    metric(0,'>',8,7);
    MEM_H(0x12,focus)=0x15C;
    REQUIRE(xr64_pause_focus_text(rdram,&ctx,node,text)==text);
    MEM_H(0x12,focus)=0x5C;
    MEM_W(0,node)=0;
    REQUIRE(xr64_pause_focus_text(rdram,&ctx,node,text)==text);
    MEM_W(0,node)=focus;
    MEM_W(0x8014B998,0)=0x807FFFFC;
    REQUIRE(xr64_pause_focus_text(rdram,&ctx,node,text)==text);
    // FR=0 clones must also rebind odd registers to their own upper lanes.
    MEM_W(0x8014B998,0)=fonts;
    ctx.mips3_float_mode=0;ctx.f_odd=&ctx.f0.u32h;ctx.f0.u32h=0x3F800000;
    verify_label(xr64_pause_focus_text(rdram,&ctx,node,text),'>');
    REQUIRE(ctx.f0.u32h==0x3F800000 && ctx.f_odd==&ctx.f0.u32h);
    if (argc == 3) {
        std::ifstream input(argv[1],std::ios::binary);
        memory.assign(std::istreambuf_iterator<char>(input),{});
        REQUIRE(memory.size()==0x00800000);
        rdram=memory.data();
        const auto captured_node=static_cast<std::uint32_t>(std::strtoul(argv[2],nullptr,16));
        REQUIRE((captured_node&0x1FFFFFFF)<0x00800000-0x3C);
        const auto strings=static_cast<std::uint32_t>(MEM_W(0x8014B9AC,0));
        const auto string_id=static_cast<std::uint32_t>(MEM_W(0x34,captured_node));
        REQUIRE(string_id<1024);
        const auto captured_text=static_cast<std::uint32_t>(MEM_W(string_id*4,strings));
        const auto replacement=xr64_pause_focus_text(rdram,&ctx,captured_node,captured_text);
        REQUIRE(replacement!=captured_text);
        verify_label(replacement,'*');
        std::puts("Captured Results helper replay: compatible marker and complete CONTINUE label PASS");
    }
    return 0;
}
