// O protótipo do render das missões de carro (docs/driving.md seção 10): BIGF/RefPack, SHPS, CARP, o carregador do
// EAGL, o GeoPrimState/TAR, o modelo e o GeoPrim, o VIF e a referência do microprograma MatCar, com dados montados à mão.

#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "driving/bigf.h"
#include "driving/car_model.h"
#include "driving/carp.h"
#include "driving/eagl_loader.h"
#include "driving/eagl_model.h"
#include "driving/eagl_state.h"
#include "driving/mat_car.h"
#include "driving/mat_car_verify.h"
#include "driving/shps.h"
#include "driving/vif.h"
#include "test_framework.h"

using namespace drv;

namespace {

void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    if (b.size() < at + 4) b.resize(at + 4);
    std::memcpy(&b[at], &v, 4);
}
void put16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
    if (b.size() < at + 2) b.resize(at + 2);
    std::memcpy(&b[at], &v, 2);
}
float fbits(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
uint32_t ubits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// Um bloco alinhado a 16 que não se move.
struct Aligned {
    std::vector<uint8_t> store;
    uint8_t* p = nullptr;
    explicit Aligned(size_t n) : store(n + 16, 0) {
        p = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(store.data()) + 15) & ~uintptr_t(15));
    }
};

}  // namespace

// ---------------------------------------------------------------- BIGF e RefPack

TEST(driving_refpack_literals_and_copies) {
    // "ABCD" e uma cópia de 3 bytes a 4 para trás: ABCDABC.
    const uint8_t a[] = {0x10, 0xFB, 0x00, 0x00, 0x07, 0xE0, 'A', 'B', 'C', 'D', 0x00, 0x03, 0xFC};
    std::vector<uint8_t> out;
    CHECK(refpack_is(a, sizeof a));
    CHECK(refpack_decode(a, sizeof a, out));
    CHECK(std::string(out.begin(), out.end()) == "ABCDABC");
    // Cópia sobreposta: "x" e 3 cópias a 1 byte: xxxx.
    const uint8_t b[] = {0x10, 0xFB, 0x00, 0x00, 0x04, 0x01, 0x00, 'x', 0xFC};
    CHECK(refpack_decode(b, sizeof b, out) && std::string(out.begin(), out.end()) == "xxxx");
    // Forma de 3 bytes (0x80-0xBF): 4 literais e 5 bytes a 4 para trás: "abcd" + "abcda".
    const uint8_t c[] = {0x10, 0xFB, 0x00, 0x00, 0x09, 0xE0, 'a', 'b', 'c', 'd', 0x81, 0x00, 0x03, 0xFC};
    CHECK(refpack_decode(c, sizeof c, out) && std::string(out.begin(), out.end()) == "abcdabcda");
    // Tamanho errado no cabeçalho: falha.
    const uint8_t d[] = {0x10, 0xFB, 0x00, 0x00, 0x09, 0xE0, 'A', 'B', 'C', 'D', 0xFC};
    CHECK(!refpack_decode(d, sizeof d, out));
}

TEST(driving_bigf_directory) {
    std::vector<uint8_t> h = {'B', 'I', 'G', 'F', 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0};
    auto be = [&](uint32_t v) {
        for (int i = 3; i >= 0; --i) h.push_back(uint8_t(v >> (i * 8)));
    };
    be(0x800);
    be(5);
    for (char ch : std::string("a\\one.txt")) h.push_back(uint8_t(ch));
    h.push_back(0);
    be(0x1000);
    be(77);
    for (char ch : std::string("data\\car\\model\\vanquish.crp")) h.push_back(uint8_t(ch));
    h.push_back(0);
    std::vector<BigfEntry> e;
    CHECK(Bigf::parse_directory(h.data(), h.size(), e));
    CHECK(e.size() == 2);
    CHECK(e[1].offset == 0x1000 && e[1].size == 77 && e[1].name == "data\\car\\model\\vanquish.crp");
    CHECK(bigf_base_name(e[1].name) == "vanquish");
    h[3] = 'X';
    CHECK(!Bigf::parse_directory(h.data(), h.size(), e));
}

// ---------------------------------------------------------------- SHPS

TEST(driving_shps_8bit_csm1_4bit_rgba) {
    // Três imagens: 8 bits 2x1 com paleta CSM1 de 256, 4 bits 3x1 com paleta de 16, RGBA 1x1.
    std::vector<uint8_t> f(16 + 3 * 8, 0);
    std::memcpy(f.data(), "SHPS", 4);
    put32(f, 8, 3);
    std::memcpy(&f[12], "G354", 4);
    auto block = [&](uint8_t type, uint32_t next, uint16_t w, uint16_t h, uint16_t xflags) {
        const size_t at = f.size();
        f.resize(at + 16, 0);
        f[at] = type;
        f[at + 1] = uint8_t(next);
        f[at + 2] = uint8_t(next >> 8);
        f[at + 3] = uint8_t(next >> 16);
        put16(f, at + 4, w);
        put16(f, at + 6, h);
        put16(f, at + 12, xflags);
        return at;
    };
    // Imagem 0: "img8" 8 bits, pixels 8 e 16; paleta CSM1 (bandeira 0x2000): o índice 8 mora na entrada 16 e vice-versa.
    std::memcpy(&f[16], "img8", 4);
    put32(f, 20, uint32_t(f.size()));
    block(0x02, 16 + 4, 2, 1, 0);
    f.push_back(8);
    f.push_back(16);
    f.resize(f.size() + 2);  // alinha
    const size_t pal = block(0x21, 0, 256, 1, 0x2000);
    f.resize(pal + 16 + 256 * 4, 0);
    f[pal + 16 + 16 * 4] = 0xAA;  // entrada 16 (o índice 8 em CSM1): vermelho 0xAA
    f[pal + 16 + 16 * 4 + 3] = 0x80;
    f[pal + 16 + 8 * 4 + 1] = 0xBB;  // entrada 8 (o índice 16): verde 0xBB
    // Imagem 1: "img4" 4 bits, pixels 1, 2, 3 (o par no nibble baixo).
    std::memcpy(&f[24], "img4", 4);
    put32(f, 28, uint32_t(f.size()));
    block(0x01, 16 + 4, 3, 1, 0);
    f.push_back(0x21);
    f.push_back(0x03);
    f.resize(f.size() + 2);
    const size_t pal4 = block(0x21, 0, 16, 1, 0);
    f.resize(pal4 + 16 + 16 * 4, 0);
    for (int k = 0; k < 16; ++k) f[pal4 + 16 + k * 4 + 2] = uint8_t(k * 10);  // azul = 10 x índice
    // Imagem 2: "rgba" 1x1.
    std::memcpy(&f[32], "rgba", 4);
    put32(f, 36, uint32_t(f.size()));
    block(0x05, 0, 1, 1, 0);
    f.push_back(1);
    f.push_back(2);
    f.push_back(3);
    f.push_back(0x40);
    put32(f, 4, uint32_t(f.size()));

    ShpsFile s;
    CHECK(shps_parse(f.data(), f.size(), s));
    CHECK(s.images.size() == 3 && s.version == "G354");
    const ShpsImage* a = s.find("img8");
    CHECK(a && a->width == 2 && a->csm1 && !a->swizzled && a->gs_psm == 0x13);
    CHECK(a->rgba[0] == 0xAA && a->rgba[3] == 0x80);  // o índice 8 leu a entrada 16
    CHECK(a->rgba[4 + 1] == 0xBB);                     // o índice 16 leu a entrada 8
    const ShpsImage* b = s.find("img4");
    CHECK(b && b->rgba[2] == 10 && b->rgba[4 + 2] == 20 && b->rgba[8 + 2] == 30);
    const ShpsImage* c = s.find("rgba");
    CHECK(c && c->rgba[0] == 1 && c->rgba[3] == 0x40 && c->gs_psm == 0);
}

TEST(driving_shps_psmt8_swizzle_is_a_block_permutation) {
    // Propriedades do swizzle do GS (PSMT8 gravado como PSMCT32): é uma permutação, e cada faixa de 16 linhas fica nos
    // seus 16 x largura bytes (o swizzle só embaralha dentro da faixa).
    const int w = 64, h = 32;
    std::set<int> seen;
    bool blocks_ok = true;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int i = shps_psmt8_swizzled_index(x, y, w);
            seen.insert(i);
            if (i / (16 * w) != y / 16) blocks_ok = false;
        }
    CHECK(int(seen.size()) == w * h && *seen.begin() == 0 && *seen.rbegin() == w * h - 1);
    CHECK(blocks_ok);
    CHECK(shps_csm1_index(8) == 16 && shps_csm1_index(16) == 8 && shps_csm1_index(0x2f) == 0x37 && shps_csm1_index(7) == 7);
}

// ---------------------------------------------------------------- CARP

TEST(driving_carp_entries) {
    // Um UGroup: a raiz ('CARP', bit 1, um subgrupo pelas bandeiras >> 5 e um dado) e o grupo 'Arti' com dois dados.
    std::vector<uint8_t> f(128, 0);
    std::memcpy(f.data(), "PRAC", 4);
    put32(f, 4, 1u << 5 | 0x12);  // um subgrupo, posição relativa (bit 1), grupos ordenados (0x10)
    put32(f, 8, 1);
    put32(f, 12, 1);  // o vetor a 16 bytes da raiz
    put32(f, 16, carp_tag("Arti"));
    put32(f, 20, 0x02);  // nenhum subgrupo, relativo
    put32(f, 24, 2);
    put32(f, 28, 2);  // o vetor a 32 bytes do 'Arti': 48
    // O dado da raiz vem depois dos cabeçalhos de subgrupo: 'ELFd' relativo (bit 1) com 0x40 bytes, a 0x30 da entrada.
    put32(f, 32, carp_tag("ELFd"));
    put32(f, 36, 0x40u << 8 | 0x1a);
    put32(f, 44, 0x30);
    // No 'Arti': 'sn' família curta (bit 0), absoluta; 'sr#0001' curta e relativa, um texto.
    put32(f, 48, carp_short_tag("sn", 5));
    put32(f, 52, 0x10u << 8 | 0x09);
    put32(f, 60, 0x99);
    put32(f, 64, carp_short_tag("sr", 1));
    put32(f, 68, 8u << 8 | 0x03);
    put32(f, 76, 0x20);
    std::memcpy(f.data() + 96, "abc", 4);
    Carp c;
    CHECK(carp_parse(f.data(), f.size(), c));
    CHECK(c.entries.size() == 3 && c.root.groups.size() == 1 && c.root.data.size() == 1);
    const CarpEntry* e = c.find(carp_tag("ELFd"));
    CHECK(e && e->data == 32 + 0x30 && e->size() == 0x40 && e->tag_name() == "ELFd");
    const CarpGroup* arti = c.group(carp_tag("Arti"));
    CHECK(arti && arti->array == 48 && arti->data.size() == 2 && !c.find(carp_short_tag("sn", 5)));
    const CarpEntry* sn = arti ? arti->find(carp_short_tag("sn", 5)) : nullptr;
    CHECK(sn && sn->short_tag() && sn->data == 0x99 && sn->tag_name() == "sn#0005");
    const CarpEntry* sr = arti ? arti->find(carp_short_tag("sr", 1)) : nullptr;
    CHECK(sr && carp_string(f.data(), f.size(), *sr) == "abc");
}

// As instâncias de um objeto articulado ('in': 64 bytes cada, a peça 'as' no w da linha 1, o nome do modelo pela 'sr'
// da palavra 9 do 'as') e as listas de desenho por vista ('vd').
TEST(driving_carp_instances_and_view_lists) {
    CarpGroup g;
    std::vector<uint8_t> f(512, 0);
    auto entry = [&](uint32_t tag, uint32_t size, uint32_t count, size_t data) {
        CarpEntry e;
        e.tag = tag;
        e.flags = size << 8 | 1;
        e.count = count;
        e.data = data;
        g.data.push_back(e);
    };
    // Duas instâncias: a primeira da peça as#0001 transladada (1, 2, 3) e espelhada em x; a segunda de uma peça sem
    // modelo (as#0002, 32 bytes).
    const float m0[16] = {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 2, 3, 0};
    for (int k = 0; k < 16; ++k) put32(f, size_t(k) * 4, ubits(m0[k]));
    put32(f, 12, 0x0000ff80);                       // bandeiras: espelhada
    put32(f, 28, carp_short_tag("as", 1));          // a peça
    put32(f, 60, 0x00a0280a);
    for (int k = 0; k < 16; ++k) put32(f, 64 + size_t(k) * 4, ubits(k % 5 == 0 ? 1.0f : 0.0f));
    put32(f, 64 + 28, carp_short_tag("as", 2));
    entry(carp_short_tag("in", 0), 128, 2, 0);
    put32(f, 200 + 36, carp_short_tag("sr", 7));    // o 'as' de 40 bytes aponta o nome
    entry(carp_short_tag("as", 1), 40, 0, 200);
    entry(carp_short_tag("as", 2), 32, 0, 256);
    std::memcpy(f.data() + 300, "EAGL::car::{as  0001}::Model[0].00", 35);
    entry(carp_short_tag("sr", 7), 40, 0, 300);
    put16(f, 400, 1);
    put16(f, 402, 0);
    entry(carp_short_tag("vd", 1), 4, 2, 400);
    const std::vector<CarpInstance> in = carp_instances(f.data(), f.size(), g);
    CHECK(in.size() == 2);
    CHECK(in.size() == 2 && in[0].matrix[0] == -1.0f && in[0].matrix[12] == 1.0f && in[0].matrix[14] == 3.0f &&
          in[0].matrix[3] == 0.0f && in[0].matrix[15] == 1.0f && in[0].flags == 0xff80 &&
          in[0].piece == carp_short_tag("as", 1) && in[0].extra[1] == 0x00a0280a &&
          in[0].model == "car::{as  0001}::Model[0].00");
    CHECK(in.size() == 2 && in[1].model.empty() && in[1].matrix[5] == 1.0f);
    const auto views = carp_view_lists(f.data(), f.size(), g);
    CHECK(views.size() == 2 && views[0].empty() && views[1].size() == 2 && views[1][0] == 1 && views[1][1] == 0);
}

// ---------------------------------------------------------------- o carregador do EAGL

namespace {

// Um ELF32 MIPS relocável: .data, .shstrtab, .strtab, .symtab, .rel.data. `data_words` vão na .data; `syms` são
// {nome, valor, seção (1 = .data, 0 = indefinido)}; `rels` {posição na .data, símbolo, tipo}.
struct Sym {
    std::string name;
    uint32_t value;
    uint16_t shndx;
};
struct Rel {
    uint32_t off, sym, type;
};
std::vector<uint8_t> make_elf(const std::vector<uint32_t>& data_words, const std::vector<Sym>& syms,
                              const std::vector<Rel>& rels) {
    std::vector<uint8_t> e(0x40, 0);
    e[0] = 0x7f;
    e[1] = 'E';
    e[2] = 'L';
    e[3] = 'F';
    put16(e, 16, 1);  // ET_REL
    put16(e, 18, 8);  // MIPS
    const size_t data_off = 0x40;
    for (uint32_t w : data_words) put32(e, e.size(), w);
    const std::string shstr = std::string("\0.data\0.shstrtab\0.strtab\0.symtab\0.rel.data\0", 43);
    const size_t shstr_off = e.size();
    e.insert(e.end(), shstr.begin(), shstr.end());
    std::string str(1, '\0');
    std::vector<uint32_t> name_off;
    for (const Sym& s : syms) {
        name_off.push_back(uint32_t(str.size()));
        str += s.name;
        str.push_back('\0');
    }
    const size_t str_off = e.size();
    e.insert(e.end(), str.begin(), str.end());
    while (e.size() & 3) e.push_back(0);
    const size_t sym_off = e.size();
    e.resize(e.size() + 16, 0);  // o símbolo nulo
    for (size_t i = 0; i < syms.size(); ++i) {
        const size_t at = e.size();
        e.resize(at + 16, 0);
        put32(e, at, name_off[i]);
        put32(e, at + 4, syms[i].value);
        e[at + 12] = syms[i].shndx ? 0x11 : 0x10;
        put16(e, at + 14, syms[i].shndx);
    }
    const size_t rel_off = e.size();
    for (const Rel& r : rels) {
        put32(e, e.size(), r.off);
        put32(e, e.size(), (r.sym + 1) << 8 | r.type);
    }
    while (e.size() & 3) e.push_back(0);
    const size_t sh_off = e.size();
    auto sh = [&](uint32_t name, uint32_t type, size_t off, size_t size, uint32_t link, uint32_t info) {
        const size_t at = e.size();
        e.resize(at + 40, 0);
        put32(e, at, name);
        put32(e, at + 4, type);
        put32(e, at + 16, uint32_t(off));
        put32(e, at + 20, uint32_t(size));
        put32(e, at + 24, link);
        put32(e, at + 28, info);
    };
    sh(0, 0, 0, 0, 0, 0);
    sh(1, 1, data_off, data_words.size() * 4, 0, 0);       // 1 .data
    sh(7, 3, shstr_off, shstr.size(), 0, 0);               // 2 .shstrtab
    sh(17, 3, str_off, str.size(), 0, 0);                  // 3 .strtab
    sh(25, 2, sym_off, (syms.size() + 1) * 16, 3, 1);      // 4 .symtab
    sh(33, 9, rel_off, rels.size() * 8, 4, 1);             // 5 .rel.data
    put32(e, 0x20, uint32_t(sh_off));
    put16(e, 0x2e, 40);
    put16(e, 0x30, 6);
    put16(e, 0x32, 2);
    return e;
}

}  // namespace

TEST(driving_eagl_loader_symbols_relocations_and_types) {
    eagl::reset_registries();
    eagl::diagnostics().clear();
    // Objeto A: define "__Model:::carro" (na .data+8) e "Partilhado" (+12); a palavra 0 aponta o próprio modelo
    // (relocação à seção) com adendo 4, a 1 importa "GAME::Valor" (pela função de resolução), a 2 um HI16 (sem o vai-um
    // do LO16) e a 3 um LO16 de "Partilhado".
    std::vector<uint32_t> a_words = {4, 100, 0xAAAA0001, 0xBBBB0002, 0, 0};
    auto ea = make_elf(a_words, {{"__Model:::carro", 8, 1}, {"GAME::Valor", 0, 0}, {"Partilhado", 12, 1}},
                       {{0, 0, 2}, {4, 1, 2}, {8, 2, 5}, {12, 2, 6}});
    Aligned ba(ea.size());
    std::memcpy(ba.p, ea.data(), ea.size());
    int ctor_calls = 0;
    uint32_t ctor_addr = 0;
    eagl::register_type(
        "Model",
        [&](uint32_t addr, eagl::DynamicLoader&) {
            ++ctor_calls;
            ctor_addr = addr;
        },
        nullptr);
    static uint32_t valor[4] = {7, 7, 7, 7};
    eagl::DynamicLoader la(ba.p, uint32_t(ea.size()), [](const char* n, bool& found) -> uint32_t {
        found = std::strcmp(n, "GAME::Valor") == 0;
        return found ? eagl::addr_of(valor) : 0;
    });
    CHECK(la.ok());
    CHECK(eagl::diagnostics().empty());
    const uint32_t data = eagl::addr_of(la.data());
    const uint32_t* w = reinterpret_cast<const uint32_t*>(la.data());
    CHECK(w[0] == data + 8 + 4);                  // R_MIPS_32: S + A
    CHECK(w[1] == eagl::addr_of(valor) + 100);    // o indefinido pela função de resolução
    const uint32_t S = data + 12;
    CHECK((w[2] & 0xFFFF) == ((0x0001 + (S >> 16)) & 0xFFFF) && (w[2] >> 16) == 0xAAAA);  // HI16
    CHECK((w[3] & 0xFFFF) == ((0x0002 + S) & 0xFFFF) && (w[3] >> 16) == 0xBBBB);           // LO16
    // O nome "__Tipo:::nome" virou "nome" com o tipo depois do zero; o construtor do tipo rodou com o endereço.
    bool found_model = false;
    for (int i = 0; i < la.GetCount(); ++i) {
        const eagl::SymbolInfo s = la.GetSymbol(i);
        if (s.name && std::strcmp(s.name, "carro") == 0) {
            found_model = std::strcmp(s.type, "Model") == 0 && s.defined && s.addr == data + 8;
        }
    }
    CHECK(found_model);
    CHECK(ctor_calls == 1 && ctor_addr == data + 8);
    uint32_t addr = 0;
    CHECK(la.GetAddr("Model", "carro", addr) && addr == data + 8);
    CHECK(!la.GetAddr("EAGL::TAR", "carro", addr));
    // Objeto B: importa "Partilhado" do A (a lista global dos carregados) e "EAGL::ViewPort::gpModelMatrix" (RegisterVar).
    static float matrix[16];
    eagl::register_var("EAGL::ViewPort::gpModelMatrix", eagl::addr_of(matrix));
    auto eb = make_elf({0, 8}, {{"Partilhado", 0, 0}, {"EAGL::ViewPort::gpModelMatrix", 0, 0}}, {{0, 0, 2}, {4, 1, 2}});
    Aligned bb(eb.size());
    std::memcpy(bb.p, eb.data(), eb.size());
    eagl::DynamicLoader lb(bb.p, uint32_t(eb.size()), nullptr);
    const uint32_t* wb = reinterpret_cast<const uint32_t*>(lb.data());
    CHECK(wb[0] == data + 12);
    CHECK(wb[1] == eagl::addr_of(matrix) + 8);
    // Um indefinido sem dono: a mensagem do jogo e o valor do símbolo (0).
    auto ec = make_elf({5}, {{"Ninguem", 0, 0}}, {{0, 0, 2}});
    Aligned bc(ec.size());
    std::memcpy(bc.p, ec.data(), ec.size());
    eagl::DynamicLoader lc(bc.p, uint32_t(ec.size()), nullptr);
    CHECK(reinterpret_cast<const uint32_t*>(lc.data())[0] == 5);
    CHECK(!eagl::diagnostics().empty() && eagl::diagnostics().back().find("Ninguem") != std::string::npos);
    eagl::reset_registries();
}

TEST(driving_eagl_loader_split_elf) {
    // O mesmo ELF partido como o CARP o guarda: a primeira parte até o fim da .data, a segunda (o 'ELFr') com o resto;
    // as posições acima da fronteira são lidas na segunda parte.
    eagl::reset_registries();
    auto e = make_elf({1, 2, 3}, {{"x", 4, 1}}, {{0, 0, 2}});
    const size_t cut = 0x40 + 12;  // o fim da .data
    Aligned first(cut), second(e.size() - cut);
    std::memcpy(first.p, e.data(), cut);
    std::memcpy(second.p, e.data() + cut, e.size() - cut);
    eagl::DynamicLoader l(first.p, uint32_t(cut), second.p, true, nullptr);
    CHECK(l.ok());
    l.Resolve();
    CHECK(reinterpret_cast<const uint32_t*>(l.data())[0] == eagl::addr_of(l.data()) + 4 + 1);
    uint32_t a = 0;
    CHECK(l.GetAddr("", "x", a) && a == eagl::addr_of(l.data()) + 4);
    eagl::reset_registries();
}

// ---------------------------------------------------------------- GeoPrimState e TAR

TEST(driving_geoprimstate_default_and_specs) {
    eagl::RStateManager_Reset();
    alignas(16) uint8_t s[0x80];
    eagl::GeoPrimState_Init(s);
    auto q = [](const uint8_t* p, int off) { return *reinterpret_cast<const uint64_t*>(p + off); };
    CHECK(q(s, 0x00) == 0x1000000000000001ull && q(s, 0x20) == 0x5000000000008001ull);
    CHECK(q(s, 0x38) == 0x47 && q(s, 0x48) == 0x42 && q(s, 0x58) == 0x4C && q(s, 0x68) == 0x4E && q(s, 0x78) == 0x4A);
    CHECK(q(s, 0x10) == 8);                                       // Gouraud, pontos
    CHECK(((q(s, 0x30) >> 16) & 1) == 1 && ((q(s, 0x30) >> 17) & 3) == 2);  // Z ligado, GEQUAL
    CHECK(q(s, 0x40) == 0x80000000A8ull);
    // O padrão do RStateManager: triângulos, textura, névoa, mistura (Cs - Cd) As + Cd, teste de alfa GREATER 0xa0
    // com AFAIL "só RGB".
    const uint8_t* d = eagl::RStateManager_GetGeoPrimState("");
    CHECK((q(d, 0x10) & 0x7F) == (3 | 0x08 | 0x10 | 0x20 | 0x40));
    CHECK((q(d, 0x30) & 0x3FFF) == 0x3A0D && q(d, 0x40) == 0x8000000044ull);
    // primtype=tristrip -> PRIM 4; o mesmo texto devolve o mesmo estado.
    const uint8_t* t = eagl::RStateManager_GetGeoPrimState("primtype=tristrip");
    CHECK((q(t, 0x10) & 7) == 4);
    CHECK(eagl::RStateManager_GetGeoPrimState("primtype=tristrip") == t);
    // name=envmap: Cs As + Cd, sem névoa, com o teste de alfa.
    const uint8_t* env = eagl::RStateManager_GetGeoPrimState("name=envmap;primtype=tristrip");
    CHECK(q(env, 0x40) == 0x8000000048ull && (q(env, 0x10) & 0x78) == 0x58 && (q(env, 0x30) & 1) == 1);
    // modify=specmap volta a valer no fim (a primeira passada devolve 1): mistura e teste desligados mesmo com
    // "alphatest=on" depois.
    const uint8_t* sp = eagl::RStateManager_GetGeoPrimState("modify=specmap;alphatest=on;transparency=alpha");
    CHECK((q(sp, 0x10) & 0x40) == 0 && (q(sp, 0x30) & 1) == 0);
    // texalpha só vale com o teste ligado: noA desliga mistura e teste; deepA põe AREF = 1 e AFAIL "só RGB".
    const uint8_t* noa = eagl::RStateManager_GetGeoPrimState("alphatest=on;texalpha=noA");
    CHECK((q(noa, 0x10) & 0x40) == 0 && (q(noa, 0x30) & 1) == 0);
    const uint8_t* deep = eagl::RStateManager_GetGeoPrimState("alphatest=on;texalpha=deepA");
    CHECK(((q(deep, 0x30) >> 4) & 0xFF) == 1 && ((q(deep, 0x30) >> 12) & 3) == 3 && (q(deep, 0x10) & 0x40) == 0x40);
    const uint8_t* off = eagl::RStateManager_GetGeoPrimState("alphatest=off;texalpha=deepA");
    CHECK((q(off, 0x30) & 1) == 0 && ((q(off, 0x30) >> 4) & 0xFF) == 0xA0);  // sem o teste, o texalpha não muda nada
    // antialias: qualquer valor da tabela, até "off", liga o AA1 (o tratador só olha se achou o valor).
    const uint8_t* aa = eagl::RStateManager_GetGeoPrimState("antialias=off");
    CHECK((q(aa, 0x10) & 0x80) == 0x80);
    // depthtest=lequal não existe no GS (o Z fica GEQUAL); greater vira GREATER.
    const uint8_t* le = eagl::RStateManager_GetGeoPrimState("depthtest=lequal");
    CHECK(((q(le, 0x30) >> 17) & 3) == 2);
    const uint8_t* gt = eagl::RStateManager_GetGeoPrimState("depthtest=greater");
    CHECK(((q(gt, 0x30) >> 17) & 3) == 3);
    eagl::RStateManager_Reset();
}

TEST(driving_tar_clamp_tfx_and_packet) {
    eagl::tar_images_reset();
    eagl::TarImage* im = eagl::tar_image_create(42, 128, 64, 0x13, true);
    eagl::set_shape_lookup([](const char n[4], void* u) -> eagl::TarImage* {
        return std::memcmp(n, "abcd", 4) == 0 ? static_cast<eagl::TarImage*>(u) : nullptr;
    }, im);
    // TAR: faixa [0,1], U repete, V prende, modo 1 (DECAL).
    alignas(16) uint8_t tar[0x60] = {};
    std::memcpy(tar + 4, "abcd", 4);
    const float one = 1.0f;
    std::memcpy(tar + 0x0c, &one, 4);
    std::memcpy(tar + 0x14, &one, 4);
    *reinterpret_cast<int32_t*>(tar + 0x18) = 1;
    *reinterpret_cast<int32_t*>(tar + 0x1c) = 1;
    *reinterpret_cast<int32_t*>(tar + 0x20) = 0;
    eagl::TAR_Construct(eagl::addr_of(tar));
    CHECK(*reinterpret_cast<uint32_t*>(tar) == eagl::addr_of(im));
    const uint32_t flags = *reinterpret_cast<uint32_t*>(tar + 0x10);
    CHECK((flags & 3) == 0 && ((flags >> 2) & 3) == 1);  // WMS REPEAT, WMT CLAMP
    CHECK(((flags >> 25) & 3) == 1);                     // TFX DECAL
    uint64_t pk[14];
    CHECK(eagl::TAR_WritePacket(eagl::addr_of(tar) + 4, pk) == pk + 14);
    eagl::GsRegs r;
    eagl::gs_read_packet(reinterpret_cast<const uint8_t*>(pk), 7, r);
    CHECK(r.has_tex0 && (r.tex0 & 0x3FFF) == 42 && ((r.tex0 >> 20) & 0x3F) == 0x13);
    CHECK(((r.tex0 >> 26) & 0xF) == 7 && ((r.tex0 >> 30) & 0xF) == 6);  // TW = log2(128), TH = log2(64)
    CHECK(((r.tex0 >> 35) & 3) == 1);
    CHECK(r.has_clamp && (r.clamp & 0xF) == (0 | 1 << 2));
    eagl::set_shape_lookup(nullptr, nullptr);
    eagl::tar_images_reset();
}

// ---------------------------------------------------------------- o VIF

TEST(driving_vif_unpack_formats_mask_and_double_buffer) {
    Vif1 v;
    std::vector<uint32_t> w;
    // BASE 0x10, OFFSET 0x20: TOPS = 0x10.
    w.push_back(0x03000010);
    w.push_back(0x02000020);
    w.push_back(0x01000404);  // STCYCL 4/4
    // STMASK: w da linha; STROW (0,0,0,1.0); STMOD 0; V3-32 com máscara, 2 vetores em TOPS+1.
    w.push_back(0x20000000);
    w.push_back(0x40404040);
    w.push_back(0x30000000);
    w.push_back(0);
    w.push_back(0);
    w.push_back(0);
    w.push_back(ubits(1.0f));
    w.push_back(0x7802c001);
    for (float f : {1.f, 2.f, 3.f, 4.f, 5.f, 6.f}) w.push_back(ubits(f));
    // V2-16 com sinal em TOPS+3 (sem USN): -1 e 2; V4-8 USN em TOPS+4: 200, 1, 2, 3.
    w.push_back(0x65018003);
    w.push_back(0x0002FFFF);
    w.push_back(0x6e01c004);
    w.push_back(0x030201C8);
    // STMOD 1 (offset) com a linha (0,0,0,1): V4-32 em TOPS+5 = dado + linha.
    w.push_back(0x05000001);
    w.push_back(0x6c018005);
    for (uint32_t x : {10u, 20u, 30u, 40u}) w.push_back(x);
    w.push_back(0x05000000);
    w.push_back(0x17000000);  // MSCNT: TOP = 0x10, TOPS passa a 0x30
    w.push_back(0x6c01c000);  // V4-32 em TOPS+0 (0x30)
    for (uint32_t x : {7u, 7u, 7u, 7u}) w.push_back(x);
    w.push_back(0x17000000);  // TOP = 0x30, TOPS volta a 0x10
    std::vector<uint32_t> tops;
    v.on_kick = [&](Vif1::Kick k, uint32_t, uint32_t top, uint32_t) {
        if (k == Vif1::kMscnt) tops.push_back(top);
    };
    v.process(w.data(), w.size());
    CHECK(v.errors.empty());
    CHECK(v.mem.f(0x11, 0) == 1.f && v.mem.f(0x11, 2) == 3.f && v.mem.f(0x11, 3) == 1.0f);  // w da linha
    CHECK(v.mem.f(0x12, 1) == 5.f && v.mem.f(0x12, 3) == 1.0f);
    CHECK(v.mem.i(0x13, 0) == -1 && v.mem.i(0x13, 1) == 2);
    CHECK(v.mem.u(0x14, 0) == 200 && v.mem.u(0x14, 3) == 3);
    CHECK(v.mem.u(0x15, 0) == 10 && v.mem.u(0x15, 3) == 40 + ubits(1.0f));  // offset: dado + linha
    CHECK(v.mem.u(0x30, 0) == 7);
    CHECK(tops.size() == 2 && tops[0] == 0x10 && tops[1] == 0x30);
}

TEST(driving_vif_dma_chain_call_ret) {
    // CNT (2 palavras de VIF na etiqueta + 1 qword), CALL para um pacote com RET, END.
    Aligned buf(16 * 8);
    uint32_t* q = reinterpret_cast<uint32_t*>(buf.p);
    const uint32_t base = eagl::addr_of(buf.p);
    // 0: CNT qwc 1, VIF na etiqueta: NOP, NOP; dados: V4-32 n=1 @0 x3 (3 palavras de dados não cabem: usa S-32 n=3).
    q[0] = 0x10000001;
    q[1] = 0;
    q[2] = 0;
    q[3] = 0x60030000;  // S-32 n=3 @0 (a primeira palavra do comando na etiqueta)
    q[4] = 11;
    q[5] = 12;
    q[6] = 13;
    q[7] = 0;
    // 2: CALL qwc 0 -> 4
    q[8] = 0x50000000;
    q[9] = base + 64;
    q[10] = 0;
    q[11] = 0;
    // 3: END
    q[12] = 0x70000000;
    q[13] = 0;
    q[14] = 0;
    q[15] = 0x17000000;  // MSCNT na etiqueta do END
    // 4: RET qwc 0 com V4-8 na etiqueta? Não: a etiqueta só leva 2 palavras: NOP e STMOD 0.
    q[16] = 0x60000000;
    q[17] = 0;
    q[18] = 0;
    q[19] = 0x05000000;
    Vif1 v;
    int kicks = 0;
    v.on_kick = [&](Vif1::Kick, uint32_t, uint32_t, uint32_t) { ++kicks; };
    v.run_dma_chain(base);
    CHECK(v.errors.empty());
    CHECK(v.mem.u(0, 0) == 11 && v.mem.u(1, 0) == 12 && v.mem.u(2, 3) == 13);
    CHECK(kicks == 1);
}

// ---------------------------------------------------------------- o modelo e o GeoPrim

TEST(driving_geoprim_draw_and_model_chain) {
    eagl::GeoPrim_ResetMicrocode();
    // Duas primitivas num grupo: a primeira com o op 0xe/5/0xf e o op 4 (CALL para um pacote de vértices), a segunda
    // só com outro op 5. O construtor do modelo encadeia a segunda na primeira (a palavra 0 do fim vira o salto 0x10) e
    // a tira da lista.
    Aligned mem(0x400);
    uint8_t* m = mem.p;
    const uint32_t base = eagl::addr_of(m);
    auto W = [&](uint32_t off) -> uint32_t& { return *reinterpret_cast<uint32_t*>(m + off); };
    static float props[4] = {1, 2, 3, 4};
    static float other[4] = {5, 6, 7, 8};
    // Lista de comandos A em 0x000: 0xe; 5 (4 palavras de VIF, 2 u64 de props) -> UNPACK V4-32 n=1 @9 TOPS; 0xf; 4 (CALL
    // em 0x100); 0.
    uint32_t a = 0;
    W(a) = 0x0e000000;
    W(a + 4) = 0x05000004;
    W(a + 8) = 2;
    W(a + 12) = eagl::addr_of(props);
    W(a + 16) = 0;
    W(a + 20) = 0;
    W(a + 24) = 0;
    W(a + 28) = 0x6c01c009;
    W(a + 32) = 0x0f000000;
    W(a + 36) = 0x04000002;
    W(a + 40) = base + 0x100;
    W(a + 44) = 0;
    // Lista B em 0x040: 0xe; 5 (other -> @10); 0xf; 0.
    const uint32_t b = 0x40;
    W(b) = 0x0e000000;
    W(b + 4) = 0x05000004;
    W(b + 8) = 2;
    W(b + 12) = eagl::addr_of(other);
    W(b + 16) = 0;
    W(b + 20) = 0;
    W(b + 24) = 0;
    W(b + 28) = 0x6c01c00a;
    W(b + 32) = 0x0f000000;
    W(b + 36) = 0;
    // CALL qwc 0 em 0x100 -> o pacote em 0x120 (RET com V4-32 n=1 @0 e MSCNT).
    W(0x100) = 0x50000000;
    W(0x104) = base + 0x120;
    W(0x120) = 0x60000002;
    W(0x12c) = 0x6c01c000;
    W(0x130) = 3;
    W(0x140) = 0x17000000;
    // GeoPrims (0x18 bytes) em 0x180 e 0x1a0, handles em 0x1c0 e 0x1c8.
    W(0x180) = base + a;
    W(0x1a0) = base + b;
    W(0x1c0) = base + 0x180;
    W(0x1c8) = base + 0x1a0;
    // Modelo em 0x200 (+0xcc a lista em 0x300): cabeçalho {0xa0000000, 0, 4}, duas entradas, terminador 0.
    W(0x200 + 0xcc) = base + 0x300;
    W(0x300) = 0xa0000000;
    W(0x304) = 0;
    W(0x308) = 4;
    W(0x30c) = 0xa000ffff;
    W(0x310) = base + 0x1c0;
    W(0x314) = 0xa000ffff;
    W(0x318) = base + 0x1c8;
    W(0x31c) = 0;
    // O construtor do modelo, com a base dos saltos (só do port) no começo do bloco: o salto para B (0x40) é 0x10000010.
    eagl::Model_SetCommandBase(base);
    eagl::Model_Construct(base + 0x200);
    CHECK(W(0x308) == 2);                         // o grupo ficou com uma primitiva (2 palavras)
    CHECK(W(a + 44) == 0x10000010 && W(b + 36) == 0);  // o fim de A virou o salto para B
    const auto groups = eagl::Model_Groups(base + 0x200);
    CHECK(groups.size() == 1 && groups[0].geoprims.size() == 1 && groups[0].geoprims[0] == base + 0x180);
    // O desenho segue o salto: as duas UNPACK (props em TOP+9, other em TOP+10) e o vértice do pacote.
    eagl::DmaBuffer dma(256);
    eagl::DrawContext ctx;
    ctx.cmd_base = base;
    eagl::DrawStats ds;
    eagl::GeoPrim_Draw(base + 0x180, dma, ctx, ds);
    dma.put64(0x70000000ull);
    dma.put64(0);
    Vif1 v;
    std::vector<uint32_t> tops;
    v.on_kick = [&](Vif1::Kick, uint32_t, uint32_t top, uint32_t) { tops.push_back(top); };
    v.run_dma_chain(eagl::addr_of(dma.begin()));
    CHECK(v.errors.empty());
    CHECK(tops.size() == 1);
    CHECK(v.mem.f(9, 0) == 1.f && v.mem.f(9, 3) == 4.f && v.mem.f(10, 0) == 5.f && v.mem.u(0, 0) == 3);
    CHECK(ds.geoprims == 1 && ds.unported_ops == 0);
}

// ---------------------------------------------------------------- a referência do microprograma MatCar

namespace {

// Um lote de um vértice com valores escolhidos para que as contas sejam exatas.
CarBatch one_vertex_batch(const CarMethodLayout* l) {
    CarBatch b;
    b.layout = l;
    b.count = 1;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) b.mvp[r][c] = b.world[r][c] = r == c ? 1.0f : 0.0f;
    b.props[0] = 1.0f;  // ambiente
    b.props[1] = 1.0f;  // difusa
    b.props[2] = 0.5f;  // especular
    b.props[3] = 0.25f; // reflexo
    for (int& s : b.split) s = 0;
    CarVertexIn v{};
    v.pos[0] = ubits(2.0f);
    v.pos[1] = ubits(4.0f);
    v.pos[2] = ubits(8.0f);
    v.stq[0] = 0x8000;  // 32768 x I1 - 32: ~0
    v.stq[1] = 0x8000;
    v.normal[0] = 0;    // -1
    v.normal[1] = 255;  // 255 x I2 - 1 = 1
    v.normal[2] = 0;
    for (int c = 0; c < 4; ++c) v.dmgvec[c] = 0x8000;
    for (int c = 0; c < 3; ++c) v.dmgnrm[c] = 0x8000;
    v.dmgnrm[3] = 0;
    b.v.push_back(v);
    return b;
}

}  // namespace

TEST(driving_matcar_reference_by_hand) {
    const CarMethodLayout* l = car_method_layout("MatCarTexturePacked");
    CHECK(l && l->passes == (kPassBase | kPassSpec | kPassRefl) && l->packed && car_method_layout("MatCarChrome") &&
          car_method_layout("MatCarTextureDullPacked"));
    CHECK(!car_method_layout("MatCarGlassPacked") && !car_method_layout("MatCarTextureSwitchPacked"));
    CarBatch b = one_vertex_batch(l);
    CarConstants k;
    // Janela de vista: escala 1, deslocamento 0 (a tela = o clip / w). Névoa: perto 0, longe 10, escala 2.
    for (int c = 0; c < 4; ++c) {
        k.vp_offset[c] = 0;
        k.vp_scale[c] = 1;
    }
    k.fog[0] = 0;
    k.fog[2] = 10;
    k.fog[3] = 2;
    // Uma luz direcional que aponta +y (as direções transpostas: a coluna 0 é a luz 0) com cor 0,5; ambiente 0,25.
    k.light_dir_t[1][0] = 1.0f;
    for (int c = 0; c < 3; ++c) {
        k.light_col[0][c] = 0.5f;
        k.ambient[c] = 0.25f;
    }
    // Luzes pontuais apagadas: K = 0 dá att = 255, então a cor delas tem de ser 0.
    // Especular: SpecularMat identidade, ReflectionInfo2 = (0,5; 0,25; 0,5; 1).
    for (int r = 0; r < 4; ++r) k.spec_mat[r][r] = 1.0f;
    k.refl_info2[0] = 0.5f;
    k.refl_info2[1] = 0.25f;
    k.refl_info2[2] = 0.5f;
    k.refl_info2[3] = 1.0f;
    std::vector<CarVertexOut> out[3];
    car_reference(b, k, out);
    CHECK(out[0].size() == 1 && out[1].size() == 1 && out[2].size() == 1);
    // Normal: (0 x I2 - 1, 255 x I2 - 1, -1): I2 = 0x3c008080 fica um pouco abaixo de 2/255, então n.y não chega a 1
    // (0,9999999): a normal "para cima" do PS2 é um ulp mais curta.
    const float I2 = fbits(0x3c008080);
    const float ny = 255.0f * I2 - 1.0f;
    CHECK(ny < 1.0f && ny > 0.999999f);
    // Cor: 127 x (0,5 x 1 x max(0, n.y) + 0,25 x 1) = 127 x 0,75 = 95,25 -> 95; alfa 128.
    CHECK(out[0][0].rgba[0] == 95 && out[0][0].rgba[1] == 95 && out[0][0].rgba[2] == 95 && out[0][0].rgba[3] == 128);
    // STQ: s = 32768 x I1 - 32 (I1 = 0x3a7ffff7, um pouco menos que 1/1024): negativo, bem perto de 0; Q = 1 / w = 1.
    const float I1 = fbits(0x3a7ffff7);
    const float s = 32768.0f * I1 - 32.0f;
    CHECK(s < 0.0f && s > -0.0002f);
    CHECK(out[0][0].stq[2] == 1.0f && out[0][0].stq[0] == s);
    // Tela = clip (MVP identidade, w = 1): XYZF = (2, 4, 8) x 16; F = 255 - 2 x (1 - 0) = 253 -> 253 x 16.
    CHECK(out[0][0].xyzf[0] == 32 && out[0][0].xyzf[1] == 64 && out[0][0].xyzf[2] == 128);
    // O SplitReg zerado põe o ADC (bit 15 do w, com o AND 0xbffe do microprograma) nos vértices 0 e 1.
    CHECK(out[0][0].adc && out[0][0].xyzf[3] == ((253 * 16) & 0xBFFE) + 0x8000);
    // Passada 2: E = Mundo x SpecularMat x 0,5 = 0,5 I; ST = 0,5 + 0,5 n.xy = (0, ~1); Q = 1; cor = int(0,5 x 128) = 64.
    CHECK(out[1][0].stq[0] == 0.0f && out[1][0].stq[2] == 1.0f);
    CHECK_NEAR(out[1][0].stq[1], 1.0, 1e-6);
    CHECK(out[1][0].rgba[0] == 64 && out[1][0].rgba[3] == 128);
    // Passada 3: v = p x 0,5 + n = (0, ~3, 3); a = Mundo x v x 0,25 = (0, ~0,75, 0,75); ST = (0,5, 1,25);
    // cor = int(min(0,25 x 128, 100000 x 0,75)) = 32.
    CHECK(out[2][0].stq[0] == 0.5f && out[2][0].stq[1] == 1.25f);
    CHECK(out[2][0].rgba[0] == 32);
    // Sem luz que alcance (n.y = -1): só o ambiente, 127 x 0,25 = 31,75 -> 31.
    b.v[0].normal[1] = 0;
    car_reference(b, k, out);
    CHECK(out[0][0].rgba[0] == 31);
    // Abaixo do "horizonte" do reflexo (a.y < 0: p.y = -8, v.y = -4 + n.y): o reflexo some (o 100000 x max(a.y, 0)).
    b.v[0].pos[1] = ubits(-8.0f);
    car_reference(b, k, out);
    CHECK(out[2][0].rgba[0] == 0);
}

// O MatCarDash é o mesmo microprograma sem o passo 1 (as entradas já em float) e sem a passada 3; o z do STQ vem do
// arquivo e entra no Q da passada 1 (S = s Q, T = t Q, Q = z Q).
TEST(driving_matcar_dash_float_inputs_two_passes) {
    const CarMethodLayout* l = car_method_layout("MatCarDash");
    CHECK(l && !l->packed && l->passes == (kPassBase | kPassSpec) && l->normal == 0x4e && l->stq == 0x7a &&
          l->state == 0xfe && l->env_state == 0x106);
    CarBatch b = one_vertex_batch(l);
    CarVertexIn& v = b.v[0];
    v.stq[0] = ubits(0.25f);
    v.stq[1] = ubits(0.5f);
    v.stq[2] = ubits(2.0f);
    v.normal[0] = ubits(0.0f);
    v.normal[1] = ubits(1.0f);
    v.normal[2] = ubits(0.0f);
    for (int c = 0; c < 4; ++c) v.dmgvec[c] = ubits(0.0f);
    for (int c = 0; c < 3; ++c) v.dmgnrm[c] = ubits(0.0f);
    v.dmgnrm[3] = 5;  // a zona continua inteira (ILWR.w)
    const CarVertexWork w = car_convert(*l, v);
    CHECK(w.pos[1] == 4.0f && w.stq[0] == 0.25f && w.stq[2] == 2.0f && w.normal[1] == 1.0f && w.zone == 5);
    CarConstants k;
    for (int c = 0; c < 4; ++c) {
        k.vp_offset[c] = 0;
        k.vp_scale[c] = 1;
    }
    k.light_dir_t[1][0] = 1.0f;
    for (int c = 0; c < 3; ++c) {
        k.light_col[0][c] = 0.5f;
        k.ambient[c] = 0.25f;
    }
    for (int r = 0; r < 4; ++r) k.spec_mat[r][r] = 1.0f;
    k.refl_info2[2] = 0.5f;
    std::vector<CarVertexOut> out[3];
    car_reference(b, k, out);
    CHECK(out[0].size() == 1 && out[1].size() == 1 && out[2].empty());
    // n.y = 1 exato: 127 x (0,5 + 0,25) = 95,25 -> 95.
    CHECK(out[0][0].rgba[0] == 95 && out[0][0].rgba[3] == 128);
    CHECK(out[0][0].stq[0] == 0.25f && out[0][0].stq[1] == 0.5f && out[0][0].stq[2] == 2.0f);
    // Especular: ST = 0,5 + 0,5 x (0, 1) = (0,5; 1); cor = int(0,5 x 128) = 64.
    CHECK(out[1][0].stq[0] == 0.5f && out[1][0].stq[1] == 1.0f && out[1][0].stq[2] == 1.0f);
    CHECK(out[1][0].rgba[0] == 64);
    // O mesmo vértice "Packed" (os inteiros que o passo 1 leva aos mesmos floats) dá a mesma cor.
    CarVertexIn pk{};
    for (int c = 0; c < 3; ++c) pk.pos[c] = v.pos[c];
    pk.normal[1] = 255;
    const CarVertexWork pw = car_convert(*car_method_layout("MatCarTexturePacked"), pk);
    CHECK(pw.stq[2] == 1.0f && pw.normal[0] == -1.0f && pw.normal[1] < 1.0f);
}

TEST(driving_matcar_damage_point_lights_and_strips) {
    const CarMethodLayout* l = car_method_layout("MatCarTextureDullPacked");
    CarBatch b = one_vertex_batch(l);
    CarConstants k;
    for (int c = 0; c < 4; ++c) {
        k.vp_offset[c] = 0;
        k.vp_scale[c] = 1;
    }
    for (int c = 0; c < 3; ++c) k.ambient[c] = 1.0f;
    // Zona de dano 3: z.x = 0,5, z.y = 0,5, z.z = 0. Vetor de dano (+1024 em x -> 1, w +256 -> 0,25).
    k.damage[3][0] = 0.5f;
    k.damage[3][1] = 0.5f;
    b.v[0].dmgnrm[3] = 3;
    b.v[0].dmgvec[0] = 0x8000 + 1024;
    b.v[0].dmgvec[3] = 0x8000 + 256;
    // Uma luz pontual em (2, 4, 8) (no vértice deslocado? não: o vértice vai para x = 2 + 1 x 0,5), K = 4, cor 0,1.
    k.point_pos[0][0] = 2.0f;
    k.point_pos[1][0] = 4.0f;
    k.point_pos[2][0] = 8.0f;
    k.point_pos[3][0] = 4.0f;
    k.point_col[0][0] = 0.1f;
    std::vector<CarVertexOut> out[3];
    car_reference(b, k, out);
    CHECK(out[1].empty() && out[2].empty());  // Dull: uma passada
    // A posição andou 1 x 0,5 em x (I1 x 33792 - 32 é 1 menos um pouco): clip.x ~ 2,5 -> FTOI4 = 39 (2,4999 x 16).
    const float I1 = fbits(0x3a7ffff7);
    const float dx = (33792.0f * I1 - 32.0f) * 0.5f;
    CHECK(out[0][0].xyzf[0] == int((2.0f + dx) * 16.0f));
    // Cor: 127 x (1 - 0,25 x 0,5) = 111,125 -> 111 (o dano escurece); x ambiente 1 = 111; luz pontual: d² = dx², att =
    // 255 - 4 dx², vermelho += 0,1 x att.
    const float wv = 256.0f * I1;  // dmgvec.w = 33024 x I1 - 32
    const float dw = (33024.0f * I1 - 32.0f);
    (void)wv;
    const int base = int(std::max(127.0f * (1.0f - dw * 0.5f), 0.0f));
    const float att = std::max(dx * dx * -4.0f + 255.0f, 0.0f);
    CHECK(out[0][0].rgba[1] == base);
    CHECK(out[0][0].rgba[0] == int(std::min(float(base) + 0.1f * att, 255.0f)));
    // Faixas: 6 vértices, recomeço no vértice 3 (SplitReg = 9): triângulos (0,1,2) e (3,4,5).
    CarBatch s = one_vertex_batch(l);
    s.count = 6;
    s.v.resize(6, s.v[0]);
    s.split[0] = 9;
    std::vector<uint16_t> tris;
    car_triangles(s, tris);
    // Os SplitReg zerados também marcam 0 e 1 (inofensivo: não fecham triângulo).
    CHECK(tris.size() == 6 && tris[0] == 0 && tris[2] == 2 && tris[3] == 3 && tris[5] == 5);
    // Numa lista os triângulos fecham no 2 e no 5, nenhum dos dois marcado.
    s.strip = false;
    tris.clear();
    car_triangles(s, tris);
    CHECK(tris.size() == 6 && tris[3] == 3 && tris[5] == 5);
    // Um recomeço no vértice 4 (SplitReg = 12) marca 4 e 5: a lista perde o (3,4,5).
    s.split[0] = 12;
    tris.clear();
    car_triangles(s, tris);
    CHECK(tris.size() == 3);
}

// As constantes globais vão para a memória da VU nos endereços do microprograma e voltam iguais (a conferência contra o
// interpretador lê da memória o que a referência usa).
TEST(driving_car_constants_round_trip) {
    CarConstants k;
    k.vp_offset[0] = 2048.5f;
    k.point_pos[3][2] = 0.25f;
    k.fog[3] = 3.0f;
    k.light_dir_t[2][1] = -0.5f;
    k.ambient[2] = 0.125f;
    k.spec_mat[3][0] = 7.0f;
    k.refl_info2[2] = 0.9f;
    k.damage[15][3] = 11.0f;
    VuMemory m{};
    k.write(m);
    CHECK(m.f(0, 0) == 2048.5f && m.f(0x43, 2) == 0.25f && m.f(0x48, 3) == 3.0f && m.f(0x4b, 1) == -0.5f &&
          m.f(0x4f, 2) == 0.125f && m.f(0x53, 0) == 7.0f && m.f(0x55, 2) == 0.9f && m.f(0x65, 3) == 11.0f);
    const CarConstants r = car_constants_from(m);
    CHECK(std::memcmp(&r, &k, sizeof k) == 0);
}

TEST(driving_car_lighting_from_tuning) {
    float p[7][4];
    // As linhas como no disco: "\r\n\r" (o "\r" do fim vai para o começo da linha seguinte).
    car_lighting_from_tuning("[Paint]Ambient{default}=0.5\r\n\r[Paint]Ambient{vanquish}=0.790000\r\n\r"
                             "[Paint]Diffuse{vanquish}=0.42\r\n\rSpecularSpot{vanquish}=0.900002\r\n\r"
                             "[Dull]Diffuse{bus}=9\r\n\r",
                             "vanquish", p);
    CHECK_NEAR(p[2][0], 0.79, 1e-6);
    CHECK_NEAR(p[2][1], 0.42, 1e-6);
    CHECK_NEAR(p[6][2], 0.900002, 1e-6);
    CHECK_NEAR(p[3][1], 0.5, 1e-6);  // o {bus} não vale
    CHECK_NEAR(p[2][3], 0.8, 1e-6);  // o padrão do RReflection
}
