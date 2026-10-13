// As tabelas da câmera do jogador (driving/camera/camera_ini.h).

#include "driving/camera/camera_ini.h"

#include <cstdio>
#include <cstring>

#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

float fbits(uint32_t b) { return ps2::ffloat(b); }

CameraPtmf ptmf(uint32_t fn) { return CameraPtmf{0, -1, fn}; }

bool has(const char* text, const char* word) { return std::strstr(text, word) != nullptr; }

// O strtok da newlib sobre o buffer do DAFI (o valor de "heights"; delimitadores " ,\t").
char* tok_next(char*& save, const char* delims) {
    char* s = save;
    if (s == nullptr) return nullptr;
    s += std::strspn(s, delims);
    if (*s == 0) {
        save = nullptr;
        return nullptr;
    }
    char* e = s + std::strcspn(s, delims);
    if (*e != 0) {
        *e = 0;
        save = e + 1;
    } else {
        save = nullptr;
    }
    return s;
}

constexpr int kMaxArms = 0x20;
constexpr int kMaxEllipseHeights = 0x1e;

}  // namespace

float CameraClamp(float v, float lo, float hi) {
    if (ee::lt(v, lo)) v = lo;
    if (ee::lt(hi, v)) v = hi;
    return v;
}

void ReadAnchorInfo(IniFile& ini, const char* section, CameraAnchorInfo* info) {
    info->offset[0] = ini.read_float(section, "Anchor_X", 0.0f);
    info->offset[1] = ini.read_float(section, "Anchor_Y", 0.0f);
    info->offset[2] = ini.read_float(section, "Anchor_Z", 0.0f);
    info->slide_dist = ini.read_float(section, "Anchor_Slide_Dist", 0.0f);
    info->slide_rate = ini.read_float(section, "Anchor_Slide_Rate", 0.0f);
    info->slide_rec_rate = ini.read_float(section, "Anchor_Slide_Rec_Rate", 0.0f);
}

bool CameraCarListContains(const char* name, const char* list) {
    const size_t n = std::strlen(name);
    if (std::strlen(list) < n) return false;
    const char* p = std::strstr(list, name);
    if (p == nullptr) return false;
    const char c = p[n];
    return c == ' ' || c == ',' || c == 0;
}

bool CheckCameraAgainstCar(char* cars, const CameraCarInfo& car) {
    if (cars == nullptr) return true;
    for (size_t i = 0; i < std::strlen(cars); ++i)
        if (cars[i] >= 'A' && cars[i] <= 'Z') cars[i] = char(cars[i] + 0x20);
    if (!car.override_type.empty() && car.car_class == 0) return CameraCarListContains(car.override_type.c_str(), cars);
    if (!car.secondary.empty() && CameraCarListContains(car.secondary.c_str(), cars)) return true;
    return CameraCarListContains(car.type.c_str(), cars);
}

void ResolveWeaponNames(const char* names, uint32_t mask[2], const std::vector<CameraWeaponSlot>& weapons) {
    mask[0] = mask[1] = 0;
    if (names == nullptr) return;
    for (int i = 0; i < 32; ++i) {
        if (size_t(i) >= weapons.size()) continue;  // uma vaga sem nome
        const CameraWeaponSlot& slot = weapons[size_t(i)];
        if (slot.name.empty() || std::strstr(names, slot.name.c_str()) == nullptr) continue;
        const int32_t w = slot.weapon;
        const int32_t word = (w > -1 ? w : w + 31) >> 5;
        if (word >= 2) return;
        mask[word] |= 1u << (uint32_t(w) & 31);
    }
}

void CameraTables::reset() {
    *this = CameraTables{};
}

void CameraTables::load(IniFile& ini, const CameraCarInfo& car, const std::vector<CameraWeaponSlot>& weapons) {
    if (nf::raw(tables.modes) != nullptr) return;
    CameraTableGlobals& g = tables;
    g.world_anim_index = g.rel_anim_index = g.ai_path_anim_index = g.collision_index = -1;
    g.autodrive_index = g.default_index = g.tumble_index = -1;
    g.pause_index = g.missile_index = g.cinematic_heli_index = g.cinematic_mode_index = -1;
    g.bumper_count = g.heli_count = g.fixed_count = g.spline_count = g.ellipse_count = 0;
    g.dashboard_count = g.mode_count = 0;

    // A primeira passada: quantos de cada tipo.
    const int32_t sections = ini.dafi().section_count();
    int32_t autodrive_seen = 0, anycar_helis = 0, anycar_dashboards = 0;
    for (int32_t s = 0; s < sections; ++s) {
        const char* name = ini.dafi().section_by_index(s);
        if (has(name, "Global") || has(name, "Debug") || has(name, ":Arm")) continue;
        if (!CheckCameraAgainstCar(const_cast<char*>(ini.read_string(name, "car", nullptr)), car)) continue;
        if (has(name, "Bumper")) {
            ++g.bumper_count;
            ++g.mode_count;
        } else if (has(name, "Heli")) {
            if (has(name, "AnyCar")) {
                if (g.heli_count != anycar_helis) continue;
                ++anycar_helis;
            }
            ++g.heli_count;
            ++g.mode_count;
        } else if (has(name, "Fixed")) {
            ++g.mode_count;
            ++g.fixed_count;
        } else if (has(name, "Spline")) {
            ++g.mode_count;
            ++g.spline_count;
        } else if (has(name, "Ellipse")) {
            ++g.mode_count;
            ++g.ellipse_count;
        } else if (has(name, "Dashboard")) {
            if (has(name, "AnyCar")) {
                if (g.dashboard_count != anycar_dashboards) continue;
                ++anycar_dashboards;
            }
            ++g.dashboard_count;
            ++g.mode_count;
        } else if (has(name, "Tumble") || has(name, "WorldAnim") || has(name, "RelativeAnim") ||
                   has(name, "AIPathAnim") || has(name, "Collision")) {
            ++g.mode_count;
        } else if (has(name, "AutoDrive")) {
            if (autodrive_seen == 0) {
                autodrive_seen = 1;
                ++g.mode_count;
            }
        }
        // (um tipo desconhecido: o sprintf da mensagem, sem contar)
    }

    bumpers_.assign(size_t(g.bumper_count), BumperCamInfo{});
    helis_.assign(size_t(g.heli_count), HeliCamInfo{});
    fixeds_.assign(size_t(g.fixed_count), FixedCamInfo{});
    splines_.assign(size_t(g.spline_count), SplineCamInfo{});
    ellipses_.assign(size_t(g.ellipse_count), EllipseCamInfo{});
    dashboards_.assign(size_t(g.dashboard_count), DashboardCamInfo{});
    modes_.assign(size_t(g.mode_count + 1), CameraModeInfo{});
    heli_arms_.assign(size_t(g.heli_count), {});
    heights_.assign(size_t(g.ellipse_count), {});
    g.bumpers = bumpers_.data();
    g.helis = helis_.data();
    g.fixeds = fixeds_.data();
    g.splines = splines_.data();
    g.ellipses = ellipses_.data();
    g.dashboards = dashboards_.data();
    g.modes = modes_.data();

    // A segunda passada: as tabelas.
    int32_t bumper = 0, heli = 0, spline = 0, ellipse = 0, fixed = 0, dashboard = 0, camera = 0;
    anycar_helis = anycar_dashboards = 0;
    char arm_section[0x80];
    for (int32_t s = 0; s < sections; ++s) {
        const char* name = ini.dafi().section_by_index(s);
        if (has(name, "Global") || has(name, "Debug") || has(name, ":Arm")) continue;
        if (!CheckCameraAgainstCar(const_cast<char*>(ini.read_string(name, "car", nullptr)), car)) continue;
        CameraModeInfo scratch{};
        CameraModeInfo* mode = camera < int32_t(modes_.size()) ? &modes_[size_t(camera)] : &scratch;
        if (mode == &scratch) ++guards;  // o PS2 escreveria além da tabela (um tipo desconhecido antes)
        mode->tumble = uint8_t(ini.read_integer(name, "tumble", 0));
        mode->shake = uint8_t(ini.read_integer(name, "shake", 0));
        mode->smooth_trans = int8_t(ini.read_integer(name, "smoothTrans", 0));
        mode->look_back = uint8_t(ini.read_integer(name, "lookBack", 0));
        mode->selectable = uint8_t(ini.read_integer(name, "selectable", 0));
        mode->lerp_rotation = uint8_t(ini.read_integer(name, "lerpRotation", 0));
        mode->default_fov = ini.read_float(name, "defaultFov", fbits(0x42040000));  // 33
        mode->explosion_shake_scale = ini.read_float(name, "explosionShakeScale", 1.0f);
        mode->interior_view = uint8_t(ini.read_integer(name, "interiorView", 0));
        mode->cam_id = int16_t(ini.read_integer(name, "camID", 0));
        if (g.default_index == -1 && ini.read_integer(name, "defaultCamera", 0) != 0) g.default_index = int8_t(camera);

        if (has(name, "Heli")) {
            if (has(name, "AnyCar")) {
                if (heli != anycar_helis) continue;
                anycar_helis = heli + 1;
            }
            mode->type = kCamHeli;
            mode->update = ptmf(kCamUpdateMomentumHeli);
            mode->index = int8_t(heli);
            HeliCamInfo& info = helis_[size_t(heli)];
            const int32_t this_heli = heli++;
            info.min_rate = ini.read_float(name, "Min_Rate", 0.0f);
            info.max_rate = ini.read_float(name, "Max_Rate", 0.0f);
            info.speed_rate_diff = ini.read_float(name, "Speed_Rate_Diff", 0.0f);
            info.height_factor = ini.read_float(name, "Height_Factor", 0.0f);
            info.fallback_factor = ini.read_float(name, "Fallback_Factor", 0.0f);
            info.max_fallback = ini.read_float(name, "Max_Fallback", 0.0f);
            info.vertigo_lerp = ini.read_float(name, "Vertigo_Lerp", 0.0f);
            info.tumble_arm_scale = ini.read_float(name, "Tumble_Arm_Scale", 0.0f);
            info.rigid_arm = uint8_t(ini.read_integer(name, "rigidArm", 0));
            info.check_collisions = uint8_t(ini.read_integer(name, "checkCollisions", 1));
            info.max_vertigo_downhill = CameraClamp(ini.read_float(name, "MaxVertigoDownhill", 0.0f), 0.0f, 1.0f);
            info.max_vertigo_uphill = CameraClamp(ini.read_float(name, "MaxVertigoUphill", 0.0f), 0.0f, 1.0f);
            info.noise_pace = ini.read_float(name, "noisePace", 0.0f);
            info.noise_amount = ini.read_float(name, "noiseAmount", 0.0f);
            info.noise_frequency = ini.read_float(name, "noiseFrequency", 0.0f);
            info.up_rate = ini.read_float(name, "upRate", 0.0f);
            info.look_up = ini.read_float(name, "lookUp", 0.0f);
            if (ini.read_integer(name, "Cinematic", 0) != 0) {
                g.cinematic_heli_index = int8_t(heli);  // já contado depois deste (o original guarda o seguinte)
                g.cinematic_mode_index = int8_t(camera);
            }
            std::vector<HeliArmInfo>& arms = heli_arms_[size_t(this_heli)];
            int arm = 0;
            for (; arm < kMaxArms; ++arm) {
                std::snprintf(arm_section, sizeof(arm_section), "%s:Arm%d", name, arm);
                if (!ini.section_exists(arm_section)) break;
                HeliArmInfo a{};
                a.sideways = ini.read_float(arm_section, "Heli_Sideways", 0.0f);
                a.height = ini.read_float(arm_section, "Heli_Height", 0.0f);
                a.distance = ini.read_float(arm_section, "Heli_Distance", 0.0f);
                a.arm_transition = ini.read_integer(arm_section, "armTransition", mode->smooth_trans);
                ReadAnchorInfo(ini, arm_section, &a.anchor);
                ResolveWeaponNames(ini.read_string(arm_section, "weapons", nullptr), a.weapons, weapons);
                arms.push_back(a);
            }
            info.arm_count = arm;
            info.arms = arms.empty() ? nullptr : arms.data();
        } else if (has(name, "Spline")) {
            mode->type = kCamSpline;
            mode->update = ptmf(kCamUpdateSpline);
            mode->index = int8_t(spline);
            SplineCamInfo& info = splines_[size_t(spline++)];
            info.spline_speed = ini.read_float(name, "splineSpeed", 0.0f);
            info.height_offset = ini.read_float(name, "heightOffset", 0.0f);
            info.max_spline_cam_dist = ini.read_float(name, "maxSplineCamDist", 0.0f);
            info.exp_life_time = ini.read_float(name, "expLifeTime", 0.0f);
            info.min_zoom_dist = ini.read_float(name, "minZoomDist", 0.0f);
            info.min_fov = ini.read_float(name, "minFov", 0.0f);
            ReadAnchorInfo(ini, name, &info.anchor);
        } else if (has(name, "Ellipse")) {
            mode->type = kCamEllipse;
            mode->update = ptmf(kCamUpdateEllipse);
            mode->index = int8_t(ellipse);
            const int32_t this_ellipse = ellipse++;
            EllipseCamInfo& info = ellipses_[size_t(this_ellipse)];
            info.x_rad = ini.read_float(name, "xRad", 0.0f);
            info.z_rad = ini.read_float(name, "zRad", 0.0f);
            info.facets = ini.read_integer(name, "facets", 0);
            ReadAnchorInfo(ini, name, &info.anchor);
            info.facets -= info.facets & 3;
            // "heights": até 30 números, cada um dobrado e truncado num byte; o strncpy do original para no primeiro
            // zero e completa com zeros.
            char buffer[kMaxEllipseHeights] = {};
            int count = 0;
            static char fallback[] = "2.0";
            char* text = const_cast<char*>(ini.read_string(name, "heights", fallback));
            char* save = text;
            char* tok = tok_next(save, " ,\t");
            while (tok != nullptr) {
                const float v = dafi_atof(tok);
                buffer[count++] = char(ee::cvt_w(ee::add(v, v)));
                tok = tok_next(save, " ,\t");
                if (count >= kMaxEllipseHeights) break;
            }
            info.height_count = count;
            std::vector<int8_t>& heights = heights_[size_t(this_ellipse)];
            heights.assign(size_t(count), 0);
            for (int i = 0; i < count && buffer[i] != 0; ++i) heights[size_t(i)] = int8_t(buffer[i]);
            info.heights = heights.empty() ? nullptr : heights.data();
        } else if (has(name, "Bumper")) {
            mode->type = kCamBumper;
            mode->update = ptmf(kCamUpdateBumper);
            mode->index = int8_t(bumper);
            BumperCamInfo& info = bumpers_[size_t(bumper++)];
            info.forward_arm[0] = ini.read_float(name, "forwardArm.x", 0.0f);
            info.forward_arm[1] = ini.read_float(name, "forwardArm.y", 0.0f);
            info.forward_arm[2] = ini.read_float(name, "forwardArm.z", 0.0f);
            info.forward_arm[3] = ini.read_float(name, "forwardArm.panUp", 0.0f);
            info.backwards_arm[0] = ini.read_float(name, "backwardsArm.x", 0.0f);
            info.backwards_arm[1] = ini.read_float(name, "backwardsArm.y", 0.0f);
            info.backwards_arm[2] = ini.read_float(name, "backwardsArm.z", 0.0f);
            info.backwards_arm[3] = ini.read_float(name, "backwardsArm.panUp", 0.0f);
            ReadAnchorInfo(ini, name, &info.anchor);
        } else if (has(name, "Dashboard")) {
            if (has(name, "AnyCar")) {
                if (dashboard != anycar_dashboards) continue;
                anycar_dashboards = dashboard + 1;
            }
            mode->type = kCamDashboard;
            mode->update = ptmf(kCamUpdateDashboard);
            mode->index = int8_t(dashboard);
            DashboardCamInfo& info = dashboards_[size_t(dashboard++)];
            info.forward_arm[0] = ini.read_float(name, "forwardArm.x", 0.0f);
            info.forward_arm[1] = ini.read_float(name, "forwardArm.y", 0.0f);
            info.forward_arm[2] = ini.read_float(name, "forwardArm.z", 0.0f);
            info.forward_arm[3] = 0.0f;
            info.backwards_arm[0] = ini.read_float(name, "backwardsArm.x", 0.0f);
            info.backwards_arm[1] = ini.read_float(name, "backwardsArm.y", 0.0f);
            info.backwards_arm[2] = ini.read_float(name, "backwardsArm.z", 0.0f);
            info.backwards_arm[3] = 0.0f;
            info.forward_pitch = ini.read_float(name, "forwardPitch", 0.0f);
            info.forward_yaw = ini.read_float(name, "forwardYaw", 0.0f);
            info.intertia_scale = ini.read_float(name, "intertiaScale", 0.0f);
            info.intertia_min = ini.read_float(name, "intertiaMin", 0.0f);
            info.intertia_max = ini.read_float(name, "intertiaMax", 0.0f);
            info.steer_scale = ini.read_float(name, "steerScale", 0.0f);
            info.steer_max = ini.read_float(name, "steerMax", 0.0f);
            info.steer_pace = ini.read_float(name, "steerPace", 0.0f);
            info.glance_scale = ini.read_float(name, "glanceScale", 0.0f);
            info.glance_max = ini.read_float(name, "glanceMax", 0.0f);
            info.glance_pace = ini.read_float(name, "glancePace", 0.0f);
            info.force_scale[0] = ini.read_float(name, "forceScale.x", 0.0f);
            info.force_scale[1] = ini.read_float(name, "forceScale.y", 0.0f);
            info.force_scale[2] = ini.read_float(name, "forceScale.z", 0.0f);
            info.force_scale[3] = 0.0f;
            info.force_max[0] = ini.read_float(name, "forceMax.x", 0.0f);
            info.force_max[1] = ini.read_float(name, "forceMax.y", 0.0f);
            info.force_max[2] = ini.read_float(name, "forceMax.z", 0.0f);
            info.force_max[3] = 0.0f;
            info.force_pace = ini.read_float(name, "forcePace", 0.0f);
            info.torque_scale[0] = ini.read_float(name, "torqueScale.x", 0.0f);
            info.torque_scale[1] = ini.read_float(name, "torqueScale.y", 0.0f);
            info.torque_scale[2] = ini.read_float(name, "torqueScale.z", 0.0f);
            info.torque_scale[3] = 0.0f;
            info.torque_max[0] = ini.read_float(name, "torqueMax.x", 0.0f);
            info.torque_max[1] = ini.read_float(name, "torqueMax.y", 0.0f);
            info.torque_max[2] = ini.read_float(name, "torqueMax.z", 0.0f);
            info.torque_max[3] = 0.0f;
            info.torque_pace = ini.read_float(name, "torquePace", 0.0f);
            info.noise_amount = ini.read_float(name, "noiseAmount", 0.0f);
            info.noise_frequency = ini.read_float(name, "noiseFrequency", 0.0f);
            mode->default_fov = ini.read_float(name, "defaultFov", fbits(0x42040000));
            info.vertigo_lerp = ini.read_float(name, "Vertigo_Lerp", 0.0f);
            info.max_vertigo_downhill = CameraClamp(ini.read_float(name, "MaxVertigoDownhill", 0.0f), 0.0f, 1.0f);
            info.max_vertigo_uphill = CameraClamp(ini.read_float(name, "MaxVertigoUphill", 0.0f), 0.0f, 1.0f);
            ReadAnchorInfo(ini, name, &info.anchor);
        } else if (has(name, "Fixed")) {
            mode->type = kCamFixed;
            mode->update = ptmf(kCamUpdateFixed);
            mode->index = int8_t(fixed);
            FixedCamInfo& info = fixeds_[size_t(fixed++)];
            info.min_zoom_dist = ini.read_float(name, "minZoomDist", 0.0f);
            info.min_fov = ini.read_float(name, "minFov", 0.0f);
            info.max_zoom_dist = ini.read_float(name, "maxZoomDist", 0.0f);
            info.rot_speed = ini.read_float(name, "rotSpeed", 1.0f);
            ReadAnchorInfo(ini, name, &info.anchor);
        } else if (has(name, "Tumble")) {
            g.tumble_index = int8_t(camera);
            mode->type = kCamTumble;
            mode->update = ptmf(kCamUpdateTumble);
            mode->index = 0;
            modes.rel_pos_lerp = ini.read_float(name, "relPosLerp", fbits(0x3d75c28f));
            modes.vector_lerp = ini.read_float(name, "vectorLerp", fbits(0x3c23d70a));
            ReadAnchorInfo(ini, name, &modes.tumble_anchor);
        } else if (has(name, "WorldAnim") || has(name, "RelativeAnim") || has(name, "AIPathAnim")) {
            if (has(name, "WorldAnim")) {
                g.world_anim_index = int8_t(camera);
                mode->type = kCamAnimation;
                mode->update = ptmf(kCamUpdateWorldAnimation);
            } else if (has(name, "RelativeAnim")) {
                g.rel_anim_index = int8_t(camera);
                mode->type = kCamAnimation;
                mode->update = ptmf(kCamUpdateRelativeAnimation);
            } else {
                g.ai_path_anim_index = int8_t(camera);
                mode->type = kCamAIPathAnimation;
                mode->update = ptmf(kCamUpdateAIPathAnimation);
            }
            mode->index = 0;
            ReadAnchorInfo(ini, name, &modes.anim_anchor);
        } else if (has(name, "Collision")) {
            g.collision_index = int8_t(camera);
            mode->type = kCamCollision;
            mode->update = ptmf(kCamUpdateNothing);
            mode->index = 0;
        } else if (has(name, "AutoDrive")) {
            if (g.autodrive_index >= 0) continue;
            g.autodrive_index = int8_t(camera);
            mode->type = kCamAutoDrive;
            mode->update = ptmf(kCamUpdateAutoDrive);
            mode->index = 0;
            modes.inertia = ini.read_float(name, "inertia", fbits(0x3e4ccccd));
            modes.zoom_factor = ini.read_float(name, "zoomFactor", fbits(0x3dcccccd));
            modes.max_weap_trans_time = ini.read_float(name, "MaxWeapTransTime", fbits(0x41f00000));
            const float inv_two_pi = fbits(0x3e22f984);
            const float pi = fbits(0x40490fda), half = 0.5f, tenth = fbits(0x3dcccccd);
            int arm = 0;
            autodrive_arms_.clear();
            for (; arm < kMaxArms; ++arm) {
                std::snprintf(arm_section, sizeof(arm_section), "%s:Arm%d", name, arm);
                if (!ini.section_exists(arm_section)) break;
                AutoDriveArmInfo a{};
                a.max_deadzone_pitch = ee::mul(ini.read_float(arm_section, "maxDeadzonePitch", tenth), inv_two_pi);
                a.max_deadzone_yaw = ee::mul(ini.read_float(arm_section, "maxDeadzoneYaw", tenth), inv_two_pi);
                a.max_autoaim_pitch = ee::mul(ini.read_float(arm_section, "maxAutoaimPitch", tenth), inv_two_pi);
                a.max_autoaim_yaw = ee::mul(ini.read_float(arm_section, "maxAutoaimYaw", tenth), inv_two_pi);
                a.autoaim_interpol_speed = ini.read_float(arm_section, "autoaimInterpolSpeed", 1.0f);
                a.rest_interpol_fall_scale = ini.read_float(arm_section, "restInterpolFallScale", fbits(0x42c80000));
                a.aim_fov = ini.read_float(arm_section, "aimFov", 0.0f);
                a.min_fov = ini.read_float(arm_section, "minFov", 0.0f);
                a.rel_pos[0] = ini.read_float(arm_section, "relPos.x", 0.0f);
                a.rel_pos[1] = ini.read_float(arm_section, "relPos.y", 1.0f);
                a.rel_pos[2] = ini.read_float(arm_section, "relPos.z", 0.0f);
                char key[0x40];
                for (int k = 0; k < 5; ++k) {
                    if (k < 1) std::snprintf(key, sizeof(key), "maxYaw");
                    else std::snprintf(key, sizeof(key), "maxYaw%d", k);
                    a.max_yaw[k] = CameraClamp(ini.read_float(arm_section, key, 0.0f), -0.5f, 0.5f);
                    if (k < 1) std::snprintf(key, sizeof(key), "minYaw");
                    else std::snprintf(key, sizeof(key), "minYaw%d", k);
                    a.min_yaw[k] = CameraClamp(ini.read_float(arm_section, key, 0.0f), -0.5f, 0.5f);
                    if (k < 1) std::snprintf(key, sizeof(key), "maxPitch");
                    else std::snprintf(key, sizeof(key), "maxPitch%d", k);
                    a.max_pitch[k] = CameraClamp(ini.read_float(arm_section, key, 0.0f), fbits(0xbf7fbe77),
                                                 fbits(0x3f7fbe77));
                    if (k < 1) std::snprintf(key, sizeof(key), "minPitch");
                    else std::snprintf(key, sizeof(key), "minPitch%d", k);
                    a.min_pitch[k] = CameraClamp(ini.read_float(arm_section, key, 0.0f), fbits(0xbf7fbe77),
                                                 fbits(0x3f7fbe77));
                }
                a.allow_deadzone = ini.read_integer(arm_section, "AllowDeadzone", 1) != 0;
                a.preserve_transform = ini.read_integer(arm_section, "PreserveTransform", 0) != 0;
                a.has_rest_pos = ini.read_integer(arm_section, "HasRestPos", 0) != 0;
                a.lock_arm_to_car = ini.read_integer(arm_section, "LockArmToCar", 0) != 0;
                a.norm_cursor_move_speed = ee::mul(ee::mul(ini.read_float(arm_section, "NormCursorMoveSpeed", 1.0f), pi), half);
                a.norm_cursor_end_move_speed =
                    ee::mul(ee::mul(ini.read_float(arm_section, "NormCursorEndMoveSpeed", 1.0f), pi), half);
                a.target_cursor_move_speed = ini.read_float(arm_section, "TargetCursorMoveSpeed", 1.0f);
                a.target_cursor_end_move_speed = ini.read_float(arm_section, "TargetCursorEndMoveSpeed", 1.0f);
                a.rotation[3] = a.rotation_from[3] = a.rotation_to[3] = 1.0f;
                ReadAnchorInfo(ini, arm_section, &a.anchor);
                ResolveWeaponNames(ini.read_string(arm_section, "weapons", nullptr), a.weapons, weapons);
                autodrive_arms_.push_back(a);
            }
            modes.autodrive_arm_count = arm;
            modes.autodrive_arms = autodrive_arms_.empty() ? nullptr : autodrive_arms_.data();
            g.autodrive_prev_weapon = 0;
        } else {
            ++guards;  // tipo desconhecido: o original só imprime (e a entrada conta)
        }
        if (has(name, "Pause")) g.pause_index = int8_t(camera);
        if (has(name, "Missile")) g.missile_index = int8_t(camera);
        ++camera;
    }
    if (g.mode_count >= 0 && size_t(g.mode_count) < modes_.size()) modes_[size_t(g.mode_count)] = modes_[0];

    CameraConstants& c = constants;
    c.max_tumble = ini.read_integer("Global", "kMaxTumble", 100);
    c.max_collision = ini.read_integer("Global", "kMaxCollision", 0x32);
    c.default_transition = ini.read_integer("Global", "kDefualtTransition", 0x3c);
    c.trans_rate = ini.read_float("Global", "kTransRate", fbits(0x3cf5c28f));
    c.trans_rate_lerp_rate = ini.read_float("Global", "kTransRateLerpRate", fbits(0x3ca3d70a));
    c.bumper_y_lerp_rate = ini.read_float("Global", "kBumperYLerpRate", fbits(0x3ecccccd));
    c.collide_radius = ini.read_float("Global", "kCollideRadius", 1.0f);
    c.spline_offset_lerp = ini.read_float("Global", "kSplineOffsetLerp", fbits(0x3c23d70a));
    c.explosion_scale = ini.read_float("Global", "kExplosionScale", fbits(0x3d0f5c29));
    c.explosion_max_shake = ini.read_float("Global", "kExplosionMaxShake", 0.25f);
    c.explosion_shake_period = ini.read_integer("Global", "kExplosionShakePeriod", 8);
    c.explosion_after_shock = ini.read_float("Global", "kExplosionAfterShock", fbits(0x3dcccccd));
    c.explosion_time_scale = ini.read_float("Global", "kExplosionTimeScale", 1.0f);
    c.object_radius_ex[0] = ini.read_float("Global", "kCameraObjectRadiusEx.x", fbits(0x3fd66666));
    c.object_radius_ex[1] = ini.read_float("Global", "kCameraObjectRadiusEx.y", fbits(0x3ff66666));
    c.object_radius_ex[2] = ini.read_float("Global", "kCameraObjectRadiusEx.z", fbits(0x3fa9999a));
    c.object_sphere_rad = ini.read_float("Global", "kCameraObjectSphereRad", fbits(0x40380000));
    c.collision_min_rate = ini.read_float("Global", "kCollisionMinRate", fbits(0x3d23d70a));
    c.collision_max_rate = ini.read_float("Global", "kCollisionMaxRate", fbits(0x3e051eb8));
    c.collision_rate_diff = ini.read_float("Global", "kCollisionRateDiff", fbits(0x3c03126f));
    c.zoom_inc_speed = ini.read_float("Global", "kZoomIncSpeed", 1.5f);
    c.centering_speed = ini.read_float("Global", "kCenteringSpeed", 0.5f);
    c.auto_drive_latency = ini.read_integer("Global", "kAutoDriveLatency", 0);
    c.weapon_arm_change_latency = ini.read_integer("Global", "kWeaponArmChangeLatency", 0x3c);
    c.missile_cam_latency = ini.read_integer("Global", "kMissileCamLatency", 0x28);
    c.weapon_animation_length = ini.read_float("Global", "kfWeaponAnimationLength", 60.0f);
    c.weapon_animation_amplitude = ini.read_float("Global", "kfWeaponAnimationAmplitude", 1.0f);
    c.max_autoaim_distance = ini.read_float("Global", "kfMaxAutoaimDistance", 300.0f);
}

}  // namespace drv
