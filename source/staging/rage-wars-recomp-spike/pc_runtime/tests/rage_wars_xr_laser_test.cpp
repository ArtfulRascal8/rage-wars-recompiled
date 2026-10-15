#include "rage_wars_xr_laser.hpp"
#include <cmath>
#include <cstdio>
#include <string>

using namespace xr64;
using namespace xr64::rage_wars::recomp;
namespace {
N64RawFast3DVertex at(float x, float y, float z) { N64RawFast3DVertex v; v.x = x; v.y = y; v.z = z; v.w = 1; return v; }
void wall(N64DecodedFrame& f, float z, bool alpha = false, bool attached = false) {
    N64RawFast3DDrawState s;
    s.perspective_projection = true; s.alpha_blend = alpha; s.camera_attachment = attached;
    s.source_projection = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    std::string e;
    f.draw_triangles({at(-5,-5,z), at(5,-5,z), at(5,5,z), at(-5,-5,z), at(5,5,z), at(-5,5,z)}, s, e);
}
}
int main() {
    N64DecodedFrame f; std::string e; f.begin_frame(320, 240, e);
    wall(f, -10.0F); wall(f, -4.0F, true); wall(f, -2.0F, false, true); wall(f, -20.0F);
    f.end_frame(e);
    XrLaserWorld world; world.build(f, 1);
    float t = 0;
    const bool hit = world.raycast({0, 0, 0}, {0, 0, -1}, 0.3F, 1000.0F, t);
    float t_miss = 0;
    const bool miss = world.raycast({0, 0, 0}, {0, 0, 1}, 0.3F, 1000.0F, t_miss);
    float t_edge = 0;
    const bool edge = world.raycast({6, 0, 0}, {0, 0, -1}, 0.3F, 1000.0F, t_edge);
    const bool ok = world.triangles.size() == 4 && hit && std::fabs(t - 10.0F) < 1e-4F && !miss && !edge;
    if (!ok) { std::fprintf(stderr, "FAIL laser tris=%zu hit=%d t=%f miss=%d edge=%d\n", world.triangles.size(), hit, t, miss, edge); return 1; }
    std::puts("PASS xr laser: nearest opaque world hit, alpha effects and first-person draws ignored, misses rejected");
    return 0;
}
