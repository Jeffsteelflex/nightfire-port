// O jogador. Ver player.h.
//
// Divergências:
// - Player_AddNewStartPos ignora pontos além dos 32 de player_start, e Player_Start/Player_Init
//   tratam canal de switch acima de 255 como desligado (para não sair das tabelas).
// - Só no PC: o mouse gira a visão diretamente (Player_Update, entre Player_Aiming e
//   Player_ViewClamping), somando ao giro do analógico, nos mesmos subestados em que o analógico
//   gira (platform/psi_input.h); mirando, dividido pelo zoom do jogo vezes o da mira moderna.
// - Só no PC: com a mira moderna numa arma sem luneta (engine/player_ads.h), Player_Aiming trata a arma como a de
//   luneta (a mira fica no centro e o analógico gira a visão), e Player_Move deixa andar para a frente e para trás
//   mirando (a 85%; o modo de mira do jogo só deixa andar de lado), em qualquer arma no estilo moderno.
// - Se a arma na mão (BLData::gun, de Player_InitWeapon) não puder ser criada, o port pula o que
//   depende dela em Player_Update; o original nunca a lê nula.
// - Player_Init no multijogador: as tabelas de peles (MP_skins) ainda não existem; usa a pele do
//   modo história.
// - Nas contas da VU0 o temporário escalado (vmulx.xyz sobre uma variável da pilha sem valor) entra
//   na soma .xyzw com w indefinido; aqui esse w vale 0.
// - MP_RegisterBulletHit e Player_CheckForDeath no multijogador: sem texto (Txt_GetStringFromHeap e
//   Txt_BindLabel ainda devolvem nulo), as mensagens de fogo amigo e de morte não são montadas.

#include "engine/lights.h"
#include "engine/player.h"
#include "engine/player_weapon.h"
#include "engine/player_ads.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine/anim.h"
#include "engine/build.h"
#include "engine/bullet.h"
#include "engine/collide.h"
#include "engine/drone_stubs.h"
#include "engine/game.h"
#include "engine/gameflow.h"
#include "engine/gamestate.h"
#include "engine/hashtable.h"
#include "engine/hud.h"
#include "engine/input.h"
#include "engine/modules.h"
#include "engine/object.h"
#include "engine/parsemap.h"
#include "engine/pickup.h"
#include "engine/resetmap.h"
#include "engine/vecmath.h"
#include "engine/view.h"
#include "engine/door.h"
#include "platform/pending.h"
#include "platform/psi.h"
#include "platform/psi_input.h"

PLAYERSTART_tag player_start[kPlayerStarts];
uint32_t player_start_positions_index = 0;
uint32_t player_start_position = 0;
PlayerInitCall g_player_init_call;
BLData* glb_blokes[4];
_VECTOR WldGravity{0.0f, 0.0f, 0.0f, 0.0f};
uint8_t surfaced = 0;
uint8_t debug_scanmode = 0;
uint8_t debug_clipmode = 0;
uint8_t benchmark = 0;

float Plr_DMod_Multi = 1.0f;
float Plr_DMod_Head = 1.0f;
float Plr_DMod_LowerLimb = 1.0f;
float Plr_DMod_UpperLimb = 1.0f;
float Plr_DMod_Easy = 1.0f;
float Plr_DMod_Normal = 1.0f;
float Plr_DMod_Hard = 1.0f;

float Plr_AimSpeed_X = 0.15f;
float Plr_AimSpeed_Y = 0.16f;
float Plr_AimTurnSpeed_X = 0.05f;
float Plr_AimTurnSpeed_Y = 0.06f;
float Plr_ScopeSpeed_X = 0.015f;
float Plr_ScopeSpeed_X_Mul = 3.0f;
float Plr_ScopeSpeed_X_Steps = 120.0f;
float Plr_ScopeSpeed_Y = 0.011f;
float Plr_ScopeSpeed_Y_Mul = 3.0f;
float Plr_ScopeSpeed_Y_Steps = 120.0f;
float Plr_NoAimTurnSpeed_X = 0.04f;
float Plr_NoAimTurnSpeed_X_Mul = 2.0f;
float Plr_NoAimTurnSpeed_X_Steps = 120.0f;
float Plr_NoAimTurnSpeed_Y = 0.01f;
float Plr_NoAimTurnSpeed_Y_Mul = 2.0f;
float Plr_NoAimTurnSpeed_Y_Steps = 120.0f;

namespace {
constexpr float kHalfPi = 1.5707964f;
const uint32_t kPunchSkin[4] = {0x50000ab, 0x5000099, 0x500003d, 0x50000a1};  // PunchSkin.204

// pos + dir * k nos três primeiros componentes; w = o de pos (ver o cabeçalho).
_VECTOR madd3(const _VECTOR& pos, const _VECTOR& dir, float k) {
    return {pos.x + dir.x * k, pos.y + dir.y * k, pos.z + dir.z * k, pos.w};
}
_VECTOR sub4(const _VECTOR& a, const _VECTOR& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
_VECTOR add4(const _VECTOR& a, const _VECTOR& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
// Linhas 0-2 de m vezes v (vmulax/vmadday/vmaddz .xyz); w = o de v.
_VECTOR rotate3(const MATRIX& m, const _VECTOR& v) {
    return {m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z, m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z,
            m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z, v.w};
}
_VECTOR& matrix_row3(MATRIX& m) { return *reinterpret_cast<_VECTOR*>(&m.m[3][0]); }
float clamp_fall(float v) { return -45.0f <= v ? (v < 45.0f ? v : 45.0f) : -45.0f; }
int16_t read16(const uint8_t* p) {
    int16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
float weapon_float(int8_t weapon, uint32_t offset) {
    float v;
    std::memcpy(&v, weapon_data + int32_t(weapon) * 0x10c + int32_t(offset), 4);
    return v;
}
uint32_t weapon_u32(int8_t weapon, uint32_t offset) {
    uint32_t v;
    std::memcpy(&v, weapon_data + int32_t(weapon) * 0x10c + int32_t(offset), 4);
    return v;
}
uint32_t mp_flag(uint32_t offset) {
    uint32_t v;
    std::memcpy(&v, MPSettings + offset, 4);
    return v;
}
// Os subestados em que Player_Move anda e vira (o mesmo teste do começo dela).
bool moves_in(const obj_tag* obj) {
    if (4 < int16_t(obj->state) || int16_t(obj->state) < 2) {
        const int16_t s = int16_t(obj->unknown_f6);
        if (s < 8) return s < 6 && s != 1;
        return s != 0xf;
    }
    return false;
}
// Os subestados em que Player_Aiming olha para cima e para baixo.
bool aims_in(const obj_tag* obj) {
    const int16_t s = int16_t(obj->unknown_f6);
    return !(7 < s && (s < 0xd || s == 0x10));
}
// O padrão das funções de giro: soma step; abaixo do alvo para nele, acima dele não (ver AccelFunc0).
float approach(float speed, float target, float step) {
    if (speed < target) {
        const float v = speed + step;
        return target < v ? target : v;
    }
    if (target < speed) {
        const float v = speed + step;
        return v < target ? target : v;
    }
    return speed;
}
// Limita v a +-limite pelo sinal de v.
float clamp_signed(float v, float limit) {
    if (0.0f <= v) return limit < v ? limit : v;
    return v < -limit ? -limit : v;
}
}  // namespace

// [decomp] 0x00195bf0 Player_ResetStartPos
void Player_ResetStartPos() {
    player_start_positions_index = 0;
    player_start_position = 0;
}

// [decomp] 0x00195c00 Player_AddNewStartPos
void Player_AddNewStartPos(_VECTOR* pos, _VECTOR* rot, uint16_t kind, level_tag* level) {
    if (player_start_positions_index >= kPlayerStarts) return;
    PLAYERSTART_tag& s = player_start[player_start_positions_index];
    s.pos = *pos;
    s.rot = *rot;
    s.kind = kind;
    std::memcpy(&s.level, level, sizeof(level_tag));
    ++player_start_positions_index;
}

// [decomp] 0x00190fd0 Player_Start
// O jogador nasce no primeiro início de fase habilitado (sem canal ou com o canal de switch ligado);
// se nenhum estiver, no primeiro início de fase, que perde o canal.
void Player_Start() {
    for (uint32_t i = 0; i < player_start_positions_index; ++i) {
        PLAYERSTART_tag& s = player_start[i];
        const uint32_t channel = s.level.params[0];
        if (s.kind == 1 && (channel == 0 || (channel < 256 && switch_channels[channel] != 0))) {
            Player_Init(0, &s.pos, &s.rot, &s.level);
            return;
        }
    }
    for (uint32_t i = 0; i < player_start_positions_index; ++i) {
        PLAYERSTART_tag& s = player_start[i];
        if (s.kind == 1) {
            s.level.params[0] = 0;
            Player_Init(0, &s.pos, &s.rot, &s.level);
            return;
        }
    }
}

// [decomp] 0x001910f8 Player_Init
// Cria o objeto do jogador (tipo 3) com o bloke, liga a câmera do jogador a ele, escolhe a pele
// pela fase e põe o jogador de pé no ponto de partida no subestado pedido pelo posicionamento
// (params[3]: 1 nadando, 2 gravidade zero, 3 agachado).
obj_tag* Player_Init(int16_t index, _VECTOR* pos, _VECTOR* rot, level_tag* level) {
    const uint32_t channel = level->params[0];
    if (channel != 0 && (channel >= 256 || switch_channels[channel] == 0)) return nullptr;
    switch_channels[0x62] = 0;
    switch_channels[0x5e] = 0;
    if (glb_players[index] != nullptr) {
        switch_channels[0x5e] = 0;
        switch_channels[0x62] = 0;
        return nullptr;
    }
    obj_tag* obj = control_create_object(sizeof(BLData), pos, nullptr, nullptr);
    BLData* b = bloke_of(obj);
    glb_players[index] = obj;
    if (index == 0) ads_reset();  // só do port: a mira moderna começa no quadril (engine/player_ads.h)
    glb_blokes[index] = b;
    b->player = int8_t(index);
    b->player_b = int8_t(index);
    obj->hit_test = &b->hit_test;
    Sound_StopAllWithId(0x12);
    Sound_StopAllWithId(0x3d);
    Sound_StopAllWithId(0xf0);
    Camera_Enable(uint32_t(index), 1, 1, nullptr);
    Camera_SetFade(uint16_t(index), -45.0f, 0xff, 1);
    control_init_collision(obj);
    b->aim_x = 0.0f;
    b->aim_y = 0.0f;
    b->pitch_target = 0.0f;
    b->pitch = 0.0f;
    b->pitch_hold = 0;
    b->scope_speed_x = 0.0f;
    b->scope_speed_y = 0.0f;
    b->aim_speed_x = 0.0f;
    b->aim_speed_y = 0.0f;
    b->turn_speed = 0.0f;
    b->look_speed = 0.0f;
    b->unknown_955 = 0;
    b->zoom = 1.0f;
    b->zoom_max = 1.0f;
    b->zoom_target = 1.0f;
    Vec_Zero(&b->vel);
    Vec_Zero(&b->prev_vel);
    Vec_Zero(&b->prev_rot_vel);
    Vec_Zero(&b->rot_vel);
    Vec_Zero(&b->fall);
    b->orbit[3] = 0.0f;
    b->orbit[2] = 1.5f;
    b->orbit[0] = 1.5f;
    b->orbit[1] = 1.5f;
    b->ride = nullptr;
    b->unknown_87c = nullptr;
    b->ladder = nullptr;
    b->target = nullptr;
    obj->type = 3;
    obj->rot.x = 0.0f;
    obj->rot.z = 0.0f;
    obj->rot.y = rot->y;
    build_LinkToRoom(obj, 0, glb_world);
    int32_t punch = 0;
    uint32_t skin = 0x5000007;
    if (mp_flag(0x180) == 0) {
        switch (GameState.level_file) {
            case 0x7000001:
            case 0x7000002:
            case 0x7000003:
            case 0x7000004:
                punch = 1;
                skin = 0x5000009;
                break;
            case 0x7000007:
            case 0x7000008:
                punch = 2;
                skin = 0x500000d;
                break;
            case 0x700001b:
                punch = 3;
                skin = 0x500007c;
                break;
            default:
                break;
        }
        std::memcpy(weapon_data + 488, &kPunchSkin[punch], 4);
        weapon_data[0x3999] = 1;
    } else {
        pending_call("Player_Init (multijogador)");
        const uint32_t mp_punch = 0x50000b0;
        std::memcpy(weapon_data + 488, &mp_punch, 4);
        weapon_data[0x3999] = 2;
    }
    b->punch_skin = int8_t(punch);
    AnimObjectNew(obj, skin);
    AnimDebugCreate();
    Player_SetCamMode(b, 0);
    viewer_tag* v = glb_viewer[index];
    v->lod_scale = 1.0f;
    Camera_Set(v, pos, &obj->rot);
    float health = 100.0f;
    v->cel = nullptr;
    v->player = obj;
    b->armour = 0.0f;
    b->unknown_8b4 = 0.0f;
    b->unknown_8bc = 1.0f;
    if (mp_flag(0x180) != 0) {
        int32_t bonus;
        std::memcpy(&bonus, MPSettings + b->player * 0x30 + 0x2c, 4);
        health = float(bonus) + 100.0f;
    }
    Player_SetHealth(b, health);
    b->visibility = 1.0f;
    b->unknown_914 = 0.0f;
    b->fade_time = 0.0f;
    b->fade_left = 0.0f;
    b->underwater = 0.0f;
    b->water_flags = 0;
    b->pain_flash = 0;
    b->hit_dir = 0;
    b->health_shown = b->health;
    b->menu = -1;
    b->blur = 0xff;
    b->air = 100.0f;
    HUD_Init(b, obj);
    HUD_Update(b, obj);
    Player_InitWeapon(b, obj, level);
    Player_InitGrapple(b, obj);
    Camera_SetUpdator(uint16_t(index), obj, Player_PositionCamera);
    obj->flags |= 0x800;
    b->active = 1;
    b->frozen = 0;
    WldGravity.x = 0.0f;
    WldGravity.y = -9.8f;
    WldGravity.z = 0.0f;
    if (mp_flag(0x180) != 0) {
        Player_StandAtNewPosition(obj, &obj->pos, &obj->rot, 0);
        AnimScriptAdd(obj, 0x60001d4);
    } else {
        switch (level->params[3]) {
            case 2:
                Player_StandAtNewPosition(obj, &obj->pos, &obj->rot, kPlrZeroG);
                AnimScriptAdd(obj, 0x6000a00);
                Vec_Zero(&WldGravity);
                break;
            case 1:
                Player_StandAtNewPosition(obj, &obj->pos, &obj->rot, kPlrSwim);
                AnimScriptAdd(obj, 0x6000a00);
                break;
            case 3:
                Player_StandAtNewPosition(obj, &obj->pos, &obj->rot, kPlrCrouch);
                AnimScriptAdd(obj, 0x60009ff);
                b->crouch_drop = 0.45f;
                break;
            default:
                Player_StandAtNewPosition(obj, &obj->pos, &obj->rot, kPlrWalk);
                AnimScriptAdd(obj, 0x6000a00);
                break;
        }
    }
    b->body_glow = BodyGlow_Create(obj);
    Control_BuildWorldSph(obj);
    PlarStat_LogTimerUnpause(uint32_t(b->player));
    g_player_init_call.called = true;
    g_player_init_call.pos = *pos;
    g_player_init_call.rot = *rot;
    return obj;
}

// [decomp] 0x00190d88 Player_StandAtNewPosition
// Põe o jogador em pos com a rotação rot e o subestado pedido, zera as inércias e os contadores,
// baixa a posição até o chão (até 3 abaixo) e sobe 1,1: obj_tag::pos fica na altura do quadril.
void Player_StandAtNewPosition(obj_tag* obj, _VECTOR* pos, _VECTOR* rot, uint16_t substate) {
    BLData* b = bloke_of(obj);
    const _VECTOR p = *pos;
    obj->pos = p;
    obj->old_pos = p;
    b->old_pos = p;
    const _VECTOR r = *rot;
    obj->state = 1;
    obj->old_rot = r;
    obj->rot = r;
    Player_ChangeSubState(obj, debug_scanmode == 0 ? substate : kPlrScanMode);
    Player_ClearInertia(obj);
    Player_ClearAimInertia(obj);
    b->active = 1;
    b->unknown_920 = 1800.0f;
    b->touch_type = 0xff;
    b->frozen = 0;
    b->traverse_wait = 0;
    b->unknown_953 = 0;
    b->walk_class = 1;
    b->unknown_8cc = 0.0f;
    b->bond_moment = 0;
    b->unknown_95e = 0;
    b->turn_kick = 0.0f;
    b->unknown_961 = 0;
    b->step = 0.0f;
    b->fall_time = 0.0f;
    b->pain_flash = 0;
    b->hit_dir = 0;
    surfaced = 1;
    anim_of(obj)->flags &= 0xfeff;
    b->jump = 0;
    b->crouch_drop = 0.0f;
    b->anim_wait = 0;
    b->eye_height = 0.7f;
    Vec_Zero(&b->vel);
    Vec_Zero(&b->prev_vel);
    Vec_Zero(&b->prev_rot_vel);
    Vec_Zero(&b->rot_vel);
    Vec_Zero(&b->fall);
    b->idle_time = 0;
    _VECTOR floor{obj->pos.x, obj->pos.y + 0.1f, obj->pos.z, obj->pos.w};
    cel_tag* cel = build_FindCel(&floor, glb_world);
    build_PointOnFloor(cel, nullptr, &floor, 3.0f, nullptr);
    floor.y += 1.1f;
    obj->pos = floor;
    control_unlink_object(obj);
    Control_BuildWorldSph(obj);
    build_LinkToRoom(obj, 1, glb_world);
    viewer_tag* v = glb_viewer[b->player];
    v->cel = nullptr;
    Camera_Set(v, &v->pos, &v->rot);
    if (mp_flag(0x180) != 0) AnimObjectSetAiming(obj, &b->muzzle, 1000.0f, 0.2f, 0, 5);
    obj->mode |= 0x20;
}

// [decomp] 0x00190630 Player_ChangeSubState
// Troca o subestado (obj_tag::unknown_f6), recomeça as animações do novo e devolve o anterior. A
// varredura de depuração (5) e a gravidade zero (8, 9) usam a matriz fixa do objeto (mode bit 1);
// os outros zeram a inclinação.
int16_t Player_ChangeSubState(obj_tag* obj, int32_t substate) {
    BLData* b = bloke_of(obj);
    const int16_t old = int16_t(obj->unknown_f6);
    obj->unknown_f6 = uint16_t(substate);
    b->anim_set = nullptr;
    AnimSetDeleteAll(obj);
    if (substate == 5 || (5 < substate && substate < 10 && 7 < substate)) {
        obj->mode |= 2;
    } else {
        obj->mode &= 0xfffd;
        obj->rot.x = 0.0f;
    }
    obj->flags &= 0xffffffef;
    switch (substate) {
        case 0:
            PlayerAnimSetInitNormal(obj);
            bloke_of(obj)->unknown_954 = 0xff;
            if (old == 4) {
                PlayerAnimCrouch2Stand(obj);
                b->anim_wait = int8_t(int32_t(FRAME_RATE));
            }
            break;
        case 4:
            PlayerAnimSetInitCrouch(obj);
            bloke_of(obj)->unknown_954 = 0xff;
            if (old == 0) {
                PlayerAnimStand2Crouch(obj);
                b->anim_wait = int8_t(int32_t(FRAME_RATE));
            }
            break;
        case 8:
        case 9:
            PlayerAnimSetInitNormal(obj);
            bloke_of(obj)->unknown_954 = 0xff;
            break;
        case 0xd:
        case 0xe:
            bloke_of(obj)->anim_set = nullptr;
            AnimSetDeleteAll(obj);
            AnimScriptAdd(obj, WldGravity.y == 0.0f ? 0x60008e0 : 0x6000151);
            break;
        default:
            break;
    }
    if (0 < substate && (substate <= 3 || (substate <= 0x10 && 5 <= substate))) {
        Sound_StopAllWithId(0x12);
        Sound_StopAllWithId(0x3d);
        Sound_StopAllWithId(0xf0);
    }
    b->unknown_95a = 0;
    return old;
}

// [decomp] 0x00195928 Player_SetCamMode
// Modo de câmera (BLData::cam_mode): 0 primeira pessoa, 1 orbital... Reinicia o HUD.
void Player_SetCamMode(BLData* b, uint16_t mode) {
    b->cam_mode = uint8_t(mode);
    viewer_tag* v = glb_viewer[b->player];
    switch (mode) {
        case 1:
            b->orbit[3] = 0.0f;
            b->orbit[1] = 0.15f;
            b->orbit[2] = 1.5f;
            b->orbit[0] = 1.5f;
            HUD_Reset(b);
            v->mode = 0;
            break;
        case 0xc:
            HUD_Reset(b);
            v->mode = 0;
            HUD_Enable(b->hud, 9, 1, 0);
            break;
        case 0xd:
            HUD_Reset(b);
            v->mode = 0;
            HUD_Enable(b->hud, 10, 1, 0);
            break;
        case 0xf:
            HUD_Reset(b);
            v->mode = 0;
            HUD_Enable(b->hud, 0x12, 1, 0);
            break;
        default:
            HUD_Reset(b);
            v->mode = 0;
            break;
    }
}

// [decomp] 0x00195e08 Player_ClearInertia
void Player_ClearInertia(obj_tag* obj) {
    BLData* b = bloke_of(obj);
    Vec_Zero(&b->prev_vel);
    Vec_Zero(&b->prev_rot_vel);
    Vec_Zero(&b->vel);
    Vec_Zero(&b->rot_vel);
}

// [decomp] 0x001a9790 Player_ClearAimInertia
void Player_ClearAimInertia(obj_tag* obj) {
    BLData* b = bloke_of(obj);
    b->aim_turn_y = 0.0f;
    b->aim_turn_x = 0.0f;
}

// [decomp] 0x00196040 Player_SetFlashBang
// O clarão (granada de luz, o tremor de fim de fase): o borrão (BLData::blur) e a duração em ticks nos dois contadores
// do esmaecimento branco que Player_Update conduz.
void Player_SetFlashBang(BLData* b, float ticks, uint8_t level) {
    if (b == nullptr) return;
    b->blur = level;
    b->fade_left = ticks;
    b->fade_time = ticks;
}

// [decomp] 0x001960b8 Player_SetHealth
void Player_SetHealth(BLData* b, float health) {
    if (!PlrStat_OkToUpdate()) return;
    if (health < 0.0f) health = 0.0f;
    b->health = health;
    PlrStat_LogHealth(health, uint32_t(b->player));
}

namespace {

// CheatInfo +0: invencível.
uint32_t cheat_god() {
    uint32_t v;
    std::memcpy(&v, CheatInfo, 4);
    return v;
}

int16_t mp_game_i16(int32_t player, uint32_t offset) {
    int16_t v;
    std::memcpy(&v, MPGame + player * 0x30 + int32_t(offset), 2);
    return v;
}

void set_mp_game_i16(int32_t player, uint32_t offset, int16_t v) {
    std::memcpy(MPGame + player * 0x30 + int32_t(offset), &v, 2);
}

uint16_t weapon_class(int32_t weapon) {
    uint16_t v;
    std::memcpy(&v, weapon_data + weapon * 0x10c + 2, 2);
    return v;
}

// A cortina de sangue (pane 0xc do HUD) está ligada: o byte +0x1a4 do HUDINFO_tag.
bool death_panel_on(const BLData* b) { return static_cast<const HUDINFO_tag*>(b->hud)->panes[kHudBlood].enabled != 0; }

}  // namespace

// [decomp] 0x001857c0 MP_RegisterBulletHit
// Fora do multijogador todo acerto vale. No multijogador só valem tiros de jogadores (objetos dos
// tipos 2, 3 e 0x47, ou um projétil (5) de um deles): guarda em MPGame quem acertou e, com times,
// ignora o fogo amigo quando ele está desligado, avisando os dois uma vez a cada 3 s.
bool MP_RegisterBulletHit(int16_t player, obj_tag* shooter, float health) {
    (void)health;
    if (mp_flag(0x180) == 0) return true;
    if (shooter == nullptr) return false;
    const uint8_t type = shooter->type;
    int32_t who = -1;
    if (uint8_t(type - 2) <= 1 || type == 0x47) {
        who = Control_Plr2Ind(shooter);
    } else if (type == 5) {
        who = Control_Plr2Ind(static_cast<BU_tag*>(shooter->data)->from);
    }
    if (who < 0) return false;
    const int32_t me = int32_t(uint16_t(player));
    set_mp_game_i16(me, 0x20, int16_t(who));
    if (mp_flag(0x18c) == 0) return true;
    if (mp_flag(0x20 + uint32_t(me) * 0x30) != mp_flag(0x20 + uint32_t(who) * 0x30)) return true;
    if (who == me) return true;
    if (mp_flag(0x198) == 0) return false;
    if (mp_game_i16(me, 0x22) != 0) return true;
    char* text = Txt_GetStringFromHeap(0);
    const char* format = Txt_BindLabel(0x2000029, 0);
    if (text != nullptr && format != nullptr) {
        std::sprintf(text, format, reinterpret_cast<const char*>(MPSettings + who * 0x30));
        Text_AddMsg(int8_t(player), 0, 1, text, 0, 0xb4);
        format = Txt_BindLabel(0x200002a, 0);
        if (format != nullptr) {
            std::sprintf(text, format, reinterpret_cast<const char*>(MPSettings + me * 0x30));
            Text_AddMsg(int8_t(who), 0, 1, text, 0, 0xb4);
        }
    }
    set_mp_game_i16(me, 0x22, int16_t(int16_t(FRAME_RATE_INT << 1) + int16_t(FRAME_RATE_INT)));
    return true;
}

// [decomp] 0x001902c8 Player_HandlePain
// O jogador é ferido (type: 0 tiro, 1-4 sem efeito no colete, 5-7 também, 6 queda, 7 dano contínuo;
// location: o osso atingido, -1 nenhum). O dano é multiplicado pela dificuldade (ou, no multijogador,
// pelos ajustes da partida), o colete absorve o que puder e o resto sai da vida (a não ser com a
// trapaça de invencível); às vezes um gemido, a vibração do controle e o vermelho na tela.
void Player_HandlePain(obj_tag* obj, BLData* b, float damage, uint16_t type, int16_t location) {
    if (switch_channels[0x5f] != 0) return;
    if (b->health <= 0.0f) return;
    if (damage <= 0.0f) return;
    if (mp_flag(0x184) != 0 && GameFlow_GetState() != GF_WAIT_START) return;
    b->unknown_8bc = 1.0f;
    if (mp_flag(0x180) == 0) {
        const uint32_t difficulty = GameState.difficulty;
        if (difficulty == 2) {
            damage = damage * Plr_DMod_Normal;
        } else if (difficulty < 3) {
            damage = damage * (difficulty == 1 ? Plr_DMod_Easy : Plr_DMod_Normal);
        } else if (difficulty == 3) {
            damage = damage * Plr_DMod_Hard;
        } else if (difficulty == 4) {
            damage = damage + damage;
        } else {
            damage = damage * Plr_DMod_Normal;
        }
    } else {
        if (location != -1) damage = damage * Plr_DMod_Multi;
        if (mp_flag(0x1c8) != 0) {
            switch (int16_t(location - 5)) {
                case 0:
                    damage = damage * Plr_DMod_Head;
                    break;
                case 0xf:
                case 0x10:
                case 0x1b:
                case 0x1e:
                    damage = damage * Plr_DMod_UpperLimb;
                    break;
                case 0x2c:
                case 0x2d:
                case 0x2e:
                case 0x2f:
                case 0x30:
                case 0x31:
                case 0x32:
                case 0x33:
                    damage = damage * Plr_DMod_LowerLimb;
                    break;
                default:
                    break;
            }
        }
        if (mp_flag(0x1bc) != 0) damage = damage * 3.0f;
    }
    float to_armour = 1.0f;
    switch (type) {
        case 5:
        case 6:
        case 7:
            if (mp_flag(0x184) != 0) set_mp_game_i16(b->player, 0x20, -2);
            [[fallthrough]];
        case 1:
        case 2:
        case 3:
        case 4:
            to_armour = 0.0f;
            break;
        default:
            break;
    }
    const float armour = b->armour;
    float absorbed = damage * to_armour;
    absorbed = absorbed < armour ? absorbed : armour;
    b->armour = armour - absorbed;
    const float hurt = damage - absorbed;
    float health = b->health;
    if (cheat_god() == 0) {
        health = health - hurt;
        health = 0.0f < health ? health : 0.0f;
    }
    if (type != 7 || cheat_god() == 0) {
        if (Rand_Rand(4) == 0) Sound_Play3D(0x88, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        Input_RumbleStart(int16_t(b->player), 5, uint32_t(int32_t(hurt)));
    }
    b->hit_dir = type == 6 ? 2 : 0xc;
    float flash = damage * 21.333334f;
    flash = float(b->pain_flash) + (flash < 128.0f ? flash : 128.0f);
    b->pain_flash = uint8_t(int32_t(flash < 255.0f ? flash : 255.0f));
    if (health < 1.0f) health = 0.0f;
    Player_SetHealth(b, health);
}

// [decomp] 0x00190810 Player_CheckForDeath
// Uma vez por tick (Player_CollisionHandler): prende a vida entre 0 e 500 e, zerada num jogador ainda
// do tipo type, mata-o: carros e torretas o soltam, a missão falha (canal 0x5e e a música da morte;
// no multijogador, a arma cai como item), o objeto vira o tipo 0x12 (jogador morto) no estado 2, sem
// arma nem mira, e o subestado 0xd (0xe se estava no ar com a animação de pulo). 1 se já está morto
// ou morreu agora.
uint32_t Player_CheckForDeath(obj_tag* obj, uint32_t type) {
    BLData* b = bloke_of(obj);
    const int16_t state = int16_t(obj->state);
    if (state < 4 && 1 < state) return 1;
    if (cheat_god() != 0) return 0;
    float health = b->health;
    if (0.0f <= health) {
        if (500.0f < health) health = 500.0f;
    } else {
        health = 0.0f;
    }
    Player_SetHealth(b, health);
    if (0.0f < b->health || obj->type != type) return 0;
    const int32_t weapon = anim_of(obj)->weapon;
    Car_PlayerHasDied(obj);
    GunImp_PlayerHasDied(obj);
    GT_PlayerHasDied(obj);
    if (mp_flag(0x180) == 0) {
        switch_channels[0x5e] = 1;
        Music_Event(6, 1);
    } else if (weapon_u32(int8_t(weapon), 0x80) != 0 && b->gun != nullptr) {
        const uint16_t cls = weapon_class(weapon);
        Pickup_CreateSimple(&b->gun->matrix, cls, uint16_t(b->weapons[cls].rounds), 0);
        if (cls == 0x1a) {
            obj_tag* extra = Pickup_CreateSimple(&b->gun->matrix, 0x1b, uint16_t(b->weapons[weapon_class(0x1b)].rounds), 1);
            if (extra != nullptr) extra->flags |= 0x10;
        }
    }
    Player_ClearInertia(obj);
    HUD_Update(b, obj);
    obj->type = 0x12;
    obj->created = GameState.frame;
    Player_WeaponNone(obj);
    anim_of(obj)->flags &= 0xfffe;
    Player_SetCamMode(b, 0);
    Player_SetHealth(b, 0.0f);
    obj->state = 2;
    if (mp_flag(0x180) != 0) {
        char* text = Txt_GetStringFromHeap(0);
        const int32_t me = b->player;
        const int16_t by = mp_game_i16(me, 0x20);
        const int32_t killer = by != me ? by : -1;
        const char* format = Txt_BindLabel(killer == -2 || killer == -1 ? 0x2000037 : 0x2000038, 0);
        if (text != nullptr && format != nullptr) {
            const int32_t name = killer == -2 || killer == -1 ? me : by;
            std::sprintf(text, format, reinterpret_cast<const char*>(MPSettings + name * 0x30));
            Text_AddMsg(int8_t(me), 0, 1, text, 0, 0xb4);
        }
    }
    Sound_Play3D(0x89, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    Player_ChangeSubState(obj, (anim_of(obj)->flags & 0x100) == 0 ? 0xd : 0xe);
    MP_PlayerKilled(obj);
    Collide_FreeHitList(&obj->hits);
    return 1;
}

// [decomp] 0x00190bb0 Player_HandleDeath
// O tick do jogador morto (estados 2 e 3, depois da animação): na água continua boiando; sem gravidade
// o corpo gira devagar. No modo história mostra a mensagem de morte e, quando ela acaba ou o jogador
// aperta Start, liga o canal 0x62 (missão fracassada, que Mission_Update trata); no multijogador
// renasce 5 s depois (salvo no modo de vidas contadas sem vidas).
void Player_HandleDeath(obj_tag* obj) {
    BLData* b = bloke_of(obj);
    Player_InWater(b, obj);
    if (WldGravity.y == 0.0f) {
        obj->rot.x = obj->rot.x + 0.001f;
        obj->rot.y = obj->rot.y + 0.001f;
        obj->rot.z = obj->rot.z + 0.001f;
    }
    if (mp_flag(0x180) == 0) {
        if (!PlrStat_OkToUpdate()) return;
        if (!death_panel_on(b)) {
            HUD_DisableAll(b);
            HUD_Enable(b->hud, 0xc, 1, 0);
        } else if ((HUD_State(b->hud, 0xc) != 0 || Input_Action(-1, kActMenuStart, kActionDown) != 0) &&
                   Menu_PlayerHasQuitOrRestarted() == 0) {
            switch_channels[0x62] = 1;
        }
        return;
    }
    if (!death_panel_on(b)) HUD_Enable(b->hud, 0xc, 1, 0);
    if (uint32_t(FRAME_RATE_INT * 5) <= GameState.frame - obj->created) {
        if (mp_flag(0x1a4) == 0x10) {
            int32_t lives_used;
            std::memcpy(&lives_used, MPGame + Control_Plr2Ind(obj) * 0x30 + 8, 4);
            if (int32_t(mp_flag(0x19c)) <= lives_used) return;
        }
        if (int16_t(obj->state) != 3) MP_ReSpawn(obj, int16_t(b->player));
    }
}

PlayerMouseLook g_player_mouse_look;

bool Player_PortMouseLookGate(const obj_tag* obj, bool* yaw_ok, bool* pitch_ok, bool* aiming, float* zoom) {
    *yaw_ok = *pitch_ok = *aiming = false;
    *zoom = 1.0f;
    if (obj == nullptr || obj->data == nullptr || obj->type != 3) return false;
    const BLData* b = static_cast<const BLData*>(obj->data);
    if (b->active != 1 || GS_IsPaused(b->player) || int16_t(obj->state) != 1 || benchmark != 0) return false;
    if (b->cam_mode != 0 || obj->sphere_src == nullptr) return false;
    *yaw_ok = moves_in(obj) && b->move_locked == 0;
    *pitch_ok = aims_in(obj);
    *aiming = (reinterpret_cast<const ANIMOBJ_tag*>(obj->sphere_src)->flags & 1) != 0;
    *zoom = ads_effective_zoom(b);
    return *yaw_ok || *pitch_ok;
}

// [decomp] 0x00193600 Player_Update
// O tick do jogador (control_funcs[3].update): menu de pausa, HUD, visibilidade (sombra), o
// subestado (andar, agachar, nadar...), o ar e a volta da visão ao centro, olhar, mover pela
// velocidade local e girar, depois a animação, a cápsula de colisão (Player_Collision) e as armas.
void Player_Update(obj_tag* obj) {
    BLData* b = bloke_of(obj);
    if (Input_Action(b->player, kActPause, kActionPressed) != 0 && GameState.unknown_24 == 0 && ScriptCam == 0) {
        b->menu = int16_t(MenuManager_Create(0x80000002, 0x4000004b, b->player, uint8_t(b->player), 0x1e, 1, 3));
        GS_PauseGame(1);
    }
    HUD_Update(b, obj);
    if (b->active != 1) {
        if (b->active == 0) return;
        --b->active;
        return;
    }
    if (GS_IsPaused(b->player)) return;
    obj->unknown_103[3] = 0.5f <= b->visibility ? uint8_t(int32_t(b->visibility * 255.0f)) : 0x28;
    const float d = 2.0f - b->visibility;
    float vis = b->visibility + d * REC_FRAME_RATE * 0.05f;
    if (0.0f <= d ? 2.0f < vis : vis < 2.0f) vis = 2.0f;
    b->visibility = 0.0f <= vis ? (vis < 1.0f ? vis : 1.0f) : 0.0f;
    if (obj->cel != nullptr && obj->cel->misc[9] != 0) Music_Event(obj->cel->misc[9], 1);
    b->turn_kick *= 0.9f;
    const _VECTOR start = obj->pos;
    const int16_t state = int16_t(obj->state);
    if (state == 1) {
        PlarStat_LogUpdateElapsedTime(uint32_t(b->player));
        Vec_Zero(&b->vel);
        Vec_Zero(&b->rot_vel);
        if (b->traverse_wait != 0) --b->traverse_wait;
        if (b->unknown_953 != 0) --b->unknown_953;
        if (mp_flag(0x180) == 0 && Input_Action(3, kActScanMode, kActionPressed) != 0) {
            debug_scanmode ^= 1;
            if (debug_scanmode == 0) {
                _VECTOR dir;
                Mat_GetDir(&dir, &obj->matrix);
                Vec_Cartesian_2_Spherical(&obj->rot, dir.x, dir.y, dir.z);
                obj->rot.z = 0.0f;
                obj->rot.x = 0.0f;
                obj->old_rot = obj->rot;
                Player_ChangeSubState(obj, kPlrWalk);
                Camera_CalcViewAngles(uint16_t(b->player), 1.0471976f);
            } else {
                Player_ChangeSubState(obj, kPlrScanMode);
            }
        }
        if (benchmark == 0) {
            b->old_pos = obj->pos;
            --b->idle_time;
            if (b->frozen == 0) {
                switch (obj->unknown_f6) {
                    case 0:
                        Player_SSWalk(b, obj);
                        break;
                    case 1:
                        Player_Climb(b, obj);
                        break;
                    case 2:
                        Player_SSGrapple(b, obj);
                        break;
                    case 3:
                        Player_Move(b, obj, 1.0f);
                        if (obj->cel != nullptr) {
                            // Nadando: a velocidade vai para o mundo, não sobe acima de 0,3 da água
                            // e volta para o espaço local.
                            if ((anim_of(obj)->flags & 8) != 0 && obj->cel->water_y <= obj->pos.y - 0.3f) {
                                b->look_state = 2;
                                Player_ChangeSubState(obj, kPlrWalk);
                            }
                            _VECTOR w = rotate3(obj->matrix, b->vel);
                            const float y = obj->pos.y;
                            const float top = obj->cel->water_y - 0.3f;
                            if (y < top) {
                                if (top < y + w.y) w.y = top - y;
                            } else if (0.0f < w.y) {
                                w.y = 0.0f;
                            }
                            MATRIX inv;
                            Mat_Inverse(&obj->matrix, &inv);
                            b->vel = rotate3(inv, w);
                        }
                        break;
                    case 4:
                        Player_SSCrouch(b, obj);
                        break;
                    case 5:
                        Player_SSScanMode(b, obj);
                        break;
                    case 6:
                        Player_Wire(obj);
                        break;
                    case 7:
                        Player_Creep(b, obj);
                        break;
                    case 8:
                    case 9:
                        Player_ZeroG(b, obj);
                        break;
                    case 10: {
                        const int16_t t = b->ride_time;
                        bool leave = false;
                        if (t < 1) {
                            leave = b->ride == nullptr;
                        } else {
                            b->ride_time = int16_t(t - 1);
                            if (t == 1) {
                                b->ride = nullptr;
                                leave = true;
                            }
                        }
                        if (leave) {
                            Player_SetCamMode(b, 0);
                            glb_viewer[b->player]->cel = nullptr;
                            Player_ChangeSubState(obj, b->prev_substate);
                        }
                        break;
                    }
                    case 0xf:
                        Player_Zipline(obj);
                        break;
                    default:
                        break;
                }
            }
            Player_MonitorAir(b, obj);
            const int8_t look = b->look_state;
            if (1 < look && look < 4) {
                if (b->pitch_hold == 0) {
                    const float step = (b->pitch_target - b->pitch) * 0.075f;
                    if (obj->unknown_f6 != 0 || b->jump == 0) {
                        const float p = b->pitch + step;
                        b->pitch = p;
                        if (p < -1.0f) b->pitch = -1.0f;
                        else if (1.0f < p) b->pitch = 1.0f;
                        if (step <= 0.0002f && -0.0002f <= step && b->look_state == 2) b->look_state = 3;
                    }
                } else {
                    --b->pitch_hold;
                }
            }
            Player_Aiming(obj);
            // Só no PC: o mouse.
            float yaw, pitch;
            psiInput_TakeMouseLook(b->player, &yaw, &pitch, (anim_of(obj)->flags & 1) != 0, ads_effective_zoom(b));
            const float pitch_before = b->pitch;
            const bool yaw_used = yaw != 0.0f && moves_in(obj) && b->move_locked == 0;
            const bool pitch_used = pitch != 0.0f && aims_in(obj);
            if (yaw_used) b->rot_vel.y += yaw;
            if (pitch_used) {
                b->pitch += pitch / kHalfPi;
                b->look_state = 1;
            }
            Player_ViewClamping(obj);
            if (b->player == 0) {
                // O giro do mouse deste tick, para o desenho entre ticks (g_player_mouse_look).
                g_player_mouse_look.yaw = yaw_used ? yaw : 0.0f;
                g_player_mouse_look.pitch = pitch_used ? (b->pitch - pitch_before) * kHalfPi : 0.0f;
                ++g_player_mouse_look.count;
            }
            const _VECTOR step = rotate3(obj->matrix, b->vel);
            obj->pos = add4(obj->pos, step);
            b->prev_vel = b->vel;
            b->rot_vel.y += b->turn_kick;
            obj->rot = add4(obj->rot, b->rot_vel);
            b->prev_rot_vel = b->rot_vel;
        }
    } else if (state < 2) {
        if (state == 0) obj->state = 1;
    } else if (state < 4) {
        AnimObjectUpdate(obj);
        Player_HandleDeath(obj);
    }
    Player_InShadow(b, obj);
    if (b->fade_left != 0.0f) b->fade_left -= 1.0f;
    if (b->fade_time != 0.0f) {
        if (mp_flag(0x180) == 0) GlobalBlur = b->blur;
        if (b->fade_time * 0.5f < b->fade_left) {
            Camera_SetFade(uint16_t(b->player), -100000.0f, 0xffffff00u | b->blur, 0);
        } else {
            Camera_SetFade(uint16_t(b->player), -b->fade_left, 0xffffff00u | b->blur, 0);
            b->fade_time = 0.0f;
        }
    }
    if (b->idle_time < 1) b->idle_time = int16_t((Rand_Random() & 0x3ff) + 0xff);
    AnimObjectUpdate(obj);
    anim_of(obj)->speed = Vec_Dist3D(&obj->pos, &start);
    Player_Collision(obj);
    const int16_t after = int16_t(obj->state);
    bool weapons = false;
    if (after < 4) {
        if (after < 2) {
            weapons = after == 1;
        } else {
            Player_WeaponRecoil(obj);
        }
    } else if (after == 4) {
        weapons = true;
    }
    if (weapons && b->frozen == 0) {
        if (b->cam_mode != 0) {
            _VECTOR dir, head;
            Mat_GetDir(&dir, &obj->matrix);
            AnimGetBoneWorldTrans(obj, 0x80000005, 0, &head, nullptr);
            Env_Breath(&head, &dir, obj->cel, obj->created);
        }
        if (b->gun != nullptr && int16_t(b->gun->state) != 9) {
            const auto regen = [](int16_t& ammo, uint32_t max_offset) {
                if (ammo < read16(weapon_data + max_offset)) ammo = int16_t(int32_t(float(ammo) + FRAME_RATE_MUL));
            };
            const uint32_t frame = GameState.frame;
            if ((frame & 3) == 0) {
                regen(b->weapons[0x4a].rounds, 0x4e0a);
                regen(b->weapons[0x4e].rounds, 0x523a);
            }
            if ((frame & 1) == 0) {
                regen(b->weapons[0x4c].rounds, 0x5022);
                regen(b->weapons[0x4f].rounds, 0x5346);
            }
            if (int32_t(frame) % 6 == 0) regen(b->weapons[0x45].rounds, 0x48ce);
            if ((frame & 3) == 0) regen(b->weapons[0x33].rounds, 0x35f6);
        }
        Player_WeaponRecoil(obj);
        Player_Weapon(obj);
        Player_WeaponFiring(obj, 0);
        if (PlayerSetting[b->player].unknown_0b == 0) {
            b->unknown_8bc = 1.0f;
        } else {
            const float dec = FRAME_RATE_MUL * 0.0020833334f;
            float v = b->unknown_8bc - dec;
            b->unknown_8bc = v;
            v = 0.0f <= v ? v - dec : 0.0f;
            b->unknown_8bc = v;
        }
    }
    Camera_ApplySwing(glb_viewer[b->player], weapon_float(anim_of(obj)->weapon, 0x8c));
    View_SetDrawInOtherViewsOnly(obj, uint32_t(b->player));
    const uint16_t mode = obj->mode;
    obj->mode = uint16_t(mode | 0x20);
    if ((mode & 4) == 0) matrix_row3(obj->matrix) = obj->pos;
}

// [decomp] 0x00192310 Player_SSWalk
// Andando: na água vira nado; senão anda (Player_Move), anima, recentraliza a visão quando a
// velocidade muda de faixa (com a opção ligada), trata pulo e agachar e gira a matriz pelo giro.
void Player_SSWalk(BLData* b, obj_tag* obj) {
    if (Player_InWater(b, obj)) return;
    if (b->anim_wait != 0) {
        --b->anim_wait;
        bool init = true;
        if (!AnimScriptIsStopped(obj)) {
            if (b->anim_wait != 1) init = false;
            else b->anim_wait = 0;
        } else {
            b->anim_wait = 0;
        }
        if (init) PlayerAnimSetInitNormal(obj);
    }
    Player_Move(b, obj, 1.0f);
    const float speed = std::fabs(b->vel.z);
    if (b->anim_wait == 0) {
        AnimSetUpdate(obj, bloke_of(obj)->anim_set, speed, b->vel.z, FRAME_RATE_MUL * 0.1f, b->vel.x,
                      FRAME_RATE_MUL * 0.075f);
    }
    BLData* d = bloke_of(obj);
    int8_t cls = 0;
    d->pitch_target = 0.0f;
    if (std::fabs(speed + speed) <= 0.2f) cls = 1;
    else if (0.8f <= std::fabs(speed + speed)) cls = 2;
    if (cls != 0 && cls != d->walk_class) {
        d->walk_class = cls;
        if (PlayerSetting[d->player].auto_center != 0) d->look_state = 2;
    }
    Player_HandleJump(obj, b, &WldGravity);
    MATRIX m;
    RotMatrix(&b->rot_vel, &m);
    Concat(&obj->matrix, &m);
    Mat_CopyRot(&m, &obj->matrix);
}

// [decomp] 0x001924c0 Player_SSCrouch
// Agachado: como andar a 75% da velocidade; levanta com X (ou pulo, com agachar alternado) se
// houver 0,95 livre acima.
void Player_SSCrouch(BLData* b, obj_tag* obj) {
    if (Player_InWater(b, obj)) return;
    if (b->anim_wait != 0) {
        --b->anim_wait;
        bool init = true;
        if (!AnimScriptIsStopped(obj)) {
            if (b->anim_wait != 1) init = false;
            else b->anim_wait = 0;
        } else {
            b->anim_wait = 0;
        }
        if (init) PlayerAnimSetInitCrouch(obj);
    }
    Player_Move(b, obj, 0.75f);
    if (b->anim_wait == 0) {
        AnimSetUpdate(obj, bloke_of(obj)->anim_set, std::fabs(b->vel.z), b->vel.z, FRAME_RATE_MUL * 0.1f, b->vel.x,
                      FRAME_RATE_MUL * 0.075f);
    }
    const float fwd = b->vel.z;
    int8_t cls = 0;
    BLData* d = bloke_of(obj);
    d->pitch_target = 0.0f;
    if (std::fabs(fwd) <= 0.2f) cls = 1;
    else if (0.8f <= std::fabs(fwd)) cls = 2;
    if (cls != 0 && cls != d->walk_class) {
        d->walk_class = cls;
        if (PlayerSetting[d->player].auto_center != 0) d->look_state = 2;
    }
    bool stand;
    if (PlayerSetting[b->player].crouch_toggle == 0) {
        stand = Input_Action(b->player, kActCrouch, kActionDown) == 0;
    } else {
        const uint8_t crouch = uint8_t(Input_Action(b->player, kActCrouch, kActionPressed));
        const uint8_t jump = uint8_t(Input_Action(b->player, kActJump, kActionPressed));
        stand = (crouch | jump) != 0;
    }
    if ((anim_of(obj)->flags & 0x20) == 0 && stand && b->anim_wait == 0) {
        const _VECTOR& base = matrix_row3(obj->matrix);
        const _VECTOR above{base.x, base.y + 0.95f, base.z, base.w};
        if (Collide_LineOfSight(&obj->pos, &above, obj->cel, obj, nullptr, 9)) Player_ChangeSubState(obj, kPlrWalk);
    }
    MATRIX m;
    RotMatrix(&b->rot_vel, &m);
    Concat(&obj->matrix, &m);
    Mat_CopyRot(&m, &obj->matrix);
}

namespace {

// Uma constante pelo padrão de bits do ELF (algumas não são o float mais próximo do decimal: 0,225 é 0x3e666667).
float fbits(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

// A velocidade suavizada de Player_ZeroG e Player_SSScanMode: anda `rate` da diferença até o alvo sem passar dele.
float smooth_to(float prev, float target, float rate) {
    const float diff = target - prev;
    const float v = prev + diff * rate;
    const bool over = 0.0f <= diff ? target < v : v < target;
    return over ? target : v;
}

}  // namespace

// [decomp] 0x00192ab0 Player_ZeroG
// Gravidade zero (subestados 8 e 9): sem queda; o analógico direito (mirando, a mira dividida pelo zoom) gira em y e x
// e os dois analógicos e pular/agachar dão velocidade nos três eixos do próprio jogador, suavizada (4% por tick, até
// 0,05 por tick) e acumulada (o jogador flutua). Usar endireita o jogador por 1,5 s: a frente vai para o plano
// horizontal (Mat_Align2Up) e a orientação anda até lá pelo slerp. No subestado 9 o corpo anima como andando.
void Player_ZeroG(BLData* b, obj_tag* obj) {
    Vec_Zero(&b->fall);
    b->pitch_hold = 0;
    b->look_state = 2;
    b->pitch = 0.0f;
    const float k = fbits(0x3fa0d97cu);  // 1,2566371 (2pi/5)
    if ((anim_of(obj)->flags & 1) == 0) {
        b->rot_vel.y = -Input_Actionf(b->player, kActTurn, kActionDown) * k * REC_FRAME_RATE;
        b->rot_vel.x = -Input_Actionf(b->player, kActLookY, kActionDown) * k * REC_FRAME_RATE;
    } else {
        b->rot_vel.y = -Input_Actionf(b->player, kActAimX, kActionDown) * k * REC_FRAME_RATE * 0.75f / b->zoom;
        b->rot_vel.x = -Input_Actionf(b->player, kActAimY, kActionDown) * k * REC_FRAME_RATE * 0.75f / b->zoom;
    }
    b->rot_vel.z = 0.0f;
    const float rate = fbits(0x3d23d70bu);  // 0,04 (0,040000003)
    const auto axis = [&](float& prev, float target) {
        prev = smooth_to(prev, target, FRAME_RATE_MUL * rate);
        const float limit = FRAME_RATE_MUL * 0.05f;
        if (prev < -limit) {
            prev = -limit;
        } else if (limit < prev) {
            prev = limit;
        }
        return prev;
    };
    b->vel.x += axis(b->prev_vel.x, Input_Actionf(b->player, kActStrafe, kActionDown) * 0.1f);
    b->vel.z += axis(b->prev_vel.z, Input_Actionf(b->player, kActForward, kActionDown) * 0.1f);
    const int32_t up = int32_t(Input_Action(b->player, kActJump, kActionDown)) -
                       int32_t(Input_Action(b->player, kActCrouch, kActionDown));
    b->vel.y += axis(b->prev_vel.y, float(up) * 0.1f);
    MATRIX m;
    RotMatrix(&b->rot_vel, &m);
    Concat(&obj->matrix, &m);
    Mat_CopyRot(&m, &obj->matrix);
    if (Input_Action(b->player, kActUse, kActionPressed) != 0) b->zerog_turn = int16_t(int32_t(FRAME_RATE * 1.5f));
    if (b->zerog_turn != 0) {
        b->zerog_turn = int16_t(uint16_t(b->zerog_turn) - 1);
        _VECTOR dir;
        _VECTOR up_dir;
        Mat_GetDir(&dir, &obj->matrix);
        Mat_GetUp(&up_dir, &obj->matrix);
        // VU0: vmul.xyz e a soma dos três; a frente quase vertical vira a de cima (ou o contrário).
        const float dot = (dir.x * CONSTANT_UP1.x + dir.y * CONSTANT_UP1.y) + dir.z * CONSTANT_UP1.z;
        if (fbits(0x3f7eb852u) < dot) {
            Vec_Negate(&up_dir, &dir);
        } else if (dot < -fbits(0x3f7eb852u)) {
            dir = up_dir;
        } else {
            // dir - dot * cima (vmulx.xyz num temporário da pilha sem valor: o w indefinido vale 0 aqui).
            dir.x += CONSTANT_UP1.x * -dot;
            dir.y += CONSTANT_UP1.y * -dot;
            dir.z += CONSTANT_UP1.z * -dot;
        }
        Mat_Align2Up(&b->zerog_upright, &CONSTANT_UP1, &dir);
        Quat_MatrixToQuaternion(&b->zerog_quat, &b->zerog_upright);
        quaternion_tag from;
        quaternion_tag to;
        Quat_MatrixToQuaternion(&from, &obj->matrix);
        Quat_Slerp_Acc(REC_FRAME_RATE * 3.0f, &from, &b->zerog_quat, &to);
        Quat_QuaternionToMatrix(&obj->matrix, &to);
        Mat_Normalize(&obj->matrix);
    }
    if (int16_t(obj->unknown_f6) == 9) {
        AnimSetUpdate(obj, bloke_of(obj)->anim_set, std::fabs(b->vel.z), b->vel.z, FRAME_RATE_MUL * 0.1f, b->vel.x,
                      FRAME_RATE_MUL * 0.075f);
    }
}

// [decomp] 0x00193098 Player_SSScanMode
// O voo de depuração (subestado 5: debug_scanmode, a ação 39 do jogador 3 ou a trapaça C_CHCHFLY): sem colisão nem
// queda, o analógico direito gira (pi rad/s), os dois analógicos voam no plano do jogador (suavizado, de lado até
// 0,225 e para a frente até 0,5 por tick) e pular/agachar sobem e descem 3 m/s; mirando, o direcional muda o campo
// de visão da câmera (0,2 a 3,14 rad).
void Player_SSScanMode(BLData* b, obj_tag* obj) {
    if ((anim_of(obj)->flags & 1) == 0) {
        b->vel.y = 0.0f;
        b->look_state = 2;
        constexpr float kPi = 3.14159274f;
        b->rot_vel.y = -Input_Actionf(b->player, kActTurn, kActionDown) * kPi * REC_FRAME_RATE;
        b->rot_vel.x = -(Input_Actionf(b->player, kActLookY, kActionDown) * kPi * REC_FRAME_RATE);
        const float k225 = fbits(0x3e666667u);  // 0,22500001
        b->prev_vel.x = smooth_to(b->prev_vel.x, Input_Actionf(b->player, kActStrafe, kActionDown) * k225 * FRAME_RATE_MUL,
                                  FRAME_RATE_MUL * 0.2f);
        const float x_lo = -(FRAME_RATE_MUL * 0.075f) * 3.0f;
        if (b->prev_vel.x < x_lo) {
            b->prev_vel.x = x_lo;
        } else if (FRAME_RATE_MUL * k225 < b->prev_vel.x) {
            b->prev_vel.x = FRAME_RATE_MUL * k225;
        }
        b->vel.x += b->prev_vel.x;
        b->prev_vel.z = smooth_to(b->prev_vel.z, Input_Actionf(b->player, kActForward, kActionDown) * 0.5f * FRAME_RATE_MUL,
                                  FRAME_RATE_MUL * 0.2f);
        const float z_lo = -(FRAME_RATE_MUL * 0.1f) * 5.0f;
        if (b->prev_vel.z < z_lo) {
            b->prev_vel.z = z_lo;
        } else if (FRAME_RATE_MUL * 0.5f < b->prev_vel.z) {
            b->prev_vel.z = FRAME_RATE_MUL * 0.5f;
        }
        b->vel.z += b->prev_vel.z;
        glb_viewer[b->player]->cel = nullptr;
        b->rot_vel.z = 0.0f;
        MATRIX yaw;
        MATRIX pitch;
        RotMatrixY(b->rot_vel.y, &yaw);
        RotMatrixX(b->rot_vel.x, &pitch);
        Concat(&yaw, &obj->matrix);
        Concat(&obj->matrix, &pitch);
        Mat_CopyRot(&pitch, &obj->matrix);
        const float climb = Input_Actionf(b->player, kActJump, kActionDown) - Input_Actionf(b->player, kActCrouch, kActionDown);
        const float y = obj->pos.y + climb * REC_FRAME_RATE * 3.0f;
        obj->matrix.m[3][1] = y;
        obj->pos.y = y;
    } else {
        const float zoom = Input_Actionf(b->player, kActDpadY, kActionPressed | kActionRepeat) * REC_FRAME_RATE;
        if (zoom != 0.0f) {
            viewer_tag* v = glb_viewer[b->player];
            float fov = v->fov + zoom;
            fov = 0.2f <= fov ? std::fmin(fov, 3.14f) : 0.2f;
            v->fov = fov;
            Camera_CalcViewAngles(uint16_t(int16_t(b->player)), v->fov);
        }
    }
    Vec_Zero(&b->fall);
    anim_of(obj)->speed = 0.0f;
}

// [decomp] 0x00191dd0 Player_Move
// Lê andar, de lado e virar. Andar para a frente cresce com o quadrado do analógico (0,105 por tick
// no máximo), para trás é mais lento; tudo suavizado (20% por tick) e limitado a 0,1 por tick. O
// giro passa por AccelFunc0 (acelera depois de segurar o analógico no fim). Nadando, andar segue
// a inclinação da visão.
void Player_Move(BLData* b, obj_tag* obj, float scale) {
    if (!moves_in(obj)) return;
    float fwd = Input_Actionf(b->player, kActForward, kActionDown);
    float side = Input_Actionf(b->player, kActStrafe, kActionDown);
    float turn = -Input_Actionf(b->player, kActTurn, kActionDown);
    if (uint8_t(anim_of(obj)->weapon - 0x50) < 2 && b->gun != nullptr && int16_t(b->gun->state) == 9) {
        turn = 0.0f;
        side = 0.0f;
        fwd = 0.0f;
    }
    if (b->move_locked != 0) {
        turn = 0.0f;
        side = 0.0f;
        fwd = 0.0f;
    }
    if ((anim_of(obj)->flags & 1) != 0) {
        turn = 0.0f;
        // Só do port: na mira moderna (engine/player_ads.h) o jogador anda para a frente e para trás mirando, com a frente
        // a 85% (a velocidade máxima dela, 0,1 por tick, fica perto da de lado, 0,075); no PS2 o modo de mira só deixa
        // andar de lado.
        fwd = ads_move_while_aiming(obj) ? fwd * 0.85f : 0.0f;
    }
    if (b->prev_substate == 7 && b->traverse_wait != 0) side = 0.0f;
    if (0.0f < fwd) {
        fwd = fwd * fwd * 0.1f * FRAME_RATE_MUL + FRAME_RATE_MUL * 0.005f;
    } else if (fwd < 0.0f) {
        fwd = fwd * fwd * 0.75f * -(FRAME_RATE_MUL * 0.1f) - FRAME_RATE_MUL * 0.005f;
    }
    const auto smooth = [](float target, float current) {
        const float delta = target - current;
        float v = current + delta * FRAME_RATE_MUL * 0.2f;
        if (0.0f <= delta ? target < v : v < target) v = target;
        if (target <= 0.0002f && -0.0002f <= target && std::fabs(v) < 0.01f) v = 0.0f;
        const float lim = FRAME_RATE_MUL * 0.1f;
        return v < -lim ? -lim : (lim < v ? lim : v);
    };
    float vx = smooth(side * 0.075f * FRAME_RATE_MUL, b->prev_vel.x);
    const float vz = smooth(fwd, b->prev_vel.z);
    if (GameState.level_file == 0x7000007 && switch_channels[0x48] == 0) scale *= 0.9f;
    vx *= scale;
    const float accel = AccelFunc0(turn, &b->turn_speed, Plr_NoAimTurnSpeed_X, Plr_NoAimTurnSpeed_X_Mul,
                                   Plr_NoAimTurnSpeed_X_Steps, 0.05f, 0.95f);
    b->turn_speed = accel;
    const float yaw = turn * accel * FRAME_RATE_MUL;
    if (obj->unknown_f6 == kPlrSwim) {
        MATRIX m;
        RotMatrixX(obj->rot.x - b->pitch * kHalfPi, &m);
        _VECTOR dir;
        Mat_GetDir(&dir, &m);
        b->vel = madd3(b->vel, dir, vz * scale);
        b->rot_vel.y = yaw;
        b->vel.x = vx;
    } else {
        b->rot_vel.y = yaw;
        b->vel.x = vx;
        b->vel.z = vz * scale;
    }
}

// [decomp] 0x001a9218 AccelFunc0
// Velocidade de giro: com o analógico além de "full" ela vai de base a base * mul em "steps"
// ticks; dentro da zona morta é base. Limitada a +-(base + base * mul). Peculiaridade preservada:
// acima do alvo o passo continua positivo (o original soma nos dois ramos), então com o analógico
// entre a zona morta e "full" depois de acelerar a velocidade sobe até o limite em vez de voltar.
float AccelFunc0(float input, float* speed, float base, float mul, float steps, float dead, float full) {
    const float top = base * mul;
    const float step = (top - base) / steps;
    const float target = std::fabs(input) < full ? base : top;
    float v = approach(*speed, target, step);
    if (std::fabs(input) <= dead) v = base;
    return clamp_signed(v, base + base * mul);
}

// [decomp] 0x001a1d28 Player_Aiming
// Olhar e mirar. Sem mirar (anim flag 1), o analógico direito vertical inclina a visão (pitch).
// Mirando, ele move a mira na tela; passando de +-0,296 (x) ou +-0,4 (y) a visão gira junto. Com a
// luneta (weapon_data bit 0x40), gira a visão devagar, dividido pelo zoom.
void Player_Aiming(obj_tag* obj) {
    BLData* b = bloke_of(obj);
    if (!aims_in(obj)) return;
    ANIMOBJ_tag* anim = anim_of(obj);
    if ((anim->flags & 1) == 0) {
        const float look = Input_Actionf(b->player, kActLookY, kActionDown);
        const float target = std::fabs(look) < 0.95f ? Plr_NoAimTurnSpeed_Y : Plr_NoAimTurnSpeed_Y * Plr_NoAimTurnSpeed_Y_Mul;
        float s = approach(b->look_speed, target,
                           (Plr_NoAimTurnSpeed_Y * Plr_NoAimTurnSpeed_Y_Mul - Plr_NoAimTurnSpeed_Y) /
                               Plr_NoAimTurnSpeed_Y_Steps);
        if (std::fabs(look) <= 0.05f) s = Plr_NoAimTurnSpeed_Y;
        s = clamp_signed(s, Plr_NoAimTurnSpeed_Y + Plr_NoAimTurnSpeed_Y * Plr_NoAimTurnSpeed_Y_Mul);
        b->look_speed = s;
        const float v = look * (s * FRAME_RATE_MUL);
        b->prev_rot_vel.x = v;
        if (v <= 0.0002f && -0.0002f <= v) return;
        b->look_state = 1;
        b->pitch = b->pitch + v * kHalfPi;
        return;
    }
    // Só do port: com a mira moderna numa arma sem luneta (engine/player_ads.h) a mira fica no centro, como na luneta.
    if ((weapon_u32(anim->weapon, 0x68) & 0x40) == 0 && !ads_fixed_crosshair(obj)) {
        const float ax = Input_Actionf(b->player, kActAimX, kActionDown);
        const float ay = Input_Actionf(b->player, kActAimY, kActionDown);
        bool turn_x = false;
        float tx = 0.0f, ty = 0.0f;
        const float x = ax * Plr_AimSpeed_X * FRAME_RATE_MUL * 0.125f + b->aim_x;
        b->aim_y = ay * Plr_AimSpeed_Y * FRAME_RATE_MUL * 0.125f + b->aim_y;
        b->aim_x = x;
        if (0.296f < std::fabs(x)) {
            tx = ax * 0.125f;
            turn_x = true;
            if (x < -0.296f) b->aim_x = -0.296f;
            else if (0.296f < x) b->aim_x = 0.296f;
        }
        bool turn_y = false;
        if (0.4f < std::fabs(b->aim_y)) {
            turn_y = true;
            ty = ay * 0.125f;
            if (b->aim_y < -0.4f) b->aim_y = -0.4f;
            else if (0.4f < b->aim_y) b->aim_y = 0.4f;
        }
        float sx = approach(b->aim_speed_x, turn_x ? Plr_AimTurnSpeed_X * Plr_ScopeSpeed_X_Mul : Plr_AimTurnSpeed_X,
                            (Plr_AimTurnSpeed_X * Plr_ScopeSpeed_X_Mul - Plr_AimTurnSpeed_X) / Plr_ScopeSpeed_X_Steps);
        if (std::fabs(ax) <= 0.05f) sx = Plr_AimTurnSpeed_X;
        sx = clamp_signed(sx, Plr_AimTurnSpeed_X + Plr_AimTurnSpeed_X * Plr_ScopeSpeed_X_Mul);
        b->aim_speed_x = sx;
        float sy = approach(b->aim_speed_y, turn_y ? Plr_AimTurnSpeed_Y * Plr_ScopeSpeed_Y_Mul : Plr_AimTurnSpeed_Y,
                            (Plr_AimTurnSpeed_Y * Plr_ScopeSpeed_Y_Mul - Plr_AimTurnSpeed_Y) / Plr_ScopeSpeed_Y_Steps);
        if (std::fabs(ay) <= 0.05f) sy = Plr_AimTurnSpeed_Y;
        sy = clamp_signed(sy, Plr_AimTurnSpeed_Y + Plr_AimTurnSpeed_Y * Plr_ScopeSpeed_Y_Mul);
        b->aim_speed_y = sy;
        ty = ty * (sy * FRAME_RATE_MUL);
        tx = tx * b->aim_speed_x * FRAME_RATE_MUL;
        b->aim_turn_y = ty;
        b->aim_turn_x = tx;
        ty = ty / b->zoom;
        tx = tx / b->zoom;
        if (surfaced == 0) {
            tx *= 0.4f;
            ty *= 0.7f;
        }
        b->pitch += ty;
        b->rot_vel.y -= tx * kHalfPi;
        return;
    }
    const float ax = Input_Actionf(b->player, kActAimX, kActionDown);
    const float ay = Input_Actionf(b->player, kActAimY, kActionDown);
    float sx = approach(b->scope_speed_x, std::fabs(ax) < 0.95f ? Plr_ScopeSpeed_X : Plr_ScopeSpeed_X * Plr_ScopeSpeed_X_Mul,
                        (Plr_ScopeSpeed_X * Plr_ScopeSpeed_X_Mul - Plr_ScopeSpeed_X) / Plr_ScopeSpeed_X_Steps);
    if (std::fabs(ax) <= 0.05f) sx = Plr_ScopeSpeed_X;
    sx = clamp_signed(sx, Plr_ScopeSpeed_X + Plr_ScopeSpeed_X * Plr_ScopeSpeed_X_Mul);
    b->scope_speed_x = sx;
    float sy = approach(b->scope_speed_y, std::fabs(ay) < 0.95f ? Plr_ScopeSpeed_Y : Plr_ScopeSpeed_Y * Plr_ScopeSpeed_Y_Mul,
                        (Plr_ScopeSpeed_Y * Plr_ScopeSpeed_Y_Mul - Plr_ScopeSpeed_Y) / Plr_ScopeSpeed_Y_Steps);
    if (std::fabs(ay) <= 0.05f) sy = Plr_ScopeSpeed_Y;
    sy = clamp_signed(sy, Plr_ScopeSpeed_Y + Plr_ScopeSpeed_Y * Plr_ScopeSpeed_Y_Mul);
    const float vy = ay * sy * FRAME_RATE_MUL;
    const float vx = ax * b->scope_speed_x * FRAME_RATE_MUL;
    b->aim_y = 0.0f;
    b->scope_speed_y = sy;
    b->prev_rot_vel.x = vy;
    b->prev_rot_vel.y = vx;
    b->aim_x = 0.0f;
    b->pitch = b->pitch + (vy * 0.63661975f) / b->zoom;
    b->rot_vel.y = b->rot_vel.y - vx / b->zoom;
}

// [decomp] 0x001a92e8 Player_ViewClamping
void Player_ViewClamping(obj_tag* obj) {
    BLData* b = bloke_of(obj);
    if (-1.0f <= b->pitch) {
        if (1.0f < b->pitch) b->pitch = 1.0f;
    } else {
        b->pitch = -1.0f;
    }
}

// [decomp] 0x00195e50 Player_InWater
// Com o quadril (obj_tag::pos) a menos de 0,3 acima da água da sala, o jogador passa a nadar.
bool Player_InWater(BLData* b, obj_tag* obj) {
    if (obj->cel == nullptr) return false;
    ANIMOBJ_tag* anim = anim_of(obj);
    if (obj->pos.y - 0.3f < obj->cel->water_y) {
        anim->flags |= 0x100;
        if (mp_flag(0x180) != 0) return obj->unknown_f6 == kPlrSwim;
        if (obj->state != 2) {
            Player_ChangeSubState(obj, kPlrSwim);
            b->jump = 0;
            b->look_state = 2;
            b->vel.y = 0.0f;
            b->traverse_wait = int8_t(int32_t(FRAME_RATE_DIV * 5.0f));
            obj->rot.x = 0.0f;
            BLData* d = bloke_of(obj);
            obj->rot.z = 0.0f;
            Vec_Zero(&d->prev_vel);
            Vec_Zero(&d->prev_rot_vel);
            Vec_Zero(&d->vel);
            Vec_Zero(&d->rot_vel);
            anim->walk_pos = 0.0f;
        }
    } else {
        anim->flags &= 0xfeff;
    }
    return obj->unknown_f6 == kPlrSwim;
}

// [decomp] 0x00191a70 Player_MonitorAir
// Cabeça debaixo d'água: o ar (BLData::air) cai 5 por segundo; fora, volta 20 por segundo. Sem ar,
// 2 de dano a cada meio segundo. Toca os sons de mergulhar e de sair da água.
void Player_MonitorAir(BLData* b, obj_tag* obj) {
    const bool show = obj->unknown_f6 == kPlrSwim || obj->unknown_f6 == kPlrWire;
    HUD_Enable(b->hud, 5, show ? 1 : 0, 0);
    _VECTOR head;
    MATRIX m;
    AnimGetBoneWorldTrans(obj, 0x80000005, 0, &head, &m);
    if (obj->cel == nullptr) return;
    const int16_t s = int16_t(obj->unknown_f6);
    const bool can_drown = !(s < 10 && 7 < s);
    if (head.y < obj->cel->water_y) {
        const float d = 1.0f - b->underwater;
        float v = b->underwater + d * REC_FRAME_RATE;
        if (0.0f <= d ? 1.0f < v : v < 1.0f) v = 1.0f;
        b->underwater = v;
        b->water_flags |= 0x10;
    } else {
        const float d = -b->underwater;
        float v = b->underwater + d * REC_FRAME_RATE * 0.5f;
        if (0.0f <= d ? 0.0f < v : v < 0.0f) v = 0.0f;
        b->underwater = v;
        if ((b->water_flags & 0x10) != 0) b->water_flags |= 1;
        b->water_flags &= 0xffffffef;
    }
    if (obj->unknown_f6 == kPlrWire) b->water_flags |= 0x10;
    if (obj->cel == nullptr || obj->cel->water_y <= head.y || !can_drown) {
        b->air += REC_FRAME_RATE * 20.0f;
        if (surfaced == 0) {
            if (b->water_sound != 0) {
                Sound_Stop(b->water_sound);
                b->water_sound = 0;
            }
            Sound_Play(0x60c, 100.0f, 0, 0);
            surfaced = 1;
        }
    } else {
        b->air -= REC_FRAME_RATE * 5.0f;
        if (surfaced != 0) {
            if (b->water_sound != 0) Sound_Stop(b->water_sound);
            b->water_sound = Sound_Play3D(0x27, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
            surfaced = 0;
        }
    }
    const float air = b->air;
    b->air = 0.0f <= air ? (air < 100.0f ? air : 100.0f) : 0.0f;
    if (b->air < 1.0f) {
        const uint32_t every = uint32_t(FRAME_RATE * 0.5f);
        if (every != 0 && int32_t(GameState.frame) % int32_t(every) == 0) {
            Sound_Play(0x60b, 100.0f, 0, 0);
            Player_HandlePain(obj, b, 2.0f, 7, -1);
        }
    }
}

// [decomp] 0x001bba38 Camera_Shear
// A câmera debaixo d'água (Camera_Update chama com a câmera abaixo da água da sala dela): a imagem ondula. Uma fase
// (viewer_tag +0x1e0) anda REC_FRAME_RATE por chamada (uma volta em 2 pi s a 60 Hz) e dá xs = 0,025 cos e ys =
// 0,025 sen; a matriz da câmera passa a ser (cisalhamento xs em [1][0], ys em [0][1], escalado por 1 + xs, 1 + ys,
// 1 + xs) x a rotação dela, com a posição como estava; GlobalBlur = 0x91 (o borrão da tela, que decai 16 por quadro).
// Fica em player.cpp, junto do resto da água; o original é do módulo das câmeras.
void Camera_Shear(viewer_tag* v) {
    if (v == nullptr) return;
    alignas(16) const _VECTOR pos = *reinterpret_cast<const _VECTOR*>(&v->camera.m[3][0]);
    float phase;
    std::memcpy(&phase, &v->unknown_1e0[0], 4);
    float xs, ys;
    PS2Sinf2(phase + 1.5707964f, phase, &xs, &ys);
    ys = ys * 0.025f;
    xs = xs * 0.025f;
    const float next = phase + REC_FRAME_RATE;
    float wrapped = next;
    if (3.1415927f <= std::fabs(next)) {
        wrapped = std::fmod(next, 6.2831855f);
        if (3.1415927f <= std::fabs(wrapped)) wrapped = 0.0f <= next ? wrapped - 6.2831855f : wrapped + 6.2831855f;
    }
    std::memcpy(&v->unknown_1e0[0], &wrapped, 4);
    MATRIX sm;
    Mat_IdentityT(&sm);
    sm.m[1][0] = xs;
    sm.m[0][1] = ys;
    Mat_Scale3f(&sm, &sm, xs + 1.0f, ys + 1.0f, xs + 1.0f);
    Concat(&v->camera, &sm);
    Mat_CopyRot(&sm, &v->camera);
    *reinterpret_cast<_VECTOR*>(&v->camera.m[3][0]) = pos;
    GlobalBlur = 0x91;
}

// [decomp] 0x001916b0 Player_HandleJump
// jump = 0 no chão: X agacha; com o pé no chão nos últimos ticks, nada acima e 0,95 livre sobre a
// cabeça, o triângulo pula (velocidade de 0,4 da gravidade para cima, uns 3,9 m/s). Na fase
// 07000007, antes do canal 0x48, só toca um som. jump = 1 subindo: bater no teto passa para 2;
// jump 1 ou 2: ao tocar o chão (descendo) volta a 0 com o som de pouso.
void Player_HandleJump(obj_tag* obj, BLData* b, _VECTOR* gravity) {
    ANIMOBJ_tag* anim = anim_of(obj);
    const int8_t state = b->jump;
    if (state == 1) {
        if ((anim->flags & 0x20) != 0) {
            b->jump = 2;
            Vec_Zero(&b->fall);
            return;
        }
        if (b->jump_wait != 0) {
            --b->jump_wait;
            return;
        }
        if ((anim->flags & 8) == 0) return;
        if (0.0f < b->fall.y) return;
    } else if (state < 2) {
        if (state != 0) return;
        if (int8_t(Input_Action(b->player, kActCrouch, kActionPressed)) != 0 && b->anim_wait == 0) {
            Player_ChangeSubState(obj, kPlrCrouch);
            return;
        }
        if (b->ground_bits == 0) return;
        if ((anim->flags & 0x20) != 0) return;
        if (GameState.level_file == 0x7000007 && switch_channels[0x48] == 0) {
            if (Input_Action(b->player, kActJump, kActionPressed) == 0) return;
            Sound_Play(0x1db, 100.0f, 0, 0);
            return;
        }
        if (Input_Action(b->player, kActJump, kActionPressed) == 0) return;
        if (b->touch_type == 3) {
            obj_tag* best = nullptr;
            float best_d = 1000.0f;
            for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
                if (o->type != 0x2c) continue;
                const float d = Vec_Dist3D(&o->pos, &obj->pos);
                if (d < best_d) {
                    best = o;
                    best_d = d;
                }
            }
            if (best != nullptr) {
                Player_CollWire(obj, b, best);
                return;
            }
        }
        _VECTOR above = matrix_row3(obj->matrix);
        above.y += 0.95f;
        if (!Collide_LineOfSight(&obj->pos, &above, obj->cel, obj, nullptr, 9)) return;
        Vec_Zero(&b->fall);
        b->fall = madd3(b->fall, *gravity, -0.4f);
        b->jump_wait = 4;
        b->jump = 1;
        anim->flags &= 0xfff7;
        Sound_Play3D(0xa3, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        return;
    } else if (state != 2) {
        return;
    }
    if ((anim->flags & 8) != 0) {
        Vec_Zero(&b->fall);
        b->jump = 0;
        if (uint8_t(anim->floor.surface - 0xd) < 2) {
            Sound_Play3D(0x5fb, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        } else {
            Sound_Play3D(0xa3, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        }
        b->step_reset = 8;
        b->step = 999.0f;
    }
}

// [decomp] 0x00194df0 Player_Collision
// Aplica a gravidade (fora do chão, ou em chão íngreme) e a velocidade de queda à posição e monta a
// cápsula do corpo em BLData::hit_test para o Collide_Update: um segmento do quadril para baixo até
// raio acima dos pés (root_height abaixo da posição), raio 0,55, ignorando a arma (ou a escada, o
// veículo).
void Player_Collision(obj_tag* obj) {
    BLData* b = bloke_of(obj);
    ANIMOBJ_tag* anim = anim_of(obj);
    float root = anim->root_height;
    const uint16_t none = 0xfffe;
    std::memcpy(MPGame + b->player * 0x30 + 0x20, &none, 2);
    HITTEST_tag& t = b->hit_test;
    t.mask = 0;
    t.cel = obj->cel;
    _VECTOR up;
    Mat_GetUp(&up, &obj->matrix);
    const int16_t sub = int16_t(obj->unknown_f6);
    const auto fall_step = [&](float k) { obj->pos = madd3(obj->pos, b->fall, k); };
    switch (sub) {
        case 1:
        case 2:
        case 5:
        case 6:
        case 12:
        case 14:
        case 15:
            break;
        case 8:
        case 9:
            fall_step(REC_FRAME_RATE * 3.0f);
            break;
        case 13:
            if ((anim->flags & 0x10) == 0) b->fall = madd3(b->fall, WldGravity, REC_FRAME_RATE);
            else Vec_Zero(&b->fall);
            b->fall.y = clamp_fall(b->fall.y);
            fall_step(REC_FRAME_RATE);
            break;
        default:
            if (obj->cel == nullptr || obj->cel->water_y <= obj->pos.y) {
                if ((anim->flags & 8) == 0 || b->floor_normal_y < 0.5f) {
                    b->fall = madd3(b->fall, WldGravity, REC_FRAME_RATE);
                } else if (b->jump == 1) {
                } else if (anim->floor.obj == nullptr) {
                    b->vel.y = 0.0f;
                    Vec_Zero(&b->fall);
                } else {
                    Vec_MulR32(&b->fall, &b->fall, REC_FRAME_RATE + REC_FRAME_RATE);
                }
                b->fall.y = clamp_fall(b->fall.y);
                fall_step(REC_FRAME_RATE);
            }
            break;
    }
    float len = 0.0f;
    _VECTOR axis = up;
    switch (sub) {
        case 1:
        case 7:
            t.radius = 0.275f;
            t.type = 0x800;
            t.unknown_90 = 0;
            t.start = madd3(obj->pos, up, 0.55f);
            len = t.radius - root;
            break;
        case 4:
            t.type = 0x800;
            t.ignore_b = b->gun;
            t.radius = 0.55f;
            t.unknown_90 = 0;
            t.start = madd3(obj->pos, up, 0.1f - (root - 0.55f));
            len = 0.45f - root;
            break;
        case 6:
            t.radius = 0.55f;
            t.ignore_b = b->ladder;
            t.type = 0x800;
            t.unknown_90 = 0;
            t.start = madd3(obj->pos, up, 0.275f);
            len = 2.1f - root;
            break;
        case 12:
            if (b->ride->type != 0x36) {
                t.type = 0x800;
                t.ignore_b = b->ride;
                t.radius = 0.55f;
                t.unknown_90 = 0;
                t.start = madd3(obj->pos, up, 0.275f);
                t.end = madd3(obj->pos, up, 0.55f - root);
                t.type = 0;
                obj->unknown_fd = 1;
                return;
            }
            [[fallthrough]];
        default:
            t.ignore_b = b->gun;
            t.type = 0x800;
            t.radius = 0.55f;
            t.unknown_90 = 0;
            t.start = madd3(obj->pos, up, 0.275f);
            len = 0.55f - root;
            break;
        case 13:
        case 14: {
            t.radius = root < 0.55f ? root : 0.55f;
            _VECTOR head, a, c;
            AnimGetBoneWorldTrans(obj, 0x80000005, 0, &head, nullptr);
            root = 0.5f;
            AnimGetBoneWorldTrans(obj, 0x80000037, 0, &a, nullptr);
            AnimGetBoneWorldTrans(obj, 0x80000033, 0, &c, nullptr);
            axis = sub4(head, a);
            const float l1 = Vec_NormaliseLen(&axis, &axis) * root;
            const float l2 = Vec_Dist3D(&head, &c) * root;
            t.ignore_b = b->gun;
            t.type = 0x800;
            const float one = 1.0f;
            std::memcpy(&t.unknown_90, &one, 4);
            t.start = head;
            len = 0.55f - (l1 < l2 ? l2 : l1);
            break;
        }
        case 15:
            t.radius = 0.55f;
            t.ignore_b = b->ladder;
            t.type = 0x800;
            t.mask = 0x1c0;
            t.unknown_90 = 0;
            t.start = madd3(obj->pos, up, 0.275f);
            len = 2.1f - root;
            break;
    }
    t.end = madd3(obj->pos, axis, len);
    obj->unknown_fd = 1;
}

// [decomp] 0x001949a8 Player_CollisionHandler
// Depois do Collide_Update (control_funcs[3].collide): procura o chão (Player_FeetOnPoint), conta o
// tempo de queda (mais de um segundo machuca ao pousar), aplica o empurrão da cápsula, trata cada
// acerto (Player_DealWithObjHit) e marca no anim se encostou em algo e se foi acima (teto) ou no
// chão. Depois a arma, a morte, o laser, o clarão do tiro, a corda e o relógio.
void Player_CollisionHandler(obj_tag* obj) {
    BLData* b = bloke_of(obj);
    b->touch_type = 0xff;
    HITDATA_tag* best = nullptr;
    if (b->active == 0) return;
    ANIMOBJ_tag* anim = anim_of(obj);
    anim->flags &= 0xffc7;
    anim->flags &= 0xff4f;
    anim->floor.surface = 0;
    if (debug_scanmode != 0) return;
    // O primeiro acerto que empurra (sem o bit 2), menos a escada; acima ou abaixo do começo da
    // cápsula.
    const auto first_pushing = [&](bool skip_ladder) {
        for (HITDATA_tag* h = obj->hits; h != nullptr;) {
            const uint8_t flags = h->flags;
            HITDATA_tag* cand = best != nullptr ? best : h;
            h = h->next;
            if ((flags & 2) == 0) best = cand;
            if (skip_ladder && best != nullptr && best->obj == b->ladder) best = nullptr;
        }
    };
    if (obj->unknown_f6 == kPlrClimb) {
        if (obj->hits != nullptr) first_pushing(true);
        if (best == nullptr) {
            b->fall_time = 0.0f;
        } else {
            const _VECTOR d = sub4(best->pos, b->hit_test.start);
            if (0.0f < d.y) anim->flags |= 0x20;
            if (d.y < 0.0f) anim->flags |= 8;
            b->fall_time = 0.0f;
        }
    } else {
        Player_FeetOnPoint(obj, &b->hit_test, &b->floor_normal_y, uint16_t(obj->state - 2) < 2 ? 1 : 0);
        const int16_t bits = int16_t(b->ground_bits);
        b->ground_bits = uint16_t(bits >> 1);
        if ((anim->flags & 8) != 0) b->ground_bits = uint16_t((bits >> 1) | 8);
        switch (obj->unknown_f6) {
            case 1:
            case 2:
            case 3:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
            case 13:
            case 14:
            case 15:
                b->fall_time = 0.0f;
                break;
            default:
                break;
        }
        bool check_hits = true;
        if (b->ground_bits == 0) {
            if (0.0f <= b->fall.y) b->fall_time = 0.0f;
            else b->fall_time += FRAME_RATE_MUL;
            if (GameState.level_file == 0x7000012 && obj->pos.y < -10.0f) switch_channels[0x7c] = 1;
        } else {
            if (60.0f < b->fall_time) {
                const float over = b->fall_time - 60.0f;
                const int32_t damage = int16_t(int32_t(0.0f < over ? over : 0.0f));
                if (10 <= damage) Player_HandlePain(obj, b, float(damage), 6, -1);
            }
            b->fall_time = 0.0f;
        }
        if (check_hits && obj->hits != nullptr) {
            obj->pos = add4(obj->pos, b->hit_test.push);
            for (HITDATA_tag* h = obj->hits; h != nullptr;) {
                Player_DealWithObjHit(obj, b, h);
                const uint8_t flags = h->flags;
                HITDATA_tag* cand = best != nullptr ? best : h;
                h = h->next;
                if ((flags & 2) == 0) best = cand;
            }
            if (best != nullptr) {
                const _VECTOR d = sub4(best->pos, b->hit_test.start);
                anim->flags |= 0x10;
                if (0.0f < d.y) anim->flags |= 0x20;
            }
        }
        if ((anim->flags & 8) != 0 && b->jump != 1) {
            b->vel.y = 0.0f;
            Vec_Zero(&b->fall);
        }
    }
    matrix_row3(obj->matrix) = obj->pos;
    obj->mode |= 0x20;
    Player_PositionGun(obj, b);
    AnimObjectUpdate(b->gun);
    ads_pose_arms(obj);  // só do port: os braços na pose de mira moderna (engine/player_ads.h)
    glb_viewer[b->player]->unknown_24 = b->cam_mode;
    Player_CheckForDeath(obj, 3);
    Player_LaserPointer(obj);
    if (b->muzzle_flash != 0) Player_MuzzleFlash(obj, &b->muzzle);
    int16_t t = b->muzzle_time;
    if (0 < t) {
        --b->muzzle_time;
        t = b->muzzle_time;
    }
    if (t == 0) View_SetDrawInNoViews(b->muzzle_obj);
    else Draw_MuzzleFlash(obj);
    Player_GrappleSetRope(obj);
    Player_SetWatch(obj);
}

// [decomp] 0x00194188 Player_FeetOnPoint
// Raio da posição até 0,6 abaixo do pé da cápsula. O acerto mais próximo é o chão se estiver a até
// root_height + 0,1 da posição (normal_y recebe a inclinação dele); o anim copia o material do chão
// e, no primeiro objeto pisado que não seja personagem, porta ou similar, o jogador herda o
// movimento dele (plataformas) e o objeto recebe um acerto vindo de cima. Devolve o objeto pisado.
obj_tag* Player_FeetOnPoint(obj_tag* obj, HITTEST_tag* test, float* normal_y, int8_t) {
    HITDATA_tag* stand = nullptr;
    HITDATA_tag* surface = nullptr;
    bool done = false;
    HITDATA_tag* hits = nullptr;
    _VECTOR up;
    Mat_GetUp(&up, &obj->matrix);
    if (normal_y != nullptr) *normal_y = 1.0f;
    ANIMOBJ_tag* anim = anim_of(obj);
    const float root = anim->root_height;
    const _VECTOR to = madd3(test->end, up, -(root + 0.6f));
    Collide_RayIntersect(&obj->pos, &to, obj->cel, obj, nullptr, &hits, 0, 0xc, 8);
    if (hits != nullptr) {
        surface = hits->surface != 0 ? hits : nullptr;
        if (hits->dist - 0.1f <= root) {
            stand = hits;
            if (normal_y != nullptr) *normal_y = hits->plane.b;
        }
        for (HITDATA_tag* h = hits; h != nullptr; h = h->next) {
            if (h->cel != nullptr && (h->cel->glist_flags & 0x40) != 0) continue;
            obj_tag* o = h->obj;
            if (o != nullptr && (o->flags & 0x40) != 0) continue;
            if (done) continue;
            if (o != nullptr) {
                switch (o->type) {
                    case 2:
                    case 3:
                    case 5:
                    case 0x11:
                    case 0x12:
                    case 0x1a:
                        break;
                    default:
                        Control_InheritVelocity(obj, h->obj, 3.0f);
                        anim->flags |= 0x80;
                        break;
                }
                HITDATA_tag* n = Coll_AddHitToList(&h->obj->hits, 1.0f, nullptr, obj);
                n->unknown_08 = 0;
                std::memcpy(&n->plane, &CONSTANT_UP1, sizeof(_VECTOR));
                Vec_Negate(&CONSTANT_UP1, &n->dir);
                n->pos = h->pos;
            }
            done = true;
        }
    }
    if (test->found != 0 || stand != nullptr) anim->flags |= 8;
    if (surface != nullptr && surfaced != 0) {
        const uint8_t s = anim->floor.surface;
        if ((0xe < s || s < 0xd) && (anim->flags & 8) != 0) amemcpy(anim, surface, 0x60);
    }
    Collide_FreeHitList(&hits);
    return stand != nullptr ? stand->obj : nullptr;
}

// [decomp] 0x00194458 Player_DealWithObjHit
// Um acerto da cápsula: água (materiais 13 e 14) vira o chão do anim e faz marola nos passos;
// objetos que machucam (0x27), corda (0x2c), escada (0x2d) e balas (5: dano pela arma de quem
// atirou, direção do golpe na tela, som e empurrão de lado para alguns tiros) têm tratamento.
uint32_t Player_DealWithObjHit(obj_tag* obj, BLData* b, HITDATA_tag* hit) {
    bool special = false;
    ANIMOBJ_tag* anim = anim_of(obj);
    const uint8_t surface = hit->surface & 0x3f;
    if (surface < 0xf && 0xc < surface) {
        const uint8_t s = anim->floor.surface;
        if (!(s < 0xf && 0xc < s)) amemcpy(anim, hit, 0x60);
        if ((anim->flags & 4) != 0) Env_WaterRipple(hit);
    }
    obj_tag* o = hit->obj;
    if (o == nullptr) return 0;
    float hit_damage;
    std::memcpy(&hit_damage, &hit->unknown_08, 4);
    switch (o->type) {
        case 0x27:
            Player_HandlePain(obj, b, hit_damage + Hurt_GetDamage(o), Hurt_GetType(hit->obj), -1);
            return 0;
        case 0x2c:
            Player_CollWire(obj, b, o);
            return 0;
        case 0x2d:
            Player_CollLadder(obj, b, o);
            return 0;
        case 5:
            break;
        default:
            return 0;
    }
    uint8_t* bullet = static_cast<uint8_t*>(o->data);
    obj_tag* shooter;
    std::memcpy(&shooter, bullet + 0x30, 4);
    uint8_t* info;
    std::memcpy(&info, bullet + 0x44, 4);
    uint16_t kind;
    std::memcpy(&kind, &hit->unknown_52, 2);
    const auto scale_damage = [&](float k) {
        hit_damage *= k;
        std::memcpy(&hit->unknown_08, &hit_damage, 4);
    };
    if (mp_flag(0x184) != 0) {
        if (3 < Control_Plr2Ind(shooter) && shooter != nullptr) {
            const int16_t weapon = read16(info);
            uint8_t* drone;
            std::memcpy(&drone, static_cast<uint8_t*>(shooter->data) + 0xd1c, 4);
            const uint8_t skill = drone[0xac];
            if (weapon == 1 || weapon == 100 || weapon == 0x65 || weapon == 0x66) {
                if ((skill & 4) != 0) scale_damage(1.5f);
            } else if ((skill & 2) != 0) {
                scale_damage(1.25f);
            }
        }
        if (mp_flag(0x1a4) == 0x400 && MP_IsAssasin(shooter) && MP_IsTarget(obj)) {
            if (kind != 0 && kind < 5) {
                const float a = ATAN2_APPROX(hit->dir.x, hit->dir.z);
                const float diff = Vec_AngleDifference(a, obj->rot.y);
                if (std::fabs(diff * 57.295776f) < 70.0f) std::memcpy(&hit->unknown_08, &b->health, 4);
            } else if (kind == 5) {
                std::memcpy(&hit->unknown_08, &b->health, 4);
            }
            std::memcpy(&hit_damage, &hit->unknown_08, 4);
        }
        special = kind == 5;
    }
    if (!MP_RegisterBulletHit(b->player, hit->obj, b->health - hit_damage)) return 0;
    uint8_t* data = static_cast<uint8_t*>(hit->obj->data);
    if (!Drone_ModPlayerHitDamage(obj, hit)) return 0;
    std::memcpy(&hit_damage, &hit->unknown_08, 4);
    Player_HandlePain(obj, b, hit_damage, 0, int16_t(kind));
    _VECTOR dir = hit->dir;
    ApplyMatrixLVI(&glb_viewer[b->player]->view, &dir);
    Vec_Normalise(&dir, &dir);
    b->hit_dir = 0;
    uint8_t hd = 0xc;
    if (0.5f <= std::fabs(dir.x) || 0.5f <= std::fabs(dir.y)) {
        if (std::fabs(dir.y) < std::fabs(dir.x)) {
            if (dir.x < 0.0f) b->hit_dir |= 4;
            hd = dir.x <= 0.0f ? b->hit_dir : uint8_t(b->hit_dir | 8);
        } else {
            if (dir.y < 0.0f) b->hit_dir |= 1;
            hd = dir.y <= 0.0f ? b->hit_dir : uint8_t(b->hit_dir | 2);
        }
    }
    b->hit_dir = hd;
    std::memcpy(&info, data + 0x44, 4);
    uint32_t info_flags;
    std::memcpy(&info_flags, info + 0x70, 4);
    if ((info_flags & 0x180) == 0) Sound_Play(special ? 0x600 : 0x412, 100.0f, 0, 0);
    uint16_t push;
    std::memcpy(&push, info + 0x10, 2);
    if (mp_flag(0x180) == 0 && (push & 1) != 0) {
        const int16_t weapon = read16(info + 2);
        if (weapon == 0x65) b->turn_kick = -0.1f;
        if (weapon == 0x66) b->turn_kick = 0.1f;
        if (weapon == 99) b->turn_kick = 0.1f;
        Sound_Play3D(0x171, &obj->pos, 100.0f, -1.0f, -1.0f, 0, 0, 0);
    }
    return 0;
}

// [decomp] 0x001a5170 Player_Activate
// Usar: uma esfera de raio 1 um metro à frente dos olhos; ativa o que encontrar (portas, gatilhos,
// interruptores, carros, cadeados...). Encostado numa parede de esgueirar (touch_type 6), gruda
// nela. Devolve se abriu ou fechou alguma porta.
uint8_t Player_Activate(obj_tag* obj, BLData* b) {
    uint8_t opened = 0;
    HITDATA_tag* hits = nullptr;
    if (b->touch_type == 6) {
        obj_tag* best = nullptr;
        float best_d = 1000.0f;
        for (obj_tag* o = control_first_object(); o != nullptr; o = o->next) {
            if (o->type != 0x2b) continue;
            const float d = Vec_SqDist3D(&o->pos, &obj->pos);
            if (d < best_d) {
                best = o;
                best_d = d;
            }
        }
        if (best != nullptr) {
            Player_CollCreepWall(obj, b, best);
            return 1;
        }
    }
    _VECTOR head, dir;
    Player_GetHeadPos(&head, obj, nullptr);
    Mat_GetDir(&dir, &glb_viewer[b->player]->camera);
    head = add4(head, dir);
    if (!Collide_SphereIntersect(&head, 1.0f, obj->cel, obj, b->gun, &hits, 0, 0, 0, 4)) return 0;
    for (HITDATA_tag* h = hits; h != nullptr; h = h->next) {
        obj_tag* o = h->obj;
        if (o == nullptr) continue;
        if (o->script != nullptr && o->type == 0) {
            SP_Activate(o, obj);
            continue;
        }
        switch (o->type) {
            case 0x1c:
                opened |= uint8_t(Door_Activate(o, obj, 1));
                break;
            case 0x1d:
                Trigger_Activate(o, obj);
                break;
            case 0x25:
                SS_Activate(o, obj);
                break;
            case 0x28:
                Car_Activate(o, obj);
                break;
            case 0x36:
                GT_Activate(o, obj);
                break;
            case 0x38:
                Monitor_Activate(o, obj);
                break;
            case 0x3a: {
                // Cadeado: só pela frente.
                _VECTOR f;
                Mat_GetDir(&f, &o->matrix);
                const _VECTOR d = sub4(matrix_row3(obj->matrix), matrix_row3(o->matrix));
                if (f.x * d.x + f.y * d.y + f.z * d.z < 0.0f) break;
                Lock_Activate(o, obj);
                break;
            }
            case 0x47:
                GunImp_Activate(o, obj);
                break;
            case 0x49:
                PCQWorm_Activate(o, obj);
                break;
            default:
                break;
        }
    }
    Collide_FreeHitList(&hits);
    return opened;
}

// [decomp] 0x0018f5d8 Player_PositionCamera
// Atualizador da câmera do jogador (Camera_SetUpdator). Desce os olhos ao agachar; no modo 0
// (primeira pessoa) esconde o corpo nesta câmera, sobe os olhos para 0,7 fora d'água (0 debaixo) e
// põe a câmera na cabeça (Camera_SetToPlayer). Os outros modos são câmeras de terceira pessoa,
// veículos e controles remotos.
void Player_PositionCamera(obj_tag* obj, void*) {
    BLData* b = bloke_of(obj);
    const int8_t index = b->player;
    viewer_tag* v = glb_viewer[index];
    if (!GS_IsPaused(-1)) {
        const int16_t s = int16_t(obj->unknown_f6);
        if (s == kPlrCrouch) {
            const float c = b->crouch_drop + FRAME_RATE_MUL * 0.022499999f;
            b->crouch_drop = c;
            if (0.45f < c) b->crouch_drop = 0.45f;
        } else if (!(s >= 10 && s < 0xd) && s != 0x10) {
            const float c = b->crouch_drop - FRAME_RATE_MUL * 0.022499999f;
            b->crouch_drop = c;
            if (c < 0.0f) b->crouch_drop = 0.0f;
        }
    }
    switch (b->cam_mode) {
        case 0:
        case 0xe: {
            View_SetDrawInOtherViewsOnly(obj, uint32_t(index));
            View_DrawInThisViewOnly(b->gun, uint32_t(index));
            ANIMOBJ_tag* anim = anim_of(obj);
            if ((anim->flags & 1) != 0 && (weapon_u32(anim->weapon, 0x68) & 0x40) != 0) View_SetDrawInNoViews(b->gun);
            if (weapon_u32(anim_of(obj)->weapon, 0xdc) == 0) View_SetDrawInNoViews(b->gun);
            const float target = surfaced == 0 ? 0.0f : 0.7f;
            const float d = target - b->eye_height;
            float e = b->eye_height + d * 0.1f;
            if (0.0f <= d ? target < e : e < target) e = target;
            b->eye_height = e;
            v->lod_scale = b->zoom;
            if (uint16_t(obj->state - 2) < 2) b->crouch_drop = 0.0f;
            Camera_SetToPlayer(obj, v);
            return;
        }
        case 1:
            v->lod_scale = 1.0f;
            b->orbit_obj = obj;
            AnimGetBoneWorldTrans(obj, 0x80000005, 0, &b->orbit_target, nullptr);
            View_SetDrawInAllViews(obj);
            View_SetDrawInNoViews(b->gun);
            Camera_Orbit(v, &b->orbit_target);
            v->cel = nullptr;
            break;
        case 2:
            View_SetDrawInAllViews(obj);
            View_SetDrawInNoViews(b->gun);
            camera_track_XZ_pos_behind(&obj->pos, &obj->rot, v, 1.0f, 10.0f);
            camera_track_Y_pos(obj, v, 1.0f, 0.0f);
            Camera_PosYRot(&obj->pos, v, 1.0f);
            Camera_PosXRot(&obj->pos, v, 1.0f);
            v->lod_scale = 1.0f;
            v->cel = nullptr;
            break;
        case 3:
            View_SetDrawInAllViews(obj);
            View_SetDrawInNoViews(b->gun);
            camera_freechase(obj, v);
            v->cel = nullptr;
            v->lod_scale = 1.0f;
            break;
        case 4:
            View_SetDrawInAllViews(obj);
            View_SetDrawInNoViews(b->gun);
            camera_fixed(obj, v);
            v->lod_scale = 1.0f;
            break;
        case 5:
            View_SetDrawInAllViews(obj);
            View_SetDrawInNoViews(b->gun);
            camera_snake(obj, v);
            v->cel = nullptr;
            v->lod_scale = 1.0f;
            break;
        case 6:
            View_SetDrawInAllViews(obj);
            View_SetDrawInNoViews(b->gun);
            camera_chasecam(obj, v);
            v->cel = nullptr;
            v->lod_scale = 1.0f;
            break;
        case 8: {
            View_SetDrawInAllViews(obj);
            View_SetDrawInNoViews(b->gun);
            v->pos.x = obj->pos.x;
            v->pos.z = obj->pos.z;
            v->rot.y = 0.0f;
            v->rot.z = 0.0f;
            v->rot.x = 1.5707964f;
            v->lod_scale = 1.0f;
            v->pos.y = obj->pos.y + 2.0f + debug_cam_posy;
            Camera_Set(v, &v->pos, &v->rot);
            return;
        }
        case 9: {
            obj_tag* r = b->ride;
            if (r == nullptr) return;
            _VECTOR p{0.0f, 0.0f, -0.2f, 0.0f};
            View_SetDrawInAllViews(obj);
            const _VECTOR rr{r->rot.x, r->rot.y, r->rot.z, 0.0f};
            MATRIX m;
            RotMatrixZYX(&rr, &m);
            ApplyMatrixLVI(&m, &p);
            p.x += r->pos.x;
            p.y += r->pos.y;
            p.z += r->pos.z;
            v->pos.x = p.x;
            v->pos.y = p.y;
            v->pos.z = p.z;
            v->pos.w = p.w;
            v->rot.x = r->rot.x;
            v->rot.y = r->rot.y;
            v->lod_scale = 1.0f;
            v->rot.z = r->rot.z;
            Camera_Set(v, &v->pos, &v->rot);
            return;
        }
        case 10: {
            obj_tag* other = glb_blokes[index]->unknown_87c;
            View_SetDrawInNoViews(obj);
            View_SetDrawInAllViews(other);
            View_SetDrawInNoViews(b->gun);
            camera_chasecam(other, v);
            v->lod_scale = 1.0f;
            break;
        }
        case 0xb:
            View_SetDrawInAllViews(obj);
            View_SetDrawInNoViews(b->gun);
            camera_tracking(obj, v);
            v->cel = nullptr;
            v->lod_scale = 1.0f;
            break;
        case 0xc:
            if (b->ride == nullptr) return;
            View_SetDrawInAllViews(obj);
            View_SetDrawInNoViews(b->gun);
            View_SetDrawInOtherViewsOnly(b->ride, uint32_t(index));
            Mat_Copy(&b->ride->matrix, &v->camera);
            return;
        case 0xd: {
            if (b->ride == nullptr) return;
            View_SetDrawInAllViews(obj);
            View_SetDrawInNoViews(b->gun);
            View_SetDrawInOtherViewsOnly(b->ride, uint32_t(index));
            _VECTOR up;
            Mat_GetUp(&up, &b->ride->matrix);
            Mat_Copy(&b->ride->matrix, &v->camera);
            const float k = b->ride->glist == hashtable_hashcode_to_celglist(0x2000194) ? 0.1f
                                                                                        : std::fabs(b->ride->glist->sphere.w * 3.0f);
            matrix_row3(v->camera) = madd3(matrix_row3(b->ride->matrix), up, k);
            break;
        }
        case 0xf: {
            if (b->ride == nullptr) return;
            uint8_t* zip = static_cast<uint8_t*>(b->ride->data);
            obj_tag* line;
            std::memcpy(&line, zip + 0xc0, 4);
            View_SetDrawInAllViews(obj);
            View_SetDrawInNoViews(b->gun);
            View_SetDrawInOtherViewsOnly(b->ride, uint32_t(index));
            _VECTOR p{0.0f, 0.15f, -0.4f, 0.0f};
            ApplyMatrixLVI(&line->matrix, &p);
            _VECTOR at = add4(p, matrix_row3(line->matrix));
            Mat_Copy(&line->matrix, &v->camera);
            Mat_SetTrans(&at, &v->camera);
            break;
        }
        default:
            break;
    }
}

// [decomp] 0x00195aa0 Player_GetHeadPos
// Os olhos: eye_height - crouch_drop acima de obj_tag::pos (na vertical do mundo); na varredura e
// na gravidade zero, ao longo da vertical do próprio jogador. m recebe a câmera do jogador.
void Player_GetHeadPos(_VECTOR* out, obj_tag* obj, MATRIX* m) {
    BLData* b = bloke_of(obj);
    const int16_t s = int16_t(obj->unknown_f6);
    if (s == kPlrScanMode || (s < 10 && 7 < s)) {
        const _VECTOR local{0.0f, b->eye_height - b->crouch_drop, 0.0f, out->w};
        *out = add4(rotate3(obj->matrix, local), obj->pos);
        return;
    }
    out->x = obj->pos.x;
    out->y = (obj->pos.y + b->eye_height) - b->crouch_drop;
    out->z = obj->pos.z;
    if (m != nullptr) Mat_Copy(&glb_viewer[b->player]->camera, m);
}

// [decomp] 0x00193498 Player_InShadow
// Quanta luz chega ao jogador (BLData::unknown_914): a cor da sala mais as luzes próximas, pela
// distância; agachado conta a metade. Os inimigos usam para enxergar.
void Player_InShadow(BLData* b, obj_tag* obj) {
    uint32_t sum = uint32_t(int32_t(float(uint32_t(obj->color[0]) + obj->color[1] + obj->color[2]) * 0.33333334f)) & 0xffff;
    uint16_t count = 0;
    light_tag** lights =
        Lights_CalcClosestLights(reinterpret_cast<sphere_tag*>(&obj->sphere), uint16_t(obj->flags), &count);
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t* l = reinterpret_cast<const uint8_t*>(lights[i]);
        _VECTOR pos;
        std::memcpy(&pos, l + 0x10, sizeof(pos));
        float radius;
        std::memcpy(&radius, l + 0x3c, 4);
        const float d = Vec_Dist3D(&pos, &obj->sphere);
        sum = (sum + uint32_t(int32_t(float((uint32_t(l[0x44]) + l[0x45] + l[0x46]) >> 3) * (d / radius)))) & 0xffff;
    }
    b->unknown_914 = float(sum) * 0.003921569f;
    if (obj->unknown_f6 == kPlrCrouch) b->unknown_914 = float(sum) * 0.003921569f * 0.5f;
}

// Só para testes.
void player_debug_power_on() {
    g_player_mouse_look = PlayerMouseLook{};
    Player_ResetStartPos();
    std::memset(glb_blokes, 0, sizeof(glb_blokes));
    std::memset(weapon_data, 0, sizeof(weapon_data));
    WldGravity = {0.0f, 0.0f, 0.0f, 0.0f};
    surfaced = 0;
    debug_scanmode = 0;
    debug_clipmode = 0;
    benchmark = 0;
    g_player_init_call = PlayerInitCall{};
}
