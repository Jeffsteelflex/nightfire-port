#include "render/renderer.h"

#include <d3d11_3.h>
#include <d3dcompiler.h>
#include <dxgi1_5.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "platform/vu1.h"
#include "render/gpu_adapters.h"
#include "render/movie_layer.h"
#include "render/ui_hd.h"
#include "render/ui_hd_draw.h"
#include "render/ui_modern_draw.h"
#include "ui/modern_ui.h"

namespace nf {

namespace {

void check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s falhou (HRESULT 0x%08lX)", what, static_cast<unsigned long>(hr));
        throw std::runtime_error(buf);
    }
}

const char kShader[] = R"(
cbuffer Frame : register(b0) {
    row_major float4x4 view_proj;
    float3 light_dir;   // só no sombreamento de depuração (tecla V)
    float ambient;
    float debug_shading;
    float lights_on;    // 0: as rotinas de luz da VU1 viram as sem luz (--no-lights)
    float2 pad;
};
cbuffer Object : register(b1) {
    row_major float4x4 world;
    float4 tint;            // TAmbient: tweak / 255 (r, g, b, a); com luzes, o termo ambiente
    float4 light_pos[2];    // luzes da VU1 no espaço dos vértices (rígida) ou no mundo (skinning)
    float4 light_col[2];    // TColor0/1: r, g, b (0..255 x intensidade), w = 1 / raio²
    float4 env_s;           // linhas 0 e 1 da câmera (mapa de ambiente, rotina 0x2e0)
    float4 env_t;
    float num_lights;
    float env;              // 1: rotina 0x2e0 (ST pela normal, sem luzes)
    float premult;          // 1: a cor sai multiplicada por As (o blend state usa ONE no lugar de As)
    float sprite;           // 1: sprite de partículas (cor na escala do GS, sem rotina da VU1)
    float atst;             // teste de alfa do GS (ATST 0..7); -1: sem teste
    float aref;             // AREF, na escala do GS (As = 1.0 vale 128)
    float aint;             // 1: compara o alfa inteiro, como o GS; 0: o valor contínuo (regra antiga)
    float screen;           // 1: 2D, a posição já está na tela (x, y em -1..1)
};
struct VSIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv : TEXCOORD;
    float4 col : COLOR;
};
struct PSIn {
    float4 pos : SV_Position;
    float3 nrm : NORMAL;
    float2 uv : TEXCOORD;
    float4 col : COLOR;   // cor do vértice depois da VU1, com 0x80 = 1.0
};
// Cor do vértice na escala do GS (0..255, 0x80 = 1.0) depois da rotina da VU1, igual a
// vu1::tweak_vertex e vu1::light_vertex (platform/vu1.cpp): sem luzes (0x25e, 0x2a3, 0x2e0) a cor vezes
// TAmbient; com luzes (0x340), TAmbient * cor + TColor1 * s1 + TColor0 * s0, saturado em 255, com
// s = max(0, max(0, 1 / |D|² - 1 / raio²) * (n . D)) e D = luz - p. FTOI0 no fim (trunca).
// n: normal / 128, como a rotina a usa; p: no espaço das luzes.
float4 vu1_colour(float4 c, float3 n, float3 p) {
    float4 o = tint * c;
    if (num_lights > 0.5 && env < 0.5 && lights_on > 0.5) {
        float3 d0 = light_pos[0].xyz - p;
        float3 d1 = light_pos[1].xyz - p;
        float s0 = max(0.0, max(0.0, 1.0 / dot(d0, d0) - light_col[0].w) * dot(n, d0));
        float s1 = max(0.0, max(0.0, 1.0 / dot(d1, d1) - light_col[1].w) * dot(n, d1));
        o.rgb = min(o.rgb + light_col[1].rgb * s1 + light_col[0].rgb * s0, 255.0);
        o.a = min(o.a, 255.0);
    }
    return floor(o);
}
// Rotina 0x2e0: ST = 0.5 + (n . linha da câmera) / 256, com n inteiro (vu1::env_st).
float2 vu1_env_st(float3 nvu) {
    return 0.5 + float2(dot(nvu, env_s.xyz), dot(nvu, env_t.xyz)) / 256.0;
}
PSIn vs_main(VSIn i) {
    PSIn o;
    float4 w = mul(float4(i.pos, 1.0), world);
    o.pos = mul(w, view_proj);
    o.nrm = mul(i.nrm, (float3x3)world);
    o.uv = i.uv;
    float4 c = round(i.col * 255.0);  // escala do GS, como no disco
    if (screen > 0.5) {
        o.pos = float4(i.pos.xy, 0.0, 1.0);
        o.nrm = float3(0.0, 0.0, 1.0);
        o.col = c / 128.0;
        return o;
    }
    if (sprite > 0.5) {
        o.col = c / 128.0;
        return o;
    }
    // Malha rígida: a normal guardada é o inteiro do UNPACK / 128.
    if (env > 0.5) o.uv = vu1_env_st(i.nrm * 128.0);
    o.col = vu1_colour(c, i.nrm, i.pos) / 128.0;
    return o;
}
// Skinning (o SkinIt da VU0): a posição na pose de repouso vai ao mundo por dois ossos da paleta,
// w * (p * M0) + (1 - w) * (p * M1), igual a nf::skin_vertex (src/data/map_item.cpp); a normal da VU1
// é a do disco * 128 girada só pelo primeiro osso e truncada (FTOI0), como no microprograma da VU0.
cbuffer Skin : register(b2) {
    row_major float4x4 bones[128];  // kMaxSkinBones
};
struct VSSkinIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv : TEXCOORD;
    float4 col : COLOR;
    uint4 bone : BLENDINDICES;
    float weight : BLENDWEIGHT;
};
PSIn vs_skin(VSSkinIn i) {
    PSIn o;
    float4 p = float4(i.pos, 1.0);
    float4x4 m0 = bones[i.bone.x];
    float4x4 m1 = bones[i.bone.y];
    float3 sp = i.weight * mul(p, m0).xyz + (1.0 - i.weight) * mul(p, m1).xyz;
    float3 sn = i.weight * mul(i.nrm, (float3x3)m0) + (1.0 - i.weight) * mul(i.nrm, (float3x3)m1);
    float4 w = mul(float4(sp, 1.0), world);
    o.pos = mul(w, view_proj);
    o.nrm = mul(sn, (float3x3)world);
    o.uv = i.uv;
    float3 nvu = trunc(mul(i.nrm * 128.0, (float3x3)m0));
    if (env > 0.5) o.uv = vu1_env_st(nvu);
    o.col = vu1_colour(round(i.col * 255.0), nvu / 128.0, sp) / 128.0;
    return o;
}
// Rostos (o SkinIt antes do skinning, docs/formats.md "Morphs do rosto"): num vértice com índice de morph m >= 0,
// pos += soma em k de peso[k] * delta[m + k], os alvos em ordem (os pesos 0, que o jogo não escolheu, não mudam
// nada), igual a nf::morph_vertex; depois, o skinning de vs_skin. A normal não muda (nem no PS2).
cbuffer Morph : register(b4) {
    float4 morph_w[22];   // kMaxMorphTargets; o peso do alvo k em morph_w[k].x
    uint morph_n;         // alvos (22 no disco)
    uint3 morph_pad;
};
StructuredBuffer<float3> morph_delta : register(t3);
struct VSSkinMorphIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv : TEXCOORD;
    float4 col : COLOR;
    uint4 bone : BLENDINDICES;
    float weight : BLENDWEIGHT;
    int morph : MORPHINDEX;
};
PSIn vs_skin_morph(VSSkinMorphIn i) {
    PSIn o;
    float3 mp = i.pos;
    if (i.morph >= 0) {
        for (uint k = 0; k < morph_n; ++k) mp += morph_delta[uint(i.morph) + k] * morph_w[k].x;
    }
    float4 p = float4(mp, 1.0);
    float4x4 m0 = bones[i.bone.x];
    float4x4 m1 = bones[i.bone.y];
    float3 sp = i.weight * mul(p, m0).xyz + (1.0 - i.weight) * mul(p, m1).xyz;
    float3 sn = i.weight * mul(i.nrm, (float3x3)m0) + (1.0 - i.weight) * mul(i.nrm, (float3x3)m1);
    float4 w = mul(float4(sp, 1.0), world);
    o.pos = mul(w, view_proj);
    o.nrm = mul(sn, (float3x3)world);
    o.uv = i.uv;
    float3 nvu = trunc(mul(i.nrm * 128.0, (float3x3)m0));
    if (env > 0.5) o.uv = vu1_env_st(nvu);
    o.col = vu1_colour(round(i.col * 255.0), nvu / 128.0, sp) / 128.0;
    return o;
}
Texture2D tex : register(t0);
SamplerState smp : register(s0);
// Teste de alfa do GS (TEST_1.ATST) com o alfa a e a referência na escala do GS.
bool alpha_test(float a) {
    if (aint > 0.5) a = floor(a + 0.001);
    int f = (int)atst;
    if (f == 0) return false;
    if (f == 2) return a < aref;
    if (f == 3) return a <= aref;
    if (f == 4) return a == aref;
    if (f == 5) return a >= aref;
    if (f == 6) return a > aref;
    if (f == 7) return a != aref;
    return true;
}
// MODULATE do GS (TEX0.TFX = 0, TCC = 1) com a cor do vértice: Cf = Ct * Cv / 128 (saturada) e
// Af = At * Av / 128 (até 0xff); As = Af / 128. O alfa da textura está na escala do GS.
float4 ps_main(PSIn i) : SV_Target {
    float4 c = tex.Sample(smp, i.uv);
    float a = min(c.a * (255.0 / 128.0) * i.col.a, 255.0 / 128.0);
    float3 lit = saturate(c.rgb * i.col.rgb);
    if (debug_shading > 0.5 && sprite < 0.5) {
        // Sombreamento de depuração (só do PC): luz direcional fixa, sem as cores dos vértices; as
        // faces são de dois lados, como no PS2, então ilumina pelo módulo do cosseno.
        float ndl = abs(dot(normalize(i.nrm), -normalize(light_dir)));
        lit = c.rgb * (ambient + (1.0 - ambient) * ndl) * tint.rgb;
    }
    if (atst >= 0.0 && !alpha_test(a * 128.0)) discard;
    if (premult > 0.5) lit *= a;
    // O alfa vai para o alvo em todos os desenhos, como no GS (fator Ad das misturas seguintes).
    return float4(lit, saturate(a));
}
// Só do PC, com MSAA: os desenhos opacos recortados pelo teste de alfa (gs_alpha_coverage_candidate) usam o alfa para a
// cobertura (AlphaToCoverageEnable) em vez de descartar o pixel inteiro: o alfa sai afiado em volta do limiar do teste,
// de 0 a 1 em um pixel (a derivada de tela do alfa), então a borda fica onde o teste a põe em 1x, com as amostras do
// MSAA suavizando o degrau. A cor é a de ps_main; o alfa gravado no alvo é a cobertura (1 dentro).
float4 ps_a2c(PSIn i) : SV_Target {
    float4 c = tex.Sample(smp, i.uv);
    float a = min(c.a * (255.0 / 128.0) * i.col.a, 255.0 / 128.0);
    float3 lit = saturate(c.rgb * i.col.rgb);
    if (debug_shading > 0.5 && sprite < 0.5) {
        float ndl = abs(dot(normalize(i.nrm), -normalize(light_dir)));
        lit = c.rgb * (ambient + (1.0 - ambient) * ndl) * tint.rgb;
    }
    // O menor alfa que passa no teste, na escala do GS: GEQUAL aref; GREATER aref + 1 no teste inteiro do GS.
    float a128 = a * 128.0;
    float t = aref;
    if (aint > 0.5) t += ((int)atst == 6 ? 1.0 : 0.0) - 0.001;
    float w = max(fwidth(a128), 0.001);
    // A rampa centrada no limiar (metade da cobertura onde o alfa é o limiar). Com o limiar no topo do alfa (o 0x7f
    // das folhas: só passa o alfa cheio), o alfa nunca passa do limiar e a rampa centrada daria metade da cobertura
    // a todo pixel de uma folha fina (pontilhada): aí todo pixel que passa em 1x fica inteiro e a rampa vai para fora.
    float bias = t > 127.0 ? 1.0 : 0.5;
    float cov = saturate((a128 - t) / w + bias);
    if (cov <= 0.0) discard;
    return float4(lit, cov);
}
// Passada de névoa (psiDrawFogPass): o Z do GS de cada pixel (A / z + B, módulo 2^24; 1 onde nada
// escreveu Z, como o CreateZClearSprite) dá Ad = bits 8-15; onde Z < 0x7fff o quadro vira
// (Cd - névoa) * Ad / 128 + névoa. Igual a vu1::gs_z e vu1::fog_keep.
cbuffer Fog : register(b3) {
    float4 fog_color;   // r, g, b (0..1)
    float fog_a;        // A e B do Z do GS (vu1::screen_z)
    float fog_b;
    float depth_nf;     // perto * longe da projeção do PC
    float depth_fmn;    // longe - perto
    float depth_n;      // perto
    float3 pad2;
};
Texture2D<float> depth_tex : register(t1);
Texture2DMS<float> depth_ms : register(t2);  // só do PC: com MSAA, a profundidade de cada amostra
float4 vs_fog(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 fog_of(float d) {
    float z = 1.0;
    if (d > 0.0) {
        float view_z = depth_nf / (d * depth_fmn + depth_n);  // profundidade invertida
        float s = fog_a / view_z + fog_b;
        z = s > 0.0 ? fmod(floor(s), 16777216.0) : 0.0;
    }
    float keep = z >= 32767.0 ? 1.0 : floor(z / 256.0) / 128.0;
    return float4(fog_color.rgb, keep);
}
float4 ps_fog(float4 pos : SV_Position) : SV_Target {
    return fog_of(depth_tex.Load(int3(pos.xy, 0)));
}
// Com MSAA (só do PC): roda por amostra e mistura a névoa de cada uma pela profundidade dela.
float4 ps_fog_ms(float4 pos : SV_Position, uint s : SV_SampleIndex) : SV_Target {
    return fog_of(depth_ms.Load(int2(pos.xy), s));
}
)";

// Cópia de uma textura para outra do mesmo tamanho (BC7 -> RGBA8 antes de gerar os mipmaps).
const char kBlitShader[] = R"(
float4 vs_blit(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}
Texture2D src : register(t0);
float4 ps_blit(float4 pos : SV_Position) : SV_Target {
    return src.Load(int3(pos.xy, 0));
}
// Só do PC: a cena (em escala) reduzida para a janela. O pixel da janela cobre `ratio` texels da cena;
// taps x taps amostras bilineares espalhadas por essa área dão a média (em 200%, os 4 texels exatos). Com
// ratio < 1 (cena menor que a janela), uma amostra bilinear.
cbuffer Down : register(b4) {
    float2 src_size;
    float2 ratio;
    float taps;
    float3 pad_down;
};
SamplerState smp_down : register(s0);
float4 ps_down(float4 pos : SV_Position) : SV_Target {
    float2 start = (pos.xy - 0.5) * ratio;
    float2 step = ratio / taps;
    int n = (int)taps;
    float4 sum = 0;
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            float2 t = start + (float2(i, j) + 0.5) * step;
            sum += src.SampleLevel(smp_down, t / src_size, 0);
        }
    }
    return sum / (taps * taps);
}
// Só do PC: o fundo desfocado dos menus Modern. Uma gaussiana de 9 amostras em 5 leituras bilineares, na
// direção blur_dir (em texels da fonte, já com o espaçamento), e a mistura final com a imagem nítida.
cbuffer Blur : register(b5) {
    float2 blur_texel;
    float2 blur_dir;
    float blur_amount;
    float3 pad_blur;
};
Texture2D blur_src : register(t1);
float4 ps_blur(float4 pos : SV_Position) : SV_Target {
    float2 uv = pos.xy * blur_texel;
    float2 d = blur_dir * blur_texel;
    float4 c = src.SampleLevel(smp_down, uv, 0) * 0.2270270270;
    c += src.SampleLevel(smp_down, uv + d * 1.3846153846, 0) * 0.3162162162;
    c += src.SampleLevel(smp_down, uv - d * 1.3846153846, 0) * 0.3162162162;
    c += src.SampleLevel(smp_down, uv + d * 3.2307692308, 0) * 0.0702702703;
    c += src.SampleLevel(smp_down, uv - d * 3.2307692308, 0) * 0.0702702703;
    return c;
}
float4 ps_blur_mix(float4 pos : SV_Position) : SV_Target {
    float4 sharp = src.Load(int3(pos.xy, 0));
    float4 soft = blur_src.SampleLevel(smp_down, pos.xy * blur_texel, 0);
    return lerp(sharp, soft, blur_amount);
}
)";

struct BlurCB {
    float texel[2];
    float dir[2];
    float amount;
    float pad[3];
};
static_assert(sizeof(BlurCB) == 32);

struct DownCB {
    float src_size[2];
    float ratio[2];
    float taps;
    float pad[3];
};
static_assert(sizeof(DownCB) == 32);


struct FrameCB {
    float view_proj[16];
    float light_dir[3];
    float ambient;
    float debug_shading;
    float lights_on;
    float pad[2];
};

struct ObjectCB {
    float world[16];
    float tint[4];
    float light_pos[2][4];
    float light_col[2][4];
    float env_s[4];
    float env_t[4];
    float num_lights;
    float env;
    float premult;
    float sprite;
    float atst;
    float aref;
    float aint;
    float screen;
};
static_assert(sizeof(ObjectCB) == 208);

struct FogCB {
    float color[4];
    float a, b;
    float depth_nf, depth_fmn, depth_n;
    float pad[3];
};
static_assert(sizeof(FogCB) == 48);

// Estado efetivo de um desenho: a equação do GS, o teste de alfa e o de Z.
struct DrawState {
    uint64_t alpha = 0;
    bool abe = true;
    float atst = -1.0f;  // ATST, -1 sem teste
    float aref = 0.0f;
    bool aint = true;
    D3D11_COMPARISON_FUNC zfunc = D3D11_COMPARISON_GREATER_EQUAL;
    bool zwrite = true;
};

// Junta o estado da parte da malha (own), o do desenho (item) e, para o que ninguém escreveu, a regra
// antiga pelo modo de psiSetUpColourBlend: opaco testa alfa em 50% e escreve Z; misturado descarta só
// alfa < 0.004 e não escreve Z.
DrawState resolve_state(const GsState& own, const DrawItem& item) {
    DrawState s;
    const bool opaque = item.blend == Blend::Opaque;
    s.alpha = (own.has & GsState::kAlpha) ? own.alpha
                                           : ((item.gs.has & GsState::kAlpha) ? item.gs.alpha : gs_alpha_of_blend(item.blend));
    s.abe = own.abe != 0;
    const bool has_test = ((own.has | item.gs.has) & GsState::kTest) != 0;
    if (has_test) {
        const uint64_t test = (own.has & GsState::kTest) ? own.test : item.gs.test;
        if (test & 1) {  // ATE
            s.atst = float((test >> 1) & 7);
            s.aref = float((test >> 4) & 0xff);
        }
        // AFAIL (bits 12-13) é 0 (KEEP) em todos os dados: o pixel reprovado some por inteiro.
        if (((test >> 16) & 1) == 0) {
            s.zfunc = D3D11_COMPARISON_ALWAYS;  // ZTE desligado
        } else {
            switch ((test >> 17) & 3) {
                case 0: s.zfunc = D3D11_COMPARISON_NEVER; break;
                case 1: s.zfunc = D3D11_COMPARISON_ALWAYS; break;
                case 2: s.zfunc = D3D11_COMPARISON_GREATER_EQUAL; break;  // Z invertido, como no GS
                default: s.zfunc = D3D11_COMPARISON_GREATER; break;
            }
        }
    } else {
        s.atst = 5.0f;  // GEQUAL, na escala contínua
        s.aref = opaque ? 64.0f : 0.004f * 128.0f;
        s.aint = false;
    }
    const bool has_zbuf = ((own.has | item.gs.has) & GsState::kZbuf) != 0;
    if (has_zbuf) {
        const uint64_t zbuf = (own.has & GsState::kZbuf) ? own.zbuf : item.gs.zbuf;
        s.zwrite = ((zbuf >> 32) & 1) == 0;
    } else {
        s.zwrite = opaque;
    }
    return s;
}

static_assert(kMaxSkinBones == 128, "o tamanho de bones[] no shader acompanha kMaxSkinBones");
struct SkinCB {
    float bones[kMaxSkinBones][16];
};
static_assert(kMaxMorphTargets == 22, "o tamanho de morph_w[] no shader acompanha kMaxMorphTargets");
struct MorphCB {
    float w[kMaxMorphTargets][4];  // o peso em x
    uint32_t n;
    uint32_t pad[3];
};


Microsoft::WRL::ComPtr<ID3DBlob> compile(const char* entry, const char* target, const char* source = kShader,
                                         size_t size = sizeof(kShader) - 1) {
    Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(source, size, "level.hlsl", nullptr, nullptr, entry, target,
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr)) {
        std::string msg = "erro no shader: ";
        if (errors) msg += static_cast<const char*>(errors->GetBufferPointer());
        throw std::runtime_error(msg);
    }
    return code;
}

}  // namespace

Renderer::Renderer(HWND hwnd, int width, int height, bool vsync, bool offscreen, IDXGIAdapter1* chosen)
    : hwnd_(hwnd), width_(width), height_(height), vsync_(vsync), offscreen_(offscreen) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    // BGRA: o Direct2D da camada 2D em alta definição usa o mesmo dispositivo. Sem adaptador escolhido, o padrão do
    // sistema (como sempre); com um ([video] adapter, --adapter), o dispositivo nasce nele.
    check(D3D11CreateDevice(chosen, chosen != nullptr ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                            D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1, D3D11_SDK_VERSION, &device_, nullptr, &ctx_),
          "D3D11CreateDevice");

    Com<IDXGIDevice> dxgi_device;
    Com<IDXGIAdapter> adapter;
    Com<IDXGIFactory2> factory;
    check(device_.As(&dxgi_device), "IDXGIDevice");
    check(dxgi_device->GetAdapter(&adapter), "GetAdapter");
    check(adapter->GetParent(IID_PPV_ARGS(&factory)), "IDXGIFactory2");
    // O adaptador em uso (o relatório, a opção dos menus, o --bench): integrado pela memória unificada do Direct3D.
    {
        Com<IDXGIAdapter1> a1;
        if (SUCCEEDED(adapter.As(&a1))) adapter_ = adapter_info(a1.Get(), -1);
        D3D11_FEATURE_DATA_D3D11_OPTIONS2 o2{};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &o2, sizeof(o2)))) {
            adapter_.integrated = o2.UnifiedMemoryArchitecture != FALSE;
            adapter_.uma_known = true;
        }
        // O índice na enumeração do DXGI (o do --list-adapters).
        Com<IDXGIFactory1> f1;
        if (SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&f1)))) {
            for (UINT i = 0;; ++i) {
                Com<IDXGIAdapter1> ad;
                if (FAILED(f1->EnumAdapters1(i, &ad))) break;
                DXGI_ADAPTER_DESC1 d{};
                if (SUCCEEDED(ad->GetDesc1(&d)) && d.AdapterLuid.LowPart == adapter_.luid_low &&
                    d.AdapterLuid.HighPart == adapter_.luid_high) {
                    adapter_.index = int(i);
                    break;
                }
            }
        }
    }

    DXGI_SWAP_CHAIN_DESC1 sd{};
    RECT client{};
    GetClientRect(hwnd, &client);
    // Fora da tela, a janela só mostra o que já tinha: o back buffer fica do tamanho dela.
    sd.Width = offscreen ? UINT(std::max<LONG>(1, client.right - client.left)) : UINT(width);
    sd.Height = offscreen ? UINT(std::max<LONG>(1, client.bottom - client.top)) : UINT(height);
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    // Só do PC: sem VSync, o Present em janela (composta pelo DWM) ainda esperaria a atualização do monitor; com
    // ALLOW_TEARING (Windows 10 1709+, quando o DXGI diz que há suporte) ele não espera, como o VSync desligado promete.
    {
        Com<IDXGIFactory5> f5;
        BOOL tearing = FALSE;
        if (SUCCEEDED(factory.As(&f5)) &&
            SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) && tearing) {
            tearing_ = true;
            swap_flags_ |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        }
    }
    sd.Flags = swap_flags_;
    check(factory->CreateSwapChainForHwnd(device_.Get(), hwnd, &sd, nullptr, nullptr, &swap_),
          "CreateSwapChainForHwnd");
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);  // Alt+Enter é tratado pela janela

    create_targets();
    create_pipeline();
}

Renderer::~Renderer() {
    // O DXGI pede que a cadeia saia da tela cheia exclusiva antes de ser liberada.
    if (swap_ != nullptr) swap_->SetFullscreenState(FALSE, nullptr);
}

void Renderer::set_movie_frame(const uint8_t* bgra, int width, int height, uint64_t serial) {
    if (movie_ == nullptr) {
        movie_ = std::make_unique<MovieLayer>();
        if (!movie_->init(device_.Get())) {
            movie_.reset();
            return;
        }
    }
    movie_->upload(ctx_.Get(), bgra, width, height, serial);
}

void Renderer::set_movie_background(bool on, const float rect[4]) {
    movie_on_ = on && movie_ != nullptr && movie_->srv() != nullptr;
    std::memcpy(movie_rect_, rect, sizeof(movie_rect_));
}

ID3D11ShaderResourceView* Renderer::movie_srv() const { return movie_ != nullptr ? movie_->srv() : nullptr; }

void Renderer::create_targets() {
    if (offscreen_) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = UINT(width_);
        td.Height = UINT(height_);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        check(device_->CreateTexture2D(&td, nullptr, &target_), "alvo fora da tela");
    } else {
        check(swap_->GetBuffer(0, IID_PPV_ARGS(&target_)), "GetBuffer");
    }
    check(device_->CreateRenderTargetView(target_.Get(), nullptr, &rtv_), "CreateRenderTargetView");
    if (ui_ != nullptr) ui_->set_target(target_.Get());
    if (modern_ != nullptr) modern_->set_target(target_.Get());
    create_scene_targets();
}

// A profundidade da cena e, com escala ou MSAA (só do PC), a cor própria dela. Sem memória ou sem suporte
// para o pedido, tenta sem MSAA e depois em 100% (o caminho de antes).
void Renderer::create_scene_targets() {
    if (try_scene_targets(scene_scale_, scene_msaa_)) return;
    if (scene_msaa_ > 1 && try_scene_targets(scene_scale_, 1)) return;
    if (!try_scene_targets(100, 1)) throw std::runtime_error("alvos da cena: sem memória para a profundidade");
}

bool Renderer::try_scene_targets(int scale_percent, int msaa) {
    scene_color_.Reset();
    scene_rtv_.Reset();
    scene_resolved_.Reset();
    scene_srv_.Reset();
    dsv_.Reset();
    depth_srv_.Reset();
    depth_.Reset();
    const int max_side = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    const int w = std::clamp(int(std::lround(double(width_) * scale_percent / 100.0)), 1, max_side);
    const int h = std::clamp(int(std::lround(double(height_) * scale_percent / 100.0)), 1, max_side);
    if (msaa > 1) {
        UINT q_color = 0, q_depth = 0;
        device_->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, UINT(msaa), &q_color);
        device_->CheckMultisampleQualityLevels(DXGI_FORMAT_D32_FLOAT, UINT(msaa), &q_depth);
        if (q_color == 0 || q_depth == 0) return false;
    }
    D3D11_TEXTURE2D_DESC dd{};
    dd.Width = UINT(w);
    dd.Height = UINT(h);
    dd.MipLevels = 1;
    dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_R32_TYPELESS;  // D32_FLOAT para o teste, R32_FLOAT para a névoa ler
    dd.SampleDesc.Count = UINT(msaa);
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&dd, nullptr, &depth_))) return false;
    D3D11_DEPTH_STENCIL_VIEW_DESC dvd{};
    dvd.Format = DXGI_FORMAT_D32_FLOAT;
    dvd.ViewDimension = msaa > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
    check(device_->CreateDepthStencilView(depth_.Get(), &dvd, &dsv_), "CreateDepthStencilView");
    D3D11_SHADER_RESOURCE_VIEW_DESC svd{};
    svd.Format = DXGI_FORMAT_R32_FLOAT;
    svd.ViewDimension = msaa > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
    svd.Texture2D.MipLevels = 1;
    check(device_->CreateShaderResourceView(depth_.Get(), &svd, &depth_srv_), "SRV da profundidade");
    const bool scaled = w != width_ || h != height_;
    if (msaa > 1 || scaled) {
        D3D11_TEXTURE2D_DESC cd{};
        cd.Width = UINT(w);
        cd.Height = UINT(h);
        cd.MipLevels = 1;
        cd.ArraySize = 1;
        cd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        cd.SampleDesc.Count = UINT(msaa);
        cd.BindFlags = D3D11_BIND_RENDER_TARGET | (msaa > 1 ? 0u : UINT(D3D11_BIND_SHADER_RESOURCE));
        if (FAILED(device_->CreateTexture2D(&cd, nullptr, &scene_color_)) ||
            FAILED(device_->CreateRenderTargetView(scene_color_.Get(), nullptr, &scene_rtv_))) {
            return false;
        }
        if (msaa == 1) {
            check(device_->CreateShaderResourceView(scene_color_.Get(), nullptr, &scene_srv_), "SRV da cena");
        } else if (scaled) {
            cd.SampleDesc.Count = 1;
            cd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(device_->CreateTexture2D(&cd, nullptr, &scene_resolved_))) return false;
            check(device_->CreateShaderResourceView(scene_resolved_.Get(), nullptr, &scene_srv_), "SRV da cena");
        }
    }
    scene_w_ = w;
    scene_h_ = h;
    scene_scale_ = scale_percent;
    scene_msaa_ = msaa;
    return true;
}

bool Renderer::set_scene_quality(int scale_percent, int msaa) {
    scale_percent = std::clamp(scale_percent, 25, 400);
    msaa = msaa >= 8 ? 8 : msaa >= 4 ? 4 : msaa >= 2 ? 2 : 1;
    if (scale_percent == scene_scale_ && msaa == scene_msaa_ && depth_ != nullptr) return true;
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView* none[2] = {};
    ctx_->PSSetShaderResources(0, 2, none);
    scene_scale_ = scale_percent;
    scene_msaa_ = msaa;
    create_scene_targets();
    return scene_scale_ == scale_percent && scene_msaa_ == msaa;
}

std::vector<int> Renderer::supported_msaa() const {
    std::vector<int> out;
    for (int n : {2, 4, 8}) {
        UINT q_color = 0, q_depth = 0;
        device_->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, UINT(n), &q_color);
        device_->CheckMultisampleQualityLevels(DXGI_FORMAT_D32_FLOAT, UINT(n), &q_depth);
        if (q_color > 0 && q_depth > 0) out.push_back(n);
    }
    return out;
}

void Renderer::set_texture_filter(TextureFilter f) {
    D3D11_SAMPLER_DESC sd{};
    const int aniso = texture_filter_anisotropy(f);
    if (aniso > 0) {
        sd.Filter = D3D11_FILTER_ANISOTROPIC;
        sd.MaxAnisotropy = UINT(aniso);
    } else {
        sd.Filter = f == TextureFilter::Bilinear ? D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.MaxAnisotropy = 1;
    }
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    Com<ID3D11SamplerState> s;
    check(device_->CreateSamplerState(&sd, &s), "sampler");
    sampler_ = s;
    filter_ = f;
}

namespace {
// A saída (o monitor) da janela: a da cadeia de troca; sem ela, a primeira do adaptador; num adaptador sem monitor (o
// integrado desenhando, o monitor na outra placa: só do port, [video] adapter), a saída de qualquer adaptador que mostra
// o monitor da janela.
Microsoft::WRL::ComPtr<IDXGIOutput> output_of(IDXGISwapChain1* swap, ID3D11Device* device, HWND hwnd) {
    Microsoft::WRL::ComPtr<IDXGIOutput> out;
    if (swap != nullptr && SUCCEEDED(swap->GetContainingOutput(&out))) return out;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi))) && SUCCEEDED(dxgi->GetAdapter(&adapter)) &&
        SUCCEEDED(adapter->EnumOutputs(0, &out))) {
        return out;
    }
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (adapter == nullptr || FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return nullptr;
    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    Microsoft::WRL::ComPtr<IDXGIOutput> first;
    for (UINT i = 0;; ++i) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> ad;
        if (FAILED(factory->EnumAdapters1(i, &ad))) break;
        for (UINT k = 0;; ++k) {
            Microsoft::WRL::ComPtr<IDXGIOutput> o;
            if (FAILED(ad->EnumOutputs(k, &o))) break;
            DXGI_OUTPUT_DESC d{};
            if (SUCCEEDED(o->GetDesc(&d)) && d.Monitor == monitor) return o;
            if (first == nullptr) first = o;
        }
    }
    return first;
}
}  // namespace

std::vector<DisplayMode> Renderer::display_modes() const {
    std::vector<DisplayMode> out;
    const auto output = output_of(swap_.Get(), device_.Get(), hwnd_);
    if (output == nullptr) return out;
    UINT n = 0;
    if (FAILED(output->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &n, nullptr)) || n == 0) return out;
    std::vector<DXGI_MODE_DESC> modes(n);
    if (FAILED(output->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &n, modes.data()))) return out;
    for (UINT i = 0; i < n; ++i) out.push_back(DisplayMode{int(modes[i].Width), int(modes[i].Height)});
    return out;
}

DisplayMode Renderer::desktop_mode() const {
    const auto output = output_of(swap_.Get(), device_.Get(), hwnd_);
    DXGI_OUTPUT_DESC d{};
    if (output == nullptr || FAILED(output->GetDesc(&d))) return DisplayMode{};
    return DisplayMode{int(d.DesktopCoordinates.right - d.DesktopCoordinates.left),
                       int(d.DesktopCoordinates.bottom - d.DesktopCoordinates.top)};
}

bool Renderer::set_exclusive_fullscreen(bool on, int width, int height) {
    if (offscreen_ || swap_ == nullptr) return false;
    if (!on) {
        if (exclusive_fullscreen()) swap_->SetFullscreenState(FALSE, nullptr);
        return true;
    }
    const auto output = output_of(swap_.Get(), device_.Get(), hwnd_);
    if (output == nullptr) return false;
    DXGI_MODE_DESC want{};
    want.Width = UINT(width);
    want.Height = UINT(height);
    want.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    DXGI_MODE_DESC mode{};
    if (FAILED(output->FindClosestMatchingMode(&want, &mode, device_.Get()))) return false;
    // Como a documentação do DXGI pede: o tamanho do alvo, a troca de estado, de novo o alvo sem a taxa
    // de atualização e então os buffers (WM_SIZE chega ao main, que chama resize de novo).
    swap_->ResizeTarget(&mode);
    if (FAILED(swap_->SetFullscreenState(TRUE, output.Get()))) return false;
    mode.RefreshRate.Numerator = 0;
    mode.RefreshRate.Denominator = 0;
    swap_->ResizeTarget(&mode);
    resize(int(mode.Width), int(mode.Height));
    return true;
}

bool Renderer::exclusive_fullscreen() const {
    BOOL fs = FALSE;
    if (swap_ == nullptr || offscreen_ || FAILED(swap_->GetFullscreenState(&fs, nullptr))) return false;
    return fs != FALSE;
}

void Renderer::resize(int width, int height) {
    if (width <= 0 || height <= 0 || offscreen_) return;
    width_ = width;
    height_ = height;
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
    if (ui_ != nullptr) ui_->set_target(nullptr);
    if (modern_ != nullptr) modern_->set_target(nullptr);
    blur_full_.Reset();
    blur_full_srv_.Reset();
    target_.Reset();
    rtv_.Reset();
    dsv_.Reset();
    depth_srv_.Reset();
    depth_.Reset();
    check(swap_->ResizeBuffers(0, UINT(width), UINT(height), DXGI_FORMAT_UNKNOWN, swap_flags_), "ResizeBuffers");
    create_targets();
}

void Renderer::create_pipeline() {
    auto vs = compile("vs_main", "vs_5_0");
    auto ps = compile("ps_main", "ps_5_0");
    check(device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vs_), "vertex shader");
    check(device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &ps_), "pixel shader");
    auto pa = compile("ps_a2c", "ps_5_0");
    check(device_->CreatePixelShader(pa->GetBufferPointer(), pa->GetBufferSize(), nullptr, &ps_a2c_), "pixel shader a2c");
    auto bvs = compile("vs_blit", "vs_5_0", kBlitShader, sizeof(kBlitShader) - 1);
    auto bps = compile("ps_blit", "ps_5_0", kBlitShader, sizeof(kBlitShader) - 1);
    check(device_->CreateVertexShader(bvs->GetBufferPointer(), bvs->GetBufferSize(), nullptr, &blit_vs_), "blit vs");
    check(device_->CreatePixelShader(bps->GetBufferPointer(), bps->GetBufferSize(), nullptr, &blit_ps_), "blit ps");
    auto fvs = compile("vs_fog", "vs_5_0");
    auto fps = compile("ps_fog", "ps_5_0");
    check(device_->CreateVertexShader(fvs->GetBufferPointer(), fvs->GetBufferSize(), nullptr, &fog_vs_), "fog vs");
    check(device_->CreatePixelShader(fps->GetBufferPointer(), fps->GetBufferSize(), nullptr, &fog_ps_), "fog ps");
    // Só do PC: a névoa por amostra (MSAA) e a redução da cena em escala para a janela.
    auto fms = compile("ps_fog_ms", "ps_5_0");
    check(device_->CreatePixelShader(fms->GetBufferPointer(), fms->GetBufferSize(), nullptr, &fog_ps_ms_), "fog ps ms");
    auto dps = compile("ps_down", "ps_5_0", kBlitShader, sizeof(kBlitShader) - 1);
    check(device_->CreatePixelShader(dps->GetBufferPointer(), dps->GetBufferSize(), nullptr, &down_ps_), "down ps");
    auto blps = compile("ps_blur", "ps_5_0", kBlitShader, sizeof(kBlitShader) - 1);
    check(device_->CreatePixelShader(blps->GetBufferPointer(), blps->GetBufferSize(), nullptr, &blur_ps_), "blur ps");
    auto bmps = compile("ps_blur_mix", "ps_5_0", kBlitShader, sizeof(kBlitShader) - 1);
    check(device_->CreatePixelShader(bmps->GetBufferPointer(), bmps->GetBufferSize(), nullptr, &blur_mix_ps_), "blur mix ps");

    const D3D11_INPUT_ELEMENT_DESC elems[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, pos), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(Vertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    check(device_->CreateInputLayout(elems, 4, vs->GetBufferPointer(), vs->GetBufferSize(), &layout_),
          "input layout");

    auto svs = compile("vs_skin", "vs_5_0");
    check(device_->CreateVertexShader(svs->GetBufferPointer(), svs->GetBufferSize(), nullptr, &vs_skin_),
          "vertex shader de skinning");
    const D3D11_INPUT_ELEMENT_DESC skin_elems[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(SkinnedVertex, pos), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(SkinnedVertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(SkinnedVertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(SkinnedVertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, offsetof(SkinnedVertex, bones), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(SkinnedVertex, weight), D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    check(device_->CreateInputLayout(skin_elems, 6, svs->GetBufferPointer(), svs->GetBufferSize(), &layout_skin_),
          "input layout de skinning");
    // Os rostos: o mesmo vértice com o índice de morph (só esta variante o lê; vs_skin fica como era).
    auto mvs = compile("vs_skin_morph", "vs_5_0");
    check(device_->CreateVertexShader(mvs->GetBufferPointer(), mvs->GetBufferSize(), nullptr, &vs_skin_morph_),
          "vertex shader dos rostos");
    D3D11_INPUT_ELEMENT_DESC morph_elems[7];
    std::copy(std::begin(skin_elems), std::end(skin_elems), morph_elems);
    morph_elems[6] = {"MORPHINDEX", 0, DXGI_FORMAT_R32_SINT, 0, offsetof(SkinnedVertex, morph),
                      D3D11_INPUT_PER_VERTEX_DATA, 0};
    check(device_->CreateInputLayout(morph_elems, 7, mvs->GetBufferPointer(), mvs->GetBufferSize(),
                                     &layout_skin_morph_),
          "input layout dos rostos");

    D3D11_BUFFER_DESC cb{};
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cb.ByteWidth = sizeof(FrameCB);
    check(device_->CreateBuffer(&cb, nullptr, &cb_frame_), "constant buffer");
    cb.ByteWidth = sizeof(ObjectCB);
    check(device_->CreateBuffer(&cb, nullptr, &cb_object_), "constant buffer");
    cb.ByteWidth = sizeof(SkinCB);
    check(device_->CreateBuffer(&cb, nullptr, &cb_skin_), "constant buffer");
    cb.ByteWidth = sizeof(MorphCB);
    check(device_->CreateBuffer(&cb, nullptr, &cb_morph_), "constant buffer");
    cb.ByteWidth = sizeof(FogCB);
    check(device_->CreateBuffer(&cb, nullptr, &cb_fog_), "constant buffer");
    cb.ByteWidth = sizeof(DownCB);
    check(device_->CreateBuffer(&cb, nullptr, &cb_down_), "constant buffer");
    cb.ByteWidth = sizeof(BlurCB);
    check(device_->CreateBuffer(&cb, nullptr, &cb_blur_), "constant buffer");

    // Névoa: cor = névoa * (1 - fica) + quadro * fica, com "fica" no alfa da saída; o alfa do quadro
    // não muda.
    D3D11_BLEND_DESC fbd{};
    D3D11_RENDER_TARGET_BLEND_DESC& frt = fbd.RenderTarget[0];
    frt.BlendEnable = TRUE;
    frt.SrcBlend = D3D11_BLEND_INV_SRC_ALPHA;
    frt.DestBlend = D3D11_BLEND_SRC_ALPHA;
    frt.BlendOp = D3D11_BLEND_OP_ADD;
    frt.SrcBlendAlpha = D3D11_BLEND_ZERO;
    frt.DestBlendAlpha = D3D11_BLEND_ONE;
    frt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    frt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    check(device_->CreateBlendState(&fbd, &fog_blend_), "blend state da névoa");
    D3D11_DEPTH_STENCIL_DESC nds{};
    nds.DepthEnable = FALSE;
    nds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    nds.DepthFunc = D3D11_COMPARISON_ALWAYS;
    check(device_->CreateDepthStencilState(&nds, &no_depth_), "depth state da névoa");

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_ANISOTROPIC;
    sd.MaxAnisotropy = 8;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    check(device_->CreateSamplerState(&sd, &sampler_), "sampler");

    // 2D: o nível 0 da textura (o GS não tem mipmaps nos sprites), de ponto nas fontes e bilinear no resto.
    D3D11_SAMPLER_DESC sd2{};
    sd2.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd2.AddressU = sd2.AddressV = sd2.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd2.MaxLOD = 0.0f;
    check(device_->CreateSamplerState(&sd2, &sampler_point_), "sampler 2D");
    sd2.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    check(device_->CreateSamplerState(&sd2, &sampler_linear_), "sampler 2D");
    sd2.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd2.MaxLOD = D3D11_FLOAT32_MAX;
    check(device_->CreateSamplerState(&sd2, &sampler_smooth_), "sampler 2D HD");

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;  // o jogo desenha faces dos dois lados
    rd.DepthClipEnable = TRUE;
    check(device_->CreateRasterizerState(&rd, &raster_solid_), "rasterizer");
    rd.ScissorEnable = TRUE;
    check(device_->CreateRasterizerState(&rd, &raster_2d_), "rasterizer 2D");
    rd.ScissorEnable = FALSE;
    rd.FillMode = D3D11_FILL_WIREFRAME;
    check(device_->CreateRasterizerState(&rd, &raster_wire_), "rasterizer");

    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;  // profundidade invertida
    check(device_->CreateDepthStencilState(&ds, &depth_state_), "depth state");

    const uint32_t white = 0xFFFFFFFF;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = 1;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{&white, 4, 0};
    Com<ID3D11Texture2D> tex;
    check(device_->CreateTexture2D(&td, &init, &tex), "textura branca");
    check(device_->CreateShaderResourceView(tex.Get(), nullptr, &white_), "SRV branca");
    // kQuadNoTexture: 0x80 = 1.0 na escala do GS em todos os canais (a modulação devolve a cor e o alfa do vértice).
    const uint32_t gs_one = 0x80808080;
    D3D11_SUBRESOURCE_DATA init_one{&gs_one, 4, 0};
    Com<ID3D11Texture2D> tex_one;
    check(device_->CreateTexture2D(&td, &init_one, &tex_one), "textura sem textura");
    check(device_->CreateShaderResourceView(tex_one.Get(), nullptr, &no_texture_), "SRV sem textura");
}

uint64_t gs_alpha_of_blend(Blend mode) {
    switch (mode) {
        case Blend::Additive: return 0x48;
        case Blend::Alpha: return 0x44;
        case Blend::Subtractive: return 0x42;
        default: return 0x2a;
    }
}

namespace {
D3D11_BLEND d3d_factor(BlendFactor f) {
    switch (f) {
        case BlendFactor::Zero: return D3D11_BLEND_ZERO;
        case BlendFactor::One: return D3D11_BLEND_ONE;
        case BlendFactor::SrcAlpha: return D3D11_BLEND_SRC_ALPHA;
        case BlendFactor::InvSrcAlpha: return D3D11_BLEND_INV_SRC_ALPHA;
        case BlendFactor::DstAlpha: return D3D11_BLEND_DEST_ALPHA;
        case BlendFactor::InvDstAlpha: return D3D11_BLEND_INV_DEST_ALPHA;
        case BlendFactor::Fix: return D3D11_BLEND_BLEND_FACTOR;
        default: return D3D11_BLEND_INV_BLEND_FACTOR;
    }
}
D3D11_BLEND_OP d3d_op(BlendOp op) {
    switch (op) {
        case BlendOp::Subtract: return D3D11_BLEND_OP_SUBTRACT;
        case BlendOp::RevSubtract: return D3D11_BLEND_OP_REV_SUBTRACT;
        default: return D3D11_BLEND_OP_ADD;
    }
}
bool uses_fix(const GsBlendEq& g) {
    const auto fix = [](BlendFactor f) { return f == BlendFactor::Fix || f == BlendFactor::InvFix; };
    return g.enable && (fix(g.src) || fix(g.dst));
}
}  // namespace

ID3D11BlendState* Renderer::blend_state(const GsBlendEq& g, bool coverage) {
    const uint32_t key = uint32_t(g.src) | uint32_t(g.dst) << 8 | uint32_t(g.op) << 16 | uint32_t(g.enable) << 24 |
                         uint32_t(coverage) << 25;
    for (const auto& [k, s] : gs_blends_) {
        if (k == key) return s.Get();
    }
    D3D11_BLEND_DESC bd{};
    bd.AlphaToCoverageEnable = coverage ? TRUE : FALSE;
    D3D11_RENDER_TARGET_BLEND_DESC& rt = bd.RenderTarget[0];
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    rt.BlendEnable = g.enable;
    rt.SrcBlend = d3d_factor(g.src);
    rt.DestBlend = d3d_factor(g.dst);
    rt.BlendOp = d3d_op(g.op);
    // O GS mistura só r, g e b; o alfa gravado é o do pixel (As), que ALPHA com C = Ad lê depois.
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_ZERO;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    Com<ID3D11BlendState> state;
    check(device_->CreateBlendState(&bd, &state), "blend state do GS");
    gs_blends_.emplace_back(key, state);
    return state.Get();
}

ID3D11DepthStencilState* Renderer::depth_state(D3D11_COMPARISON_FUNC func, bool write) {
    const uint32_t key = uint32_t(func) | uint32_t(write) << 8;
    for (const auto& [k, s] : depths_) {
        if (k == key) return s.Get();
    }
    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc = func;
    Com<ID3D11DepthStencilState> state;
    check(device_->CreateDepthStencilState(&ds, &state), "depth state do GS");
    depths_.emplace_back(key, state);
    return state.Get();
}

void Renderer::reset_resources() {
    meshes_.clear();
    textures_.clear();
    hd_textures_.clear();
}

bool Renderer::set_texture_dds(int slot, const std::wstring& path) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || f == nullptr) return false;
    std::vector<uint8_t> file;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size > 128) {
        file.resize(size_t(size));
        if (std::fread(file.data(), 1, file.size(), f) != file.size()) file.clear();
    }
    std::fclose(f);
    if (file.size() <= 128 || std::memcmp(file.data(), "DDS ", 4) != 0) return false;
    const auto u32 = [&](size_t o) {
        uint32_t v;
        std::memcpy(&v, file.data() + o, 4);
        return v;
    };
    const uint32_t height = u32(12), width = u32(16);
    const uint32_t mips = std::max(1u, u32(28));
    const uint32_t pf_flags = u32(80), fourcc = u32(84), rgb_bits = u32(88);
    const auto cc = [](const char* s) { return uint32_t(s[0]) | uint32_t(s[1]) << 8 | uint32_t(s[2]) << 16 | uint32_t(s[3]) << 24; };
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    size_t offset = 128;
    if ((pf_flags & 4) != 0 && fourcc == cc("DX10")) {
        if (file.size() < 148) return false;
        fmt = DXGI_FORMAT(u32(128));
        offset = 148;
    } else if ((pf_flags & 4) != 0 && fourcc == cc("DXT1")) {
        fmt = DXGI_FORMAT_BC1_UNORM;
    } else if ((pf_flags & 4) != 0 && fourcc == cc("DXT3")) {
        fmt = DXGI_FORMAT_BC2_UNORM;
    } else if ((pf_flags & 4) != 0 && fourcc == cc("DXT5")) {
        fmt = DXGI_FORMAT_BC3_UNORM;
    } else if ((pf_flags & 0x40) != 0 && rgb_bits == 32 && u32(92) == 0xff && u32(100) == 0xff0000) {
        fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    size_t block_bytes;  // 0: sem compressão (4 bytes por texel)
    switch (fmt) {
        case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: block_bytes = 8; break;
        case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB: block_bytes = 16; break;
        case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: block_bytes = 0; break;
        default: return false;
    }
    if (width == 0 || height == 0 || width > 16384 || height > 16384) return false;
    std::vector<D3D11_SUBRESOURCE_DATA> levels;
    uint32_t w = width, h = height;
    for (uint32_t m = 0; m < mips; ++m) {
        const size_t pitch = block_bytes ? size_t(std::max(1u, (w + 3) / 4)) * block_bytes : size_t(w) * 4;
        const size_t rows = block_bytes ? std::max(1u, (h + 3) / 4) : h;
        if (offset + pitch * rows > file.size()) break;  // arquivo curto: usa os níveis completos
        levels.push_back({file.data() + offset, UINT(pitch), 0});
        offset += pitch * rows;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    if (levels.empty()) return false;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = UINT(levels.size());
    td.ArraySize = 1;
    td.Format = fmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    Com<ID3D11Texture2D> tex;
    if (FAILED(device_->CreateTexture2D(&td, levels.data(), &tex))) return false;
    Com<ID3D11ShaderResourceView> srv;
    if (FAILED(device_->CreateShaderResourceView(tex.Get(), nullptr, &srv))) return false;
    if (levels.size() == 1 && std::max(width, height) > 1) {
        // Sem mipmaps: copia para RGBA8 na GPU e gera a cadeia, para não cintilar de longe.
        D3D11_TEXTURE2D_DESC rd = td;
        rd.MipLevels = 0;
        rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rd.Usage = D3D11_USAGE_DEFAULT;
        rd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        rd.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        Com<ID3D11Texture2D> full;
        Com<ID3D11ShaderResourceView> full_srv;
        Com<ID3D11RenderTargetView> rtv;
        D3D11_RENDER_TARGET_VIEW_DESC rvd{};
        rvd.Format = rd.Format;
        rvd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        if (SUCCEEDED(device_->CreateTexture2D(&rd, nullptr, &full)) &&
            SUCCEEDED(device_->CreateShaderResourceView(full.Get(), nullptr, &full_srv)) &&
            SUCCEEDED(device_->CreateRenderTargetView(full.Get(), &rvd, &rtv))) {
            ctx_->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
            ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
            const D3D11_VIEWPORT vp{0, 0, float(width), float(height), 0, 1};
            ctx_->RSSetViewports(1, &vp);
            ctx_->RSSetState(raster_solid_.Get());
            ctx_->IASetInputLayout(nullptr);
            ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ctx_->VSSetShader(blit_vs_.Get(), nullptr, 0);
            ctx_->PSSetShader(blit_ps_.Get(), nullptr, 0);
            ctx_->PSSetShaderResources(0, 1, srv.GetAddressOf());
            ctx_->Draw(3, 0);
            ID3D11ShaderResourceView* none = nullptr;
            ctx_->PSSetShaderResources(0, 1, &none);
            ctx_->OMSetRenderTargets(0, nullptr, nullptr);
            ctx_->GenerateMips(full_srv.Get());
            srv = full_srv;
        }
    }
    if (slot >= int(hd_textures_.size())) hd_textures_.resize(size_t(slot) + 1);
    hd_textures_[size_t(slot)] = std::move(srv);
    return true;
}

int Renderer::add_mesh(const EntityMesh& mesh) {
    GpuMesh gm;
    gm.skinned = mesh.skinned;
    gm.bones = mesh.bone_count;
    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init{};
    if (mesh.skinned) {
        bd.ByteWidth = UINT(mesh.skin_vertices.size() * sizeof(SkinnedVertex));
        init.pSysMem = mesh.skin_vertices.data();
    } else {
        bd.ByteWidth = UINT(mesh.vertices.size() * sizeof(Vertex));
        init.pSysMem = mesh.vertices.data();
    }
    check(device_->CreateBuffer(&bd, &init, &gm.vb), "vertex buffer");
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.ByteWidth = UINT(mesh.indices.size() * sizeof(uint32_t));
    init.pSysMem = mesh.indices.data();
    check(device_->CreateBuffer(&bd, &init, &gm.ib), "index buffer");
    gm.submeshes = mesh.submeshes;
    // Os alvos de morph de um rosto: os deltas de todos os elementos do bloco num buffer estruturado (float3).
    if (mesh.skinned && !mesh.morph.empty()) {
        D3D11_BUFFER_DESC mb{};
        mb.Usage = D3D11_USAGE_IMMUTABLE;
        mb.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        mb.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        mb.StructureByteStride = 12;
        mb.ByteWidth = UINT(mesh.morph.elements() * 12);
        D3D11_SUBRESOURCE_DATA md{};
        md.pSysMem = mesh.morph.deltas.data();
        Com<ID3D11Buffer> buf;
        check(device_->CreateBuffer(&mb, &md, &buf), "buffer de morphs");
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_UNKNOWN;
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sd.Buffer.FirstElement = 0;
        sd.Buffer.NumElements = UINT(mesh.morph.elements());
        check(device_->CreateShaderResourceView(buf.Get(), &sd, &gm.morph_srv), "SRV de morphs");
        gm.morph_targets = int(mesh.morph.targets);
    }
    meshes_.push_back(std::move(gm));
    return int(meshes_.size()) - 1;
}

void Renderer::set_texture(int slot, const Texture& t) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(t.width);
    td.Height = UINT(t.height);
    td.MipLevels = 0;  // cadeia completa, gerada na GPU
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    Com<ID3D11Texture2D> tex;
    check(device_->CreateTexture2D(&td, nullptr, &tex), "textura");
    ctx_->UpdateSubresource(tex.Get(), 0, nullptr, t.rgba.data(), UINT(t.width * 4), 0);
    Com<ID3D11ShaderResourceView> srv;
    check(device_->CreateShaderResourceView(tex.Get(), nullptr, &srv), "SRV");
    ctx_->GenerateMips(srv.Get());
    if (slot >= int(textures_.size())) textures_.resize(size_t(slot) + 1);
    textures_[size_t(slot)] = std::move(srv);
}

void Renderer::set_ui_image(uint32_t key, const uint8_t* rgba, int width, int height) {
    if (rgba == nullptr || width <= 0 || height <= 0) return;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(width);
    td.Height = UINT(height);
    td.MipLevels = 0;  // cadeia completa, gerada na GPU
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    Com<ID3D11Texture2D> tex;
    Com<ID3D11ShaderResourceView> srv;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &tex)) ||
        FAILED(device_->CreateShaderResourceView(tex.Get(), nullptr, &srv))) {
        return;
    }
    ctx_->UpdateSubresource(tex.Get(), 0, nullptr, rgba, UINT(width * 4), 0);
    ctx_->GenerateMips(srv.Get());
    for (auto& [k, v] : ui_images_) {
        if (k == key) {
            v = srv;
            return;
        }
    }
    ui_images_.emplace_back(key, srv);
}

ID3D11ShaderResourceView* Renderer::ui_image(uint32_t key) const {
    for (const auto& [k, v] : ui_images_) {
        if (k == key) return v.Get();
    }
    return nullptr;
}

// O estado do Direct3D no começo do quadro (e depois de cada trecho de Direct2D da camada HD).
void Renderer::bind_frame_state(const FrameParams& frame) {
    // Com o alvo próprio da cena (só do PC): a primeira passada desenha nele, com a profundidade dele; a
    // segunda (in_2d_), o 2D no back buffer, sem profundidade.
    ID3D11RenderTargetView* rtv = in_2d_ ? rtv_.Get() : scene_rtv();
    ctx_->OMSetRenderTargets(1, &rtv, in_2d_ ? nullptr : dsv_.Get());
    ctx_->OMSetDepthStencilState(depth_state_.Get(), 0);
    const bool scene = separate_scene() && !in_2d_;
    D3D11_VIEWPORT vp{0, 0, float(scene ? scene_w_ : width_), float(scene ? scene_h_ : height_), 0, 1};
    ctx_->RSSetViewports(1, &vp);
    ctx_->RSSetState(frame.wireframe ? raster_wire_.Get() : raster_solid_.Get());
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->IASetInputLayout(layout_.Get());
    ctx_->VSSetShader(vs_.Get(), nullptr, 0);
    ctx_->PSSetShader(ps_.Get(), nullptr, 0);
    a2c_bound_ = false;
    ctx_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    ID3D11Buffer* cbs[3] = {cb_frame_.Get(), cb_object_.Get(), cb_skin_.Get()};
    ctx_->VSSetConstantBuffers(0, 3, cbs);
    ctx_->PSSetConstantBuffers(0, 2, cbs);
    shader_state_ = -1;
}

void Renderer::render(const std::vector<DrawItem>& items, const FrameParams& frame) {
    const float* clear = frame.clear_color.data();
    env_parts_ = 0;
    a2c_parts_ = 0;
    in_2d_ = false;
    gpu_phase(GpuPhase::Scene);
    ctx_->ClearRenderTargetView(scene_rtv(), clear);
    ctx_->ClearDepthStencilView(dsv_.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
    // Só do port: o quadro de um filme é o fundo (render/movie_layer.h), no alvo da cena e no tamanho dele.
    if (movie_on_ && movie_ != nullptr) {
        const bool scene = separate_scene();
        const float sx = scene ? float(scene_w_) / float(width_) : 1.0f, sy = scene ? float(scene_h_) / float(height_) : 1.0f;
        const float r[4] = {movie_rect_[0] * sx, movie_rect_[1] * sy, movie_rect_[2] * sx, movie_rect_[3] * sy};
        movie_->draw(ctx_.Get(), scene_rtv(), scene ? scene_w_ : width_, scene ? scene_h_ : height_, r);
        ++counters_.draws;
    }
    bind_frame_state(frame);

    D3D11_MAPPED_SUBRESOURCE map;
    check(ctx_->Map(cb_frame_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map), "Map");
    FrameCB fcb{};
    std::memcpy(fcb.view_proj, frame.view_proj.data(), sizeof(fcb.view_proj));
    std::memcpy(fcb.light_dir, frame.light_dir, sizeof(fcb.light_dir));
    fcb.ambient = frame.ambient;
    fcb.debug_shading = frame.debug_shading ? 1.0f : 0.0f;
    fcb.lights_on = frame.lights ? 1.0f : 0.0f;
    std::memcpy(map.pData, &fcb, sizeof(fcb));
    ctx_->Unmap(cb_frame_.Get(), 0);

    // Os itens vêm na ordem do Game_Draw (opacos, depois os transparentes de trás para a frente):
    // desenha nessa ordem. O estado do GS de cada parte (mistura, teste de alfa, teste e escrita de
    // Z) sai de resolve_state; os blend e depth states ficam em cache.
    shader_state_ = -1;
    if (!separate_scene()) {
        for (const DrawItem& inst : items) draw_item(inst, frame);
        return;
    }
    // Só do PC, com escala ou MSAA: a cena no alvo dela, a redução para a janela e então o 2D por cima, no
    // tamanho da janela (cada grupo na ordem do jogo; o 2D sempre fica por cima, como na tela do PS2).
    const auto is_2d = [](const DrawItem& d) { return d.ui != nullptr || !d.quads2d.empty(); };
    for (const DrawItem& inst : items) {
        if (!is_2d(inst)) draw_item(inst, frame);
    }
    gpu_phase(GpuPhase::Resolve);
    resolve_scene();
    in_2d_ = true;
    bind_frame_state(frame);
    for (const DrawItem& inst : items) {
        if (is_2d(inst)) draw_item(inst, frame);
    }
}

void Renderer::render_2d(const std::vector<DrawItem>& items, const FrameParams& frame) {
    in_2d_ = true;
    gpu_phase(GpuPhase::TwoD);
    bind_frame_state(frame);
    for (const DrawItem& inst : items) {
        if (inst.ui != nullptr || !inst.quads2d.empty()) draw_item(inst, frame);
    }
}

void Renderer::draw_item(const DrawItem& inst, const FrameParams& frame) {
    D3D11_MAPPED_SUBRESOURCE map;
    const UINT offset = 0;
    if (inst.kind == ObjectKind::Volume && !frame.show_volumes) return;
    ++counters_.items;
    // O trecho da GPU (--bench): o 2D, a névoa ou a cena.
    if (profiling_) {
        const bool two_d = inst.ui != nullptr || !inst.quads2d.empty();
        gpu_phase(two_d ? GpuPhase::TwoD : inst.fog_pass ? GpuPhase::Fog : GpuPhase::Scene);
    }
    if (inst.fog_pass) {
        if (frame.fog) draw_fog(inst, frame);
        return;
    }
    if (inst.clear_depth) {
        // psiClearZ: o ZClearPacket do PS2 escreve o Z mais longe na tela inteira (aqui 0: a profundidade é
        // invertida), no alvo da cena (com MSAA, todas as amostras).
        if (!in_2d_) ctx_->ClearDepthStencilView(dsv_.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
        return;
    }
    if (inst.ui != nullptr) {
        draw_ui(*inst.ui, frame);
        return;
    }
    if (!inst.quads2d.empty()) {
        draw_2d(inst);
        ctx_->RSSetState(frame.wireframe ? raster_wire_.Get() : raster_solid_.Get());
        ctx_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
        return;
    }
    if (!inst.sprites.empty()) {
        draw_sprites(inst);
        return;
    }
    if (!inst.triangles.empty()) {
        draw_triangles(inst);
        return;
    }
    if (inst.mesh < 0 || inst.mesh >= int(meshes_.size())) return;
    const GpuMesh& gm = meshes_[size_t(inst.mesh)];
    const bool skinned = !inst.palette.empty();
    if (skinned != gm.skinned) return;
    if (skinned && inst.palette.size() < size_t(gm.bones)) return;
    // Um rosto com algum peso diferente de 0 usa a variante com os morphs; sem pesos, o skinning comum (a mesma
    // imagem de antes dos morphs).
    bool morph = false;
    if (skinned && frame.morphs && gm.morph_srv != nullptr) {
        for (const float w : inst.morph) morph = morph || w != 0.0f;
    }
    use_shader(morph ? 2 : (skinned ? 1 : 0));
    if (skinned) {
        check(ctx_->Map(cb_skin_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map), "Map");
        const size_t n = std::min(inst.palette.size(), size_t(kMaxSkinBones));
        std::memcpy(map.pData, inst.palette.data(), n * sizeof(inst.palette[0]));
        ctx_->Unmap(cb_skin_.Get(), 0);
    }
    if (morph) {
        MorphCB mc{};
        mc.n = uint32_t(std::min<size_t>({inst.morph.size(), size_t(gm.morph_targets), size_t(kMaxMorphTargets)}));
        for (uint32_t k = 0; k < mc.n; ++k) mc.w[k][0] = inst.morph[k];
        check(ctx_->Map(cb_morph_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map), "Map");
        std::memcpy(map.pData, &mc, sizeof(mc));
        ctx_->Unmap(cb_morph_.Get(), 0);
        ID3D11Buffer* mcb = cb_morph_.Get();
        ctx_->VSSetConstantBuffers(4, 1, &mcb);
        ctx_->VSSetShaderResources(3, 1, gm.morph_srv.GetAddressOf());
    }
    const UINT stride = skinned ? UINT(sizeof(SkinnedVertex)) : UINT(sizeof(Vertex));
    ctx_->IASetVertexBuffers(0, 1, gm.vb.GetAddressOf(), &stride, &offset);
    ctx_->IASetIndexBuffer(gm.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    for (const SubMesh& sm : gm.submeshes) {
        apply_state(inst, sm.gs, false, sm.env && frame.env_map);
        if (sm.env) ++env_parts_;
        ID3D11ShaderResourceView* srv = texture_srv(sm.texture);
        ctx_->PSSetShaderResources(0, 1, &srv);
        ctx_->DrawIndexed(sm.index_count, sm.first_index, 0);
        ++counters_.draws;
    }
}

// Só do PC: a cena do alvo próprio para o back buffer. Com MSAA, ResolveSubresource (direto no back buffer
// quando o tamanho é o mesmo); com escala, ps_down reduz (ou amplia) para o tamanho da janela.
void Renderer::resolve_scene() {
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
    const bool scaled = scene_w_ != width_ || scene_h_ != height_;
    if (scene_msaa_ > 1) {
        ID3D11Texture2D* dst = scaled ? scene_resolved_.Get() : target_.Get();
        ctx_->ResolveSubresource(dst, 0, scene_color_.Get(), 0, DXGI_FORMAT_B8G8R8A8_UNORM);
        if (!scaled) return;
    }
    D3D11_MAPPED_SUBRESOURCE map;
    check(ctx_->Map(cb_down_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map), "Map");
    DownCB d{};
    d.src_size[0] = float(scene_w_);
    d.src_size[1] = float(scene_h_);
    d.ratio[0] = float(scene_w_) / float(width_);
    d.ratio[1] = float(scene_h_) / float(height_);
    d.taps = float(std::clamp(int(std::ceil(std::max(d.ratio[0], d.ratio[1]) - 0.01f)), 1, 4));
    std::memcpy(map.pData, &d, sizeof(d));
    ctx_->Unmap(cb_down_.Get(), 0);
    ctx_->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);
    const D3D11_VIEWPORT vp{0, 0, float(width_), float(height_), 0, 1};
    ctx_->RSSetViewports(1, &vp);
    ctx_->RSSetState(raster_solid_.Get());
    ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx_->OMSetDepthStencilState(no_depth_.Get(), 0);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(fog_vs_.Get(), nullptr, 0);  // o triângulo que cobre a tela
    ctx_->PSSetShader(down_ps_.Get(), nullptr, 0);
    ID3D11Buffer* cb = cb_down_.Get();
    ctx_->PSSetConstantBuffers(4, 1, &cb);
    ctx_->PSSetShaderResources(0, 1, scene_srv_.GetAddressOf());
    ctx_->PSSetSamplers(0, 1, sampler_linear_.GetAddressOf());
    ctx_->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    ctx_->PSSetShaderResources(0, 1, &none);
    shader_state_ = -1;
}

void Renderer::use_shader(int skinned) {
    if (skinned == shader_state_) return;
    ID3D11InputLayout* layouts[3] = {layout_.Get(), layout_skin_.Get(), layout_skin_morph_.Get()};
    ID3D11VertexShader* shaders[3] = {vs_.Get(), vs_skin_.Get(), vs_skin_morph_.Get()};
    const int i = skinned >= 0 && skinned <= 2 ? skinned : 0;
    ctx_->IASetInputLayout(layouts[i]);
    ctx_->VSSetShader(shaders[i], nullptr, 0);
    shader_state_ = skinned;
}

ID3D11ShaderResourceView* Renderer::texture_srv(int slot) const {
    if (slot == kQuadNoTexture) return no_texture_.Get();
    if (use_hd_ && slot >= 0 && slot < int(hd_textures_.size()) && hd_textures_[size_t(slot)]) {
        return hd_textures_[size_t(slot)].Get();
    }
    if (slot >= 0 && slot < int(textures_.size()) && textures_[size_t(slot)]) return textures_[size_t(slot)].Get();
    return white_.Get();
}

// Estado do GS (mistura, teste de alfa, teste e escrita de Z) e constantes de uma parte do desenho.
void Renderer::apply_state(const DrawItem& inst, const GsState& own, bool sprite, bool env, bool screen) {
    const DrawState st = resolve_state(own, inst);
    GsBlendEq gb = gs_blend_equation(st.alpha, st.abe, sprite);
    // Só do PC: com MSAA, um desenho opaco recortado pelo teste de alfa vai com o alfa para a cobertura (ps_a2c); sem
    // MSAA nada muda.
    const bool coverage = alpha_coverage_ && scene_msaa_ > 1 && !in_2d_ && !screen && !sprite &&
                          gs_alpha_coverage_candidate(st.atst, st.aref, st.aint, gb);
    if (coverage) {
        gb.enable = false;  // o teste só deixa passar As >= 1: (Cs - Cd) * As + Cd é Cs
        gb.src = BlendFactor::One;
        gb.dst = BlendFactor::Zero;
        gb.op = BlendOp::Add;
        ++a2c_parts_;
    }
    if (coverage != a2c_bound_) {
        ctx_->PSSetShader(coverage ? ps_a2c_.Get() : ps_.Get(), nullptr, 0);
        a2c_bound_ = coverage;
    }
    const float factor[4] = {gb.fix, gb.fix, gb.fix, gb.fix};
    ctx_->OMSetBlendState(blend_state(gb, coverage), uses_fix(gb) ? factor : nullptr, 0xffffffff);
    ctx_->OMSetDepthStencilState(depth_state(st.zfunc, st.zwrite), 0);
    D3D11_MAPPED_SUBRESOURCE map;
    check(ctx_->Map(cb_object_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map), "Map");
    ObjectCB ocb{};
    std::memcpy(ocb.world, inst.world.data(), sizeof(ocb.world));
    ocb.premult = (sprite && gb.enable && gb.premultipliable) ? 1.0f : 0.0f;
    ocb.sprite = sprite ? 1.0f : 0.0f;
    ocb.env = env ? 1.0f : 0.0f;
    ocb.num_lights = float(inst.num_lights);
    std::memcpy(ocb.light_pos, inst.light_pos, sizeof(ocb.light_pos));
    std::memcpy(ocb.light_col, inst.light_color, sizeof(ocb.light_col));
    std::memcpy(ocb.env_s, inst.env_s, sizeof(inst.env_s));
    std::memcpy(ocb.env_t, inst.env_t, sizeof(inst.env_t));
    std::memcpy(ocb.tint, inst.tint.data(), sizeof(ocb.tint));
    ocb.atst = st.atst;
    ocb.aref = st.aref;
    ocb.aint = st.aint ? 1.0f : 0.0f;
    ocb.screen = screen ? 1.0f : 0.0f;
    std::memcpy(map.pData, &ocb, sizeof(ocb));
    ctx_->Unmap(cb_object_.Get(), 0);
}

// Sprites de psiDrawParticleList: os vértices do item vão para um vertex buffer dinâmico e cada
// quatro viram dois triângulos (índices fixos 0 1 2, 0 2 3).
void Renderer::draw_sprites(const DrawItem& inst) {
    const size_t vertices = inst.sprites.size() - inst.sprites.size() % 4;
    if (vertices == 0) return;
    const size_t quads = vertices / 4;
    if (vertices > sprite_vb_vertices_) {
        sprite_vb_vertices_ = std::max<size_t>(vertices, 4096);
        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bd.ByteWidth = UINT(sprite_vb_vertices_ * sizeof(Vertex));
        sprite_vb_.Reset();
        check(device_->CreateBuffer(&bd, nullptr, &sprite_vb_), "vertex buffer dos sprites");
    }
    if (quads > sprite_ib_quads_) {
        sprite_ib_quads_ = std::max<size_t>(quads, 1024);
        std::vector<uint32_t> idx;
        idx.reserve(sprite_ib_quads_ * 6);
        for (uint32_t q = 0; q < uint32_t(sprite_ib_quads_); ++q) {
            const uint32_t b = q * 4;
            idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
        }
        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.ByteWidth = UINT(idx.size() * sizeof(uint32_t));
        D3D11_SUBRESOURCE_DATA init{idx.data(), 0, 0};
        sprite_ib_.Reset();
        check(device_->CreateBuffer(&bd, &init, &sprite_ib_), "index buffer dos sprites");
    }
    D3D11_MAPPED_SUBRESOURCE map;
    check(ctx_->Map(sprite_vb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map), "Map");
    std::memcpy(map.pData, inst.sprites.data(), vertices * sizeof(Vertex));
    ctx_->Unmap(sprite_vb_.Get(), 0);
    use_shader(0);
    apply_state(inst, GsState{}, true);
    const UINT stride = UINT(sizeof(Vertex)), offset = 0;
    ctx_->IASetVertexBuffers(0, 1, sprite_vb_.GetAddressOf(), &stride, &offset);
    ctx_->IASetIndexBuffer(sprite_ib_.Get(), DXGI_FORMAT_R32_UINT, 0);
    ID3D11ShaderResourceView* srv = texture_srv(inst.sprite_texture);
    ctx_->PSSetShaderResources(0, 1, &srv);
    ctx_->DrawIndexed(UINT(quads * 6), 0, 0);
    ++counters_.draws;
}

// Triângulos soltos (os estilhaços de psiDrawShard): os vértices do item vão para um vertex buffer
// dinâmico e são desenhados sem índices, com o shader e o estado dos sprites (cor na escala do GS, sem
// rotina da VU1, como a rotina 0 que o PS2 usa nos estilhaços).
void Renderer::draw_triangles(const DrawItem& inst) {
    const size_t vertices = inst.triangles.size() - inst.triangles.size() % 3;
    if (vertices == 0) return;
    if (vertices > tri_vb_vertices_) {
        tri_vb_vertices_ = std::max<size_t>(vertices, 3072);
        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bd.ByteWidth = UINT(tri_vb_vertices_ * sizeof(Vertex));
        tri_vb_.Reset();
        check(device_->CreateBuffer(&bd, nullptr, &tri_vb_), "vertex buffer dos estilhaços");
    }
    D3D11_MAPPED_SUBRESOURCE map;
    check(ctx_->Map(tri_vb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map), "Map");
    std::memcpy(map.pData, inst.triangles.data(), vertices * sizeof(Vertex));
    ctx_->Unmap(tri_vb_.Get(), 0);
    use_shader(0);
    apply_state(inst, GsState{}, true);
    const UINT stride = UINT(sizeof(Vertex)), offset = 0;
    ctx_->IASetVertexBuffers(0, 1, tri_vb_.GetAddressOf(), &stride, &offset);
    ID3D11ShaderResourceView* srv = texture_srv(inst.triangle_texture);
    ctx_->PSSetShaderResources(0, 1, &srv);
    ctx_->Draw(UINT(vertices), 0);
    ++counters_.draws;
}

// 2D (psiDrawSprites): quatro vértices por quadrado num vertex buffer dinâmico, já em coordenadas da
// tela (-1..1), com os índices fixos dos sprites; quadrados seguidos com a mesma textura, o mesmo ALPHA_1
// e o mesmo filtro saem num desenho só. O estado é o de psiStartSprites: TEST_1 = alfa > 0 sem teste de Z
// (0xd) e ZBUF_1 sem escrita; a mistura é a do quadrado. Recorte na área da tela do PS2 (clip2d).
void Renderer::draw_2d(const DrawItem& inst) {
    const size_t quads = inst.quads2d.size();
    const size_t vertices = quads * 4;
    if (vertices > quad2d_vb_vertices_) {
        quad2d_vb_vertices_ = std::max<size_t>(vertices, 4096);
        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bd.ByteWidth = UINT(quad2d_vb_vertices_ * sizeof(Vertex));
        quad2d_vb_.Reset();
        check(device_->CreateBuffer(&bd, nullptr, &quad2d_vb_), "vertex buffer 2D");
    }
    if (quads > sprite_ib_quads_) {
        sprite_ib_quads_ = std::max<size_t>(quads, 1024);
        std::vector<uint32_t> idx;
        idx.reserve(sprite_ib_quads_ * 6);
        for (uint32_t q = 0; q < uint32_t(sprite_ib_quads_); ++q) {
            const uint32_t b = q * 4;
            idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
        }
        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.ByteWidth = UINT(idx.size() * sizeof(uint32_t));
        D3D11_SUBRESOURCE_DATA init{idx.data(), 0, 0};
        sprite_ib_.Reset();
        check(device_->CreateBuffer(&bd, &init, &sprite_ib_), "index buffer dos sprites");
    }
    D3D11_MAPPED_SUBRESOURCE map;
    check(ctx_->Map(quad2d_vb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map), "Map");
    auto* out = static_cast<Vertex*>(map.pData);
    const float sx = 2.0f / float(width_), sy = 2.0f / float(height_);
    for (const Quad2D& q : inst.quads2d) {
        const float x0 = q.x0 * sx - 1.0f, x1 = q.x1 * sx - 1.0f;
        const float y0 = 1.0f - q.y0 * sy, y1 = 1.0f - q.y1 * sy;
        const float corner[4][4] = {{x0, y0, q.u0, q.v0}, {x1, y0, q.u1, q.v0}, {x1, y1, q.u1, q.v1}, {x0, y1, q.u0, q.v1}};
        for (const auto& c : corner) {
            Vertex v{};
            v.pos[0] = c[0];
            v.pos[1] = c[1];
            v.uv[0] = c[2];
            v.uv[1] = c[3];
            std::memcpy(v.color, q.rgba, 4);
            *out++ = v;
        }
    }
    ctx_->Unmap(quad2d_vb_.Get(), 0);
    use_shader(0);
    const UINT stride = UINT(sizeof(Vertex)), offset = 0;
    ctx_->IASetVertexBuffers(0, 1, quad2d_vb_.GetAddressOf(), &stride, &offset);
    ctx_->IASetIndexBuffer(sprite_ib_.Get(), DXGI_FORMAT_R32_UINT, 0);
    ctx_->RSSetState(raster_2d_.Get());
    const D3D11_RECT clip{LONG(inst.clip2d[0]), LONG(inst.clip2d[1]), LONG(inst.clip2d[2] + 0.5f),
                          LONG(inst.clip2d[3] + 0.5f)};
    ctx_->RSSetScissorRects(1, &clip);
    size_t first = 0;
    while (first < quads) {
        const Quad2D& a = inst.quads2d[first];
        size_t end = first + 1;
        while (end < quads && inst.quads2d[end].texture == a.texture && inst.quads2d[end].alpha == a.alpha &&
               inst.quads2d[end].linear == a.linear && inst.quads2d[end].srv == a.srv &&
               inst.quads2d[end].smooth == a.smooth) {
            ++end;
        }
        GsState own;
        own.alpha = a.alpha;
        own.test = 0xd;              // ATE, ATST = GREATER, AREF = 0, sem ZTE
        own.zbuf = 0x131000000ull;   // ZMSK
        own.has = GsState::kAlpha | GsState::kTest | GsState::kZbuf;
        apply_state(inst, own, true, false, true);
        // Uma substituta HD (pacote do PCSX2) é amostrada com filtro bilinear mesmo nas fontes: o filtro de
        // ponto do GS é para a textura original, do tamanho da tela do PS2.
        const bool hd = use_hd_ && a.texture >= 0 && a.texture < int(hd_textures_.size()) &&
                        hd_textures_[size_t(a.texture)] != nullptr;
        ctx_->PSSetSamplers(0, 1, a.smooth ? sampler_smooth_.GetAddressOf()
                                           : (a.linear || hd ? sampler_linear_.GetAddressOf() : sampler_point_.GetAddressOf()));
        ID3D11ShaderResourceView* srv = a.srv != nullptr ? a.srv : texture_srv(a.texture);
        ctx_->PSSetShaderResources(0, 1, &srv);
        ctx_->DrawIndexed(UINT((end - first) * 6), UINT(first * 6), 0);
        ++counters_.draws;
        first = end;
    }
}

// Camada 2D em alta definição (render/ui_hd.h): as imagens seguidas com o mesmo recorte viram um item 2D
// comum (com a substituta no lugar da textura e o filtro com mipmaps); os textos seguidos, um trecho de
// Direct2D, depois do qual o estado do Direct3D do quadro volta.
void Renderer::draw_ui(const UiHdBatch& batch, const FrameParams& frame) {
    if (ui_ == nullptr || !ui_->ok()) return;
    DrawItem images;
    images.blend = Blend::Alpha;
    const auto flush_images = [&]() {
        if (images.quads2d.empty()) return;
        draw_2d(images);
        ctx_->RSSetState(frame.wireframe ? raster_wire_.Get() : raster_solid_.Get());
        ctx_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
        images.quads2d.clear();
    };
    bool in_text = false;
    const auto end_text = [&]() {
        if (!in_text) return;
        ui_->end_text();
        bind_frame_state(frame);
        in_text = false;
    };
    for (const UiHdOp& op : batch.ops) {
        if (op.kind == UiHdOp::Text || op.kind == UiHdOp::Key) {
            flush_images();
            if (!in_text) {
                ui_->begin_text();
                in_text = true;
            }
            if (op.kind == UiHdOp::Text) {
                ui_->draw_text(batch.texts[op.index]);
            } else {
                ui_->draw_key(batch.keys[op.index]);
            }
            continue;
        }
        end_text();
        const UiHdImage& im = batch.images[op.index];
        if (!images.quads2d.empty() && std::memcmp(images.clip2d, im.clip, sizeof(im.clip)) != 0) flush_images();
        std::memcpy(images.clip2d, im.clip, sizeof(im.clip));
        Quad2D q;
        q.x0 = im.x0;
        q.y0 = im.y0;
        q.x1 = im.x1;
        q.y1 = im.y1;
        q.u0 = im.u0;
        q.v0 = im.v0;
        q.u1 = im.u1;
        q.v1 = im.v1;
        std::memcpy(q.rgba, im.rgba, 4);
        q.texture = im.texture;
        q.alpha = im.alpha;
        q.linear = im.linear;
        q.smooth = im.linear;
        if (im.asset >= 0) {
            q.srv = ui_->asset(im.asset, im.asset_w, im.asset_h);
            if (q.srv == nullptr) {  // sem a substituta: a textura original, com o ST original
                q.u0 = im.orig_uv[0];
                q.v0 = im.orig_uv[1];
                q.u1 = im.orig_uv[2];
                q.v1 = im.orig_uv[3];
            }
        }
        images.quads2d.push_back(q);
    }
    flush_images();
    end_text();
}

void Renderer::set_ui_hd(const UiHdConfig* config, const std::wstring& dir) {
    if (config == nullptr) {
        ui_.reset();
        return;
    }
    if (ui_ == nullptr) {
        ui_ = std::make_unique<UiHdDevice>(device_.Get(), ctx_.Get());
        ui_->set_target(target_.Get());
    }
    ui_->configure(config, dir);
}

bool Renderer::set_modern_ui(const std::wstring& dir) {
    if (modern_ == nullptr) {
        modern_ = std::make_unique<UiModernDevice>(device_.Get(), ctx_.Get());
        modern_->set_target(target_.Get());
    }
    modern_->configure(dir);
    return modern_->ok();
}

bool Renderer::modern_ok() const { return modern_ != nullptr && modern_->ok(); }

float Renderer::modern_measure(const std::wstring& text, const ui::TextStyle& style) const {
    return modern_ != nullptr ? modern_->measure(text, style) : ui::estimate_width(text, style);
}

std::string Renderer::modern_describe() const { return modern_ != nullptr ? modern_->describe() : "desligado"; }

void Renderer::draw_modern(const ui::DrawList& list, const ui::Canvas& canvas) {
    if (modern_ == nullptr || !modern_->ok()) return;
    gpu_phase(GpuPhase::Modern);
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
    modern_->draw(list, canvas);
    shader_state_ = -1;
}

void Renderer::ensure_blur_targets() {
    const int qw = std::max(1, width_ / 4), qh = std::max(1, height_ / 4);
    if (blur_full_ == nullptr) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = UINT(width_);
        td.Height = UINT(height_);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        check(device_->CreateTexture2D(&td, nullptr, &blur_full_), "desfoque: cópia");
        check(device_->CreateShaderResourceView(blur_full_.Get(), nullptr, &blur_full_srv_), "desfoque: cópia");
    }
    if (blur_w_ == qw && blur_h_ == qh && blur_tex_[0] != nullptr) return;
    blur_w_ = qw;
    blur_h_ = qh;
    for (int k = 0; k < 2; ++k) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = UINT(qw);
        td.Height = UINT(qh);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        check(device_->CreateTexture2D(&td, nullptr, &blur_tex_[k]), "desfoque");
        check(device_->CreateRenderTargetView(blur_tex_[k].Get(), nullptr, &blur_rtv_[k]), "desfoque");
        check(device_->CreateShaderResourceView(blur_tex_[k].Get(), nullptr, &blur_srv_[k]), "desfoque");
    }
}

void Renderer::blur_background(float amount) {
    if (amount <= 0.001f || target_ == nullptr) return;
    gpu_phase(GpuPhase::Blur);
    ensure_blur_targets();
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
    ctx_->CopyResource(blur_full_.Get(), target_.Get());
    ctx_->RSSetState(raster_solid_.Get());
    ctx_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx_->OMSetDepthStencilState(no_depth_.Get(), 0);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(fog_vs_.Get(), nullptr, 0);
    ctx_->PSSetSamplers(0, 1, sampler_linear_.GetAddressOf());
    ID3D11ShaderResourceView* none[2] = {nullptr, nullptr};
    // 1/4: a média de 4 x 4 texels (ps_down).
    D3D11_MAPPED_SUBRESOURCE map;
    check(ctx_->Map(cb_down_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map), "Map");
    DownCB d{};
    d.src_size[0] = float(width_);
    d.src_size[1] = float(height_);
    d.ratio[0] = float(width_) / float(blur_w_);
    d.ratio[1] = float(height_) / float(blur_h_);
    d.taps = 4.0f;
    std::memcpy(map.pData, &d, sizeof(d));
    ctx_->Unmap(cb_down_.Get(), 0);
    ID3D11Buffer* cbd = cb_down_.Get();
    ctx_->PSSetConstantBuffers(4, 1, &cbd);
    const D3D11_VIEWPORT qvp{0, 0, float(blur_w_), float(blur_h_), 0, 1};
    ctx_->RSSetViewports(1, &qvp);
    ctx_->OMSetRenderTargets(1, blur_rtv_[0].GetAddressOf(), nullptr);
    ctx_->PSSetShader(down_ps_.Get(), nullptr, 0);
    ctx_->PSSetShaderResources(0, 1, blur_full_srv_.GetAddressOf());
    ctx_->Draw(3, 0);
    // A gaussiana: horizontal e vertical, duas vezes (a segunda mais espaçada).
    ID3D11Buffer* cbb = cb_blur_.Get();
    ctx_->PSSetConstantBuffers(5, 1, &cbb);
    ctx_->PSSetShader(blur_ps_.Get(), nullptr, 0);
    const float spacing[2] = {1.0f, 2.2f};
    for (int pass = 0; pass < 2; ++pass) {
        for (int axis = 0; axis < 2; ++axis) {
            BlurCB b{};
            b.texel[0] = 1.0f / float(blur_w_);
            b.texel[1] = 1.0f / float(blur_h_);
            b.dir[0] = axis == 0 ? spacing[pass] : 0.0f;
            b.dir[1] = axis == 1 ? spacing[pass] : 0.0f;
            check(ctx_->Map(cb_blur_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map), "Map");
            std::memcpy(map.pData, &b, sizeof(b));
            ctx_->Unmap(cb_blur_.Get(), 0);
            const int from = axis == 0 ? 0 : 1, to = 1 - from;
            ctx_->PSSetShaderResources(0, 2, none);
            ctx_->OMSetRenderTargets(1, blur_rtv_[to].GetAddressOf(), nullptr);
            ctx_->PSSetShaderResources(0, 1, blur_srv_[from].GetAddressOf());
            ctx_->Draw(3, 0);
        }
    }
    // A mistura no alvo.
    BlurCB b{};
    b.texel[0] = 1.0f / float(width_);
    b.texel[1] = 1.0f / float(height_);
    b.amount = std::clamp(amount, 0.0f, 1.0f);
    check(ctx_->Map(cb_blur_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map), "Map");
    std::memcpy(map.pData, &b, sizeof(b));
    ctx_->Unmap(cb_blur_.Get(), 0);
    ctx_->PSSetShaderResources(0, 2, none);
    const D3D11_VIEWPORT vp{0, 0, float(width_), float(height_), 0, 1};
    ctx_->RSSetViewports(1, &vp);
    ctx_->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);
    ID3D11ShaderResourceView* srvs[2] = {blur_full_srv_.Get(), blur_srv_[0].Get()};
    ctx_->PSSetShaderResources(0, 2, srvs);
    ctx_->PSSetShader(blur_mix_ps_.Get(), nullptr, 0);
    ctx_->Draw(3, 0);
    ctx_->PSSetShaderResources(0, 2, none);
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
    shader_state_ = -1;
}

std::string Renderer::ui_hd_describe() const { return ui_ != nullptr ? ui_->describe() : "desligada"; }

float Renderer::ui_hd_measure(const std::wstring& label, float cap_px) const {
    return ui_ != nullptr ? ui_->measure(label, cap_px) : 0.0f;
}

bool Renderer::ui_hd_ok() const { return ui_ != nullptr && ui_->ok(); }

bool Renderer::read_texture(int slot, int& width, int& height, std::vector<uint8_t>& rgba) {
    if (slot < 0 || slot >= int(textures_.size()) || textures_[size_t(slot)] == nullptr) return false;
    Com<ID3D11Resource> res;
    textures_[size_t(slot)]->GetResource(&res);
    Com<ID3D11Texture2D> tex;
    if (FAILED(res.As(&tex))) return false;
    D3D11_TEXTURE2D_DESC td;
    tex->GetDesc(&td);
    D3D11_TEXTURE2D_DESC sd = td;
    sd.MipLevels = 1;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;
    Com<ID3D11Texture2D> staging;
    if (FAILED(device_->CreateTexture2D(&sd, nullptr, &staging))) return false;
    ctx_->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, tex.Get(), 0, nullptr);
    D3D11_MAPPED_SUBRESOURCE map;
    if (FAILED(ctx_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;
    width = int(td.Width);
    height = int(td.Height);
    rgba.resize(size_t(width) * size_t(height) * 4);
    for (int y = 0; y < height; ++y) {
        std::memcpy(&rgba[size_t(y) * size_t(width) * 4], static_cast<const uint8_t*>(map.pData) + size_t(y) * map.RowPitch,
                    size_t(width) * 4);
    }
    ctx_->Unmap(staging.Get(), 0);
    return true;
}

bool Renderer::write_png(const std::wstring& path, int width, int height, const uint8_t* rgba) {
    if (width <= 0 || height <= 0) return false;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Com<IWICImagingFactory> wic;
    Com<IWICStream> stream;
    Com<IWICBitmapEncoder> enc;
    Com<IWICBitmapFrameEncode> fr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) ||
        FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache)) || FAILED(enc->CreateNewFrame(&fr, nullptr)) ||
        FAILED(fr->Initialize(nullptr)) || FAILED(fr->SetSize(UINT(width), UINT(height)))) {
        return false;
    }
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppRGBA;
    if (FAILED(fr->SetPixelFormat(&fmt))) return false;
    std::vector<uint8_t> pixels(rgba, rgba + size_t(width) * size_t(height) * 4);
    if (fmt != GUID_WICPixelFormat32bppRGBA) {  // o codificador pediu BGRA
        for (size_t i = 0; i < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);
    }
    return SUCCEEDED(fr->WritePixels(UINT(height), UINT(width) * 4, UINT(pixels.size()), pixels.data())) &&
           SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit());
}

// Passada de névoa (psiDrawFogPass): um triângulo que cobre a tela lê a profundidade de cada pixel e
// mistura a cor da névoa como o sprite do PS2 (ps_fog). A profundidade sai do alvo enquanto é lida.
void Renderer::draw_fog(const DrawItem& inst, const FrameParams& frame) {
    D3D11_MAPPED_SUBRESOURCE map;
    check(ctx_->Map(cb_fog_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map), "Map");
    FogCB f{};
    for (int k = 0; k < 3; ++k) f.color[k] = float(inst.fog_rgb[k]) / 255.0f;
    f.color[3] = 1.0f;
    vu1::screen_z_coefficients(inst.fog_far1, f.a, f.b);  // Z do GS = A / z + B
    f.depth_nf = frame.z_near * frame.z_far;
    f.depth_fmn = frame.z_far - frame.z_near;
    f.depth_n = frame.z_near;
    std::memcpy(map.pData, &f, sizeof(f));
    ctx_->Unmap(cb_fog_.Get(), 0);

    ID3D11RenderTargetView* rtv = scene_rtv();
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    ctx_->OMSetBlendState(fog_blend_.Get(), nullptr, 0xffffffff);
    ctx_->OMSetDepthStencilState(no_depth_.Get(), 0);
    ctx_->IASetInputLayout(nullptr);
    ctx_->VSSetShader(fog_vs_.Get(), nullptr, 0);
    // Com MSAA (só do PC), por amostra: a profundidade multiamostrada vai no t2.
    const bool ms = scene_msaa_ > 1;
    ctx_->PSSetShader(ms ? fog_ps_ms_.Get() : fog_ps_.Get(), nullptr, 0);
    ID3D11Buffer* cb = cb_fog_.Get();
    ctx_->PSSetConstantBuffers(3, 1, &cb);
    ctx_->PSSetShaderResources(ms ? 2 : 1, 1, depth_srv_.GetAddressOf());
    ctx_->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    ctx_->PSSetShaderResources(ms ? 2 : 1, 1, &none);
    ctx_->OMSetRenderTargets(1, &rtv, dsv_.Get());
    ctx_->PSSetShader(ps_.Get(), nullptr, 0);
    a2c_bound_ = false;
    shader_state_ = -1;  // o próximo desenho volta o input layout e o vertex shader
}

void Renderer::present() {
    if (!offscreen_) {
        // Sem VSync e fora da tela cheia exclusiva (onde o DXGI recusa a flag), sem esperar o monitor.
        const UINT flags = !vsync_ && tearing_ && !exclusive_fullscreen() ? DXGI_PRESENT_ALLOW_TEARING : 0;
        if (swap_->Present(vsync_ ? 1 : 0, flags) == DXGI_STATUS_OCCLUDED) ++counters_.occluded;
    }
    if (ui_ != nullptr) ui_->end_frame();
    // Só do port (--bench fora da tela): sem Present, a CPU seguraria a GPU só pelo tamanho do buffer de comandos;
    // como a fila de uma cadeia de troca, espera o quadro de dois quadros atrás acabar.
    if (offscreen_ && throttle_) {
        Com<ID3D11Query>& q = throttle_q_[throttle_i_];
        if (q != nullptr) {
            BOOL done = FALSE;
            while (ctx_->GetData(q.Get(), &done, sizeof(done), 0) == S_FALSE) YieldProcessor();
        } else {
            const D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0};
            device_->CreateQuery(&qd, &q);
        }
        if (q != nullptr) ctx_->End(q.Get());
        ctx_->Flush();
        throttle_i_ = (throttle_i_ + 1) % 3;
    }
}

// ---- as medidas do --bench (só do port) ----

const char* gpu_phase_name(GpuPhase p) {
    switch (p) {
        case GpuPhase::Scene: return "cena";
        case GpuPhase::Fog: return "nevoa";
        case GpuPhase::Resolve: return "resolucao";
        case GpuPhase::TwoD: return "2d";
        case GpuPhase::Blur: return "desfoque";
        case GpuPhase::Modern: return "direct2d";
        default: return "?";
    }
}

void Renderer::set_profiling(bool on) {
    profiling_ = on;
    if (!on) gpu_cur_ = -1;
}

void Renderer::gpu_stamp(GpuPhase p) {
    if (gpu_cur_ < 0) return;
    GpuFrame& f = gpu_ring_[gpu_cur_];
    if (f.used >= 64) return;
    if (f.used >= int(f.stamps.size())) {
        Com<ID3D11Query> q;
        const D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP, 0};
        if (FAILED(device_->CreateQuery(&qd, &q))) return;
        f.stamps.push_back(q);
        f.phases.push_back(p);
    }
    f.phases[size_t(f.used)] = p;
    ctx_->End(f.stamps[size_t(f.used)].Get());
    ++f.used;
}

void Renderer::gpu_phase(GpuPhase p) {
    if (!profiling_ || gpu_cur_ < 0 || p == phase_) return;
    gpu_stamp(phase_);  // o fim do trecho anterior
    phase_ = p;
}

void Renderer::gpu_frame_begin() {
    if (!profiling_) return;
    // O quadro do anel que vai ser reusado: lido antes (esperando só por ele, se ainda não acabou).
    GpuFrame& f = gpu_ring_[gpu_next_];
    if (f.pending) gpu_read(f, true);
    if (f.disjoint == nullptr) {
        const D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        if (FAILED(device_->CreateQuery(&qd, &f.disjoint))) return;
    }
    gpu_cur_ = gpu_next_;
    gpu_next_ = (gpu_next_ + 1) % kGpuRing;
    f.used = 0;
    f.seq = gpu_seq_++;
    ctx_->Begin(f.disjoint.Get());
    phase_ = GpuPhase::Scene;
    gpu_stamp(GpuPhase::Scene);  // o começo
}

void Renderer::gpu_frame_end() {
    if (!profiling_ || gpu_cur_ < 0) return;
    GpuFrame& f = gpu_ring_[gpu_cur_];
    gpu_stamp(phase_);
    ctx_->End(f.disjoint.Get());
    f.pending = true;
    gpu_cur_ = -1;
    gpu_collect(false);
}

// Lê um quadro do anel: falso se ainda não acabou (sem wait). Os tempos vão para gpu_times_.
bool Renderer::gpu_read(GpuFrame& f, bool wait) {
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
    HRESULT hr;
    while ((hr = ctx_->GetData(f.disjoint.Get(), &dj, sizeof(dj), wait ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE &&
           wait) {
        YieldProcessor();
    }
    if (hr != S_OK) return false;
    std::vector<uint64_t> t(size_t(f.used), 0);
    bool ok = true;
    for (int i = 0; i < f.used && ok; ++i) {
        while ((hr = ctx_->GetData(f.stamps[size_t(i)].Get(), &t[size_t(i)], sizeof(uint64_t), 0)) == S_FALSE) {
            YieldProcessor();
        }
        ok = hr == S_OK;
    }
    f.pending = false;
    if (!ok || dj.Disjoint || dj.Frequency == 0 || f.used < 2) {
        ++gpu_times_.disjoint;
        return true;
    }
    const double to_ms = 1000.0 / double(dj.Frequency);
    const double total = double(t[size_t(f.used - 1)] - t[0]) * to_ms;
    gpu_times_.total_ms += total;
    gpu_times_.max_ms = std::max(gpu_times_.max_ms, total);
    GpuFrameTimes per{};
    per[0] = float(total);
    for (int i = 1; i < f.used; ++i) {
        const double ms = double(t[size_t(i)] - t[size_t(i - 1)]) * to_ms;
        gpu_times_.phase_ms[int(f.phases[size_t(i)])] += ms;
        per[size_t(int(f.phases[size_t(i)]) + 1)] += float(ms);
    }
    ++gpu_times_.frames;
    if (f.seq < 1000000) {
        if (gpu_per_frame_.size() <= f.seq) {
            GpuFrameTimes none;
            none.fill(-1.0f);
            gpu_per_frame_.resize(size_t(f.seq) + 1, none);
        }
        gpu_per_frame_[size_t(f.seq)] = per;
    }
    return true;
}

void Renderer::gpu_collect(bool wait) {
    // Os quadros prontos, do mais velho para o mais novo (gpu_next_ é o mais velho do anel); sem wait, para no
    // primeiro que ainda não acabou (os mais novos também não acabaram).
    for (int k = 0; k < kGpuRing; ++k) {
        const int idx = (gpu_next_ + k) % kGpuRing;
        GpuFrame& f = gpu_ring_[idx];
        if (!f.pending || idx == gpu_cur_) continue;
        if (!gpu_read(f, wait)) return;
    }
}

void Renderer::clear_target(const float rgba[4]) { ctx_->ClearRenderTargetView(rtv_.Get(), rgba); }

void Renderer::screen_blur(float keep) {
    if (!(keep > 0.0f) || target_ == nullptr) {
        blur_history_valid_ = false;
        return;
    }
    D3D11_TEXTURE2D_DESC td;
    target_->GetDesc(&td);
    D3D11_TEXTURE2D_DESC have{};
    if (blur_history_ != nullptr) blur_history_->GetDesc(&have);
    if (blur_history_ == nullptr || have.Width != td.Width || have.Height != td.Height || have.Format != td.Format) {
        blur_history_.Reset();
        blur_history_srv_.Reset();
        blur_history_valid_ = false;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = 0;
        td.MiscFlags = 0;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.SampleDesc.Count = 1;
        td.SampleDesc.Quality = 0;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &blur_history_))) return;
        if (FAILED(device_->CreateShaderResourceView(blur_history_.Get(), nullptr, &blur_history_srv_))) return;
    }
    if (blur_history_blend_ == nullptr) {
        D3D11_BLEND_DESC bd{};
        auto& rt = bd.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D11_BLEND_BLEND_FACTOR;      // o anterior x keep
        rt.DestBlend = D3D11_BLEND_INV_BLEND_FACTOR;  // o atual x (1 - keep)
        rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ZERO;
        rt.DestBlendAlpha = D3D11_BLEND_ONE;
        rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device_->CreateBlendState(&bd, &blur_history_blend_))) return;
    }
    if (blur_history_valid_) {
        gpu_phase(GpuPhase::Blur);
        const float k = std::clamp(keep, 0.0f, 1.0f);
        const float factor[4] = {k, k, k, k};
        ctx_->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);
        const D3D11_VIEWPORT vp{0, 0, float(width_), float(height_), 0, 1};
        ctx_->RSSetViewports(1, &vp);
        ctx_->RSSetState(raster_solid_.Get());
        ctx_->OMSetBlendState(blur_history_blend_.Get(), factor, 0xffffffff);
        ctx_->OMSetDepthStencilState(no_depth_.Get(), 0);
        ctx_->IASetInputLayout(nullptr);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->VSSetShader(blit_vs_.Get(), nullptr, 0);
        ctx_->PSSetShader(blit_ps_.Get(), nullptr, 0);
        ctx_->PSSetShaderResources(0, 1, blur_history_srv_.GetAddressOf());
        ctx_->Draw(3, 0);
        ++counters_.draws;
        ID3D11ShaderResourceView* none = nullptr;
        ctx_->PSSetShaderResources(0, 1, &none);
        ctx_->OMSetRenderTargets(0, nullptr, nullptr);
        shader_state_ = -1;
    }
    ctx_->CopyResource(blur_history_.Get(), target_.Get());
    blur_history_valid_ = true;
}

void Renderer::save_loading_snapshot() {
    D3D11_TEXTURE2D_DESC td;
    target_->GetDesc(&td);
    D3D11_TEXTURE2D_DESC have{};
    if (loading_snapshot_ != nullptr) loading_snapshot_->GetDesc(&have);
    if (loading_snapshot_ == nullptr || have.Width != td.Width || have.Height != td.Height || have.Format != td.Format) {
        loading_snapshot_.Reset();
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = 0;
        td.MiscFlags = 0;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.SampleDesc.Count = 1;
        td.SampleDesc.Quality = 0;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &loading_snapshot_))) return;
    }
    ctx_->CopyResource(loading_snapshot_.Get(), target_.Get());
}

bool Renderer::restore_loading_snapshot() {
    if (loading_snapshot_ == nullptr) return false;
    D3D11_TEXTURE2D_DESC a, b;
    loading_snapshot_->GetDesc(&a);
    target_->GetDesc(&b);
    if (a.Width != b.Width || a.Height != b.Height || a.Format != b.Format) return false;  // a janela mudou
    ctx_->CopyResource(target_.Get(), loading_snapshot_.Get());
    return true;
}

void Renderer::release_loading_snapshot() { loading_snapshot_.Reset(); }

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Renderer::keep_texture(int slot) const {
    if (slot < 0) return nullptr;
    if (use_hd_ && slot < int(hd_textures_.size()) && hd_textures_[size_t(slot)] != nullptr) return hd_textures_[size_t(slot)];
    if (slot < int(textures_.size())) return textures_[size_t(slot)];
    return nullptr;
}

void Renderer::save_png(const std::wstring& path) {
    Com<ID3D11Texture2D> back = target_;
    D3D11_TEXTURE2D_DESC td;
    back->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    Com<ID3D11Texture2D> staging;
    check(device_->CreateTexture2D(&td, nullptr, &staging), "textura de leitura");
    ctx_->CopyResource(staging.Get(), back.Get());
    D3D11_MAPPED_SUBRESOURCE map;
    check(ctx_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map), "Map");

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Com<IWICImagingFactory> wic;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)), "WIC");
    Com<IWICStream> stream;
    check(wic->CreateStream(&stream), "WIC stream");
    check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "arquivo PNG");
    Com<IWICBitmapEncoder> enc;
    check(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc), "encoder PNG");
    check(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache), "encoder");
    Com<IWICBitmapFrameEncode> fr;
    check(enc->CreateNewFrame(&fr, nullptr), "frame");
    check(fr->Initialize(nullptr), "frame");
    check(fr->SetSize(td.Width, td.Height), "tamanho");
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    check(fr->SetPixelFormat(&fmt), "formato");
    // Força alfa opaco no arquivo.
    std::vector<uint8_t> pixels(size_t(td.Width) * td.Height * 4);
    for (UINT y = 0; y < td.Height; ++y) {
        std::memcpy(&pixels[size_t(y) * td.Width * 4], static_cast<uint8_t*>(map.pData) + size_t(y) * map.RowPitch,
                    size_t(td.Width) * 4);
    }
    for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    check(fr->WritePixels(td.Height, td.Width * 4, UINT(pixels.size()), pixels.data()), "WritePixels");
    check(fr->Commit(), "Commit");
    check(enc->Commit(), "Commit");
    ctx_->Unmap(staging.Get(), 0);
}

}  // namespace nf
