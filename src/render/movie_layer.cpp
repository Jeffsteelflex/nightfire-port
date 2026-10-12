// O quadro dos filmes no Direct3D 11: ver movie_layer.h.

#include "render/movie_layer.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>

namespace nf {

namespace {

// Um triângulo que cobre o viewport (o retângulo do filme) com as coordenadas da imagem; a imagem amostrada com o
// Catmull-Rom separável em 9 leituras bilineares (os pesos do meio juntos numa leitura só, entre os dois texels).
const char kMovieShader[] = R"(
struct VsOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};
VsOut vs_movie(uint id : SV_VertexID) {
    VsOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = p;
    return o;
}
cbuffer Movie : register(b0) {
    float2 tex_size;
    float2 pad;
};
Texture2D img : register(t0);
SamplerState smp : register(s0);
float4 ps_movie(VsOut i) : SV_Target {
    float2 at = i.uv * tex_size;
    float2 c1 = floor(at - 0.5) + 0.5;  // o centro do texel à esquerda/acima do ponto
    float2 f = at - c1;
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);
    float2 w12 = w1 + w2;
    float2 t0 = (c1 - 1.0) / tex_size;
    float2 t12 = (c1 + w2 / w12) / tex_size;
    float2 t3 = (c1 + 2.0) / tex_size;
    float3 c = 0;
    c += img.SampleLevel(smp, float2(t0.x, t0.y), 0).rgb * (w0.x * w0.y);
    c += img.SampleLevel(smp, float2(t12.x, t0.y), 0).rgb * (w12.x * w0.y);
    c += img.SampleLevel(smp, float2(t3.x, t0.y), 0).rgb * (w3.x * w0.y);
    c += img.SampleLevel(smp, float2(t0.x, t12.y), 0).rgb * (w0.x * w12.y);
    c += img.SampleLevel(smp, float2(t12.x, t12.y), 0).rgb * (w12.x * w12.y);
    c += img.SampleLevel(smp, float2(t3.x, t12.y), 0).rgb * (w3.x * w12.y);
    c += img.SampleLevel(smp, float2(t0.x, t3.y), 0).rgb * (w0.x * w3.y);
    c += img.SampleLevel(smp, float2(t12.x, t3.y), 0).rgb * (w12.x * w3.y);
    c += img.SampleLevel(smp, float2(t3.x, t3.y), 0).rgb * (w3.x * w3.y);
    // O alfa do quadro como o de um desenho opaco (0x80 do IPU = 1.0 na escala do GS).
    return float4(saturate(c), 1.0);
}
)";

struct MovieCB {
    float tex_size[2];
    float pad[2];
};

Microsoft::WRL::ComPtr<ID3DBlob> compile(const char* entry, const char* target) {
    Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
    if (FAILED(D3DCompile(kMovieShader, sizeof(kMovieShader) - 1, "movie.hlsl", nullptr, nullptr, entry, target,
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors))) {
        return nullptr;
    }
    return code;
}

}  // namespace

bool MovieLayer::init(ID3D11Device* device) {
    device_ = device;
    auto vs = compile("vs_movie", "vs_5_0");
    auto ps = compile("ps_movie", "ps_5_0");
    if (vs == nullptr || ps == nullptr) return false;
    if (FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vs_))) return false;
    if (FAILED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &ps_))) return false;
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(MovieCB);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&bd, nullptr, &cb_))) return false;
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&sd, &sampler_))) return false;
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rd, &raster_))) return false;
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = FALSE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
    return SUCCEEDED(device->CreateDepthStencilState(&dd, &no_depth_));
}

void MovieLayer::upload(ID3D11DeviceContext* ctx, const uint8_t* bgra, int width, int height, uint64_t serial) {
    if (device_ == nullptr || bgra == nullptr || width <= 0 || height <= 0) return;
    if (tex_ == nullptr || width != width_ || height != height_) {
        tex_.Reset();
        srv_.Reset();
        D3D11_TEXTURE2D_DESC td{};
        td.Width = UINT(width);
        td.Height = UINT(height);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DYNAMIC;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &tex_))) return;
        if (FAILED(device_->CreateShaderResourceView(tex_.Get(), nullptr, &srv_))) {
            tex_.Reset();
            return;
        }
        width_ = width;
        height_ = height;
        serial_ = ~0ull;
    }
    if (serial == serial_) return;
    D3D11_MAPPED_SUBRESOURCE map;
    if (FAILED(ctx->Map(tex_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return;
    const size_t row = size_t(width) * 4;
    for (int y = 0; y < height; ++y) {
        std::memcpy(static_cast<uint8_t*>(map.pData) + size_t(y) * map.RowPitch, bgra + size_t(y) * row, row);
    }
    ctx->Unmap(tex_.Get(), 0);
    serial_ = serial;
}

void MovieLayer::draw(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv, int target_w, int target_h,
                      const float rect[4]) {
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    ctx->ClearRenderTargetView(rtv, black);
    if (srv_ == nullptr || ps_ == nullptr) return;
    const float x0 = std::clamp(rect[0], 0.0f, float(target_w)), y0 = std::clamp(rect[1], 0.0f, float(target_h));
    const float x1 = std::clamp(rect[2], 0.0f, float(target_w)), y1 = std::clamp(rect[3], 0.0f, float(target_h));
    if (x1 - x0 < 1.0f || y1 - y0 < 1.0f) return;
    D3D11_MAPPED_SUBRESOURCE map;
    if (FAILED(ctx->Map(cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return;
    MovieCB cb{{float(width_), float(height_)}, {0.0f, 0.0f}};
    std::memcpy(map.pData, &cb, sizeof(cb));
    ctx->Unmap(cb_.Get(), 0);
    // O viewport é o retângulo da imagem (cortado no alvo: o triângulo cobre só o que cabe).
    const D3D11_VIEWPORT vp{rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1], 0.0f, 1.0f};
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx->OMSetDepthStencilState(no_depth_.Get(), 0);
    ctx->RSSetState(raster_.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs_.Get(), nullptr, 0);
    ctx->PSSetShader(ps_.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, cb_.GetAddressOf());
    ctx->PSSetShaderResources(0, 1, srv_.GetAddressOf());
    ctx->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    ctx->PSSetShaderResources(0, 1, &none);
}

}  // namespace nf
