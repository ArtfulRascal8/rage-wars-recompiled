#include "funcs.h"
#include <stdio.h>
extern uint32_t xr64_controls_take_weapon_cycle(void);

/* Called inside the original weapon-selection callback after its stun, mode,
 * menu and wheel gates. Selection writes the same pending field as native quick
 * select; the original callback then owns cache invalidation and animation. */
uint32_t xr64_controls_read_weapon_cycle(uint8_t* rdram,uint32_t actor) {
    if(actor<0x80000000U || actor>0x807FE918U || (actor&3U) ||
       MEM_W(0x5D4,actor)!=0 || (uint32_t)MEM_W(0x698,actor)!=0x80109328U)return 0;
    return xr64_controls_take_weapon_cycle();
}

void xr64_controls_weapon_cycle(uint8_t* rdram,recomp_context* ctx,uint32_t actor,uint32_t cycle) {
    int current,order,step,target=-1;
    recomp_context saved;
    if(actor<0x80000000U || actor>0x807FE918U || (actor&3U) ||
       MEM_W(0x5D4,actor)!=0 || (uint32_t)MEM_W(0x698,actor)!=0x80109328U)return;
    if((cycle!=1 && cycle!=2) || MEM_BU(0x80140225,0)!=1 ||
       MEM_W(0x801407D4,0)!=0 || MEM_W(0x5E4,actor)<=0)return;
    current=MEM_H(0x62E,actor);
    if(current<0 || current>=22)return;
    saved=*ctx;
    if(cycle==2) {
        ctx->r4=(gpr)(int32_t)actor;
        trace_func_0022FAD8_r000306D8(rdram,ctx);
        target=(int32_t)ctx->r2;
    } else {
        order=MEM_B(0x603+current*2,actor);
        if(order<1 || order>7)order=1;
        /* Reverse the native quick-select order (1..7), using its eligibility
         * routine for ammo/weapon restrictions. Inventory bytes stay untouched. */
        for(step=1;step<=7 && target<0;++step) {
            const int wanted=1+(order-1-step+7)%7;
            int id;
            for(id=0;id<22;++id) {
                if(id==current || MEM_B(0x603+id*2,actor)!=wanted)continue;
                ctx->r4=(gpr)(int32_t)actor;ctx->r5=id;
                trace_func_0022EAE0_r0002F6E0(rdram,ctx);
                if(ctx->r2!=0){target=id;break;}
            }
        }
    }
    if(target>=0 && target<22 && target!=current) {
        MEM_H(0x770,actor)=target;
        fprintf(stderr,"RW105_WEAPON_CYCLE direction=%s from=%d to=%d\n",cycle==1?"previous":"next",current,target);
    }
    *ctx=saved;
}
