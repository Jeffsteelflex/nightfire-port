// O chão da física (driving/physics/world.h).

#include "driving/physics/world.h"

namespace drv {

namespace {
WorldCollision* g_world = nullptr;
}

void set_world(WorldCollision* world) { g_world = world; }
WorldCollision* world() { return g_world; }

bool FlatGround::find_closest_face(WWorldPos& wp, const float pos[3], bool flag) {
    (void)pos;
    (void)flag;
    ++queries;
    // Um triângulo fixo do plano: (v1 - v0) x (v0 - v2) sai na vertical (a física vira a normal para cima).
    wp.v0[0] = 0.0f;
    wp.v0[1] = height_;
    wp.v0[2] = 0.0f;
    wp.v1[0] = 0.0f;
    wp.v1[1] = height_;
    wp.v1[2] = 1.0f;
    wp.v2[0] = 1.0f;
    wp.v2[1] = height_;
    wp.v2[2] = 0.0f;
    wp.face = this;
    return true;
}

void FlatGround::make_face_at_point(WWorldPos& wp, const float pos[3]) { find_closest_face(wp, pos, true); }

float FlatGround::surface_bump_height(const float pos[3], const uint8_t* surface) {
    (void)pos;
    (void)surface;
    return 0.0f;
}

}  // namespace drv
