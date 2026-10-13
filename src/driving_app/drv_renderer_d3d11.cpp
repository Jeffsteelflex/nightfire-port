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
};
cbuffer Draw : register(b1) {
    float4 props;   // LightingProperties
    int4 mode;      // x passada (0, 1, 2), y textura ligada, z névoa (FGE), w substituto
    int4 gs;        // x TFX, y TCC, z teste de alfa (0 sem, 1 só quem passa, 2 só quem falha), w ATST
    float4 gs2;     // x AREF
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
};
struct VSOut {
    float4 pos : SV_Position;
    float3 uvp : TEXCOORD0;                // a STQ da passada 1 (S = s Q, T = t Q, Q = z/w: o GS divide S/Q)
    noperspective float2 uva : TEXCOORD1;  // as das passadas 2 e 3 (Q = 1: afim na tela, como o GS)
    noperspective float4 col : COLOR0;     // Gouraud do GS: linear na tela, 0..255
    noperspective float fogf : FOG;
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
    // 5. transformação (a do PC) e a névoa do microprograma
    precise float4 clip = mul(float4(pw, 1.0), view_proj);
    o.pos = clip;
    float ww = min(max(clip.w, fog.x), fog.z);
    o.fogf = 255.0 - fog.w * (ww - fog.x);
    o.uvp = i.stq;  // interpolado com perspectiva: (soma de s/w) / (soma de z/w), como S/Q no GS
    o.uva = i.stq.xy;
    o.col = float4(col, 128.0);
    if (mode.x == 1) {  // 6. o especular: mapa esférico das normais
        float4 E[3];
        [unroll] for (int k3 = 0; k3 < 3; ++k3)
            E[k3] = (spec_mat[0] * world[k3].x + spec_mat[1] * world[k3].y + spec_mat[2] * world[k3].z +
                     spec_mat[3] * world[k3].w) * refl.z;
        float4 e = E[0] * n.x + E[1] * n.y + E[2] * n.z;
        o.uva = e.xy + 0.5;
        o.col = float4(trunc(props.z * alpha_f).xxx, 128.0);
    } else if (mode.x == 2) {  // 7. o reflexo
        float3 v = p * refl.x + n;
        float4 a = (world[0] * refl.y) * v.x + (world[1] * refl.y) * v.y + (world[2] * refl.y) * v.z;
        o.uva = float2(a.x + 0.5, a.z + 0.5);
        float gate = max(a.y, 0.0) * 100000.0;
        o.col = float4(trunc(min(props.w * alpha_f, gate)).xxx, 128.0);
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
};
struct DrawCB {
    float props[4];
    int32_t mode[4];
    int32_t gs[4];
    float gs2[4];
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
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vb, ib, cb_frame, cb_draw, cb_object;
    std::vector<ComPtr<ID3D11ShaderResourceView>> textures;
    std::map<uint32_t, ComPtr<ID3D11SamplerState>> samplers;
    std::map<uint64_t, ComPtr<ID3D11BlendState>> blends;
    std::map<uint32_t, ComPtr<ID3D11DepthStencilState>> depths;
    ComPtr<ID3D11RasterizerState> raster;
    int width = 0, height = 0, samples = 1;
    float center[3] = {0, 0, 0}, radius = 1;
    bool windowed = false;

    bool make_targets(std::string& error);
    ID3D11SamplerState* sampler(uint64_t clamp);
    ID3D11BlendState* blend(uint64_t alpha, bool abe, int mask);
    ID3D11DepthStencilState* depth_state(uint64_t test, bool zwrite);
};

DrvRenderer::DrvRenderer() : d_(new Impl) {}
DrvRenderer::~DrvRenderer() { delete d_; }

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

    ComPtr<ID3DBlob> vsb, psb, err;
    if (FAILED(D3DCompile(kShader, sizeof kShader - 1, "car.hlsl", nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &vsb, &err)) ||
        FAILED(D3DCompile(kShader, sizeof kShader - 1, "car.hlsl", nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &psb, &err))) {
        error = err ? std::string(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize()) : "D3DCompile";
        return false;
    }
    I.dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &I.vs);
    I.dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &I.ps);
    static_assert(offsetof(CarGpuVertex, stq) == 12 && offsetof(CarGpuVertex, normal) == 24 &&
                  offsetof(CarGpuVertex, dmgvec) == 36 && offsetof(CarGpuVertex, dmgnrm) == 52 &&
                  offsetof(CarGpuVertex, zone) == 64 && sizeof(CarGpuVertex) == 68);
    const D3D11_INPUT_ELEMENT_DESC el[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32_FLOAT, 0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 3, DXGI_FORMAT_R32_UINT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(I.dev->CreateInputLayout(el, 6, vsb->GetBufferPointer(), vsb->GetBufferSize(), &I.layout))) {
        error = "CreateInputLayout";
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
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;  // o GS não descarta faces
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = I.samples > 1;
    I.dev->CreateRasterizerState(&rd, &I.raster);
    return true;
}

bool DrvRenderer::upload(const CarModel& car, std::string& error) {
    Impl& I = *d_;
    if (car.vertices().empty() || car.indices().empty()) {
        error = "modelo sem vértices";
        return false;
    }
    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.ByteWidth = UINT(car.vertices().size() * sizeof(CarGpuVertex));
    D3D11_SUBRESOURCE_DATA sd{car.vertices().data(), 0, 0};
    if (FAILED(I.dev->CreateBuffer(&bd, &sd, &I.vb))) {
        error = "vertex buffer";
        return false;
    }
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.ByteWidth = UINT(car.indices().size() * 4);
    sd.pSysMem = car.indices().data();
    if (FAILED(I.dev->CreateBuffer(&bd, &sd, &I.ib))) {
        error = "index buffer";
        return false;
    }
    I.textures.clear();
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
        I.textures.push_back(srv);
    }
    float lo[3], hi[3];
    car.bounds(unsigned(std::max(0, I.opt.view)), lo, hi);
    for (int c = 0; c < 3; ++c) I.center[c] = 0.5f * (lo[c] + hi[c]);
    const float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
    I.radius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
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

void DrvRenderer::draw(const CarModel& car, float yaw, float pitch, float dist) {
    Impl& I = *d_;
    const std::string adapter_name = stats_.adapter;
    stats_ = DrvRenderStats();
    stats_.adapter = adapter_name;
    const float clear[4] = {0.18f, 0.20f, 0.23f, 1.0f};
    I.ctx->ClearRenderTargetView(I.rtv.Get(), clear);
    I.ctx->ClearDepthStencilView(I.dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    D3D11_VIEWPORT vp{0, 0, float(I.width), float(I.height), 0, 1};
    I.ctx->RSSetViewports(1, &vp);
    ID3D11RenderTargetView* rtv = I.rtv.Get();
    I.ctx->OMSetRenderTargets(1, &rtv, I.dsv.Get());
    I.ctx->RSSetState(I.raster.Get());
    I.ctx->IASetInputLayout(I.layout.Get());
    I.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(CarGpuVertex), offset = 0;
    ID3D11Buffer* vb = I.vb.Get();
    I.ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    I.ctx->IASetIndexBuffer(I.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    I.ctx->VSSetShader(I.vs.Get(), nullptr, 0);
    I.ctx->PSSetShader(I.ps.Get(), nullptr, 0);
    ID3D11Buffer* cbs[3] = {I.cb_frame.Get(), I.cb_draw.Get(), I.cb_object.Get()};
    I.ctx->VSSetConstantBuffers(0, 3, cbs);
    I.ctx->PSSetConstantBuffers(0, 3, cbs);

    // A matriz do carro: gira em torno do eixo vertical (y) pelo centro da caixa.
    FrameCB f{};
    float t[16], rot[16], world[16];
    mat_identity(t);
    t[12] = -I.center[0];
    t[13] = -I.center[1];
    t[14] = -I.center[2];
    mat_identity(rot);
    rot[0] = nf::fm::cos(yaw);
    rot[2] = -nf::fm::sin(yaw);
    rot[8] = nf::fm::sin(yaw);
    rot[10] = nf::fm::cos(yaw);
    mat_mul(t, rot, world);
    // A câmera: olha a origem de uma distância em função do raio, inclinada de `pitch` (mão esquerda, y para cima).
    const float d = I.radius * 2.2f * dist;
    const float eye[3] = {0.0f, d * nf::fm::sin(pitch), -d * nf::fm::cos(pitch)};
    float zaxis[3] = {-eye[0], -eye[1], -eye[2]};
    const float zl = std::sqrt(zaxis[0] * zaxis[0] + zaxis[1] * zaxis[1] + zaxis[2] * zaxis[2]);
    for (float& v : zaxis) v /= zl;
    float xaxis[3] = {zaxis[2], 0.0f, -zaxis[0]};  // up x z
    const float xl = std::sqrt(xaxis[0] * xaxis[0] + xaxis[2] * xaxis[2]);
    xaxis[0] /= xl;
    xaxis[2] /= xl;
    const float yaxis[3] = {zaxis[1] * xaxis[2] - zaxis[2] * xaxis[1], zaxis[2] * xaxis[0] - zaxis[0] * xaxis[2],
                            zaxis[0] * xaxis[1] - zaxis[1] * xaxis[0]};
    float view[16] = {xaxis[0], yaxis[0], zaxis[0], 0, xaxis[1], yaxis[1], zaxis[1], 0, xaxis[2], yaxis[2], zaxis[2], 0,
                      0,        0,        0,        1};
    for (int c = 0; c < 3; ++c) {
        const float* ax = c == 0 ? xaxis : c == 1 ? yaxis : zaxis;
        view[12 + c] = -(ax[0] * eye[0] + ax[1] * eye[1] + ax[2] * eye[2]);
    }
    const float zn = std::max(0.01f, d - I.radius * 1.5f), zf = d + I.radius * 1.5f;
    const float ys = 1.0f / nf::fm::tan(I.opt.fov_deg * 3.14159265f / 360.0f), xs = ys * float(I.height) / float(I.width);
    float proj[16] = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, zf / (zf - zn), 1, 0, 0, -zn * zf / (zf - zn), 0};
    mat_mul(view, proj, f.view_proj);
    const CarConstants& k = car.constants();
    std::memcpy(f.point_pos, k.point_pos, sizeof f.point_pos);
    std::memcpy(f.point_col, k.point_col, sizeof f.point_col);
    std::memcpy(f.fog, k.fog, 16);
    std::memcpy(f.light_dir_t, k.light_dir_t, sizeof f.light_dir_t);
    std::memcpy(f.light_col, k.light_col, sizeof f.light_col);
    std::memcpy(f.ambient, k.ambient, 16);
    // O SpecularMat: a rotação da câmera (as normais na vista; ver car_model.h).
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) f.spec_mat[r][c] = (r < 3 && c < 3) ? view[r * 4 + c] : (r == c ? 1.0f : 0.0f);
    std::memcpy(f.refl, k.refl_info2, 16);
    std::memcpy(f.damage, k.damage, sizeof f.damage);
    f.fog_color[0] = f.fog_color[1] = f.fog_color[2] = 128.0f;
    D3D11_MAPPED_SUBRESOURCE map;
    I.ctx->Map(I.cb_frame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
    std::memcpy(map.pData, &f, sizeof f);
    I.ctx->Unmap(I.cb_frame.Get(), 0);

    const float blend_factor[4] = {1, 1, 1, 1};
    // As peças nas instâncias do objeto articulado (gpModelMatrix = instância x carro), na ordem do CARP; sem
    // instâncias, cada peça uma vez com a matriz do carro.
    std::vector<std::vector<size_t>> by_part(car.parts().size());
    for (size_t i = 0; i < car.draws().size(); ++i)
        if (car.draws()[i].part >= 0 && size_t(car.draws()[i].part) < by_part.size())
            by_part[size_t(car.draws()[i].part)].push_back(i);
    struct Placed {
        int part;
        float m[16];
    };
    std::vector<Placed> placed;
    if (car.instances().empty()) {
        for (size_t p = 0; p < by_part.size(); ++p) {
            Placed pl{int(p), {}};
            std::memcpy(pl.m, world, 64);
            placed.push_back(pl);
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
            Placed pl{inst.part, {}};
            mat_mul(inst.matrix, world, pl.m);
            placed.push_back(pl);
        }
    }
    for (const Placed& pl : placed) {
        if (I.opt.only_part >= 0 && pl.part != I.opt.only_part) continue;
        ObjectCB ob;
        std::memcpy(ob.world, pl.m, 64);
        I.ctx->Map(I.cb_object.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
        std::memcpy(map.pData, &ob, sizeof ob);
        I.ctx->Unmap(I.cb_object.Get(), 0);
        ++stats_.instances;
        for (size_t di : by_part[size_t(pl.part)]) {
            const CarDraw& dr = car.draws()[di];
            if (dr.fallback && !I.opt.fallback) continue;
            for (int p = 0; p < 3; ++p) {
                if (!((dr.passes >> p) & 1)) continue;
                if (p == 1 && !I.opt.spec) continue;
                if (p == 2 && !I.opt.refl) continue;
                const eagl::GsRegs& g = dr.pass[p];
                const uint64_t prim = g.has_prim ? g.prim : 0x18;
                const bool tme = (prim >> 4) & 1, fge = (prim >> 5) & 1, abe = (prim >> 6) & 1;
                const int tex = dr.texture[p];
                DrawCB c{};
                std::memcpy(c.props, dr.props, 16);
                c.mode[0] = p;
                c.mode[1] = (tme && tex >= 0 && tex < int(I.textures.size()) && I.textures[size_t(tex)]) ? 1 : 0;
                c.mode[2] = fge ? 1 : 0;
                c.mode[3] = dr.fallback ? 1 : 0;
                c.gs[0] = int((g.tex0 >> 35) & 3);
                c.gs[1] = int((g.tex0 >> 34) & 1);
                const uint64_t test = g.has_test ? g.test : 0x50000;
                const bool ate = test & 1;
                const int atst = int(test >> 1) & 7;
                const int afail = int(test >> 12) & 3;
                c.gs[3] = atst;
                c.gs2[0] = float((test >> 4) & 0xFF);
                const bool zmsk = g.has_zbuf && ((g.zbuf >> 32) & 1);
                ID3D11ShaderResourceView* srv = c.mode[1] ? I.textures[size_t(tex)].Get() : nullptr;
                I.ctx->PSSetShaderResources(0, 1, &srv);
                ID3D11SamplerState* s = I.sampler(g.has_clamp ? g.clamp : 0);
                I.ctx->PSSetSamplers(0, 1, &s);
                // Sem teste de alfa: um desenho. Com teste: quem passa grava tudo; quem falha segue o AFAIL (0 nada, 1 só
                // o quadro, 2 só o Z, 3 só o RGB).
                const int sub_count = ate && afail != 0 ? 2 : 1;
                for (int sub = 0; sub < sub_count; ++sub) {
                    c.gs[2] = !ate ? 0 : sub == 0 ? 1 : 2;
                    int mask = D3D11_COLOR_WRITE_ENABLE_ALL;
                    bool zwrite = !zmsk;
                    if (sub == 1) {
                        if (afail == 1) zwrite = false;
                        else if (afail == 2) mask = 0;
                        else if (afail == 3) {
                            mask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
                            zwrite = false;
                        }
                    }
                    I.ctx->Map(I.cb_draw.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
                    std::memcpy(map.pData, &c, sizeof c);
                    I.ctx->Unmap(I.cb_draw.Get(), 0);
                    const float fix = float((g.alpha >> 32) & 0xFF) / 128.0f;
                    const float bf[4] = {fix, fix, fix, fix};
                    I.ctx->OMSetBlendState(I.blend(g.alpha, abe, mask), g.has_alpha ? bf : blend_factor, 0xFFFFFFFF);
                    I.ctx->OMSetDepthStencilState(I.depth_state(test, zwrite), 0);
                    I.ctx->DrawIndexed(dr.index_count, dr.first_index, 0);
                    ++stats_.draw_calls;
                }
                ++stats_.pass_calls[p];
                if (dr.fallback) ++stats_.fallback_calls;
            }
        }
    }
    if (I.samples > 1) I.ctx->ResolveSubresource(I.color.Get(), 0, I.color_ms.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
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
