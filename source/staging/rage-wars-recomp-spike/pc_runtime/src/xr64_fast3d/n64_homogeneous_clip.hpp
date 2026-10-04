#pragma once
#include "n64_raw_fast3d_renderer.hpp"
#include <algorithm>
#include <cmath>
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
    std::vector<N64RawFast3DVertex> result;
    result.reserve(input.size());
    for(std::size_t i=0;i+2<input.size();i+=3) {
        std::vector<N64RawFast3DVertex> polygon{input[i],input[i+1],input[i+2]};
        bool finite=true;
        for(const auto& v:polygon) finite &= std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::isfinite(v.w);
        if(!finite)continue;
        for(int plane=0;plane<6 && !polygon.empty();++plane) {
            bool all_inside=true;for(const auto& v:polygon)all_inside &= distance(v,plane)>=0;
            if(all_inside)continue;
            std::vector<N64RawFast3DVertex> clipped;
            for(std::size_t j=0;j<polygon.size();++j) {
                const auto& a=polygon[j];const auto& b=polygon[(j+1)%polygon.size()];
                const double da=distance(a,plane),db=distance(b,plane);
                if(da>=0)clipped.push_back(a);
                if((da>=0)!=(db>=0))clipped.push_back(interpolate(a,b,da/(da-db)));
            }
            polygon=std::move(clipped);
        }
        for(std::size_t j=1;j+1<polygon.size();++j) {
            if(polygon[0].w<=0 || polygon[j].w<=0 || polygon[j+1].w<=0)continue;
            result.insert(result.end(),{polygon[0],polygon[j],polygon[j+1]});
        }
    }
    return result;
}
}
