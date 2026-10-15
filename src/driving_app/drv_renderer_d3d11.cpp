// O renderizador Direct3D 11 do nf_driving: o backend do Windows de drv_renderer.h.

#include "driving_app/drv_renderer.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <map>
#include <vector>

#include "platform/gpu_adapter.h"
#include "platform/image_io.h"
#include "render/gpu_adapters_dxgi.h"
#include "platform/fmath.h"

namespace drv {

using Microsoft::WRL::ComPtr;

namespace {

// O microprograma da família MatCar (driving/mat_car.h, passos 2-7; o passo 1, a conversão dos fluxos inteiros, é feito
// na carga por car_convert) e o GS. As contas seguem a ordem do microcódigo: multiplicação e soma separadas (o MADD da VU
// arredonda o produto), FTOI0 = trunc.
const char kShader[] = R"HLSL(
cbuffer Object : register(b2) {
    row_major float4x4 world;      // a matriz da peça (gpModelMatrix = instância x carro): p' = p x M
    int4 obj_flags;                // x: 1 sem névoa (RFog::DisableFog), 2 sem luzes pontuais (o céu)
};
cbuffer Frame : register(b0) {
    row_major float4x4 view_proj;  // a câmera do PC
    float4 point_pos[4];           // VU 0x40-0x43 (SoA: x, y, z, K)
    float4 point_col[4];           // VU 0x44-0x47
    float4 fog;                    // VU 0x48
    float4 light_dir_t[3];         // VU 0x49-0x4b
    float4 light_col[3];           // VU 0x4c-0x4e
    float4 ambient;                // VU 0x4f
    float4 spec_mat[4];            // VU 0x50-0x53
    float4 refl;                   // VU 0x55 (ReflectionInfo2)
    float4 damage[16];             // VU 0x56-0x65
    float4 fog_color;              // FOGCOL do GS (0..255)
    float4 camera_pos;             // GAME::CameraPos (o mapa de ambiente do MatBondReflectTexturePacked)
    row_major float4x4 view;       // a vista do PC (a ModelView do vidro = world x view)
    float4 dyn_lights[7];          // GAME::LightSources (MatDynamicTexturePacked): as direções transpostas e as cores
};
cbuffer Draw : register(b1) {
    float4 props;   // LightingProperties
    int4 mode;      // x o STQ da passada (0 base, 1 especular, 2 reflexo), y textura ligada, z névoa (FGE), w substituto
    int4 gs;        // x TFX, y TCC, z teste de alfa (0 sem, 1 só quem passa, 2 só quem falha), w ATST
    float4 gs2;     // x AREF
    int4 mode2;     // x o alfa (0 o 128, 1 o w da zona de dano, 2 int(127 - w): o vidro), y a face de trás (o vidro),
                    // z o passo das luzes (MatCarTextureSwitchPacked), w a cor por vértice (MatCarGouraudDullPacked)
    float4 sw;      // SwitchData
    float4 swp;     // SwitchParam
    float4 bond_off;  // MatBond: o deslocamento da STQ da passada 1 (xy) e da 2 (zw): o passo 3b (Scroll2, FrameAnim)
};
Texture2D tex : register(t0);
SamplerState smp : register(s0);

struct VSIn {
    float3 pos : POSITION;
    float3 stq : TEXCOORD0;
    float3 nrm : NORMAL;
    float4 dv : TEXCOORD1;
    float3 dn : TEXCOORD2;
    uint zone : TEXCOORD3;
    uint4 rgbw : TEXCOORD4;  // o Gouraud: a cor crua do ColourSet
};
struct VSOut {
    float4 pos : SV_Position;
    float3 uvp : TEXCOORD0;                // a STQ da passada 1 (S = s Q, T = t Q, Q = z/w: o GS divide S/Q)
    noperspective float2 uva : TEXCOORD1;  // as das passadas 2 e 3 (Q = 1: afim na tela, como o GS)
    noperspective float4 col : COLOR0;     // Gouraud do GS: linear na tela, 0..255
    noperspective float fogf : FOG;
    nointerpolation float kill : KILL;     // o vidro: o ADC da face de trás do vértice que fecha o triângulo (o provocador)
};

VSOut vs_main(VSIn i) {
    VSOut o;
    float4 dv = i.dv;
    float3 dn = i.dn;
    float3 n = i.nrm;
    // 2. dano
    float4 z = damage[i.zone & 15];
    precise float3 p = i.pos + dv.xyz * z.x;
    n = n + dn * z.x;
    float3 col = trunc(max(127.0 * (1.0 - dv.w * z.y), 0.0));
    float alpha_f = max(128.0 * (1.0 - dv.w * z.z), 0.0);
    // 3. luzes direcionais (as linhas da matriz do mundo normalizadas pelo ERLENG)
    float3 r[3];
    [unroll] for (int k = 0; k < 3; ++k) r[k] = world[k].xyz * (1.0 / sqrt(dot(world[k].xyz, world[k].xyz)));
    float3 A[3];
    [unroll] for (int k2 = 0; k2 < 3; ++k2)
        A[k2] = light_dir_t[0].xyz * r[k2].x + light_dir_t[1].xyz * r[k2].y + light_dir_t[2].xyz * r[k2].z;
    float3 d = max(A[0] * n.x + A[1] * n.y + A[2] * n.z, 0.0);
    float3 l = (light_col[0].xyz * props.y) * d.x + light_col[1].xyz * d.y + light_col[2].xyz * d.z + ambient.xyz * props.x;
    col = trunc(min(l * col, 255.0));
    // 4. luzes pontuais
    float3 pw = mul(float4(p, 1.0), world).xyz;
    float4 dx = point_pos[0] - pw.x, dy = point_pos[1] - pw.y, dz = point_pos[2] - pw.z;
    float4 att = max((dx * dx + dy * dy + dz * dz) * -point_pos[3] + 255.0, 0.0);
    float3 c4 = col + point_col[0].xyz * att.x;
    c4 = c4 + point_col[1].xyz * att.y;
    c4 = c4 + point_col[2].xyz * att.z;
    c4 = c4 + point_col[3].xyz * att.w;
    col = trunc(min(c4, 255.0));
    // O Gouraud (mat_car.h): a luz direcional vezes 127 (não a cor), as pontuais somadas a ela, depois vezes a cor do
    // ColourSet com o dano e / 128; o GS lê os 8 bits de baixo de cada palavra (as cores lixo além do ColourSet
    // passam de 255).
    if (mode2.w != 0) {
        float3 cs = max(float3(asint(i.rgbw.xyz)) * (1.0 - dv.w * z.y), 0.0);
        cs = trunc(min(cs, 2147483520.0));
        float3 dl = trunc(min(l * 127.0, 255.0));
        float3 v4 = dl + point_col[0].xyz * att.x;
        v4 = v4 + point_col[1].xyz * att.y;
        v4 = v4 + point_col[2].xyz * att.z;
        v4 = v4 + point_col[3].xyz * att.w;
        float3 fin = trunc(min((min(v4, 255.0) * cs) * (1.0 / 128.0), 2147483520.0));
        col = float3(uint3(max(fin, 0.0)) & 0xff);
    }
    // As luzes: a cor e o alfa puxados para (s.z, s.z, s.z, s.y) x 255 x k.x por s.w.
    float alpha_sw = mode2.w != 0 ? float(i.rgbw.w & 0xff) : 128.0;
    if (mode2.z != 0) {
        float4 target = (float4(sw.zzz, sw.y) * 255.0) * swp.x;
        float4 v = float4(col, 128.0) * (1.0 - sw.w) + target * sw.w;
        v = trunc(min(v, 255.0));
        col = v.rgb;
        alpha_sw = v.a;
    }
    // 5. transformação (a do PC) e a névoa do microprograma
    precise float4 clip = mul(float4(pw, 1.0), view_proj);
    o.pos = clip;
    float ww = min(max(clip.w, fog.x), fog.z);
    o.fogf = 255.0 - fog.w * (ww - fog.x);
    o.uvp = i.stq;  // interpolado com perspectiva: (soma de s/w) / (soma de z/w), como S/Q no GS
    o.uva = i.stq.xy;
    // O alfa: o 128 da cor de partida; no vidro o w cru da zona de dano (passada 1) ou int(127 - w) (as outras). O GS lê
    // só os 8 bits de baixo do A do RGBAQ.
    float alpha = alpha_sw;
    if (mode2.x != 0) {
        int zw = asint(z.w);
        alpha = float((mode2.x == 1 ? zw : int(trunc(float(zw) * -1.0 + 127.0))) & 0xFF);
    }
    o.col = float4(col, alpha);
    o.kill = 0.0;
    if (mode2.y != 0) {
        // A face de trás do vidro: n' e p' na câmera (n' = M0 n.x + M1 n.y + M2 n.z, p' = p x M), d = (z + x) + y.
        float3 nw = world[0].xyz * n.x + world[1].xyz * n.y + world[2].xyz * n.z;
        float3 nc = view[0].xyz * nw.x + view[1].xyz * nw.y + view[2].xyz * nw.z;
        float3 pc = mul(float4(pw, 1.0), view).xyz;
        float dd = (nc.z * pc.z + nc.x * pc.x) + nc.y * pc.y;
        o.kill = dd >= 0.0 ? 1.0 : 0.0;
    }
    if (mode.x == 1) {  // 6. o especular: mapa esférico das normais
        float4 E[3];
        [unroll] for (int k3 = 0; k3 < 3; ++k3)
            E[k3] = (spec_mat[0] * world[k3].x + spec_mat[1] * world[k3].y + spec_mat[2] * world[k3].z +
                     spec_mat[3] * world[k3].w) * refl.z;
        float4 e = E[0] * n.x + E[1] * n.y + E[2] * n.z;
        o.uva = e.xy + 0.5;
        o.col = float4(trunc(props.z * alpha_f).xxx, alpha);
    } else if (mode.x == 2) {  // 7. o reflexo
        float3 v = p * refl.x + n;
        float4 a = (world[0] * refl.y) * v.x + (world[1] * refl.y) * v.y + (world[2] * refl.y) * v.z;
        o.uva = float2(a.x + 0.5, a.z + 0.5);
        float gate = max(a.y, 0.0) * 100000.0;
        o.col = float4(trunc(min(props.w * alpha_f, gate)).xxx, alpha);
    }
    return o;
}

// A família MatBond do mundo (driving/mat_bond.h): a cor do vértice + as luzes pontuais, a transformação, a névoa e,
// no Reflect, a STQ do mapa de ambiente (passo 2). As duas passadas usam a STQ com perspectiva (S/Q no GS).
struct BondIn {
    float4 pos : POSITION;
    float3 stq : TEXCOORD0;
    float4 col : COLOR0;
};
// O segundo fluxo (só os desenhos do Dynamic e dos Scroll2 o ligam: o primeiro fica com 44 bytes por vértice).
struct BondExt {
    float3 stq1 : TEXCOORD1;
    float3 nrm : NORMAL;
};
// mode2.w: o BondKind (0 Texture, 1 Reflect, 2 Dynamic, 3 Scroll2, 4 FrameAnim); mode.w a passada (0 ou 1).
VSOut bond_common(BondIn i, BondExt e) {
    VSOut o;
    float3 pw = mul(float4(i.pos.xyz, 1.0), world).xyz;
    float3 col = i.col.rgb;
    float alpha = i.col.a;
    if (mode2.w == 2) {
        // O passo 2b do Dynamic: M = as linhas do mundo x L, d = max(0, n x M), cor = int(min(255, 127 x (C0 d.x +
        // C1 d.y + C2 d.z + C3))), alfa 128 (a cor de trabalho (127, 127, 127, 128)).
        float3 r[3];
        [unroll] for (int k = 0; k < 3; ++k)
            r[k] = dyn_lights[0].xyz * world[k].x + dyn_lights[1].xyz * world[k].y + dyn_lights[2].xyz * world[k].z;
        float3 d = max(r[0] * e.nrm.x + r[1] * e.nrm.y + r[2] * e.nrm.z, 0.0);
        float3 s = dyn_lights[3].xyz * d.x + dyn_lights[4].xyz * d.y + dyn_lights[5].xyz * d.z + dyn_lights[6].xyz;
        col = trunc(min(s * 127.0, 255.0));
        alpha = 128.0;
    }
    if ((obj_flags.x & 2) == 0) {
        float4 dx = point_pos[0] - pw.x, dy = point_pos[1] - pw.y, dz = point_pos[2] - pw.z;
        float4 att = max((dx * dx + dy * dy + dz * dz) * -point_pos[3] + 255.0, 0.0);
        float3 c = col + point_col[0].xyz * att.x;
        c = c + point_col[1].xyz * att.y;
        c = c + point_col[2].xyz * att.z;
        c = c + point_col[3].xyz * att.w;
        col = trunc(min(c, 255.0));
    }
    precise float4 clip = mul(mul(i.pos, world), view_proj);  // o w da posição (1 nos "Packed"), como o MADDw
    o.pos = clip;
    float ww = min(max(clip.w, fog.x), fog.z);
    float fscale = (obj_flags.x & 1) ? 0.0 : fog.w;
    o.fogf = 255.0 - fscale * (ww - fog.x);
    float3 stq = i.stq;
    if (mode2.w == 3 && mode.w == 1) stq = float3(e.stq1.xy + bond_off.zw, e.stq1.z);  // o Texture1 dos Scroll2
    else if (mode2.w == 3 || mode2.w == 4) stq.xy += bond_off.xy;
    if (mode2.w == 1 && mode.w == 1) {  // o mapa de ambiente: a direção da câmera (5 m abaixo) ao ponto, x 0,4375
        float3 d = pw - (camera_pos.xyz - float3(0.0, 5.0, 0.0));
        d = d * (1.0 / sqrt(dot(d, d))) * 0.4375;
        stq = float3(0.5 + d.x, 0.5 + d.z, 1.0);
    }
    o.uvp = stq;
    o.uva = stq.xy;
    o.col = float4(col, alpha);
    o.kill = 0.0;
    return o;
}
VSOut vs_bond(BondIn i) {
    BondExt e;
    e.stq1 = float3(0.0, 0.0, 1.0);
    e.nrm = float3(0.0, 0.0, 0.0);
    return bond_common(i, e);
}
VSOut vs_bond_ext(BondIn i, BondExt e) { return bond_common(i, e); }

// A família de pele dos personagens (driving/mat_char.h): a paleta misturada (os três ossos e os pesos), a cor (as três
// luzes direcionais do GAME::ActorLightSources pelas linhas normalizadas da matriz do mundo, depois as quatro
// pontuais; a sombra do CharacterOptions.z dá preto), a transformação e as passadas 2 e 3 do Specular. Sem o recorte do
// microprograma (o ADC do 2049). O microprograma grava F = 0 no XYZF, e o estado dos personagens (o
// RUNTIME_ALLOC::EAGL::GeoPrimState dos modelos, a partir do construtor) não liga o FGE: sem névoa.
cbuffer Char : register(b3) {
    float4 char_light[7];  // GAME::ActorLightSources: as direções (0-2), as cores (3-5), o ambiente (6)
    float4 char_refl;      // CharacterReflectionInfo (o ReflectionInfo2 dos lotes)
    float4 char_opt;       // CharacterOptions (inteiros): z a sombra, w o alfa
};
cbuffer Bones : register(b4) {
    row_major float4x4 bones[128];  // o EAGLAnimationBuffer do quadro
};
struct CharIn {
    float3 pos : POSITION;    // p x escala
    float3 nrm : NORMAL;      // n x escala (3 x 3)
    float2 st : TEXCOORD0;
    int4 bone : BLENDINDICES;
    float4 wt : BLENDWEIGHT;
};
VSOut vs_char(CharIn i) {
    VSOut o;
    float4 p = float4(0.0, 0.0, 0.0, 0.0);
    float3 n = float3(0.0, 0.0, 0.0);
    [unroll] for (int k = 0; k < 3; ++k) {
        if (i.bone[k] >= 0) {
            const float4x4 b = bones[i.bone[k]];
            p += mul(float4(i.pos, 1.0), b) * i.wt[k];
            n += mul(i.nrm, (float3x3)b) * i.wt[k];
        }
    }
    float3 r[3];
    [unroll] for (int k2 = 0; k2 < 3; ++k2) r[k2] = world[k2].xyz * (1.0 / sqrt(dot(world[k2].xyz, world[k2].xyz)));
    float3 col = float3(0.0, 0.0, 0.0);
    if (char_opt.z == 0.0) {
        float3 A[3];
        [unroll] for (int k3 = 0; k3 < 3; ++k3)
            A[k3] = char_light[0].xyz * r[k3].x + char_light[1].xyz * r[k3].y + char_light[2].xyz * r[k3].z;
        float3 d = max(A[0] * n.x + A[1] * n.y + A[2] * n.z, 0.0);
        float3 l = (char_light[3].xyz * props.y) * d.x + char_light[4].xyz * d.y + char_light[5].xyz * d.z +
                   char_light[6].xyz * props.x;
        col = trunc(min(l * 127.0, 255.0));
        float3 pw = mul(float4(p.xyz, 1.0), world).xyz;
        float4 dx = point_pos[0] - pw.x, dy = point_pos[1] - pw.y, dz = point_pos[2] - pw.z;
        float4 att = max((dx * dx + dy * dy + dz * dz) * -point_pos[3] + 255.0, 0.0);
        float3 c4 = col + point_col[0].xyz * att.x;
        c4 = c4 + point_col[1].xyz * att.y;
        c4 = c4 + point_col[2].xyz * att.z;
        c4 = c4 + point_col[3].xyz * att.w;
        col = trunc(min(c4, 255.0));
    }
    precise float4 clip = mul(mul(p, world), view_proj);  // o w da posição (a soma dos pesos), como o MADDw
    o.pos = clip;
    o.fogf = 0.0;  // o F do XYZF (só vale com o FGE)
    o.uvp = float3(i.st, 1.0);
    o.uva = i.st;
    o.col = float4(col, char_opt.w);
    o.kill = 0.0;
    if (mode.x == 1) {  // o especular: E = linhas do mundo x SpecularMat, normalizadas, x ReflectionInfo2.z
        float4 E[3];
        [unroll] for (int k4 = 0; k4 < 3; ++k4) {
            const float4 e = spec_mat[0] * world[k4].x + spec_mat[1] * world[k4].y + spec_mat[2] * world[k4].z +
                             spec_mat[3] * world[k4].w;
            E[k4] = e * (1.0 / sqrt(dot(e.xyz, e.xyz))) * char_refl.z;
        }
        const float4 a = E[0] * n.x + E[1] * n.y + E[2] * n.z;
        o.uva = a.xy + 0.5;
        o.col = float4(trunc(128.0 * props.z).xxx, char_opt.w);
    } else if (mode.x == 2) {  // o reflexo
        const float3 a = r[0] * char_refl.y * n.x + r[1] * char_refl.y * n.y + r[2] * char_refl.y * n.z;
        o.uva = float2(a.x + 0.5, a.z + 0.5);
        o.col = float4(trunc(min(128.0 * props.w, max(a.y, 0.0) * 100000.0)).xxx, char_opt.w);
    }
    return o;
}

bool alpha_test(float a, int atst, float aref) {
    if (atst == 0) return false;
    if (atst == 1) return true;
    if (atst == 2) return a < aref;
    if (atst == 3) return a <= aref;
    if (atst == 4) return a == aref;
    if (atst == 5) return a >= aref;
    if (atst == 6) return a > aref;
    return a != aref;
}

float4 ps_main(VSOut i) : SV_Target {
    if (i.kill > 0.5) discard;
    float4 cv = i.col;
    float4 c = cv;
    if (mode.y != 0) {
        float4 t = tex.Sample(smp, mode.x == 0 ? i.uvp.xy / i.uvp.z : i.uva) * 255.0;
        if (gs.x == 0) {  // MODULATE: (Ct x Cv) >> 7
            c.rgb = min(floor(t.rgb * cv.rgb / 128.0), 255.0);
            c.a = gs.y != 0 ? min(floor(t.a * cv.a / 128.0), 255.0) : cv.a;
        } else {  // DECAL
            c.rgb = t.rgb;
            c.a = gs.y != 0 ? t.a : cv.a;
        }
    }
    if (gs.z != 0) {
        bool ok = alpha_test(round(c.a), gs.w, gs2.x);
        if (gs.z == 1 && !ok) discard;
        if (gs.z == 2 && ok) discard;
    }
    if (mode.z != 0) c.rgb = floor((i.fogf * c.rgb + (255.0 - i.fogf) * fog_color.rgb) / 255.0);
    return float4(c.rgb / 255.0, saturate(c.a / 128.0));
}
)HLSL";

struct ObjectCB {
    float world[16];
    int32_t flags[4];
};
struct FrameCB {
    float view_proj[16];
    float point_pos[4][4];
    float point_col[4][4];
    float fog[4];
    float light_dir_t[3][4];
    float light_col[3][4];
    float ambient[4];
    float spec_mat[4][4];
    float refl[4];
    float damage[16][4];
    float fog_color[4];
    float camera_pos[4];
    float view[16];
    float dyn_lights[7][4];
};
struct DrawCB {
    float props[4];
    int32_t mode[4];
    int32_t gs[4];
    float gs2[4];
    int32_t mode2[4];
    float sw[4];
    float swp[4];
    float bond_off[4];
};
struct CharCB {
    float light[7][4];
    float refl[4];
    float opt[4];
};
struct BonesCB {
    float bones[kCarAnimBufferBones][16];
};

// A interface (os DrvSprite): os vértices já em coordenadas de recorte, a cor vezes a textura quando há.
const char kUiShader[] = R"HLSL(
struct UiIn { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; float tex : TEXCOORD1; };
struct UiOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; nointerpolation float tex : TEXCOORD1; };
UiOut vs_ui(UiIn i) {
    UiOut o;
    o.pos = float4(i.pos, 0.0, 1.0);
    o.uv = i.uv;
    o.col = i.col;
    o.tex = i.tex;
    return o;
}
Texture2D ui_tex : register(t0);
SamplerState ui_smp : register(s0);
float4 ps_ui(UiOut i) : SV_Target {
    float4 c = i.col;
    if (i.tex > 0.5) c *= ui_tex.Sample(ui_smp, i.uv);
    return c;
}
)HLSL";

// Os efeitos (DrvFxBatch): o vértice no recorte, ou no mundo vezes a vista e a projeção; a cor do GS (0x80 = 1,0) e o
// MODULATE do GS com a textura (Ct x Cv / 128, o alfa da textura e o do vértice na escala do GS); o alfa sai As / 128,
// como o dos outros desenhos (a mistura do ALPHA_1 pelo blend()).
const char kFxShader[] = R"HLSL(
cbuffer FxFrame : register(b0) { row_major float4x4 view_proj; float4 fx_flags; };
struct FxIn { float4 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };
struct FxOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };
FxOut vs_fx(FxIn i) {
    FxOut o;
    o.pos = fx_flags.x > 0.5 ? mul(float4(i.pos.xyz, 1.0), view_proj) : i.pos;
    o.uv = i.uv;
    o.col = i.col * 255.0;
    return o;
}
Texture2D fx_tex : register(t0);
SamplerState fx_smp : register(s0);
float4 ps_fx(FxOut i) : SV_Target {
    float4 t = fx_flags.y > 0.5 ? fx_tex.Sample(fx_smp, i.uv) * 255.0 : float4(128.0, 128.0, 128.0, 128.0);
    float3 rgb = min(floor(t.rgb * i.col.rgb / 128.0), 255.0) / 255.0;
    float a = min(floor(t.a * i.col.a / 128.0), 255.0);
    return float4(rgb, min(a / 128.0, 1.0));
}
)HLSL";

struct FxGpuVertex {
    float pos[4];
    float uv[2];
    uint32_t colour;
};

struct UiVertex {
    float pos[2];
    float uv[2];
    float col[4];
    float tex;
};

void mat_identity(float m[16]) {
    std::memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}
void mat_mul(const float a[16], const float b[16], float o[16]) {  // o = a x b (linhas)
    float t[16];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) t[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c] + a[r * 4 + 3] * b[12 + c];
    std::memcpy(o, t, 64);
}

}  // namespace

struct DrvRenderer::Impl {
    DrvRenderOptions opt;
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID3D11Texture2D> color_ms, color, depth;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11VertexShader> vs, vs_bond, vs_bond_ext, vs_char;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout, layout_bond, layout_bond_ext, layout_char;
    ComPtr<ID3D11Buffer> cb_frame, cb_draw, cb_object, cb_char, cb_bones;
    ComPtr<ID3D11Buffer> quad_vb;  // os quadrados da cena (DrvQuad: 4 vértices BondVertexWork)
    // A interface (os DrvSprite): o shader, o buffer de vértices (cresce), a textura, a mistura e a profundidade.
    ComPtr<ID3D11VertexShader> vs_ui;
    ComPtr<ID3D11PixelShader> ps_ui;
    ComPtr<ID3D11InputLayout> layout_ui;
    ComPtr<ID3D11Buffer> ui_vb;
    UINT ui_vb_capacity = 0;
    ComPtr<ID3D11ShaderResourceView> ui_tex;
    ComPtr<ID3D11BlendState> ui_blend;
    ComPtr<ID3D11DepthStencilState> ui_depth;
    void render_sprites(const DrvScene& scene, DrvRenderStats& st);
    // Os efeitos (DrvFxBatch).
    ComPtr<ID3D11VertexShader> vs_fx;
    ComPtr<ID3D11PixelShader> ps_fx;
    ComPtr<ID3D11InputLayout> layout_fx;
    ComPtr<ID3D11Buffer> fx_vb, cb_fx;
    UINT fx_vb_capacity = 0;
    std::map<int, ComPtr<ID3D11ShaderResourceView>> fx_textures;
    void render_fx(const DrvScene& scene, DrvRenderStats& st);
    // Um modelo enviado: os buffers das duas famílias, as texturas e os desenhos de cada peça.
    struct ModelGpu {
        ComPtr<ID3D11Buffer> vb, ib, bvb, bvb_ext, bib;  // bvb: pos, stq, cor (44 bytes); bvb_ext: stq1, normal
        ComPtr<ID3D11Buffer> cvb, cib;                   // a família de pele (CharGpuVertex)
        std::vector<ComPtr<ID3D11ShaderResourceView>> textures;
        std::vector<std::vector<size_t>> by_part;
        float center[3] = {0, 0, 0}, radius = 1;
    };
    std::map<const CarModel*, ModelGpu> models;
    std::map<uint32_t, ComPtr<ID3D11SamplerState>> samplers;
    std::map<uint64_t, ComPtr<ID3D11BlendState>> blends;
    std::map<uint32_t, ComPtr<ID3D11DepthStencilState>> depths;
    ComPtr<ID3D11RasterizerState> raster;
    int width = 0, height = 0, samples = 1;
    bool windowed = false;

    bool make_targets(std::string& error);
    ID3D11SamplerState* sampler(uint64_t clamp);
    ID3D11BlendState* blend(uint64_t alpha, bool abe, int mask);
    ID3D11DepthStencilState* depth_state(uint64_t test, bool zwrite);
    void render(const DrvScene& scene, DrvRenderStats& stats);
};

DrvRenderer::DrvRenderer() : d_(new Impl) {}
DrvRenderer::~DrvRenderer() { delete d_; }
int DrvRenderer::width() const { return d_->width; }
int DrvRenderer::height() const { return d_->height; }

void drv_mat_mul(const float a[16], const float b[16], float o[16]) { mat_mul(a, b, o); }

void drv_look_at(const float eye[3], const float at[3], float view[16]) {
    float z[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    const float zl = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
    for (float& v : z) v /= zl > 0 ? zl : 1.0f;
    float x[3] = {z[2], 0.0f, -z[0]};  // (0, 1, 0) x z
    const float xl = std::sqrt(x[0] * x[0] + x[2] * x[2]);
    if (xl > 1e-6f) {
        x[0] /= xl;
        x[2] /= xl;
    } else {
        x[0] = 1.0f;
        x[2] = 0.0f;
    }
    const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
    const float m[16] = {x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0, 0, 0, 0, 1};
    std::memcpy(view, m, 64);
    for (int c = 0; c < 3; ++c) {
        const float* ax = c == 0 ? x : c == 1 ? y : z;
        view[12 + c] = -(ax[0] * eye[0] + ax[1] * eye[1] + ax[2] * eye[2]);
    }
}

void drv_perspective(float fov_y_deg, float aspect, float zn, float zf, float proj[16]) {
    const float ys = 1.0f / nf::fm::tan(fov_y_deg * 3.14159265f / 360.0f), xs = ys / aspect;
    const float p[16] = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, zf / (zf - zn), 1, 0, 0, -zn * zf / (zf - zn), 0};
    std::memcpy(proj, p, 64);
}

bool DrvRenderer::Impl::make_targets(std::string& error) {
    color_ms.Reset();
    color.Reset();
    depth.Reset();
    rtv.Reset();
    dsv.Reset();
    samples = std::max(1, opt.msaa);
    UINT q = 0;
    while (samples > 1 && (FAILED(dev->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, UINT(samples), &q)) || !q))
        samples /= 2;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(width);
    td.Height = UINT(height);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (windowed) {
        if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&color)))) {
            error = "GetBuffer";
            return false;
        }
    } else if (FAILED(dev->CreateTexture2D(&td, nullptr, &color))) {
        error = "CreateTexture2D (cor)";
        return false;
    }
    td.SampleDesc.Count = UINT(samples);
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (samples > 1 && FAILED(dev->CreateTexture2D(&td, nullptr, &color_ms))) {
        error = "CreateTexture2D (MSAA)";
        return false;
    }
    if (FAILED(dev->CreateRenderTargetView(samples > 1 ? color_ms.Get() : color.Get(), nullptr, &rtv))) {
        error = "CreateRenderTargetView";
        return false;
    }
    td.Format = DXGI_FORMAT_D32_FLOAT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &depth)) || FAILED(dev->CreateDepthStencilView(depth.Get(), nullptr, &dsv))) {
        error = "profundidade";
        return false;
    }
    return true;
}

bool DrvRenderer::init(void* window, const DrvRenderOptions& opt, std::string& error) {
    const HWND hwnd = static_cast<HWND>(window);
    Impl& I = *d_;
    I.opt = opt;
    I.width = opt.width;
    I.height = opt.height;
    I.windowed = hwnd != nullptr;
    ComPtr<IDXGIAdapter1> adapter;
    if (!opt.adapter.empty()) {
        nf::AdapterPreference pref;
        if (nf::parse_adapter_preference(opt.adapter, &pref)) {
            const auto list = nf::enumerate_adapters(true);
            const int k = nf::choose_adapter(list, pref);
            if (k >= 0) {
                adapter = nf::open_adapter(list[size_t(k)]);
                stats_.adapter = list[size_t(k)].name;
            }
        }
    }
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    if (FAILED(D3D11CreateDevice(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                 levels, 1, D3D11_SDK_VERSION, &I.dev, nullptr, &I.ctx))) {
        error = "D3D11CreateDevice";
        return false;
    }
    if (stats_.adapter.empty()) {
        ComPtr<IDXGIDevice> dxdev;
        ComPtr<IDXGIAdapter> a;
        DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(I.dev.As(&dxdev)) && SUCCEEDED(dxdev->GetAdapter(&a)) && SUCCEEDED(a->GetDesc(&desc))) {
            char name[128];
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof name, nullptr, nullptr);
            stats_.adapter = name;
        }
    }
    if (hwnd) {
        ComPtr<IDXGIDevice> dxdev;
        ComPtr<IDXGIAdapter> a;
        ComPtr<IDXGIFactory2> f;
        I.dev.As(&dxdev);
        dxdev->GetAdapter(&a);
        a->GetParent(IID_PPV_ARGS(&f));
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = UINT(I.width);
        sd.Height = UINT(I.height);
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (FAILED(f->CreateSwapChainForHwnd(I.dev.Get(), hwnd, &sd, nullptr, nullptr, &I.swap))) {
            error = "CreateSwapChainForHwnd";
            return false;
        }
    }
    if (!I.make_targets(error)) return false;

    ComPtr<ID3DBlob> vsb, vbb, vbe, vcb, psb, err;
    if (FAILED(D3DCompile(kShader, sizeof kShader - 1, "car.hlsl", nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &vsb, &err)) ||
        FAILED(D3DCompile(kShader, sizeof kShader - 1, "car.hlsl", nullptr, nullptr, "vs_bond", "vs_5_0", 0, 0, &vbb, &err)) ||
        FAILED(D3DCompile(kShader, sizeof kShader - 1, "car.hlsl", nullptr, nullptr, "vs_bond_ext", "vs_5_0", 0, 0, &vbe, &err)) ||
        FAILED(D3DCompile(kShader, sizeof kShader - 1, "car.hlsl", nullptr, nullptr, "vs_char", "vs_5_0", 0, 0, &vcb, &err)) ||
        FAILED(D3DCompile(kShader, sizeof kShader - 1, "car.hlsl", nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &psb, &err))) {
        error = err ? std::string(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize()) : "D3DCompile";
        return false;
    }
    I.dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &I.vs);
    I.dev->CreateVertexShader(vbb->GetBufferPointer(), vbb->GetBufferSize(), nullptr, &I.vs_bond);
    I.dev->CreateVertexShader(vbe->GetBufferPointer(), vbe->GetBufferSize(), nullptr, &I.vs_bond_ext);
    I.dev->CreateVertexShader(vcb->GetBufferPointer(), vcb->GetBufferSize(), nullptr, &I.vs_char);
    I.dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &I.ps);
    static_assert(offsetof(CarGpuVertex, stq) == 12 && offsetof(CarGpuVertex, normal) == 24 &&
                  offsetof(CarGpuVertex, dmgvec) == 36 && offsetof(CarGpuVertex, dmgnrm) == 52 &&
                  offsetof(CarGpuVertex, zone) == 64 && offsetof(CarGpuVertex, colour) == 68 &&
                  sizeof(CarGpuVertex) == 84);
    const D3D11_INPUT_ELEMENT_DESC el[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32_FLOAT, 0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 3, DXGI_FORMAT_R32_UINT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_UINT, 0, 68, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(I.dev->CreateInputLayout(el, 7, vsb->GetBufferPointer(), vsb->GetBufferSize(), &I.layout))) {
        error = "CreateInputLayout";
        return false;
    }
    static_assert(offsetof(BondVertexWork, stq) == 16 && offsetof(BondVertexWork, col) == 28 &&
                  offsetof(BondVertexWork, stq1) == 44 && offsetof(BondVertexWork, nrm) == 56 &&
                  sizeof(BondVertexWork) == 68);
    const D3D11_INPUT_ELEMENT_DESC eb[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(I.dev->CreateInputLayout(eb, 5, vbe->GetBufferPointer(), vbe->GetBufferSize(), &I.layout_bond_ext))) {
        error = "CreateInputLayout (mundo, o segundo fluxo)";
        return false;
    }
    if (FAILED(I.dev->CreateInputLayout(eb, 3, vbb->GetBufferPointer(), vbb->GetBufferSize(), &I.layout_bond))) {
        error = "CreateInputLayout (mundo)";
        return false;
    }
    static_assert(offsetof(CharGpuVertex, normal) == 12 && offsetof(CharGpuVertex, st) == 24 &&
                  offsetof(CharGpuVertex, bone) == 32 && offsetof(CharGpuVertex, weight) == 48);
    const D3D11_INPUT_ELEMENT_DESC ec[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_SINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(I.dev->CreateInputLayout(ec, 5, vcb->GetBufferPointer(), vcb->GetBufferSize(), &I.layout_char))) {
        error = "CreateInputLayout (personagens)";
        return false;
    }
    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.ByteWidth = (sizeof(FrameCB) + 15) & ~15u;
    I.dev->CreateBuffer(&bd, nullptr, &I.cb_frame);
    bd.ByteWidth = (sizeof(DrawCB) + 15) & ~15u;
    I.dev->CreateBuffer(&bd, nullptr, &I.cb_draw);
    bd.ByteWidth = (sizeof(ObjectCB) + 15) & ~15u;
    I.dev->CreateBuffer(&bd, nullptr, &I.cb_object);
    bd.ByteWidth = (sizeof(CharCB) + 15) & ~15u;
    I.dev->CreateBuffer(&bd, nullptr, &I.cb_char);
    bd.ByteWidth = (sizeof(BonesCB) + 15) & ~15u;
    I.dev->CreateBuffer(&bd, nullptr, &I.cb_bones);
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;  // o GS não descarta faces
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = I.samples > 1;
    I.dev->CreateRasterizerState(&rd, &I.raster);
    // A interface.
    ComPtr<ID3DBlob> vub, pub;
    if (FAILED(D3DCompile(kUiShader, sizeof kUiShader - 1, "ui.hlsl", nullptr, nullptr, "vs_ui", "vs_5_0", 0, 0, &vub, &err)) ||
        FAILED(D3DCompile(kUiShader, sizeof kUiShader - 1, "ui.hlsl", nullptr, nullptr, "ps_ui", "ps_5_0", 0, 0, &pub, &err))) {
        error = err ? std::string(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize()) : "D3DCompile (ui)";
        return false;
    }
    I.dev->CreateVertexShader(vub->GetBufferPointer(), vub->GetBufferSize(), nullptr, &I.vs_ui);
    I.dev->CreatePixelShader(pub->GetBufferPointer(), pub->GetBufferSize(), nullptr, &I.ps_ui);
    static_assert(offsetof(UiVertex, uv) == 8 && offsetof(UiVertex, col) == 16 && offsetof(UiVertex, tex) == 32 &&
                  sizeof(UiVertex) == 36);
    const D3D11_INPUT_ELEMENT_DESC eu[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(I.dev->CreateInputLayout(eu, 4, vub->GetBufferPointer(), vub->GetBufferSize(), &I.layout_ui))) {
        error = "CreateInputLayout (ui)";
        return false;
    }
    D3D11_BLEND_DESC ub{};
    ub.RenderTarget[0].BlendEnable = TRUE;
    ub.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    ub.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    ub.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    ub.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    ub.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    ub.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    ub.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    I.dev->CreateBlendState(&ub, &I.ui_blend);
    D3D11_DEPTH_STENCIL_DESC ud{};
    ud.DepthEnable = FALSE;
    ud.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    ud.DepthFunc = D3D11_COMPARISON_ALWAYS;
    I.dev->CreateDepthStencilState(&ud, &I.ui_depth);
    // Os efeitos.
    ComPtr<ID3DBlob> vfb, pfb;
    if (FAILED(D3DCompile(kFxShader, sizeof kFxShader - 1, "fx.hlsl", nullptr, nullptr, "vs_fx", "vs_5_0", 0, 0, &vfb, &err)) ||
        FAILED(D3DCompile(kFxShader, sizeof kFxShader - 1, "fx.hlsl", nullptr, nullptr, "ps_fx", "ps_5_0", 0, 0, &pfb, &err))) {
        error = err ? std::string(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize()) : "D3DCompile (fx)";
        return false;
    }
    I.dev->CreateVertexShader(vfb->GetBufferPointer(), vfb->GetBufferSize(), nullptr, &I.vs_fx);
    I.dev->CreatePixelShader(pfb->GetBufferPointer(), pfb->GetBufferSize(), nullptr, &I.ps_fx);
    static_assert(offsetof(FxGpuVertex, uv) == 16 && offsetof(FxGpuVertex, colour) == 24 && sizeof(FxGpuVertex) == 28);
    const D3D11_INPUT_ELEMENT_DESC ef[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(I.dev->CreateInputLayout(ef, 3, vfb->GetBufferPointer(), vfb->GetBufferSize(), &I.layout_fx))) {
        error = "CreateInputLayout (fx)";
        return false;
    }
    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = 80;
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    I.dev->CreateBuffer(&cbd, nullptr, &I.cb_fx);
    return true;
}

bool DrvRenderer::set_fx_texture(int slot, int width, int height, const uint8_t* rgba) {
    Impl& I = *d_;
    I.fx_textures.erase(slot);
    if (width <= 0 || height <= 0 || !rgba) return false;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(width);
    td.Height = UINT(height);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{rgba, UINT(width * 4), 0};
    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(I.dev->CreateTexture2D(&td, &sd, &tex))) return false;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(I.dev->CreateShaderResourceView(tex.Get(), nullptr, &srv))) return false;
    I.fx_textures[slot] = srv;
    return true;
}

// Os DrvFxBatch na ordem: um desenho por lote, com a mistura do ALPHA_1 (o ABE ligado) e a profundidade do TEST_1.
void DrvRenderer::Impl::render_fx(const DrvScene& scene, DrvRenderStats& st) {
    if (scene.fx.empty()) return;
    size_t total = 0;
    for (const DrvFxBatch& b : scene.fx) total += b.verts.size();
    if (total == 0) return;
    const UINT bytes = UINT(total * sizeof(FxGpuVertex));
    if (bytes > fx_vb_capacity) {
        fx_vb.Reset();
        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bd.ByteWidth = std::max<UINT>(bytes, UINT(8192 * sizeof(FxGpuVertex)));
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &fx_vb))) return;
        fx_vb_capacity = bd.ByteWidth;
    }
    D3D11_MAPPED_SUBRESOURCE map;
    if (FAILED(ctx->Map(fx_vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return;
    FxGpuVertex* out = static_cast<FxGpuVertex*>(map.pData);
    for (const DrvFxBatch& b : scene.fx)
        for (const DrvFxVertex& v : b.verts) {
            std::memcpy(out->pos, v.pos, 16);
            std::memcpy(out->uv, v.uv, 8);
            out->colour = v.colour;
            ++out;
        }
    ctx->Unmap(fx_vb.Get(), 0);
    float vp[16];
    mat_mul(scene.view, scene.proj, vp);
    ID3D11Buffer* vb = fx_vb.Get();
    UINT stride = sizeof(FxGpuVertex), offset = 0;
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx->IASetInputLayout(layout_fx.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs_fx.Get(), nullptr, 0);
    ctx->PSSetShader(ps_fx.Get(), nullptr, 0);
    ID3D11Buffer* cb = cb_fx.Get();
    ctx->VSSetConstantBuffers(0, 1, &cb);
    ctx->PSSetConstantBuffers(0, 1, &cb);
    ID3D11SamplerState* smp = sampler(0x5);  // CLAMP nos dois eixos
    ctx->PSSetSamplers(0, 1, &smp);
    UINT first = 0;
    for (const DrvFxBatch& b : scene.fx) {
        const UINT n = UINT(b.verts.size());
        if (n == 0) continue;
        auto it = fx_textures.find(b.texture);
        ID3D11ShaderResourceView* srv = it != fx_textures.end() ? it->second.Get() : nullptr;
        float c[20];
        std::memcpy(c, vp, 64);
        c[16] = b.world ? 1.0f : 0.0f;
        c[17] = srv ? 1.0f : 0.0f;
        c[18] = c[19] = 0.0f;
        D3D11_MAPPED_SUBRESOURCE m2;
        if (SUCCEEDED(ctx->Map(cb_fx.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m2))) {
            std::memcpy(m2.pData, c, sizeof c);
            ctx->Unmap(cb_fx.Get(), 0);
        }
        ctx->PSSetShaderResources(0, 1, &srv);
        const float fix = float((b.alpha >> 32) & 0xFF) / 128.0f;
        const float bf[4] = {fix, fix, fix, fix};
        ctx->OMSetBlendState(blend(b.alpha, true, D3D11_COLOR_WRITE_ENABLE_ALL), bf, 0xFFFFFFFF);
        ctx->OMSetDepthStencilState(depth_state(b.test, b.zwrite), 0);
        ctx->Draw(n, first);
        first += n;
        ++st.draw_calls;
        ++st.fx_calls;
        st.fx_triangles += int(n / 3);
    }
    ctx->PSSetShader(ps.Get(), nullptr, 0);
}

bool DrvRenderer::set_ui_texture(int width, int height, const uint8_t* rgba) {
    Impl& I = *d_;
    I.ui_tex.Reset();
    if (width <= 0 || height <= 0 || !rgba) return false;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(width);
    td.Height = UINT(height);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{rgba, UINT(width * 4), 0};
    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(I.dev->CreateTexture2D(&td, &sd, &tex))) return false;
    return SUCCEEDED(I.dev->CreateShaderResourceView(tex.Get(), nullptr, &I.ui_tex));
}

// Os DrvSprite: dois triângulos cada, num só desenho (a textura da interface ligada; os sem textura não a leem).
void DrvRenderer::Impl::render_sprites(const DrvScene& scene, DrvRenderStats& st) {
    if (scene.sprites.empty()) return;
    std::vector<UiVertex> v;
    v.reserve(scene.sprites.size() * 6);
    const float sx = 2.0f / float(std::max(1, width)), sy = 2.0f / float(std::max(1, height));
    for (const DrvSprite& s : scene.sprites) {
        float col[4];
        for (int c = 0; c < 4; ++c) col[c] = float((s.colour >> (8 * c)) & 0xFF) / 255.0f;
        const float tex = s.textured && ui_tex ? 1.0f : 0.0f;
        const float x0 = s.x0 * sx - 1.0f, x1 = s.x1 * sx - 1.0f, y0 = 1.0f - s.y0 * sy, y1 = 1.0f - s.y1 * sy;
        const UiVertex a{{x0, y0}, {s.u0, s.v0}, {col[0], col[1], col[2], col[3]}, tex};
        const UiVertex b{{x1, y0}, {s.u1, s.v0}, {col[0], col[1], col[2], col[3]}, tex};
        const UiVertex c{{x0, y1}, {s.u0, s.v1}, {col[0], col[1], col[2], col[3]}, tex};
        const UiVertex d{{x1, y1}, {s.u1, s.v1}, {col[0], col[1], col[2], col[3]}, tex};
        v.push_back(a);
        v.push_back(b);
        v.push_back(c);
        v.push_back(c);
        v.push_back(b);
        v.push_back(d);
    }
    const UINT bytes = UINT(v.size() * sizeof(UiVertex));
    if (bytes > ui_vb_capacity) {
        ui_vb.Reset();
        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bd.ByteWidth = std::max<UINT>(bytes, UINT(4096 * sizeof(UiVertex)));
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &ui_vb))) return;
        ui_vb_capacity = bd.ByteWidth;
    }
    D3D11_MAPPED_SUBRESOURCE map;
    if (FAILED(ctx->Map(ui_vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return;
    std::memcpy(map.pData, v.data(), bytes);
    ctx->Unmap(ui_vb.Get(), 0);
    ID3D11Buffer* vb = ui_vb.Get();
    UINT stride = sizeof(UiVertex), offset = 0;
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx->IASetInputLayout(layout_ui.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs_ui.Get(), nullptr, 0);
    ctx->PSSetShader(ps_ui.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srv = ui_tex.Get();
    ctx->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* smp = sampler(0x5);  // CLAMP nos dois eixos
    ctx->PSSetSamplers(0, 1, &smp);
    const float bf[4] = {1, 1, 1, 1};
    ctx->OMSetBlendState(ui_blend.Get(), bf, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(ui_depth.Get(), 0);
    ctx->Draw(UINT(v.size()), 0);
    ++st.draw_calls;
    st.sprites += int(scene.sprites.size());
    ctx->PSSetShader(ps.Get(), nullptr, 0);
}

bool DrvRenderer::upload(const CarModel& car, std::string& error) {
    Impl& I = *d_;
    if (car.indices().empty() && car.bond_indices().empty() && car.char_indices().empty()) {
        error = "modelo sem vértices";
        return false;
    }
    Impl::ModelGpu& g = I.models[&car];
    g = Impl::ModelGpu();
    auto make = [&](const void* data, size_t bytes, UINT bind, ComPtr<ID3D11Buffer>& out) {
        if (!bytes) return true;
        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = bind;
        bd.ByteWidth = UINT(bytes);
        D3D11_SUBRESOURCE_DATA sd{data, 0, 0};
        return SUCCEEDED(I.dev->CreateBuffer(&bd, &sd, &out));
    };
    // A MatBond em dois fluxos: o que todo desenho lê (posição, STQ, cor: 44 bytes) e o do Dynamic e dos Scroll2.
    const std::vector<BondVertexWork>& bv = car.bond_vertices();
    std::vector<float> base(bv.size() * 11), ext(bv.size() * 6);
    for (size_t k = 0; k < bv.size(); ++k) {
        std::memcpy(&base[k * 11], &bv[k], 44);
        std::memcpy(&ext[k * 6], bv[k].stq1, 12);
        std::memcpy(&ext[k * 6 + 3], bv[k].nrm, 12);
    }
    if (!make(car.vertices().data(), car.vertices().size() * sizeof(CarGpuVertex), D3D11_BIND_VERTEX_BUFFER, g.vb) ||
        !make(car.indices().data(), car.indices().size() * 4, D3D11_BIND_INDEX_BUFFER, g.ib) ||
        !make(base.data(), base.size() * 4, D3D11_BIND_VERTEX_BUFFER, g.bvb) ||
        !make(ext.data(), ext.size() * 4, D3D11_BIND_VERTEX_BUFFER, g.bvb_ext) ||
        !make(car.bond_indices().data(), car.bond_indices().size() * 4, D3D11_BIND_INDEX_BUFFER, g.bib) ||
        !make(car.char_vertices().data(), car.char_vertices().size() * sizeof(CharGpuVertex), D3D11_BIND_VERTEX_BUFFER,
              g.cvb) ||
        !make(car.char_indices().data(), car.char_indices().size() * 4, D3D11_BIND_INDEX_BUFFER, g.cib)) {
        error = "vertex/index buffer";
        return false;
    }
    for (const CarTexture& t : car.textures()) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = UINT(t.width);
        td.Height = UINT(t.height);
        td.MipLevels = 0;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        ComPtr<ID3D11Texture2D> tex;
        ComPtr<ID3D11ShaderResourceView> srv;
        if (SUCCEEDED(I.dev->CreateTexture2D(&td, nullptr, &tex))) {
            I.ctx->UpdateSubresource(tex.Get(), 0, nullptr, t.rgba.data(), UINT(t.width * 4), 0);
            I.dev->CreateShaderResourceView(tex.Get(), nullptr, &srv);
            I.ctx->GenerateMips(srv.Get());
        }
        g.textures.push_back(srv);
    }
    g.by_part.assign(car.parts().size(), {});
    for (size_t i = 0; i < car.draws().size(); ++i)
        if (car.draws()[i].part >= 0 && size_t(car.draws()[i].part) < g.by_part.size())
            g.by_part[size_t(car.draws()[i].part)].push_back(i);
    float lo[3], hi[3];
    car.bounds(unsigned(std::max(0, I.opt.view)), lo, hi);
    for (int c = 0; c < 3; ++c) g.center[c] = 0.5f * (lo[c] + hi[c]);
    const float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
    g.radius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
    return true;
}

void DrvRenderer::resize(int width, int height) {
    Impl& I = *d_;
    if (width <= 0 || height <= 0 || !I.swap) return;
    I.width = width;
    I.height = height;
    I.ctx->OMSetRenderTargets(0, nullptr, nullptr);
    I.color_ms.Reset();
    I.color.Reset();
    I.rtv.Reset();
    I.swap->ResizeBuffers(0, UINT(width), UINT(height), DXGI_FORMAT_UNKNOWN, 0);
    std::string err;
    I.make_targets(err);
}

ID3D11SamplerState* DrvRenderer::Impl::sampler(uint64_t clamp) {
    const uint32_t key = uint32_t(clamp & 0xF);
    auto it = samplers.find(key);
    if (it != samplers.end()) return it->second.Get();
    auto mode = [](uint64_t w) { return (w & 1) ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP; };
    D3D11_SAMPLER_DESC d{};
    d.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    d.AddressU = mode(clamp & 3);
    d.AddressV = mode((clamp >> 2) & 3);
    d.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    d.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> s;
    dev->CreateSamplerState(&d, &s);
    samplers[key] = s;
    return s.Get();
}

// O ALPHA_1 do GS ((A - B) x C + D; A, B, D: 0 Cs, 1 Cd, 2 zero; C: 0 As, 1 Ad, 2 FIX) como mistura do Direct3D.
ID3D11BlendState* DrvRenderer::Impl::blend(uint64_t alpha, bool abe, int mask) {
    const uint64_t key = (abe ? alpha : 0xFFFFFFFFFFull) ^ (uint64_t(mask) << 48);
    auto it = blends.find(key);
    if (it != blends.end()) return it->second.Get();
    D3D11_BLEND_DESC d{};
    D3D11_RENDER_TARGET_BLEND_DESC& r = d.RenderTarget[0];
    r.RenderTargetWriteMask = UINT8(mask);
    if (abe) {
        const int A = int(alpha & 3), B = int((alpha >> 2) & 3), C = int((alpha >> 4) & 3), D = int((alpha >> 6) & 3);
        const D3D11_BLEND cf = C == 0 ? D3D11_BLEND_SRC_ALPHA : C == 1 ? D3D11_BLEND_DEST_ALPHA : D3D11_BLEND_BLEND_FACTOR;
        const D3D11_BLEND icf = C == 0 ? D3D11_BLEND_INV_SRC_ALPHA : C == 1 ? D3D11_BLEND_INV_DEST_ALPHA : D3D11_BLEND_INV_BLEND_FACTOR;
        // (A - B) C + D = Cs Ks + Cd Kd, com Ks = ([A=Cs] - [B=Cs]) C + [D=Cs] e Kd igual para Cd. Cada coeficiente é
        // 0, 1, C, 1 - C ou -C (o -C vira uma subtração); 1 + C (A = D) não existe no Direct3D e fica 1.
        enum Coef { kZero, kOne, kC, kInvC, kNegC };
        auto coef = [](int cterm, int one) {
            if (cterm == 0) return one ? kOne : kZero;
            if (cterm > 0) return one ? kOne : kC;
            return one ? kInvC : kNegC;
        };
        const Coef ks = coef((A == 0) - (B == 0), D == 0);
        const Coef kd = coef((A == 1) - (B == 1), D == 1);
        auto factor = [&](Coef k) {
            return k == kOne ? D3D11_BLEND_ONE : k == kC || k == kNegC ? cf : k == kInvC ? icf : D3D11_BLEND_ZERO;
        };
        r.BlendEnable = TRUE;
        r.SrcBlend = factor(ks);
        r.DestBlend = factor(kd);
        r.BlendOp = ks == kNegC ? D3D11_BLEND_OP_REV_SUBTRACT : kd == kNegC ? D3D11_BLEND_OP_SUBTRACT : D3D11_BLEND_OP_ADD;
        // O alfa do quadro recebe o As (o GS grava As no alfa do quadro).
        r.SrcBlendAlpha = D3D11_BLEND_ONE;
        r.DestBlendAlpha = D3D11_BLEND_ZERO;
        r.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    } else {
        r.BlendEnable = FALSE;
    }
    ComPtr<ID3D11BlendState> s;
    dev->CreateBlendState(&d, &s);
    blends[key] = s;
    return s.Get();
}

// O TEST_1 (ZTE, ZTST) e o ZBUF (ZMSK) como profundidade do Direct3D (o Z do GS cresce para perto).
ID3D11DepthStencilState* DrvRenderer::Impl::depth_state(uint64_t test, bool zwrite) {
    const bool zte = (test >> 16) & 1;
    const int ztst = int(test >> 17) & 3;
    const uint32_t key = uint32_t(zte) | uint32_t(ztst) << 1 | uint32_t(zwrite) << 3;
    auto it = depths.find(key);
    if (it != depths.end()) return it->second.Get();
    D3D11_DEPTH_STENCIL_DESC d{};
    d.DepthEnable = TRUE;
    d.DepthWriteMask = zwrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    static const D3D11_COMPARISON_FUNC f[4] = {D3D11_COMPARISON_NEVER, D3D11_COMPARISON_ALWAYS, D3D11_COMPARISON_LESS_EQUAL,
                                               D3D11_COMPARISON_LESS};
    d.DepthFunc = zte ? f[ztst] : D3D11_COMPARISON_ALWAYS;
    ComPtr<ID3D11DepthStencilState> s;
    dev->CreateDepthStencilState(&d, &s);
    depths[key] = s;
    return s.Get();
}

void DrvRenderer::Impl::render(const DrvScene& scene, DrvRenderStats& st) {
    ctx->ClearRenderTargetView(rtv.Get(), scene.clear);
    ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    D3D11_VIEWPORT vp{0, 0, float(width), float(height), 0, 1};
    ctx->RSSetViewports(1, &vp);
    ID3D11RenderTargetView* r = rtv.Get();
    ctx->OMSetRenderTargets(1, &r, dsv.Get());
    ctx->RSSetState(raster.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->PSSetShader(ps.Get(), nullptr, 0);
    ID3D11Buffer* cbs[5] = {cb_frame.Get(), cb_draw.Get(), cb_object.Get(), cb_char.Get(), cb_bones.Get()};
    ctx->VSSetConstantBuffers(0, 5, cbs);
    ctx->PSSetConstantBuffers(0, 3, cbs);
    const float* bound_bones = reinterpret_cast<const float*>(1);  // nenhuma ainda (nulo é a pose de ligação)
    int bound_bone_count = -1;

    FrameCB f{};
    mat_mul(scene.view, scene.proj, f.view_proj);
    const CarConstants& k = scene.k;
    std::memcpy(f.point_pos, k.point_pos, sizeof f.point_pos);
    std::memcpy(f.point_col, k.point_col, sizeof f.point_col);
    std::memcpy(f.fog, k.fog, 16);
    std::memcpy(f.light_dir_t, k.light_dir_t, sizeof f.light_dir_t);
    std::memcpy(f.light_col, k.light_col, sizeof f.light_col);
    std::memcpy(f.ambient, k.ambient, 16);
    if (scene.spec_from_view) {
        // O SpecularMat: a rotação da câmera (as normais na vista; ver car_model.h).
        for (int rr = 0; rr < 4; ++rr)
            for (int c = 0; c < 4; ++c) f.spec_mat[rr][c] = (rr < 3 && c < 3) ? scene.view[rr * 4 + c] : (rr == c ? 1.0f : 0.0f);
    } else {
        std::memcpy(f.spec_mat, k.spec_mat, sizeof f.spec_mat);
    }
    std::memcpy(f.refl, k.refl_info2, 16);
    std::memcpy(f.damage, k.damage, sizeof f.damage);
    for (int c = 0; c < 3; ++c) f.fog_color[c] = scene.fog_color[c];
    std::memcpy(f.camera_pos, scene.eye, 16);
    std::memcpy(f.view, scene.view, 64);
    std::memcpy(f.dyn_lights, scene.light_sources, sizeof f.dyn_lights);
    D3D11_MAPPED_SUBRESOURCE map;
    ctx->Map(cb_frame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
    std::memcpy(map.pData, &f, sizeof f);
    ctx->Unmap(cb_frame.Get(), 0);

    const float blend_factor[4] = {1, 1, 1, 1};
    int bound_family = -1;
    const ModelGpu* bound_model = nullptr;
    for (const DrvItem& item : scene.items) {
        if (item.quad >= 0) {
            // Um quadrado (a sombra simples): a faixa de 4 vértices pelo vs_bond sem névoa nem luzes pontuais, a textura
            // do modelo em MODULATE, a mistura do ALPHA_1 com o FIX, o Z testado (GEQUAL) sem gravar.
            if (size_t(item.quad) >= scene.quads.size()) continue;
            const DrvQuad& q = scene.quads[size_t(item.quad)];
            auto qm = models.find(q.model);
            if (qm == models.end()) continue;
            const ModelGpu& g = qm->second;
            BondVertexWork v[4] = {};
            for (int vi = 0; vi < 4; ++vi) {
                for (int c = 0; c < 3; ++c) v[vi].pos[c] = q.pos[vi][c];
                v[vi].pos[3] = 1.0f;
                v[vi].stq[0] = q.uv[vi][0];
                v[vi].stq[1] = q.uv[vi][1];
                v[vi].stq[2] = 1.0f;
                for (int c = 0; c < 4; ++c) v[vi].col[c] = float((q.colour >> (8 * c)) & 0xFF);
            }
            if (!quad_vb) {
                D3D11_BUFFER_DESC bd{};
                bd.Usage = D3D11_USAGE_DYNAMIC;
                bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                bd.ByteWidth = sizeof v;
                dev->CreateBuffer(&bd, nullptr, &quad_vb);
            }
            ctx->Map(quad_vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
            std::memcpy(map.pData, v, sizeof v);
            ctx->Unmap(quad_vb.Get(), 0);
            ObjectCB ob{};
            mat_identity(ob.world);
            ob.flags[0] = 1 | 2;
            ctx->Map(cb_object.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
            std::memcpy(map.pData, &ob, sizeof ob);
            ctx->Unmap(cb_object.Get(), 0);
            ID3D11Buffer* vb = quad_vb.Get();
            UINT stride = sizeof(BondVertexWork), offset = 0;
            ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            ctx->IASetInputLayout(layout_bond.Get());
            ctx->VSSetShader(vs_bond.Get(), nullptr, 0);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            bound_family = -1;
            bound_model = nullptr;
            DrawCB c{};
            const bool tex = q.texture >= 0 && size_t(q.texture) < g.textures.size() && g.textures[size_t(q.texture)];
            c.mode[1] = tex ? 1 : 0;
            ID3D11ShaderResourceView* srv = tex ? g.textures[size_t(q.texture)].Get() : nullptr;
            ctx->PSSetShaderResources(0, 1, &srv);
            ID3D11SamplerState* smp = sampler(0x5);  // CLAMP nos dois eixos (o 'cshd' do RShadowMap)
            ctx->PSSetSamplers(0, 1, &smp);
            ctx->Map(cb_draw.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
            std::memcpy(map.pData, &c, sizeof c);
            ctx->Unmap(cb_draw.Get(), 0);
            const float fix = float((q.alpha >> 32) & 0xFF) / 128.0f;
            const float bf[4] = {fix, fix, fix, fix};
            ctx->OMSetBlendState(blend(q.alpha, true, D3D11_COLOR_WRITE_ENABLE_ALL), bf, 0xFFFFFFFF);
            ctx->OMSetDepthStencilState(depth_state(0x50000, false), 0);
            ctx->Draw(4, 0);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ++st.draw_calls;
            ++st.quads;
            continue;
        }
        auto mit = models.find(item.model);
        if (mit == models.end()) continue;
        const ModelGpu& g = mit->second;
        if (item.part < 0 || size_t(item.part) >= g.by_part.size()) continue;
        if (opt.only_part >= 0 && item.part != opt.only_part) continue;
        {
            // O dano de cada carro (os carros do tráfego e o do jogador num mesmo quadro).
            const float(*want)[4] = item.damage ? item.damage : k.damage;
            if (std::memcmp(want, f.damage, sizeof f.damage) != 0) {
                std::memcpy(f.damage, want, sizeof f.damage);
                ctx->Map(cb_frame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
                std::memcpy(map.pData, &f, sizeof f);
                ctx->Unmap(cb_frame.Get(), 0);
            }
        }
        ObjectCB ob{};
        std::memcpy(ob.world, item.world, 64);
        ob.flags[0] = ((item.flags & DrvItem::kNoFog) ? 1 : 0) | ((item.flags & DrvItem::kNoPointLights) ? 2 : 0);
        ctx->Map(cb_object.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
        std::memcpy(map.pData, &ob, sizeof ob);
        ctx->Unmap(cb_object.Get(), 0);
        if (item.bones != bound_bones || item.bone_count != bound_bone_count) {
            // O EAGLAnimationBuffer do personagem (identidades sem ele, como na carga).
            static BonesCB bc;
            for (int bi = 0; bi < kCarAnimBufferBones; ++bi) {
                if (item.bones && bi < item.bone_count)
                    std::memcpy(bc.bones[bi], item.bones + 16 * bi, 64);
                else
                    mat_identity(bc.bones[bi]);
            }
            ctx->Map(cb_bones.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
            std::memcpy(map.pData, &bc, sizeof bc);
            ctx->Unmap(cb_bones.Get(), 0);
            bound_bones = item.bones;
            bound_bone_count = item.bone_count;
        }
        ++st.instances;
        for (size_t di : g.by_part[size_t(item.part)]) {
            const CarDraw& dr = item.model->draws()[di];
            if (dr.fallback && !opt.fallback) continue;
            // 0 a MatCar, 1 a MatBond, 2 a MatBond com o segundo fluxo (o Dynamic e os Scroll2).
            const int binding = dr.family == 2   ? 3
                                : dr.family == 1 ? ((dr.bond_kind == kBondDynamic || dr.bond_kind == kBondScroll2) ? 2 : 1)
                                                 : 0;
            if (bound_family != binding || bound_model != &g) {
                // Os buffers e o shader da família (MatCar, MatBond ou a pele dos personagens) do modelo.
                if (binding == 3) {
                    ID3D11Buffer* cvb = g.cvb.Get();
                    UINT stride = UINT(sizeof(CharGpuVertex)), offset = 0;
                    ctx->IASetVertexBuffers(0, 1, &cvb, &stride, &offset);
                    ctx->IASetIndexBuffer(g.cib.Get(), DXGI_FORMAT_R32_UINT, 0);
                    ctx->IASetInputLayout(layout_char.Get());
                    ctx->VSSetShader(vs_char.Get(), nullptr, 0);
                } else {
                    ID3D11Buffer* vbs[2] = {binding ? g.bvb.Get() : g.vb.Get(), g.bvb_ext.Get()};
                    UINT strides[2] = {binding ? 44u : UINT(sizeof(CarGpuVertex)), 24u}, offsets[2] = {0, 0};
                    ctx->IASetVertexBuffers(0, binding == 2 ? 2 : 1, vbs, strides, offsets);
                    ctx->IASetIndexBuffer(binding ? g.bib.Get() : g.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
                    ctx->IASetInputLayout(binding == 2 ? layout_bond_ext.Get()
                                          : binding == 1 ? layout_bond.Get()
                                                         : layout.Get());
                    ctx->VSSetShader(binding == 2 ? vs_bond_ext.Get() : binding == 1 ? vs_bond.Get() : vs.Get(), nullptr,
                                     0);
                }
                bound_family = binding;
                bound_model = &g;
            }
            if (dr.family == 2) {
                CharCB cc{};
                std::memcpy(cc.light, dr.char_lights, sizeof cc.light);
                std::memcpy(cc.refl, dr.char_refl, 16);
                for (int c = 0; c < 4; ++c) cc.opt[c] = float(int32_t(dr.char_options[c] & 0xFFFF));
                cc.opt[3] = float(int32_t(dr.char_options[3]));
                ctx->Map(cb_char.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
                std::memcpy(map.pData, &cc, sizeof cc);
                ctx->Unmap(cb_char.Get(), 0);
            }
            // As passadas na ordem do microprograma: MatCar base, especular, reflexo (o vidro: DmgTexture, Texture,
            // especular, reflexo); MatBond o ambiente (bit 1, pass[1]) e depois a textura (bit 0, pass[0]).
            int order[kCarMaxPasses] = {0, 1, 2, 3};
            int norder = kCarMaxPasses;
            if (dr.family == 1) {
                // O Reflect chuta o ambiente antes da textura; os Scroll2 a textura e depois o Texture1.
                order[0] = dr.bond_kind == kBondReflect ? 1 : 0;
                order[1] = dr.bond_kind == kBondReflect ? 0 : 1;
                norder = 2;
            }
            // O passo 3b do lote neste quadro (a aritmética da VU, mat_bond.h).
            float bond_off[4] = {0, 0, 0, 0};
            if (dr.family == 1 && dr.bond_kind == kBondScroll2) {
                bond_scroll_offset(dr.scroll[0], scene.sim_step[0], bond_off);
                bond_scroll_offset(dr.scroll[1], scene.sim_step[0], bond_off + 2);
            } else if (dr.family == 1 && dr.bond_kind == kBondFrameAnim) {
                bond_off[1] = bond_frame_offset(dr.frame_anim, scene.sim_step);
            }
            const bool glass = dr.layout && dr.layout->kind == kCarGlass;
            for (int oi = 0; oi < norder; ++oi) {
                const int p = order[oi];
                if (!((dr.passes >> p) & 1)) continue;
                const int stq_kind = dr.family == 2 ? p : dr.layout ? car_pass_stq(*dr.layout, p) : p;
                if (dr.family == 0 && stq_kind == 1 && !opt.spec) continue;
                if (dr.family == 0 && stq_kind == 2 && !opt.refl) continue;
                if (dr.family == 1 && p == 1 && dr.bond_kind == kBondReflect && !opt.refl) continue;
                if (dr.family == 2 && ((p == 1 && !opt.spec) || (p == 2 && !opt.refl))) continue;
                const eagl::GsRegs& gr = dr.pass[p];
                const uint64_t prim = gr.has_prim ? gr.prim : 0x18;
                const bool tme = (prim >> 4) & 1, fge = (prim >> 5) & 1, abe = (prim >> 6) & 1;
                const int tex = dr.texture[p];
                DrawCB c{};
                std::memcpy(c.props, dr.props, 16);
                c.mode[0] = dr.family == 1 ? 0 : stq_kind;
                if (dr.family == 2) ++st.char_calls;
                c.mode2[0] = dr.layout ? car_pass_alpha(*dr.layout, p) : 0;
                c.mode2[1] = glass ? 1 : 0;
                c.mode2[2] = dr.layout && dr.layout->kind == kCarSwitch ? 1 : 0;
                c.mode2[3] = dr.layout && dr.layout->kind == kCarGouraud ? 1 : 0;
                std::memcpy(c.sw, dr.switch_data, 16);
                if (item.switch_data && dr.switch_index >= 0) std::memcpy(c.sw, item.switch_data[dr.switch_index], 16);
                std::memcpy(c.swp, dr.switch_param, 16);
                if (dr.family == 1) {
                    c.mode2[3] = dr.bond_kind;
                    std::memcpy(c.bond_off, bond_off, 16);
                }
                c.mode[1] = (tme && tex >= 0 && tex < int(g.textures.size()) && g.textures[size_t(tex)]) ? 1 : 0;
                c.mode[2] = fge ? 1 : 0;
                c.mode[3] = dr.family == 1 ? p : (dr.fallback ? 1 : 0);
                c.gs[0] = int((gr.tex0 >> 35) & 3);
                c.gs[1] = int((gr.tex0 >> 34) & 1);
                const uint64_t test = gr.has_test ? gr.test : 0x50000;
                const bool ate = test & 1;
                const int atst = int(test >> 1) & 7;
                const int afail = int(test >> 12) & 3;
                c.gs[3] = atst;
                c.gs2[0] = float((test >> 4) & 0xFF);
                const bool zmsk = gr.has_zbuf && ((gr.zbuf >> 32) & 1);
                ID3D11ShaderResourceView* srv = c.mode[1] ? g.textures[size_t(tex)].Get() : nullptr;
                ctx->PSSetShaderResources(0, 1, &srv);
                ID3D11SamplerState* s = sampler(gr.has_clamp ? gr.clamp : 0);
                ctx->PSSetSamplers(0, 1, &s);
                // Sem teste de alfa: um desenho. Com teste: quem passa grava tudo; quem falha segue o AFAIL (0 nada, 1 só
                // o quadro, 2 só o Z, 3 só o RGB).
                const int sub_count = ate && afail != 0 ? 2 : 1;
                for (int sub = 0; sub < sub_count; ++sub) {
                    c.gs[2] = !ate ? 0 : sub == 0 ? 1 : 2;
                    int mask = D3D11_COLOR_WRITE_ENABLE_ALL;
                    // O céu sem gravar Z: RSky::Draw (0x001ece18) chama EAGL::RenderContext::SetZWritesEnable(false)
                    // (0x002913f8) antes das instâncias e (true) depois.
                    bool zwrite = !zmsk && !(item.flags & DrvItem::kSky);
                    if (sub == 1) {
                        if (afail == 1) zwrite = false;
                        else if (afail == 2) mask = 0;
                        else if (afail == 3) {
                            mask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
                            zwrite = false;
                        }
                    }
                    ctx->Map(cb_draw.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
                    std::memcpy(map.pData, &c, sizeof c);
                    ctx->Unmap(cb_draw.Get(), 0);
                    const float fix = float((gr.alpha >> 32) & 0xFF) / 128.0f;
                    const float bf[4] = {fix, fix, fix, fix};
                    ctx->OMSetBlendState(blend(gr.alpha, abe, mask), gr.has_alpha ? bf : blend_factor, 0xFFFFFFFF);
                    ctx->OMSetDepthStencilState(depth_state(test, zwrite), 0);
                    ctx->DrawIndexed(dr.index_count, dr.first_index, 0);
                    ++st.draw_calls;
                    if (dr.family == 1) ++st.bond_calls;
                }
                if (dr.family == 0) ++st.pass_calls[p];
                if (dr.fallback) ++st.fallback_calls;
            }
        }
    }
    render_fx(scene, st);
    render_sprites(scene, st);
    if (samples > 1) ctx->ResolveSubresource(color.Get(), 0, color_ms.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
}

void DrvRenderer::draw_scene(const DrvScene& scene) {
    const std::string adapter_name = stats_.adapter;
    stats_ = DrvRenderStats();
    stats_.adapter = adapter_name;
    d_->render(scene, stats_);
}

void DrvRenderer::draw(const CarModel& car, float yaw, float pitch, float dist) {
    Impl& I = *d_;
    auto mit = I.models.find(&car);
    if (mit == I.models.end()) return;
    const Impl::ModelGpu& g = mit->second;
    DrvScene scene;
    scene.clear[0] = 0.18f;
    scene.clear[1] = 0.20f;
    scene.clear[2] = 0.23f;
    scene.clear[3] = 1.0f;
    // A matriz do carro: gira em torno do eixo vertical (y) pelo centro da caixa.
    float t[16], rot[16], world[16];
    mat_identity(t);
    t[12] = -g.center[0];
    t[13] = -g.center[1];
    t[14] = -g.center[2];
    mat_identity(rot);
    rot[0] = nf::fm::cos(yaw);
    rot[2] = -nf::fm::sin(yaw);
    rot[8] = nf::fm::sin(yaw);
    rot[10] = nf::fm::cos(yaw);
    mat_mul(t, rot, world);
    // A câmera: olha a origem de uma distância em função do raio, inclinada de `pitch` (mão esquerda, y para cima).
    const float d = g.radius * 2.2f * dist;
    const float eye[3] = {0.0f, d * nf::fm::sin(pitch), -d * nf::fm::cos(pitch)};
    const float at[3] = {0.0f, 0.0f, 0.0f};
    drv_look_at(eye, at, scene.view);
    const float zn = std::max(0.01f, d - g.radius * 1.5f), zf = d + g.radius * 1.5f;
    drv_perspective(I.opt.fov_deg, float(I.width) / float(I.height), zn, zf, scene.proj);
    scene.k = car.constants();
    scene.fog_color[0] = scene.fog_color[1] = scene.fog_color[2] = 128.0f;
    // As peças nas instâncias do objeto articulado (gpModelMatrix = instância x carro), na ordem do CARP; sem
    // instâncias, cada peça uma vez com a matriz do carro.
    if (car.instances().empty()) {
        for (size_t p = 0; p < g.by_part.size(); ++p) {
            DrvItem it;
            it.model = &car;
            it.part = int(p);
            std::memcpy(it.world, world, 64);
            scene.items.push_back(it);
        }
    } else {
        // A lista da vista (RVehicle::SetViewDrawList: a 1 é a de fora); sem ela, todas as instâncias.
        std::vector<uint16_t> list = I.opt.view > 0 ? car.view_draw_list(unsigned(I.opt.view)) : std::vector<uint16_t>();
        if (list.empty())
            for (size_t i = 0; i < car.instances().size(); ++i) list.push_back(uint16_t(i));
        for (uint16_t ii : list) {
            if (ii >= car.instances().size()) continue;
            const CarInstance& inst = car.instances()[ii];
            if (inst.part < 0) continue;
            DrvItem it;
            it.model = &car;
            it.part = inst.part;
            mat_mul(inst.matrix, world, it.world);
            scene.items.push_back(it);
        }
    }
    draw_scene(scene);
}

void DrvRenderer::present() {
    if (d_->swap) d_->swap->Present(1, 0);
}

void DrvRenderer::finish() {
    Impl& I = *d_;
    D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0};
    ComPtr<ID3D11Query> q;
    if (FAILED(I.dev->CreateQuery(&qd, &q))) return;
    I.ctx->End(q.Get());
    BOOL done = FALSE;
    while (I.ctx->GetData(q.Get(), &done, sizeof done, 0) == S_FALSE) {
    }
}

bool DrvRenderer::save_png(const std::wstring& path) {
    Impl& I = *d_;
    D3D11_TEXTURE2D_DESC td{};
    I.color->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> st;
    if (FAILED(I.dev->CreateTexture2D(&td, nullptr, &st))) return false;
    I.ctx->CopyResource(st.Get(), I.color.Get());
    D3D11_MAPPED_SUBRESOURCE map;
    if (FAILED(I.ctx->Map(st.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;
    std::vector<uint8_t> px(size_t(td.Width) * td.Height * 4);
    for (UINT y = 0; y < td.Height; ++y) {
        const uint8_t* src = static_cast<const uint8_t*>(map.pData) + size_t(y) * map.RowPitch;
        for (UINT x = 0; x < td.Width; ++x) {
            std::memcpy(&px[(size_t(y) * td.Width + x) * 4], src + x * 4, 3);
            px[(size_t(y) * td.Width + x) * 4 + 3] = 255;  // o alfa do quadro é o As do GS, não opacidade
        }
    }
    I.ctx->Unmap(st.Get(), 0);
    // Cria as pastas do caminho.
    for (size_t pos = path.find_first_of(L"\\/", 3); pos != std::wstring::npos; pos = path.find_first_of(L"\\/", pos + 1))
        CreateDirectoryW(path.substr(0, pos).c_str(), nullptr);
    return nf::write_png_rgba(path, int(td.Width), int(td.Height), px.data());  // platform/image_io.h (o WIC)
}

}  // namespace drv
