#include "render/remix_d3d9.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "core/xxh3.h"
#include "platform/psi_draw.h"
#include "platform/vu1.h"

namespace nf {

namespace {

// Vértice rígido (FVF: posição, normal, cor, uma coordenada de textura): 36 bytes.
struct V9 {
    float pos[3];
    float normal[3];
    DWORD color;  // D3DCOLOR (A, R, G, B), a cor do disco na escala do GS (0x80 = 1.0)
    float uv[2];
};
constexpr DWORD kFvf = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1;
static_assert(sizeof(V9) == 36, "o FVF rígido tem 36 bytes");

// Vértice com skinning (FVF com dois betas: o peso do primeiro osso e, no último em UBYTE4, os índices): 44 bytes.
// Com D3DRS_VERTEXBLEND = D3DVBF_1WEIGHTS e D3DRS_INDEXEDVERTEXBLENDENABLE, a posição vai ao mundo por
// w * (p * WORLDMATRIX(i0)) + (1 - w) * (p * WORLDMATRIX(i1)), a conta do vs_skin do Direct3D 11.
struct VSkin9 {
    float pos[3];
    float weight;
    DWORD bones;  // UBYTE4: byte 0 = osso com o peso, byte 1 = osso com 1 - peso
    float normal[3];
    DWORD color;
    float uv[2];
};
constexpr DWORD kFvfSkin = D3DFVF_XYZB2 | D3DFVF_LASTBETA_UBYTE4 | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1;
static_assert(sizeof(VSkin9) == 44, "o FVF com skinning tem 44 bytes");

void check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Direct3D 9: %s falhou (HRESULT 0x%08lX)", what, static_cast<unsigned long>(hr));
        throw std::runtime_error(buf);
    }
}

DWORD gs_colour(const uint8_t c[4]) { return D3DCOLOR_ARGB(c[3], c[0], c[1], c[2]); }

DWORD fbits(float f) {
    DWORD d;
    std::memcpy(&d, &f, 4);
    return d;
}

const D3DMATRIX& as_d3d(const float* m) { return *reinterpret_cast<const D3DMATRIX*>(m); }

// a * b, matrizes de vetor-linha (v' = v * a * b).
std::array<float, 16> mul(const float* a, const float* b) {
    std::array<float, 16> o{};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a[r * 4 + k] * b[k * 4 + c];
            o[size_t(r * 4 + c)] = s;
        }
    }
    return o;
}

bool is_identity_tint(const std::array<float, 4>& t) {
    return t[0] >= 0.999f && t[1] >= 0.999f && t[2] >= 0.999f && t[3] >= 0.999f;
}

// O estado efetivo de uma parte, como o resolve_state do Direct3D 11 (render/renderer.cpp, que continua a
// referência): a parte da malha, depois o desenho, depois a regra antiga pelo modo de psiSetUpColourBlend (opaco
// testa alfa em 50% e escreve Z; misturado descarta só alfa < 0.004 e não escreve Z). A profundidade aqui é a comum
// (perto = 0), então GEQUAL do GS vira LESSEQUAL e GREATER vira LESS.
struct State9 {
    uint64_t alpha = 0;
    bool abe = true;
    int atst = -1;  // ATST do GS; -1 sem teste
    float aref = 0.0f;
    D3DCMPFUNC zfunc = D3DCMP_LESSEQUAL;
    bool zwrite = true;
};

State9 resolve(const GsState& own, const DrawItem& item) {
    State9 s;
    const bool opaque = item.blend == Blend::Opaque;
    s.alpha = (own.has & GsState::kAlpha) ? own.alpha
                                           : ((item.gs.has & GsState::kAlpha) ? item.gs.alpha : gs_alpha_of_blend(item.blend));
    s.abe = own.abe != 0;
    if (((own.has | item.gs.has) & GsState::kTest) != 0) {
        const uint64_t test = (own.has & GsState::kTest) ? own.test : item.gs.test;
        if (test & 1) {
            s.atst = int((test >> 1) & 7);
            s.aref = float((test >> 4) & 0xff);
        }
        if (((test >> 16) & 1) == 0) {
            s.zfunc = D3DCMP_ALWAYS;
        } else {
            switch ((test >> 17) & 3) {
                case 0: s.zfunc = D3DCMP_NEVER; break;
                case 1: s.zfunc = D3DCMP_ALWAYS; break;
                case 2: s.zfunc = D3DCMP_LESSEQUAL; break;
                default: s.zfunc = D3DCMP_LESS; break;
            }
        }
    } else {
        s.atst = 5;  // GEQUAL
        s.aref = opaque ? 64.0f : 0.004f * 128.0f;
    }
    if (((own.has | item.gs.has) & GsState::kZbuf) != 0) {
        const uint64_t zbuf = (own.has & GsState::kZbuf) ? own.zbuf : item.gs.zbuf;
        s.zwrite = ((zbuf >> 32) & 1) == 0;
    } else {
        s.zwrite = opaque;
    }
    return s;
}

D3DBLEND d3d9_factor(BlendFactor f) {
    switch (f) {
        case BlendFactor::Zero: return D3DBLEND_ZERO;
        case BlendFactor::One: return D3DBLEND_ONE;
        case BlendFactor::SrcAlpha: return D3DBLEND_SRCALPHA;
        case BlendFactor::InvSrcAlpha: return D3DBLEND_INVSRCALPHA;
        case BlendFactor::DstAlpha: return D3DBLEND_DESTALPHA;
        case BlendFactor::InvDstAlpha: return D3DBLEND_INVDESTALPHA;
        case BlendFactor::Fix: return D3DBLEND_BLENDFACTOR;
        default: return D3DBLEND_INVBLENDFACTOR;
    }
}

D3DBLENDOP d3d9_op(BlendOp op) {
    switch (op) {
        case BlendOp::Subtract: return D3DBLENDOP_SUBTRACT;
        case BlendOp::RevSubtract: return D3DBLENDOP_REVSUBTRACT;
        default: return D3DBLENDOP_ADD;
    }
}

D3DCMPFUNC gs_atst(int atst) {
    switch (atst) {
        case 0: return D3DCMP_NEVER;
        case 2: return D3DCMP_LESS;
        case 3: return D3DCMP_LESSEQUAL;
        case 4: return D3DCMP_EQUAL;
        case 5: return D3DCMP_GREATEREQUAL;
        case 6: return D3DCMP_GREATER;
        case 7: return D3DCMP_NOTEQUAL;
        default: return D3DCMP_ALWAYS;
    }
}

// A névoa do GS como névoa linear da função fixa. No PS2 o que fica da cor do pixel é vu1::fog_keep(vu1::gs_z(z)), uma
// curva em 1 / z (A / z + B), que nenhuma névoa do Direct3D 9 (linear, EXP, EXP2) reproduz. A reta começa onde a curva
// começa a cair de 1 e passa por 50% onde ela passa (a névoa some antes e fecha mais cedo que no PS2, mas o meio, onde
// está quase tudo o que se vê, bate). O Remix lê só o modo, a cor e o começo e o fim.
float fog_first_below(float far1, float level, bool or_equal) {
    const auto below = [far1, level, or_equal](float z) {
        const float k = vu1::fog_keep(vu1::gs_z(z, far1));
        return or_equal ? k <= level : k < level;
    };
    constexpr int kSteps = 600;
    const float lo = 0.1f, hi = 200000.0f;
    const float ratio = std::pow(hi / lo, 1.0f / float(kSteps));
    float prev = lo;
    for (int i = 0; i <= kSteps; ++i) {
        const float z = lo * std::pow(ratio, float(i));
        if (below(z)) {
            float a = prev, b = z;
            for (int it = 0; it < 30; ++it) {
                const float m = 0.5f * (a + b);
                (below(m) ? b : a) = m;
            }
            return b;
        }
        prev = z;
    }
    return hi;
}

void fog_range(float far1, float* start, float* end) {
    *start = fog_first_below(far1, 1.0f, false);
    const float half = fog_first_below(far1, 0.5f, true);
    const float full = fog_first_below(far1, 0.0f, true);
    *end = std::clamp(2.0f * half - *start, *start + 0.01f, std::max(full, *start + 0.01f));
}

// Um nível de mipmap a partir do anterior (BGRA8), média 2 x 2 (os lados ímpares repetem a última coluna ou linha).
std::vector<uint8_t> half(const std::vector<uint8_t>& src, int w, int h, int* ow, int* oh) {
    const int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
    std::vector<uint8_t> out(size_t(nw) * size_t(nh) * 4);
    for (int y = 0; y < nh; ++y) {
        for (int x = 0; x < nw; ++x) {
            const int x0 = std::min(w - 1, 2 * x), x1 = std::min(w - 1, 2 * x + 1);
            const int y0 = std::min(h - 1, 2 * y), y1 = std::min(h - 1, 2 * y + 1);
            for (int c = 0; c < 4; ++c) {
                const int s = src[(size_t(y0) * w + x0) * 4 + c] + src[(size_t(y0) * w + x1) * 4 + c] +
                              src[(size_t(y1) * w + x0) * 4 + c] + src[(size_t(y1) * w + x1) * 4 + c];
                out[(size_t(y) * nw + x) * 4 + c] = uint8_t((s + 2) / 4);
            }
        }
    }
    *ow = nw;
    *oh = nh;
    return out;
}

LRESULT CALLBACK hidden_proc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

}  // namespace

HWND RemixD3D9Renderer::create_hidden_window(int width, int height) {
    static bool registered = false;
    const HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = hidden_proc;
        wc.hInstance = inst;
        wc.lpszClassName = L"NightfireD3D11Oculta";
        RegisterClassW(&wc);
        registered = true;
    }
    // WS_POPUP: a área do cliente tem width x height (o Renderer offscreen cria a cadeia de troca desse tamanho).
    return CreateWindowExW(WS_EX_TOOLWINDOW, L"NightfireD3D11Oculta", L"Nightfire D3D11 (oculta)", WS_POPUP, 0, 0,
                           std::max(1, width), std::max(1, height), nullptr, nullptr, inst, nullptr);
}

RemixD3D9Renderer::RemixD3D9Renderer(HWND hwnd, int width, int height, bool vsync, const AdapterInfo* adapter)
    : hwnd_(hwnd), width_(std::max(1, width)), height_(std::max(1, height)), vsync_(vsync) {
    // O d3d9.dll só é carregado neste modo, e pela ordem de busca comum: com o RTX Remix instalado ao lado do
    // executável, é o d3d9.dll da ponte dele (o Direct3D 11, o padrão, nunca o carrega).
    HMODULE lib = LoadLibraryW(L"d3d9.dll");
    using CreateFn = IDirect3D9*(WINAPI*)(UINT);
    const auto create = lib != nullptr ? reinterpret_cast<CreateFn>(GetProcAddress(lib, "Direct3DCreate9")) : nullptr;
    if (create == nullptr) throw std::runtime_error("d3d9.dll sem Direct3DCreate9");
    d3d_.Attach(create(D3D_SDK_VERSION));
    if (d3d_ == nullptr) throw std::runtime_error("Direct3DCreate9 falhou (sem Direct3D 9)");
    char path[MAX_PATH] = {};
    GetModuleFileNameA(lib, path, MAX_PATH);
    module_ = path;
    // O adaptador (só do port, [video] adapter): o do Direct3D 11 pelo fabricante e modelo, se o Direct3D 9 o lista.
    {
        D3DADAPTER_IDENTIFIER9 id{};
        bool followed = false;
        if (adapter != nullptr) {
            for (UINT i = 0; i < d3d_->GetAdapterCount(); ++i) {
                if (SUCCEEDED(d3d_->GetAdapterIdentifier(i, 0, &id)) && id.VendorId == adapter->vendor_id &&
                    id.DeviceId == adapter->device_id) {
                    ordinal_ = i;
                    followed = true;
                    break;
                }
            }
        }
        if (SUCCEEDED(d3d_->GetAdapterIdentifier(ordinal_, 0, &id))) {
            adapter_note_ = std::to_string(ordinal_) + " \"" + id.Description + "\"";
        }
        if (adapter != nullptr) adapter_note_ += followed ? " (o do Direct3D 11)" : " (padrao: o do Direct3D 11 nao esta na lista)";
    }
    pp_.BackBufferWidth = UINT(width_);
    pp_.BackBufferHeight = UINT(height_);
    // O alfa do alvo: as misturas do GS com C = Ad (as máscaras 0x64/0x58) leem o alfa gravado.
    pp_.BackBufferFormat = SUCCEEDED(d3d_->CheckDeviceType(ordinal_, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8,
                                                           D3DFMT_A8R8G8B8, TRUE))
                               ? D3DFMT_A8R8G8B8
                               : D3DFMT_X8R8G8B8;
    pp_.BackBufferCount = 1;
    pp_.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp_.hDeviceWindow = hwnd_;
    pp_.Windowed = TRUE;
    pp_.EnableAutoDepthStencil = TRUE;
    pp_.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp_.PresentationInterval = vsync_ ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    // FPU_PRESERVE: a lógica do jogo conta com a FPU como o port a deixa (o Direct3D 9 e a ponte do Remix poriam
    // precisão simples e arredondamento para o mais perto).
    const DWORD base = D3DCREATE_FPU_PRESERVE;
    HRESULT hr = d3d_->CreateDevice(ordinal_, D3DDEVTYPE_HAL, hwnd_, base | D3DCREATE_MIXED_VERTEXPROCESSING,
                                    &pp_, &dev_);
    if (SUCCEEDED(hr)) {
        mixed_ = true;
    } else {
        hr = d3d_->CreateDevice(ordinal_, D3DDEVTYPE_HAL, hwnd_, base | D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                &pp_, &dev_);
        if (FAILED(hr)) {
            check(d3d_->CreateDevice(ordinal_, D3DDEVTYPE_HAL, hwnd_,
                                     base | D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp_, &dev_),
                  "CreateDevice");
            software_ = true;
        }
    }
    dev_->GetDeviceCaps(&caps_);
    // A textura branca dos desenhos sem textura (como a white_ do Direct3D 11).
    check(dev_->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &white_, nullptr), "textura branca");
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(white_->LockRect(0, &lr, nullptr, 0))) {
        *static_cast<DWORD*>(lr.pBits) = 0xffffffffu;
        white_->UnlockRect(0);
    }
    // kQuadNoTexture (o sprite de psiFadeView): cor 0x80 (x 2 do MODULATE2X = 1.0) e alfa 0xff (o alfa do GS dobrado,
    // como nas texturas do jogo), para a cor e o alfa do vértice valerem como no GS sem textura.
    check(dev_->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &no_texture_, nullptr), "textura sem textura");
    if (SUCCEEDED(no_texture_->LockRect(0, &lr, nullptr, 0))) {
        *static_cast<DWORD*>(lr.pBits) = 0xff808080u;
        no_texture_->UnlockRect(0);
    }
}

RemixD3D9Renderer::~RemixD3D9Renderer() = default;

void RemixD3D9Renderer::mirror_reset() {
    textures_.clear();
    hashes_.clear();
    meshes_.clear();
    textures_count_ = meshes_count_ = software_meshes_ = failed_textures_ = failed_meshes_ = 0;
}

void RemixD3D9Renderer::mirror_texture(int slot, const Texture& t) {
    if (slot < 0) return;
    // Um slot recriado perde a textura antiga já aqui: se a nova falhar, o slot fica branco, não com a de outra fase.
    if (slot < int(textures_.size())) textures_[size_t(slot)].Reset();
    if (slot < int(hashes_.size())) hashes_[size_t(slot)] = 0;
    if (t.width <= 0 || t.height <= 0 || t.rgba.size() < size_t(t.width) * size_t(t.height) * 4) {
        ++failed_textures_;
        return;
    }
    // Nível 0: RGBA com o alfa do GS (0x80 = opaco) -> BGRA com o alfa comum (0xff = opaco, saturado).
    std::vector<uint8_t> level(size_t(t.width) * size_t(t.height) * 4);
    for (size_t i = 0; i < level.size(); i += 4) {
        level[i + 0] = t.rgba[i + 2];
        level[i + 1] = t.rgba[i + 1];
        level[i + 2] = t.rgba[i + 0];
        level[i + 3] = uint8_t(std::min(255, int(t.rgba[i + 3]) * 2));
    }
    // Depuração: NF_D3D9_DUMP=<pasta> grava o nível 0 de cada textura enviada (dados do jogo: só em out/).
    char dir[MAX_PATH] = {};
    if (GetEnvironmentVariableA("NF_D3D9_DUMP", dir, MAX_PATH) > 0) {
        std::vector<uint8_t> rgba(level.size());
        for (size_t i = 0; i < level.size(); i += 4) {
            rgba[i] = level[i + 2];
            rgba[i + 1] = level[i + 1];
            rgba[i + 2] = level[i];
            rgba[i + 3] = level[i + 3];
        }
        wchar_t name[64];
        swprintf(name, 64, L"\\slot_%04d.png", slot);
        std::wstring path(dir, dir + std::strlen(dir));
        Renderer::write_png(path + name, t.width, t.height, rgba.data());
    }
    const uint64_t level0_hash = xxh3_64(level.data(), level.size());
    int levels = 1;
    for (int s = std::max(t.width, t.height); s > 1; s /= 2) ++levels;
    Com<IDirect3DTexture9> tex;
    if (FAILED(dev_->CreateTexture(UINT(t.width), UINT(t.height), UINT(levels), 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                   &tex, nullptr))) {
        ++failed_textures_;
        return;
    }
    int w = t.width, h = t.height;
    for (int l = 0; l < levels; ++l) {
        D3DLOCKED_RECT lr;
        if (FAILED(tex->LockRect(UINT(l), &lr, nullptr, 0))) {
            ++failed_textures_;
            return;
        }
        for (int y = 0; y < h; ++y) {
            std::memcpy(static_cast<uint8_t*>(lr.pBits) + size_t(y) * size_t(lr.Pitch), level.data() + size_t(y) * w * 4,
                        size_t(w) * 4);
        }
        tex->UnlockRect(UINT(l));
        if (l + 1 < levels) level = half(level, w, h, &w, &h);
    }
    if (slot >= int(textures_.size())) textures_.resize(size_t(slot) + 1);
    textures_[size_t(slot)] = tex;
    if (slot >= int(hashes_.size())) hashes_.resize(size_t(slot) + 1);
    hashes_[size_t(slot)] = level0_hash;
    ++textures_count_;
}

void RemixD3D9Renderer::mirror_mesh(int id, const EntityMesh& mesh) {
    if (id < 0) return;
    if (id < int(meshes_.size())) meshes_[size_t(id)] = Mesh9{};
    if (mesh.empty()) return;
    Mesh9 m;
    m.skinned = mesh.skinned;
    m.bones = mesh.bone_count;
    const size_t nverts = mesh.skinned ? mesh.skin_vertices.size() : mesh.vertices.size();
    if (nverts == 0) return;
    // Mais ossos do que o hardware faz: o desenho vai por software (só com o dispositivo misto ou de software).
    m.software = mesh.skinned && !software_ && m.bones - 1 > int(caps_.MaxVertexBlendMatrixIndex);
    const DWORD usage = D3DUSAGE_WRITEONLY | ((m.software || software_) ? D3DUSAGE_SOFTWAREPROCESSING : 0);
    const UINT stride = mesh.skinned ? UINT(sizeof(VSkin9)) : UINT(sizeof(V9));
    if (FAILED(dev_->CreateVertexBuffer(UINT(nverts) * stride, usage, mesh.skinned ? kFvfSkin : kFvf, D3DPOOL_MANAGED,
                                        &m.vb, nullptr))) {
        ++failed_meshes_;
        return;
    }
    void* data = nullptr;
    if (FAILED(m.vb->Lock(0, 0, &data, 0))) {
        ++failed_meshes_;
        return;
    }
    if (mesh.skinned) {
        auto* out = static_cast<VSkin9*>(data);
        for (const SkinnedVertex& v : mesh.skin_vertices) {
            VSkin9 o{};
            std::memcpy(o.pos, v.pos, sizeof(o.pos));
            o.weight = v.weight;
            std::memcpy(&o.bones, v.bones, 4);
            std::memcpy(o.normal, v.normal, sizeof(o.normal));
            o.color = gs_colour(v.color);
            std::memcpy(o.uv, v.uv, sizeof(o.uv));
            *out++ = o;
        }
    } else {
        auto* out = static_cast<V9*>(data);
        for (const Vertex& v : mesh.vertices) {
            V9 o{};
            std::memcpy(o.pos, v.pos, sizeof(o.pos));
            std::memcpy(o.normal, v.normal, sizeof(o.normal));
            o.color = gs_colour(v.color);
            std::memcpy(o.uv, v.uv, sizeof(o.uv));
            *out++ = o;
        }
    }
    m.vb->Unlock();
    // Índices de 16 bits quando cabem (o caso de quase todas as malhas), de 32 nos outros.
    const bool idx16 = nverts <= 0xffff;
    const UINT ibytes = UINT(mesh.indices.size() * (idx16 ? 2 : 4));
    if (FAILED(dev_->CreateIndexBuffer(ibytes, usage, idx16 ? D3DFMT_INDEX16 : D3DFMT_INDEX32, D3DPOOL_MANAGED, &m.ib,
                                       nullptr)) ||
        FAILED(m.ib->Lock(0, 0, &data, 0))) {
        ++failed_meshes_;
        return;
    }
    if (idx16) {
        auto* out = static_cast<uint16_t*>(data);
        for (uint32_t i : mesh.indices) *out++ = uint16_t(i);
    } else {
        std::memcpy(data, mesh.indices.data(), ibytes);
    }
    m.ib->Unlock();
    for (const SubMesh& sm : mesh.submeshes) {
        if (sm.index_count < 3 || size_t(sm.first_index) + sm.index_count > mesh.indices.size()) continue;
        Part p;
        p.texture = sm.texture;
        p.first_index = sm.first_index;
        p.index_count = sm.index_count - sm.index_count % 3;
        p.gs = sm.gs;
        p.env = sm.env;
        uint32_t lo = 0xffffffffu, hi = 0;
        for (uint32_t k = 0; k < p.index_count; ++k) {
            const uint32_t i = mesh.indices[p.first_index + k];
            lo = std::min(lo, i);
            hi = std::max(hi, i);
        }
        p.min_vertex = lo;
        p.num_vertices = hi - lo + 1;
        m.parts.push_back(p);
    }
    if (id >= int(meshes_.size())) meshes_.resize(size_t(id) + 1);
    meshes_[size_t(id)] = std::move(m);
    ++meshes_count_;
    if (meshes_[size_t(id)].software) ++software_meshes_;
}

IDirect3DTexture9* RemixD3D9Renderer::texture(int slot) {
    if (slot == kQuadNoTexture) return no_texture_.Get();
    if (slot >= 0 && slot < int(textures_.size()) && textures_[size_t(slot)] != nullptr) return textures_[size_t(slot)].Get();
    if (slot >= 0) ++stats_.missing_textures;
    return white_.Get();
}

uint64_t RemixD3D9Renderer::texture_hash(int slot) const {
    return slot >= 0 && slot < int(hashes_.size()) ? hashes_[size_t(slot)] : 0;
}

void RemixD3D9Renderer::record(std::set<uint64_t>& set, int slot) {
    if (const uint64_t h = texture_hash(slot); h != 0) set.insert(h);
}

bool RemixD3D9Renderer::begin_frame() {
    if (!lost_) return true;
    const HRESULT hr = dev_->TestCooperativeLevel();
    if (hr == D3DERR_DEVICENOTRESET) {
        // Tudo é D3DPOOL_MANAGED (e a profundidade automática): o Reset não perde nada.
        if (FAILED(dev_->Reset(&pp_))) return false;
        lights_enabled_ = 0;
        lost_ = false;
        return true;
    }
    return hr == D3D_OK ? (lost_ = false, true) : false;
}

void RemixD3D9Renderer::set_frame_states(const D3D9FrameParams& frame) {
    dev_->SetRenderState(D3DRS_LIGHTING, FALSE);  // a cor do vértice já é a iluminação (as cores do disco)
    dev_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);  // o jogo desenha faces dos dois lados
    dev_->SetRenderState(D3DRS_NORMALIZENORMALS, TRUE);
    dev_->SetRenderState(D3DRS_COLORVERTEX, TRUE);
    dev_->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    dev_->SetRenderState(D3DRS_DITHERENABLE, FALSE);
    dev_->SetRenderState(D3DRS_FILLMODE, frame.wireframe ? D3DFILL_WIREFRAME : D3DFILL_SOLID);
    // Como no GS, o alfa gravado no alvo é o do pixel (As); a mistura só mexe em r, g e b.
    dev_->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
    dev_->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
    dev_->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO);
    dev_->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
    dev_->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev_->SetRenderState(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
    dev_->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_NONE);
    dev_->SetRenderState(D3DRS_RANGEFOGENABLE, FALSE);
    dev_->SetRenderState(D3DRS_FOGENABLE, FALSE);
    // Texturas do 3D: repetidas, bilinear com mipmaps e anisotrópico até 8x (o padrão do Direct3D 11).
    const DWORD aniso = std::min<DWORD>(8, caps_.MaxAnisotropy);
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    dev_->SetSamplerState(0, D3DSAMP_MINFILTER, aniso > 1 ? D3DTEXF_ANISOTROPIC : D3DTEXF_LINEAR);
    dev_->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev_->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    dev_->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, std::max<DWORD>(1, aniso));
    dev_->SetSamplerState(0, D3DSAMP_MAXMIPLEVEL, 0);
    // Estágio 0: textura x cor do vértice x 2 (a cor do disco está na escala do GS, 0x80 = 1.0), o padrão das
    // texturas com luz "assada" na cor dos vértices, que o Remix reconhece (Modulate2x). O tweak entra no estágio 1.
    dev_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE2X);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dev_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev_->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev_->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev_->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
    dev_->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
    dev_->SetTransform(D3DTS_VIEW, &as_d3d(frame.view.data()));
    dev_->SetTransform(D3DTS_PROJECTION, &as_d3d(frame.projection.data()));
    const D3DVIEWPORT9 vp{0, 0, DWORD(width_), DWORD(height_), 0.0f, 1.0f};
    dev_->SetViewport(&vp);
}

// As luzes da VU1 do quadro (psiLight_SetLights: as duas mais perto de cada desenho) no mundo, sem repetição, como
// D3DLIGHT9 pontuais. Com D3DRS_LIGHTING desligado elas não mudam a imagem rasterizada; o Remix as lê do estado do
// dispositivo e as converte em luzes do path tracer. A queda da VU1 (1 / d² - 1 / r², vezes N . D sem normalizar) não
// existe na função fixa: aqui alcance r, queda 1 / d² e cor = TColor / 128 (a escala do GS).
void RemixD3D9Renderer::set_lights(const std::vector<DrawItem>& items) {
    struct L {
        float pos[3];
        float col[3];
        float range;
    };
    std::vector<L> lights;
    const DWORD max_lights = std::min<DWORD>(8, std::max<DWORD>(1, caps_.MaxActiveLights));
    for (const DrawItem& it : items) {
        if (it.mesh < 0 || it.num_lights == 0) continue;
        for (int k = 0; k < std::min<int>(2, it.num_lights); ++k) {
            L l{};
            const float* p = it.light_pos[k];
            if (it.palette.empty()) {
                // A luz foi levada ao espaço do objeto por (L - t) * R^T (vu1::lights_to_local): volta pelo mundo.
                const float* w = it.world.data();
                for (int c = 0; c < 3; ++c) l.pos[c] = p[0] * w[0 + c] + p[1] * w[4 + c] + p[2] * w[8 + c] + w[12 + c];
            } else {
                std::memcpy(l.pos, p, sizeof(l.pos));
            }
            for (int c = 0; c < 3; ++c) l.col[c] = it.light_color[k][c] / 128.0f;
            l.range = it.light_color[k][3] > 0.0f ? 1.0f / std::sqrt(it.light_color[k][3]) : 100.0f;
            if (l.col[0] + l.col[1] + l.col[2] <= 0.0f) continue;
            bool same = false;
            for (const L& o : lights) {
                same = same || (std::fabs(o.pos[0] - l.pos[0]) < 0.01f && std::fabs(o.pos[1] - l.pos[1]) < 0.01f &&
                                std::fabs(o.pos[2] - l.pos[2]) < 0.01f && std::fabs(o.col[0] - l.col[0]) < 0.01f &&
                                std::fabs(o.col[1] - l.col[1]) < 0.01f && std::fabs(o.col[2] - l.col[2]) < 0.01f);
            }
            if (!same && lights.size() < max_lights) lights.push_back(l);
        }
    }
    for (DWORD i = 0; i < DWORD(lights.size()); ++i) {
        D3DLIGHT9 d{};
        d.Type = D3DLIGHT_POINT;
        d.Diffuse = {lights[i].col[0], lights[i].col[1], lights[i].col[2], 1.0f};
        d.Position = {lights[i].pos[0], lights[i].pos[1], lights[i].pos[2]};
        d.Range = lights[i].range;
        d.Attenuation2 = 1.0f;
        dev_->SetLight(i, &d);
        dev_->LightEnable(i, TRUE);
    }
    for (DWORD i = DWORD(lights.size()); i < lights_enabled_; ++i) dev_->LightEnable(i, FALSE);
    lights_enabled_ = DWORD(lights.size());
    stats_.lights = DWORD(lights.size());
}

// O estado do GS de uma parte (mistura, teste de alfa, Z), o tweak e a névoa.
void RemixD3D9Renderer::apply_state(const DrawItem& item, const GsState& own, bool sprite, bool env, bool fog) {
    const State9 st = resolve(own, item);
    const GsBlendEq gb = gs_blend_equation(st.alpha, st.abe, false);
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, gb.enable ? TRUE : FALSE);
    if (gb.enable) {
        dev_->SetRenderState(D3DRS_SRCBLEND, d3d9_factor(gb.src));
        dev_->SetRenderState(D3DRS_DESTBLEND, d3d9_factor(gb.dst));
        dev_->SetRenderState(D3DRS_BLENDOP, d3d9_op(gb.op));
        const DWORD f = DWORD(std::clamp(gb.fix, 0.0f, 1.0f) * 255.0f + 0.5f);
        dev_->SetRenderState(D3DRS_BLENDFACTOR, D3DCOLOR_ARGB(f, f, f, f));
    }
    // O teste de alfa compara o alfa do pixel (As: 0x80 do GS = 1.0 = 0xff aqui) com AREF na escala do GS.
    dev_->SetRenderState(D3DRS_ALPHATESTENABLE, st.atst >= 0 ? TRUE : FALSE);
    if (st.atst >= 0) {
        dev_->SetRenderState(D3DRS_ALPHAFUNC, gs_atst(st.atst));
        dev_->SetRenderState(D3DRS_ALPHAREF, DWORD(std::clamp(std::lround(st.aref * 255.0f / 128.0f), 0L, 255L)));
    }
    dev_->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev_->SetRenderState(D3DRS_ZFUNC, st.zfunc);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, st.zwrite ? TRUE : FALSE);
    // O tweak (TAmbient) multiplica cor e alfa: um segundo estágio com D3DTA_TFACTOR (os sprites não o usam).
    if (!sprite && !is_identity_tint(item.tint)) {
        const auto b = [](float v) { return DWORD(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        dev_->SetRenderState(D3DRS_TEXTUREFACTOR,
                             D3DCOLOR_ARGB(b(item.tint[3]), b(item.tint[0]), b(item.tint[1]), b(item.tint[2])));
        dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_MODULATE);
        dev_->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_CURRENT);
        dev_->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_TFACTOR);
        dev_->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        dev_->SetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
        dev_->SetTextureStageState(1, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
        dev_->SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
        dev_->SetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    } else {
        dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        dev_->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    }
    // Rotina 0x2e0 (mapa de ambiente): as partes dela usam as coordenadas guardadas na malha, sem gerar ST pela normal.
    // A geração da função fixa (D3DTSS_TCI_CAMERASPACENORMAL com D3DTS_TEXTURE0) foi tentada: no Direct3D 9 nativo
    // desta máquina ela estragou as coordenadas de todos os desenhos seguintes do quadro (o 2D inclusive) em 0700000c,
    // mesmo voltando TEXCOORDINDEX, TEXTURETRANSFORMFLAGS e a matriz; e o Remix lê as coordenadas do vertex buffer, não
    // as geradas. As superfícies cromadas ficam para um material do Remix (metálico).
    if (env) ++stats_.env_parts;
    dev_->SetRenderState(D3DRS_FOGENABLE, fog ? TRUE : FALSE);
}

void RemixD3D9Renderer::draw_mesh(const DrawItem& item, bool fog) {
    if (item.mesh < 0 || item.mesh >= int(meshes_.size()) || meshes_[size_t(item.mesh)].vb == nullptr) {
        ++stats_.skipped;
        return;
    }
    const Mesh9& m = meshes_[size_t(item.mesh)];
    const bool skinned = !item.palette.empty();
    if (skinned != m.skinned || (skinned && item.palette.size() < size_t(m.bones))) {
        ++stats_.skipped;
        return;
    }
    bool sw = false;
    if (skinned) {
        // Vertex blending indexado: WORLDMATRIX(i) = paleta[i] (já no mundo, com a pose inversa) x a matriz do desenho
        // (a identidade nos desenhos com skinning, como no PS2).
        if (m.software && !mixed_) {
            ++stats_.skipped;
            return;
        }
        dev_->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_1WEIGHTS);
        dev_->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, TRUE);
        for (int b = 0; b < m.bones; ++b) {
            const std::array<float, 16> w = mul(item.palette[size_t(b)].data(), item.world.data());
            dev_->SetTransform(D3DTS_WORLDMATRIX(b), &as_d3d(w.data()));
        }
        if (m.software && mixed_) {
            dev_->SetSoftwareVertexProcessing(TRUE);
            sw = true;
        }
        dev_->SetFVF(kFvfSkin);
        dev_->SetStreamSource(0, m.vb.Get(), 0, sizeof(VSkin9));
    } else {
        dev_->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
        dev_->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
        dev_->SetTransform(D3DTS_WORLD, &as_d3d(item.world.data()));
        dev_->SetFVF(kFvf);
        dev_->SetStreamSource(0, m.vb.Get(), 0, sizeof(V9));
    }
    dev_->SetIndices(m.ib.Get());
    for (const Part& p : m.parts) {
        apply_state(item, p.gs, false, p.env, fog);
        dev_->SetTexture(0, texture(p.texture));
        dev_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, p.min_vertex, p.num_vertices, p.first_index, p.index_count / 3);
        ++(skinned ? stats_.skinned : stats_.rigid);
    }
    if (sw) {
        dev_->SetSoftwareVertexProcessing(FALSE);
        ++stats_.swvp;
    }
    if (skinned) {
        dev_->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
        dev_->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
    }
}

// Partículas e estilhaços: vértices já no mundo, desenhados direto da memória (DrawPrimitiveUP), sem o tweak (como o
// microprograma de partículas e a rotina 0 dos estilhaços). O Remix não acha hash estável neles: são as categorias de
// partícula (rtx.particleTextures) e de geometria dinâmica.
void RemixD3D9Renderer::draw_loose(const std::vector<Vertex>& tris, int tex, const DrawItem& item, bool fog) {
    if (tris.size() < 3) return;
    std::vector<V9> v(tris.size());
    for (size_t i = 0; i < tris.size(); ++i) {
        std::memcpy(v[i].pos, tris[i].pos, sizeof(v[i].pos));
        std::memcpy(v[i].normal, tris[i].normal, sizeof(v[i].normal));
        v[i].color = gs_colour(tris[i].color);
        std::memcpy(v[i].uv, tris[i].uv, sizeof(v[i].uv));
    }
    static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    dev_->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
    dev_->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
    dev_->SetTransform(D3DTS_WORLD, &as_d3d(kIdentity));
    dev_->SetFVF(kFvf);
    apply_state(item, GsState{}, true, false, fog);
    dev_->SetTexture(0, texture(tex));
    dev_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, UINT(v.size() / 3), v.data(), sizeof(V9));
}

// 2D (psiDrawSprites): quadrados em pixels da janela, desenhados com projeção ortográfica (_44 = 1) e sem Z, depois
// de todo o 3D: o que o Remix trata como UI. Quadrados seguidos com a mesma textura, o mesmo ALPHA_1 e o mesmo filtro
// saem num desenho só; o estado é o de psiStartSprites (alfa > 0, sem Z) e o recorte é a área da tela do PS2.
void RemixD3D9Renderer::draw_2d(const DrawItem& item) {
    const size_t n = item.quads2d.size();
    const RECT clip{LONG(item.clip2d[0]), LONG(item.clip2d[1]), LONG(item.clip2d[2] + 0.5f), LONG(item.clip2d[3] + 0.5f)};
    dev_->SetScissorRect(&clip);
    size_t first = 0;
    std::vector<V9> v;
    while (first < n) {
        const Quad2D& a = item.quads2d[first];
        size_t end = first + 1;
        while (end < n && item.quads2d[end].texture == a.texture && item.quads2d[end].alpha == a.alpha &&
               item.quads2d[end].linear == a.linear) {
            ++end;
        }
        v.clear();
        for (size_t i = first; i < end; ++i) {
            const Quad2D& q = item.quads2d[i];
            // O centro do pixel do Direct3D 9 fica em (0, 0): meio pixel para trás.
            const float x0 = q.x0 - 0.5f, x1 = q.x1 - 0.5f, y0 = q.y0 - 0.5f, y1 = q.y1 - 0.5f;
            const float c[4][4] = {{x0, y0, q.u0, q.v0}, {x1, y0, q.u1, q.v0}, {x1, y1, q.u1, q.v1}, {x0, y1, q.u0, q.v1}};
            const int order[6] = {0, 1, 2, 0, 2, 3};
            for (int k : order) {
                V9 o{};
                o.pos[0] = c[k][0];
                o.pos[1] = c[k][1];
                o.normal[2] = -1.0f;
                o.color = gs_colour(q.rgba);
                o.uv[0] = c[k][2];
                o.uv[1] = c[k][3];
                v.push_back(o);
            }
        }
        GsState own;
        own.alpha = a.alpha;
        own.test = 0xd;             // ATE, ATST = GREATER, AREF = 0, sem ZTE
        own.zbuf = 0x131000000ull;  // ZMSK
        own.has = GsState::kAlpha | GsState::kTest | GsState::kZbuf;
        apply_state(item, own, true, false, false);
        dev_->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        dev_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        const DWORD filter = a.linear ? D3DTEXF_LINEAR : D3DTEXF_POINT;
        dev_->SetSamplerState(0, D3DSAMP_MINFILTER, filter);
        dev_->SetSamplerState(0, D3DSAMP_MAGFILTER, filter);
        dev_->SetTexture(0, texture(a.texture));
        dev_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, UINT(v.size() / 3), v.data(), sizeof(V9));
        stats_.quads2d += uint32_t(end - first);
        first = end;
    }
}

void RemixD3D9Renderer::render(const std::vector<DrawItem>& items, const D3D9FrameParams& frame) {
    stats_ = D3D9FrameStats{};
    if (!begin_frame()) return;
    // A passada de névoa do jogo: o que vem antes dela na lista (a lista opaca) leva a névoa; o fundo vazio fica da cor
    // dela (o Z apagado do PS2 dá só névoa).
    int fog_at = -1;
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].fog_pass) {
            fog_at = int(i);
            break;
        }
    }
    D3DCOLOR clear = D3DCOLOR_COLORVALUE(0.125f, 0.145f, 0.19f, 1.0f);
    if (fog_at >= 0 && frame.fog) {
        const DrawItem& f = items[size_t(fog_at)];
        clear = D3DCOLOR_ARGB(255, f.fog_rgb[0], f.fog_rgb[1], f.fog_rgb[2]);
        fog_range(f.fog_far1, &stats_.fog_start, &stats_.fog_end);
        stats_.fog = stats_.fog_start < stats_.fog_end;
        dev_->SetRenderState(D3DRS_FOGCOLOR, clear);
        dev_->SetRenderState(D3DRS_FOGSTART, fbits(stats_.fog_start));
        dev_->SetRenderState(D3DRS_FOGEND, fbits(stats_.fog_end));
    }
    if (FAILED(dev_->BeginScene())) return;
    dev_->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear, 1.0f, 0);
    set_frame_states(frame);
    if (frame.game_lights) set_lights(items);
    const auto is_2d = [](const DrawItem& d) { return d.ui != nullptr || !d.quads2d.empty(); };
    // O 3D, na ordem do Game_Draw (opacos, depois os transparentes de trás para a frente).
    for (size_t i = 0; i < items.size(); ++i) {
        const DrawItem& it = items[i];
        if (is_2d(it) || it.fog_pass) continue;
        if (it.clear_depth) {
            // psiClearZ antes da lista 2 (a arma em primeira pessoa fica por cima do cenário), como no Direct3D 11.
            dev_->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
            ++stats_.depth_clears;
            continue;
        }
        if (it.kind == ObjectKind::Volume && !frame.show_volumes) continue;
        const bool fog = stats_.fog && int(i) < fog_at;
        const bool viewmodel = frame.viewmodel_max_z > 0.0f && it.interp.list == 2;
        if (viewmodel) {
            const D3DVIEWPORT9 vp{0, 0, DWORD(width_), DWORD(height_), 0.0f, frame.viewmodel_max_z};
            dev_->SetViewport(&vp);
            ++stats_.viewmodel;
        }
        if (!it.sprites.empty()) {
            // Quatro vértices por partícula (cantos em sentido horário) viram dois triângulos.
            std::vector<Vertex>& t = scratch_;
            t.clear();
            for (size_t q = 0; q + 3 < it.sprites.size(); q += 4) {
                for (int k : {0, 1, 2, 0, 2, 3}) t.push_back(it.sprites[q + size_t(k)]);
            }
            draw_loose(t, it.sprite_texture, it, fog);
            ++stats_.sprites;
            if (record_) record(seen_particle_, it.sprite_texture);
        } else if (!it.triangles.empty()) {
            draw_loose(it.triangles, it.triangle_texture, it, fog);
            ++stats_.triangles;
            if (record_) record(seen_world_, it.triangle_texture);
        } else if (it.mesh >= 0) {
            draw_mesh(it, fog);
            if (record_ && it.mesh < int(meshes_.size())) {
                const auto kind = PsiOwnerKind(it.interp.owner_kind);
                for (const Part& p : meshes_[size_t(it.mesh)].parts) {
                    record(seen_world_, p.texture);
                    if (kind == PsiOwnerKind::Sky) record(seen_sky_, p.texture);
                    if (kind == PsiOwnerKind::Attach) record(seen_decal_, p.texture);
                }
            }
        } else {
            ++stats_.skipped;
        }
        if (viewmodel) {
            const D3DVIEWPORT9 vp{0, 0, DWORD(width_), DWORD(height_), 0.0f, 1.0f};
            dev_->SetViewport(&vp);
        }
    }
    // O 2D por cima, com projeção ortográfica em pixels da janela (z 0, _44 = 1) e Z desligado.
    static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float w = float(width_), h = float(height_);
    // XMMatrixOrthographicOffCenterLH(0, w, h, 0, 0, 1), vetor-linha.
    const float ortho[16] = {2.0f / w, 0, 0, 0, 0, -2.0f / h, 0, 0, 0, 0, 1, 0, -1, 1, 0, 1};
    dev_->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    dev_->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev_->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
    dev_->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
    dev_->SetTransform(D3DTS_WORLD, &as_d3d(kIdentity));
    dev_->SetTransform(D3DTS_VIEW, &as_d3d(kIdentity));
    dev_->SetTransform(D3DTS_PROJECTION, &as_d3d(ortho));
    dev_->SetFVF(kFvf);
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev_->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev_->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    for (const DrawItem& it : items) {
        if (it.ui != nullptr) {
            ++stats_.hd_items;  // a camada HD é Direct2D: não existe neste caminho
        } else if (!it.quads2d.empty()) {
            draw_2d(it);
            if (record_) {
                for (const Quad2D& q : it.quads2d) record(seen_ui_, q.texture);
            }
        }
    }
    dev_->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev_->EndScene();
}

void RemixD3D9Renderer::present() {
    if (lost_) return;
    const HRESULT hr = dev_->Present(nullptr, nullptr, nullptr, nullptr);
    if (hr == D3DERR_DEVICELOST) lost_ = true;
}

bool RemixD3D9Renderer::save_png(const std::wstring& path) {
    Com<IDirect3DSurface9> back, sys;
    if (FAILED(dev_->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back))) return false;
    D3DSURFACE_DESC d;
    back->GetDesc(&d);
    if (FAILED(dev_->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr)) ||
        FAILED(dev_->GetRenderTargetData(back.Get(), sys.Get()))) {
        return false;
    }
    D3DLOCKED_RECT lr;
    if (FAILED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
    std::vector<uint8_t> rgba(size_t(d.Width) * d.Height * 4);
    for (UINT y = 0; y < d.Height; ++y) {
        const uint8_t* src = static_cast<const uint8_t*>(lr.pBits) + size_t(y) * size_t(lr.Pitch);
        uint8_t* dst = rgba.data() + size_t(y) * d.Width * 4;
        for (UINT x = 0; x < d.Width; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = 255;  // alfa opaco no arquivo, como o save_png do Direct3D 11
        }
    }
    sys->UnlockRect();
    return Renderer::write_png(path, int(d.Width), int(d.Height), rgba.data());
}

bool RemixD3D9Renderer::write_remix_conf(const std::wstring& path) const {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w") != 0 || f == nullptr) return false;
    // A UI que também aparece no mundo (a mesma textura num desenho 3D) fica de fora: marcada como UI, o Remix deixaria
    // de fazer o path tracing dos desenhos 3D com ela.
    const auto list = [&](const char* key, const std::set<uint64_t>& set, const std::set<uint64_t>* minus) {
        std::string line = std::string(key) + " = ";
        bool first = true;
        int n = 0;
        for (uint64_t h : set) {
            if (minus != nullptr && minus->count(h) != 0) continue;
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%s0x%016llX", first ? "" : ", ", static_cast<unsigned long long>(h));
            line += buf;
            first = false;
            ++n;
        }
        std::fprintf(f, "# %d textura(s)\n%s%s\n", n, n > 0 ? "" : "# ", line.c_str());
    };
    std::fprintf(f,
                 "# rtx.conf sugerido pelo port do Nightfire (--renderer d3d9ff --d3d9-remix-conf; docs/remix.md).\n"
                 "# Os hashes são o XXH3 de 64 bits do nível 0 de cada textura como o port a envia (A8R8G8B8): o que o\n"
                 "# dxvk-remix calcula. São candidatos: confira no menu do Remix (Alt+X > Developer Settings > Game Setup)\n"
                 "# que as texturas aparecem marcadas. Gerado a partir dos dados do jogo: não vai para o git.\n"
                 "# Unidades do jogo em metros (sceneScale = 1 cm / unidade), y para cima, mão direita.\n"
                 "rtx.sceneScale = 0.01\n"
                 "rtx.zUp = False\n"
                 "rtx.leftHandedCoordinateSystem = False\n"
                 "rtx.orthographicIsUI = True\n"
                 "# Com --d3d9-viewmodel 0.1 (a arma em primeira pessoa no viewport de profundidade 0 a 0.1):\n"
                 "# rtx.viewModel.enable = True\n"
                 "# rtx.viewModel.maxZThreshold = 0.1\n");
    list("rtx.uiTextures", seen_ui_, &seen_world_);
    list("rtx.skyBoxTextures", seen_sky_, nullptr);
    list("rtx.decalTextures", seen_decal_, nullptr);
    list("rtx.particleTextures", seen_particle_, nullptr);
    std::fclose(f);
    return true;
}

std::string RemixD3D9Renderer::describe() const {
    char line[640];
    std::snprintf(line, sizeof(line),
                  "d3d9ff dispositivo %s blend_max_idx %lu luzes_max %lu aniso %lu | malhas %u (sw %u, falhas %u) "
                  "texturas %u (falhas %u) | quadro rigidas %u pele %u sw %u ambiente %u particulas %u cacos %u "
                  "quads2d %u pulados %u sem_textura %u hd %u luzes %u view_model %u limpa_z %u nevoa %d %.2f-%.2f",
                  software_ ? "software" : mixed_ ? "misto" : "hardware",
                  static_cast<unsigned long>(caps_.MaxVertexBlendMatrixIndex),
                  static_cast<unsigned long>(caps_.MaxActiveLights), static_cast<unsigned long>(caps_.MaxAnisotropy),
                  meshes_count_, software_meshes_, failed_meshes_, textures_count_, failed_textures_, stats_.rigid,
                  stats_.skinned, stats_.swvp, stats_.env_parts, stats_.sprites, stats_.triangles, stats_.quads2d,
                  stats_.skipped, stats_.missing_textures, stats_.hd_items, stats_.lights, stats_.viewmodel,
                  stats_.depth_clears, stats_.fog ? 1 : 0,
                  double(stats_.fog_start), double(stats_.fog_end));
    return std::string(line) + " | modulo " + module_ + " | adaptador " + adapter_note_;
}

}  // namespace nf
