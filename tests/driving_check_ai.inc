// nf_driving_check, parte 17 (tests/driving_check_traffic.h: run_ai_checks; docs/driving.md seção 21): a IA dos elementos
// de Paris e o dano do carro, o código ORIGINAL do DRIVING.ELF no interpretador MIPS (com a VU0 em modo macro) lado a lado
// com o porte. Incluído no fim de tests/driving_check_traffic.cpp para usar a mesma máquina da parte 14 (a rede de
// estradas copiada, o navegador, a frota de carros na RAM emulada com os ganchos).
//
//  a) As peças sem estado: FindOBBIntersect, BytesToCoord, CARP::BaseDesc::CalcDamageZone e as zonas vizinhas,
//     InCollisionRange, o AIZoneController (Init, RegisterZone, InZone).
//  b) O navegador nos modos dos agentes: CalcNextSegmentRandom (4), CalcNextSegmentDirection (3 e 5, rumo a um trecho),
//     IncNavPosition nesses modos, PathShareRoadSegment e PathForwardRoadSegment.
//  c) O dano: PBondCar::ApplyDamage (os quatro tipos, os pneus, o escudo, a imunidade, os mortos, os submarinos e o
//     helicóptero, a força do tiro), PBondCar::SetVisualDamage e PBondCar::AttackWithEmp. O que vai para fora (a missão,
//     o HUD, o RAnimEngine, o RVehicle::TriggerFX, o som) é anotado nos dois lados e comparado em ordem.
//  d) Os agentes: AIGroundVehicle::Update no estado 5 (UpdateStuck, DoAgentStateUpdate, DoAttackMode 13-15,
//     DoWeaponUpdate, DoMoveMode 1-4 com os pesos das faixas e o DriveToPoint) com os carros da frota como agentes.
//  e) Os comandos: EAICommand::~EAICommand (o comando 'AICo' de Paris sorteado aplicado a um motorista), e o
//     ESetRecSpeedOffset.
//  f) O AIElementController sobre o 'AIEl' de Paris: o construtor, os acompanhamentos (Update e UpdatePosition),
//     o Update do controlador (o SpawnCarObject trocado por um que não acha lugar: ninguém acorda de verdade), o
//     CompareInRangePrio, o RedirectCarpElement, o CalcMightAsWellBeDead e o OkToSpawnHench.

namespace {

// As funções originais da parte 17.
constexpr uint32_t kFnObbIntersect = 0x2d3950, kFnBytesToCoord = 0x2d0b08, kFnCalcZone = 0x2db798;
constexpr uint32_t kFnZoneNeighbours = 0x2db930, kFnInCollisionRange = 0x135b10;
constexpr uint32_t kFnZoneInit = 0x140110, kFnZoneIn = 0x140218, kFnZoneRegister = 0x1402f8;
constexpr uint32_t kFnNextRandom = 0x2235c8, kFnNextDirection = 0x223a30;
constexpr uint32_t kFnPathShare = 0x226bd8, kFnPathForward = 0x226c88;
constexpr uint32_t kFnApplyDamage = 0x189598, kFnSetVisualDamage = 0x1876c8, kFnAttackWithEmp = 0x18ac28;
constexpr uint32_t kFnAICommandDtor = 0x145ff8, kFnRecSpeedDtor = 0x1587f8;
constexpr uint32_t kFnCtlConstruct = 0x12be48, kFnCtlUpdate = 0x12bec8, kFnTrackUpdate = 0x12afb8;
constexpr uint32_t kFnTrackPosition = 0x12b440, kFnPrio = 0x12dc18, kFnRedirect = 0x12c608;
constexpr uint32_t kFnMightDead = 0x12c678, kFnOkHench = 0x12c5d0;
// Os globais.
constexpr uint32_t kZoneTable = 0x32b768, kZoneActive = 0x32bab0, kZoneInstance = 0x32b760;
constexpr uint32_t kZoneDuration = 0x4230d8, kZoneLimit = 0x4230e8, kZoneRadius = 0x4230f8;
constexpr uint32_t kCtlObj = 0x327c30, kHardLimit = 0x327c2c, kActorDb = 0x326000;
// A RAM emulada da parte 17.
constexpr uint32_t kAiScratch = 0x01e10000;   // entradas e saídas das funções puras
constexpr uint32_t kDmgRender = 0x01e11000, kDmgRenderVt = 0x01e11100, kDmgBase = 0x01e11400;
constexpr uint32_t kAudioVt = 0x01e11500, kSoundBuf = 0x01e11600, kDmgSig = 0x01e11900;
constexpr uint32_t kFnRenderOffset = 0x01e11a00, kFnTriggerFx = 0x01e11a10, kFnAudioCall = 0x01e11a20;
constexpr uint32_t kAgentNavs = 0x01e20000;   // 8 x 0x600: os navegadores 2-5 de cada motorista (0x140 cada)
constexpr uint32_t kEventObj = 0x01e30000, kAicoCopy = 0x01e31000;  // o objeto do evento; os 'AICo' copiados
constexpr uint32_t kAielCopy = 0x01e40000, kAielUData = 0x01e3ff00;  // os 'AIEl' copiados; o UData falso

uint32_t agent_nav_emu(int car, int k) { return kAgentNavs + 0x600u * uint32_t(car) + 0x140u * uint32_t(k); }

// As chamadas para fora anotadas pelo original (os ganchos) e pelo porte (car_damage_stats().calls_out).
std::vector<drv::CarDamageCall> g_emu_calls;
void emu_note(uint32_t fn, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
    g_emu_calls.push_back({fn, {a0, a1, a2, a3}});
}
std::string same_calls(const std::vector<drv::CarDamageCall>& port) {
    const size_t n = std::max(g_emu_calls.size(), port.size());
    for (size_t i = 0; i < n; ++i) {
        const drv::CarDamageCall* a = i < g_emu_calls.size() ? &g_emu_calls[i] : nullptr;
        const drv::CarDamageCall* b = i < port.size() ? &port[i] : nullptr;
        if (a && b && a->fn == b->fn && std::memcmp(a->a, b->a, sizeof a->a) == 0) continue;
        auto txt = [](const drv::CarDamageCall* c) {
            if (!c) return std::string("nada");
            return hex(c->fn) + "(" + hex(c->a[0]) + "," + hex(c->a[1]) + "," + hex(c->a[2]) + "," + hex(c->a[3]) + ")";
        };
        return "a chamada " + std::to_string(i) + ": original " + txt(a) + ", porte " + txt(b);
    }
    return std::string();
}

// O carro da frota cujo PBondCar emulado está em `addr`.
drv::BondCar* fleet_car_at(Fleet& f, uint32_t addr) {
    for (size_t i = 0; i < f.cars.size(); ++i)
        if (car_emu(int(i)) == addr) return f.cars[i];
    return nullptr;
}

// Os ganchos da parte 17 (depois dos da frota).
void hook_ai(TrafficEmu& e, Fleet& f) {
    MipsMachine& m = e.m;
    m.hook(0x2068b8, "SMissionManager::IncPlayerDamage", [](MipsMachine& mm) { emu_note(0x2068b8, fbits(mm.fpr(12))); });
    m.hook(0x206900, "SMissionManager::IncShotsHit",
           [](MipsMachine& mm) { emu_note(0x206900, uint32_t(mm.gpr(R::a1)) & 0xff); });
    m.hook(0x206960, "SMissionManager::IncKills", [](MipsMachine& mm) { emu_note(0x206960, uint32_t(mm.gpr(R::a1))); });
    m.hook(0x208540, "SMissionManager::ProgrammerDefinedEvent",
           [](MipsMachine& mm) { emu_note(0x208540, uint32_t(mm.gpr(R::a1))); });
    m.hook(0x23c618, "GHud::TheApp", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0x01c1d000); });
    m.hook(0x23e270, "GHud::TriggerDamageFlash", [](MipsMachine& mm) { emu_note(0x23e270, fbits(mm.fpr(12))); });
    m.hook(0x1a27a0, "RAnimEngine::Handle::ProcessStimuli (o sistema)", [](MipsMachine& mm) {
        emu_note(0x1a27a0, uint32_t(mm.gpr(R::a1)), uint32_t(mm.gpr(R::a2)), uint32_t(mm.gpr(R::a3)),
                 uint32_t(mm.gpr(R::t0)));
    });
    m.hook(0x1a2868, "RAnimEngine::Handle::ProcessStimuli", [](MipsMachine& mm) {
        emu_note(0x1a2868, uint32_t(mm.gpr(R::a1)), uint32_t(mm.gpr(R::a2)), uint32_t(mm.gpr(R::a3)));
    });
    m.hook(0x1a2b50, "RAnimEngine::Handle::ProcessStimuliZones", [](MipsMachine& mm) {
        emu_note(0x1a2b50, uint32_t(mm.gpr(R::a1)), uint32_t(mm.gpr(R::a2)), uint32_t(mm.gpr(R::a3)),
                 uint32_t(mm.gpr(R::t0)));
    });
    m.hook(0x2fca38, "ABaseSound::operator new", [](MipsMachine& mm) {
        emu_note(0x2fca38);
        std::memset(mm.mem(kSoundBuf, 0x200), 0, 0x200);
        mm.set_gpr(R::v0, kSoundBuf);
    });
    m.hook(0x2f5880, "AIndex::Lookup", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
    m.hook(0x2f2d50, "AMix::Add", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
    m.hook(0x2dd790, "AVoice::AVoice", [](MipsMachine&) {});
    m.hook(0x12da78, "AIElementController::ForceVehicleToSleep", [](MipsMachine&) { emu_note(0x12da78); });
    m.hook(0x2f4b10, "AMenuSound::Trigger", [](MipsMachine& mm) { emu_note(0x2f4b10, uint32_t(mm.gpr(R::a0))); });
    m.hook(0x1d2bd8, "RVehicle::SetEMPVictimEffect", [](MipsMachine& mm) { emu_note(0x1d2bd8, uint32_t(mm.gpr(R::a1))); });
    m.hook(0x1d2c18, "RVehicle::TriggerEmpBolts", [](MipsMachine&) { emu_note(0x1d2c18); });
    m.hook(0x2a9898, "strncmp", [](MipsMachine& mm) {
        const char* a = reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a0)), 1));
        const char* b = reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a1)), 1));
        mm.set_gpr(R::v0, uint64_t(int64_t(std::strncmp(a, b, size_t(mm.gpr(R::a2))))));
    });
    // O render do carro do dano: o RSceneObj::GetBoundingDimensions, o GetRenderOffset (+0x54) e o TriggerFX (+0x84).
    m.hook(0x1c9778, "RSceneObj::GetBoundingDimensions", [&f](MipsMachine& mm) {
        const uint32_t owner = mm.read32(uint32_t(mm.gpr(R::a0)) + 0x30);
        const drv::BondCar* c = fleet_car_at(f, owner);
        float h[4] = {0, 0, 0, 0};
        if (c) std::memcpy(h, c->desc_half, 12);
        std::memcpy(mm.mem(uint32_t(mm.gpr(R::a1)), 16), h, 16);
    });
    m.hook(kFnRenderOffset, "RVehicle::GetRenderOffset", [&f](MipsMachine& mm) {
        const uint32_t owner = mm.read32(uint32_t(mm.gpr(R::a0)) + 0x30);
        const drv::BondCar* c = fleet_car_at(f, owner);
        mm.set_fpr(0, c ? c->desc_render_offset : 0.0f);
    });
    m.hook(kFnTriggerFx, "RVehicle::TriggerFX",
           [](MipsMachine& mm) { emu_note(0x1d3c28, uint32_t(mm.gpr(R::a1)), uint32_t(mm.gpr(R::a2))); });
    m.hook(kFnAudioCall, "o som do carro (+0x388, a queda do helicóptero)", [](MipsMachine&) { emu_note(0x388); });
    for (uint32_t off = 0; off < 0x200; off += 8) {
        m.write32(kDmgRenderVt + off, 0);
        m.write32(kDmgRenderVt + off + 4, kFnFake);
    }
    m.write32(kDmgRenderVt + 0x54, kFnRenderOffset);
    m.write32(kDmgRenderVt + 0x84, kFnTriggerFx);
    m.write32(kDmgRender + 0x3c, kDmgRenderVt);
    m.write32(kDmgRender + 0x0c, kDmgBase);
    m.write32(kDmgRender + 0x28, 0x01e11800);  // o Handle do RAnimEngine (só um número)
    for (uint32_t off = 0; off < 0x100; off += 4) m.write32(kAudioVt + off, 0);
    m.write32(kAudioVt + 0x2c, kFnAudioCall);
    m.write32(kAudio + 0xac, kAudioVt);
    m.write32(kDmgSig, 0);
    // AttributeSet::LookupInt ("IS_HELI") e o LookupBool das chaves que a IA e o dano perguntam.
    auto car_of_attr = [&f](MipsMachine& mm) -> drv::BondCar* {
        const uint32_t a = uint32_t(mm.gpr(R::a0)) - 0x5c;
        for (size_t i = 0; i < f.cars.size(); ++i)
            if (car_emu(int(i)) == a) return f.cars[i];
        return nullptr;
    };
    m.hook(0x169bf0, "AttributeSet::LookupInt", [car_of_attr](MipsMachine& mm) {
        drv::BondCar* c = car_of_attr(mm);
        const char* key = reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a1)), 1));
        mm.set_gpr(R::v0, c && std::strcmp(key, "IS_HELI") == 0 && c->is_heli ? 1 : 0);
    });
    m.hook(0x169b28, "AttributeSet::LookupBool", [car_of_attr](MipsMachine& mm) {
        drv::BondCar* c = car_of_attr(mm);
        const char* key = reinterpret_cast<const char*>(mm.mem(uint32_t(mm.gpr(R::a1)), 1));
        bool v = false;
        if (c && std::strcmp(key, "IS_HELI") == 0) v = c->is_heli;
        else if (c && std::strcmp(key, "HAS_SIREN") == 0) v = c->has_siren;
        else if (c) v = c->attr_bool(key);
        mm.set_gpr(R::v0, v ? 1 : 0);
    });
    // As armas dos agentes (driving/ai/ai_weapons.h: sem ganchos no porte, só contam).
    m.hook(0x12f730, "AIGroundVehicle::FireBullets", [](MipsMachine&) { emu_note(0x12f730); });
    m.hook(0x12fa28, "AIGroundVehicle::FireRockets", [](MipsMachine&) { emu_note(0x12fa28); });
    m.hook(0x12fd58, "AIGroundVehicle::FireMissiles", [](MipsMachine&) { emu_note(0x12fd58); });
    m.hook(0x12f170, "AIGroundVehicle::TurnTurret", [](MipsMachine&) { emu_note(0x12f170); });
    // E no porte só a contagem (as funções de verdade, conferidas na parte 20, mexeriam nos prazos e no RAnimEngine).
    drv::ai_weapon_hooks().fire_bullets = [](drv::AIGroundVehicle&) {};
    drv::ai_weapon_hooks().fire_rockets = [](drv::AIGroundVehicle&) {};
    drv::ai_weapon_hooks().fire_missiles = [](drv::AIGroundVehicle&) {};
    drv::ai_weapon_hooks().turn_turret = [](drv::AIGroundVehicle&) {};
    // O jogador para o SetTargetMode (o motorista do carro 0 da frota).
    m.hook(0x13ebc0, "AIVehicleController::GetPlayerGroundVehiclePtr", [&f](MipsMachine& mm) {
        mm.set_gpr(R::v0, f.ai_addr(nf::p32_u(drv::AIVehicleController_GetPlayerGroundVehicle(0))));
    });
    m.hook(0x13e678, "AIVehicleController::Get", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0x01c1f010); });
    m.write32(kSim + 0xdfc, 0x01c1d800);
    m.write32(kSim + 0xe00, 0x01c1d800);
}

// ---------------------------------------------------------------- a) as peças sem estado

int check_pieces(TrafficEmu& e, int cases, Rng& rng) {
    int failures = 0;
    Tally t_obb, t_bytes, t_zone, t_neigh, t_range, t_zones;
    for (int c = 0; c < cases; ++c) {
        // FindOBBIntersect: uma caixa e um segmento (de dentro, de fora, paralelo a um eixo).
        {
            ++t_obb.cases;
            float half[4] = {rng.uni(0.3f, 3), rng.uni(0.3f, 2), rng.uni(0.5f, 5), 0};
            float a[4], b[4];
            for (int i = 0; i < 3; ++i) {
                a[i] = rng.chance(0.4) ? rng.uni(-half[i], half[i]) : rng.uni(-8, 8);
                b[i] = rng.uni(-8, 8);
            }
            a[3] = b[3] = 0;
            if (rng.chance(0.1)) b[rng.range(0, 2)] = a[0];
            if (rng.chance(0.05)) std::memcpy(b, a, 16);
            float out[4] = {rng.uni(-1, 1), rng.uni(-1, 1), rng.uni(-1, 1), rng.uni(-1, 1)};
            put(e.m, kAiScratch, half, 16);
            put(e.m, kAiScratch + 0x10, a, 16);
            put(e.m, kAiScratch + 0x20, b, 16);
            put(e.m, kAiScratch + 0x30, out, 16);
            e.call(kFnObbIntersect, {kAiScratch, kAiScratch + 0x10, kAiScratch + 0x20, kAiScratch + 0x30});
            const bool r = drv::FindOBBIntersect(half, a, b, out);
            std::string why;
            if ((e.m.gpr(R::v0) != 0) != r) t_obb.fail("o retorno");
            else if (!same_words("o ponto", e.m.mem(kAiScratch + 0x30, 16), reinterpret_cast<const uint8_t*>(out), 12, {},
                                 why))
                t_obb.fail(why);
        }
        // BytesToCoord com e sem cada parcela.
        {
            ++t_bytes.cases;
            const uint32_t w = rng.next() ^ (rng.next() << 16);
            float add[4] = {rng.uni(-1, 1), rng.uni(-1, 1), rng.uni(-1, 1), 0};
            float sc[4] = {rng.uni(-9000, 9000), rng.uni(-9000, 9000), rng.uni(-9000, 9000), 0};
            float add2[4] = {rng.uni(-5, 5), rng.uni(-5, 5), rng.uni(-5, 5), 0};
            const bool ua = rng.chance(0.8), us = rng.chance(0.8), u2 = rng.chance(0.3);
            float out[4] = {0, 0, 0, 0};
            put(e.m, kAiScratch, add, 16);
            put(e.m, kAiScratch + 0x10, sc, 16);
            put(e.m, kAiScratch + 0x20, add2, 16);
            put(e.m, kAiScratch + 0x30, out, 16);
            e.m.set_gpr(R::t0, kAiScratch + 0x30);
            e.call(kFnBytesToCoord, {w, ua ? kAiScratch : 0u, us ? kAiScratch + 0x10 : 0u, u2 ? kAiScratch + 0x20 : 0u,
                                     kAiScratch + 0x30});
            drv::BytesToCoord(w, ua ? add : nullptr, us ? sc : nullptr, u2 ? add2 : nullptr, out);
            std::string why;
            if (!same_words("BytesToCoord", e.m.mem(kAiScratch + 0x30, 16), reinterpret_cast<const uint8_t*>(out), 12, {},
                            why))
                t_bytes.fail(why);
        }
        // CalcDamageZone: uma caixa de zonas (às vezes torta) e um ponto perto dela.
        {
            ++t_zone.cases;
            uint8_t base[0x60] = {};
            float lo[3], hi[3];
            for (int i = 0; i < 3; ++i) {
                lo[i] = rng.uni(-3, 0);
                hi[i] = rng.chance(0.05) ? rng.uni(-3, 0) : rng.uni(0, 3);
            }
            std::memcpy(base + 0x40, lo, 12);
            std::memcpy(base + 0x50, hi, 12);
            float p[4] = {rng.uni(-4, 4), rng.uni(-4, 4), rng.uni(-4, 4), 0};
            if (rng.chance(0.1)) p[0] = lo[0];
            put(e.m, kAiScratch + 0x100, base, sizeof base);
            put(e.m, kAiScratch, p, 16);
            e.call(kFnCalcZone, {kAiScratch + 0x100, kAiScratch});
            const int32_t z = drv::CalcDamageZone(lo, hi, p);
            if (int32_t(e.m.gpr(R::v0)) != z)
                t_zone.fail("a zona: original " + std::to_string(int32_t(e.m.gpr(R::v0))) + ", porte " + std::to_string(z));
            ++t_neigh.cases;
            e.call(kFnZoneNeighbours, {kAiScratch + 0x100, uint32_t(z)});
            if (uint32_t(e.m.gpr(R::v0) & 0xffff) != drv::DamageZoneNeighbours(z)) t_neigh.fail("as vizinhas");
        }
        // InCollisionRange.
        {
            ++t_range.cases;
            float a[4] = {rng.uni(-50, 50), rng.uni(-5, 5), rng.uni(-50, 50), 0};
            float b[4] = {a[0] + rng.uni(-30, 30), a[1] + rng.uni(-8, 8), a[2] + rng.uni(-30, 30), 0};
            float d[4] = {rng.uni(-1, 1), rng.uni(-0.2f, 0.2f), rng.uni(-1, 1), 0};
            const float sp = rng.uni(-5, 40), ot = rng.uni(0, 10);
            const bool any = rng.chance(0.3);
            put(e.m, kAiScratch, a, 16);
            put(e.m, kAiScratch + 0x10, b, 16);
            put(e.m, kAiScratch + 0x20, d, 16);
            e.m.set_fpr(12, sp);
            e.m.set_fpr(13, ot);
            e.call(kFnInCollisionRange, {kAiScratch, kAiScratch + 0x10, kAiScratch + 0x20, any ? 1u : 0u});
            const bool r = drv::InCollisionRange(a, b, d, sp, ot, any);
            if ((e.m.gpr(R::v0) != 0) != r) t_range.fail("o retorno");
        }
    }
    // O AIZoneController: Init, depois zonas registradas e consultadas.
    {
        e.call(kFnZoneInit, {});
        drv::AIZoneController_Init();
        drv::AIZoneController_Active() = 0;
        std::memset(drv::AIZoneController_Zones(), 0, 30 * 0x1c);
        std::memset(e.m.mem(kZoneTable, 30 * 0x1c), 0, 30 * 0x1c);
        e.m.write32(kZoneActive, e.m.read32(kZoneActive) & 0xffff0000u);
        for (int c = 0; c < cases; ++c) {
            ++t_zones.cases;
            float p[4] = {rng.uni(-60, 60), rng.uni(-2, 2), rng.uni(-60, 60), 0};
            put(e.m, kAiScratch, p, 16);
            std::string why;
            if (rng.chance(0.4)) {
                const int type = rng.range(-1, 8);
                e.call(kFnZoneRegister, {e.m.read32(kZoneInstance), uint32_t(type), kAiScratch});
                drv::AIZoneController_RegisterZone(type, p);
                uint16_t act = 0;
                std::memcpy(&act, e.m.mem(kZoneActive, 2), 2);
                if (!same_words("as zonas", e.m.mem(kZoneTable, 30 * 0x1c), drv::AIZoneController_Zones(), 30 * 0x1c,
                                {}, why))
                    t_zones.fail(why);
                else if (int32_t(int16_t(act)) != drv::AIZoneController_Active())
                    t_zones.fail("fActiveZones");
            } else {
                const int z = rng.range(0, 29);
                const float extra = rng.uni(0, 400);
                e.m.set_fpr(12, extra);
                e.call(kFnZoneIn, {e.m.read32(kZoneInstance), uint32_t(z), kAiScratch});
                if ((e.m.gpr(R::v0) != 0) != drv::AIZoneController_InZone(extra, z, p)) t_zones.fail("InZone");
            }
        }
    }
    report("FindOBBIntersect", t_obb, failures);
    report("BytesToCoord", t_bytes, failures);
    report("CARP::BaseDesc::CalcDamageZone", t_zone, failures);
    report("as zonas vizinhas (0x002db930)", t_neigh, failures);
    report("InCollisionRange", t_range, failures);
    report("AIZoneController (Init, RegisterZone, InZone)", t_zones, failures);
    return failures;
}

// ---------------------------------------------------------------- b) o navegador nos modos dos agentes

int check_agent_nav(TrafficEmu& e, const RoadCopy& rc, int cases, Rng& rng) {
    int failures = 0;
    drv::RoadNetworkGlobals& r = drv::road_network();
    e.call(kFnNavCtor, {kNav});
    NavCopy nc{&e, &rc, e.m.read32(kNav + 0x90)};
    drv::WRoadNav nav{};
    drv::WRoadNav_Construct(nav);
    Tally t_init, t_random, t_dir, t_inc, t_share, t_fwd;
    uint64_t moved = 0, zero_loops = 0;
    for (int c = 0; c < cases; ++c) {
        const int si = rng.range(0, r.num_segments - 1);
        float p[4] = {0, 0, 0, 0};
        drv::WRoadNetwork_GetPointOnSegmentT(drv::road_segment(si), rng.uni(0, 1), p);
        float h[4] = {rng.uni(-1, 1), rng.uni(-0.2f, 0.2f), rng.uni(-1, 1), 0};
        nav.mode = rng.chance(0.5) ? 4 : 3;
        drv::WRoadNav_InitAtPoint(nav, p, h, false);
        if (nav.segment < 0) continue;
        nav.unknownb1 = rng.chance(0.3) ? 1 : 0;
        nav.unknownb2 = rng.chance(0.2) ? 1 : 0;
        nav.next_choice = int8_t(rng.chance(0.15) ? rng.range(0, 3) : -1);
        nav.forward = int8_t(rng.range(0, 1));
        std::string why;
        // InitAtPoint num ponto qualquer (perto da rede ou a quilômetros dela), nos modos dos agentes.
        {
            float q[4] = {p[0], p[1], p[2], 0};
            if (rng.chance(0.5))
                for (int k = 0; k < 3; k += 2) q[k] = rng.chance(0.5) ? rng.uni(-4000, 4000) : p[k] + rng.uni(-60, 60);
            float qh[4] = {rng.uni(-1, 1), rng.uni(-0.2f, 0.2f), rng.uni(-1, 1), 0};
            drv::WRoadNav probe = nav;
            uint8_t spline[0x64];
            std::memcpy(spline, drv::road_nav_spline(nav), sizeof spline);
            probe.mode = rng.chance(0.5) ? 3 : 4;
            const bool flag = rng.chance(0.5);
            nc.put_nav(probe);
            put_random(e);
            put_stamp(e);
            put(e.m, kAiScratch + 0x60, q, 16);
            put(e.m, kAiScratch + 0x70, qh, 16);
            ++t_init.cases;
            e.call(kFnInitAtPoint, {kNav, kAiScratch + 0x60, kAiScratch + 0x70, flag ? 1u : 0u});
            drv::WRoadNav_InitAtPoint(probe, q, qh, flag);
            std::string w;
            if (!nc.same(probe, w) || !same_stamp(e, w)) t_init.fail(w);
            std::memcpy(drv::road_nav_spline(nav), spline, sizeof spline);  // o navegador do teste segue como estava
        }
        // CalcNextSegmentRandom / CalcNextSegmentDirection direto.
        {
            float hd[4] = {rng.uni(-1, 1), rng.uni(-0.3f, 0.3f), rng.uni(-1, 1), 0};
            if (rng.chance(0.05)) hd[0] = hd[1] = hd[2] = 0;
            int8_t ahead = int8_t(rng.range(0, 1));
            int16_t target = int16_t(rng.chance(0.4) ? -1 : rng.range(0, r.num_segments - 1));
            nc.put_nav(nav);
            put_random(e);
            put(e.m, kAiScratch, hd, 16);
            e.m.write32(kAiScratch + 0x10, uint32_t(uint8_t(ahead)));
            e.m.write32(kAiScratch + 0x14, uint32_t(uint16_t(target)));
            e.m.write32(kAiScratch + 0x18, 0);
            const bool random = rng.chance(0.5);
            int16_t got;
            if (random) {
                ++t_random.cases;
                e.call(kFnNextRandom, {kNav, kAiScratch, kAiScratch + 0x10, kAiScratch + 0x18});
                got = drv::WRoadNav_CalcNextSegmentRandom(nav, hd, &ahead);
            } else {
                ++t_dir.cases;
                e.m.set_gpr(R::t0, kAiScratch + 0x18);
                e.call(kFnNextDirection, {kNav, kAiScratch, kAiScratch + 0x14, kAiScratch + 0x10, kAiScratch + 0x18});
                got = drv::WRoadNav_CalcNextSegmentDirection(nav, hd, &target, &ahead);
            }
            Tally& t = random ? t_random : t_dir;
            if (int16_t(e.m.gpr(R::v0)) != got)
                t.fail("o trecho: original " + std::to_string(int16_t(e.m.gpr(R::v0))) + ", porte " + std::to_string(got));
            else if (int8_t(e.m.mem(kAiScratch + 0x10, 1)[0]) != ahead)
                t.fail("o sentido");
            else if (!same_words("a direção", e.m.mem(kAiScratch, 16), reinterpret_cast<const uint8_t*>(hd), 12, {}, why))
                t.fail(why);
            else if (!nc.same(nav, why) || !same_random(e, why))
                t.fail(why);
        }
        // IncNavPosition nos modos 3, 4 e 5 (uma sequência).
        nav.mode = rng.chance(0.5) ? 4 : (rng.chance(0.7) ? 3 : 5);
        const int ops = rng.range(3, 20);
        for (int k = 0; k < ops; ++k) {
            ++t_inc.cases;
            nc.put_nav(nav);
            put_random(e);
            put_stamp(e);
            const float step = rng.chance(0.05) ? 0.0f : rng.uni(0, rng.chance(0.2) ? 150.0f : 20.0f);
            float hd[4] = {rng.uni(-1, 1), 0, rng.uni(-1, 1), 0};
            const int16_t target = int16_t(rng.chance(0.4) ? -1 : rng.range(0, r.num_segments - 1));
            put(e.m, kScratch + 0x20, hd, 16);
            e.m.set_fpr(12, step);
            const int16_t before = nav.segment;
            // O porte primeiro: num laço de trechos de comprimento zero o original recorreria até estourar a pilha
            // (o porte para numa guarda): esses casos ficam de fora.
            const drv::WRoadNav saved = nav;
            uint8_t saved_spline[0x64];
            std::memcpy(saved_spline, drv::road_nav_spline(nav), sizeof saved_spline);
            const drv::SimRandom saved_random = drv::sim_random();
            const uint64_t guards = drv::road_nav_stats().guards;
            drv::WRoadNav_IncNavPosition(nav, step, hd, target);
            if (drv::road_nav_stats().guards != guards) {
                nav = saved;
                std::memcpy(drv::road_nav_spline(nav), saved_spline, sizeof saved_spline);
                drv::sim_random() = saved_random;
                --t_inc.cases;
                ++zero_loops;
                break;
            }
            e.call(kFnIncNav, {kNav, kScratch + 0x20, uint32_t(int32_t(target))});
            if (nav.segment != before) ++moved;
            if (!nc.same(nav, why) || !same_random(e, why)) {
                t_inc.fail(why + " (modo " + std::to_string(nav.mode) + ")");
                break;
            }
        }
        // PathShareRoadSegment / PathForwardRoadSegment.
        {
            const int16_t s2 = int16_t(rng.range(0, r.num_segments - 1));
            nc.put_nav(nav);
            ++t_share.cases;
            e.call(kFnPathShare, {kNav, uint32_t(int32_t(s2))});
            if ((e.m.gpr(R::v0) != 0) != drv::WRoadNav_PathShareRoadSegment(nav, s2)) t_share.fail("o retorno");
            ++t_fwd.cases;
            const uint32_t junk = rng.next();
            e.m.write32(kAiScratch + 0x40, junk);
            int32_t out = int32_t(junk);
            e.call(kFnPathForward, {kNav, uint32_t(int32_t(s2)), kAiScratch + 0x40});
            const int32_t rv = drv::WRoadNav_PathForwardRoadSegment(nav, s2, &out);
            if (int32_t(e.m.gpr(R::v0)) != rv) t_fwd.fail("o retorno");
            else if (int32_t(e.m.read32(kAiScratch + 0x40)) != out) t_fwd.fail("a palavra de saída");
        }
    }
    report("WRoadNav::InitAtPoint (os modos 3 e 4, pontos longe da rede)", t_init, failures);
    report("WRoadNav::CalcNextSegmentRandom (o modo 4)", t_random, failures);
    report("WRoadNav::CalcNextSegmentDirection (os modos 3 e 5)", t_dir, failures);
    report("WRoadNav::IncNavPosition (os modos 3, 4 e 5)", t_inc, failures);
    report("WRoadNav::PathShareRoadSegment", t_share, failures);
    report("WRoadNav::PathForwardRoadSegment", t_fwd, failures);
    std::printf("  (trocas de trecho %" PRIu64 ", laços de trechos de comprimento zero fora %" PRIu64 ")\n", moved,
                zero_loops);
    drv::WRoadNav_Destruct(nav);
    return failures;
}

}  // namespace

namespace {

constexpr uint32_t kFnCtlCtor = 0x12b8b0;   // AIElementController::AIElementController(UData *)
constexpr uint32_t kSettingsEmu = 0x01e11c00;  // a cópia das MissionSettings do porte (0x2f8)
constexpr uint32_t kCtlEmu = 0x01e50000;       // o AIElementController emulado (0x58)
constexpr uint32_t kCtlHeap = 0x01e50100, kCtlHeapEnd = 0x01ef0000;  // o operator new do construtor

struct AiCarp {
    uint32_t aiel = 0, aico = 0;  // (p32) os registros do porte
    int aiel_count = 0, aico_count = 0;
    uint32_t aiel_emu(uint32_t p32) const {
        if (!p32 || !aiel || p32 < aiel || (p32 - aiel) % 0x100 || int((p32 - aiel) / 0x100) >= aiel_count) return 0;
        return kAielCopy + (p32 - aiel);
    }
    uint32_t aico_emu(uint32_t p32) const {
        if (!p32 || !aico || p32 < aico || (p32 - aico) % 0xa0 || int((p32 - aico) / 0xa0) >= aico_count) return 0;
        return kAicoCopy + (p32 - aico);
    }
};

// O +0x204 de um motorista (o elemento do alvo secundário, um registro do 'AIEl'): na RAM emulada, o da cópia.
void put_secondary(TrafficEmu& e, int i, const drv::AIGroundVehicle& v, const AiCarp& ac) {
    e.m.write32(ai_emu(i) + 0x204, ac.aiel_emu(uint32_t(v.unknown204)));
}
std::string same_secondary(TrafficEmu& e, int i, const drv::AIGroundVehicle& v, const AiCarp& ac) {
    const uint32_t emu = e.m.read32(ai_emu(i) + 0x204);
    const uint32_t want = v.unknown204 == 0 ? 0u : ac.aiel_emu(uint32_t(v.unknown204));
    if (emu != want && !(want == 0 && emu == uint32_t(v.unknown204)))
        return "o alvo secundário: original " + hex(emu) + ", porte " + hex(uint32_t(v.unknown204));
    e.m.write32(ai_emu(i) + 0x204, uint32_t(v.unknown204));  // para o compare
    return std::string();
}

// A missão emulada como a do porte: as configurações (os fatores da dificuldade), a dificuldade, a direção automática e
// o modo de cinema. A direção automática e o cinema são as mesmas palavras que o tráfego lê (traffic_world()).
void sync_mission() {
    drv::SMissionManagerData& m = drv::mission_manager();
    m.auto_drive = drv::traffic_world().mission_camera_check;
    m.cinematic_mode = uint32_t(drv::traffic_world().mission_no_traffic);
}
void put_mission(TrafficEmu& e) {
    const drv::SMissionManagerData& m = drv::mission_manager();
    if (const drv::MissionSettings* s = nf::p32_ptr<const drv::MissionSettings>(m.settings)) {
        put(e.m, kSettingsEmu, s, sizeof *s);
        e.m.write32(kMission + 0x1c, kSettingsEmu);
    } else {
        e.m.write32(kMission + 0x1c, 0);
    }
    e.m.write32(kMission + 0x350, uint32_t(m.auto_drive));
    e.m.write32(kMission + 0x3cc, m.cinematic_mode);
    e.m.write32(kMission + 0x3b8, uint32_t(m.difficulty));
}
void random_mission(Rng& rng) {
    drv::SMissionManagerData& m = drv::mission_manager();
    m.difficulty = rng.range(0, 2);
    if (drv::MissionSettings* s = nf::p32_ptr<drv::MissionSettings>(m.settings))
        for (int k = 0; k < 3; ++k) {
            s->scale_a[k] = rng.chance(0.5) ? 1.0f : rng.uni(0.5f, 1.5f);
            s->scale_b[k] = rng.chance(0.5) ? 1.0f : rng.uni(0.5f, 1.5f);
        }
}

// ---------------------------------------------------------------- c) o dano do carro

// O carro `i` com o render do dano (a caixa das zonas no BaseDesc, o dono para os ganchos) e o RRandom do render.
void put_damage_car(TrafficEmu& e, Fleet& f, int i) {
    drv::BondCar& car = *f.cars[size_t(i)];
    e.m.write32(car_emu(i) + 0x48, kDmgRender);
    e.m.write32(kDmgRender + 0x30, car_emu(i));
    uint8_t base[0x60] = {};
    std::memcpy(base + 0x40, car.desc_zone_min, 12);
    std::memcpy(base + 0x50, car.desc_zone_max, 12);
    put(e.m, kDmgBase, base, sizeof base);
    e.m.write32(0x333e60, drv::render_random().seed);
    e.m.write32(0x333e64, drv::render_random().multiplier);
}

void random_damage_state(Rng& rng, drv::BondCar& car) {
    drv::PBondCarData& c = car.d;
    for (int z = 0; z < 16; ++z) {
        c.damage_zones[z][0] = rng.chance(0.4) ? 0.0f : rng.uni(0, 1);
        c.damage_zones[z][1] = rng.chance(0.4) ? 0.0f : rng.uni(0, 2);
    }
    for (int w = 0; w < 4; ++w)
        c.tyre_damage_points[w] =
            rng.chance(0.2) ? 0u : uint32_t(rng.range(1, int(car.tyre_damage_points_attr) + 50));
    c.unknown2dc = rng.chance(0.3) ? -1.0f : rng.uni(0.001f, 0.2f);
    c.scoreable = rng.chance(0.6) ? 1u : 0u;
    c.immunity = rng.chance(0.15) ? 1u : 0u;
    c.tyre_blow_outs = rng.chance(0.5) ? 1u : 0u;
    c.emp_active = rng.chance(0.2) ? 1 : 0;
    c.hit_point_loc = nf::p32_u(&c.hit_points);
    c.shield_point_loc = 0;
    c.hit_points = rng.chance(0.15) ? rng.uni(-5, 0) : (rng.chance(0.3) ? rng.uni(0.1f, 30) : rng.uni(30, 2000));
    const int classes[] = {0, 1, 2, 2, 3};
    c.car_class = classes[rng.range(0, 4)];
}

int check_damage(TrafficEmu& e, Fleet& f, int cases, Rng& rng) {
    int failures = 0;
    Tally t_apply, t_visual, t_emp;
    drv::CarDamageStats& ds = drv::car_damage_stats();
    const drv::CarDamageStats before = ds;
    uint64_t kinds[4] = {0, 0, 0, 0};
    for (int c = 0; c < cases; ++c) {
        random_scene(rng, f);
        random_mission(rng);
        sync_mission();
        const int i = rng.range(0, int(f.cars.size()) - 1);
        drv::BondCar& car = *f.cars[size_t(i)];
        const int saved_class = car.d.car_class;
        const std::string saved_name = car.name_;
        drv::CarPhysics* phys = car.physics();
        const int32_t saved_sub = phys->sub_physics;
        const bool saved_heli = car.is_heli;
        random_damage_state(rng, car);
        if (rng.chance(0.08)) phys->sub_physics = 1;
        if (rng.chance(0.05)) car.is_heli = true;
        drv::RigidBody* rb = car.body();
        const std::string tag = " (caso " + std::to_string(c) + ", carro " + std::to_string(i) + ")";
        const int op = rng.range(0, 9);
        if (op < 7) {
            // ApplyDamage: de um ponto em volta do carro ao centro (ou a outro ponto), os quatro tipos.
            alignas(16) float from[4], to[4];
            for (int k = 0; k < 3; ++k) {
                from[k] = rb->pos[k] + rng.uni(-6, 6);
                to[k] = rng.chance(0.7) ? rb->pos[k] + rng.uni(-0.5f, 0.5f) : rb->pos[k] + rng.uni(-6, 6);
            }
            from[3] = to[3] = 0;
            const float amount =
                rng.chance(0.05) ? 1000000.0f : (rng.chance(0.2) ? rng.uni(0, 2) : rng.uni(0, 300));
            const float split = rng.chance(0.3) ? 0.5f : rng.uni(0, 1);
            const int kind = rng.range(0, 3);
            ++kinds[kind];
            f.put_all();
            put_mission(e);
            put_damage_car(e, f, i);
            put(e.m, kAiScratch, from, 16);
            put(e.m, kAiScratch + 0x10, to, 16);
            // O escudo (+0x434, o float para onde ele aponta) em um terço dos casos.
            static float shield = 0.0f;
            const bool with_shield = rng.chance(0.33);
            if (with_shield) {
                shield = rng.chance(0.3) ? 0.0f : rng.uni(0, 400);
                car.d.shield_point_loc = nf::p32_u(&shield);
                e.m.write32(car_emu(i) + 0x434, kAiScratch + 0x80);
                e.m.write32(kAiScratch + 0x80, ps2::fbits(shield));
            }
            g_emu_calls.clear();
            ds.trace = true;
            ds.calls_out.clear();
            drv::damage_source_is_player() = false;
            ++t_apply.cases;
            e.m.set_fpr(12, amount);
            e.m.set_fpr(13, split);
            e.m.set_gpr(R::t0, kDmgSig);
            e.call(kFnApplyDamage, {car_emu(i), kAiScratch, kAiScratch + 0x10, uint32_t(kind)});
            const int32_t rv = drv::PBondCar_ApplyDamage(car, from, to, amount, split, kind);
            ds.trace = false;
            std::string why;
            if (int32_t(e.m.gpr(R::v0)) != rv)
                why = "o retorno: original " + hex(uint32_t(e.m.gpr(R::v0))) + ", porte " + hex(uint32_t(rv));
            if (why.empty() && with_shield && e.m.read32(kAiScratch + 0x80) != ps2::fbits(shield))
                why = "o escudo: original " + hex(e.m.read32(kAiScratch + 0x80)) + ", porte " + hex(ps2::fbits(shield));
            car.d.shield_point_loc = 0;
            if (why.empty()) why = f.compare(false);
            if (why.empty()) why = same_calls(ds.calls_out);
            if (!why.empty()) t_apply.fail(why + tag + " tipo " + std::to_string(kind));
        } else if (op < 9) {
            // SetVisualDamage: os limites às vezes fora (o original não faz nada).
            float lim[4];
            lim[0] = rng.uni(0, 0.7f);
            lim[1] = rng.chance(0.1) ? rng.uni(0, 0.5f) : lim[0] + rng.uni(0, 1 - lim[0]);
            lim[2] = rng.uni(0, 0.7f);
            lim[3] = rng.chance(0.1) ? rng.uni(0.9f, 1.4f) : lim[2] + rng.uni(0, 1 - lim[2]);
            drv::render_random().seed = rng.next() & 0xffff;
            f.put_all();
            put_mission(e);
            put_damage_car(e, f, i);
            g_emu_calls.clear();
            ds.trace = true;
            ds.calls_out.clear();
            ++t_visual.cases;
            for (int k = 0; k < 4; ++k) e.m.set_fpr(12 + k, lim[k]);
            e.call(kFnSetVisualDamage, {car_emu(i)});
            drv::PBondCar_SetVisualDamage(car, lim[0], lim[1], lim[2], lim[3]);
            ds.trace = false;
            std::string why = f.compare(false);
            if (why.empty()) why = same_calls(ds.calls_out);
            if (why.empty() && e.m.read32(0x333e60) != drv::render_random().seed)
                why = "o RRandom: original " + hex(e.m.read32(0x333e60)) + ", porte " + hex(drv::render_random().seed);
            if (!why.empty()) t_visual.fail(why + tag);
        } else {
            // AttackWithEmp (às vezes a van da bomba: o ProgrammerDefinedEvent(0)).
            if (rng.chance(0.4)) car.name_ = "paris_bombvan";
            f.put_all();
            put_mission(e);
            put_damage_car(e, f, i);
            g_emu_calls.clear();
            ds.trace = true;
            ds.calls_out.clear();
            ++t_emp.cases;
            e.call(kFnAttackWithEmp, {car_emu(i)});
            drv::PBondCar_AttackWithEmp(car);
            ds.trace = false;
            std::string why = f.compare(false);
            if (why.empty()) why = same_calls(ds.calls_out);
            if (!why.empty()) t_emp.fail(why + tag);
        }
        car.d.car_class = saved_class;
        car.name_ = saved_name;
        phys->sub_physics = saved_sub;
        car.is_heli = saved_heli;
    }
    report("PBondCar::ApplyDamage (os tipos 0-3, os pneus, os mortos, o tiro)", t_apply, failures);
    report("PBondCar::SetVisualDamage", t_visual, failures);
    report("PBondCar::AttackWithEmp", t_emp, failures);
    std::printf("  (tipos %" PRIu64 "/%" PRIu64 "/%" PRIu64 "/%" PRIu64 ": acertaram a caixa %" PRIu64
                ", pneus atingidos %" PRIu64 ", estouros %" PRIu64 ", destruídos %" PRIu64 ", escudo %" PRIu64
                ", estímulos %" PRIu64 ", efeitos %" PRIu64 ")\n",
                kinds[0], kinds[1], kinds[2], kinds[3], ds.hits - before.hits, ds.tyres_hit - before.tyres_hit,
                ds.blowouts - before.blowouts, ds.deaths - before.deaths, ds.shield - before.shield,
                ds.stimuli - before.stimuli, ds.render_fx - before.render_fx);
    return failures;
}

// ---------------------------------------------------------------- d) os agentes

// Os casos em que o original leria memória inválida (o porte guarda e conta: o alvo nulo de um UpdateNavPos com +0xb1,
// um elemento fora da cópia...) ou o lixo da pilha (o t do InitAtPoint numa célula sem trechos) não têm resultado
// definido: um passo que os encontra encerra a sequência sem comparar.
uint64_t undefined_count() {
    return drv::road_nav_stats().stack_garbage + drv::road_nav_stats().guards + drv::ground_vehicle_stats().guards +
           drv::ai_element_stats().guards;
}

// Os navegadores 2-5 do motorista `i` na RAM emulada (cada um com o seu RCameraSpline em +0xc0).
// Os RCameraSpline dos navegadores 2-5 de cada motorista, construídos pelo WRoadNav::WRoadNav original (a lista do
// +0x64 que o BuildSplineEx percorre fica válida), antes da marca do heap emulado.
uint32_t g_agent_splines[kMaxCars][4] = {};
void construct_agent_navs(TrafficEmu& e) {
    for (int i = 0; i < kMaxCars; ++i)
        for (int k = 0; k < 4; ++k) {
            e.call(kFnNavCtor, {agent_nav_emu(i, k)});
            g_agent_splines[i][k] = e.m.read32(agent_nav_emu(i, k) + 0x90);
        }
}
void put_agent_navs(TrafficEmu& e, int i, const drv::AIGroundVehicle& v) {
    const uint32_t ptrs[4] = {v.nav2, v.nav3, v.nav4, v.nav5};
    const uint32_t offs[4] = {0xf0, 0x100, 0x110, 0x114};
    for (int k = 0; k < 4; ++k) {
        const drv::WRoadNav* n = nf::p32_ptr<const drv::WRoadNav>(ptrs[k]);
        if (!n) continue;
        drv::WRoadNav c = *n;
        c.spline = g_agent_splines[i][k];
        put(e.m, agent_nav_emu(i, k), &c, 0xc0);
        put(e.m, g_agent_splines[i][k], drv::road_nav_spline(*n), 0x64);
        e.m.write32(ai_emu(i) + offs[k], agent_nav_emu(i, k));
    }
}
// Os de todos os motoristas da frota (o alvo e os vizinhos também usam os navegadores deles: GetCollNav).
void put_fleet_navs(TrafficEmu& e, Fleet& f) {
    for (size_t i = 0; i < f.cars.size(); ++i)
        if (const drv::AIGroundVehicle* v = drv::ai_of(*f.cars[i])) put_agent_navs(e, int(i), *v);
}
std::string same_agent_navs(TrafficEmu& e, int i, const drv::AIGroundVehicle& v) {
    const uint32_t ptrs[4] = {v.nav2, v.nav3, v.nav4, v.nav5};
    std::string out;
    for (int k = 0; k < 4; ++k) {
        const drv::WRoadNav* n = nf::p32_ptr<const drv::WRoadNav>(ptrs[k]);
        if (!n) continue;
        const std::string what = "o navegador " + std::to_string(k + 2);
        if (!same_words(what.c_str(), e.m.mem(agent_nav_emu(i, k), 0xc0), reinterpret_cast<const uint8_t*>(n), 0xc0,
                        {{0x90, 0x94}}, out))
            return out;
        if (!same_words((what + " (RCameraSpline)").c_str(), e.m.mem(g_agent_splines[i][k], 0x64),
                        reinterpret_cast<const uint8_t*>(drv::road_nav_spline(*n)), 0x64, {}, out))
            return out;
    }
    return out;
}

void random_agent(Rng& rng, drv::AIGroundVehicle& v, drv::BondCar& car, const AiCarp& ac) {
    v.controller = 5;
    v.active = 1;
    v.valid_command = 1;
    const int navs[] = {1, 2, 4, 4, 4, 3};
    v.navigate_mode = navs[rng.range(0, 5)];
    v.attack_mode = rng.range(13, 15);
    drv::AIGroundVehicle* player = drv::AIVehicleController_GetPlayerGroundVehicle(0);
    // (O modo 14 lê o alvo sem testar: sempre com alvo, ou o original leria o PhysicsObject do endereço 0.)
    v.primary_target = (rng.chance(0.8) || v.attack_mode == 14) && player ? nf::p32_u(player) : 0;
    v.default_target = v.primary_target;
    v.target_mode = v.primary_target ? 0 : 1;
    for (int k = 0; k < 3; ++k) v.unknown1a8[k] = car.body()->pos[k] + rng.uni(-60, 60);
    v.unknown204 = rng.chance(0.5) ? 0 : int32_t(ac.aiel + 0x100u * uint32_t(rng.range(0, ac.aiel_count - 1)));
    v.unknown208 = rng.chance(0.7) ? 0 : (rng.chance(0.7) ? 1 : 2);
    v.unknown148 = rng.chance(0.3) ? 1 : 0;
    v.recorded_glue = rng.chance(0.5) ? 1 : 0;
    v.unknown164 = int16_t(rng.chance(0.1) ? 1 : 0);
    v.speed_limit = rng.uni(10, 60);
    v.speed_scaler = rng.uni(0.5f, 1.5f);
    v.rec_speed_offset = rng.uni(0.5f, 1.5f);
    v.min_dist_to_target = rng.uni(5, 40);
    v.ram_freq = rng.uni(0, 1);
    v.lead_scaler = rng.uni(0, 1);
    v.has_turret = uint8_t(rng.chance(0.2));
    v.has_machine_guns = uint8_t(rng.chance(0.3));
    v.has_rockets = uint8_t(rng.chance(0.2));
    v.has_missiles = uint8_t(rng.chance(0.2));
    v.weapons_active = uint8_t(rng.chance(0.7));
    v.rate_of_fire = rng.uni(0, 2);
    v.accuracy = uint8_t(rng.range(0, 100));
    v.coll_avoidance = rng.chance(0.8) ? 1 : 0;
    v.unknown074 = rng.chance(0.8) ? 0 : drv::simulation().steps + rng.range(-30, 30);
    for (int k = 0; k < 3; ++k) v.direction[k] = rng.uni(-1, 1);
    drv::WRoadNav& nav = *drv::nav_of(v);
    nav.mode = v.navigate_mode == 4 ? (rng.chance(0.5) ? 4 : 3) : (v.navigate_mode == 3 ? 1 : 0);
    nav.unknownb1 = rng.chance(0.2) ? 1 : 0;
    nav.unknownb2 = rng.chance(0.1) ? 1 : 0;
    // Sem alvo, o UpdateNavPos com +0xb1/+0xb2 leria o PhysicsObject do endereço 0 (o porte guarda): fora.
    if (v.primary_target == 0) nav.unknownb1 = nav.unknownb2 = 0;
}

int check_agents(TrafficEmu& e, Fleet& f, const AiCarp& ac, int cases, Rng& rng) {
    int failures = 0;
    Tally t_update;
    const drv::AIWeaponStats wbefore = drv::ai_weapon_stats();
    uint64_t emu_fire = 0, skipped = 0;
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
        const std::string tag = " (caso " + std::to_string(c) + ")";
        const int steps = rng.chance(0.4) ? rng.range(2, 15) : 1;
        f.put_all();
        put_mission(e);
        put_fleet_navs(e, f);
        for (int k = 0; k < steps; ++k) {
            ++t_update.cases;
            put_secondary(e, i, *v, ac);
            g_emu_calls.clear();
            const drv::AIWeaponStats wb = drv::ai_weapon_stats();
            const int nav_mode = v->navigate_mode, attack = v->attack_mode;
            const uint64_t garbage = undefined_count();
            e.call(kFnAIUpdate, {ai_emu(i)});
            drv::AIGroundVehicle_Update(*v);
            if (undefined_count() != garbage) {
                --t_update.cases;
                ++skipped;
                break;
            }
            const drv::AIWeaponStats& wa = drv::ai_weapon_stats();
            uint64_t emu_weapons = 0;
            for (const drv::CarDamageCall& cl : g_emu_calls)
                if (cl.fn == 0x12f730 || cl.fn == 0x12fa28 || cl.fn == 0x12fd58 || cl.fn == 0x12f170) ++emu_weapons;
            emu_fire += emu_weapons;
            std::string why = same_secondary(e, i, *v, ac);
            if (why.empty()) why = f.compare(false);
            for (size_t j = 0; j < f.cars.size() && why.empty(); ++j)
                if (const drv::AIGroundVehicle* w = drv::ai_of(*f.cars[j])) why = same_agent_navs(e, int(j), *w);
            const uint64_t port_weapons = (wa.bullets - wb.bullets) + (wa.rockets - wb.rockets) +
                                          (wa.missiles - wb.missiles) + (wa.turret - wb.turret);
            if (why.empty() && emu_weapons != port_weapons)
                why = "as armas: original " + std::to_string(emu_weapons) + ", porte " + std::to_string(port_weapons);
            if (!why.empty()) {
                t_update.fail(why + tag + " passo " + std::to_string(k) + " navegação " + std::to_string(nav_mode) +
                              " ataque " + std::to_string(attack));
                break;
            }
        }
        v->controller = saved_controller;
    }
    report("AIGroundVehicle::Update (os agentes: o estado 5 inteiro)", t_update, failures);
    const drv::AIWeaponStats& wa = drv::ai_weapon_stats();
    std::printf("  (armas %" PRIu64 ": balas %" PRIu64 ", foguetes %" PRIu64 ", mísseis %" PRIu64 ", torre %" PRIu64
                "; fora, memória inválida ou lixo da pilha no original: %" PRIu64 ")\n",
                emu_fire, wa.bullets - wbefore.bullets, wa.rockets - wbefore.rockets, wa.missiles - wbefore.missiles,
                wa.turret - wbefore.turret, skipped);
    return failures;
}

// As partes do estado 5 uma a uma (o mesmo motorista sorteado): o DoAgentStateUpdate, o DoAttackMode, o DoMoveMode, o
// DoWeaponUpdate, o UpdateStuck, os dois desvios, o DriveToPoint e o GetCollNav.
int check_agent_parts(TrafficEmu& e, Fleet& f, const AiCarp& ac, int cases, Rng& rng) {
    int failures = 0;
    constexpr int kParts = 9;
    const char* names[kParts] = {"AIGroundVehicle::DoAgentStateUpdate",
                                 "AIGroundVehicle::DoAttackMode (13 a 15)",
                                 "AIGroundVehicle::DoMoveMode (1 a 4 dos agentes)",
                                 "AIGroundVehicle::DoWeaponUpdate",
                                 "AIGroundVehicle::UpdateStuck",
                                 "AIGroundVehicle::CheckAgentRoadNetworkCollision",
                                 "AIGroundVehicle::CheckAgentDirectCollision",
                                 "AIGroundVehicle::DriveToPoint",
                                 "AIGroundVehicle::GetCollNav"};
    const uint32_t fns[kParts] = {0x131770, 0x12e808, 0x130508, 0x130050, 0x133850,
                                  0x135f70, 0x137310, 0x133f60, 0x1351d0};
    Tally t[kParts];
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
        const int which = rng.range(0, kParts - 1);
        const drv::RigidBody* rb = car.body();
        alignas(16) float pt[4] = {rb->pos[0] + rng.uni(-60, 60), rb->pos[1] + rng.uni(-2, 2),
                                   rb->pos[2] + rng.uni(-60, 60), 0.0f};
        alignas(16) float dir[4] = {rng.uni(-1, 1), rng.uni(-0.1f, 0.1f), rng.uni(-1, 1), 0.0f};
        f.put_all();
        put_mission(e);
        put_fleet_navs(e, f);
        put_secondary(e, i, *v, ac);
        put(e.m, kAiScratch, pt, 16);
        put(e.m, kAiScratch + 0x10, dir, 16);
        g_emu_calls.clear();
        const drv::AIWeaponStats wb = drv::ai_weapon_stats();
        ++t[which].cases;
        std::string why;
        const uint64_t garbage = undefined_count();
        switch (which) {
            case 0:
                e.call(fns[which], {ai_emu(i)});
                drv::AIGroundVehicle_DoAgentStateUpdate(*v);
                break;
            case 1:
                e.call(fns[which], {ai_emu(i)});
                drv::AIGroundVehicle_DoAttackMode(*v);
                break;
            case 2:
                e.call(fns[which], {ai_emu(i)});
                drv::AIGroundVehicle_DoMoveMode(*v);
                break;
            case 3:
                e.call(fns[which], {ai_emu(i)});
                drv::AIGroundVehicle_DoWeaponUpdate(*v);
                break;
            case 4:
                e.call(fns[which], {ai_emu(i)});
                drv::AIGroundVehicle_UpdateStuck(*v);
                break;
            case 5:
                e.call(fns[which], {ai_emu(i), kAiScratch});
                drv::AIGroundVehicle_CheckAgentRoadNetworkCollision(*v, pt);
                break;
            case 6:
                e.call(fns[which], {ai_emu(i), kAiScratch});
                drv::AIGroundVehicle_CheckAgentDirectCollision(*v, pt);
                break;
            case 7: {
                const float speed = rng.uni(0, 40);
                e.m.set_fpr(12, speed);
                e.call(fns[which], {ai_emu(i), kAiScratch});
                drv::AIGroundVehicle_DriveToPoint(*v, pt, speed);
                break;
            }
            default: {
                const bool alt = rng.chance(0.5);
                const int8_t lane = int8_t(rng.range(-3, 3));
                e.call(fns[which], {ai_emu(i), kAiScratch + 0x10, alt ? 1u : 0u, uint32_t(int32_t(lane))});
                const drv::WRoadNav* pn = drv::AIGroundVehicle_GetCollNav(*v, dir, alt, lane);
                const uint32_t want = agent_nav_emu(i, alt ? 1 : 0);
                if (uint32_t(e.m.gpr(R::v0)) != want || pn != nf::p32_ptr<drv::WRoadNav>(alt ? v->nav3 : v->nav2))
                    why = "o navegador devolvido: original " + hex(uint32_t(e.m.gpr(R::v0)));
                break;
            }
        }
        if (undefined_count() != garbage) {
            --t[which].cases;
            v->controller = saved_controller;
            continue;
        }
        if (why.empty() && (which == 5 || which == 6 || which == 7))
            same_words("o ponto", e.m.mem(kAiScratch, 16), reinterpret_cast<const uint8_t*>(pt), 12, {}, why);
        if (why.empty()) why = same_secondary(e, i, *v, ac);
        if (why.empty()) why = f.compare(false);
        for (size_t j = 0; j < f.cars.size() && why.empty(); ++j)
            if (const drv::AIGroundVehicle* w = drv::ai_of(*f.cars[j])) why = same_agent_navs(e, int(j), *w);
        const drv::AIWeaponStats& wa = drv::ai_weapon_stats();
        uint64_t emu_weapons = 0;
        for (const drv::CarDamageCall& cl : g_emu_calls)
            if (cl.fn == 0x12f730 || cl.fn == 0x12fa28 || cl.fn == 0x12fd58 || cl.fn == 0x12f170) ++emu_weapons;
        const uint64_t port_weapons = (wa.bullets - wb.bullets) + (wa.rockets - wb.rockets) +
                                      (wa.missiles - wb.missiles) + (wa.turret - wb.turret);
        if (why.empty() && emu_weapons != port_weapons)
            why = "as armas: original " + std::to_string(emu_weapons) + ", porte " + std::to_string(port_weapons);
        if (!why.empty()) {
            const std::string tag = " (caso " + std::to_string(c) + ", navegação " + std::to_string(v->navigate_mode) +
                                    ", ataque " + std::to_string(v->attack_mode) + ")";
            t[which].fail(why + tag);
        }
        v->controller = saved_controller;
    }
    for (int k = 0; k < kParts; ++k) report(names[k], t[k], failures);
    return failures;
}

// ---------------------------------------------------------------- e) o AIElementController


// Os 'AICo' e os 'AIEl' de Paris na RAM emulada (os ponteiros entre eles trocados; o trilho 'AISp' do modo 5 fica de
// fora: o RPathEngine é da seção 23).
void put_carp(TrafficEmu& e, const AiCarp& ac) {
    for (int k = 0; k < ac.aico_count; ++k) {
        uint8_t rec[0xa0];
        std::memcpy(rec, nf::p32_ptr<const uint8_t>(ac.aico + 0xa0u * uint32_t(k)), 0xa0);
        uint32_t w = 0;
        std::memcpy(&w, rec + 0x50, 4);
        w = ac.aiel_emu(w);
        std::memcpy(rec + 0x50, &w, 4);
        std::memset(rec + 0x80, 0, 4);
        put(e.m, kAicoCopy + 0xa0u * uint32_t(k), rec, 0xa0);
    }
    for (int k = 0; k < ac.aiel_count; ++k) {
        uint8_t rec[0x100];
        std::memcpy(rec, nf::p32_ptr<const uint8_t>(ac.aiel + 0x100u * uint32_t(k)), 0x100);
        uint32_t w = 0;
        std::memcpy(&w, rec + 0xf8, 4);
        w = ac.aico_emu(w);
        std::memcpy(rec + 0xf8, &w, 4);
        put(e.m, kAielCopy + 0x100u * uint32_t(k), rec, 0x100);
    }
}

// O controlador emulado comparado com o do porte (o cabeçalho, as cópias dos elementos e os acompanhamentos).
std::string same_controller(TrafficEmu& e, const AiCarp& ac) {
    const drv::AIElementControllerData* pc = drv::ai_controller();
    const uint32_t ec = e.m.read32(kCtlObj);
    std::string out;
    if (!pc || !ec) return "sem controlador";
    if (!same_words("AIElementController", e.m.mem(ec, 0x58), reinterpret_cast<const uint8_t*>(pc), 0x58,
                    {{0x0c, 0x1c}, {0x38, 0x40}}, out))
        return out;
    const uint32_t eel = e.m.read32(ec + 0x0c), etr = e.m.read32(ec + 0x14);
    for (int k = 0; k < pc->count; ++k) {
        uint8_t mine[0x100];
        std::memcpy(mine, drv::ai_element(k), 0x100);
        uint32_t w = 0;
        std::memcpy(&w, mine + 0xf8, 4);
        w = ac.aico_emu(w);
        std::memcpy(mine + 0xf8, &w, 4);
        if (!same_words("o elemento", e.m.mem(eel + 0x100u * uint32_t(k), 0x100), mine, 0x100,
                        {{0x50, 0x54}, {0xfc, 0x100}}, out))
            return out + " (" + std::to_string(k) + ")";
        if (!same_words("o acompanhamento", e.m.mem(etr + 0x50u * uint32_t(k), 0x50),
                        reinterpret_cast<const uint8_t*>(drv::ai_tracking(k)), 0x50, {{0x20, 0x28}}, out))
            return out + " (" + std::to_string(k) + ")";
    }
    return out;
}

// O estado sorteado dos elementos (os dois lados recebem o mesmo): as bandeiras, a vida, o estado, o relógio.
void random_elements(Rng& rng, TrafficEmu& e, int count) {
    const uint32_t ec = e.m.read32(kCtlObj);
    const uint32_t eel = e.m.read32(ec + 0x0c), etr = e.m.read32(ec + 0x14);
    for (int k = 0; k < count; ++k) {
        drv::AIElement* pe = drv::ai_element(k);
        drv::AIElementTracking* pt = drv::ai_tracking(k);
        if (pe->state == 1) continue;  // (ninguém acorda aqui: o SpawnCarObject não acha lugar)
        if (rng.chance(0.3)) pe->flags = uint16_t(pe->flags ^ (1u << rng.range(0, 6)));
        if (rng.chance(0.2)) pe->health = rng.chance(0.3) ? 0.0f : rng.uni(1, 3000);
        if (rng.chance(0.1)) pe->state = 2;
        else if (rng.chance(0.1)) pe->state = 0;
        if (rng.chance(0.2)) pe->difficulty = uint16_t(rng.range(0, 6));
        if (rng.chance(0.3)) pt->next_update = drv::simulation().steps + rng.range(-4, 4);
        if (rng.chance(0.2)) drv::AIElementTracking_SetWakeRange(*pt, rng.uni(-10, 400));
        put(e.m, eel + 0x100u * uint32_t(k) + 0x40, reinterpret_cast<const uint8_t*>(pe) + 0x40, 0x10);
        put(e.m, etr + 0x50u * uint32_t(k), pt, 0x20);
    }
}

uint32_t g_ctl_heap = kCtlHeap;

void hook_controller(TrafficEmu& e) {
    MipsMachine& m = e.m;
    g_ctl_heap = kCtlHeap;
    m.hook(0x2d09c8, "operator new (o construtor do controlador)", [](MipsMachine& mm) {
        const uint32_t n = (uint32_t(mm.gpr(R::a0)) + 15) & ~15u;
        if (g_ctl_heap + n > kCtlHeapEnd) throw nf::MipsError("operator new: a área acabou", g_ctl_heap, n);
        std::memset(mm.mem(g_ctl_heap, n), 0, n);
        mm.set_gpr(R::v0, g_ctl_heap);
        g_ctl_heap += n;
    });
    m.hook(0x2a7e84, "memcpy", [](MipsMachine& mm) {
        const uint32_t dst = uint32_t(mm.gpr(R::a0)), src = uint32_t(mm.gpr(R::a1)), n = uint32_t(mm.gpr(R::a2));
        if (n) std::memmove(mm.mem(dst, n), mm.mem(src, n), n);
        mm.set_gpr(R::v0, mm.gpr(R::a0));
    });
    m.hook(0x196740, "PVehicle::GetNamedAttribs", [](MipsMachine&) {});
    m.hook(0x1967a8, "PVehicle::RenderNameAttrib", [](MipsMachine& mm) { mm.set_gpr(R::v0, kName); });
    m.hook(0x169728, "AttributeSet::~AttributeSet", [](MipsMachine&) {});
    m.hook(0x1c7f48, "RSceneObj::PreLoad", [](MipsMachine&) {});
    m.hook(0x10cb88, "ActCharacter::LoadCharacter", [](MipsMachine&) {});
    m.hook(0x2d6c38, "AssertMessage", [](MipsMachine&) { throw nf::MipsError("AssertMessage", 0, 0); });
    m.hook(0x1ff850, "Simulation::SpawnCarObject (sem lugar)", [](MipsMachine& mm) { mm.set_gpr(R::v0, 0); });
    m.write32(kHardLimit, 1);
    m.write32(kActorDb, 0);
}

int check_controller(TrafficEmu& e, Fleet& f, const AiCarp& ac, drv::UData* aiel, int cases, Rng& rng) {
    int failures = 0;
    Tally t_ctor, t_track, t_update, t_prio, t_redirect, t_dead, t_hench;
    // O construtor dos dois lados (com a dificuldade e os fatores da vida da missão).
    random_scene(rng, f);
    random_mission(rng);
    sync_mission();
    f.put_all();
    put_mission(e);
    put_carp(e, ac);
    drv::UData ud = *aiel;
    ud.flags &= ~2u;
    ud.offset = kAielCopy;
    put(e.m, kAielUData, &ud, sizeof ud);
    hook_controller(e);
    e.call(kFnCtlCtor, {kCtlEmu, kAielUData});
    e.m.write32(kCtlObj, kCtlEmu);
    drv::AIElementController_Construct(aiel);
    ++t_ctor.cases;
    std::string why = same_controller(e, ac);
    if (!why.empty()) t_ctor.fail(why);
    const int count = drv::ai_controller()->count;
    const uint32_t ec = kCtlEmu;
    uint64_t pdes = 0, wanted = 0;
    for (int c = 0; c < cases && t_ctor.bad == 0; ++c) {
        random_scene(rng, f);
        random_mission(rng);
        sync_mission();
        // O jogador perto de um elemento sorteado às vezes (para entrar no raio de acordar).
        drv::RigidBody* p = f.cars[0]->body();
        if (rng.chance(0.6)) {
            const drv::AIElement* el = drv::ai_element(rng.range(0, count - 1));
            for (int k = 0; k < 3; k += 2) p->pos[k] = el->matrix[3][k] + rng.uni(-250, 250);
            drv::UpdateZonalInfo(p);
        }
        f.put_all();
        put_mission(e);
        put(e.m, kCamObj2 + 0x20, f.camera[2], 16);
        random_elements(rng, e, count);
        g_emu_calls.clear();
        const std::string tag = " (caso " + std::to_string(c) + ")";
        const int op = rng.range(0, 9);
        if (op < 3) {
            ++t_track.cases;
            const int k = rng.range(0, count - 1);
            alignas(16) float pp[4] = {p->pos[0], p->pos[1], p->pos[2], 0};
            put(e.m, kAiScratch, pp, 16);
            const uint32_t etr = e.m.read32(ec + 0x14) + 0x50u * uint32_t(k);
            if (rng.chance(0.5)) {
                e.call(kFnTrackUpdate, {etr, kAiScratch});
                drv::AIElementTracking_Update(*drv::ai_tracking(k), pp);
            } else {
                e.call(kFnTrackPosition, {etr, kAiScratch});
                drv::AIElementTracking_UpdatePosition(*drv::ai_tracking(k), pp);
            }
            why = same_controller(e, ac);
            if (why.empty()) why = f.compare(false);
            if (!why.empty()) t_track.fail(why + tag);
        } else if (op < 7) {
            ++t_update.cases;
            e.call(kFnCtlUpdate, {ec});
            drv::AIElementController_Update();
            for (const drv::CarDamageCall& cl : g_emu_calls)
                if (cl.fn == 0x208540) ++pdes;
            wanted += uint64_t(drv::ai_controller()->vehicles_wanted);
            why = same_controller(e, ac);
            if (why.empty()) why = f.compare(false);
            if (!why.empty()) t_update.fail(why + tag);
        } else if (op < 8) {
            const int k = rng.range(0, count - 1);
            ++t_prio.cases;
            e.call(kFnPrio, {ec, e.m.read32(ec + 0x0c) + 0x100u * uint32_t(k)});
            if (int32_t(e.m.gpr(R::v0)) != drv::AIElementController_CompareInRangePrio(*drv::ai_element(k)))
                t_prio.fail("o retorno" + tag);
            ++t_redirect.cases;
            e.call(kFnRedirect, {ec, kAielCopy + 0x100u * uint32_t(k)});
            const drv::AIElement* pe = drv::AIElementController_RedirectCarpElement(ac.aiel + 0x100u * uint32_t(k));
            const uint32_t want = e.m.read32(ec + 0x0c) + 0x100u * uint32_t(k);
            if (uint32_t(e.m.gpr(R::v0)) != want || pe != drv::ai_element(k)) t_redirect.fail("o elemento" + tag);
        } else if (op < 9) {
            ++t_dead.cases;
            e.call(kFnMightDead, {ec});
            drv::AIElementController_CalcMightAsWellBeDead();
            why = same_controller(e, ac);
            if (!why.empty()) t_dead.fail(why + tag);
        } else {
            ++t_hench.cases;
            const int32_t w = rng.range(0, 10);
            drv::ai_controller()->vehicles_wanted = w;
            e.m.write32(ec + 0x50, uint32_t(w));
            e.call(kFnOkHench, {ec});
            if ((e.m.gpr(R::v0) != 0) != drv::AIElementController_OkToSpawnHench()) t_hench.fail("o retorno" + tag);
        }
    }
    report("AIElementController::AIElementController (o 'AIEl' de Paris)", t_ctor, failures);
    report("AIElementTracking::Update e UpdatePosition", t_track, failures);
    report("AIElementController::Update (WakeUp sem lugar)", t_update, failures);
    report("AIElementController::CompareInRangePrio", t_prio, failures);
    report("AIElementController::RedirectCarpElement", t_redirect, failures);
    report("AIElementController::CalcMightAsWellBeDead", t_dead, failures);
    report("AIElementController::OkToSpawnHench", t_hench, failures);
    std::printf("  (ProgrammerDefinedEvent(1) %" PRIu64 ", veículos querendo acordar %" PRIu64 ")\n", pdes, wanted);
    return failures;
}

// ---------------------------------------------------------------- f) os comandos

int check_commands(TrafficEmu& e, Fleet& f, const AiCarp& ac, int cases, Rng& rng) {
    int failures = 0;
    Tally t_cmd, t_rec;
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
        v->ai_command = 0;
        const std::string tag = " (caso " + std::to_string(c) + ")";
        if (rng.chance(0.8)) {
            // Um comando sorteado (sem o modo 5) aplicado ao motorista direto (o obj[4] do evento).
            const int k = rng.range(0, ac.aico_count - 1);
            const uint8_t* rec = nf::p32_ptr<const uint8_t>(ac.aico + 0xa0u * uint32_t(k));
            if (rec[0x79] == 5) {
                v->controller = saved_controller;
                continue;
            }
            ++t_cmd.cases;
            f.put_all();
            put_mission(e);
            put_fleet_navs(e, f);
            put_secondary(e, i, *v, ac);
            put_carp(e, ac);
            uint32_t obj[8] = {0x371728, kAicoCopy + 0xa0u * uint32_t(k), 0, 0, ai_emu(i), 0, 0, 0};
            put(e.m, kEventObj, obj, sizeof obj);
            e.call(kFnAICommandDtor, {kEventObj, 0});
            drv::PendingEvent pe{65, 0, 0.0f, 0, 0, 0};
            pe.obj[0] = 0x371728;
            pe.obj[1] = ac.aico + 0xa0u * uint32_t(k);
            pe.obj[4] = nf::p32_u(v);
            const uint64_t garbage = undefined_count();
            drv::ai_event_execute(pe);
            if (undefined_count() != garbage) {
                --t_cmd.cases;
                v->controller = saved_controller;
                continue;
            }
            // O +0x88 (o comando em vigor) guarda o endereço do registro: o emulado e o do porte.
            const uint32_t emu_cmd = e.m.read32(ai_emu(i) + 0x88);
            std::string why;
            if (ac.aico_emu(uint32_t(v->ai_command)) != emu_cmd)
                why = "o comando em vigor: original " + hex(emu_cmd) + ", porte " + hex(uint32_t(v->ai_command));
            e.m.write32(ai_emu(i) + 0x88, uint32_t(v->ai_command));
            if (why.empty()) why = same_secondary(e, i, *v, ac);
            if (why.empty()) why = f.compare(false);
            for (size_t j = 0; j < f.cars.size() && why.empty(); ++j)
                if (const drv::AIGroundVehicle* w = drv::ai_of(*f.cars[j])) why = same_agent_navs(e, int(j), *w);
            if (!why.empty()) t_cmd.fail(why + tag + " comando " + std::to_string(k));
        } else {
            // ESetRecSpeedOffset sem elemento: o motorista do jogador.
            ++t_rec.cases;
            const float off = rng.uni(0, 2);
            f.put_all();
            put_mission(e);
            uint32_t obj[3] = {0x370660, 0, ps2::fbits(off)};
            put(e.m, kEventObj, obj, sizeof obj);
            e.call(kFnRecSpeedDtor, {kEventObj, 0});
            drv::PendingEvent pe{232, 0, 0.0f, 0, 0, 0};
            pe.obj[0] = 0x370660;
            pe.obj[2] = ps2::fbits(off);
            drv::ai_event_execute(pe);
            const std::string why = f.compare(false);
            if (!why.empty()) t_rec.fail(why + tag);
        }
        v->controller = saved_controller;
    }
    report("EAICommand::~EAICommand (os 'AICo' de Paris num motorista)", t_cmd, failures);
    report("ESetRecSpeedOffset::~ESetRecSpeedOffset", t_rec, failures);
    return failures;
}

}  // namespace

int run_ai_checks(const nf::Elf& elf, const nf::DiscSource& disc, const uint8_t* vu0_program, uint32_t vu0_size,
                  int cases, uint32_t seed) {
    std::printf("\nparte 17: a IA dos elementos de Paris e o dano do carro\n");
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
    TrafficEmu e;
    e.setup(elf, vu0_program, vu0_size);
    // fptodp (o float para double do libgcc): só alimenta a linha de depuração do IncNavPosition num trecho de
    // comprimento zero (o sprintf, sem saída); o resultado vai como os bits do double em v0.
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
    e.call(kFnRoadInit, {});  // o WRoadNetwork::Init original sobre a cópia (a parte 14 o confere)
    // Os 'AIEl' e 'AICo' do 'Map ' de Paris.
    drv::UGroup* root = track.root();
    drv::UGroup* map = drv::UGroup_GroupLocateTag(root, kTagMap);
    drv::UData* aiel = nullptr;
    AiCarp ac;
    if (map != drv::UGroup_GroupEnd(root)) {
        drv::UData* d = drv::UGroup_DataLocateTag(map, 0x4149456cu);
        if (d != drv::UGroup_DataEnd(map)) {
            aiel = d;
            ac.aiel = nf::p32_u(drv::UData_Data(d));
            ac.aiel_count = int(d->count);
        }
        d = drv::UGroup_DataLocateTag(map, 0x4149436fu);
        if (d != drv::UGroup_DataEnd(map)) {
            ac.aico = nf::p32_u(drv::UData_Data(d));
            ac.aico_count = int(d->count);
        }
    }
    if (!aiel || ac.aico_count == 0) {
        std::printf("  Paris sem 'AIEl'/'AICo'\n");
        return 1;
    }
    std::printf("  Paris: %d elementos, %d comandos\n", ac.aiel_count, ac.aico_count);
    drv::SimRandom_Reset(drv::sim_random());
    drv::mission_reset();
    drv::SMissionManager_Construct(false);
    Rng rng(seed ^ 0x17u);
    int failures = 0;
    Fleet fleet;
    fleet.e = &e;
    fleet.rc = &rc;
    fleet.tw = &tw;
    fleet.wc = &wc;
    try {
        failures += check_pieces(e, cases, rng);
        failures += check_agent_nav(e, rc, std::max(1, cases / 4), rng);
        // Os motoristas estáticos do controlador (jogadores e tráfego) ainda presos aos carros de uma parte anterior
        // (a 14 apaga a frota dela): soltos, para a frota desta parte pegar o jogador 0.
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
            // O RCameraSpline estático do GetSegmentCurveStep (0x4191e0, construído na primeira curva) também antes da
            // marca do heap emulado: a lista dele (+0x64) fica de pé entre os casos.
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
            const int n = std::max(1, cases / 4);
            failures += check_damage(e, fleet, n, rng);
            failures += check_controller(e, fleet, ac, aiel, n, rng);
            failures += check_agents(e, fleet, ac, n, rng);
            failures += check_agent_parts(e, fleet, ac, n, rng);
            failures += check_commands(e, fleet, ac, n, rng);
        }
    } catch (const nf::MipsError& err) {
        std::printf("  erro no interpretador: %s\n", err.what());
        ++failures;
    }
    drv::ai_weapon_hooks() = drv::AIWeaponHooks{};
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
