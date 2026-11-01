// O desenho do HUD das missões de carro (driving/game/hud_draw.h; docs/driving.md seção 28), portado do MIPS do
// DRIVING.ELF (GHud.obj). As desmontagens ficam em out/agent_drv_hud/hud/ do checkout principal (fora do git).

#include "driving/game/hud_draw.h"

#include <cstdio>
#include <cstring>

#include "driving/camera/camera_aim.h"
#include "driving/camera/player_camera.h"
#include "driving/drv.h"
#include "driving/game/ai_character.h"
#include "driving/game/hud_pause.h"
#include "driving/game/missile.h"
#include "driving/game/mission.h"
#include "driving/game/target_picker.h"
#include "driving/game/weapons.h"
#include "driving/physics/simulation.h"
#include "driving/real/vu0_math.h"
#include "driving/render/hud_gfx.h"
#include "driving/render/subtitles.h"
#include "driving/light_manager.h"
#include "driving/sound/audio.h"
#include "driving/sound/audio_stream.h"
#include "platform/fmath.h"
#include "platform/ps2_float.h"

namespace drv {

namespace ee = ps2::ee;

namespace {

HudGalleryStore g_store;
HudDrawEnv g_env;
HudDrawStats g_stats;
HudDrawGlobals g_globals;

template <typename T>
T rd(const uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof v);
    return v;
}
float rdf(const uint8_t* p) { return rd<float>(p); }
uint8_t* bytes(GHudData& h) { return reinterpret_cast<uint8_t*>(&h); }
const uint8_t* bytes(const GHudData& h) { return reinterpret_cast<const uint8_t*>(&h); }
uint32_t u32(const GHudData& h, uint32_t off) { return rd<uint32_t>(bytes(h) + off); }
int32_t i32(const GHudData& h, uint32_t off) { return rd<int32_t>(bytes(h) + off); }
float f32(const GHudData& h, uint32_t off) { return rdf(bytes(h) + off); }
void set_u32(GHudData& h, uint32_t off, uint32_t v) { std::memcpy(bytes(h) + off, &v, 4); }
void set_f32(GHudData& h, uint32_t off, float v) { std::memcpy(bytes(h) + off, &v, 4); }

// O GGallery do +8 e uma chave (p32).
GGalleryData& gal(GHudData& h) { return *nf::p32_ptr<GGalleryData>(u32(h, 8)); }
uint8_t* key_ptr(uint32_t k) {
    if (k == 0) ++g_stats.guards;
    return nf::p32_ptr<uint8_t>(k);
}
// O puppet na posição `off` do GHud.
uint32_t pup(const GHudData& h, uint32_t off) { return u32(h, off); }
float kf(uint32_t k, uint32_t off) { return rdf(key_ptr(k) + off); }

// AMenuSound::Trigger(int categoria, char *, char *, AView) (driving/sound/audio.h: nada sem o som ligado).
void menu_sound(int32_t bank, const char* name, const char* mix, int32_t arg) {
    g_stats.sounds.push_back({bank, name, mix, arg});
    AMenuSound_TriggerCategory(bank, name, mix, arg);
}

// Os arredondamentos "(int)(0,5 + x)" e "(int)(-0,5 + x)" dos alfas do GHud.
int32_t round_half(float x) { return ee::cvt_w(ee::add(ee::lt(x, 0.0f) ? -0.5f : 0.5f, x)); }

// A conversão de um unsigned para float do GCC (o bit 31: metade, mais o bit 0, e o dobro).
float cvt_u(uint32_t v) {
    if (int32_t(v) < 0) {
        const float f = ee::cvt_s(int32_t((v >> 1) | (v & 1)));
        return ee::add(f, f);
    }
    return ee::cvt_s(int32_t(v));
}

// O registro do puppet na tabela do GHud::GHud: o canvas (a palavra do GHud), o nome, a palavra de destino e a chave
// (-1: o próprio GET_Puppet; n: o GET_Key n).
struct PuppetRow {
    uint16_t canvas;
    const char* name;
    uint16_t word;
    int16_t key;
};
// Na ordem do GHud::GHud (0x0023ae30); os laços "gadget%d", "iris%d" e "arm%d" ficam no código.
constexpr PuppetRow kPuppets[] = {
    {0x35, "text1", 0x36, -1},          {0x35, "text2", 0x37, -1},
    {0x35, "text3", 0x38, -1},          {0x35, "text4", 0x39, -1},
    {0x35, "quad1", 0x3a, -1},          {0x35, "quad2", 0x3b, -1},
    {0x04, "crossNorm", 0x9c, -1},      {0x04, "crossZoom", 0x9d, -1},
    {0x04, "crossTorpNorm", 0x9e, -1},  {0x04, "crossTorpRoam", 0x9f, -1},
    {0x04, "crossTorpOffscreen", 0xa0, -1}, {0x04, "TargetZone", 0xa1, -1},
    {0x04, "TargetLock", 0xa2, -1},     {0x04, "hole", 0xa3, -1},
    {0x04, "zoomLevel", 0x24, -1},      {0x04, "zoomCircle", 0x25, -1},
    {0x04, "rdrarrow", 0x87, -1},       {0x04, "rdrarrow", 0x88, 1},
    {0x04, "rdrarrowbrdr", 0x89, -1},   {0x04, "radar", 0x8b, -1},
    {0x04, "radar_border", 0x8c, -1},   {0x04, "radar_stencil", 0x8d, -1},
    {0x04, "pulse", 0x8a, -1},          {0x04, "fbw1", 0x8e, -1},
    {0x04, "fbw2", 0x8f, -1},           {0x04, "fbw3", 0x90, -1},
    {0x04, "fbw4", 0x91, -1},           {0x04, "guidedtxt", 0x92, -1},
    {0x04, "guideddot", 0x93, -1},      {0x04, "autoAimDot", 0x95, -1},
    {0x04, "autoAimText", 0x94, -1},    {0x96, "status", 0x26, -1},
    {0x96, "statusback", 0x27, -1},     {0x96, "statusbacktop", 0x2a, -1},
    {0x96, "statusbackbot", 0x2d, -1},  {0x96, "statusback", 0x28, 1},
    {0x96, "statusbacktop", 0x2b, 1},   {0x96, "statusbackbot", 0x2e, 1},
    {0x96, "statusback", 0x29, 2},      {0x96, "statusbacktop", 0x2c, 2},
    {0x96, "statusbackbot", 0x2f, 2},   {0x96, "buttonTxt", 0x30, -1},
    {0x3c, "00", 0x3d, -1},             {0x3c, "01", 0x3e, -1},
    {0x3c, "02", 0x3f, -1},             {0x3c, "continueMis", 0x40, -1},
    {0x3c, "restartMis", 0x41, -1},     {0x3c, "quitMis", 0x42, -1},
    {0x3c, "mission", 0x43, -1},        {0x3c, "objectives", 0x44, -1},
    {0x3c, "controls", 0x46, -1},       {0x3c, "score", 0x47, -1},
    {0x3c, "arrow", 0x48, -1},          {0x3c, "controlsArrowL", 0x49, -1},
    {0x3c, "controlsArrowR", 0x4a, -1}, {0x3c, "controlsGelL", 0x4b, -1},
    {0x3c, "controlsGelM", 0x4c, -1},   {0x3c, "controlsGelR", 0x4d, -1},
    {0x3c, "controlsHeader", 0x4e, -1}, {0x3c, "controlsDetails", 0x4f, -1},
    {0x3c, "controlsYAxis", 0x50, -1},  {0x3c, "controlsInverted", 0x51, -1},
    {0x3c, "continue", 0x6f, -1},       {0x3c, "select", 0x72, -1},
    {0x3c, "hint", 0x73, -1},           {0x3c, "back", 0x70, -1},
    {0x3c, "invert", 0x71, -1},         {0x3c, "changepage", 0x74, -1},
    {0x3c, "scroll", 0x75, -1},         {0x3c, "restartConfirm", 0x7d, -1},
    {0x3c, "quitConfirm", 0x7e, -1},    {0x3c, "yes", 0x7f, -1},
    {0x3c, "no", 0x80, -1},             {0x3c, "scratchTxt", 0x7c, -1},
    {0x3c, "objCheck", 0x6d, -1},       {0x3c, "objBullet", 0x6e, -1},
    {0x3c, "hintTitle", 0x62, -1},      {0x3c, "hintText", 0x63, -1},
    {0x3c, "hintQuit", 0x64, -1},       {0x3c, "continueButPS2", 0x77, -1},
    {0x3c, "selectButPS2", 0x7a, -1},   {0x3c, "backButPS2", 0x78, -1},
    {0x3c, "changepageButPS2", 0x7b, -1}, {0x3c, "scrollButPS2", 0x76, -1},
    {0x3c, "invertButPS2", 0x79, -1},   {0x3c, "controlsPS2Cross", 0x56, -1},
    {0x3c, "controlsPS2Triangle", 0x57, -1}, {0x3c, "controlsPS2Square", 0x58, -1},
    {0x3c, "controlsPS2Circle", 0x59, -1}, {0x3c, "controlsPS2DHor", 0x5a, -1},
    {0x3c, "controlsPS2DVert", 0x5b, -1}, {0x3c, "controlsPS2LA", 0x5c, -1},
    {0x3c, "controlsPS2L1", 0x5d, -1},  {0x3c, "controlsPS2L2", 0x5e, -1},
    {0x3c, "controlsPS2RA", 0x5f, -1},  {0x3c, "controlsPS2R1", 0x60, -1},
    {0x3c, "controlsPS2R2", 0x61, -1},  {0x96, "PS2R1", 0x31, -1},
    {0x96, "PS2R1", 0x32, -1},          {0x96, "PS2L1", 0x33, -1},
    {0x96, "PS2X", 0x34, -1},
};
// Depois dos laços "gadget%d" (0x1e..0x23), "stylin" (0x1d), "weapon" (5), "ammo" (6), "iris%d" (7..0xd), "a007" (0xe),
// "arm%d" (0xf..0x15):
constexpr PuppetRow kPuppetsAfter[] = {
    {0x04, "healthBorder", 0x16, -1},   {0x04, "blip", 0x81, -1},
    {0x04, "blipBond", 0x82, -1},       {0x04, "blipEnemy", 0x83, -1},
    {0x04, "blipPowerup", 0x84, -1},    {0x04, "blipObjective", 0x85, -1},
    {0x04, "rechargeBar", 0x86, -1},    {0x04, "ared", 0xa5, -1},
    {0x04, "radioactive", 0xa6, -1},    {0x04, "ayellow", 0xa7, -1},
    {0x96, "txt", 0x97, -1},            {0x96, "objcomptxt", 0x98, -1},
    {0x96, "objnewtxt", 0x99, -1},      {0x96, "objobjtxt", 0x9a, -1},
    {0x96, "objbody", 0x9b, -1},
};

}  // namespace

void hud_menu_sound(int32_t bank, const char* name, const char* mix, int32_t arg) { menu_sound(bank, name, mix, arg); }
HudGalleryStore& hud_gallery_store() { return g_store; }
HudDrawEnv& hud_draw_env() { return g_env; }

bool hud_speech_done_default() {
    const AStreamView sp = astream_view("Speech");
    return !sp.exists || sp.over != 0 || sp.mix != "Speech";
}

bool hud_result_streams_done_default() {
    astream_test_stop("Music");  // AStream::Stop(AStream::Get("Music"))
    const AStreamView mu = astream_view("Music");
    const bool music = !mu.exists || mu.not_found != 0 || mu.over != 0 || mu.paused != 0 || mu.loop != 0;
    if (!music) return false;
    const AStreamView sp = astream_view("Speech");
    return !sp.exists || sp.not_found != 0 || sp.over != 0 || sp.mix != "Speech";
}
HudDrawStats& hud_draw_stats() { return g_stats; }
HudDrawGlobals& hud_draw_globals() { return g_globals; }
uint32_t hud_puppet(const GHudData& h, uint32_t word) { return u32(h, word * 4); }

void hud_view_transform(const GHudData& h, float out[4]) {
    out[0] = h.view_sx;
    out[1] = h.view_sy;
    out[2] = h.view_tx;
    out[3] = h.view_ty;
}

// ---------------------------------------------------------------- o GHud::GHud

bool GHud_Construct(GHudData& h, HudFiles files) {
    if (files.gallery.empty()) return false;
    set_u32(h, 0x390, 0);  // puVar11[0xe4]
    set_u32(h, 0x39c, 0);  // puVar11[0xe7]
    h.vtable = 0;          // &DAT_003aaec8 (o port não tem a tabela virtual)
    // O GGallery(hudNTSC.gal) (o UGroup::Deserialize no arquivo do port, que fica vivo no g_store).
    g_store.file = std::move(files.gallery);
    g_store.gallery = GGalleryData();
    auto image = files.image;
    GGallery_Construct(g_store.gallery, g_store.file.data(), [&](const uint8_t* ssh, uint32_t size) {
        return image ? image(ssh, size, false) : 0u;
    });
    g_store.loaded = true;
    set_u32(h, 8, nf::p32_u(&g_store.gallery));
    // A cortina: UFileLoader::ShapeFileLoad + o EAGL::TAR da primeira entrada (o +0x14 do SHPS).
    set_u32(h, 0, files.blood.empty() || !image ? 0u : image(files.blood.data(), uint32_t(files.blood.size()), true));
    // GGallery::FONT_Load(data\loading\loading.sfn)
    const uint32_t font_img = files.font_image ? files.font_image(files.font) : 0u;
    GGallery_FONT_Load(g_store.gallery, std::move(files.font), font_img);
    GGalleryData& g = g_store.gallery;
    set_u32(h, 0xc, GGallery_GET_Gallery(g));
    set_u32(h, 0x10, GGallery_GET_Canvas(g, u32(h, 0xc), "hud_base"));
    set_u32(h, 0x96 * 4, GGallery_GET_Canvas(g, u32(h, 0xc), "hud_msg"));
    set_u32(h, 0x3c * 4, GGallery_GET_Canvas(g, u32(h, 0xc), "pauseN"));
    set_u32(h, 0x35 * 4, GGallery_GET_Canvas(g, u32(h, 0xc), "paus_err"));
    auto put = [&](const PuppetRow& r) {
        uint32_t p = GGallery_GET_Puppet(g, u32(h, r.canvas * 4u), r.name);
        if (r.key >= 0) p = GGallery_GET_Key(g, p, r.key);
        set_u32(h, r.word * 4u, p);
    };
    for (const PuppetRow& r : kPuppets) put(r);
    char name[16];
    for (int i = 0; i < 6; ++i) {
        std::snprintf(name, sizeof name, "gadget%d", i);
        set_u32(h, (0x1e + i) * 4u, GGallery_GET_Puppet(g, u32(h, 0x10), name));
    }
    set_u32(h, 0x1d * 4, GGallery_GET_Puppet(g, u32(h, 0x10), "stylin"));
    set_u32(h, 5 * 4, GGallery_GET_Puppet(g, u32(h, 0x10), "weapon"));
    set_u32(h, 6 * 4, GGallery_GET_Puppet(g, u32(h, 0x10), "ammo"));
    for (int i = 1; i <= 7; ++i) {
        std::snprintf(name, sizeof name, "iris%d", i);
        set_u32(h, (7 + i - 1) * 4u, GGallery_GET_Puppet(g, u32(h, 0x10), name));
    }
    set_u32(h, 0xe * 4, GGallery_GET_Puppet(g, u32(h, 0x10), "a007"));
    for (int i = 1; i <= 7; ++i) {
        std::snprintf(name, sizeof name, "arm%d", i);
        set_u32(h, (0xf + i - 1) * 4u, GGallery_GET_Puppet(g, u32(h, 0x10), name));
    }
    for (const PuppetRow& r : kPuppetsAfter) put(r);
    // As peças sobem 10 pontos (o y da primeira chave, no arquivo), na ordem do PS2; depois o "- 0".
    auto add_key = [&](uint32_t word, uint32_t off, float d) {
        uint8_t* k = key_ptr(u32(h, word * 4));
        if (k == nullptr) return;
        const float v = ee::add(rdf(k + off), d);
        std::memcpy(k + off, &v, 4);
    };
    auto sub_y = [&](uint32_t word, float d) {
        uint8_t* k = key_ptr(u32(h, word * 4));
        if (k == nullptr) return;
        const float v = ee::sub(rdf(k + 4), d);
        std::memcpy(k + 4, &v, 4);
    };
    sub_y(5, 10.0f);
    sub_y(6, 10.0f);
    for (float d : {10.0f, 0.0f}) {
        if (d == 0.0f) {
            sub_y(5, 0.0f);
            sub_y(6, 0.0f);
        }
        for (uint32_t w = 7; w <= 0xd; ++w) sub_y(w, d);       // iris
        for (uint32_t w = 0x1e; w <= 0x23; ++w) sub_y(w, d);   // gadgets
        for (uint32_t w = 0xe; w <= 0x15; ++w) sub_y(w, d);    // a007 e arm
        sub_y(0x16, d);                                        // healthBorder
    }
    // A tela larga e os modos de TV 1/2 (o RRenderer +0x5c/+0x58) mudam as posições; o port tem 4:3 e o modo 0.
    const HudRendererInfo& r = hud_renderer();
    if (r.wide == 0 || r.mode == 1 || r.mode == 2) {
        if (r.mode == 1 || r.mode == 2) {
            add_key(5, 0, 78.0f);
            add_key(6, 0, 78.0f);
            add_key(0x1d, 0, 78.0f);
            for (uint32_t w = 7; w <= 0xd; ++w) add_key(w, 0, -78.0f);
            for (uint32_t w = 0xe; w <= 0x15; ++w) add_key(w, 0, -78.0f);
            for (uint32_t w = 0x1e; w <= 0x23; ++w) add_key(w, 0, 78.0f);
            add_key(0x16, 0, -78.0f);
        }
    } else {
        ++g_stats.guards;  // a tela larga (o port não a usa)
    }
    // O quadro de tempo (GetFoundationVideoMode: 2 PAL 25, 3 60, o resto 30).
    const int32_t vm = g_env.video_mode;
    h.frame_rate = vm == 2 ? 25.0f : (vm == 3 ? 60.0f : 30.0f);
    GHud_Reset(h);
    hud_exists() = true;  // DAT_0033ab6c
    return true;
}

// ---------------------------------------------------------------- o quadro

void GHud_Render(GHudData& h, bool draw) {
    const Simulation& s = simulation();
    const uint32_t n = uint32_t(s.steps) - u32(h, 0x360);
    set_u32(h, 0x364, n);
    set_u32(h, 0x360, uint32_t(s.steps));
    set_f32(h, 0x368, ee::mul(cvt_u(n), s.dt));
    int32_t st = h.result_state;
    if (uber_bond().word(0x5d8) >= 1 && u32(h, 0x360) >= 0x259) set_u32(h, 0x440, 1);
    st = h.result_state;
    if (st == 5 && ee::le(h.result_delay, 0.0f)) {
        // A "Music" para (AStream::Stop) e, acabada ela e a "Speech", o UberBond +0x53c desce e o fim (+0x440).
        if (g_env.result_streams_done ? g_env.result_streams_done() : hud_result_streams_done_default()) {
            int32_t& lives = uber_bond().word(0x53c);
            const bool low = lives < 1;
            lives -= 1;
            if (low) lives = 0;
            set_u32(h, 0x440, 1);
        }
    }
    // A fila de ações "HUDQ" (a pausa, as trapaças, o pular cena: driving/game/hud_pause.h).
    GHud_RenderQueue(h);
    // RViewCamera::Render(DAT_0033ab70): o GOrthoHudView (PreRender: nada que o port use; DoRender; PostRender).
    if (!g_store.loaded || !draw) return;
    GOrthoHudView_DoRender(h);
    // [driving] 0x0023aa70 GOrthoHudView::PostRender: o EExit e o ERestart de cada quadro (o port conta).
    if (h.exit != 0) ++g_stats.exits;
    if (h.restart != 0) ++g_stats.restarts;
}

void GOrthoHudView_DoRender(GHudData& h) {
    GGallery_SPRITE_Flush(gal(h));
    if (g_env.hud_on) {
        GHud_DrawScreenHits(h);
        GHud_DrawScreenFades(h);
        GHud_DrawBloodCurtain(h);
        if (hud_renderer().mode == 1) {
            const float bar = ee::mul(h.screen_height, 0.125f);
            draw_globals().untextured.depth_test = 1;
            Draw_DrawBox(0, 0, h.screen_width, bar, 0x7f000000u);
            Draw_DrawBox(0, ee::sub(h.screen_height, bar), h.screen_width, bar, 0x7f000000u);
        }
        GHud_DrawLetterBox(h);
    }
    // A matriz da vista (o EAGL::ViewPort): a escala do y pela altura / 480 e meio pixel do NTSC.
    float sx = 1.0f, tx = 0.0f;
    float sy = ee::div(h.screen_height, 480.0f);
    float ty = ee::div(g_env.video_mode == 1 ? 32.0f : -32.0f, h.screen_height);
    if (hud_renderer().wide != 0) {
        sx = 0.75f;
        tx = ee::add(tx, 80.0f);
        if (hud_renderer().mode != 2) {
            sy = ee::mul(sy, 0.75f);
            ty = ee::add(ty, ee::div(60.0f, h.screen_height));
        }
    }
    ty = ee::add(0.0f, ty);
    h.view_sx = sx;
    h.view_sy = sy;
    h.view_tx = ee::add(0.0f, tx);
    h.view_ty = ty;
    GHud_Draw(h);
}

void GHud_Draw(GHudData& h) {
    Draw_SetZDepthNear();
    ++g_stats.frames;
    GGalleryData& g = gal(h);
    if (!g_env.hud_on) {
        if (h.aiming == 0) return;
        GHud_DrawCrossHairs(h);
        GGallery_SPRITE_Flush(g);
        return;
    }
    if (!g_env.fly_by_wire) {
        if (h.result_state == 0) {
            GHud_DrawObjectiveMessage(h);
            GHud_DrawStylin(h);
            if (h.letterbox_mode == 1) {
                GHud_DrawWeapon(h);
                GHud_DrawCrossHairs(h);
                GHud_DrawHealth(h);
                GHud_DrawMessage(h);
                ++g_stats.debug_message;  // GHud::DrawDebugMessage (vazio no PS2)
                GHud_DrawButtonMessage(h);
                GHud_DrawGadget(h);
                GHud_DrawTimer(h);
                GGallery_SPRITE_Flush(g);
            }
        } else {
            GHud_DrawMissionResult(h);
            GGallery_SPRITE_Flush(g);
        }
    } else {
        GHud_DrawFlyByWire(h);
        GGallery_SPRITE_Flush(g);
    }
    if (h.paused == 0) {
        if (GSubtitlesData* s = GSubtitles_TheApp()) {
            HudFrameItem it;
            it.is_packet = true;
            GSubtitles_DrawSubtitles(*s, it.packet);
            if (!it.packet.words.empty()) hud_frame().push_back(std::move(it));
        }
    }
    GGallery_SPRITE_Flush(g);
    if (h.paused != 0) {
        if (h.pause_errors == 1) {
            ++g_stats.pause_errors;
            if (g_env.pause_draw) g_env.pause_draw(h, true);
            else GHud_DrawPauseErrors(h);
        } else {
            ++g_stats.pause_menu;
            if (g_env.pause_draw) g_env.pause_draw(h, false);
            else GHud_DrawPauseMenu(h);
        }
        GGallery_SPRITE_Flush(g);
    }
}

// ---------------------------------------------------------------- as tarjas, o escurecer, a cortina, os golpes

void GHud_DrawLetterBox(GHudData& h) {
    int32_t mode = h.letterbox_mode;
    if (mode == 1) return;
    if (mode == 2) {
        h.letterbox_count += 1;
        if (0x1d < h.letterbox_count) {
            h.letterbox_mode = 0;
            h.letterbox_count = 0x1e;
        }
        mode = h.letterbox_mode;
    }
    if (mode == 3) {
        h.letterbox_count -= 1;
        if (h.letterbox_count < 1) {
            h.letterbox_count = 0;
            h.letterbox_mode = 1;
        }
    }
    int32_t n = ee::cvt_w(ee::div(ee::mul(ee::cvt_s(h.letterbox_count), 65.0f), 30.0f));
    if (hud_renderer().mode == 1) n = ee::cvt_w(ee::mul(ee::cvt_s(n), 1.5f));
    const float bar = ee::cvt_s(n);
    draw_globals().untextured.depth_test = 1;
    Draw_DrawBox(0, 0, h.screen_width, bar, 0x7f000000u);
    Draw_DrawBox(0, ee::sub(h.screen_height, bar), h.screen_width, bar, 0x7f000000u);
}

void GHud_DrawScreenFades(GHudData& h) {
    float v;
    if (h.black_fading == 0) {
        v = h.black_fade;
    } else {
        if (h.paused == 0) {
            const float fv = ee::add(h.black_fade_from,
                                     ee::mul(h.black_fade_rate, ee::cvt_s(simulation().steps - h.black_fade_start)));
            h.black_fade = fv;
            bool done = false;
            if (ee::lt(fv, 0.0f)) {
                h.black_fade = 0;
                done = true;
            } else if (!ee::le(fv, 1.0f)) {
                h.black_fade = 1.0f;
                done = true;
            }
            if (done) {
                h.black_fading = 0;
                // ASoundManager::fHUDFadeValue = 0 (o port não o tem) e fHUDFade = 0.
                AStreamGlobalsView sg = astream_globals();
                sg.hud_fade = 0;
                astream_set_globals(sg);
            }
        } else {
            h.black_fade = 0;
            h.black_fading = 0;
            AStreamGlobalsView sg = astream_globals();
            sg.hud_fade = 0;
            astream_set_globals(sg);
        }
        v = h.black_fade;
    }
    const int32_t frames = g_env.renderer_frames;
    if (ee::lt(0.0f, v) || frames < 3) {
        uint32_t a = uint32_t(ee::cvt_w(ee::mul(v, 255.0f))) & 0xff;
        if (frames < 3) a = 0xff;
        if (hud_renderer().wide == 0)
            Draw_DrawBox(0, 0, h.screen_width, h.screen_height, a << 24);
        else
            Draw_DrawBox(-78.0f, 0, ee::add(h.screen_width, 156.0f), h.screen_height, a << 24);
    }
}

void GHud_DrawScreenHits(GHudData& h) {
    if (h.letterbox_mode != 1) return;
    const int32_t hits = i32(h, 0x328);
    if (hits < 1) {
        set_u32(h, 0x32c, 0);
        return;
    }
    const int32_t total = i32(h, 0x32c);
    if (total < 1) {
        set_u32(h, 0x32c, 0);
        return;
    }
    const uint32_t k = pup(h, 0x28c);
    const float kh = kf(k, 0x44), kw = kf(k, 0x40);
    float x0 = 0.0f, w = h.screen_width;
    const float sh = h.screen_height;
    if (hud_renderer().wide != 0) {
        x0 = -78.0f;
        w = ee::add(w, 156.0f);
    }
    GGalleryData& g = gal(h);
    GGallery_SPRITE_SetState(g, key_ptr(k));
    const float a = ee::mul(ee::div(ee::cvt_s(hits), ee::cvt_s(total)), 47.0f);
    g.scale[2] = 1.0f;
    g.scale[0] = ee::div(w, kw);
    g.scale[1] = ee::div(sh, kh);
    g.colour = uint32_t(round_half(a)) << 24 | 0x2000afu;
    GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(x0), 0, key_ptr(k), nullptr);
    set_u32(h, 0x328, uint32_t(hits - 1));
}

void GHud_DrawBloodCurtain(GHudData& h) {
    if (g_env.fly_by_wire || h.result_state == 0) return;
    const float t = h.blood_curtain;
    if (!ee::le(0.0f, t)) return;
    const float d = ee::sub(6.0f, t);
    const float k = ee::div(h.screen_height, 6.0f);
    const float hb = ee::mul(d, 21.333334f);  // 0x41aaaaab
    float x0 = 0.0f, w = h.screen_width;
    const float ht = ee::mul(d, k);
    if (hud_renderer().wide != 0 || hud_renderer().mode == 1) {
        x0 = -110.0f;
        w = ee::add(w, 220.0f);
    }
    // As duas faixas (o meio da textura e a borda de baixo) em subtração e depois em soma.
    const float uv0[2][4] = {{0, 0.05f, 1, 0}, {1, 0.15f, 1, 0}};
    const float uv1[2][4] = {{0, 0.05f, 1, 0}, {1, 0.95f, 1, 0}};
    const uint32_t tex = u32(h, 0);
    HudPrimState& st = draw_globals().textured;
    st.alpha = 0x8000000062ull;
    st.flags = (st.flags & ~0x40ull) | 0x40;
    Draw_DrawSprite(x0, 0, w, ht, 0x7f7f7f1fu, uv0, tex);
    Draw_DrawSprite(x0, ht, w, hb, 0x7f7f7f1fu, uv1, tex);
    st.alpha = 0x8000000048ull;
    st.flags = (st.flags & ~0x40ull) | 0x40;
    Draw_DrawSprite(x0, 0, w, ht, 0x7f00000fu, uv0, tex);
    Draw_DrawSprite(x0, ht, w, hb, 0x7f00000fu, uv1, tex);
    st.alpha = 0x8000000044ull;
    st.flags = (st.flags & ~0x40ull) | 0x40;
    const float r = ee::sub(h.blood_curtain, h.frame_time);
    h.blood_curtain = r;
    if (ee::le(r, 0.0f)) h.blood_curtain = 0.0f;
}

// ---------------------------------------------------------------- as fontes

float GHud_GetCenter(GHudData& h, float scale, uint32_t key, const char* text) {
    if (text == nullptr) text = LOCALE_Symbol(rd<int32_t>(key_ptr(key) + 0x34));
    GGalleryData& g = gal(h);
    GGallery_FONT_SetState(g, key_ptr(key));
    GGallery_FONT_SetScale(g, scale, scale);
    const float w = GGallery_FONT_Width(g, text);
    return ee::sub(h.screen_cx, ee::mul(w, 0.5f));
}

void GHud_DrawFontAndShadow(GHudData& h, float x, float y, float scale, uint32_t key, uint32_t shadow, uint32_t colour,
                            const char* text) {
    GGalleryData& g = gal(h);
    GGallery_FONT_SetState(g, key_ptr(key));
    if (text == nullptr) text = LOCALE_Symbol(rd<int32_t>(key_ptr(key) + 0x34));
    g.colour = shadow;
    GGallery_FONT_SetScale(g, scale, scale);
    GGallery_FONT_Draw(g, ee::add(x, 1.0f), ee::add(y, 1.0f), text);
    g.colour = colour;
    GGallery_FONT_SetScale(g, scale, scale);
    GGallery_FONT_Draw(g, x, y, text);
}

void GHud_DrawFont(GHudData& h, float x, float y, float scale, uint32_t key, uint32_t colour, const char* text) {
    GGalleryData& g = gal(h);
    GGallery_FONT_SetState(g, key_ptr(key));
    GGallery_FONT_SetScale(g, scale, scale);
    if (text == nullptr) text = LOCALE_Symbol(rd<int32_t>(key_ptr(key) + 0x34));
    g.colour = colour;
    GGallery_FONT_Draw(g, x, y, text);
}

// ---------------------------------------------------------------- as mensagens

void GHud_DrawObjectiveMessage(GHudData& h) {
    if (h.paused != 0) return;
    const int32_t next = h.objective_next;
    float t;
    if (next == -1) {
        t = h.objective_time;
    } else if (h.objective_kind == -1) {
        t = ee::sub(h.objective_time, h.frame_time);
        h.objective_time = t;
        if (!ee::le(t, 0.0f)) return;
        h.objective_frames = 0;
        h.objective_text = next;
        h.objective_kind = h.objective_next_kind;
        h.objective_alpha = 0;
        h.objective_time = 5.0f;
        menu_sound(8, "SFX_misstatus", "Menu Sounds", 2);
        h.objective_next_kind = -1;
        h.objective_next = -1;
        return;
    } else {
        t = h.objective_time;
    }
    if (!ee::lt(0.0f, t)) {
        if (next != -1) {
            h.objective_kind = -1;
            h.objective_time = 2.0f;
        }
        return;
    }
    const int32_t kind = h.objective_kind;
    int32_t label = 0x1869e;
    if (kind == 2)
        label = 99999;
    else if (kind == 3)
        label = 0x18598;
    const char* title = LOCALE_Symbol(label);
    const char* body = LOCALE_Symbol(h.objective_text);
    GGalleryData& g = gal(h);
    const uint32_t kt = pup(h, 0x268), kb = pup(h, 0x26c);
    const float s11 = ps2::ffloat(0x3f8ccccdu);
    GGallery_FONT_SetState(g, key_ptr(kt));
    GGallery_FONT_SetScale(g, s11, s11);
    const float wt = GGallery_FONT_Width(g, title);
    const float ht = GGallery_FONT_Height(g, title);
    const float wb = GGallery_FONT_Width(g, body);
    const float hb = GGallery_FONT_Height(g, body);
    const float wmax = ee::lt(wb, wt) ? wt : wb;
    const float y0 = ee::sub(kf(kt, 4), 5.0f);
    const float bw = ee::add(wmax, 20.0f);
    const float bh = ee::sub(ee::add(ee::add(ee::add(kf(kb, 4), 5.0f), hb), ht), y0);
    GGallery_QUAD_Draw2D(g, ee::sub(h.screen_cx, ee::mul(bw, 0.5f)), y0, bw, bh, 0x20000000u);
    float a = 1.0f;
    if (ee::lt(ee::cvt_s(h.objective_frames), 2.0f)) {
        const float al = h.objective_alpha;
        if (ee::lt(al, 0.5f)) {
            a = ee::add(al, al);
            h.objective_alpha = ee::add(al, h.frame_time);
        } else {
            h.objective_alpha = 0;
            h.objective_frames += 1;
            a = 1.0f;
        }
    }
    const float cx = GHud_GetCenter(h, s11, kt, title);
    GHud_DrawFontAndShadow(h, cx, kf(kt, 4), s11, kt, uint32_t(round_half(ee::mul(a, 70.0f))) << 24,
                           uint32_t(round_half(ee::mul(a, 127.0f))) << 24 | 0x64ccfeu, title);
    float frames = ee::cvt_s(h.objective_frames);
    if (frames == 2.0f) {
        menu_sound(8, h.objective_kind == 2 ? "SFX_objcomp" : "SFX_objective", "Menu Sounds", 2);
        h.objective_frames += 1;
        if (uint32_t(h.objective_kind - 2) < 2) h.objective_time = 1.0f;
        frames = ee::cvt_s(h.objective_frames);
    }
    if (ee::le(2.0f, frames)) {
        if (!(uint32_t(h.objective_kind - 2) < 2)) {
            const float bx = GHud_GetCenter(h, s11, kb, body);
            GHud_DrawFontAndShadow(h, bx, ee::add(kf(kb, 4), 5.0f), s11, kb, 0x46000000u, 0x7fb3dbfau, body);
        }
        h.objective_time = ee::sub(h.objective_time, h.frame_time);
    }
    if (ee::le(h.objective_time, 0.0f)) {
        h.objective_time = 0;
        h.objective_extra = 0;
    }
}

void GHud_DrawMessage(GHudData& h) {
    float a = 1.0f;
    if (ee::le(h.message_time, 0.0f)) {
        a = h.message_fade;
        if (ee::lt(0.0f, a)) {
            a = ee::add(a, a);
        } else {
            h.message_long = 0;
            a = 0.0f;
        }
    }
    if (ee::lt(0.0f, a)) {
        const char* text = LOCALE_Symbol(h.message_text);
        const float s12 = ps2::ffloat(0x3f99999au);
        const uint32_t k = pup(h, 0x25c);
        const float cx = GHud_GetCenter(h, s12, k, text);
        const uint32_t colour = uint32_t(round_half(ee::mul(a, 127.0f))) << 24 | 0xb3dbfau;
        const uint32_t shadow = uint32_t(round_half(ee::mul(a, 32.0f))) << 24;
        GGalleryData& g = gal(h);
        GGallery_FONT_SetState(g, key_ptr(k));
        GGallery_FONT_SetScale(g, s12, s12);
        const float w = GGallery_FONT_Width(g, text);
        const float ht = GGallery_FONT_Height(g, text);
        GGallery_QUAD_Draw2D(g, ee::sub(cx, 4.0f), 376.0f, ee::add(w, 12.0f), ee::add(ee::add(ht, 10.0f), 8.0f),
                             0x20000000u);
        GHud_DrawFontAndShadow(h, cx, 380.0f, s12, k, shadow, colour, text);
    }
    if (ee::lt(0.0f, h.message_fade)) {
        const float f = ee::sub(h.message_fade, h.frame_time);
        h.message_fade = f;
        if (ee::le(f, 0.0f)) {
            h.message_fade = 0;
            h.message_long = 0;
        }
    }
    if (ee::lt(0.0f, h.message_time)) {
        const float f = ee::sub(h.message_time, h.frame_time);
        h.message_time = f;
        if (ee::le(f, 0.0f)) {
            h.message_time = 0;
            h.message_fade = 0.5f;
        }
    }
}

int32_t GHud_GetButton(const char* text, char* before, char* after) {
    static const char* const kNeedles[4] = {"*A*", "*G*", "*P*", "*D*"};  // 0x003aae00
    for (int32_t i = 0; i < 4; ++i) {
        const char* s = std::strstr(text, kNeedles[i]);
        if (s != nullptr) {
            // Os buffers de quem chama têm 0x100 bytes (guarda do port: o PS2 passaria do fim).
            size_t n = std::strlen(text) - std::strlen(s);
            if (n > 0xff) {
                n = 0xff;
                ++g_stats.guards;
            }
            std::memcpy(before, text, n);  // sem o terminador (o buffer vem zerado)
            size_t m = std::strlen(s + 3);
            if (m > 0xff) {
                m = 0xff;
                ++g_stats.guards;
            }
            std::memcpy(after, s + 3, m);
            after[m] = 0;
            return i;
        }
    }
    return -1;
}

void GHud_DrawButtonMessage(GHudData& h) {
    if (!ee::lt(0.0f, h.button_time)) return;
    // O PS2 tem 0x40 bytes para cada parte na pilha; o port tem mais (um texto de botão maior estragaria a pilha lá;
    // os do jogo cabem).
    char before[0x100] = {}, after[0x100] = {};
    const float f = ee::add(h.button_fade, h.button_fade);
    const uint32_t colour = uint32_t(round_half(ee::mul(f, 127.0f))) << 24 | 0xb3dbfau;
    const uint32_t shadow = uint32_t(round_half(ee::mul(f, 70.0f))) << 24;
    // Com o +0x2fc positivo o texto é o rótulo dele (GSystem::LOCALE_Symbol(+0x2fc)); senão a cadeia do +0x2f8.
    const char* text;
    if (h.button_symbol < 1)
        text = nf::p32_ptr<const char>(uint32_t(h.button_text));
    else
        text = LOCALE_Symbol(h.button_symbol);
    if (text == nullptr) {
        ++g_stats.null_button_text;  // "Could not draw button text because the string is NULL!\n"
        GHud_KillButtonMessage(h);
        return;
    }
    const float s14 = ps2::ffloat(0x3fb33333u);
    const uint32_t k = pup(h, 0xc0);
    float x = GHud_GetCenter(h, s14, k, text);
    const int32_t b = GHud_GetButton(text, before, after);
    uint32_t sprite = 0;
    const char* first = text;
    bool split = true;
    switch (b) {
        case 0: sprite = pup(h, 0xd0); break;
        case 1: sprite = pup(h, 0xcc); break;
        case 2: sprite = pup(h, 0xc4); break;
        case 3: sprite = pup(h, 0xc8); break;
        default:
            split = false;
            first = nullptr;  // o PS2 passa o texto nulo: o rótulo +0x34 da chave
            break;
    }
    if (!split) {
        GHud_DrawFontAndShadow(h, x, 100.0f, s14, k, shadow, colour, first);
    } else {
        const uint32_t sc = uint32_t(round_half(ee::mul(f, 127.0f))) << 24 | 0xffffffu;
        GHud_DrawFontAndShadow(h, x, 100.0f, s14, k, shadow, colour, before);
        GGalleryData& g = gal(h);
        float w = GGallery_FONT_Width(g, before);
        if (hud_renderer().wide != 0) w = ee::mul(w, ps2::ffloat(0x3faaaaaau));
        x = ee::add(x, w);
        x = ee::add(x, GGallery_FONT_Width(g, "s"));
        GGallery_SPRITE_SetState(g, key_ptr(sprite));
        g.colour = sc;
        const float sy = ee::sub(100.0f, ee::mul(kf(sprite, 0x44), ps2::ffloat(0x3e4ccccdu)));
        GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(ee::add(x, 5.0f)), ee::cvt_w(sy), key_ptr(sprite), nullptr);
        x = ee::add(x, kf(sprite, 0x40));
        const float ws = GGallery_FONT_Width(g, "s");
        GHud_DrawFontAndShadow(h, ee::add(x, ws), 100.0f, s14, k, shadow, colour, after);
    }
    float bf = ee::sub(h.button_fade, h.frame_time);
    h.button_fade = bf;
    if (ee::le(bf, 0.0f)) h.button_fade = 0.5f;
    bf = ee::sub(h.button_time, h.frame_time);
    h.button_time = bf;
    if (ee::le(bf, 0.0f)) h.button_time = 0.0f;
}

void GHud_DrawTimer(GHudData& h) {
    const float half = ee::mul(h.screen_width, 0.5f);
    if (h.timer < 0) return;
    if (!mission_exists()) return;
    SMissionTimer* t = mission_timer(mission_manager(), h.timer);
    if (t == nullptr) {
        ++g_stats.guards;
        return;
    }
    const int32_t rate = simulation().frame_rate;
    if (rate == 0) {
        ++g_stats.guards;  // o trap(7) da divisão por zero
        return;
    }
    const int32_t v = SMissionTimer_GetValue(*t) / rate;
    const bool reverse = SMissionTimer_IsReverse(*t);
    if (v < 1) return;
    GGalleryData& g = gal(h);
    const uint32_t k = pup(h, 0x1f0);
    const float s14 = ps2::ffloat(0x3fb33333u);
    GGallery_FONT_SetState(g, key_ptr(k));
    GGallery_FONT_SetScale(g, s14, s14);
    const float w = GGallery_FONT_Width(g, "00:00");
    const float ht = GGallery_FONT_Height(g, "00:00");
    const float x = ee::sub(half, ee::mul(w, 0.5f));
    GGallery_QUAD_Draw2D(g, x, 33.0f, w, ee::sub(ee::add(ht, ht), 2.0f), 0x4605050fu);
    const int32_t sec = v % 60;
    char buf[0x40];
    std::snprintf(buf, sizeof buf, "%2d:%02d", v / 60, sec);
    if (!reverse || v / 60 != 0 || 9 < sec) {
        GHud_DrawFontAndShadow(h, ee::sub(half, ee::mul(w, 0.5f)), 35.0f, s14, k, 0x46000000u, 0x7fb3dbfau, buf);
    } else {
        GHud_DrawFontAndShadow(h, x, 35.0f, s14, k, 0x46000000u, 0x7f0505c8u, buf);
        if (sec == h.timer_beep_second - 1) {
            menu_sound(0, "SFX_timerbeep", "POV Weapons", 0);
            h.timer_beep_second = sec;
        }
    }
}

// ---------------------------------------------------------------- a vida, a arma, o aparelho, o "stylin"

void GHud_DrawHealth(GHudData& h) {
    if (h.hud_off != 0 || h.message_long != 0) return;
    if (GSubtitlesData* s = GSubtitles_TheApp())
        if (GSubtitles_IsSubtitleDisplaying(*s)) return;
    const SMissionManagerData& m = mission_manager();
    float vis = 1.0f;
    const float life = ee::div(m.player_health, m.player_health_max);
    const float shield = ee::div(m.player_shield, m.player_shield_max);
    // A vida aparece enquanto +0x2cc (o lampejo) ou +0x2d0 (o esmaecer) correm, ou sempre com o +0x434; o +0x2d4 pisca
    // com a vida baixa.
    if (h.no_health_fade == 0 && !ee::lt(0.0f, h.health_flash)) {
        const float fd = h.health_fade;
        vis = 0.0f;
        if (ee::lt(0.0f, fd)) vis = ee::add(fd, fd);
    }
    const float low = h.health_low;
    if (ee::lt(0.0f, low)) vis = ee::add(low, low);
    const float l7 = ee::mul(life, 7.0f);
    int32_t nl = ee::cvt_w(l7);
    int32_t ns = ee::cvt_w(ee::mul(shield, 8.0f));
    const float fl = ee::cvt_s(nl), fs = ee::cvt_s(ns);
    if (ee::le(life, 0.0f)) {
        vis = 1.0f;
        nl = 0;
        ns = 0;
    }
    GGalleryData& g = gal(h);
    if (ee::lt(0.0f, vis)) {
        uint32_t c7, c9;
        if (ee::lt(0.5f, life)) {
            c7 = 0x468ba852u;
            c9 = 0x138ba852u;
        } else if (ee::lt(0.25f, life)) {
            c7 = 0x464e98c6u;
            c9 = 0x134e98c6u;
        } else {
            c7 = 0x46252cb0u;
            c9 = 0x13252cb0u;
        }
        const float a7 = ee::mul(ee::cvt_s(int32_t(c7 >> 24)), vis);
        const uint32_t ia7 = uint32_t(round_half(a7));
        const uint32_t c8 = (c7 & 0xffffffu) | ia7 << 24;
        const float a9 = ee::mul(ee::cvt_s(int32_t(c9 >> 24)), vis);
        const float a12 = ee::mul(ee::cvt_s(int32_t(ia7 & 0xff)), ee::sub(l7, fl));
        const float s13 = ee::mul(ee::sub(ee::mul(shield, 8.0f), fs), 127.0f);
        const float a13 = ee::mul(ee::cvt_s(int32_t(uint32_t(round_half(s13)) & 0xff)), vis);
        const float a127 = ee::mul(vis, 127.0f);
        // O fundo da íris (sete peças) e as peças cheias da vida.
        for (uint32_t i = 0; i < 7; ++i) {
            const uint32_t k = pup(h, 0x1c + i * 4);
            GGallery_SPRITE_SetState(g, key_ptr(k));
            g.colour = (c9 & 0xffffffu) | uint32_t(round_half(a9)) << 24;
            GGallery_SPRITE_Draw2D(g, key_ptr(k));
        }
        for (int32_t i = 0; i < nl; ++i) {
            const uint32_t k = pup(h, 0x1c + uint32_t(i) * 4);
            GGallery_SPRITE_SetState(g, key_ptr(k));
            g.colour = c8;
            GGallery_SPRITE_Draw2D(g, key_ptr(k));
        }
        if (ee::lt(life, 1.0f)) {
            const uint32_t k = pup(h, 0x1c + uint32_t(nl) * 4);
            GGallery_SPRITE_SetState(g, key_ptr(k));
            g.colour = (c7 & 0xffffffu) | uint32_t(round_half(a12)) << 24;
            GGallery_SPRITE_Draw2D(g, key_ptr(k));
        }
        for (int32_t i = 0; i < ns; ++i) {
            const uint32_t k = pup(h, 0x38 + uint32_t(i) * 4);
            GGallery_SPRITE_SetState(g, key_ptr(k));
            g.colour = uint32_t(round_half(a127)) << 24 | 0xffffffu;
            GGallery_SPRITE_Draw2D(g, key_ptr(k));
        }
        if (ee::lt(shield, 1.0f)) {
            const uint32_t k = pup(h, 0x38 + uint32_t(ns) * 4);
            GGallery_SPRITE_SetState(g, key_ptr(k));
            g.colour = uint32_t(round_half(a13)) << 24 | 0xffffffu;
            GGallery_SPRITE_Draw2D(g, key_ptr(k));
        }
        const uint32_t kb = pup(h, 0x58);
        GGallery_SPRITE_SetState(g, key_ptr(kb));
        g.colour = c8;
        GGallery_SPRITE_Draw2D(g, key_ptr(kb));
    }
    if (i32(h, 0x2a8) != 0) {
        if (ee::cvt_w(ee::mul(life, 32.0f)) < ee::cvt_w(ee::mul(h.unknown36c, 32.0f))) {
            static const char* const kHits[3] = {"SFX_Bond_body_hit", "SFX_Bond_body_hit2", "SFX_Bond_body_hit3"};
            int32_t& hit = g_globals.body_hit;
            menu_sound(7, kHits[hit], "Player Grunts", 2);
            ++hit;
            if (hit == 3) hit = 0;
        }
    }
    if (ee::lt(0.0f, h.health_flash)) {
        const float f = ee::sub(h.health_flash, h.frame_time);
        h.health_flash = f;
        if (ee::le(f, 0.0f)) {
            h.health_flash = 0;
            h.health_fade = 0.5f;
        }
    }
    if (ee::lt(0.0f, h.health_fade)) {
        const float f = ee::sub(h.health_fade, h.frame_time);
        h.health_fade = f;
        if (ee::le(f, 0.0f)) h.health_fade = 0;
    }
    if (ee::le(life, 0.25f)) {
        const float f = ee::sub(h.health_low, h.frame_time);
        h.health_low = f;
        if (ee::le(f, 0.0f)) h.health_low = 0.5f;
    } else {
        h.health_low = 0;
    }
    if (life != h.unknown36c || shield != h.shield_last) {
        h.unknown36c = life;
        h.health_flash = 3.0f;
        h.shield_last = shield;
    }
}

void GHud_DrawWeapon(GHudData& h) {
    if (h.hud_off != 0 || h.message_long != 0) return;
    if (GSubtitlesData* s = GSubtitles_TheApp())
        if (GSubtitles_IsSubtitleDisplaying(*s)) return;
    SWeaponManagerData* wm = weapon_manager();
    if (wm == nullptr) {
        ++g_stats.guards;
        return;
    }
    const WeaponEntry& e = nf::p32_ptr<WeaponEntry>(wm->weapons)[wm->secondary];
    GGalleryData& g = gal(h);
    char buf[0x40];
    int32_t label = e.name_label;
    bool skip_ammo = false;
    int32_t ammo = e.ammo;
    if (e.name_label == 0x64b && e.ammo != 0) skip_ammo = true;
    if (!skip_ammo) {
        if (ammo < 0) ammo = 0;
        std::snprintf(buf, sizeof buf, "%d", ammo);
        GGallery_FONT_SetScale(g, 1.0f, 1.0f);
        const float w = GGallery_FONT_Width(g, buf);
        const uint32_t k = pup(h, 0x18);
        GHud_DrawFontAndShadow(h, ee::sub(kf(k, 0), w), kf(k, 4), 1.0f, k, 0x46000000u, 0x7fb3dbfau, buf);
        label = e.name_label;
    }
    std::snprintf(buf, sizeof buf, "%s", LOCALE_Symbol(label));
    const uint32_t k = pup(h, 0x14);
    GGallery_FONT_SetState(g, key_ptr(k));
    const float w = GGallery_FONT_Width(g, buf);
    GHud_DrawFontAndShadow(h, ee::sub(kf(k, 0), w), kf(k, 4), 1.0f, k, 0x46000000u, 0x7fb3dbfau, buf);
}

void GHud_DrawGadget(GHudData& h) {
    if (!ee::lt(0.0f, h.gadget_time)) return;
    const uint32_t k = h.gadget < 6 ? pup(h, h.gadget * 4 + 0x78) : pup(h, 0x84);
    float a = ee::mul(h.gadget_blink, 127.0f);
    a = ee::add(a, a);
    const float s = ee::add(h.gadget_slide, h.gadget_slide);
    const float kh = kf(k, 0x44), ky = kf(k, 4), cy = h.screen_cy;
    const float e = ee::mul(ee::mul(ee::sub(3.0f, ee::add(s, s)), s), s);
    const float x = ee::add(ee::mul(e, ee::sub(h.screen_cx, ee::mul(kf(k, 0x40), 0.5f))),
                            ee::mul(ee::sub(1.0f, e), kf(k, 0)));
    GGalleryData& g = gal(h);
    GGallery_SPRITE_SetState(g, key_ptr(k));
    g.colour = uint32_t(round_half(a)) << 24 | 0x7f7f7fu;
    const float y = ee::add(ee::mul(e, ee::sub(cy, kh)), ee::mul(ee::sub(1.0f, e), ky));
    GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(x), ee::cvt_w(y), key_ptr(k), nullptr);
    float t = ee::sub(h.gadget_time, h.frame_time);
    h.gadget_time = t;
    if (ee::le(t, 0.0f)) h.gadget_time = 0;
    t = h.gadget_slide;
    if (h.gadget_blinks < 1) {
        h.gadget_slide = ee::sub(t, h.frame_time);
        t = h.gadget_slide;
    }
    if (ee::le(t, 0.0f)) h.gadget_slide = 0;
    t = h.gadget_blink;
    if (0 < h.gadget_blinks) {
        h.gadget_blink = ee::sub(t, h.frame_time);
        t = h.gadget_blink;
    }
    if (ee::le(t, 0.0f)) {
        h.gadget_blink = 0.5f;
        h.gadget_blinks -= 1;
    }
}

void GHud_DrawStylin(GHudData& h) {
    if (h.stylin_state == 0) return;
    const uint32_t k = pup(h, 0x74);
    const float kw = kf(k, 0x40), kh = kf(k, 0x44);
    float lim = ee::div(100.0f, h.frame_rate);
    const float cx = h.screen_cx;
    GGalleryData& g = gal(h);
    if (h.stylin_state == 1) {
        const float t = h.stylin_time;
        float nt;
        if (ee::le(t, 0.0f) || ee::le(t, lim)) {
            nt = 3.0f;
            h.stylin_state = 2;
        } else {
            const float d = ee::sub(t, lim);
            const float dd = ee::add(d, d);
            const float sc = ee::div(ee::sub(lim, d), lim);
            float p = ee::div(dd, lim);
            while (ee::lt(1.0f, p)) p = ee::sub(p, 1.0f);
            const float sy = ee::mul(sc, VU0_Cos(p));
            GGallery_SPRITE_SetState(g, key_ptr(k));
            h.stylin_scale = sc;
            g.scale[1] = sy;
            g.scale[0] = sc;
            g.scale[2] = 1.0f;
            g.colour = 0x7f676767u;
            const float hy = ee::mul(ee::mul(sy, 0.5f), kh);
            const float hx = ee::mul(ee::mul(kw, 0.5f), sc);
            GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(ee::sub(cx, hx)), ee::cvt_w(ee::sub(95.0f, hy)), key_ptr(k), nullptr);
            nt = ee::sub(h.stylin_time, h.frame_time);
        }
        h.stylin_time = nt;
    }
    if (h.stylin_state == 2) {
        float vis = 1.0f;
        if (ee::le(h.stylin_time, 0.0f)) {
            const float f = h.stylin_fade;
            vis = 0.0f;
            if (ee::lt(0.0f, f)) vis = ee::add(f, f);
        }
        const uint32_t c = uint32_t(round_half(ee::mul(vis, 127.0f))) << 24 | 0x676767u;
        GGallery_SPRITE_SetState(g, key_ptr(k));
        g.colour = c;
        g.scale[2] = 1.0f;
        g.scale[1] = h.stylin_scale;
        g.scale[0] = h.stylin_scale;
        const float hs = ee::mul(h.stylin_scale, 0.5f);
        const float hy = ee::mul(hs, kh), hx = ee::mul(hs, kw);
        GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(ee::sub(cx, hx)), ee::cvt_w(ee::sub(95.0f, hy)), key_ptr(k), nullptr);
        if (ee::lt(0.0f, h.stylin_fade)) {
            const float f = ee::sub(h.stylin_fade, cvt_u(h.frame_steps));
            h.stylin_fade = f;
            if (ee::le(f, 0.0f)) h.stylin_fade = 0;
        }
        const float t = h.stylin_time;
        if (ee::lt(0.0f, t)) {
            const float r = ee::sub(t, h.frame_time);
            h.stylin_time = r;
            if (ee::le(r, 0.0f)) {
                h.stylin_time = 0;
                h.stylin_fade = 0.5f;
                h.stylin_state = 0;
            }
        }
    }
    (void)lim;
}

// ---------------------------------------------------------------- o resultado da missão

namespace {
// O estado do quadro (QUAD_SetState sem a chamada: a posição e o quatérnio da chave, a cor 0).
void quad_state_from(GGalleryData& g, uint32_t k) {
    const uint8_t* p = key_ptr(k);
    g.pos[2] = rdf(p + 8);
    g.pos[0] = rdf(p);
    g.pos[1] = rdf(p + 4);
    std::memcpy(g.quat, p + 0x10, 16);
    g.colour = 0;
}
}  // namespace

void GHud_DrawMissionResult(GHudData& h) {
    if (h.paused != 0) return;
    GGalleryData& g = gal(h);
    float a = 1.0f;
    uint8_t q[0x30];
    const float s13 = ps2::ffloat(0x3fa66666u);
    auto draw3 = [&](uint32_t o0, uint32_t o1, uint32_t o2) {
        for (uint32_t o : {o0, o1, o2}) {
            GGallery_QUAD_SetState(g, key_ptr(pup(h, o)));
            GGallery_QUAD_Draw(g, 1.0f, key_ptr(pup(h, o)) + 0x30);
        }
    };
    switch (h.result_state) {
        case 1: {
            const int32_t n = rd<int32_t>(key_ptr(pup(h, 0xa0)) + 0xc);
            const float t = ee::div(ee::cvt_s(h.unknown318), ee::cvt_s(n));
            for (auto [a0, b0] : {std::pair<uint32_t, uint32_t>{0x9c, 0xa0}, {0xa8, 0xac}, {0xb4, 0xb8}}) {
                GGallery_QUAD_Interpolate(t, key_ptr(pup(h, a0)) + 0x30, key_ptr(pup(h, b0)) + 0x30, q);
                quad_state_from(g, pup(h, a0));
                GGallery_QUAD_Draw(g, 1.0f, q);
            }
            if (h.unknown318 < n) {
                h.unknown318 += 1;
                if (n <= h.unknown318) {
                    h.unknown318 = 0;
                    h.result_state = 2;
                }
            }
            break;
        }
        case 2: {
            const int32_t n = rd<int32_t>(key_ptr(pup(h, 0xa4)) + 0xc) - rd<int32_t>(key_ptr(pup(h, 0xa0)) + 0xc);
            const float t = ee::div(ee::cvt_s(h.unknown318), ee::cvt_s(n));
            GGallery_QUAD_Interpolate(t, key_ptr(pup(h, 0xa0)) + 0x30, key_ptr(pup(h, 0xa4)) + 0x30, q);
            quad_state_from(g, pup(h, 0xa0));
            GGallery_QUAD_Draw(g, 1.0f, q);
            for (auto [a0, b0] : {std::pair<uint32_t, uint32_t>{0xac, 0xb0}, {0xb8, 0xbc}}) {
                uint8_t ik[0x34];
                GGallery_Get_InterpolatedKey(t, ik, key_ptr(pup(h, a0)), key_ptr(pup(h, b0)));
                GGallery_QUAD_SetState(g, ik);
                GGallery_QUAD_Draw(g, 1.0f, key_ptr(pup(h, a0)) + 0x30);
            }
            if (h.unknown318 < n) {
                h.unknown318 += 1;
                if (n <= h.unknown318) {
                    h.result_state = 3;
                    h.result_fade = 0.5f;
                    h.unknown318 = 0;
                }
            }
            break;
        }
        case 3: {
            draw3(0xa4, 0xb0, 0xbc);
            const float f = h.result_fade;
            if (ee::lt(0.0f, f)) a = ee::sub(1.0f, ee::add(f, f));
            const char* text = LOCALE_Symbol(h.result_text);
            const uint32_t k = pup(h, 0x98);
            const float cx = GHud_GetCenter(h, s13, k, text);
            GHud_DrawFontAndShadow(h, cx, kf(k, 4), s13, k, uint32_t(round_half(ee::mul(a, 70.0f))) << 24,
                                   uint32_t(round_half(ee::mul(a, 127.0f))) << 24 | 0xb3dbfau, text);
            if (!ee::lt(0.0f, h.result_fade)) return;
            const float r = ee::sub(h.result_fade, h.frame_time);
            h.result_fade = r;
            if (ee::lt(0.0f, r)) return;
            h.result_fade = 0;
            h.result_wait = 3.0f;
            h.result_state = 4;
            return;
        }
        case 4: {
            draw3(0xa4, 0xb0, 0xbc);
            const char* text = LOCALE_Symbol(h.result_text);
            const uint32_t k = pup(h, 0x98);
            const float cx = GHud_GetCenter(h, s13, k, text);
            GHud_DrawFontAndShadow(h, cx, kf(k, 4), s13, k, 0x46000000u, 0x7fb3dbfau, text);
            h.result_wait = ee::sub(h.result_wait, h.frame_time);
            if (ee::le(h.result_wait, 0.0f)) {
                // AStream::Get("Speech"): espera a fala acabar (ou outra mixagem tomar o canal).
                if (!(g_env.speech_done ? g_env.speech_done() : hud_speech_done_default())) return;
                h.result_wait = 0;
                h.result_delay = 1.5f;
                h.result_state = 5;
            }
            break;
        }
        case 5: {
            if (h.result_text != 0x18577) return;
            const float d = h.result_delay;
            const float t = ee::div(d, 1.5f);
            if (ee::lt(t, 0.2f) && h.paused == 0) {
                // GHud::PauseOn: o menu de fim de missão (RECOMEÇAR / SAIR; driving/game/hud_pause.h).
                if (g_env.pause_on)
                    g_env.pause_on(h);
                else
                    GHud_PauseOn(h);
                return;
            }
            if (ee::le(d, 0.0f)) return;
            const float r = ee::sub(d, h.frame_time);
            h.result_delay = r;
            if (ee::le(r, 0.0f)) h.result_delay = 0;
            if (ee::lt(0.0f, h.blood_curtain)) return;
            draw3(0xa4, 0xb0, 0xbc);
            const char* text = LOCALE_Symbol(h.result_text);
            const uint32_t k = pup(h, 0x98);
            const float cx = GHud_GetCenter(h, s13, k, text);
            GHud_DrawFontAndShadow(h, cx, kf(k, 4), s13, k, uint32_t(round_half(ee::mul(t, 70.0f))) << 24,
                                   uint32_t(round_half(ee::mul(t, 127.0f))) << 24 | 0xb3dbfau, text);
            break;
        }
        default:
            break;
    }
}

void GHud_DrawFlyByWire(GHudData& h) {
    int32_t& s_gadgets_enum = g_globals.gadgets_enum;
    if (h.paused == 0 && s_gadgets_enum == 0) menu_sound(0, "SFX_Gtorpbeep", "Weapons Init", 2);
    GGalleryData& g = gal(h);
    for (uint32_t o : {0x238u, 0x23cu, 0x240u, 0x244u}) {
        GGallery_BITMAP_SetState(g, key_ptr(pup(h, o)));
        GGallery_BITMAP_Draw2D(g, key_ptr(pup(h, o)));
    }
    if (s_gadgets_enum < 0xf) {
        GGallery_SPRITE_SetState(g, key_ptr(pup(h, 0x24c)));
        GGallery_SPRITE_Draw2D(g, key_ptr(pup(h, 0x24c)));
    }
    GGallery_FONT_SetState(g, key_ptr(pup(h, 0x248)));
    GGallery_FONT_SetScale(g, 0.75f, 0.75f);
    GGallery_FONT_Draw_Text(g, key_ptr(pup(h, 0x248)) + 0x30, nullptr);
    s_gadgets_enum += 1;
    if (0x1e < s_gadgets_enum) s_gadgets_enum = 0;
}

// ---------------------------------------------------------------- as miras

// [driving] 0x0024e340 fptoui (o __fixunssfsi do fp-bit: o __unpack_f e o deslocamento da fração): negativo, zero, os
// denormais e os NaN dão 0, o infinito e de 2^32 em diante 0xffffffff.
uint32_t fptoui(float v) {
    uint32_t b;
    std::memcpy(&b, &v, 4);
    const uint32_t exp = (b >> 23) & 0xff, frac = b & 0x7fffff;
    if (exp == 0) return 0;                  // zero ou denormal (expoente negativo)
    if (exp == 0xff && frac != 0) return 0;  // NaN
    if (b >> 31) return 0;                   // negativo
    if (exp == 0xff) return 0xffffffffu;     // infinito
    const int32_t e = int32_t(exp) - 127;
    if (e < 0) return 0;
    if (e >= 32) return 0xffffffffu;
    const uint32_t m = (frac | 0x800000u) << 7;  // o bit implícito no bit 30
    if (e >= 31) return m << (e - 30);
    return m >> (30 - e);
}

namespace {

const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // 0x00355bb0

// [driving] 0x002d1188 BuildTranslate(MATRIX4 &, float x, float y, float z): a identidade com a linha 3 = (x, y, z, 1).
void build_translate(float m[16], float x, float y, float z) {
    std::memcpy(m, kIdentity, sizeof kIdentity);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

void mat_mult(const float* a, const float* b, float* out) {
    float r[4][4];
    VU0_MATRIX4_mult(reinterpret_cast<const float(*)[4]>(a), reinterpret_cast<const float(*)[4]>(b), r);
    std::memcpy(out, r, sizeof r);
}

float hands_aim_fraction() { return g_env.aim_fraction ? g_env.aim_fraction() : AICharacterHands_GetAimFraction(); }

float player_zoom_percent() {
    if (g_env.zoom_percent) return g_env.zoom_percent();
    PlayerCamera* c = current_player_camera();
    return c ? RPlayerCamera_GetZoomPercent(*c) : 0.0f;
}

bool target_only_emp(uint32_t t) {
    if (g_env.only_emp) return g_env.only_emp(t);
    const WeaponsEnv& we = weapons_env();
    return we.target_only_emp ? we.target_only_emp(t) : false;
}

// Os carros do jogador: o objeto que a tabela virtual do carro (+0x68, a entrada +0x40/+0x44) dá (o som da arma).
// Sem o gancho: 1 o +0xc0 e 2 o AZoomObj::InTheZone do objeto de som do carro do jogador (driving/sound/audio.h); 3 (a
// entrada +0x54 da tabela, só das armas 0x1d e 0x1e, que Paris não tem: a do APlayerHeli e a do APlayerVehicle são 0)
// fica anotado.
void car_call(int32_t kind, float v) {
    g_stats.car_calls.push_back({kind, v});
    if (g_env.car_call) {
        g_env.car_call(kind, v);
        return;
    }
    if (kind == 1) audio_player_scene_c0(int32_t(v));
    else if (kind == 2) audio_player_zoom(int32_t(v));
}

// A posição do alvo na tela pela câmera do HUD (o +0x10 e o +0x14 do WTargetable).
void target_screen(const GHudData& h, uint32_t t, float& x, float& y) {
    const uint8_t* p = key_ptr(t);
    x = rdf(p + 0x10);
    y = rdf(p + 0x14);
    if (hud_renderer().wide == 0) x = ee::div(ee::sub(x, h.view_tx), h.view_sx);
    y = ee::div(ee::sub(y, h.view_ty), h.view_sy);
}

void sprite_scale(GGalleryData& g, float s88, float s80, float s84) {
    g.scale[2] = s88;
    g.scale[0] = s80;
    g.scale[1] = s84;
}

int32_t weapon_type() {
    SWeaponManagerData* wm = weapon_manager();
    if (wm == nullptr) {
        ++g_stats.guards;
        return -1;
    }
    return nf::p32_ptr<WeaponEntry>(wm->weapons)[wm->secondary].type;
}

}  // namespace

float RPlayerCamera_GetZoomPercent(const CameraModeInfo& mode, const AutoDriveArmInfo* arms, int8_t arm, float fov) {
    if (mode.type != 0x200) return 0.0f;
    if (arms == nullptr) {
        ++g_stats.guards;
        return 0.0f;
    }
    const float f2 = mode.default_fov, f1 = arms[arm].min_fov;
    if (!ee::lt(f1, f2)) return 0.0f;
    return ee::div(ee::sub(f2, fov), ee::sub(f2, f1));
}

float RPlayerCamera_GetZoomPercent(PlayerCamera& cam) {
    if (cam.tables == nullptr) {
        ++g_stats.guards;
        return 0.0f;
    }
    return RPlayerCamera_GetZoomPercent(cam.tables->mode(cam.d.mode), nf::raw(cam.tables->modes.autodrive_arms),
                                        cam.d.ad_arm, cam.d.fov);
}

void GHud_AimOn(GHudData& h) {
    h.aiming = 1;
    if (h.hud_off != 0) menu_sound(0, "SFX_whoosh", "1:Effects", 0);
}

void GHud_SetTarget(GHudData& h, const float pt[2]) {
    h.aim_x = pt[0];
    h.aim_y = pt[1];
    if (hud_renderer().wide == 0) h.aim_x = ee::div(ee::sub(h.aim_x, h.view_tx), h.view_sx);
    h.aim_y = ee::div(ee::sub(h.aim_y, h.view_ty), h.view_sy);
}

void GHud_SetTargetLockState(GHudData& h, int32_t lock) {
    const int32_t steps = simulation().steps;
    if (h.lock == 0 && lock == 1) {
        h.lock_y = -5000.0f;  // 0xc59c4000
        h.lock_frames = 0;
        set_u32(h, 0x3a0, uint32_t(steps + 0xf));
        h.lock_x = -5000.0f;
    }
    h.lock = lock;
    // O ATargeting (+0x39c) das pistas que o criam (o som da trava), fora do voo pelo fio (FUN_0023eab0).
    if (u32(h, 0x39c) != 0 && !g_env.fly_by_wire) ATargeting_SetState(*nf::p32_ptr<ATargetingData>(u32(h, 0x39c)), lock);
}

void WTargetPicker_DrawTargetingSystem(GHudData& h) {
    const TargetPickerData& p = target_picker();
    GHud_SetTargetLockState(h, p.lock);
    if (p.active == 0) return;
    if (p.mode == 0) {
        float pt[2];
        WTargetPicker_GetScreenPos(p.world_target, pt);
        GHud_SetTarget(h, pt);
    } else if (p.mode == 1) {
        GHud_SetTarget(h, p.cursor);
    }
}

uint32_t GHud_DrawZoomingCrossHairs(GHudData& h, uint32_t key) {
    if (h.zooming == 0) h.zooming = 1;
    float m0[16], m1[16], m2[16];
    std::memcpy(m0, kIdentity, 64);
    std::memcpy(m1, kIdentity, 64);
    std::memcpy(m2, kIdentity, 64);
    const uint32_t t = WTargetPicker_GetSelectedTarget();
    float x = 0, y = 0;
    if (t != 0) {
        target_screen(h, t, x, y);
    } else {
        // Sem alvo o PS2 lê os endereços 0x10 e 0x14 (o começo da RAM: zeros): o port usa zeros (guarda).
        ++g_stats.guards;
        if (hud_renderer().wide == 0) x = ee::div(ee::sub(0.0f, h.view_tx), h.view_sx);
        y = ee::div(ee::sub(0.0f, h.view_ty), h.view_sy);
    }
    h.lock_x = x;
    h.lock_y = y;
    const float kw = kf(key, 0x40);
    const float frac = ee::div(cvt_u(h.lock_frames), 7.0f);
    if (h.lock_frames < 7u) {
        const uint32_t v = h.lock_frames + 1;
        h.lock_frames_seen = v;
        h.lock_frames = v;
    }
    const float s = ee::sub(1.0f, ee::mul(ee::sub(1.0f, ee::div(kf(pup(h, 0x27c), 0x40), ee::add(kw, kw))), frac));
    uint32_t draw = 0x7fffffffu, ret = 0x7fffffffu;
    const float k95 = ps2::ffloat(0x3f733333u);
    switch (weapon_type()) {
        case 0:
            draw = ee::lt(k95, frac) ? 0x5f0505a5u : 0x1f0505a5u;
            ret = 0x1f0505a5u;
            break;
        case 2:
        case 0x1c:
        case 0x1e:
            if (ee::lt(k95, frac)) {
                draw = 0x5f0505a5u;
                ret = 0x1f0505a5u;
            } else {
                draw = 0x1f0505a5u;
                ret = 0x46ffffffu;
            }
            break;
        case 0x15:
            if (ee::lt(k95, frac)) {
                draw = 0x7f0505a5u;
                ret = 0x1f0505a5u;
            } else {
                draw = 0x1f0505a5u;
                ret = 0x46ffffffu;
            }
            break;
        default:
            break;
    }
    BuildRotate(m0, ee::sub(ee::mul(frac, 90.0f), 90.0f), 0.0f, 0.0f, 1.0f);
    build_translate(m1, x, y, 0.0f);
    mat_mult(m0, m1, m2);
    Draw_SetModelMatrix(m2);
    GGalleryData& g = gal(h);
    GGallery_SPRITE_SetState(g, key_ptr(key));
    g.colour = draw;
    sprite_scale(g, 1.0f, s, s);
    GGallery_SPRITE_Draw2D_At(g, 0, 0, key_ptr(key), nullptr);
    Draw_SetModelMatrix(nullptr);
    return ret;
}

void GHud_DrawFadingCrossHairs(GHudData& h, uint32_t key) {
    float m0[16];
    std::memcpy(m0, kIdentity, 64);
    const float kw = kf(key, 0x40);
    const float f2 = ee::sub(1.0f, ee::div(kf(pup(h, 0x27c), 0x40), ee::add(kw, kw)));
    const float s = ee::sub(1.0f, ee::mul(f2, ee::div(cvt_u(h.lock_frames_seen), 7.0f)));
    const uint32_t n = h.lock_frames;
    const float t = ee::div(cvt_u(n), 7.0f);
    if (n == 0) return;
    h.lock_frames = n - 1;
    WTargetPicker_GetSelectedTarget();
    const float a = ee::mul(t, weapon_type() == 0x15 ? 127.0f : 95.0f);
    const uint32_t colour = uint32_t(round_half(a)) << 24 | 0x505a5u;
    build_translate(m0, h.lock_x, h.lock_y, 0.0f);
    Draw_SetModelMatrix(m0);
    GGalleryData& g = gal(h);
    GGallery_SPRITE_SetState(g, key_ptr(key));
    g.colour = colour;
    sprite_scale(g, 1.0f, s, s);
    GGallery_SPRITE_Draw2D_At(g, 0, 0, key_ptr(pup(h, 0x288)), nullptr);
    Draw_SetModelMatrix(nullptr);
}

void GHud_DrawCrossHairs(GHudData& h) {
    float m0[16], m1[16], m2[16];  // sp+0, sp+0x40, sp+0x80
    std::memcpy(m0, kIdentity, 64);
    std::memcpy(m1, kIdentity, 64);
    std::memcpy(m2, kIdentity, 64);
    const char* track = uber_bond().text(0x4f4);
    const bool paris = std::strcmp(track, "paris_mis01") == 0;
    const bool jungle = std::strcmp(track, "junglea_mis13a") == 0;
    const bool snow = std::strcmp(track, "snow2a_mis4") == 0;
    SWeaponManagerData* wm = weapon_manager();
    if (wm == nullptr) {
        ++g_stats.guards;
        return;
    }
    const WeaponEntry& e = nf::p32_ptr<WeaponEntry>(wm->weapons)[wm->secondary];
    GGalleryData& g = gal(h);
    const TargetPickerData& p = target_picker();
    const bool wide = hud_renderer().wide != 0;
    float f27 = 1.0f;
    int32_t type = e.type;
    if (type == 0x15 || type == 0x1c) {
        const int32_t n = p.count;
        const uint32_t sel = WTargetPicker_GetSelectedTarget();
        for (int32_t i = 0; i < n; ++i) {
            const uint32_t t = p.sorted[i];
            float x, y;
            target_screen(h, t, x, y);
            if (rd<int32_t>(key_ptr(t) + 0x44) != 0) {
                uint32_t col = 0x46ffffffu;
                if (t == sel && h.lock != 0) col = GHud_DrawZoomingCrossHairs(h, pup(h, 0x288));
                build_translate(m2, x, y, 0.0f);
                const uint32_t k = pup(h, 0x27c);
                const float kh = ee::mul(kf(k, 0x44), f27), kw = ee::mul(kf(k, 0x40), f27);
                Draw_SetModelMatrix(m2);
                GGallery_SPRITE_SetState(g, key_ptr(k));
                g.colour = col;
                sprite_scale(g, f27, f27, f27);
                GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(ee::mul(ee::neg(kw), 0.5f)), ee::cvt_w(ee::mul(ee::neg(kh), 0.5f)),
                                          key_ptr(k), nullptr);
                Draw_SetModelMatrix(nullptr);
            } else {
                // Fora da tela: a seta na borda, girada para o alvo (o atan2f do newlib: o nf::fm::atan2).
                const float ang = ee::add(ee::mul(nf::fm::atan2(ee::sub(y, h.screen_cy), ee::sub(x, h.screen_cx)),
                                                  ps2::ffloat(0x42652ee2u)),
                                          90.0f);
                if (wide) x = ee::add(h.screen_cx, ee::mul(ee::sub(x, h.screen_cx), ps2::ffloat(0x3faaaaaau)));
                build_translate(m1, x, y, 0.0f);
                BuildRotate(m0, ang, 0.0f, 0.0f, 1.0f);
                mat_mult(m0, m1, m2);
                const uint32_t k = pup(h, 0x280);
                const float kh = kf(k, 0x44), kw = kf(k, 0x40);
                Draw_SetModelMatrix(m2);
                GGallery_SPRITE_SetState(g, key_ptr(k));
                g.colour = 0x46ffffffu;
                sprite_scale(g, f27, f27, f27);
                GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(ee::mul(ee::neg(kw), 0.5f)), ee::cvt_w(ee::mul(ee::neg(kh), 0.5f)),
                                          key_ptr(k), nullptr);
                Draw_SetModelMatrix(nullptr);
            }
        }
        type = e.type;
    } else if (type == 2) {
        if (!(paris && h.hud_off != 0)) {
            int32_t n = p.count;
            if (snow) {
                const int32_t lim = wm->multi_damage != 0 ? 4 : 2;
                if (lim < n) n = lim;
            }
            const uint32_t sel = WTargetPicker_GetSelectedTarget();
            for (int32_t i = 0; i < n; ++i) {
                const uint32_t t = p.sorted[i];
                const uint8_t* tp = key_ptr(t);
                const uint32_t owner = rd<int32_t>(tp + 0x34) == 1 ? rd<uint32_t>(tp + 0x48) : 0u;
                if (owner != 0 && target_only_emp(t)) continue;
                f27 = 1.0f;
                float x, y;
                target_screen(h, t, x, y);
                if (rd<int32_t>(tp + 0x44) == 0) continue;
                uint32_t col = 0x46ffffffu;
                if (t == sel && h.lock != 0) col = GHud_DrawZoomingCrossHairs(h, pup(h, 0x288));
                build_translate(m2, x, y, 0.0f);
                const uint32_t k = pup(h, 0x27c);
                const float kh = ee::mul(kf(k, 0x44), f27), kw = ee::mul(kf(k, 0x40), f27);
                Draw_SetModelMatrix(m2);
                GGallery_SPRITE_SetState(g, key_ptr(k));
                g.colour = col;
                sprite_scale(g, 1.0f, f27, f27);
                GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(ee::mul(ee::neg(kw), 0.5f)), ee::cvt_w(ee::mul(ee::neg(kh), 0.5f)),
                                          key_ptr(k), nullptr);
                Draw_SetModelMatrix(nullptr);
            }
        }
        type = e.type;
    }
    if (type == 0x1e) {
        // A barra da recarga (rechargeBar): a fração do intervalo de tiro desde o último, presa em 1.
        float f = ee::div(ee::cvt_s(simulation().steps - e.last_fire[0]), ee::cvt_s(e.fire_delay));
        if (!ee::lt(f, 1.0f)) f = 1.0f;
        car_call(3, f);  // o som da carga (a tabela virtual +0xac, a entrada +0x54)
        const uint32_t k = pup(h, 0x218);
        const float kh = kf(k, 0x44), kw = kf(k, 0x40), kx = kf(k, 0), ky = ee::sub(kf(k, 4), 150.0f);
        uint32_t n = fptoui(ee::div(ee::mul(f, 150.0f), kh));
        GGallery_QUAD_Draw2D(g, kx, ee::add(ky, kh), kw, 150.0f, 0x4605050fu);
        GGallery_SPRITE_SetState(g, key_ptr(k));
        g.colour = 0x7fffffffu;
        if (n > 0x10000u) {
            ++g_stats.guards;  // o PS2 desenharia até 2^32 peças
            n = 0x10000u;
        }
        for (uint32_t i = 0; i < n; ++i)
            GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(kf(k, 0)), ee::cvt_w(ee::sub(kf(k, 4), ee::mul(kh, cvt_u(i)))),
                                      key_ptr(k), nullptr);
    }
    if (h.unknown438 != 0 && h.aiming == 0 && !jungle) return;
    if (u32(h, 0x2a8) == 0) {
        type = e.type;
        if (type == 0) {
            if (jungle && h.lock != 0) {
                GHud_DrawZoomingCrossHairs(h, pup(h, 0x288));
                WTargetPicker_GetSelectedTarget();
            }
        } else if (type == 0x15) {
            const uint32_t k = pup(h, 0x284);
            const uint32_t col = 0x1fffffffu;
            float fx = ee::div(ee::sub(h.screen_cx, h.view_tx), h.view_sx);
            float fy = ee::div(ee::sub(h.screen_cy, h.view_ty), h.view_sy);
            const float dw = ee::mul(ee::add(kf(k, 0x40), 118.0f), h.view_sx);
            const float dh = ee::mul(ee::add(kf(k, 0x44), 10.0f), h.view_sy);
            GGallery_SPRITE_SetState(g, key_ptr(k));
            g.colour = col;
            const int32_t x0 = ee::cvt_w(ee::sub(fx, dw)), y0 = ee::cvt_w(ee::sub(fy, dh));
            sprite_scale(g, 1.0f, 1.0f, 1.0f);
            GGallery_SPRITE_Draw2D_At(g, x0, y0, key_ptr(k), nullptr);
            GGallery_SPRITE_SetState(g, key_ptr(k));
            fx = ee::add(fx, dw);
            g.colour = col;
            const int32_t x1 = ee::cvt_w(fx);
            sprite_scale(g, 1.0f, -1.0f, 1.0f);
            GGallery_SPRITE_Draw2D_At(g, x1, y0, key_ptr(k), nullptr);
            GGallery_SPRITE_SetState(g, key_ptr(k));
            fy = ee::add(fy, dh);
            g.colour = col;
            const int32_t y1 = ee::cvt_w(fy);
            sprite_scale(g, 1.0f, -1.0f, -1.0f);
            GGallery_SPRITE_Draw2D_At(g, x1, y1, key_ptr(k), nullptr);
            GGallery_SPRITE_SetState(g, key_ptr(k));
            g.colour = col;
            sprite_scale(g, 1.0f, 1.0f, -1.0f);
            GGallery_SPRITE_Draw2D_At(g, x0, y1, key_ptr(k), nullptr);
            if (h.lock != 0) return;
            const uint32_t kn = pup(h, 0x278);
            build_translate(m1, h.aim_x, h.aim_y, 0.0f);
            const float kh = ee::mul(kf(kn, 0x44), f27), kw = ee::mul(kf(kn, 0x40), f27);
            Draw_SetModelMatrix(m1);
            GGallery_SPRITE_SetState(g, key_ptr(kn));
            g.colour = col;
            sprite_scale(g, 1.0f, f27, f27);
            GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(ee::mul(ee::neg(kw), 0.5f)), ee::cvt_w(ee::mul(ee::neg(kh), 0.5f)),
                                      key_ptr(kn), nullptr);
            Draw_SetModelMatrix(nullptr);
        }
    } else {
        // O zoom da mira (o RPlayerCamera do jogador) e o som do zoom (o +0xc0 e o AZoomObj +0x1ac do objeto do carro).
        if (ee::lt(ps2::ffloat(0x3a83126fu), ee::abs(ee::sub(h.zoom_pct, player_zoom_percent())))) {
            h.zoom_pct = player_zoom_percent();
            car_call(1, 1.0f);
        } else {
            car_call(1, 0.0f);
        }
        if (h.hud_off != 0) car_call(2, ee::lt(ps2::ffloat(0x3f7d70a4u), h.zoom_pct) ? 1.0f : 0.0f);
        // A mira comum (crossNorm, ou crossZoom mirando) na posição do alvo da mira.
        auto common = [&](uint32_t col) {
            const uint32_t k = pup(h, h.aiming != 0 ? 0x274 : 0x270);
            const float kw = ee::mul(kf(k, 0x40), f27), kh = ee::mul(kf(k, 0x44), f27);
            GGallery_SPRITE_SetState(g, key_ptr(k));
            g.colour = col;
            sprite_scale(g, 1.0f, f27, f27);
            const float x = ee::sub(h.aim_x, ee::mul(kw, 0.5f)), y = ee::sub(h.aim_y, ee::mul(kh, 0.5f));
            GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(x), ee::cvt_w(y), key_ptr(k), nullptr);
        };
        switch (e.type) {
            case 0x1a: {
                if (h.aiming == 0) {
                    if (h.hud_off != 0) break;
                    const uint32_t k = pup(h, 0x270);
                    const float kw = ee::mul(kf(k, 0x40), f27), kh = ee::mul(kf(k, 0x44), f27);
                    GGallery_SPRITE_SetState(g, key_ptr(k));
                    g.colour = 0x5f252cb0u;
                    sprite_scale(g, 1.0f, f27, f27);
                    const float x = ee::sub(h.aim_x, ee::mul(kw, 0.5f)), y = ee::sub(h.aim_y, ee::mul(kh, 0.5f));
                    GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(x), ee::cvt_w(y), key_ptr(k), nullptr);
                    break;
                }
                if (h.hud_off != 0) {
                    int32_t& vp = g_globals.auto_aim_blink;  // 0x0033b1d0
                    if (vp < 0xf) {
                        GGallery_SPRITE_SetState(g, key_ptr(pup(h, 0x254)));
                        GGallery_SPRITE_Draw2D(g, key_ptr(pup(h, 0x254)));
                    }
                    GGallery_FONT_SetState(g, key_ptr(pup(h, 0x250)));
                    GGallery_FONT_Draw_Text(g, key_ptr(pup(h, 0x250)) + 0x30, nullptr);
                    vp += 1;
                    if (0x1e < vp) vp = 0;
                }
                // A luneta do fuzil de Bond: as quatro linhas, o círculo, os traços que giram e a mira no centro.
                uint32_t s3 = 0x7fffffffu;
                const float f28 = 1.0f;
                float cx, cy;
                if (h.hud_off != 0) {
                    cx = ee::div(ee::sub(h.screen_cx, h.view_tx), h.view_sx);
                    cy = ee::div(ee::sub(h.screen_cy, h.view_ty), h.view_sy);
                } else {
                    cx = h.aim_x;
                    cy = h.aim_y;
                }
                const float frac = hands_aim_fraction();
                f27 = 2.0f;
                s3 = (s3 & 0xffffffu) | uint32_t(round_half(ee::mul(ee::cvt_s(int32_t(s3 >> 24)), frac))) << 24;
                SniperZoomState& sz = sniper_zoom();
                sz.alpha = round_half(ee::mul(frac, 255.0f));
                sz.on = 1;
                const float r152 = 152.0f, line = 1.25f;
                GGallery_QUAD_Draw2D(g, cx, ee::sub(0.0f, h.view_ty), line, ee::add(ee::sub(cy, r152), h.view_ty), s3);
                const uint32_t kl = pup(h, 0x90), kc = pup(h, 0x94);
                const float lw = kf(kl, 0x40);
                float lh = kf(kl, 0x44);
                float w1 = ee::sub(cx, r152), x1 = 0.0f;
                if (wide) {
                    w1 = ee::add(w1, 78.0f);
                    x1 = -78.0f;
                }
                GGallery_QUAD_Draw2D(g, ee::sub(x1, h.view_tx), cy, ee::add(w1, h.view_tx), line, s3);
                const float yb = ee::add(cy, r152);
                GGallery_QUAD_Draw2D(g, cx, yb, line, ee::add(yb, h.view_ty), s3);
                const float xr = ee::add(cx, r152);
                float wr = ee::sub(h.screen_width, xr);
                if (wide) wr = ee::add(wr, 78.0f);
                GGallery_QUAD_Draw2D(g, xr, cy, ee::add(wr, h.view_tx), line, s3);
                lh = ee::neg(lh);
                const uint32_t rgb = s3 & 0xffffffu;
                const int32_t a7 = int32_t(s3 >> 24) & 0xff;
                const int32_t ny = ee::cvt_w(lh);
                GGallery_SPRITE_SetState(g, key_ptr(kc));
                g.colour = s3;
                GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(cx), ee::cvt_w(cy), key_ptr(kc), nullptr);
                float m3[16];
                build_translate(m1, cx, cy, 0.0f);
                BuildRotate(m0, -45.0f, 0.0f, 0.0f, 1.0f);
                mat_mult(m0, m1, m2);
                Draw_SetModelMatrix(m2);
                std::memcpy(m3, m2, 64);
                for (int32_t i = 9; i >= 0; --i) {
                    GGallery_SPRITE_SetState(g, key_ptr(kl));
                    g.colour = rgb | uint32_t(round_half(ee::mul(ee::cvt_s(a7), 0.3f))) << 24;
                    GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(ee::sub(ee::mul(f28, -130.0f), lw)), ny, key_ptr(kl), nullptr);
                    BuildRotate(m0, ee::div(8.0f, f28), 0.0f, 0.0f, 1.0f);
                    mat_mult(m0, m3, m3);
                    Draw_SetModelMatrix(m3);
                }
                Draw_SetModelMatrix(m2);
                if (0 < ee::cvt_w(ee::mul(h.zoom_pct, 10.0f))) {
                    int32_t i = 0;
                    do {
                        GGallery_SPRITE_SetState(g, key_ptr(kl));
                        g.colour = rgb | uint32_t(round_half(ee::mul(ee::cvt_s(a7), 1.5f))) << 24;
                        ++i;
                        GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(ee::sub(ee::mul(f28, -130.0f), lw)), ny, key_ptr(kl), nullptr);
                        BuildRotate(m0, ee::div(8.0f, f28), 0.0f, 0.0f, 1.0f);
                        mat_mult(m0, m2, m2);
                        Draw_SetModelMatrix(m2);
                    } while (i < ee::cvt_w(ee::mul(h.zoom_pct, 10.0f)));
                }
                const float z10 = ee::mul(h.zoom_pct, 10.0f);
                const float part = ee::sub(z10, ee::cvt_s(ee::cvt_w(z10)));
                const float ap = ee::mul(ee::mul(ee::cvt_s(a7), 1.5f), part);
                GGallery_SPRITE_SetState(g, key_ptr(kl));
                g.colour = rgb | uint32_t(round_half(ap)) << 24;
                GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(ee::sub(ee::mul(f28, -130.0f), lw)), ee::cvt_w(lh), key_ptr(kl),
                                          nullptr);
                Draw_SetModelMatrix(nullptr);
                const uint32_t kn = pup(h, 0x278);
                const float nw = ee::mul(kf(kn, 0x40), f27), nh = ee::mul(kf(kn, 0x44), f27);
                GGallery_SPRITE_SetState(g, key_ptr(kn));
                g.colour = s3;
                sprite_scale(g, 1.0f, f27, f27);
                GGallery_SPRITE_Draw2D_At(g, ee::cvt_w(ee::sub(cx, ee::mul(nw, 0.5f))),
                                          ee::cvt_w(ee::sub(cy, ee::mul(nh, 0.5f))), key_ptr(kn), nullptr);
                break;
            }
            case 0x1c:
            case 0x1e:
                if (h.lock != 0) return;
                common(0x7f252cb0u);
                break;
            case 0x1d:
                common(0x7f252cb0u);
                car_call(3, 1.0f);
                break;
            default:  // 0x18, 0x19, 0x1b e os outros
                common(0x7f252cb0u);
                break;
        }
    }
    if (h.lock == 0) {
        GHud_DrawFadingCrossHairs(h, pup(h, 0x288));
        h.zooming = 0;
    }
}

}  // namespace drv
