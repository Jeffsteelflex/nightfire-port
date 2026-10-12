#include "render/frame_markers.h"

#include <algorithm>
#include <cstring>

namespace nf {

DrawItem fog_pass_item(const PsiFrame& frame) {
    DrawItem item;
    item.fog_pass = true;
    std::memcpy(item.fog_rgb, frame.fog_rgb, sizeof(item.fog_rgb));
    item.fog_far1 = frame.fog_far1;
    return item;
}

DrawItem clear_depth_item() {
    DrawItem item;
    item.clear_depth = true;
    return item;
}

void add_frame_markers(const PsiFrame& frame, size_t index, std::vector<DrawItem>& out) {
    const size_t n = frame.draws.size();
    // Uma posição além do fim (não acontece: as posições são draws.size() no momento) fica no fim.
    const auto here = [&](int32_t at) {
        if (at < 0) return false;
        return size_t(at) == index || (index == n && size_t(at) > n);
    };
    if (here(frame.fog_at)) out.push_back(fog_pass_item(frame));
    for (const int32_t at : frame.clear_z_at) {
        if (at > 0 && here(at)) out.push_back(clear_depth_item());
    }
}

std::vector<DrawItem>::iterator provisional_insert_point(std::vector<DrawItem>& items) {
    return std::find_if(items.begin(), items.end(), [](const DrawItem& d) {
        return d.blend != Blend::Opaque || d.fog_pass || d.clear_depth;
    });
}

}  // namespace nf
