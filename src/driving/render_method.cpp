// O recenseamento dos render methods (render_method.h).

#include "driving/render_method.h"

#include <algorithm>
#include <cstring>
#include <set>

namespace drv {

using eagl::ptr_at;
using eagl::rd32;

namespace {

// As instruções do microprograma pela cadeia de DMA que o envia (CNT com o MPG, REF com o código).
std::vector<uint64_t> read_microcode(uint32_t mc) {
    std::vector<uint64_t> out;
    const int count = int(rd32(mc));
    uint32_t t = rd32(mc + 0x10);
    for (int guard = 0; guard < 64 && t && int(out.size()) < count; ++guard) {
        const uint32_t tag = rd32(t);
        const uint32_t qwc = tag & 0xFFFF;
        const int id = int(tag >> 28) & 7;
        const uint32_t addr = rd32(t + 4);
        if (id == 1) {  // CNT: o MPG vai no qword seguinte
            t += 16 + qwc * 16;
        } else if (id == 3) {  // REF: o código
            const uint64_t* p = ptr_at<const uint64_t>(addr);
            for (uint32_t i = 0; i < qwc * 2 && int(out.size()) < count; ++i) out.push_back(p[i]);
            t += 16;
        } else {
            break;
        }
    }
    return out;
}

}  // namespace

std::vector<RenderMethodInfo> render_methods_census(const eagl::DynamicLoader& obj, const std::string& object_name) {
    std::vector<RenderMethodInfo> out;
    const int n = obj.GetCount();
    for (int i = 0; i < n; ++i) {
        const eagl::SymbolInfo s = obj.GetSymbol(i);
        if (!s.defined || !s.name || !*s.name || s.type[0] || std::strstr(s.name, "__EAGLMicroCode") ||
            std::strncmp(s.name, "__", 2) == 0 || std::strcmp(s.name, ".data") == 0)
            continue;
        uint32_t mc = 0;
        const std::string mc_name = std::string(s.name) + "0__EAGLMicroCode";
        mc = obj.Find(mc_name.c_str());
        if (!mc) continue;
        RenderMethodInfo m;
        m.name = s.name;
        m.object = object_name;
        m.method = s.addr;
        m.microcode = mc;
        m.instructions = int(rd32(mc));
        m.max_verts = int(rd32(s.addr + 12));
        for (uint32_t p = rd32(s.addr + 16); p && rd32(p); p += 8) {
            m.params.push_back(ptr_at<const char>(rd32(p)));
            if (m.params.back() == "PalletMatrices") m.skin = true;
        }
        m.code = read_microcode(mc);
        uint64_t h = 1469598103934665603ull;
        std::set<int> abs;
        for (uint64_t ins : m.code) {
            for (int b = 0; b < 8; ++b) {
                h ^= (ins >> (b * 8)) & 0xFF;
                h *= 1099511628211ull;
            }
            const uint32_t lo = uint32_t(ins), up = uint32_t(ins >> 32);
            if ((up & 0x3C) == 0x3C && ((((up >> 6) & 0x1F) << 2) | (up & 3)) == 0x1F) m.clip = true;
            if (up >> 31) continue;  // bit I: a inferior é um imediato (LOI)
            const uint32_t op = (lo >> 25) & 0x7F;
            const int it = int(lo >> 16) & 31, is = int(lo >> 11) & 31;
            if (op == 0x40 && (lo & 0x3C) == 0x3C) {
                const int sp = int(((lo >> 6) & 0x1F) << 2 | (lo & 3));
                if (sp == 0x6C) ++m.xgkicks;
                if (sp >= 0x70 && sp <= 0x7E && sp != 0x7B) m.efu = true;
            } else if ((op == 0x00 || op == 0x04) && is == 0) {  // LQ / ILW em vi00
                int imm = int(lo & 0x7FF);
                if (imm & 0x400) imm -= 0x800;
                abs.insert(imm);
            } else if (op == 0x08 && is == 0 && it != 0) {  // IADDIU viN, vi00, X: um ponteiro absoluto
                const int imm = int(((lo >> 10) & 0x7800) | (lo & 0x7FF));
                if (imm >= 0x30 && imm < 0x400) abs.insert(imm);
            }
        }
        m.code_hash = h;
        m.abs_reads.assign(abs.begin(), abs.end());
        m.family = render_method_family(m);
        out.push_back(std::move(m));
    }
    std::sort(out.begin(), out.end(), [](const RenderMethodInfo& a, const RenderMethodInfo& b) { return a.name < b.name; });
    return out;
}

std::string render_method_family(const RenderMethodInfo& m) {
    const std::string& n = m.name;
    auto has = [&](const char* s) { return n.find(s) != std::string::npos; };
    auto param = [&](const char* s) { return std::find(m.params.begin(), m.params.end(), s) != m.params.end(); };
    if (m.max_verts == 0 || n == "MatCarPerCar" || n == "MatBondPerViewPort" || has("GlobalsUpload")) return "constantes";
    if (has("MatCar") && !has("Shadow")) return "carro";
    if (has("Shadow")) return "sombra";
    if (m.skin) return has("Envmap") || has("Reflect") || has("Specmap") || has("ToonShade") || has("Irrad")
                           ? "pele+ambiente"
                           : "pele";
    if (has("Sprite") || has("Part")) return "particulas";
    if (has("ScreenSpace") || has("Hud") || has("Line") || has("Debug")) return "2D/linhas/depuracao";
    if (has("Water") || has("UnderWater") || has("Kelp")) return "agua";
    if (has("Envmap") || has("Reflect") || has("Specmap") || has("Irrad") || has("ToonShade")) return "ambiente";
    if (param("LightSources") || has("Lit")) return "iluminado";
    if (has("Scroll") || has("XFade") || has("FrameAnim")) return "animado";
    if (param("STQ") || has("Texture")) return "texturizado";
    return "gouraud";
}

}  // namespace drv
