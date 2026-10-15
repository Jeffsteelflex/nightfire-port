// nf_driving_check, parte 20, os veículos da IA (tests/driving_check_integ.h: run_integ_ai_vehicle_checks;
// docs/driving.md seção 24): a direção automática (o estado 2 do AIGroundVehicle, DoAutoDriveUpdate) e as armas dos
// motoristas (FireBullets, FireRockets, FireMissiles, TurnTurret, ClearShot), o código ORIGINAL no interpretador MIPS lado
// a lado com o porte. Incluído no fim de tests/driving_check_traffic.cpp (depois do driving_check_ai.inc da parte 17) para
// usar a mesma máquina: a rede de estradas copiada, a frota de carros na RAM emulada, os ganchos da parte 17.

namespace {

constexpr uint32_t kFnAutoDriveUpdate = 0x131080;

// O motorista sorteado como na parte 17, levado ao estado 2 como o EngageAutoDrive o deixa (a classe 1, o ataque 15, a
// navegação 4), com os campos que o DoAutoDriveUpdate usa sorteados.
void random_autodrive(Rng& rng, drv::AIGroundVehicle& v, drv::BondCar& car, const AiCarp& ac) {
    random_agent(rng, v, car, ac);
    v.controller = 2;
    v.attack_mode = 15;
    v.navigate_mode = 4;
    v.update_state = rng.chance(0.7) ? 0 : 1;
    v.unknown174 = drv::simulation().steps + rng.range(-40, 20);
    v.unknown21c = rng.chance(0.6) ? 1 : 0;
    v.unknown27c = rng.chance(0.5) ? 1 : 0;
    v.unknown15c = rng.chance(0.5) ? 0 : rng.range(0, 90);
    v.unknown268 = rng.chance(0.08) ? 1 : 0;
    drv::WRoadNav& nav = *drv::nav_of(v);
    nav.mode = rng.chance(0.5) ? 4 : 3;
}

int check_autodrive(TrafficEmu& e, Fleet& f, const AiCarp& ac, int cases, Rng& rng) {
    int failures = 0;
    Tally t_update, t_part;
    uint64_t skipped = 0;
    for (int c = 0; c < cases; ++c) {
        random_scene(rng, f);
        random_mission(rng);
        sync_mission();
        const int i = rng.range(1, int(f.cars.size()) - 1);
        drv::BondCar& car = *f.cars[size_t(i)];
        drv::AIGroundVehicle* v = drv::ai_of(car);
        if (!v) continue;
        const int saved_controller = v->controller;
        random_autodrive(rng, *v, car, ac);
        const bool whole = rng.chance(0.6);
        const std::string tag = std::string(whole ? " (Update" : " (DoAutoDriveUpdate") + ", caso " +
                                std::to_string(c) + ")";
        const int steps = rng.chance(0.5) ? rng.range(2, 15) : 1;
        f.put_all();
        put_mission(e);
        put_fleet_navs(e, f);
        Tally& t = whole ? t_update : t_part;
        for (int k = 0; k < steps; ++k) {
            ++t.cases;
            put_secondary(e, i, *v, ac);
            g_emu_calls.clear();
            const uint64_t garbage = undefined_count();
            char pre[160];
            std::snprintf(pre, sizeof pre, " [94 %d 174 %d passo %d 21c %d 27c %d 15c %d 268 %d rodas %d]",
                          v->update_state, v->unknown174, drv::simulation().steps, v->unknown21c, v->unknown27c,
                          v->unknown15c, v->unknown268, int(car.d.num_wheels_on_ground));
            if (whole) {
                e.call(kFnAIUpdate, {ai_emu(i)});
                drv::AIGroundVehicle_Update(*v);
            } else {
                e.call(kFnAutoDriveUpdate, {ai_emu(i)});
                drv::AIGroundVehicle_DoAutoDriveUpdate(*v);
            }
            if (undefined_count() != garbage) {
                --t.cases;
                ++skipped;
                break;
            }
            std::string why = same_secondary(e, i, *v, ac);
            if (why.empty()) why = f.compare(false);
            for (size_t j = 0; j < f.cars.size() && why.empty(); ++j)
                if (const drv::AIGroundVehicle* w = drv::ai_of(*f.cars[j])) why = same_agent_navs(e, int(j), *w);
            if (!why.empty()) {
                char post[160];
                std::snprintf(post, sizeof post, " depois: 21c %d/%d 27c %d/%d 210 %08x/%08x", int(e.m.read32(ai_emu(i) + 0x21c)),
                              v->unknown21c, int(e.m.read32(ai_emu(i) + 0x27c)), v->unknown27c,
                              e.m.read32(ai_emu(i) + 0x210), ps2::fbits(v->unknown210[0]));
                t.fail(why + tag + " passo " + std::to_string(k) + pre + post);
                break;
            }
            drv::simulation().steps += rng.range(0, 20);
            e.m.write32(kSim + 0xdc4, uint32_t(drv::simulation().steps));
        }
        v->controller = saved_controller;
    }
    report("AIGroundVehicle::Update (o estado 2: a direção automática)", t_update, failures);
    report("AIGroundVehicle::DoAutoDriveUpdate", t_part, failures);
    std::printf("  (fora, memória inválida ou lixo da pilha no original: %" PRIu64 ")\n", skipped);
    return failures;
}


// ---------------------------------------------------------------- as armas dos motoristas

// As chamadas ao RAnimEngine::Handle (o ProcessStimuli, o SetFrame) dos dois lados, em ordem; o IsSystemPlaying é uma
// função sorteada do (handle, sistema) que os dois lados veem igual.
struct WeaponAnimCall {
    uint32_t fn = 0, h = 0, a = 0, b = 0, c = 0, d = 0;
    bool operator==(const WeaponAnimCall& o) const {
        return fn == o.fn && h == o.h && a == o.a && b == o.b && c == o.c && d == o.d;
    }
};
std::vector<WeaponAnimCall> g_emu_wanim, g_port_wanim;
uint32_t g_playing_seed = 0;
bool fake_playing(uint32_t h, uint32_t system) { return (((system * 2654435761u) ^ h ^ g_playing_seed) >> 7) & 1; }
constexpr uint32_t kFakeHandle = 0x01e11a00;
constexpr uint32_t kFnFireBullets = 0x12f730, kFnFireRockets = 0x12fa28, kFnFireMissiles = 0x12fd58,
                   kFnTurnTurret = 0x12f170, kFnClearShot = 0x12f530, kFnDoWeaponUpdate = 0x130050;

int check_ai_weapons(TrafficEmu& e, Fleet& f, const AiCarp& ac, int cases, Rng& rng) {
    int failures = 0;
    // O original das quatro funções (a parte 17 as troca por anotações) e os ganchos desta parte.
    for (uint32_t fn : {kFnFireBullets, kFnFireRockets, kFnFireMissiles, kFnTurnTurret}) e.m.unhook(fn);
    e.m.hook(0x1a27a0, "RAnimEngine::Handle::ProcessStimuli", [](MipsMachine& mm) {
        g_emu_wanim.push_back({0x1a27a0, uint32_t(mm.gpr(R::a0)), uint32_t(mm.gpr(R::a1)), uint32_t(mm.gpr(R::a2)),
                               uint32_t(mm.gpr(R::a3)), uint32_t(mm.gpr(R::t0))});
    });
    e.m.hook(0x1a2d10, "RAnimEngine::Handle::IsSystemPlaying", [](MipsMachine& mm) {
        mm.set_gpr(R::v0, fake_playing(uint32_t(mm.gpr(R::a0)), uint32_t(mm.gpr(R::a1))) ? 1 : 0);
    });
    e.m.hook(0x1a2630, "RAnimEngine::Handle::SetFrame", [](MipsMachine& mm) {
        g_emu_wanim.push_back({0x1a2630, uint32_t(mm.gpr(R::a0)), uint32_t(mm.gpr(R::a1)), uint32_t(mm.gpr(R::a2)),
                               uint32_t(mm.gpr(R::a3)), 0});
    });
    e.m.hook(0x2c0418, "acosf", [](MipsMachine& mm) { mm.set_fpr(0, nf::fm::acos(mm.fpr(12))); });
    e.m.hook(0x169e48, "AttributeSet::LookupVector", [&f](MipsMachine& mm) {
        const uint32_t a = uint32_t(mm.gpr(R::a0)) - 0x5c;
        const drv::BondCar* c = nullptr;
        for (size_t i = 0; i < f.cars.size(); ++i)
            if (car_emu(int(i)) == a) c = f.cars[i];
        const bool found = c && c->turret_range_found;
        if (const uint32_t pf = uint32_t(mm.gpr(R::a2))) mm.write32(pf, found ? 1 : 0);
        if (c) put(mm, kAiScratch + 0x400, c->turret_range, 16);
        mm.set_gpr(R::v0, kAiScratch + 0x400);
    });
    drv::ai_weapon_hooks() = drv::AIWeaponHooks{};
    const drv::AIWeaponAnim saved_anim = drv::ai_weapon_anim();
    drv::ai_weapon_anim().stimulus = [](uint32_t h, uint32_t s, uint32_t st, uint32_t t, uint32_t m) {
        g_port_wanim.push_back({0x1a27a0, h, s, st, t, m});
    };
    drv::ai_weapon_anim().is_playing = [](uint32_t h, uint32_t s) { return fake_playing(h, s); };
    drv::ai_weapon_anim().set_frame = [](uint32_t h, uint32_t s, uint32_t st, uint32_t fr) {
        g_port_wanim.push_back({0x1a2630, h, s, st, fr, 0});
    };
    // O RSceneObj do carro no porte: o +0x28 com o mesmo handle (um número) que o do original.
    static uint8_t fake_render[0x40] = {};
    const uint32_t fh = kFakeHandle;
    std::memcpy(fake_render + 0x28, &fh, 4);
    const char* names[6] = {"AIGroundVehicle::FireBullets", "AIGroundVehicle::FireRockets",
                            "AIGroundVehicle::FireMissiles", "AIGroundVehicle::TurnTurret",
                            "AIGroundVehicle::DoWeaponUpdate (o original inteiro)", "ClearShot"};
    const uint32_t fns[5] = {kFnFireBullets, kFnFireRockets, kFnFireMissiles, kFnTurnTurret, kFnDoWeaponUpdate};
    void (*ports[5])(drv::AIGroundVehicle&) = {drv::AIGroundVehicle_FireBullets, drv::AIGroundVehicle_FireRockets,
                                              drv::AIGroundVehicle_FireMissiles, drv::AIGroundVehicle_TurnTurret,
                                              drv::AIGroundVehicle_DoWeaponUpdate};
    Tally tally[6];
    uint64_t shots_emu = 0;
    for (int c = 0; c < cases; ++c) {
        random_scene(rng, f);
        random_mission(rng);
        sync_mission();
        const int i = rng.range(1, int(f.cars.size()) - 1);
        drv::BondCar& car = *f.cars[size_t(i)];
        drv::AIGroundVehicle* v = drv::ai_of(car);
        if (!v) continue;
        const int saved_controller = v->controller;
        random_agent(rng, *v, car, ac);
        // Os campos das armas sorteados; a direção da busca perto da frente (para o tiro acontecer).
        v->unknown1cc = rng.chance(0.85) ? 1 : 0;
        v->unknown208 = rng.chance(0.85) ? 0 : 1;
        v->unknown1d0 = rng.uni(2, 200);
        v->unknown238 = uint8_t(rng.chance(0.5) ? 1 : 0);
        v->unknown226 = int16_t(rng.range(-2, 4));
        v->unknown22c = int16_t(rng.range(-2, 4));
        v->unknown230 = int16_t(rng.range(-2, 4));
        v->unknown22e = uint8_t(rng.range(0, 1));
        v->unknown232 = uint8_t(rng.range(0, 1));
        v->rate_of_fire = rng.chance(0.1) ? rng.uni(-1, 0) : rng.uni(0, 2);
        v->turret_angle = rng.chance(0.2) ? float(rng.range(0, 1)) : rng.uni(-0.1f, 1.1f);
        v->target_mode = rng.range(0, 2);
        car.turret_range_found = rng.chance(0.4);
        for (int k = 0; k < 4; ++k) car.turret_range[k] = rng.uni(-30, 400);
        const drv::RigidBody* b = car.body();
        for (int k = 0; k < 3; ++k)
            v->direction[k] = rng.chance(0.7) ? b->info->orient[2][k] + rng.uni(-0.15f, 0.15f) : rng.uni(-1, 1);
        float l = 0;
        for (int k = 0; k < 3; ++k) l += v->direction[k] * v->direction[k];
        l = std::sqrt(std::max(l, 1e-6f));
        for (int k = 0; k < 3; ++k) v->direction[k] /= l;
        // Os outros carros: alguns agentes (o ClearShot olha os de motorista no estado 5).
        std::vector<std::pair<drv::AIGroundVehicle*, int32_t>> others;
        for (size_t j = 1; j < f.cars.size(); ++j)
            if (drv::AIGroundVehicle* w = drv::ai_of(*f.cars[j]); w && int(j) != i && rng.chance(0.3)) {
                others.push_back({w, w->controller});
                w->controller = 5;
            }
        const int which = rng.range(0, 5);
        const int saved_attack = v->attack_mode;
        if (which == 4) {
            v->controller = 5;
            if (rng.chance(0.3)) v->attack_mode = rng.chance(0.5) ? 9 : 10;
        }
        for (drv::BondCar* fc : f.cars) fc->d.render_object = rng.chance(0.9) ? nf::p32_u(fake_render) : 0u;
        f.put_all();
        for (size_t j = 0; j < f.cars.size(); ++j)
            e.m.write32(car_emu(int(j)) + 0x48, f.cars[j]->d.render_object ? kRender : 0u);
        e.m.write32(kRender + 0x28, kFakeHandle);
        put_mission(e);
        put_fleet_navs(e, f);
        put_secondary(e, i, *v, ac);
        g_playing_seed = rng.next();
        g_emu_wanim.clear();
        g_port_wanim.clear();
        const uint64_t garbage = undefined_count();
        std::string why;
        if (which < 5) {
            e.call(fns[which], {ai_emu(i)});
            ports[which](*v);
        } else {
            // ClearShot direto: a frente e a posição de um carro da frota, a distância sorteada.
            float fwd[4] = {b->info->orient[2][0], b->info->orient[2][1], b->info->orient[2][2], 0};
            float pos[4] = {b->pos[0], b->pos[1], b->pos[2], 0};
            const float dist = rng.uni(1, 300);
            put(e.m, kAiScratch + 0x420, fwd, 16);
            put(e.m, kAiScratch + 0x430, pos, 16);
            e.m.set_fpr(12, dist);
            e.call(kFnClearShot, {uint32_t(uint16_t(car.d.slot)), kAiScratch + 0x420, kAiScratch + 0x430});
            const bool emu_clear = e.m.gpr(R::v0) != 0;
            if (emu_clear != drv::ClearShot(dist, car.d.slot, fwd, pos)) why = "o resultado";
        }
        const auto restore = [&]() {
            v->controller = saved_controller;
            v->attack_mode = saved_attack;
            for (auto& [w, ctl] : others) w->controller = ctl;
            for (drv::BondCar* fc : f.cars) fc->d.render_object = 0;
        };
        if (undefined_count() != garbage) {
            restore();
            continue;
        }
        ++tally[which].cases;
        if (why.empty()) why = same_secondary(e, i, *v, ac);
        if (why.empty()) why = f.compare(false);
        if (why.empty() && g_emu_wanim.size() != g_port_wanim.size())
            why = "as chamadas ao RAnimEngine: original " + std::to_string(g_emu_wanim.size()) + ", porte " +
                  std::to_string(g_port_wanim.size());
        for (size_t k = 0; why.empty() && k < g_emu_wanim.size(); ++k)
            if (!(g_emu_wanim[k] == g_port_wanim[k])) why = "a chamada ao RAnimEngine " + std::to_string(k);
        for (const WeaponAnimCall& wc : g_emu_wanim)
            if (wc.fn == 0x1a27a0 && wc.b == 10) ++shots_emu;
        if (!why.empty()) tally[which].fail(why + " (caso " + std::to_string(c) + ")");
        restore();
    }
    for (int k = 0; k < 6; ++k) report(names[k], tally[k], failures);
    std::printf("  (estímulos de tiro dados pelo original: %" PRIu64 ")\n", shots_emu);
    drv::ai_weapon_anim() = saved_anim;
    return failures;
}

}  // namespace

int run_integ_ai_vehicle_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program,
                                uint32_t vu0_size, int cases, uint32_t seed) {
    std::printf("-- a IA: os veículos (a direção automática e as armas dos motoristas)\n");
    drv::Track track;
    std::string error;
    if (!track.load(disc, "paris_mis01", "", error)) {
        std::printf("  Paris: %s\n", error.c_str());
        return 1;
    }
    drv::TrackWorld tw;
    if (!tw.init(track.root(), &error)) {
        std::printf("  o chão de Paris: %s\n", error.c_str());
        return 1;
    }
    drv::set_world(&tw);
    drv::WRoadNetwork_Shutdown();
    drv::WRoadNetwork_Init(track.root(), "paris_mis01");
    auto ep = std::make_unique<TrafficEmu>();
    TrafficEmu& e = *ep;
    e.setup(elf, vu0_program, vu0_size);
    e.m.hook(0x24e440, "fptodp", [](MipsMachine& mm) {
        const double d = double(mm.fpr(12));
        uint64_t bits = 0;
        std::memcpy(&bits, &d, 8);
        mm.set_gpr(R::v0, bits);
    });
    WorldCopy wc;
    if (!copy_world(e.m, tw, wc, kMgr, kGrid, kWorldBegin, kWorldEnd, error)) {
        std::printf("  %s\n", error.c_str());
        return 1;
    }
    RoadCopy rc;
    if (!copy_roads(e, track, rc, error)) {
        std::printf("  %s\n", error.c_str());
        return 1;
    }
    e.call(kFnRoadInit, {});
    drv::UGroup* root = track.root();
    drv::UGroup* map = drv::UGroup_GroupLocateTag(root, kTagMap);
    AiCarp ac;
    if (map != drv::UGroup_GroupEnd(root)) {
        drv::UData* d = drv::UGroup_DataLocateTag(map, 0x4149456cu);
        if (d != drv::UGroup_DataEnd(map)) {
            ac.aiel = nf::p32_u(drv::UData_Data(d));
            ac.aiel_count = int(d->count);
        }
        d = drv::UGroup_DataLocateTag(map, 0x4149436fu);
        if (d != drv::UGroup_DataEnd(map)) {
            ac.aico = nf::p32_u(drv::UData_Data(d));
            ac.aico_count = int(d->count);
        }
    }
    drv::SimRandom_Reset(drv::sim_random());
    drv::mission_reset();
    drv::SMissionManager_Construct(false);
    Rng rng(seed ^ 0x2017u);
    int failures = 0;
    Fleet fleet;
    fleet.e = &e;
    fleet.rc = &rc;
    fleet.tw = &tw;
    fleet.wc = &wc;
    try {
        drv::AIVehicleController_Restart();
        if (!build_fleet(fleet, disc, error)) {
            std::printf("  a frota: %s\n", error.c_str());
            ++failures;
        } else {
            hook_fleet(e, fleet);
            hook_ai(e, fleet);
            e.call(kFnZoneInit, {});
            drv::AIZoneController_Init();
            construct_agent_navs(e);
            for (size_t si = 0; si < drv::road_network().segments.size(); ++si) {
                const uint32_t seg = drv::road_network().segments[si];
                if (!(nf::p32_ptr<const drv::WRoadSegment>(seg)->flags & drv::kSegCurved)) continue;
                const float zero[4] = {0, 0, 0, 0};
                put(e.m, kAiScratch, zero, 16);
                put(e.m, kAiScratch + 0x10, zero, 16);
                e.m.set_fpr(12, 0.5f);
                e.m.set_gpr(R::t0, kAiScratch + 0x20);
                e.call(kFnPoint, {1, kAiScratch, kAiScratch + 0x10, rc.to_emu(seg)});
                break;
            }
            fleet.heap_mark = e.heap;
            failures += check_autodrive(e, fleet, ac, std::max(1, cases / 4), rng);
            failures += check_ai_weapons(e, fleet, ac, std::max(1, cases / 2), rng);
        }
    } catch (const nf::MipsError& err) {
        std::printf("  erro no interpretador: %s\n", err.what());
        ++failures;
    }
    drv::AIElementController_Destruct();
    drv::mission_reset();
    drv::traffic_world().camera = nullptr;
    drv::WTriggerManager_Reset();
    for (drv::BondCar* car : fleet.cars) delete car;
    drv::simulation().cars.clear();
    drv::simulation_reset(60);
    drv::set_world(nullptr);
    return failures;
}
