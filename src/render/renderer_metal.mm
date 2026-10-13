// O renderizador Metal: o backend do macOS de render/renderer.h (o Renderer::Impl e, no fim, os repasses da fachada).
// Compilado com ARC (-fobjc-arc). A especificação é o backend do Direct3D 11 (render/renderer_d3d11.cpp, a referência):
// as mesmas contas nos shaders (aqui em MSL, compilados na partida com newLibraryWithSource), a mesma resolução do estado
// do GS, a mesma ordem dos desenhos e as mesmas opções do PC. O mapa do que mudou de uma API para a outra e o que diverge
// está em docs/port.md, "macOS: o renderizador Metal"; o teste de conformidade (tests/render_conformance.cpp,
// nf_render_check) desenha cenas sintéticas pelos dois e compara.
//
// O quadro: tudo é desenhado num alvo próprio (target, BGRA8 do tamanho da janela, ou da captura com "offscreen"), que
// faz o papel do back buffer do Direct3D 11 e guarda o quadro entre as chamadas (save_png lê dele, o rastro da tela e a
// tela de carga copiam dele). present() copia o alvo para o drawable da CAMetalLayer da NSView (Window::native_handle) e o
// apresenta; fora da tela não há drawable. Os comandos de um quadro vão num command buffer só, aberto no primeiro desenho
// e enviado no present() (ou antes, quando save_png precisa ler o alvo). Um encoder de render fica aberto enquanto os
// desenhos seguidos usam os mesmos anexos (a cor e a profundidade): a cena, a passada de névoa (sem a profundidade, que
// ela lê), a limpeza do Z do meio do quadro (um encoder novo com a profundidade limpa: o loadAction do Metal no lugar do
// ClearDepthStencilView) e o 2D no alvo trocam de encoder.
//
// Os recursos: as texturas privadas da GPU (MTLStorageModePrivate), enviadas por um command buffer de envio (cópias de
// buffers de passagem e a geração dos mipmaps), que sai antes do command buffer do quadro: o que é criado na carga, as
// substitutas HD, o quadro do filme e as imagens do port chegam antes do desenho que as usa. Os dados de cada quadro (os
// vértices dos sprites, dos estilhaços e do 2D, as paletas do skinning, as imagens dos menus Modern e da camada HD) vão
// em pedaços de buffers compartilhados que voltam ao uso quando a GPU acaba o quadro. As malhas em buffers estáticos
// (compartilhados com a memória unificada, "managed" nas placas dedicadas).
//
// O estado: os pipelines (o shader, a mistura do GS, a cobertura, as amostras e a profundidade do encoder) e os depth
// states em cache por combinação, criados no primeiro uso; nada é alocado por desenho além do pedaço dos dados do quadro.
//
// A thread: o jogo chama o renderizador da thread dele (a da pilha da arena; platform/main_thread_macos.h). O Metal
// desenha de qualquer thread; só a configuração da CAMetalLayer (o dispositivo, o formato, o VSync) vai à thread
// principal pelo run_on_main. Cada chamada da fachada roda dentro de um @autoreleasepool (a thread do jogo não tem um).
//
// O 2D vetorial (os menus Modern, os textos e as teclas da camada HD, as substitutas SVG) é a UiVectorCanvas
// (render/ui_vector.h): ela devolve imagens BGRA com o alfa pré-multiplicado que este renderizador compõe por cima do alvo
// no ponto em que o Direct2D desenha no Windows (no macOS, render/ui_vector_cg.mm: o CoreGraphics e o CoreText). Sem ela
// (create_ui_vector_canvas nula) o jogo fica com os menus Original.

#include "render/renderer.h"

// Sem os check(), verify() e require() antigos do AssertMacros.h (o AppKit o traz).
#define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

#include "platform/display_modes.h"
#include "platform/image_io.h"
#include "platform/main_thread_macos.h"
#include "platform/os.h"
#include "platform/vu1.h"
#include "render/gpu_adapters.h"
#include "render/gpu_adapters_metal.h"
#include "render/remix_d3d9.h"
#include "render/ui_hd.h"
#include "render/ui_vector.h"
#include "ui/modern_ui.h"

namespace nf {

namespace {

// Os shaders: os HLSL de renderer_d3d11.cpp e movie_layer_d3d11.cpp em MSL, com as mesmas contas na mesma ordem. As
// matrizes do motor (vetor-linha, v' = v * M) chegam como estão: o MSL lê a memória por colunas, então a matriz do shader
// é a transposta e "M * v" no MSL é "mul(v, M)" no HLSL. round() do HLSL arredonda para o par mais próximo: rint().
const char kMsl[] = R"(
#include <metal_stdlib>
using namespace metal;

struct FrameU {
    float4x4 view_proj;
    packed_float3 light_dir;   // só no sombreamento de depuração (tecla V)
    float ambient;
    float debug_shading;
    float lights_on;           // 0: as rotinas de luz da VU1 viram as sem luz (--no-lights)
    float2 pad;
};

struct ObjectU {
    float4x4 world;
    float4 tint;               // TAmbient: tweak / 255 (r, g, b, a); com luzes, o termo ambiente
    float4 light_pos[2];       // luzes da VU1 no espaço dos vértices (rígida) ou no mundo (skinning)
    float4 light_col[2];       // TColor0/1: r, g, b (0..255 x intensidade), w = 1 / raio²
    float4 env_s;              // linhas 0 e 1 da câmera (mapa de ambiente, rotina 0x2e0)
    float4 env_t;
    float num_lights;
    float env;                 // 1: rotina 0x2e0 (ST pela normal, sem luzes)
    float premult;             // 1: a cor sai multiplicada por As (a mistura usa ONE no lugar de As)
    float sprite;              // 1: sprite de partículas (cor na escala do GS, sem rotina da VU1)
    float atst;                // teste de alfa do GS (ATST 0..7); -1: sem teste
    float aref;                // AREF, na escala do GS (As = 1.0 vale 128)
    float aint;                // 1: compara o alfa inteiro, como o GS; 0: o valor contínuo (regra antiga)
    float screen;              // 1: 2D, a posição já está na tela (x, y em -1..1)
};

struct MorphU {
    float4 w[22];              // kMaxMorphTargets; o peso do alvo k em w[k].x
    uint n;                    // alvos
    uint count;                // elementos do buffer dos deltas (fora dele, 0: como a leitura fora de um buffer no D3D11)
    uint2 pad;
};

struct VIn {
    float3 pos [[attribute(0)]];
    float3 nrm [[attribute(1)]];
    float2 uv [[attribute(2)]];
    float4 col [[attribute(3)]];
};

struct VSkinIn {
    float3 pos [[attribute(0)]];
    float3 nrm [[attribute(1)]];
    float2 uv [[attribute(2)]];
    float4 col [[attribute(3)]];
    uchar4 bone [[attribute(4)]];
    float weight [[attribute(5)]];
};

struct VMorphIn {
    float3 pos [[attribute(0)]];
    float3 nrm [[attribute(1)]];
    float2 uv [[attribute(2)]];
    float4 col [[attribute(3)]];
    uchar4 bone [[attribute(4)]];
    float weight [[attribute(5)]];
    int morph [[attribute(6)]];
};

struct PIn {
    float4 pos [[position]];
    float3 nrm;
    float2 uv;
    float4 col;                // cor do vértice depois da VU1, com 0x80 = 1.0
};

float3x3 upper3(float4x4 m) { return float3x3(m[0].xyz, m[1].xyz, m[2].xyz); }

// A rotina da VU1 (vu1::tweak_vertex, vu1::light_vertex): sem luzes a cor vezes TAmbient; com luzes (0x340)
// TAmbient * cor + TColor1 * s1 + TColor0 * s0, saturado em 255, s = max(0, max(0, 1 / |D|² - 1 / raio²) * (n . D)).
float4 vu1_colour(constant FrameU& f, constant ObjectU& o, float4 c, float3 n, float3 p) {
    float4 r = o.tint * c;
    if (o.num_lights > 0.5f && o.env < 0.5f && f.lights_on > 0.5f) {
        float3 d0 = o.light_pos[0].xyz - p;
        float3 d1 = o.light_pos[1].xyz - p;
        float s0 = max(0.0f, max(0.0f, 1.0f / dot(d0, d0) - o.light_col[0].w) * dot(n, d0));
        float s1 = max(0.0f, max(0.0f, 1.0f / dot(d1, d1) - o.light_col[1].w) * dot(n, d1));
        r.rgb = min(r.rgb + o.light_col[1].rgb * s1 + o.light_col[0].rgb * s0, 255.0f);
        r.a = min(r.a, 255.0f);
    }
    return floor(r);
}

// Rotina 0x2e0: ST = 0.5 + (n . linha da câmera) / 256, com n inteiro (vu1::env_st).
float2 vu1_env_st(constant ObjectU& o, float3 nvu) {
    return 0.5f + float2(dot(nvu, o.env_s.xyz), dot(nvu, o.env_t.xyz)) / 256.0f;
}

vertex PIn vs_main(VIn i [[stage_in]], constant FrameU& f [[buffer(1)]], constant ObjectU& o [[buffer(2)]]) {
    PIn r;
    float4 w = o.world * float4(i.pos, 1.0f);
    r.pos = f.view_proj * w;
    r.nrm = upper3(o.world) * i.nrm;
    r.uv = i.uv;
    float4 c = rint(i.col * 255.0f);  // escala do GS, como no disco
    if (o.screen > 0.5f) {
        r.pos = float4(i.pos.xy, 0.0f, 1.0f);
        r.nrm = float3(0.0f, 0.0f, 1.0f);
        r.col = c / 128.0f;
        return r;
    }
    if (o.sprite > 0.5f) {
        r.col = c / 128.0f;
        return r;
    }
    // Malha rígida: a normal guardada é o inteiro do UNPACK / 128.
    if (o.env > 0.5f) r.uv = vu1_env_st(o, i.nrm * 128.0f);
    r.col = vu1_colour(f, o, c, i.nrm, i.pos) / 128.0f;
    return r;
}

// Skinning (o SkinIt da VU0): w * (p * M0) + (1 - w) * (p * M1); a normal da VU1 é a do disco * 128 girada só pelo
// primeiro osso e truncada (FTOI0).
vertex PIn vs_skin(VSkinIn i [[stage_in]], constant FrameU& f [[buffer(1)]], constant ObjectU& o [[buffer(2)]],
                   constant float4x4* bones [[buffer(3)]]) {
    PIn r;
    float4 p = float4(i.pos, 1.0f);
    float4x4 m0 = bones[i.bone.x];
    float4x4 m1 = bones[i.bone.y];
    float3 sp = i.weight * (m0 * p).xyz + (1.0f - i.weight) * (m1 * p).xyz;
    float3 sn = i.weight * (upper3(m0) * i.nrm) + (1.0f - i.weight) * (upper3(m1) * i.nrm);
    float4 w = o.world * float4(sp, 1.0f);
    r.pos = f.view_proj * w;
    r.nrm = upper3(o.world) * sn;
    r.uv = i.uv;
    float3 nvu = trunc(upper3(m0) * (i.nrm * 128.0f));
    if (o.env > 0.5f) r.uv = vu1_env_st(o, nvu);
    r.col = vu1_colour(f, o, rint(i.col * 255.0f), nvu / 128.0f, sp) / 128.0f;
    return r;
}

// Rostos: pos += soma em k de peso[k] * delta[m + k] antes do skinning (nf::morph_vertex); um delta fora do buffer vale 0.
vertex PIn vs_skin_morph(VMorphIn i [[stage_in]], constant FrameU& f [[buffer(1)]], constant ObjectU& o [[buffer(2)]],
                         constant float4x4* bones [[buffer(3)]], constant MorphU& mu [[buffer(4)]],
                         device const packed_float3* morph_delta [[buffer(5)]]) {
    PIn r;
    float3 mp = i.pos;
    if (i.morph >= 0) {
        for (uint k = 0; k < mu.n; ++k) {
            uint e = uint(i.morph) + k;
            if (e < mu.count) mp += float3(morph_delta[e]) * mu.w[k].x;
        }
    }
    float4 p = float4(mp, 1.0f);
    float4x4 m0 = bones[i.bone.x];
    float4x4 m1 = bones[i.bone.y];
    float3 sp = i.weight * (m0 * p).xyz + (1.0f - i.weight) * (m1 * p).xyz;
    float3 sn = i.weight * (upper3(m0) * i.nrm) + (1.0f - i.weight) * (upper3(m1) * i.nrm);
    float4 w = o.world * float4(sp, 1.0f);
    r.pos = f.view_proj * w;
    r.nrm = upper3(o.world) * sn;
    r.uv = i.uv;
    float3 nvu = trunc(upper3(m0) * (i.nrm * 128.0f));
    if (o.env > 0.5f) r.uv = vu1_env_st(o, nvu);
    r.col = vu1_colour(f, o, rint(i.col * 255.0f), nvu / 128.0f, sp) / 128.0f;
    return r;
}

// Teste de alfa do GS (TEST_1.ATST) com o alfa a e a referência na escala do GS.
bool alpha_pass(constant ObjectU& o, float a) {
    if (o.aint > 0.5f) a = floor(a + 0.001f);
    int fn = int(o.atst);
    if (fn == 0) return false;
    if (fn == 2) return a < o.aref;
    if (fn == 3) return a <= o.aref;
    if (fn == 4) return a == o.aref;
    if (fn == 5) return a >= o.aref;
    if (fn == 6) return a > o.aref;
    if (fn == 7) return a != o.aref;
    return true;
}

// MODULATE do GS com a cor do vértice: Cf = Ct * Cv / 128 (saturada), Af = At * Av / 128 (até 0xff); As = Af / 128.
fragment float4 ps_main(PIn i [[stage_in]], constant FrameU& f [[buffer(1)]], constant ObjectU& o [[buffer(2)]],
                        texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {
    float4 c = tex.sample(smp, i.uv);
    float a = min(c.a * (255.0f / 128.0f) * i.col.a, 255.0f / 128.0f);
    float3 lit = saturate(c.rgb * i.col.rgb);
    if (f.debug_shading > 0.5f && o.sprite < 0.5f) {
        float ndl = abs(dot(normalize(i.nrm), -normalize(float3(f.light_dir))));
        lit = c.rgb * (f.ambient + (1.0f - f.ambient) * ndl) * o.tint.rgb;
    }
    if (o.atst >= 0.0f && !alpha_pass(o, a * 128.0f)) discard_fragment();
    if (o.premult > 0.5f) lit *= a;
    // O alfa vai para o alvo em todos os desenhos, como no GS (fator Ad das misturas seguintes).
    return float4(lit, saturate(a));
}

// Com MSAA: o alfa para a cobertura em volta do limiar do teste (renderer_d3d11.cpp, ps_a2c).
fragment float4 ps_a2c(PIn i [[stage_in]], constant FrameU& f [[buffer(1)]], constant ObjectU& o [[buffer(2)]],
                       texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {
    float4 c = tex.sample(smp, i.uv);
    float a = min(c.a * (255.0f / 128.0f) * i.col.a, 255.0f / 128.0f);
    float3 lit = saturate(c.rgb * i.col.rgb);
    if (f.debug_shading > 0.5f && o.sprite < 0.5f) {
        float ndl = abs(dot(normalize(i.nrm), -normalize(float3(f.light_dir))));
        lit = c.rgb * (f.ambient + (1.0f - f.ambient) * ndl) * o.tint.rgb;
    }
    float a128 = a * 128.0f;
    float t = o.aref;
    if (o.aint > 0.5f) t += (int(o.atst) == 6 ? 1.0f : 0.0f) - 0.001f;
    float w = max(fwidth(a128), 0.001f);
    float bias = t > 127.0f ? 1.0f : 0.5f;
    float cov = saturate((a128 - t) / w + bias);
    if (cov <= 0.0f) discard_fragment();
    return float4(lit, cov);
}

// Um triângulo que cobre o alvo.
struct FullOut {
    float4 pos [[position]];
};
vertex FullOut vs_full(uint id [[vertex_id]]) {
    float2 p = float2(float((id << 1u) & 2u), float(id & 2u));
    FullOut o;
    o.pos = float4(p * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

// Passada de névoa (psiDrawFogPass): o Z do GS de cada pixel dá o quanto da cor fica (vu1::gs_z, vu1::fog_keep).
struct FogU {
    float4 color;
    float a;
    float b;
    float nf;    // perto * longe da projeção do PC
    float fmn;   // longe - perto
    float n;     // perto
    float pad0;
    float pad1;
    float pad2;
};
float4 fog_of(constant FogU& u, float d) {
    float z = 1.0f;
    if (d > 0.0f) {
        float view_z = u.nf / (d * u.fmn + u.n);  // profundidade invertida
        float s = u.a / view_z + u.b;
        z = s > 0.0f ? fmod(floor(s), 16777216.0f) : 0.0f;
    }
    float keep = z >= 32767.0f ? 1.0f : floor(z / 256.0f) / 128.0f;
    return float4(u.color.rgb, keep);
}
fragment float4 ps_fog(float4 pos [[position]], constant FogU& u [[buffer(0)]], depth2d<float> dt [[texture(1)]]) {
    return fog_of(u, dt.read(uint2(pos.xy)));
}
// Com MSAA: por amostra, com a profundidade de cada uma.
fragment float4 ps_fog_ms(float4 pos [[position]], uint s [[sample_id]], constant FogU& u [[buffer(0)]],
                          depth2d_ms<float> dt [[texture(2)]]) {
    return fog_of(u, dt.read(uint2(pos.xy), s));
}

// Cópias: o texel do pixel (o mesmo tamanho), ou amostrado pelo pixel (as texturas comprimidas).
fragment float4 ps_blit(float4 pos [[position]], texture2d<float> src [[texture(0)]]) {
    return src.read(uint2(pos.xy));
}
fragment float4 ps_blit_sample(float4 pos [[position]], texture2d<float> src [[texture(0)]]) {
    constexpr sampler s(coord::pixel, filter::nearest, address::clamp_to_edge);
    return src.sample(s, pos.xy);
}

// A cena em escala reduzida para a janela (ps_down do D3D11): taps x taps amostras bilineares sobre a área do pixel.
struct DownU {
    float2 src_size;
    float2 ratio;
    float taps;
    float pad0;
    float pad1;
    float pad2;
};
fragment float4 ps_down(float4 pos [[position]], constant DownU& u [[buffer(0)]], texture2d<float> src [[texture(0)]],
                        sampler smp [[sampler(0)]]) {
    float2 start = (pos.xy - 0.5f) * u.ratio;
    float2 step = u.ratio / u.taps;
    int n = int(u.taps);
    float4 sum = float4(0.0f);
    for (int j = 0; j < n; ++j) {
        for (int k = 0; k < n; ++k) {
            float2 t = start + (float2(float(k), float(j)) + 0.5f) * step;
            sum += src.sample(smp, t / u.src_size, level(0.0f));
        }
    }
    return sum / (u.taps * u.taps);
}

// O fundo desfocado dos menus Modern: a gaussiana de 9 amostras em 5 leituras bilineares e a mistura com a imagem nítida.
struct BlurU {
    float2 texel;
    float2 dir;
    float amount;
    float pad0;
    float pad1;
    float pad2;
};
fragment float4 ps_blur(float4 pos [[position]], constant BlurU& u [[buffer(0)]], texture2d<float> src [[texture(0)]],
                        sampler smp [[sampler(0)]]) {
    float2 uv = pos.xy * u.texel;
    float2 d = u.dir * u.texel;
    float4 c = src.sample(smp, uv, level(0.0f)) * 0.2270270270f;
    c += src.sample(smp, uv + d * 1.3846153846f, level(0.0f)) * 0.3162162162f;
    c += src.sample(smp, uv - d * 1.3846153846f, level(0.0f)) * 0.3162162162f;
    c += src.sample(smp, uv + d * 3.2307692308f, level(0.0f)) * 0.0702702703f;
    c += src.sample(smp, uv - d * 3.2307692308f, level(0.0f)) * 0.0702702703f;
    return c;
}
fragment float4 ps_blur_mix(float4 pos [[position]], constant BlurU& u [[buffer(0)]], texture2d<float> src [[texture(0)]],
                            texture2d<float> soft_tex [[texture(1)]], sampler smp [[sampler(0)]]) {
    float4 sharp = src.read(uint2(pos.xy));
    float4 soft = soft_tex.sample(smp, pos.xy * u.texel, level(0.0f));
    return mix(sharp, soft, u.amount);
}

constant float2 kCorner[6] = {float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f),
                              float2(0.0f, 0.0f), float2(1.0f, 1.0f), float2(0.0f, 1.0f)};

// O quadro do filme (movie_layer_d3d11): a imagem no retângulo da tela do PS2 com o Catmull-Rom separável em 9 leituras.
struct MovieU {
    float4 rect;       // x0, y0, x1, y1 em pixels do alvo
    float2 target;     // tamanho do alvo
    float2 tex_size;
};
struct MovieOut {
    float4 pos [[position]];
    float2 uv;
};
vertex MovieOut vs_movie(uint id [[vertex_id]], constant MovieU& u [[buffer(0)]]) {
    float2 c = kCorner[id];
    float2 px = mix(u.rect.xy, u.rect.zw, c);
    MovieOut o;
    o.pos = float4(px.x / u.target.x * 2.0f - 1.0f, 1.0f - px.y / u.target.y * 2.0f, 0.0f, 1.0f);
    o.uv = c;
    return o;
}
fragment float4 ps_movie(MovieOut i [[stage_in]], constant MovieU& u [[buffer(0)]], texture2d<float> img [[texture(0)]],
                         sampler smp [[sampler(0)]]) {
    float2 at = i.uv * u.tex_size;
    float2 c1 = floor(at - 0.5f) + 0.5f;
    float2 f = at - c1;
    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);
    float2 w12 = w1 + w2;
    float2 t0 = (c1 - 1.0f) / u.tex_size;
    float2 t12 = (c1 + w2 / w12) / u.tex_size;
    float2 t3 = (c1 + 2.0f) / u.tex_size;
    float3 c = float3(0.0f);
    c += img.sample(smp, float2(t0.x, t0.y), level(0.0f)).rgb * (w0.x * w0.y);
    c += img.sample(smp, float2(t12.x, t0.y), level(0.0f)).rgb * (w12.x * w0.y);
    c += img.sample(smp, float2(t3.x, t0.y), level(0.0f)).rgb * (w3.x * w0.y);
    c += img.sample(smp, float2(t0.x, t12.y), level(0.0f)).rgb * (w0.x * w12.y);
    c += img.sample(smp, float2(t12.x, t12.y), level(0.0f)).rgb * (w12.x * w12.y);
    c += img.sample(smp, float2(t3.x, t12.y), level(0.0f)).rgb * (w3.x * w12.y);
    c += img.sample(smp, float2(t0.x, t3.y), level(0.0f)).rgb * (w0.x * w3.y);
    c += img.sample(smp, float2(t12.x, t3.y), level(0.0f)).rgb * (w12.x * w3.y);
    c += img.sample(smp, float2(t3.x, t3.y), level(0.0f)).rgb * (w3.x * w3.y);
    return float4(saturate(c), 1.0f);
}

// Uma imagem da UiVectorCanvas (BGRA, alfa pré-multiplicado) lida texel a texel de um buffer, no retângulo dela.
struct RectU {
    float4 rect;       // x0, y0, x1, y1 em pixels do alvo
    float2 target;
    uint pitch;        // pixels por linha no buffer
    uint pad;
};
vertex FullOut vs_rect(uint id [[vertex_id]], constant RectU& u [[buffer(0)]]) {
    float2 px = mix(u.rect.xy, u.rect.zw, kCorner[id]);
    FullOut o;
    o.pos = float4(px.x / u.target.x * 2.0f - 1.0f, 1.0f - px.y / u.target.y * 2.0f, 0.0f, 1.0f);
    return o;
}
fragment float4 ps_composite(float4 pos [[position]], constant RectU& u [[buffer(0)]],
                             device const uchar4* px [[buffer(1)]]) {
    int2 p = int2(pos.xy) - int2(u.rect.xy);
    int w = int(u.rect.z - u.rect.x);
    int h = int(u.rect.w - u.rect.y);
    if (p.x < 0 || p.y < 0 || p.x >= w || p.y >= h) discard_fragment();
    uint idx = uint(clamp(p.y, 0, h - 1)) * u.pitch + uint(clamp(p.x, 0, w - 1));
    float4 c = float4(px[idx]) / 255.0f;
    return c.zyxw;
}
)";

// As constantes dos shaders, no layout dos structs do MSL acima (iguais aos constant buffers do D3D11).
struct FrameU {
    float view_proj[16];
    float light_dir[3];
    float ambient;
    float debug_shading;
    float lights_on;
    float pad[2];
};
static_assert(sizeof(FrameU) == 96);

struct ObjectU {
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
static_assert(sizeof(ObjectU) == 208);

static_assert(kMaxMorphTargets == 22, "o tamanho de w[] no shader acompanha kMaxMorphTargets");
static_assert(kMaxSkinBones == 128, "a paleta do skinning tem até 128 matrizes");
struct MorphU {
    float w[kMaxMorphTargets][4];
    uint32_t n;
    uint32_t count;
    uint32_t pad[2];
};
static_assert(sizeof(MorphU) == 368);

struct FogU {
    float color[4];
    float a, b;
    float nf, fmn, n;
    float pad[3];
};
static_assert(sizeof(FogU) == 48);

struct DownU {
    float src_size[2];
    float ratio[2];
    float taps;
    float pad[3];
};
static_assert(sizeof(DownU) == 32);

struct BlurU {
    float texel[2];
    float dir[2];
    float amount;
    float pad[3];
};
static_assert(sizeof(BlurU) == 32);

struct MovieU {
    float rect[4];
    float target[2];
    float tex_size[2];
};
static_assert(sizeof(MovieU) == 32);

struct RectU {
    float rect[4];
    float target[2];
    uint32_t pitch;
    uint32_t pad;
};
static_assert(sizeof(RectU) == 32);

// Estado efetivo de um desenho: a equação do GS, o teste de alfa e o de Z (resolve_state de renderer_d3d11.cpp).
struct DrawState {
    uint64_t alpha = 0;
    bool abe = true;
    float atst = -1.0f;  // ATST, -1 sem teste
    float aref = 0.0f;
    bool aint = true;
    MTLCompareFunction zfunc = MTLCompareFunctionGreaterEqual;
    bool zwrite = true;
};

// Junta o estado da parte da malha (own), o do desenho (item) e, para o que ninguém escreveu, a regra antiga pelo modo de
// psiSetUpColourBlend: opaco testa alfa em 50% e escreve Z; misturado descarta só alfa < 0.004 e não escreve Z.
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
        if (((test >> 16) & 1) == 0) {
            s.zfunc = MTLCompareFunctionAlways;  // ZTE desligado
        } else {
            switch ((test >> 17) & 3) {
                case 0: s.zfunc = MTLCompareFunctionNever; break;
                case 1: s.zfunc = MTLCompareFunctionAlways; break;
                case 2: s.zfunc = MTLCompareFunctionGreaterEqual; break;  // Z invertido, como no GS
                default: s.zfunc = MTLCompareFunctionGreater; break;
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

MTLBlendFactor mtl_factor(BlendFactor f) {
    switch (f) {
        case BlendFactor::Zero: return MTLBlendFactorZero;
        case BlendFactor::One: return MTLBlendFactorOne;
        case BlendFactor::SrcAlpha: return MTLBlendFactorSourceAlpha;
        case BlendFactor::InvSrcAlpha: return MTLBlendFactorOneMinusSourceAlpha;
        case BlendFactor::DstAlpha: return MTLBlendFactorDestinationAlpha;
        case BlendFactor::InvDstAlpha: return MTLBlendFactorOneMinusDestinationAlpha;
        case BlendFactor::Fix: return MTLBlendFactorBlendColor;
        default: return MTLBlendFactorOneMinusBlendColor;
    }
}

MTLBlendOperation mtl_op(BlendOp op) {
    switch (op) {
        case BlendOp::Subtract: return MTLBlendOperationSubtract;
        case BlendOp::RevSubtract: return MTLBlendOperationReverseSubtract;
        default: return MTLBlendOperationAdd;
    }
}

bool uses_fix(const GsBlendEq& g) {
    const auto fix = [](BlendFactor f) { return f == BlendFactor::Fix || f == BlendFactor::InvFix; };
    return g.enable && (fix(g.src) || fix(g.dst));
}

// Os programas (um par de funções; os três primeiros com o pixel shader da cobertura como variante).
enum Prog : uint32_t {
    kProgMain = 0,       // vs_main + ps_main (ou ps_a2c)
    kProgSkin = 1,       // vs_skin
    kProgMorph = 2,      // vs_skin_morph
    kProgFog = 3,        // vs_full + ps_fog
    kProgFogMs = 4,      // vs_full + ps_fog_ms
    kProgBlit = 5,       // vs_full + ps_blit
    kProgBlitSample = 6, // vs_full + ps_blit_sample
    kProgDown = 7,       // vs_full + ps_down
    kProgBlur = 8,       // vs_full + ps_blur
    kProgBlurMix = 9,    // vs_full + ps_blur_mix
    kProgMovie = 10,     // vs_movie + ps_movie
    kProgComposite = 11, // vs_rect + ps_composite (mistura do alfa pré-multiplicado)
    kProgHistory = 12,   // vs_full + ps_blit com a mistura pela constante (o rastro da tela)
};

// Os pedaços de buffer dos dados de cada quadro: voltam a esta lista quando a GPU acaba o command buffer que os usou.
struct ChunkPool {
    std::mutex m;
    std::vector<id<MTLBuffer>> free;
};

// Os tempos de GPU dos command buffers (o --bench): o número do quadro e os ms, gravados pelo handler de fim.
struct TimeSink {
    std::mutex m;
    std::vector<std::pair<uint64_t, double>> done;  // (época << 40 | quadro, ms)
};

constexpr size_t kChunkBytes = size_t(4) << 20;
constexpr size_t kUploadFlushBytes = size_t(64) << 20;

inline id<MTLTexture> tex_of(GpuImage* p) { return (__bridge id<MTLTexture>)static_cast<void*>(p); }
inline GpuImage* image_of(id<MTLTexture> t) { return static_cast<GpuImage*>((__bridge void*)t); }

uint32_t log2_samples(NSUInteger n) { return n >= 8 ? 3u : n >= 4 ? 2u : n >= 2 ? 1u : 0u; }

}  // namespace

uint64_t gs_alpha_of_blend(Blend mode) {
    // O mesmo de renderer_d3d11.cpp (os números de psiSetUpColourBlend).
    switch (mode) {
        case Blend::Additive: return 0x48;
        case Blend::Alpha: return 0x44;
        case Blend::Subtractive: return 0x42;
        default: return 0x2a;
    }
}

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

// O caminho de Direct3D 9 para o RTX Remix só existe no Windows (render/remix_d3d9.h).
std::unique_ptr<RemixD3D9Renderer> create_remix_d3d9(void*, int, int, bool, const AdapterInfo*) {
    throw std::runtime_error("o caminho de Direct3D 9 para o RTX Remix (--renderer d3d9ff) só existe no Windows");
}

void* create_remix_hidden_window(int, int) { return nullptr; }

struct Renderer::Impl {
    struct GpuMesh {
        id<MTLBuffer> vb, ib;
        std::vector<SubMesh> submeshes;
        bool skinned = false;
        int bones = 0;
        id<MTLBuffer> morph;          // os deltas (float3 sem folga, como o StructuredBuffer<float3>)
        uint32_t morph_elements = 0;
        int morph_targets = 0;
    };
    struct HdAsset {
        int index = -1, w = 0, h = 0;
        id<MTLTexture> tex;
        uint64_t used = 0;
    };
    struct Alloc {
        id<MTLBuffer> buf;
        size_t offset = 0;
        uint8_t* ptr = nullptr;
    };

    Impl(void* win, int width, int height, bool vsync, bool offscreen, const AdapterInfo* want);
    ~Impl();

    // ---- o dispositivo e o estado fixo ----
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLLibrary> library;
    std::unordered_map<std::string, id<MTLFunction>> functions;
    MTLVertexDescriptor* vd_rigid = nil;
    MTLVertexDescriptor* vd_skin = nil;
    MTLVertexDescriptor* vd_morph = nil;
    std::unordered_map<uint64_t, id<MTLRenderPipelineState>> pipelines;
    std::unordered_map<uint32_t, id<MTLDepthStencilState>> depth_states;
    id<MTLDepthStencilState> no_depth;
    id<MTLSamplerState> sampler;          // a cena: o filtro das opções, repetindo
    id<MTLSamplerState> sampler_point;    // 2D: de ponto, nível 0, preso
    id<MTLSamplerState> sampler_linear;   // 2D: bilinear, nível 0, preso (e a redução, o desfoque, o filme)
    id<MTLSamplerState> sampler_smooth;   // camada HD: bilinear com mipmaps
    id<MTLTexture> white, no_texture;
    MTLResourceOptions static_options = MTLResourceStorageModeShared;
    CAMetalLayer* layer = nil;
    void* window = nullptr;
    AdapterInfo adapter_;
    bool adapter_chosen_ = false;
    std::vector<DisplayMode> modes;
    DisplayMode desktop;

    int width_ = 1, height_ = 1;
    bool vsync_ = true;
    bool offscreen_ = false;
    bool exclusive_ = false;
    TextureFilter filter_ = TextureFilter::Aniso8;

    // ---- os alvos ----
    id<MTLTexture> target;          // o quadro (BGRA8, width_ x height_)
    id<MTLTexture> depth;           // a profundidade da cena (D32, com MSAA ou não)
    id<MTLTexture> scene_color;     // a cor própria da cena (com escala ou MSAA); nula: a cena vai direto ao alvo
    id<MTLTexture> scene_resolved;  // com MSAA e escala: a cena resolvida, antes de reduzir
    int scene_w = 0, scene_h = 0, scene_scale = 100, scene_msaa = 1;

    // ---- os recursos da fase e os do port ----
    std::vector<GpuMesh> meshes;
    std::vector<id<MTLTexture>> textures, hd_textures;
    std::vector<std::pair<uint32_t, id<MTLTexture>>> ui_images;
    bool use_hd = true;
    id<MTLBuffer> quad_ib;          // 0 1 2, 0 2 3 por quadrado (sprites e 2D)
    size_t quad_ib_quads = 0;
    id<MTLTexture> blur_full, blur_tex[2];
    int blur_w = 0, blur_h = 0;
    id<MTLTexture> history;         // o quadro anterior (screen_blur)
    bool history_valid = false;
    id<MTLTexture> snapshot;        // a tela de carga
    id<MTLTexture> movie_tex;
    uint64_t movie_serial = ~0ull;
    bool movie_on = false;
    float movie_rect[4]{};

    // ---- o desenho vetorial (render/ui_vector.h) ----
    std::unique_ptr<UiVectorCanvas> canvas;
    bool canvas_tried = false;
    const UiHdConfig* hd_config = nullptr;
    bool hd_on = false;
    std::vector<HdAsset> hd_assets;
    std::vector<uint8_t> hd_failed;
    uint64_t frame_no = 0;

    // ---- a gravação dos comandos ----
    id<MTLCommandBuffer> cmd;                // o do quadro
    id<MTLRenderCommandEncoder> enc;         // o encoder aberto (ou nulo)
    id<MTLTexture> pass_color, pass_depth;   // os anexos dele
    id<MTLRenderPipelineState> cur_pipe;
    id<MTLDepthStencilState> cur_depth;
    bool cur_lines = false;
    bool cur_scissor_full = true;
    MTLScissorRect cur_scissor{0, 0, 0, 0};
    FrameU frame_u{};
    bool wireframe = false;
    bool in_2d = false;
    id<MTLCommandBuffer> up_cmd;             // o de envio (texturas, mipmaps)
    id<MTLBlitCommandEncoder> up_blit;
    size_t up_bytes = 0;
    std::shared_ptr<ChunkPool> pool = std::make_shared<ChunkPool>();
    std::vector<id<MTLBuffer>> chunks;       // os pedaços do command buffer do quadro
    id<MTLBuffer> chunk;
    size_t chunk_used = 0;
    id<MTLCommandBuffer> last_cmd;
    id<MTLCommandBuffer> throttle_cmds[3];
    int throttle_i = 0;
    bool visible = true;        // a janela aparece (window_visible)
    int visible_countdown = 0;

    // ---- os contadores e as medidas ----
    RenderCounters counters_;
    uint32_t env_parts_ = 0, a2c_parts_ = 0;
    bool alpha_coverage = true;
    bool profiling = false;
    std::shared_ptr<TimeSink> times = std::make_shared<TimeSink>();
    uint64_t epoch = 0;
    uint64_t gpu_seq = 0, cur_seq = 0;
    bool seq_valid = false;
    std::map<uint64_t, std::pair<int, double>> seq_pending;  // quadro -> (command buffers enviados, ms somados)
    std::map<uint64_t, int> seq_done;                         // quadro -> command buffers acabados
    GpuTimes gpu_times;
    std::vector<GpuFrameTimes> per_frame;

    // ---- o estado fixo ----
    id<MTLFunction> fn(const char* name);
    id<MTLRenderPipelineState> pipeline(uint32_t prog, const GsBlendEq* gb, bool coverage, NSUInteger samples, bool has_depth,
                                        MTLPixelFormat format = MTLPixelFormatBGRA8Unorm);
    id<MTLDepthStencilState> depth_state(MTLCompareFunction func, bool write);
    id<MTLTexture> make_texture(MTLPixelFormat format, int w, int h, bool mips, NSUInteger samples, MTLTextureUsage usage);
    void configure_layer();
    void create_targets();
    void create_scene_targets();
    bool try_scene_targets(int scale_percent, int msaa);
    bool separate_scene() const { return scene_color != nil; }
    id<MTLTexture> scene_target() const { return scene_color != nil ? scene_color : target; }
    id<MTLTexture> texture_for(int slot) const;

    // ---- os comandos ----
    id<MTLCommandBuffer> frame_cmd();
    void end_encoder();
    void begin_pass(id<MTLTexture> color, id<MTLTexture> depth_tex, MTLLoadAction color_load, const float* clear,
                    bool clear_depth);
    void use_pass(id<MTLTexture> color, id<MTLTexture> depth_tex);
    void item_pass();
    void target_pass();
    void set_pipe(id<MTLRenderPipelineState> p);
    void set_depth(id<MTLDepthStencilState> d);
    void set_fill(bool lines);
    void set_scissor_full();
    bool set_scissor(const float clip[4]);
    Alloc transient(size_t bytes);
    void commit_frame();
    id<MTLBlitCommandEncoder> upload_blit();
    void flush_uploads();
    void upload(id<MTLTexture> tex, NSUInteger level, const void* data, size_t bytes_per_row, size_t rows, NSUInteger w,
                NSUInteger h);
    void copy_texture(id<MTLTexture> src, id<MTLTexture> dst);
    void draw_full(uint32_t prog, id<MTLTexture> dst, id<MTLTexture> src, const void* bytes, size_t len);
    void ensure_quad_ib(size_t quads);

    // ---- o desenho ----
    void render(const std::vector<DrawItem>& items, const FrameParams& frame);
    void render_2d(const std::vector<DrawItem>& items, const FrameParams& frame);
    void set_frame(const FrameParams& frame);
    void draw_item(const DrawItem& inst, const FrameParams& frame);
    void set_state(const DrawItem& inst, const GsState& own, uint32_t prog, bool sprite, bool env = false, bool screen = false);
    void draw_mesh(const DrawItem& inst, const FrameParams& frame);
    void draw_sprites(const DrawItem& inst);
    void draw_triangles(const DrawItem& inst);
    void draw_2d(const DrawItem& inst);
    void draw_fog(const DrawItem& inst, const FrameParams& frame);
    void draw_movie();
    void draw_ui(const UiHdBatch& batch);
    void composite(const UiRaster& raster);
    void resolve_scene();
    id<MTLTexture> hd_asset(int index, int w, int h);
    void ensure_blur_targets();
    void blur_background(float amount);
    void screen_blur(float keep);
    void present();
    void save_png(const std::wstring& path);
    bool read_texture(int slot, int& width, int& height, std::vector<uint8_t>& rgba);
    void resize(int width, int height);
    bool set_scene_quality(int scale_percent, int msaa);
    void set_texture_filter(TextureFilter f);
    void set_texture(int slot, const Texture& t);
    void update_texture(int slot, const Texture& t);
    bool set_texture_dds(int slot, const std::wstring& path);
    void set_ui_image(uint32_t key, const uint8_t* rgba, int width, int height);
    int add_mesh(const EntityMesh& mesh);
    void set_movie_frame(const uint8_t* bgra, int width, int height, uint64_t serial);
    void ensure_canvas();
    void gpu_collect(bool wait);
    bool window_visible();
};

// ---------------------------------------------------------------- o dispositivo

Renderer::Impl::Impl(void* win, int width, int height, bool vsync, bool offscreen, const AdapterInfo* want)
    : window(win), width_(std::max(1, width)), height_(std::max(1, height)), vsync_(vsync), offscreen_(offscreen) {
    NSArray<id<MTLDevice>>* all = MTLCopyAllDevices();
    int index = -1;
    // O adaptador pedido ([video] adapter, --adapter): o mesmo registryID (os campos do LUID), ou o índice da lista.
    if (want != nullptr) {
        const uint64_t id = uint64_t(want->luid_low) | (uint64_t(uint32_t(want->luid_high)) << 32);
        for (NSUInteger i = 0; i < all.count; ++i) {
            if (all[i].registryID == id) {
                device = all[i];
                index = int(i);
                break;
            }
        }
        if (device == nil && want->index >= 0 && NSUInteger(want->index) < all.count) {
            device = all[NSUInteger(want->index)];
            index = want->index;
        }
        adapter_chosen_ = device != nil;
    }
    // O padrão do sistema; sem ele (um processo sem a sessão gráfica, a CI), o primeiro da lista.
    if (device == nil) device = MTLCreateSystemDefaultDevice();
    if (device == nil && all.count > 0) device = all[0];
    if (device == nil) throw std::runtime_error("sem dispositivo Metal");
    if (index < 0) {
        for (NSUInteger i = 0; i < all.count; ++i) {
            if (all[i].registryID == device.registryID) index = int(i);
        }
    }
    adapter_ = metal_adapter_info(device, std::max(0, index));
    queue = [device newCommandQueue];
    if (queue == nil) throw std::runtime_error("newCommandQueue falhou");
    static_options = device.hasUnifiedMemory ? MTLResourceStorageModeShared : MTLResourceStorageModeManaged;

    MTLCompileOptions* opts = [MTLCompileOptions new];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    opts.fastMathEnabled = NO;  // as contas como escritas (o HLSL do D3D11 não troca a ordem nem aproxima a divisão)
#pragma clang diagnostic pop
    NSError* err = nil;
    library = [device newLibraryWithSource:[NSString stringWithUTF8String:kMsl] options:opts error:&err];
    if (library == nil) {
        std::string msg = "erro no shader: ";
        if (err != nil) msg += err.localizedDescription.UTF8String;
        throw std::runtime_error(msg);
    }

    // Os vértices (data/map_item.h): Vertex de 36 bytes e SkinnedVertex de 48.
    const auto common = [](MTLVertexDescriptor* vd) {
        vd.attributes[0].format = MTLVertexFormatFloat3;
        vd.attributes[0].offset = offsetof(Vertex, pos);
        vd.attributes[1].format = MTLVertexFormatFloat3;
        vd.attributes[1].offset = offsetof(Vertex, normal);
        vd.attributes[2].format = MTLVertexFormatFloat2;
        vd.attributes[2].offset = offsetof(Vertex, uv);
        vd.attributes[3].format = MTLVertexFormatUChar4Normalized;
        vd.attributes[3].offset = offsetof(Vertex, color);
        for (int a = 0; a < 4; ++a) vd.attributes[a].bufferIndex = 0;
    };
    static_assert(offsetof(Vertex, pos) == offsetof(SkinnedVertex, pos) && offsetof(Vertex, normal) == offsetof(SkinnedVertex, normal) &&
                  offsetof(Vertex, uv) == offsetof(SkinnedVertex, uv) && offsetof(Vertex, color) == offsetof(SkinnedVertex, color),
                  "os quatro primeiros campos de Vertex e SkinnedVertex no mesmo lugar");
    vd_rigid = [MTLVertexDescriptor vertexDescriptor];
    common(vd_rigid);
    vd_rigid.layouts[0].stride = sizeof(Vertex);
    vd_skin = [MTLVertexDescriptor vertexDescriptor];
    common(vd_skin);
    vd_skin.attributes[4].format = MTLVertexFormatUChar4;
    vd_skin.attributes[4].offset = offsetof(SkinnedVertex, bones);
    vd_skin.attributes[4].bufferIndex = 0;
    vd_skin.attributes[5].format = MTLVertexFormatFloat;
    vd_skin.attributes[5].offset = offsetof(SkinnedVertex, weight);
    vd_skin.attributes[5].bufferIndex = 0;
    vd_skin.layouts[0].stride = sizeof(SkinnedVertex);
    vd_morph = [MTLVertexDescriptor vertexDescriptor];
    common(vd_morph);
    vd_morph.attributes[4].format = MTLVertexFormatUChar4;
    vd_morph.attributes[4].offset = offsetof(SkinnedVertex, bones);
    vd_morph.attributes[4].bufferIndex = 0;
    vd_morph.attributes[5].format = MTLVertexFormatFloat;
    vd_morph.attributes[5].offset = offsetof(SkinnedVertex, weight);
    vd_morph.attributes[5].bufferIndex = 0;
    vd_morph.attributes[6].format = MTLVertexFormatInt;
    vd_morph.attributes[6].offset = offsetof(SkinnedVertex, morph);
    vd_morph.attributes[6].bufferIndex = 0;
    vd_morph.layouts[0].stride = sizeof(SkinnedVertex);

    MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
    dd.depthCompareFunction = MTLCompareFunctionAlways;
    dd.depthWriteEnabled = NO;
    no_depth = [device newDepthStencilStateWithDescriptor:dd];

    // 2D: o nível 0 da textura (o GS não tem mipmaps nos sprites), de ponto nas fontes e bilinear no resto.
    MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
    sd.sAddressMode = sd.tAddressMode = sd.rAddressMode = MTLSamplerAddressModeClampToEdge;
    sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterNearest;
    sd.mipFilter = MTLSamplerMipFilterNotMipmapped;
    sampler_point = [device newSamplerStateWithDescriptor:sd];
    sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear;
    sampler_linear = [device newSamplerStateWithDescriptor:sd];
    sd.mipFilter = MTLSamplerMipFilterLinear;
    sampler_smooth = [device newSamplerStateWithDescriptor:sd];
    set_texture_filter(TextureFilter::Aniso8);

    // A textura branca (sem textura) e a 0x80 em tudo (kQuadNoTexture: a modulação devolve a cor e o alfa do vértice).
    const uint32_t white_px = 0xFFFFFFFFu, gs_one = 0x80808080u;
    white = make_texture(MTLPixelFormatRGBA8Unorm, 1, 1, false, 1, MTLTextureUsageShaderRead);
    no_texture = make_texture(MTLPixelFormatRGBA8Unorm, 1, 1, false, 1, MTLTextureUsageShaderRead);
    upload(white, 0, &white_px, 4, 1, 1, 1);
    upload(no_texture, 0, &gs_one, 4, 1, 1, 1);
    flush_uploads();

    if (!offscreen_ && window != nullptr) configure_layer();
    system_display_modes(window, &modes, &desktop);
    create_targets();
}

Renderer::Impl::~Impl() {
    @autoreleasepool {
        end_encoder();
        commit_frame();
        if (last_cmd != nil) [last_cmd waitUntilCompleted];
    }
}

// A CAMetalLayer da NSView (window_macos.mm a cria; o tamanho em pixels, drawableSize, ela acompanha): o dispositivo, o
// formato e o VSync, na thread principal.
void Renderer::Impl::configure_layer() {
    CAMetalLayer* found = nil;
    id<MTLDevice> dev = device;
    const bool sync = vsync_;
    void* win = window;
    macos::run_on_main([&] {
        NSView* view = (__bridge NSView*)win;
        CALayer* l = view.layer;
        if ([l isKindOfClass:[CAMetalLayer class]]) {
            CAMetalLayer* m = (CAMetalLayer*)l;
            m.device = dev;
            m.pixelFormat = MTLPixelFormatBGRA8Unorm;
            m.framebufferOnly = YES;
            m.displaySyncEnabled = sync ? YES : NO;
            found = m;
        }
    });
    layer = found;
}

id<MTLFunction> Renderer::Impl::fn(const char* name) {
    auto it = functions.find(name);
    if (it != functions.end()) return it->second;
    id<MTLFunction> f = [library newFunctionWithName:[NSString stringWithUTF8String:name]];
    if (f == nil) throw std::runtime_error(std::string("função de shader ausente: ") + name);
    functions.emplace(name, f);
    return f;
}

id<MTLRenderPipelineState> Renderer::Impl::pipeline(uint32_t prog, const GsBlendEq* gb, bool coverage, NSUInteger samples,
                                                    bool has_depth, MTLPixelFormat format) {
    uint64_t key = uint64_t(prog) | uint64_t(coverage ? 1 : 0) << 5 | uint64_t(log2_samples(samples)) << 6 |
                   uint64_t(has_depth ? 1 : 0) << 8 | uint64_t(format == MTLPixelFormatRGBA8Unorm ? 1 : 0) << 9;
    if (gb != nullptr) {
        key |= uint64_t(gb->src) << 16 | uint64_t(gb->dst) << 24 | uint64_t(gb->op) << 32 | uint64_t(gb->enable ? 1 : 0) << 40;
    }
    auto it = pipelines.find(key);
    if (it != pipelines.end()) return it->second;

    MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
    switch (prog) {
        case kProgMain: pd.vertexFunction = fn("vs_main"); pd.vertexDescriptor = vd_rigid; break;
        case kProgSkin: pd.vertexFunction = fn("vs_skin"); pd.vertexDescriptor = vd_skin; break;
        case kProgMorph: pd.vertexFunction = fn("vs_skin_morph"); pd.vertexDescriptor = vd_morph; break;
        case kProgMovie: pd.vertexFunction = fn("vs_movie"); break;
        case kProgComposite: pd.vertexFunction = fn("vs_rect"); break;
        default: pd.vertexFunction = fn("vs_full"); break;
    }
    switch (prog) {
        case kProgMain:
        case kProgSkin:
        case kProgMorph: pd.fragmentFunction = fn(coverage ? "ps_a2c" : "ps_main"); break;
        case kProgFog: pd.fragmentFunction = fn("ps_fog"); break;
        case kProgFogMs: pd.fragmentFunction = fn("ps_fog_ms"); break;
        case kProgBlitSample: pd.fragmentFunction = fn("ps_blit_sample"); break;
        case kProgDown: pd.fragmentFunction = fn("ps_down"); break;
        case kProgBlur: pd.fragmentFunction = fn("ps_blur"); break;
        case kProgBlurMix: pd.fragmentFunction = fn("ps_blur_mix"); break;
        case kProgMovie: pd.fragmentFunction = fn("ps_movie"); break;
        case kProgComposite: pd.fragmentFunction = fn("ps_composite"); break;
        default: pd.fragmentFunction = fn("ps_blit"); break;  // kProgBlit, kProgHistory
    }
    pd.rasterSampleCount = samples;
    pd.alphaToCoverageEnabled = coverage ? YES : NO;
    pd.depthAttachmentPixelFormat = has_depth ? MTLPixelFormatDepth32Float : MTLPixelFormatInvalid;
    MTLRenderPipelineColorAttachmentDescriptor* ca = pd.colorAttachments[0];
    ca.pixelFormat = format;
    ca.writeMask = MTLColorWriteMaskAll;
    ca.blendingEnabled = NO;
    if (gb != nullptr && gb->enable) {
        // O GS mistura só r, g e b; o alfa gravado é o do pixel (As), que ALPHA com C = Ad lê depois.
        ca.blendingEnabled = YES;
        ca.sourceRGBBlendFactor = mtl_factor(gb->src);
        ca.destinationRGBBlendFactor = mtl_factor(gb->dst);
        ca.rgbBlendOperation = mtl_op(gb->op);
        ca.sourceAlphaBlendFactor = MTLBlendFactorOne;
        ca.destinationAlphaBlendFactor = MTLBlendFactorZero;
        ca.alphaBlendOperation = MTLBlendOperationAdd;
    } else if (prog == kProgFog || prog == kProgFogMs) {
        // Névoa: cor = névoa * (1 - fica) + quadro * fica, com "fica" no alfa da saída; o alfa do quadro não muda.
        ca.blendingEnabled = YES;
        ca.sourceRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        ca.destinationRGBBlendFactor = MTLBlendFactorSourceAlpha;
        ca.rgbBlendOperation = MTLBlendOperationAdd;
        ca.sourceAlphaBlendFactor = MTLBlendFactorZero;
        ca.destinationAlphaBlendFactor = MTLBlendFactorOne;
        ca.alphaBlendOperation = MTLBlendOperationAdd;
    } else if (prog == kProgHistory) {
        // O rastro: o anterior x keep + o atual x (1 - keep), com keep na constante da mistura; o alfa do alvo fica.
        ca.blendingEnabled = YES;
        ca.sourceRGBBlendFactor = MTLBlendFactorBlendColor;
        ca.destinationRGBBlendFactor = MTLBlendFactorOneMinusBlendColor;
        ca.rgbBlendOperation = MTLBlendOperationAdd;
        ca.sourceAlphaBlendFactor = MTLBlendFactorZero;
        ca.destinationAlphaBlendFactor = MTLBlendFactorOne;
        ca.alphaBlendOperation = MTLBlendOperationAdd;
    } else if (prog == kProgComposite) {
        // Uma imagem com o alfa pré-multiplicado por cima (source-over), como o Direct2D desenha no alvo.
        ca.blendingEnabled = YES;
        ca.sourceRGBBlendFactor = MTLBlendFactorOne;
        ca.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        ca.rgbBlendOperation = MTLBlendOperationAdd;
        ca.sourceAlphaBlendFactor = MTLBlendFactorOne;
        ca.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        ca.alphaBlendOperation = MTLBlendOperationAdd;
    }
    NSError* err = nil;
    id<MTLRenderPipelineState> p = [device newRenderPipelineStateWithDescriptor:pd error:&err];
    if (p == nil) {
        std::string msg = "pipeline do Metal falhou: ";
        if (err != nil) msg += err.localizedDescription.UTF8String;
        throw std::runtime_error(msg);
    }
    pipelines.emplace(key, p);
    return p;
}

id<MTLDepthStencilState> Renderer::Impl::depth_state(MTLCompareFunction func, bool write) {
    const uint32_t key = uint32_t(func) | uint32_t(write ? 1 : 0) << 8;
    auto it = depth_states.find(key);
    if (it != depth_states.end()) return it->second;
    MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
    dd.depthCompareFunction = func;
    dd.depthWriteEnabled = write ? YES : NO;
    id<MTLDepthStencilState> s = [device newDepthStencilStateWithDescriptor:dd];
    depth_states.emplace(key, s);
    return s;
}

id<MTLTexture> Renderer::Impl::make_texture(MTLPixelFormat format, int w, int h, bool mips, NSUInteger samples,
                                            MTLTextureUsage usage) {
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                                  width:NSUInteger(std::max(1, w))
                                                                                 height:NSUInteger(std::max(1, h))
                                                                              mipmapped:mips ? YES : NO];
    if (samples > 1) {
        td.textureType = MTLTextureType2DMultisample;
        td.sampleCount = samples;
    }
    td.usage = usage;
    td.storageMode = MTLStorageModePrivate;
    return [device newTextureWithDescriptor:td];
}

void Renderer::Impl::create_targets() {
    target = make_texture(MTLPixelFormatBGRA8Unorm, width_, height_, false, 1,
                          MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
    if (target == nil) throw std::runtime_error("alvo do quadro: sem memória");
    blur_full = nil;
    create_scene_targets();
}

// A profundidade da cena e, com escala ou MSAA (só do PC), a cor própria dela. Sem memória ou sem suporte para o pedido,
// tenta sem MSAA e depois em 100% (o caminho de antes).
void Renderer::Impl::create_scene_targets() {
    if (try_scene_targets(scene_scale, scene_msaa)) return;
    if (scene_msaa > 1 && try_scene_targets(scene_scale, 1)) return;
    if (!try_scene_targets(100, 1)) throw std::runtime_error("alvos da cena: sem memória para a profundidade");
}

bool Renderer::Impl::try_scene_targets(int scale_percent, int msaa) {
    const int max_side = 16384;
    const int w = std::clamp(int(std::lround(double(width_) * scale_percent / 100.0)), 1, max_side);
    const int h = std::clamp(int(std::lround(double(height_) * scale_percent / 100.0)), 1, max_side);
    if (msaa > 1 && ![device supportsTextureSampleCount:NSUInteger(msaa)]) return false;
    id<MTLTexture> d = make_texture(MTLPixelFormatDepth32Float, w, h, false, NSUInteger(msaa),
                                    MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
    if (d == nil) return false;
    id<MTLTexture> color, resolved;
    const bool scaled = w != width_ || h != height_;
    if (msaa > 1 || scaled) {
        color = make_texture(MTLPixelFormatBGRA8Unorm, w, h, false, NSUInteger(msaa),
                             MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
        if (color == nil) return false;
        if (msaa > 1 && scaled) {
            resolved = make_texture(MTLPixelFormatBGRA8Unorm, w, h, false, 1,
                                    MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
            if (resolved == nil) return false;
        }
    }
    depth = d;
    scene_color = color;
    scene_resolved = resolved;
    scene_w = w;
    scene_h = h;
    scene_scale = scale_percent;
    scene_msaa = msaa;
    return true;
}

bool Renderer::Impl::set_scene_quality(int scale_percent, int msaa) {
    scale_percent = std::clamp(scale_percent, 25, 400);
    msaa = msaa >= 8 ? 8 : msaa >= 4 ? 4 : msaa >= 2 ? 2 : 1;
    if (scale_percent == scene_scale && msaa == scene_msaa && depth != nil) return true;
    end_encoder();
    scene_scale = scale_percent;
    scene_msaa = msaa;
    create_scene_targets();
    return scene_scale == scale_percent && scene_msaa == msaa;
}

void Renderer::Impl::set_texture_filter(TextureFilter f) {
    MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
    sd.sAddressMode = sd.tAddressMode = sd.rAddressMode = MTLSamplerAddressModeRepeat;
    sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear;
    const int aniso = texture_filter_anisotropy(f);
    if (aniso > 0) {
        sd.mipFilter = MTLSamplerMipFilterLinear;
        sd.maxAnisotropy = NSUInteger(aniso);
    } else {
        sd.mipFilter = f == TextureFilter::Bilinear ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear;
        sd.maxAnisotropy = 1;
    }
    sampler = [device newSamplerStateWithDescriptor:sd];
    filter_ = f;
}

void Renderer::Impl::resize(int width, int height) {
    if (width <= 0 || height <= 0 || offscreen_) return;
    end_encoder();
    width_ = width;
    height_ = height;
    create_targets();
}

id<MTLTexture> Renderer::Impl::texture_for(int slot) const {
    if (slot == kQuadNoTexture) return no_texture;
    if (use_hd && slot >= 0 && slot < int(hd_textures.size()) && hd_textures[size_t(slot)] != nil) {
        return hd_textures[size_t(slot)];
    }
    if (slot >= 0 && slot < int(textures.size()) && textures[size_t(slot)] != nil) return textures[size_t(slot)];
    return white;
}

// ---------------------------------------------------------------- os comandos

id<MTLCommandBuffer> Renderer::Impl::frame_cmd() {
    if (cmd == nil) cmd = [queue commandBuffer];
    return cmd;
}

void Renderer::Impl::end_encoder() {
    if (enc != nil) [enc endEncoding];
    enc = nil;
    pass_color = nil;
    pass_depth = nil;
}

// Um encoder novo nos anexos dados: a cor carregada (ou limpa com `clear`, ou sem carregar: DontCare), a profundidade
// carregada ou limpa com 0 (a profundidade invertida: perto = 1). O viewport cobre o anexo; as constantes do quadro já vão.
void Renderer::Impl::begin_pass(id<MTLTexture> color, id<MTLTexture> depth_tex, MTLLoadAction color_load, const float* clear,
                                bool clear_depth) {
    end_encoder();
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = color;
    rp.colorAttachments[0].loadAction = color_load;
    if (color_load == MTLLoadActionClear && clear != nullptr) {
        rp.colorAttachments[0].clearColor = MTLClearColorMake(clear[0], clear[1], clear[2], clear[3]);
    }
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    if (depth_tex != nil) {
        rp.depthAttachment.texture = depth_tex;
        rp.depthAttachment.loadAction = clear_depth ? MTLLoadActionClear : MTLLoadActionLoad;
        rp.depthAttachment.clearDepth = 0.0;
        rp.depthAttachment.storeAction = MTLStoreActionStore;
    }
    enc = [frame_cmd() renderCommandEncoderWithDescriptor:rp];
    pass_color = color;
    pass_depth = depth_tex;
    cur_pipe = nil;
    cur_depth = nil;
    cur_lines = false;
    cur_scissor_full = true;
    MTLViewport vp = {0.0, 0.0, double(color.width), double(color.height), 0.0, 1.0};
    [enc setViewport:vp];
    [enc setCullMode:MTLCullModeNone];  // o jogo desenha faces dos dois lados
    [enc setVertexBytes:&frame_u length:sizeof(frame_u) atIndex:1];
    [enc setFragmentBytes:&frame_u length:sizeof(frame_u) atIndex:1];
}

void Renderer::Impl::use_pass(id<MTLTexture> color, id<MTLTexture> depth_tex) {
    if (enc != nil && pass_color == color && pass_depth == depth_tex) return;
    begin_pass(color, depth_tex, MTLLoadActionLoad, nullptr, false);
}

// O encoder dos itens: a cena (com a profundidade) ou, na segunda passada, o 2D no alvo do tamanho da janela.
void Renderer::Impl::item_pass() {
    if (in_2d) {
        use_pass(target, nil);
    } else {
        use_pass(scene_target(), depth);
    }
}

// O alvo, para o que o Direct2D desenharia por cima: o encoder aberto, se for nele (com ou sem a profundidade).
void Renderer::Impl::target_pass() {
    if (enc != nil && pass_color == target) return;
    use_pass(target, nil);
}

void Renderer::Impl::set_pipe(id<MTLRenderPipelineState> p) {
    if (p == cur_pipe) return;
    [enc setRenderPipelineState:p];
    cur_pipe = p;
}

void Renderer::Impl::set_depth(id<MTLDepthStencilState> d) {
    if (pass_depth == nil) d = no_depth;  // sem profundidade no encoder: sem teste
    if (d == cur_depth) return;
    [enc setDepthStencilState:d];
    cur_depth = d;
}

void Renderer::Impl::set_fill(bool lines) {
    if (lines == cur_lines) return;
    [enc setTriangleFillMode:lines ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];
    cur_lines = lines;
}

void Renderer::Impl::set_scissor_full() {
    if (cur_scissor_full) return;
    MTLScissorRect r = {0, 0, pass_color.width, pass_color.height};
    [enc setScissorRect:r];
    cur_scissor_full = true;
}

// O recorte do 2D (o RECT do D3D11: LONG(x0), LONG(y0), LONG(x1 + 0.5), LONG(y1 + 0.5)), preso ao anexo. false: vazio.
bool Renderer::Impl::set_scissor(const float clip[4]) {
    const long w = long(pass_color.width), h = long(pass_color.height);
    const long x0 = std::clamp(long(clip[0]), 0L, w), y0 = std::clamp(long(clip[1]), 0L, h);
    const long x1 = std::clamp(long(clip[2] + 0.5f), 0L, w), y1 = std::clamp(long(clip[3] + 0.5f), 0L, h);
    if (x1 <= x0 || y1 <= y0) return false;
    MTLScissorRect r = {NSUInteger(x0), NSUInteger(y0), NSUInteger(x1 - x0), NSUInteger(y1 - y0)};
    if (!cur_scissor_full && r.x == cur_scissor.x && r.y == cur_scissor.y && r.width == cur_scissor.width &&
        r.height == cur_scissor.height) {
        return true;
    }
    [enc setScissorRect:r];
    cur_scissor = r;
    cur_scissor_full = false;
    return true;
}

// Um pedaço dos dados do quadro (alinhado a 256 bytes: os buffers do espaço "constant" pedem no macOS).
Renderer::Impl::Alloc Renderer::Impl::transient(size_t bytes) {
    bytes = (std::max<size_t>(bytes, 1) + 255) & ~size_t(255);
    if (chunk == nil || chunk_used + bytes > chunk.length) {
        id<MTLBuffer> b = nil;
        const size_t want = std::max(bytes, kChunkBytes);
        {
            std::lock_guard<std::mutex> g(pool->m);
            for (size_t i = 0; i < pool->free.size(); ++i) {
                if (pool->free[i].length >= want) {
                    b = pool->free[i];
                    pool->free.erase(pool->free.begin() + long(i));
                    break;
                }
            }
        }
        if (b == nil) b = [device newBufferWithLength:want options:MTLResourceStorageModeShared];
        if (b == nil) throw std::runtime_error("buffer dos dados do quadro: sem memória");
        chunk = b;
        chunk_used = 0;
        chunks.push_back(b);
    }
    Alloc a;
    a.buf = chunk;
    a.offset = chunk_used;
    a.ptr = static_cast<uint8_t*>(chunk.contents) + chunk_used;
    chunk_used += bytes;
    return a;
}

// Envia o command buffer do quadro (depois do de envio, que leva as texturas que ele usa).
void Renderer::Impl::commit_frame() {
    end_encoder();
    flush_uploads();
    if (cmd == nil) return;
    if (!chunks.empty()) {
        std::vector<id<MTLBuffer>> used = std::move(chunks);
        chunks.clear();
        std::shared_ptr<ChunkPool> p = pool;
        [cmd addCompletedHandler:^(id<MTLCommandBuffer>) {
            std::lock_guard<std::mutex> g(p->m);
            for (id<MTLBuffer> b : used) p->free.push_back(b);
        }];
    }
    chunk = nil;
    chunk_used = 0;
    if (profiling && seq_valid) {
        const uint64_t tag = (epoch << 40) | cur_seq;
        ++seq_pending[cur_seq].first;
        std::shared_ptr<TimeSink> sink = times;
        [cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
            const double ms = (b.GPUEndTime - b.GPUStartTime) * 1000.0;
            std::lock_guard<std::mutex> g(sink->m);
            sink->done.emplace_back(tag, ms >= 0.0 && ms < 1.0e6 ? ms : 0.0);
        }];
    }
    [cmd commit];
    last_cmd = cmd;
    cmd = nil;
}

id<MTLBlitCommandEncoder> Renderer::Impl::upload_blit() {
    if (up_cmd == nil) up_cmd = [queue commandBuffer];
    if (up_blit == nil) up_blit = [up_cmd blitCommandEncoder];
    return up_blit;
}

void Renderer::Impl::flush_uploads() {
    if (up_blit != nil) [up_blit endEncoding];
    up_blit = nil;
    if (up_cmd != nil) [up_cmd commit];
    up_cmd = nil;
    up_bytes = 0;
}

// Um nível de uma textura privada por um buffer de passagem (no command buffer de envio).
void Renderer::Impl::upload(id<MTLTexture> tex, NSUInteger level, const void* data, size_t bytes_per_row, size_t rows,
                            NSUInteger w, NSUInteger h) {
    if (tex == nil || data == nullptr || bytes_per_row == 0 || rows == 0) return;
    const size_t bytes = bytes_per_row * rows;
    id<MTLBuffer> staging = [device newBufferWithBytes:data length:bytes options:MTLResourceStorageModeShared];
    if (staging == nil) return;
    [upload_blit() copyFromBuffer:staging
                     sourceOffset:0
                sourceBytesPerRow:bytes_per_row
              sourceBytesPerImage:bytes
                       sourceSize:MTLSizeMake(w, h, 1)
                        toTexture:tex
                 destinationSlice:0
                 destinationLevel:level
                destinationOrigin:MTLOriginMake(0, 0, 0)];
    up_bytes += bytes;
    if (up_bytes > kUploadFlushBytes) flush_uploads();
}

// Uma textura copiada inteira para outra igual, no command buffer do quadro (o CopyResource do D3D11).
void Renderer::Impl::copy_texture(id<MTLTexture> src, id<MTLTexture> dst) {
    end_encoder();
    id<MTLBlitCommandEncoder> b = [frame_cmd() blitCommandEncoder];
    [b copyFromTexture:src toTexture:dst];
    [b endEncoding];
}

// Um triângulo que cobre `dst` (um encoder só para ele, carregando o que há) com o programa e as constantes dadas.
void Renderer::Impl::draw_full(uint32_t prog, id<MTLTexture> dst, id<MTLTexture> src, const void* bytes, size_t len) {
    use_pass(dst, nil);
    set_pipe(pipeline(prog, nullptr, false, dst.sampleCount, false, dst.pixelFormat));
    set_depth(no_depth);
    set_fill(false);
    set_scissor_full();
    if (bytes != nullptr) [enc setFragmentBytes:bytes length:len atIndex:0];
    [enc setFragmentTexture:src atIndex:0];
    [enc setFragmentSamplerState:sampler_linear atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

void Renderer::Impl::ensure_quad_ib(size_t quads) {
    if (quads <= quad_ib_quads && quad_ib != nil) return;
    quad_ib_quads = std::max<size_t>(quads, 1024);
    std::vector<uint32_t> idx;
    idx.reserve(quad_ib_quads * 6);
    for (uint32_t q = 0; q < uint32_t(quad_ib_quads); ++q) {
        const uint32_t b = q * 4;
        idx.insert(idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
    }
    quad_ib = [device newBufferWithBytes:idx.data() length:idx.size() * sizeof(uint32_t) options:static_options];
}

// ---------------------------------------------------------------- o quadro

void Renderer::Impl::set_frame(const FrameParams& frame) {
    std::memcpy(frame_u.view_proj, frame.view_proj.data(), sizeof(frame_u.view_proj));
    std::memcpy(frame_u.light_dir, frame.light_dir, sizeof(frame_u.light_dir));
    frame_u.ambient = frame.ambient;
    frame_u.debug_shading = frame.debug_shading ? 1.0f : 0.0f;
    frame_u.lights_on = frame.lights ? 1.0f : 0.0f;
    wireframe = frame.wireframe;
}

void Renderer::Impl::render(const std::vector<DrawItem>& items, const FrameParams& frame) {
    env_parts_ = 0;
    a2c_parts_ = 0;
    in_2d = false;
    set_frame(frame);
    // O alvo da cena limpo (com o filme, de preto: o quadro dele é o fundo) e a profundidade em 0 (longe).
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const bool movie = movie_on && movie_tex != nil;
    begin_pass(scene_target(), depth, MTLLoadActionClear, movie ? black : frame.clear_color.data(), true);
    if (movie) {
        draw_movie();
        ++counters_.draws;
    }
    if (!separate_scene()) {
        for (const DrawItem& inst : items) draw_item(inst, frame);
        return;
    }
    // Com escala ou MSAA: a cena no alvo dela, a redução para a janela e então o 2D por cima, no tamanho da janela.
    const auto is_2d = [](const DrawItem& d) { return d.ui != nullptr || !d.quads2d.empty(); };
    for (const DrawItem& inst : items) {
        if (!is_2d(inst)) draw_item(inst, frame);
    }
    resolve_scene();
    in_2d = true;
    for (const DrawItem& inst : items) {
        if (is_2d(inst)) draw_item(inst, frame);
    }
}

void Renderer::Impl::render_2d(const std::vector<DrawItem>& items, const FrameParams& frame) {
    in_2d = true;
    set_frame(frame);
    for (const DrawItem& inst : items) {
        if (inst.ui != nullptr || !inst.quads2d.empty()) draw_item(inst, frame);
    }
}

void Renderer::Impl::draw_item(const DrawItem& inst, const FrameParams& frame) {
    if (inst.kind == ObjectKind::Volume && !frame.show_volumes) return;
    ++counters_.items;
    if (inst.fog_pass) {
        if (frame.fog) draw_fog(inst, frame);
        return;
    }
    if (inst.clear_depth) {
        // psiClearZ: o Z mais longe na tela inteira (0: a profundidade é invertida), no alvo da cena (com MSAA, todas as
        // amostras): um encoder novo que limpa a profundidade e carrega a cor.
        if (!in_2d) begin_pass(scene_target(), depth, MTLLoadActionLoad, nullptr, true);
        return;
    }
    if (inst.ui != nullptr) {
        draw_ui(*inst.ui);
        return;
    }
    if (!inst.quads2d.empty()) {
        draw_2d(inst);
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
    draw_mesh(inst, frame);
}

// Estado do GS (mistura, teste de alfa, teste e escrita de Z) e constantes de uma parte do desenho (apply_state do D3D11).
void Renderer::Impl::set_state(const DrawItem& inst, const GsState& own, uint32_t prog, bool sprite, bool env, bool screen) {
    const DrawState st = resolve_state(own, inst);
    GsBlendEq gb = gs_blend_equation(st.alpha, st.abe, sprite);
    // Só do PC: com MSAA, um desenho opaco recortado pelo teste de alfa vai com o alfa para a cobertura (ps_a2c).
    const bool coverage = alpha_coverage && scene_msaa > 1 && !in_2d && !screen && !sprite &&
                          pass_color.sampleCount > 1 && gs_alpha_coverage_candidate(st.atst, st.aref, st.aint, gb);
    if (coverage) {
        gb.enable = false;  // o teste só deixa passar As >= 1: (Cs - Cd) * As + Cd é Cs
        gb.src = BlendFactor::One;
        gb.dst = BlendFactor::Zero;
        gb.op = BlendOp::Add;
        ++a2c_parts_;
    }
    set_pipe(pipeline(prog, &gb, coverage, pass_color.sampleCount, pass_depth != nil));
    if (uses_fix(gb)) [enc setBlendColorRed:gb.fix green:gb.fix blue:gb.fix alpha:gb.fix];
    set_depth(depth_state(st.zfunc, st.zwrite));
    ObjectU o{};
    std::memcpy(o.world, inst.world.data(), sizeof(o.world));
    o.premult = (sprite && gb.enable && gb.premultipliable) ? 1.0f : 0.0f;
    o.sprite = sprite ? 1.0f : 0.0f;
    o.env = env ? 1.0f : 0.0f;
    o.num_lights = float(inst.num_lights);
    std::memcpy(o.light_pos, inst.light_pos, sizeof(o.light_pos));
    std::memcpy(o.light_col, inst.light_color, sizeof(o.light_col));
    std::memcpy(o.env_s, inst.env_s, sizeof(inst.env_s));
    std::memcpy(o.env_t, inst.env_t, sizeof(inst.env_t));
    std::memcpy(o.tint, inst.tint.data(), sizeof(o.tint));
    o.atst = st.atst;
    o.aref = st.aref;
    o.aint = st.aint ? 1.0f : 0.0f;
    o.screen = screen ? 1.0f : 0.0f;
    [enc setVertexBytes:&o length:sizeof(o) atIndex:2];
    [enc setFragmentBytes:&o length:sizeof(o) atIndex:2];
}

void Renderer::Impl::draw_mesh(const DrawItem& inst, const FrameParams& frame) {
    if (inst.mesh < 0 || inst.mesh >= int(meshes.size())) return;
    const GpuMesh& gm = meshes[size_t(inst.mesh)];
    const bool skinned = !inst.palette.empty();
    if (skinned != gm.skinned || gm.vb == nil || gm.ib == nil) return;
    if (skinned && inst.palette.size() < size_t(gm.bones)) return;
    // Um rosto com algum peso diferente de 0 usa a variante com os morphs; sem pesos, o skinning comum.
    bool morph = false;
    if (skinned && frame.morphs && gm.morph != nil) {
        for (const float w : inst.morph) morph = morph || w != 0.0f;
    }
    const uint32_t prog = morph ? kProgMorph : (skinned ? kProgSkin : kProgMain);
    item_pass();
    set_fill(wireframe);
    set_scissor_full();
    if (skinned) {
        const size_t n = std::min(inst.palette.size(), size_t(kMaxSkinBones));
        const Alloc a = transient(n * sizeof(inst.palette[0]));
        std::memcpy(a.ptr, inst.palette.data(), n * sizeof(inst.palette[0]));
        [enc setVertexBuffer:a.buf offset:a.offset atIndex:3];
    }
    if (morph) {
        MorphU mu{};
        mu.n = uint32_t(std::min<size_t>({inst.morph.size(), size_t(gm.morph_targets), size_t(kMaxMorphTargets)}));
        mu.count = gm.morph_elements;
        for (uint32_t k = 0; k < mu.n; ++k) mu.w[k][0] = inst.morph[k];
        [enc setVertexBytes:&mu length:sizeof(mu) atIndex:4];
        [enc setVertexBuffer:gm.morph offset:0 atIndex:5];
    }
    [enc setVertexBuffer:gm.vb offset:0 atIndex:0];
    for (const SubMesh& sm : gm.submeshes) {
        set_state(inst, sm.gs, prog, false, sm.env && frame.env_map);
        if (sm.env) ++env_parts_;
        if (sm.index_count == 0) continue;
        [enc setFragmentTexture:texture_for(sm.texture) atIndex:0];
        [enc setFragmentSamplerState:sampler atIndex:0];
        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:sm.index_count
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:gm.ib
                 indexBufferOffset:NSUInteger(sm.first_index) * sizeof(uint32_t)];
        ++counters_.draws;
    }
}

// Sprites de psiDrawParticleList: quatro vértices por partícula num pedaço do quadro, dois triângulos cada.
void Renderer::Impl::draw_sprites(const DrawItem& inst) {
    const size_t vertices = inst.sprites.size() - inst.sprites.size() % 4;
    if (vertices == 0) return;
    const size_t quads = vertices / 4;
    ensure_quad_ib(quads);
    const Alloc a = transient(vertices * sizeof(Vertex));
    std::memcpy(a.ptr, inst.sprites.data(), vertices * sizeof(Vertex));
    item_pass();
    set_fill(wireframe);
    set_scissor_full();
    set_state(inst, GsState{}, kProgMain, true);
    [enc setVertexBuffer:a.buf offset:a.offset atIndex:0];
    [enc setFragmentTexture:texture_for(inst.sprite_texture) atIndex:0];
    [enc setFragmentSamplerState:sampler atIndex:0];
    [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                    indexCount:quads * 6
                     indexType:MTLIndexTypeUInt32
                   indexBuffer:quad_ib
             indexBufferOffset:0];
    ++counters_.draws;
}

// Triângulos soltos (os estilhaços de psiDrawShard), sem índices, com o shader e o estado dos sprites.
void Renderer::Impl::draw_triangles(const DrawItem& inst) {
    const size_t vertices = inst.triangles.size() - inst.triangles.size() % 3;
    if (vertices == 0) return;
    const Alloc a = transient(vertices * sizeof(Vertex));
    std::memcpy(a.ptr, inst.triangles.data(), vertices * sizeof(Vertex));
    item_pass();
    set_fill(wireframe);
    set_scissor_full();
    set_state(inst, GsState{}, kProgMain, true);
    [enc setVertexBuffer:a.buf offset:a.offset atIndex:0];
    [enc setFragmentTexture:texture_for(inst.triangle_texture) atIndex:0];
    [enc setFragmentSamplerState:sampler atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:vertices];
    ++counters_.draws;
}

// 2D (psiDrawSprites): os quadrados já em coordenadas da tela (-1..1); seguidos com a mesma textura, o mesmo ALPHA_1 e
// o mesmo filtro, um desenho só. O estado de psiStartSprites (alfa > 0, sem teste nem escrita de Z) e o recorte.
void Renderer::Impl::draw_2d(const DrawItem& inst) {
    const size_t quads = inst.quads2d.size();
    if (quads == 0) return;
    ensure_quad_ib(quads);
    const Alloc a = transient(quads * 4 * sizeof(Vertex));
    auto* out = reinterpret_cast<Vertex*>(a.ptr);
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
    item_pass();
    set_fill(false);
    if (!set_scissor(inst.clip2d)) return;
    [enc setVertexBuffer:a.buf offset:a.offset atIndex:0];
    size_t first = 0;
    while (first < quads) {
        const Quad2D& q = inst.quads2d[first];
        size_t end = first + 1;
        while (end < quads && inst.quads2d[end].texture == q.texture && inst.quads2d[end].alpha == q.alpha &&
               inst.quads2d[end].linear == q.linear && inst.quads2d[end].image == q.image &&
               inst.quads2d[end].smooth == q.smooth) {
            ++end;
        }
        GsState own;
        own.alpha = q.alpha;
        own.test = 0xd;             // ATE, ATST = GREATER, AREF = 0, sem ZTE
        own.zbuf = 0x131000000ull;  // ZMSK
        own.has = GsState::kAlpha | GsState::kTest | GsState::kZbuf;
        set_state(inst, own, kProgMain, true, false, true);
        // Uma substituta HD (pacote do PCSX2) é amostrada com filtro bilinear mesmo nas fontes.
        const bool hd = use_hd && q.texture >= 0 && q.texture < int(hd_textures.size()) &&
                        hd_textures[size_t(q.texture)] != nil;
        [enc setFragmentSamplerState:q.smooth ? sampler_smooth : (q.linear || hd ? sampler_linear : sampler_point)
                             atIndex:0];
        [enc setFragmentTexture:q.image != nullptr ? tex_of(q.image) : texture_for(q.texture) atIndex:0];
        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:(end - first) * 6
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:quad_ib
                 indexBufferOffset:first * 6 * sizeof(uint32_t)];
        ++counters_.draws;
        first = end;
    }
}

// Passada de névoa (psiDrawFogPass): um triângulo que cobre a cena lê a profundidade de cada pixel (num encoder sem ela,
// que a lê como textura) e mistura a cor da névoa como o sprite do PS2.
void Renderer::Impl::draw_fog(const DrawItem& inst, const FrameParams& frame) {
    FogU f{};
    for (int k = 0; k < 3; ++k) f.color[k] = float(inst.fog_rgb[k]) / 255.0f;
    f.color[3] = 1.0f;
    vu1::screen_z_coefficients(inst.fog_far1, f.a, f.b);  // Z do GS = A / z + B
    f.nf = frame.z_near * frame.z_far;
    f.fmn = frame.z_far - frame.z_near;
    f.n = frame.z_near;
    id<MTLTexture> color = scene_target();
    use_pass(color, nil);
    const bool ms = color.sampleCount > 1;
    set_pipe(pipeline(ms ? kProgFogMs : kProgFog, nullptr, false, color.sampleCount, false));
    set_depth(no_depth);
    set_fill(wireframe);
    set_scissor_full();
    [enc setFragmentBytes:&f length:sizeof(f) atIndex:0];
    [enc setFragmentTexture:depth atIndex:ms ? 2 : 1];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

// O quadro do filme no começo do quadro (o encoder da cena acabou de limpar o alvo de preto): a imagem no retângulo da
// tela do PS2, na escala da cena.
void Renderer::Impl::draw_movie() {
    const float sx = float(scene_w) / float(width_), sy = float(scene_h) / float(height_);
    const float rw = float(pass_color.width), rh = float(pass_color.height);
    MovieU u{};
    u.rect[0] = movie_rect[0] * sx;
    u.rect[1] = movie_rect[1] * sy;
    u.rect[2] = movie_rect[2] * sx;
    u.rect[3] = movie_rect[3] * sy;
    const float x0 = std::clamp(u.rect[0], 0.0f, rw), y0 = std::clamp(u.rect[1], 0.0f, rh);
    const float x1 = std::clamp(u.rect[2], 0.0f, rw), y1 = std::clamp(u.rect[3], 0.0f, rh);
    if (x1 - x0 < 1.0f || y1 - y0 < 1.0f) return;
    u.target[0] = rw;
    u.target[1] = rh;
    u.tex_size[0] = float(movie_tex.width);
    u.tex_size[1] = float(movie_tex.height);
    set_pipe(pipeline(kProgMovie, nullptr, false, pass_color.sampleCount, pass_depth != nil));
    set_depth(no_depth);
    set_fill(false);
    set_scissor_full();
    [enc setVertexBytes:&u length:sizeof(u) atIndex:0];
    [enc setFragmentBytes:&u length:sizeof(u) atIndex:0];
    [enc setFragmentTexture:movie_tex atIndex:0];
    [enc setFragmentSamplerState:sampler_linear atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
}

// A cena do alvo próprio para o alvo do quadro. Com MSAA, a resolução (um encoder vazio com o storeAction
// MultisampleResolve, direto no alvo quando o tamanho é o mesmo); com escala, ps_down reduz (ou amplia) para a janela.
void Renderer::Impl::resolve_scene() {
    end_encoder();
    const bool scaled = scene_w != width_ || scene_h != height_;
    if (scene_msaa > 1) {
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = scene_color;
        rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
        rp.colorAttachments[0].storeAction = MTLStoreActionMultisampleResolve;
        rp.colorAttachments[0].resolveTexture = scaled ? scene_resolved : target;
        id<MTLRenderCommandEncoder> e = [frame_cmd() renderCommandEncoderWithDescriptor:rp];
        [e endEncoding];
        if (!scaled) return;
    }
    DownU d{};
    d.src_size[0] = float(scene_w);
    d.src_size[1] = float(scene_h);
    d.ratio[0] = float(scene_w) / float(width_);
    d.ratio[1] = float(scene_h) / float(height_);
    d.taps = float(std::clamp(int(std::ceil(std::max(d.ratio[0], d.ratio[1]) - 0.01f)), 1, 4));
    draw_full(kProgDown, target, scene_msaa > 1 ? scene_resolved : scene_color, &d, sizeof(d));
}

// Camada 2D em alta definição (render/ui_hd.h): as imagens seguidas com o mesmo recorte viram um item 2D comum (com a
// substituta no lugar da textura e o filtro com mipmaps); os textos e as teclas seguidos, uma imagem da UiVectorCanvas
// composta no alvo.
void Renderer::Impl::draw_ui(const UiHdBatch& batch) {
    if (canvas == nullptr || !canvas->ok() || !hd_on) return;
    DrawItem images;
    images.blend = Blend::Alpha;
    const auto flush_images = [&]() {
        if (images.quads2d.empty()) return;
        draw_2d(images);
        images.quads2d.clear();
    };
    // Os textos e as teclas: os ponteiros de todos os do lote (o índice de cada UiHdOp é a posição neles) e as operações
    // seguidas, na ordem.
    std::vector<const UiHdText*> texts;
    std::vector<const UiHdKey*> keys;
    for (const UiHdText& t : batch.texts) texts.push_back(&t);
    for (const UiHdKey& k : batch.keys) keys.push_back(&k);
    std::vector<UiHdOp> run;
    const auto flush_text = [&]() {
        if (run.empty()) return;
        UiRaster r;
        canvas->draw_hd(texts, keys, run, width_, height_, r);
        composite(r);
        run.clear();
    };
    for (const UiHdOp& op : batch.ops) {
        if (op.kind == UiHdOp::Text || op.kind == UiHdOp::Key) {
            flush_images();
            run.push_back(op);
            continue;
        }
        flush_text();
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
            id<MTLTexture> t = hd_asset(im.asset, im.asset_w, im.asset_h);
            q.image = t != nil ? image_of(t) : nullptr;
            if (q.image == nullptr) {  // sem a substituta: a textura original, com o ST original
                q.u0 = im.orig_uv[0];
                q.v0 = im.orig_uv[1];
                q.u1 = im.orig_uv[2];
                q.v1 = im.orig_uv[3];
            }
        }
        images.quads2d.push_back(q);
    }
    flush_images();
    flush_text();
}

// Uma imagem da UiVectorCanvas (BGRA, alfa pré-multiplicado) por cima do alvo, na posição dela: os pixels num pedaço do
// quadro, lidos pelo fragment shader texel a texel.
void Renderer::Impl::composite(const UiRaster& r) {
    if (r.empty() || r.bgra.size() < size_t(r.width) * size_t(r.height) * 4) return;
    const size_t bytes = size_t(r.width) * size_t(r.height) * 4;
    const Alloc a = transient(bytes);
    std::memcpy(a.ptr, r.bgra.data(), bytes);
    target_pass();
    set_pipe(pipeline(kProgComposite, nullptr, false, pass_color.sampleCount, pass_depth != nil));
    set_depth(no_depth);
    set_fill(false);
    set_scissor_full();
    RectU u{};
    u.rect[0] = float(r.x);
    u.rect[1] = float(r.y);
    u.rect[2] = float(r.x + r.width);
    u.rect[3] = float(r.y + r.height);
    u.target[0] = float(pass_color.width);
    u.target[1] = float(pass_color.height);
    u.pitch = uint32_t(r.width);
    [enc setVertexBytes:&u length:sizeof(u) atIndex:0];
    [enc setFragmentBytes:&u length:sizeof(u) atIndex:0];
    [enc setFragmentBuffer:a.buf offset:a.offset atIndex:1];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    ++counters_.draws;
}

// A substituta "index" da camada HD rasterizada em w x h (SVG) ou lida (PNG, com mipmaps), em cache como no Direct2D;
// esquecida depois de 300 quadros sem uso. Nula se falhar (fica a textura original, e não tenta de novo).
id<MTLTexture> Renderer::Impl::hd_asset(int index, int w, int h) {
    if (canvas == nullptr || hd_config == nullptr || index < 0 || size_t(index) >= hd_config->images.size()) return nil;
    if (hd_failed.size() < hd_config->images.size()) hd_failed.resize(hd_config->images.size(), 0);
    if (hd_failed[size_t(index)] != 0) return nil;
    const bool png = hd_config->images[size_t(index)].type == UiHdAssetType::Png;
    if (png) w = h = 0;
    w = std::clamp(w, png ? 0 : 1, 4096);
    h = std::clamp(h, png ? 0 : 1, 4096);
    for (HdAsset& a : hd_assets) {
        if (a.index == index && a.w == w && a.h == h) {
            a.used = frame_no;
            return a.tex;
        }
    }
    std::vector<uint8_t> rgba;
    int ow = 0, oh = 0;
    if (!canvas->asset(index, w, h, rgba, ow, oh) || ow <= 0 || oh <= 0 || rgba.size() < size_t(ow) * size_t(oh) * 4) {
        hd_failed[size_t(index)] = 1;
        return nil;
    }
    id<MTLTexture> t = make_texture(MTLPixelFormatRGBA8Unorm, ow, oh, png, 1, MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget);
    if (t == nil) {
        hd_failed[size_t(index)] = 1;
        return nil;
    }
    upload(t, 0, rgba.data(), size_t(ow) * 4, size_t(oh), NSUInteger(ow), NSUInteger(oh));
    if (png && t.mipmapLevelCount > 1) [upload_blit() generateMipmapsForTexture:t];
    HdAsset a;
    a.index = index;
    a.w = w;
    a.h = h;
    a.tex = t;
    a.used = frame_no;
    hd_assets.push_back(a);
    return t;
}

void Renderer::Impl::ensure_blur_targets() {
    const int qw = std::max(1, width_ / 4), qh = std::max(1, height_ / 4);
    if (blur_full == nil || int(blur_full.width) != width_ || int(blur_full.height) != height_) {
        blur_full = make_texture(MTLPixelFormatBGRA8Unorm, width_, height_, false, 1, MTLTextureUsageShaderRead);
    }
    if (blur_w == qw && blur_h == qh && blur_tex[0] != nil) return;
    blur_w = qw;
    blur_h = qh;
    for (int k = 0; k < 2; ++k) {
        blur_tex[k] = make_texture(MTLPixelFormatBGRA8Unorm, qw, qh, false, 1,
                                   MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget);
    }
}

void Renderer::Impl::blur_background(float amount) {
    if (amount <= 0.001f || target == nil) return;
    ensure_blur_targets();
    copy_texture(target, blur_full);
    // 1/4: a média de 4 x 4 texels (ps_down).
    DownU d{};
    d.src_size[0] = float(width_);
    d.src_size[1] = float(height_);
    d.ratio[0] = float(width_) / float(blur_w);
    d.ratio[1] = float(height_) / float(blur_h);
    d.taps = 4.0f;
    draw_full(kProgDown, blur_tex[0], blur_full, &d, sizeof(d));
    // A gaussiana: horizontal e vertical, duas vezes (a segunda mais espaçada).
    const float spacing[2] = {1.0f, 2.2f};
    for (int pass = 0; pass < 2; ++pass) {
        for (int axis = 0; axis < 2; ++axis) {
            BlurU b{};
            b.texel[0] = 1.0f / float(blur_w);
            b.texel[1] = 1.0f / float(blur_h);
            b.dir[0] = axis == 0 ? spacing[pass] : 0.0f;
            b.dir[1] = axis == 1 ? spacing[pass] : 0.0f;
            const int from = axis == 0 ? 0 : 1, to = 1 - from;
            draw_full(kProgBlur, blur_tex[to], blur_tex[from], &b, sizeof(b));
        }
    }
    // A mistura no alvo.
    BlurU b{};
    b.texel[0] = 1.0f / float(width_);
    b.texel[1] = 1.0f / float(height_);
    b.amount = std::clamp(amount, 0.0f, 1.0f);
    use_pass(target, nil);
    set_pipe(pipeline(kProgBlurMix, nullptr, false, 1, false));
    set_depth(no_depth);
    set_fill(false);
    set_scissor_full();
    [enc setFragmentBytes:&b length:sizeof(b) atIndex:0];
    [enc setFragmentTexture:blur_full atIndex:0];
    [enc setFragmentTexture:blur_tex[0] atIndex:1];
    [enc setFragmentSamplerState:sampler_linear atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    end_encoder();
}

void Renderer::Impl::screen_blur(float keep) {
    if (!(keep > 0.0f) || target == nil) {
        history_valid = false;
        return;
    }
    if (history == nil || history.width != target.width || history.height != target.height) {
        history = make_texture(MTLPixelFormatBGRA8Unorm, int(target.width), int(target.height), false, 1,
                               MTLTextureUsageShaderRead);
        history_valid = false;
        if (history == nil) return;
    }
    if (history_valid) {
        const float k = std::clamp(keep, 0.0f, 1.0f);
        use_pass(target, nil);
        set_pipe(pipeline(kProgHistory, nullptr, false, 1, false));
        set_depth(no_depth);
        set_fill(false);
        set_scissor_full();
        [enc setBlendColorRed:k green:k blue:k alpha:k];
        [enc setFragmentTexture:history atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        ++counters_.draws;
    }
    copy_texture(target, history);
    history_valid = true;
}

// A janela aparece na tela (não está minimizada nem coberta de todo): o occlusionState da NSWindow, lido na thread
// principal a cada 30 quadros. Coberta, o nextDrawable da CAMetalLayer pode esperar até 1 s por um drawable que o
// compositor não pede; sem pedi-lo, o laço segue no ritmo do VSync (como o Present do DXGI com DXGI_STATUS_OCCLUDED).
bool Renderer::Impl::window_visible() {
    if (--visible_countdown > 0) return visible;
    visible_countdown = 30;
    bool vis = true;
    void* win = window;
    macos::run_on_main([&] {
        NSView* view = (__bridge NSView*)win;
        NSWindow* w = view.window;
        if (w != nil) vis = (w.occlusionState & NSWindowOcclusionStateVisible) != 0;
    });
    visible = vis;
    return visible;
}

void Renderer::Impl::present() {
    if (!offscreen_ && layer != nil) {
        id<CAMetalDrawable> drawable = window_visible() ? [layer nextDrawable] : nil;
        if (drawable != nil) {
            // O alvo para o drawable: o texel do pixel no mesmo tamanho; noutro (a janela mudando de tamanho), reduzido
            // ou ampliado como a cena.
            id<MTLTexture> dt = drawable.texture;
            begin_pass(dt, nil, MTLLoadActionDontCare, nullptr, false);
            if (dt.width == target.width && dt.height == target.height) {
                draw_full(kProgBlit, dt, target, nullptr, 0);
            } else {
                DownU d{};
                d.src_size[0] = float(target.width);
                d.src_size[1] = float(target.height);
                d.ratio[0] = float(target.width) / float(dt.width);
                d.ratio[1] = float(target.height) / float(dt.height);
                d.taps = float(std::clamp(int(std::ceil(std::max(d.ratio[0], d.ratio[1]) - 0.01f)), 1, 4));
                draw_full(kProgDown, dt, target, &d, sizeof(d));
            }
            end_encoder();
            [frame_cmd() presentDrawable:drawable];
        } else {
            // Sem drawable (a janela coberta ou minimizada): o quadro não aparece; com o VSync, o ritmo de 60 Hz sem ele.
            ++counters_.occluded;
            if (vsync_) std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
    commit_frame();
    if (canvas != nullptr) canvas->end_frame();
    ++frame_no;
    hd_assets.erase(std::remove_if(hd_assets.begin(), hd_assets.end(),
                                   [&](const HdAsset& a) { return a.used + 300 < frame_no; }),
                    hd_assets.end());
    // Fora da tela não há a fila de uma cadeia de troca para segurar a CPU: espera o quadro de três presents atrás.
    if (offscreen_ && last_cmd != nil) {
        id<MTLCommandBuffer> old = throttle_cmds[throttle_i];
        if (old != nil && old != last_cmd) [old waitUntilCompleted];
        throttle_cmds[throttle_i] = last_cmd;
        throttle_i = (throttle_i + 1) % 3;
    }
}

void Renderer::Impl::save_png(const std::wstring& path) {
    const NSUInteger w = target.width, h = target.height;
    id<MTLBuffer> rb = [device newBufferWithLength:w * h * 4 options:MTLResourceStorageModeShared];
    if (rb == nil) throw std::runtime_error("save_png: sem memória para a leitura");
    end_encoder();
    id<MTLBlitCommandEncoder> b = [frame_cmd() blitCommandEncoder];
    [b copyFromTexture:target
                 sourceSlice:0
                 sourceLevel:0
                sourceOrigin:MTLOriginMake(0, 0, 0)
                  sourceSize:MTLSizeMake(w, h, 1)
                    toBuffer:rb
           destinationOffset:0
      destinationBytesPerRow:w * 4
    destinationBytesPerImage:w * h * 4];
    [b endEncoding];
    commit_frame();
    [last_cmd waitUntilCompleted];
    // Força alfa opaco no arquivo.
    std::vector<uint8_t> pixels(size_t(w) * size_t(h) * 4);
    std::memcpy(pixels.data(), rb.contents, pixels.size());
    for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    write_png_bgra(path, int(w), int(h), pixels.data());  // platform/image_io.h (o ImageIO); lança se falhar
}

bool Renderer::Impl::read_texture(int slot, int& width, int& height, std::vector<uint8_t>& rgba) {
    if (slot < 0 || slot >= int(textures.size()) || textures[size_t(slot)] == nil) return false;
    id<MTLTexture> tex = textures[size_t(slot)];
    const NSUInteger w = tex.width, h = tex.height;
    id<MTLBuffer> rb = [device newBufferWithLength:w * h * 4 options:MTLResourceStorageModeShared];
    if (rb == nil) return false;
    flush_uploads();
    id<MTLCommandBuffer> c = [queue commandBuffer];
    id<MTLBlitCommandEncoder> b = [c blitCommandEncoder];
    [b copyFromTexture:tex
                 sourceSlice:0
                 sourceLevel:0
                sourceOrigin:MTLOriginMake(0, 0, 0)
                  sourceSize:MTLSizeMake(w, h, 1)
                    toBuffer:rb
           destinationOffset:0
      destinationBytesPerRow:w * 4
    destinationBytesPerImage:w * h * 4];
    [b endEncoding];
    [c commit];
    [c waitUntilCompleted];
    width = int(w);
    height = int(h);
    rgba.resize(size_t(w) * size_t(h) * 4);
    std::memcpy(rgba.data(), rb.contents, rgba.size());
    return true;
}

// ---------------------------------------------------------------- os recursos

int Renderer::Impl::add_mesh(const EntityMesh& mesh) {
    GpuMesh gm;
    gm.skinned = mesh.skinned;
    gm.bones = mesh.bone_count;
    const void* vdata = mesh.skinned ? static_cast<const void*>(mesh.skin_vertices.data()) : mesh.vertices.data();
    const size_t vbytes = mesh.skinned ? mesh.skin_vertices.size() * sizeof(SkinnedVertex) : mesh.vertices.size() * sizeof(Vertex);
    if (vbytes > 0) gm.vb = [device newBufferWithBytes:vdata length:vbytes options:static_options];
    if (!mesh.indices.empty()) {
        gm.ib = [device newBufferWithBytes:mesh.indices.data() length:mesh.indices.size() * sizeof(uint32_t) options:static_options];
    }
    gm.submeshes = mesh.submeshes;
    // Os alvos de morph de um rosto: os deltas de todos os elementos do bloco num buffer (float3 sem folga).
    if (mesh.skinned && !mesh.morph.empty()) {
        gm.morph = [device newBufferWithBytes:mesh.morph.deltas.data()
                                       length:mesh.morph.deltas.size() * sizeof(float)
                                      options:static_options];
        gm.morph_elements = uint32_t(mesh.morph.elements());
        gm.morph_targets = int(mesh.morph.targets);
    }
    meshes.push_back(gm);
    return int(meshes.size()) - 1;
}

void Renderer::Impl::set_texture(int slot, const Texture& t) {
    if (slot < 0 || t.width <= 0 || t.height <= 0 || t.rgba.size() < size_t(t.width) * size_t(t.height) * 4) return;
    id<MTLTexture> tex = make_texture(MTLPixelFormatRGBA8Unorm, t.width, t.height, true, 1,
                                      MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget);
    if (tex == nil) throw std::runtime_error("textura: sem memória");
    upload(tex, 0, t.rgba.data(), size_t(t.width) * 4, size_t(t.height), NSUInteger(t.width), NSUInteger(t.height));
    if (tex.mipmapLevelCount > 1) [upload_blit() generateMipmapsForTexture:tex];
    if (slot >= int(textures.size())) textures.resize(size_t(slot) + 1);
    textures[size_t(slot)] = tex;
}

// O conteúdo novo da textura do slot (Renderer::update_texture; o UpdateSubresource + GenerateMips do D3D11). Fora de
// um quadro (o caso do jogo: a lógica roda entre o present e o próximo render), pelo command buffer de envio, que sobe
// antes do próximo quadro; com um quadro sendo gravado, no command buffer dele, na ordem: os desenhos de antes veem o
// conteúdo antigo, como no contexto imediato do D3D11. A substituta HD do slot sai.
void Renderer::Impl::update_texture(int slot, const Texture& t) {
    if (slot < 0 || t.width <= 0 || t.height <= 0 || t.rgba.size() < size_t(t.width) * size_t(t.height) * 4) return;
    if (slot < int(hd_textures.size())) hd_textures[size_t(slot)] = nil;
    id<MTLTexture> tex = slot < int(textures.size()) ? textures[size_t(slot)] : nil;
    if (tex == nil || tex.width != NSUInteger(t.width) || tex.height != NSUInteger(t.height)) {
        set_texture(slot, t);
        return;
    }
    const size_t row = size_t(t.width) * 4, bytes = row * size_t(t.height);
    if (cmd == nil) {
        upload(tex, 0, t.rgba.data(), row, size_t(t.height), NSUInteger(t.width), NSUInteger(t.height));
        if (tex.mipmapLevelCount > 1) [upload_blit() generateMipmapsForTexture:tex];
        return;
    }
    const Alloc a = transient(bytes);
    std::memcpy(a.ptr, t.rgba.data(), bytes);
    end_encoder();
    id<MTLBlitCommandEncoder> b = [frame_cmd() blitCommandEncoder];
    [b copyFromBuffer:a.buf
               sourceOffset:a.offset
          sourceBytesPerRow:row
        sourceBytesPerImage:bytes
                 sourceSize:MTLSizeMake(NSUInteger(t.width), NSUInteger(t.height), 1)
                  toTexture:tex
           destinationSlice:0
           destinationLevel:0
          destinationOrigin:MTLOriginMake(0, 0, 0)];
    if (tex.mipmapLevelCount > 1) [b generateMipmapsForTexture:tex];
    [b endEncoding];
}

bool Renderer::Impl::set_texture_dds(int slot, const std::wstring& path) {
    if (slot < 0) return false;
    FILE* f = os::open_file(path, "rb");
    if (f == nullptr) return false;
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
    const auto cc = [](const char* s) {
        return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 | uint32_t(uint8_t(s[3])) << 24;
    };
    // Os formatos do DXGI que o D3D11 aceita aqui, nos do Metal (os Macs com Apple Silicon e os Intel têm BC).
    uint32_t dxgi = 0;
    size_t offset = 128;
    if ((pf_flags & 4) != 0 && fourcc == cc("DX10")) {
        if (file.size() < 148) return false;
        dxgi = u32(128);
        offset = 148;
    } else if ((pf_flags & 4) != 0 && fourcc == cc("DXT1")) {
        dxgi = 71;
    } else if ((pf_flags & 4) != 0 && fourcc == cc("DXT3")) {
        dxgi = 74;
    } else if ((pf_flags & 4) != 0 && fourcc == cc("DXT5")) {
        dxgi = 77;
    } else if ((pf_flags & 0x40) != 0 && rgb_bits == 32 && u32(92) == 0xff && u32(100) == 0xff0000) {
        dxgi = 28;
    }
    MTLPixelFormat fmt = MTLPixelFormatInvalid;
    size_t block_bytes = 0;  // 0: sem compressão (4 bytes por texel)
    switch (dxgi) {
        case 71: fmt = MTLPixelFormatBC1_RGBA; block_bytes = 8; break;
        case 72: fmt = MTLPixelFormatBC1_RGBA_sRGB; block_bytes = 8; break;
        case 74: fmt = MTLPixelFormatBC2_RGBA; block_bytes = 16; break;
        case 75: fmt = MTLPixelFormatBC2_RGBA_sRGB; block_bytes = 16; break;
        case 77: fmt = MTLPixelFormatBC3_RGBA; block_bytes = 16; break;
        case 78: fmt = MTLPixelFormatBC3_RGBA_sRGB; block_bytes = 16; break;
        case 98: fmt = MTLPixelFormatBC7_RGBAUnorm; block_bytes = 16; break;
        case 99: fmt = MTLPixelFormatBC7_RGBAUnorm_sRGB; block_bytes = 16; break;
        case 28: fmt = MTLPixelFormatRGBA8Unorm; break;
        case 29: fmt = MTLPixelFormatRGBA8Unorm_sRGB; break;
        default: return false;
    }
    if (block_bytes != 0 && !device.supportsBCTextureCompression) return false;
    if (width == 0 || height == 0 || width > 16384 || height > 16384) return false;
    struct Level {
        size_t offset, pitch, rows;
        uint32_t w, h;
    };
    std::vector<Level> levels;
    uint32_t w = width, h = height;
    for (uint32_t m = 0; m < mips; ++m) {
        const size_t pitch = block_bytes ? size_t(std::max(1u, (w + 3) / 4)) * block_bytes : size_t(w) * 4;
        const size_t rows = block_bytes ? std::max(1u, (h + 3) / 4) : h;
        if (offset + pitch * rows > file.size()) break;  // arquivo curto: usa os níveis completos
        levels.push_back({offset, pitch, rows, w, h});
        offset += pitch * rows;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    if (levels.empty()) return false;
    MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt
                                                                                  width:width
                                                                                 height:height
                                                                              mipmapped:NO];
    td.mipmapLevelCount = levels.size();
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> tex = [device newTextureWithDescriptor:td];
    if (tex == nil) return false;
    for (size_t m = 0; m < levels.size(); ++m) {
        const Level& l = levels[m];
        upload(tex, NSUInteger(m), file.data() + l.offset, l.pitch, l.rows, l.w, l.h);
    }
    if (levels.size() == 1 && std::max(width, height) > 1) {
        // Sem mipmaps: copiada para RGBA8 na GPU (um desenho que lê cada texel) e com a cadeia gerada, para não
        // cintilar de longe (o mesmo do D3D11).
        id<MTLTexture> full = make_texture(MTLPixelFormatRGBA8Unorm, int(width), int(height), true, 1,
                                           MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget);
        if (full != nil) {
            if (up_blit != nil) [up_blit endEncoding];
            up_blit = nil;
            if (up_cmd == nil) up_cmd = [queue commandBuffer];
            MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
            rp.colorAttachments[0].texture = full;
            rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> e = [up_cmd renderCommandEncoderWithDescriptor:rp];
            MTLViewport vp = {0.0, 0.0, double(width), double(height), 0.0, 1.0};
            [e setViewport:vp];
            [e setRenderPipelineState:pipeline(kProgBlitSample, nullptr, false, 1, false, MTLPixelFormatRGBA8Unorm)];
            [e setFragmentTexture:tex atIndex:0];
            [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [e endEncoding];
            if (full.mipmapLevelCount > 1) [upload_blit() generateMipmapsForTexture:full];
            tex = full;
        }
    }
    if (slot >= int(hd_textures.size())) hd_textures.resize(size_t(slot) + 1);
    hd_textures[size_t(slot)] = tex;
    return true;
}

void Renderer::Impl::set_ui_image(uint32_t key, const uint8_t* rgba, int width, int height) {
    if (rgba == nullptr || width <= 0 || height <= 0) return;
    id<MTLTexture> tex = make_texture(MTLPixelFormatRGBA8Unorm, width, height, true, 1,
                                      MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget);
    if (tex == nil) return;
    upload(tex, 0, rgba, size_t(width) * 4, size_t(height), NSUInteger(width), NSUInteger(height));
    if (tex.mipmapLevelCount > 1) [upload_blit() generateMipmapsForTexture:tex];
    for (auto& [k, v] : ui_images) {
        if (k == key) {
            v = tex;
            return;
        }
    }
    ui_images.emplace_back(key, tex);
}

void Renderer::Impl::set_movie_frame(const uint8_t* bgra, int width, int height, uint64_t serial) {
    if (bgra == nullptr || width <= 0 || height <= 0) return;
    if (movie_tex == nil || int(movie_tex.width) != width || int(movie_tex.height) != height) {
        movie_tex = make_texture(MTLPixelFormatBGRA8Unorm, width, height, false, 1, MTLTextureUsageShaderRead);
        movie_serial = ~0ull;
        if (movie_tex == nil) return;
    }
    if (serial == movie_serial) return;
    upload(movie_tex, 0, bgra, size_t(width) * 4, size_t(height), NSUInteger(width), NSUInteger(height));
    movie_serial = serial;
}

void Renderer::Impl::ensure_canvas() {
    if (canvas_tried) return;
    canvas_tried = true;
    canvas = create_ui_vector_canvas();
}

// ---------------------------------------------------------------- as medidas do --bench

// Os tempos de GPU: o total de cada quadro (GPUEndTime - GPUStartTime dos command buffers dele); os trechos (GpuPhase)
// ficam em 0 (o Metal só os daria com os contadores nas fronteiras dos estágios, que nem toda GPU tem: docs/port.md).
void Renderer::Impl::gpu_collect(bool wait) {
    if (wait && last_cmd != nil) [last_cmd waitUntilCompleted];
    std::vector<std::pair<uint64_t, double>> done;
    {
        std::lock_guard<std::mutex> g(times->m);
        done.swap(times->done);
    }
    for (const auto& [tag, ms] : done) {
        if ((tag >> 40) != epoch) continue;
        const uint64_t seq = tag & ((uint64_t(1) << 40) - 1);
        seq_pending[seq].second += ms;
        ++seq_done[seq];
    }
    for (auto it = seq_pending.begin(); it != seq_pending.end();) {
        const uint64_t seq = it->first;
        const int committed = it->second.first;
        const auto d = seq_done.find(seq);
        const int finished = d != seq_done.end() ? d->second : 0;
        const bool closed = wait || !seq_valid || seq < cur_seq;
        if (!closed || committed == 0 || finished < committed) {
            ++it;
            continue;
        }
        const double total = it->second.second;
        gpu_times.total_ms += total;
        gpu_times.max_ms = std::max(gpu_times.max_ms, total);
        ++gpu_times.frames;
        if (seq < 1000000) {
            if (per_frame.size() <= seq) {
                GpuFrameTimes none;
                none.fill(-1.0f);
                per_frame.resize(size_t(seq) + 1, none);
            }
            GpuFrameTimes t{};
            t[0] = float(total);
            per_frame[size_t(seq)] = t;
        }
        if (d != seq_done.end()) seq_done.erase(d);
        it = seq_pending.erase(it);
    }
}

// ---------------------------------------------------------------- a fachada (render/renderer.h)

Renderer::Renderer(void* window, int width, int height, bool vsync, bool offscreen, const AdapterInfo* adapter) {
    @autoreleasepool {
        impl_ = std::make_unique<Impl>(window, width, height, vsync, offscreen, adapter);
    }
}
Renderer::~Renderer() {
    @autoreleasepool {
        impl_.reset();
    }
}

void Renderer::resize(int width, int height) {
    @autoreleasepool {
        impl_->resize(width, height);
    }
}
void Renderer::reset_resources() {
    impl_->meshes.clear();
    impl_->textures.clear();
    impl_->hd_textures.clear();
}
void Renderer::set_texture(int slot, const Texture& texture) {
    @autoreleasepool {
        impl_->set_texture(slot, texture);
    }
}
void Renderer::update_texture(int slot, const Texture& texture) {
    @autoreleasepool {
        impl_->update_texture(slot, texture);
    }
}
bool Renderer::set_texture_dds(int slot, const std::wstring& path) {
    @autoreleasepool {
        return impl_->set_texture_dds(slot, path);
    }
}
void Renderer::set_ui_image(uint32_t key, const uint8_t* rgba, int width, int height) {
    @autoreleasepool {
        impl_->set_ui_image(key, rgba, width, height);
    }
}
GpuImage* Renderer::ui_image(uint32_t key) const {
    for (const auto& [k, v] : impl_->ui_images) {
        if (k == key) return image_of(v);
    }
    return nullptr;
}
size_t Renderer::ui_images() const { return impl_->ui_images.size(); }
void Renderer::use_hd(bool on) { impl_->use_hd = on; }
bool Renderer::using_hd() const { return impl_->use_hd; }
int Renderer::add_mesh(const EntityMesh& mesh) {
    @autoreleasepool {
        return impl_->add_mesh(mesh);
    }
}
void Renderer::render(const std::vector<DrawItem>& items, const FrameParams& frame) {
    @autoreleasepool {
        impl_->render(items, frame);
    }
}
void Renderer::render_2d(const std::vector<DrawItem>& items, const FrameParams& frame) {
    @autoreleasepool {
        impl_->render_2d(items, frame);
    }
}
void Renderer::present() {
    @autoreleasepool {
        impl_->present();
    }
}
void Renderer::save_png(const std::wstring& path) {
    @autoreleasepool {
        impl_->save_png(path);
    }
}
void Renderer::screen_blur(float keep) {
    @autoreleasepool {
        impl_->screen_blur(keep);
    }
}
bool Renderer::screen_blur_active() const { return impl_->history_valid; }
void Renderer::clear_target(const float rgba[4]) {
    @autoreleasepool {
        impl_->begin_pass(impl_->target, nil, MTLLoadActionClear, rgba, false);
    }
}
void Renderer::save_loading_snapshot() {
    @autoreleasepool {
        Impl& m = *impl_;
        if (m.snapshot == nil || m.snapshot.width != m.target.width || m.snapshot.height != m.target.height) {
            m.snapshot = m.make_texture(MTLPixelFormatBGRA8Unorm, int(m.target.width), int(m.target.height), false, 1,
                                        MTLTextureUsageShaderRead);
            if (m.snapshot == nil) return;
        }
        m.copy_texture(m.target, m.snapshot);
    }
}
bool Renderer::restore_loading_snapshot() {
    @autoreleasepool {
        Impl& m = *impl_;
        if (m.snapshot == nil) return false;
        if (m.snapshot.width != m.target.width || m.snapshot.height != m.target.height) return false;  // a janela mudou
        m.copy_texture(m.snapshot, m.target);
        return true;
    }
}
void Renderer::release_loading_snapshot() { impl_->snapshot = nil; }
GpuImageRef Renderer::keep_texture(int slot) const {
    const Impl& m = *impl_;
    if (slot < 0) return nullptr;
    id<MTLTexture> t = nil;
    if (m.use_hd && slot < int(m.hd_textures.size()) && m.hd_textures[size_t(slot)] != nil) {
        t = m.hd_textures[size_t(slot)];
    } else if (slot < int(m.textures.size())) {
        t = m.textures[size_t(slot)];
    }
    if (t == nil) return nullptr;
    // A referência própria: o objeto retido no GpuImageRef, que o solta quando a última cópia sai.
    return GpuImageRef(static_cast<GpuImage*>(const_cast<void*>(CFBridgingRetain(t))),
                       [](GpuImage* p) { CFRelease(static_cast<CFTypeRef>(p)); });
}
bool Renderer::offscreen() const { return impl_->offscreen_; }

void Renderer::set_ui_hd(const UiHdConfig* config, const std::wstring& dir) {
    @autoreleasepool {
        Impl& m = *impl_;
        m.hd_assets.clear();
        m.hd_failed.clear();
        if (config == nullptr) {
            m.hd_on = false;
            m.hd_config = nullptr;
            return;
        }
        m.ensure_canvas();
        m.hd_config = config;
        m.hd_on = m.canvas != nullptr;
        if (m.canvas != nullptr) m.canvas->configure_hd(config, dir);
    }
}
std::string Renderer::ui_hd_describe() const {
    return impl_->hd_on && impl_->canvas != nullptr ? impl_->canvas->hd_describe() : "desligada";
}
float Renderer::ui_hd_measure(const std::wstring& label, float cap_px) const {
    return impl_->hd_on && impl_->canvas != nullptr ? impl_->canvas->hd_measure(label, cap_px) : 0.0f;
}
bool Renderer::ui_hd_ok() const { return impl_->hd_on && impl_->canvas != nullptr && impl_->canvas->ok(); }

bool Renderer::set_modern_ui(const std::wstring& hd_ui_dir) {
    @autoreleasepool {
        Impl& m = *impl_;
        m.ensure_canvas();
        if (m.canvas == nullptr) return false;
        m.canvas->configure_modern(hd_ui_dir);
        return m.canvas->ok();
    }
}
bool Renderer::modern_ok() const { return impl_->canvas != nullptr && impl_->canvas->ok(); }
float Renderer::modern_measure(const std::wstring& text, const ui::TextStyle& style) const {
    return impl_->canvas != nullptr ? impl_->canvas->measure(text, style) : ui::estimate_width(text, style);
}
void Renderer::blur_background(float amount) {
    @autoreleasepool {
        impl_->blur_background(amount);
    }
}
void Renderer::draw_modern(const ui::DrawList& list, const ui::Canvas& canvas) {
    @autoreleasepool {
        Impl& m = *impl_;
        if (m.canvas == nullptr || !m.canvas->ok()) return;
        UiRaster r;
        m.canvas->draw_modern(list, canvas, m.width_, m.height_, r);
        m.composite(r);
    }
}
std::string Renderer::modern_describe() const {
    return impl_->canvas != nullptr ? impl_->canvas->modern_describe() : "desligado (sem o desenho vetorial)";
}
bool Renderer::read_texture(int slot, int& width, int& height, std::vector<uint8_t>& rgba) {
    @autoreleasepool {
        return impl_->read_texture(slot, width, height, rgba);
    }
}
bool Renderer::write_png(const std::wstring& path, int width, int height, const uint8_t* rgba) {
    return write_png_rgba(path, width, height, rgba);
}

int Renderer::width() const { return impl_->width_; }
int Renderer::height() const { return impl_->height_; }

void Renderer::set_movie_frame(const uint8_t* bgra, int width, int height, uint64_t serial) {
    @autoreleasepool {
        impl_->set_movie_frame(bgra, width, height, serial);
    }
}
void Renderer::set_movie_background(bool on, const float rect[4]) {
    impl_->movie_on = on && impl_->movie_tex != nil;
    std::memcpy(impl_->movie_rect, rect, sizeof(impl_->movie_rect));
}
GpuImage* Renderer::movie_image() const { return impl_->movie_tex != nil ? image_of(impl_->movie_tex) : nullptr; }

const AdapterInfo& Renderer::adapter() const { return impl_->adapter_; }
bool Renderer::adapter_chosen() const { return impl_->adapter_chosen_; }
void Renderer::set_profiling(bool on) {
    impl_->profiling = on;
    if (!on) impl_->seq_valid = false;
}
bool Renderer::profiling() const { return impl_->profiling; }
void Renderer::gpu_frame_begin() {
    Impl& m = *impl_;
    if (!m.profiling) return;
    m.cur_seq = m.gpu_seq++;
    m.seq_valid = true;
}
void Renderer::gpu_frame_end() {
    if (!impl_->profiling) return;
    impl_->gpu_collect(false);
}
void Renderer::gpu_collect(bool wait) {
    @autoreleasepool {
        impl_->gpu_collect(wait);
    }
}
const GpuTimes& Renderer::gpu_times() const { return impl_->gpu_times; }
void Renderer::reset_gpu_times() {
    Impl& m = *impl_;
    m.gpu_times = GpuTimes{};
    m.gpu_seq = 0;
    m.cur_seq = 0;
    m.seq_valid = false;
    m.per_frame.clear();
    m.seq_pending.clear();
    m.seq_done.clear();
    ++m.epoch;  // os command buffers ainda na GPU são de antes: os tempos deles não contam
}
const std::vector<Renderer::GpuFrameTimes>& Renderer::gpu_per_frame() const { return impl_->per_frame; }
const RenderCounters& Renderer::counters() const { return impl_->counters_; }
void Renderer::set_alpha_to_coverage(bool on) { impl_->alpha_coverage = on; }
bool Renderer::alpha_to_coverage() const { return impl_->alpha_coverage; }
uint32_t Renderer::alpha_coverage_parts() const { return impl_->a2c_parts_; }
uint32_t Renderer::env_parts() const { return impl_->env_parts_; }
void Renderer::reset_counters() { impl_->counters_ = RenderCounters{}; }
void Renderer::set_offscreen_throttle(bool on) { (void)on; }  // fora da tela o Metal sempre segura em três quadros

void Renderer::set_vsync(bool on) {
    Impl& m = *impl_;
    if (on == m.vsync_) return;
    m.vsync_ = on;
    if (m.layer != nil) {
        CAMetalLayer* l = m.layer;
        macos::run_on_main([l, on] { l.displaySyncEnabled = on ? YES : NO; });
    }
}
bool Renderer::vsync() const { return impl_->vsync_; }
bool Renderer::set_scene_quality(int scale_percent, int msaa) {
    @autoreleasepool {
        return impl_->set_scene_quality(scale_percent, msaa);
    }
}
int Renderer::scene_scale() const { return impl_->scene_scale; }
int Renderer::scene_msaa() const { return impl_->scene_msaa; }
int Renderer::scene_width() const { return impl_->scene_w; }
int Renderer::scene_height() const { return impl_->scene_h; }
std::vector<int> Renderer::supported_msaa() const {
    std::vector<int> out;
    for (int n : {2, 4, 8}) {
        if ([impl_->device supportsTextureSampleCount:NSUInteger(n)]) out.push_back(n);
    }
    return out;
}
void Renderer::set_texture_filter(TextureFilter f) {
    @autoreleasepool {
        impl_->set_texture_filter(f);
    }
}
TextureFilter Renderer::texture_filter() const { return impl_->filter_; }
bool Renderer::tearing() const { return false; }
// A tela cheia exclusiva do DXGI (o modo do monitor trocado) não existe no macOS: o modo Exclusive das opções é a janela
// sem bordas do tamanho do monitor, que a Window já abriu. Aceita, sem fazer nada (como o renderizador nulo).
bool Renderer::set_exclusive_fullscreen(bool on, int, int) {
    impl_->exclusive_ = on && !impl_->offscreen_;
    return on == impl_->exclusive_;
}
bool Renderer::exclusive_fullscreen() const { return impl_->exclusive_; }
std::vector<DisplayMode> Renderer::display_modes() const { return impl_->modes; }
DisplayMode Renderer::desktop_mode() const { return impl_->desktop; }

}  // namespace nf
