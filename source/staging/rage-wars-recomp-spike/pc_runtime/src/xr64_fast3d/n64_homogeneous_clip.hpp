#pragma once
#include "n64_raw_fast3d_renderer.hpp"
#include <algorithm>
#include <cmath>
#include <utility>
namespace xr64 {
// Clip geometry before the existing divide. Texture state and the backend's
// interpolation mode remain unchanged; surviving original vertices are exact.
inline std::vector<N64RawFast3DVertex> n64_clip_triangles(
        const std::vector<N64RawFast3DVertex>& input,
        const N64RawFast3DViewport& viewport, float width, float height) {
    auto distance = [&](const N64RawFast3DVertex& v, int plane) -> double {
        const double w=v.w;
        const double x=(double(v.x)*viewport.scale_x+w*viewport.translate_x)/(2.0*width)-w;
        const double y=w-(w*viewport.translate_y-double(v.y)*viewport.scale_y)/(2.0*height);
        const double c=plane<2?x:plane<4?y:double(v.z);
        return w+((plane&1)?-c:c);
    };
    auto interpolate=[](const N64RawFast3DVertex& a,const N64RawFast3DVertex& b,double t) {
        auto lerp=[&](float x,float y){return float(double(x)+(double(y)-x)*t);};
        auto color=[&](N64RawFast3DColor x,N64RawFast3DColor y) {
            auto byte=[&](unsigned u,unsigned v){return static_cast<std::uint8_t>(std::clamp(std::lround(double(u)+(double(v)-u)*t),0L,255L));};
            return N64RawFast3DColor{byte(x.r,y.r),byte(x.g,y.g),byte(x.b,y.b),byte(x.a,y.a)};
        };
        auto v=a;v.x=lerp(a.x,b.x);v.y=lerp(a.y,b.y);v.z=lerp(a.z,b.z);v.w=lerp(a.w,b.w);
        v.s=lerp(a.s,b.s);v.t=lerp(a.t,b.t);v.color=color(a.color,b.color);v.attributes=color(a.attributes,b.attributes);return v;
    };
    // Fixed-capacity polygons: a triangle gains at most one vertex per plane.
    // Same distances, interpolation and emission order as the allocating form;
    // a triangle wholly outside one plane is rejected without clipping.
    std::vector<N64RawFast3DVertex> result;
    result.reserve(input.size());
    N64RawFast3DVertex buffers[2][9];
    for(std::size_t i=0;i+2<input.size();i+=3) {
        N64RawFast3DVertex* polygon=buffers[0];
        N64RawFast3DVertex* scratch=buffers[1];
        polygon[0]=input[i];polygon[1]=input[i+1];polygon[2]=input[i+2];
        std::size_t count=3;
        bool finite=true;
        for(std::size_t k=0;k<3;++k){const auto& v=polygon[k];finite &= std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::isfinite(v.w);}
        if(!finite)continue;
        for(int plane=0;plane<6 && count;++plane) {
            double dist[9];
            bool all_inside=true,all_outside=true;
            for(std::size_t k=0;k<count;++k){dist[k]=distance(polygon[k],plane);all_inside&=dist[k]>=0;all_outside&=dist[k]<0;}
            if(all_inside)continue;
            if(all_outside){count=0;break;}
            std::size_t out=0;
            for(std::size_t j=0;j<count;++j) {
                const auto& a=polygon[j];const auto& b=polygon[(j+1)%count];
                const double da=dist[j],db=dist[(j+1)%count];
                if(da>=0)scratch[out++]=a;
                if((da>=0)!=(db>=0))scratch[out++]=interpolate(a,b,da/(da-db));
            }
            std::swap(polygon,scratch);count=out;
        }
        for(std::size_t j=1;j+1<count;++j) {
            if(polygon[0].w<=0 || polygon[j].w<=0 || polygon[j+1].w<=0)continue;
            result.push_back(polygon[0]);result.push_back(polygon[j]);result.push_back(polygon[j+1]);
        }
    }
    return result;
}
}
