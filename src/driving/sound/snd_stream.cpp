// Os fluxos do SND no EE (driving/sound/snd_stream.h): SNDSTRM_*, SNDSTRMI_*, SNDPKTPLAY*, SNDLINKI_*,
// SNDI_patchtohdr, portados do MIPS do DRIVING.ELF.
//
// Divergências (contadas em snd_stream_guards()):
//  - a fila dos callbacks do tocador (0x0035e07c, 96 entradas de 8 bytes) não tem limite no original: a 97ª escreveria
//    na tabela dos tocadores; o porte não grava e conta;
//  - um pacote cujo canal não tem dados (o ponteiro 0) faz o SNDPKTPLAYI_get devolver 0xffffffff, que o
//    desempacotador leria; o porte devolve nulo e conta (nenhum SCDl do disco tem um canal vazio);
//  - mais de 4 blocos de dados do usuário (etiqueta 0x14) num cabeçalho escreveriam além da tabela: o porte para no 4º;
//  - o STREAM_* embaixo é o do PC (driving/sound/stream.h: a leitura do disco inteira e na hora).

#include "driving/sound/snd_stream.h"

#include <cstring>

#include "driving/sound/snd.h"
#include "driving/sound/snd_state.h"
#include "driving/sound/stream.h"

namespace drv {

int32_t SNDPKTPLAY_overhead(int32_t packets);
int32_t SNDPKTPLAY_create(uint32_t release_cb, uint32_t frames_cb, SndStreamChannel* context, uint8_t* mem,
                          int32_t size);
int32_t SNDPKTPLAY_destroy(int32_t player);
int32_t SNDPKTPLAY_framesoutstanding(int32_t player);
int32_t SNDPKTPLAY_submitspace(int32_t player);
int32_t SNDPKTPLAY_submit(int32_t player, const uint8_t* pkt);
int32_t SNDPKTPLAY_start(int32_t player, const SndSampleFormat* fmt, const SndSampleAttr* attr,
                         const SndPlayOpts* opts);
int32_t SNDPKTPLAY_stop(int32_t player);
int32_t SNDPKTPLAYI_gethighchannel(int32_t v, int32_t n);

namespace {

constexpr int32_t kCallbackSlots = 0x60;

struct StreamGlobals {
    nf::P32<SndStreamChannel> table[0x20] = {};  // 0x00357160
    nf::P32<SndPacketPlayer> players[0x20] = {};  // 0x0035e37c
    int32_t cb_count = 0;                         // 0x0035e078
    struct Callback {
        int16_t type;                             // 0 quadros, 1 liberação
        int16_t player;
        uint32_t data;                            // as amostras, ou o endereço dos dados (P32)
    } cb[kCallbackSlots] = {};                    // 0x0035e07c
    uint64_t guards = 0;
};
StreamGlobals g;

void guard() { ++g.guards; }

int32_t max_streams() { return snd_state().opts[0x47]; }

SndStreamChannel* ch_ptr(int32_t i) { return nf::raw(g.table[i]); }
SndPacketPlayer* pl(int32_t i) { return nf::raw(g.players[i]); }
SndPacket& packet(SndPacketPlayer* p, uint16_t index) { return p->packets[int16_t(index)]; }

void push_callback(int16_t type, int32_t player, uint32_t data) {
    if (g.cb_count >= kCallbackSlots) {
        guard();
        return;
    }
    StreamGlobals::Callback& c = g.cb[g.cb_count];
    c.data = data;
    c.player = int16_t(player);
    c.type = type;
    g.cb_count += 1;
}

// ---- SNDLINKI ----

// [driving] 0x003112e8 SNDLINKI_init
void SNDLINKI_init(SndLinkList* l) {
    l->count = 0;
    l->head = nullptr;
    l->tail = nullptr;
}

// [driving] 0x003112f8 SNDLINKI_push
void SNDLINKI_push(SndLinkList* l, SndStreamRequest* n) {
    SndStreamRequest* h = nf::raw(l->head);
    n->prev = nullptr;
    n->next = h;
    if (l->head == nullptr) l->tail = n;
    else nf::raw(l->head)->prev = n;
    l->head = n;
    l->count += 1;
}

// [driving] 0x00311328 SNDLINKI_pushtail
void SNDLINKI_pushtail(SndLinkList* l, SndStreamRequest* n) {
    n->next = nullptr;
    n->prev = l->tail;
    if (l->tail == nullptr) l->head = n;
    else nf::raw(l->tail)->next = n;
    l->tail = n;
    l->count += 1;
}

// [driving] 0x00311358 SNDLINKI_pop
SndStreamRequest* SNDLINKI_pop(SndLinkList* l) {
    SndStreamRequest* n = nf::raw(l->head);
    if (n != nullptr) {
        SndStreamRequest* nx = nf::raw(n->next);
        l->head = nx;
        if (nx == nullptr) l->tail = nullptr;
        else nx->prev = nullptr;
        l->count -= 1;
    }
    return n;
}

// [driving] 0x00311390 SNDLINKI_remove
void SNDLINKI_remove(SndLinkList* l, SndStreamRequest* n) {
    if (n == nf::raw(l->head)) l->head = n->next;
    SndStreamRequest* p;
    if (n == nf::raw(l->tail)) {
        l->tail = n->prev;
        p = nf::raw(n->prev);
    } else {
        p = nf::raw(n->prev);
    }
    if (p != nullptr) p->next = n->next;
    if (n->next != nullptr) nf::raw(n->next)->prev = n->prev;
    l->count -= 1;
}

// ---- o cabeçalho ----

// [driving] 0x00312188 (o padrão dos atributos de um fluxo)
void SNDI_defaultattributes(SndSampleAttr* a) {
    a->vol = 0x7f;
    a->pan = 0x40;
    a->render = 8;
    a->priority = 0;
    a->w00 = 0;
    a->fx = 0;
    a->bend_range = 0;
    a->t80 = 0;
    for (int i = 0; i < 4; ++i) {
        a->user[i] = nullptr;
        a->user_size[i] = 0;
    }
    for (int i = 0; i < 6; ++i) {
        a->azimuth[i] = 0;
        a->stretch[i] = nullptr;
    }
}

// [driving] 0x0030c0f8 SNDI_patchtohdr(void *, TAGGEDPATCH *, SNDSAMPLEFORMAT *, SNDSAMPLEATTRIBUTES *, ...): o
// cabeçalho PT de um fluxo; info: [0] as amostras, [1..6] os deslocamentos dos canais.
void SNDI_patchtohdr(uint32_t base, const uint8_t* patch, SndSampleFormat* fmt, SndSampleAttr* attr, uint32_t* info) {
    uint32_t az_extra[6] = {};
    std::memset(fmt, 0, 4);
    std::memset(attr, 0, sizeof *attr);
    std::memset(info, 0, 0x1c);
    SNDI_defaultattributes(attr);
    fmt->channels = 1;
    fmt->codec = 5;
    SndTagReader r;
    r.cursor = patch + 4;
    fmt->rate = 0x5622;
    info[0] = 0;
    uint32_t flags = 1, render = 8;
    const uint8_t* t8a = nullptr;
    int32_t users = 0;
    while (SNDI_gettag(r) != 0) {
        const uint32_t v = uint32_t(r.value);
        auto copy = [&](int k) {
            uint8_t* m = static_cast<uint8_t*>(SNDMEMI_allocz(int32_t(r.length)));
            attr->stretch[k] = m;
            if (m != nullptr) std::memcpy(m, r.data, r.length);  // SNDI_memcpy
            attr->stretch_size[k] = r.length;
        };
        switch (r.tag) {
        case 0xa0: fmt->codec = uint8_t(v); break;
        case 0x9c: az_extra[0] = v; break;
        case 0x9d: az_extra[1] = v; break;
        case 0x9e: az_extra[2] = v; break;
        case 0x9f: az_extra[3] = v; break;
        case 0xa6: az_extra[4] = v; break;
        case 0xa7: az_extra[5] = v; break;
        case 0x98: copy(0); break;
        case 0x99: copy(1); break;
        case 0x9a: copy(2); break;
        case 0x9b: copy(3); break;
        case 0xa4: copy(4); break;
        case 0xa5: copy(5); break;
        case 0x80: attr->t80 = uint8_t(v); break;
        case 0x82: fmt->channels = uint8_t(v); break;
        case 0x84: fmt->rate = uint16_t(v); break;
        case 0x85: info[0] = v; break;
        case 0x8a: t8a = r.data; break;
        case 0x13: attr->fx = uint8_t(v); break;
        case 0x0a: attr->bend_range = int8_t(v); break;
        case 0x06: attr->priority = uint8_t(v); break;
        case 0x8c: render = v; break;
        case 0x88: info[1] = v; break;
        case 0x89: info[2] = v; break;
        case 0x94: info[3] = v; break;
        case 0x95: info[4] = v; break;
        case 0xa2: info[5] = v; break;
        case 0xa3: info[6] = v; break;
        case 0xa1: flags = v; break;
        case 0x14:
            if (users >= 4) {
                guard();
                break;
            }
            attr->user[users] = const_cast<uint8_t*>(r.data);
            attr->user_size[users] = r.length;
            ++users;
            break;
        default: break;
        }
    }
    if (flags != 1) {
        if (render != 8) {
            attr->render = uint16_t(render);
            goto channels;
        }
        render = (flags & 1) != 0 ? 8u : 0u;
        if ((flags & 2) != 0) render |= 4;
        if ((flags & 0x80) != 0) render |= 0x20;
        render = (flags & 0x40) | render;
    }
    attr->render = uint16_t(render);
channels:
    const SndState& s = snd_state();
    for (int c = fmt->channels - 1; c >= 0; --c) {
        const int n = fmt->channels;
        if (s.opts[sndopt::kOutputMode] == 2 && fmt->channels == 2) az_extra[c] = 0;
        attr->azimuth[c] = uint16_t(s.chan_az[n - 1][c] + uint16_t(az_extra[c]));
        const uint16_t rm = attr->render;
        if ((rm & 0x14) != 0 || rm == 0) info[c + 1] = base + info[c + 1];
        if ((rm & 0x308) != 0 && t8a != nullptr) {
            uint32_t w;
            std::memcpy(&w, t8a, 4);
            if (c == 0) info[1] = w;
            else info[c + 1] = w + (info[c + 1] - info[1]);
        }
    }
}

// [driving] 0x003087e8 SNDSTRMI_calcdatarate(SNDSAMPLEFORMAT *): bytes por segundo.
int32_t SNDSTRMI_calcdatarate(const SndSampleFormat* f) {
    int32_t k = 0;
    switch (f->codec) {
    case 5: k = 0x92; break;
    case 10: k = 0x88; break;
    case 4: k = 0x33; break;
    case 8: k = 0x200; break;
    case 0xe: return int32_t(f->channels) * 0xc;
    case 0xf:
    case 0x10: return int32_t(f->channels) << 3;
    default: break;
    }
    return int32_t(uint32_t(f->rate) * f->channels * uint32_t(k)) >> 8;
}

// [driving] 0x003088d8 SNDSTRMI_getstreamptr
SndStreamChannel* SNDSTRMI_getstreamptr(int32_t stream) {
    if (stream < max_streams() && stream > -1) return ch_ptr(stream);
    return nullptr;
}

// [driving] 0x00309978 SNDSTRMI_getrequestptr
SndStreamRequest* SNDSTRMI_getrequestptr(int32_t id) {
    SndStreamChannel* c;
    if (id < 0 || (c = SNDSTRMI_getstreamptr(id & 0xff)) == nullptr) return nullptr;
    SndStreamRequest* r = nf::raw(c->active.head);
    while (r != nullptr && r->id != id) r = nf::raw(r->next);
    return r;
}

// [driving] 0x00308910 SNDSTRMI_removerequest
void SNDSTRMI_removerequest(int32_t id) {
    SndStreamChannel* c = ch_ptr(id & 0xff);
    SndStreamRequest* r = SNDSTRMI_getrequestptr(id);
    SNDLINKI_remove(&c->active, r);
    SNDLINKI_push(&c->free_list, r);
    if (nf::raw(c->header_req) == r) c->header_req = nullptr;
}

// [driving] 0x003089a0 SNDSTRMI_framescallback(int, unsigned int, SNDSTREAMCHANNEL *)
void SNDSTRMI_framescallback(int32_t /*player*/, uint32_t frames, SndStreamChannel* c) {
    int32_t n = 0;
    do {
        ++n;
        SndStreamRequest* r = nf::raw(c->active.head);
        if (r == nullptr) {
            guard();  // o original leria o pedido nulo
            return;
        }
        if (200 < n) {
            SNDSTRMI_removerequest(r->id);
            return;
        }
        uint32_t take = frames, rest = 0;
        if (r->outstanding < frames) {
            rest = frames - r->outstanding;
            take = frames - rest;
        }
        frames = rest;
        r->played += take;
        r->outstanding -= take;
        if (r->total <= r->played) SNDSTRMI_removerequest(r->id);
    } while (frames != 0);
}

// [driving] 0x00308978 SNDSTRMI_releasecallback(void *): o pacote acabou; o pedaço volta ao STREAM.
void SNDSTRMI_releasecallback(uint32_t data) {
    uint8_t* d = nf::p32_ptr<uint8_t>(data);
    uint8_t* chunk = nf::p32_load<uint8_t>(d - 4);
    STREAM_release(ch_ptr(chunk[0])->stream, chunk);
}

// [driving] 0x00308760 SNDSTRMI_startstream
void SNDSTRMI_startstream(SndStreamChannel* c) {
    c->voice = SNDPKTPLAY_start(c->player, &c->fmt, &c->attr, &c->opts);
    uint32_t present;
    std::memcpy(&present, c->filter + 0x10, 4);
    if (present != 0) guard();  // SNDCTRL_filteradd: nenhum fluxo do jogo pede filtro
    c->state = 1;
}

// [driving] 0x00308a60 SNDSTRMI_parseheader(int, STREAMCHUNKHDR *)
int32_t SNDSTRMI_parseheader(int32_t idx, uint8_t* chunk) {
    SndStreamChannel* c = ch_ptr(idx);
    c->header_req = c->header_req == nullptr ? c->active.head : nf::raw(c->header_req)->next;
    SndStreamRequest* r = nf::raw(c->header_req);
    uint32_t info[7];
    SNDI_patchtohdr(0, chunk + 8, &c->fmt_new, &c->attr_new, info);
    r->started = 0;
    r->total = info[0];
    for (int i = 0; c->attr_new.user[i] != nullptr; ++i) {
        // (os clientes do usuário do SND, com o tipo 3: nenhum no jogo)
        if (snd_state().n_user_clients > 0) guard();
        c->attr_new.user[i] = nullptr;
        c->attr_new.user_size[i] = 0;
        if (i + 1 >= 4) break;
    }
    STREAM_release(c->stream, chunk);
    r->rate = uint32_t(SNDSTRMI_calcdatarate(&c->fmt_new));
    const bool same = std::memcmp(&c->fmt, &c->fmt_new, 4) == 0 && std::memcmp(&c->attr, &c->attr_new, 0x68) == 0;
    if (!(same && c->attr_new.stretch[0] == nullptr)) {
        if (c->fmt.rate != 0) {
            c->state = 2;
            return 0;
        }
        c->fmt = c->fmt_new;
        std::memcpy(&c->attr, &c->attr_new, 0x68);
        c->attr_new.stretch[0] = nullptr;
    }
    if (c->state != 1) {
        SNDSTRMI_startstream(c);
        c->state = 1;
    }
    return 0;
}

// [driving] 0x00308d68 SNDSTRMI_parsedata(SNDSTREAMCHANNEL *, STREAMCHUNKHDR *)
void SNDSTRMI_parsedata(SndStreamChannel* c, uint8_t* chunk) {
    uint32_t frames;
    std::memcpy(&frames, chunk + 8, 4);
    frames &= 0x7fffffff;
    const uint8_t n = c->fmt.channels;
    uint8_t* data[6] = {};
    const uint8_t* offs = chunk + 0xc;
    uint8_t* base = chunk + 0xc + n * 4;
    for (int i = 0; i < n; ++i) {
        uint32_t o;
        std::memcpy(&o, offs + i * 4, 4);
        data[i] = base + o;
    }
    SndStreamRequest* r = nf::raw(c->header_req);
    if (r == nullptr || n == 0) {
        guard();  // um SCDl antes do SCHl: o original leria o pedido nulo
        STREAM_release(c->stream, chunk);
        return;
    }
    if (frames == 0) {
        STREAM_release(c->stream, chunk);
        return;
    }
    nf::p32_store(data[0] - 4, chunk);
    std::memcpy(chunk, &r->id, 4);
    if (r->started == 0 && c->fmt.codec == 0xf && frames % 0x480 == 0) {
        frames = (frames - 0x1e1) & 0x7fffffff;
        r->total -= 0x1e1;
    }
    r->outstanding += frames;
    uint8_t pkt[0x24] = {};
    const uint32_t w = frames | (uint32_t(r->started) << 31);
    std::memcpy(pkt + 4, &w, 4);
    for (int i = 0; i < n; ++i) nf::p32_store(pkt + 0xc + i * 4, data[i]);
    SNDPKTPLAY_submit(c->player, pkt);
    r->started = 1;
}

// [driving] 0x00308f30 SNDSTRMI_parsechunk(int, STREAMCHUNKHDR *)
int32_t SNDSTRMI_parsechunk(int32_t idx, uint8_t* chunk) {
    uint32_t tag;
    std::memcpy(&tag, chunk, 4);
    if (tag == 0x6c444353u) {  // 'SCDl'
        SNDSTRMI_parsedata(ch_ptr(idx), chunk);
        return 1;
    }
    if (tag == 0x6c484353u) {  // 'SCHl'
        SNDSTRMI_parseheader(idx, chunk);
        return 0;
    }
    STREAM_release(ch_ptr(idx)->stream, chunk);
    return 1;
}

// [driving] 0x00308fa8 SNDSTRMI_isheld
int32_t SNDSTRMI_isheld(SndStreamChannel* c) {
    SndStreamRequest* r = nf::raw(c->header_req);
    if (r == nullptr || r->rate == 0) return 0;
    if (r->hold < 0) return 1;
    if (r->hold == 0) return 0;
    uint32_t t = uint32_t(STREAM_gettable(c->stream));
    if (4000000u < t) t = 4000000u;
    if ((t * 1000u) / r->rate < uint32_t(r->hold)) {
        if (STREAM_state(c->stream) != 2) {
            if (c->free_list.count > 0) return 1;
            if (STREAM_state(c->stream) != 0) return 1;
        }
    }
    r->hold = 0;
    return 0;
}

// [driving] 0x003092c8 SNDSTRMI_numcreated
int32_t SNDSTRMI_numcreated() {
    int32_t n = 0;
    for (int i = 0; i < max_streams(); ++i) n += g.table[i] != nullptr ? 1 : 0;
    return n;
}

// [driving] 0x00308878 SNDSTRMI_destroyall
int32_t SNDSTRMI_destroyall(int32_t) {
    for (int i = 0; i < max_streams(); ++i) SNDSTRM_destroy(i);
    return 0;
}

// [driving] 0x003095d0 SNDSTRMI_queue(int, int, char *, int, int)
int32_t SNDSTRMI_queue(int32_t stream, int32_t hold, const char* name, uint32_t arg, int32_t type) {
    SndStreamChannel* c = SNDSTRMI_getstreamptr(stream);
    if (c == nullptr) return -8;
    if (c->free_list.count == 0) return -13;
    if (type == 0) arg = STREAM_queuefile(c->stream, name, int32_t(arg), 0x6c454353u);  // até o 'SCEl'
    else if (type == 1) guard();  // STREAM_queuemem: o jogo não usa
    if (arg == 0) return -1;
    SNDSYS_entercritical();
    SndStreamRequest* r = SNDLINKI_pop(&c->free_list);
    std::memset(static_cast<void*>(r), 0, 0x28);
    SNDLINKI_pushtail(&c->active, r);
    r->stream_req = arg;
    c->gen += 0x100;
    if (c->gen < 0) c->gen = 0;
    r->id = c->gen | stream;
    r->hold = hold;
    SNDSYS_leavecritical();
    return r->id;
}

}  // namespace

// ---- o tocador de pacotes ----

// [driving] 0x00311650 SNDPKTPLAYI_overhead / 0x00311660 SNDPKTPLAY_overhead (o da plataforma é 0)
int32_t SNDPKTPLAY_overhead(int32_t packets) { return packets * 0x20 + 0x7c; }

// [driving] 0x00311690 SNDPKTPLAY_create
int32_t SNDPKTPLAY_create(uint32_t release_cb, uint32_t frames_cb, SndStreamChannel* context, uint8_t* mem,
                          int32_t size) {
    SNDSYS_entercritical();
    for (int i = 0; i < max_streams(); ++i) {
        if (g.players[i] != nullptr) continue;
        // (SNDPLATFORM_packetplaycreate: 0; SNDPLATFORM_packetoverhead: 0)
        SndPacketPlayer* p = reinterpret_cast<SndPacketPlayer*>(mem);
        p->mem = mem;
        p->release_cb = release_cb;
        p->slots = uint16_t(uint32_t(size - 0x7c) >> 5);
        p->frames_cb = frames_cb;
        p->context = context;
        g.players[i] = p;
        p->voice = -1;
        SNDSYS_leavecritical();
        return i;
    }
    SNDSYS_leavecritical();
    return -9;
}

// [driving] 0x00311dc8 SNDPKTPLAY_destroy
int32_t SNDPKTPLAY_destroy(int32_t player) {
    g.players[player] = nullptr;
    return 0;
}

// [driving] 0x00311cc0 SNDPKTPLAY_framesoutstanding
int32_t SNDPKTPLAY_framesoutstanding(int32_t player) { return pl(player)->frames_queued + pl(player)->frames_out; }

// [driving] 0x00311c88 SNDPKTPLAY_submitspace
int32_t SNDPKTPLAY_submitspace(int32_t player) { return int32_t(int16_t(pl(player)->slots)) - pl(player)->queued[0] - 1; }

// [driving] 0x00311b30 SNDPKTPLAY_submit
int32_t SNDPKTPLAY_submit(int32_t player, const uint8_t* pkt) {
    SndPacketPlayer* p = pl(player);
    if (!(p->queued[0] < int16_t(p->slots) - 1)) return -13;
    uint32_t frames;
    std::memcpy(&frames, pkt + 4, 4);
    SndPacket& k = packet(p, p->wr);
    k.frames = frames;
    k.seq = p->seq;
    for (int i = 0; i < p->fmt.channels; ++i) {
        k.data[i] = nf::p32_load<const uint8_t>(pkt + 0xc + i * 4);
        p->queued[i] = int16_t(p->queued[i] + 1);
    }
    p->frames_queued += int32_t(frames & 0x7fffffff);
    const int32_t seq = p->seq;
    p->seq += 1;
    p->wr = uint16_t(p->wr + 1);
    p->active = 1;
    if (int16_t(p->slots) <= int16_t(p->wr)) p->wr = 0;
    return seq;
}

// [driving] 0x003117c8 SNDPKTPLAY_start(int, SNDSAMPLEFORMAT *, SNDSAMPLEATTRIBUTES *, SNDPLAYOPTS *)
int32_t SNDPKTPLAY_start(int32_t player, const SndSampleFormat* fmt, const SndSampleAttr* attr,
                         const SndPlayOpts* opts) {
    SndPacketPlayer* p = pl(player);
    SndPatchHeader fake{};  // a pilha do original: só o +3 (os canais) e o +0x18 (o modo) são lidos
    fake.channels = int8_t(fmt->channels);
    fake.render = attr->render;
    const uint8_t chans = fmt->channels;
    p->fmt = *fmt;
    p->seq = 0;
    p->frames_queued = 0;
    p->frames_out = 0;
    p->wr = 0;
    p->release_rd = 0;
    p->active = 1;
    for (int i = 0; i < p->fmt.channels; ++i) {
        p->queued[i] = 0;
        p->stretch[i] = nf::p32_u(nf::raw(attr->stretch[i]));
        p->rd[i] = 0;
    }
    int32_t index = 0;
    uint32_t mode;
    while ((mode = SNDI_validrendermode(&index, &fake)) != 0) {
        int32_t first = 0, end = 0;
        SNDPLATFORM_getvoicerange(mode, &first, &end);
        int32_t handle = 0;
        const int32_t v = SNDVOICEI_alloc(chans, 0x65, &handle, first, end);
        if (v < 0) {
            p->voice = -9;
            continue;
        }
        p->voice = handle;
        p->high = uint8_t(SNDPKTPLAYI_gethighchannel(v, fmt->channels));
        SndVoice& s = snd_state().voices[v];
        s.builtin_azimuth = 0;
        s.fade_step = 0;
        s.bank = -1;
        s.azimuth = opts->azimuth;
        s.detune = int16_t(attr->w00);
        s.pitch_mult = opts->pitch_mult;
        s.env_ticks = 0x7fffffff;
        s.env = 0x7f0000;
        s.fade = int32_t(opts->vol) << 16;
        s.env_step = 0;
        s.builtin_vol = int8_t(attr->vol);
        s.env_count = 1;
        s.bend = opts->bend;
        s.env_current = 0;
        s.env_release = 0;
        s.builtin_fx = int8_t(attr->fx);
        s.prog_vol = opts->prog_vol;
        s.fx_level = opts->fx_level;
        s.key = 0;
        s.key_last = 0;
        s.bend_range = int16_t(attr->bend_range * 100);
        s.env_table = nullptr;
        s.vol_table = nullptr;
        s.bend_table = nullptr;
        s.vol_lfo = nullptr;
        s.pitch_lfo = nullptr;
        for (int i = 0; i < fmt->channels; ++i) s.channel_azimuth[i] = attr->azimuth[i];
        s.loop_end = -1;
        s.sample_rate = fmt->rate;
        s.frames = 0;
        s.sample_rep = fmt->codec;
        s.render = uint16_t(mode);
        s.channels = fmt->channels;
        s.detune_linear = 0;
        iSNDcalcpitch(v);
        iSNDcalcvol(v);
        SNDI_calcfxlevel(0, v);
        const int32_t r = SNDPLATFORM_packetplay(player, v, opts->time_mult, opts->distort, opts->lowpass,
                                                 opts->highpass, fmt, attr->stretch);
        if (r >= 0) break;
        for (int i = 0; i < chans; ++i) SNDVOICEI_free(snd_state().voices[v].channels_v[i]);
        p->voice = r;
    }
    return p->voice;
}

// [driving] 0x00311d10 SNDPKTPLAY_stop
int32_t SNDPKTPLAY_stop(int32_t player) {
    SndPacketPlayer* p = pl(player);
    SNDstop(p->voice);
    SNDPKTPLAYI_flushcallbackdata();
    // (SNDPKTPLAY_purge: -15, nada)
    const uint8_t n = p->fmt.channels;
    p->voice = -1;
    for (int i = 0; i < n; ++i)
        if (p->stretch[i] != 0) SNDMEMI_free(nf::p32_ptr<uint8_t>(p->stretch[i]));
    return 0;
}

// [driving] 0x003115e8 SNDPKTPLAYI_gethighchannel(int, int): o canal da voz alocada por último (o maior índice).
int32_t SNDPKTPLAYI_gethighchannel(int32_t v, int32_t n) {
    int16_t best = -1;
    int32_t r = -1;
    for (int i = 0; i < n; ++i) {
        const int16_t c = snd_state().voices[v].channels_v[i];
        if (best < c) {
            best = c;
            r = i;
        }
    }
    return r;
}

// [driving] 0x00311e08 SNDPKTPLAYI_get
const int16_t* SNDPKTPLAYI_get(int32_t player, int32_t chan, int32_t* frames, int32_t* cont) {
    SndPacketPlayer* p = pl(player);
    if (p->queued[chan] != 0) {
        uint16_t& rd = p->rd[chan];
        SndPacket& k = packet(p, rd);
        *frames = int32_t(k.frames << 1) >> 1;
        *cont = int32_t(k.frames) >> 31;
        if (chan == p->high) {
            if (rd != p->release_rd) {
                if (p->release_cb != 0) push_callback(1, player, nf::p32_u(nf::raw(packet(p, p->release_rd).data[0])));
                p->release_rd = uint16_t(p->release_rd + 1);
                if (int16_t(p->slots) <= int16_t(p->release_rd)) p->release_rd = 0;
            }
            const int32_t f = int32_t(k.frames << 1) >> 1;
            p->frames_queued -= f;
            p->frames_out += f;
        }
        rd = uint16_t(rd + 1);
        if (int16_t(p->slots) <= int16_t(rd)) rd = 0;
        p->queued[chan] = int16_t(p->queued[chan] - 1);
        if (p->queued[chan] == 0 && chan == p->high) p->active = 0;
        const uint8_t* d = nf::raw(k.data[chan]);
        if (d == nullptr) guard();  // o original devolveria 0xffffffff
        return reinterpret_cast<const int16_t*>(d);
    }
    if (chan == p->high && p->active == 0 && p->rd[chan] != p->release_rd) {
        if (p->release_cb != 0) push_callback(1, player, nf::p32_u(nf::raw(packet(p, p->release_rd).data[0])));
        p->release_rd = uint16_t(p->release_rd + 1);
        if (int16_t(p->slots) <= int16_t(p->release_rd)) p->release_rd = 0;
    }
    return nullptr;
}

// [driving] 0x00312068 SNDPKTPLAYI_freeframes
void SNDPKTPLAYI_freeframes(int32_t player, int32_t chan, int32_t frames) {
    SndPacketPlayer* p = pl(player);
    if (chan != p->high) return;
    p->frames_out -= frames;
    if (p->frames_cb != 0) push_callback(0, player, uint32_t(frames));
}

// [driving] 0x003120d8 SNDPKTPLAYI_flushcallbackdata
void SNDPKTPLAYI_flushcallbackdata() {
    for (int i = 0; i < g.cb_count; ++i) {
        const StreamGlobals::Callback& c = g.cb[i];
        SndPacketPlayer* p = pl(uint16_t(c.player));
        if (c.type == 0) SNDSTRMI_framescallback(uint16_t(c.player), c.data, nf::raw(p->context));
        else SNDSTRMI_releasecallback(c.data);
    }
    g.cb_count = 0;
}

// [driving] 0x003192b8 SNDPKTPLAYI_voicetopackethandle
int32_t SNDPKTPLAYI_voicetopackethandle(int32_t voice) {
    for (int i = 0; i < max_streams(); ++i)
        if (g.players[i] != nullptr && SNDVOICEI_get(pl(i)->voice) == voice) return i;
    return -1;
}

// [driving] 0x00319030 SNDDRV_getmastervoice
int32_t SNDDRV_getmastervoice(int32_t mv) {
    const SndState& s = snd_state();
    const int32_t v = mv + s.opts[sndopt::kSpuVoices] + s.opts[sndopt::kIopVoices];
    const int32_t m = s.voices[v].master;
    return m == -1 ? v : m;
}

// [driving] 0x00319068 SNDDRV_getsamplechan
int32_t SNDDRV_getsamplechan(int32_t mv) {
    const SndState& s = snd_state();
    const int32_t v = mv + s.opts[sndopt::kSpuVoices] + s.opts[sndopt::kIopVoices];
    const int16_t m = s.voices[v].master;
    if (m == -1) return 0;
    for (int i = 1; i < 6; ++i)
        if (s.voices[m].channels_v[i] == v) return i;
    return 0;
}

// ---- a API ----

// [driving] 0x003099d0 SNDSTRM_overheadtap
int32_t SNDSTRM_overheadtap(int32_t requests, int32_t packets) {
    return requests * 0x28 + 0x138 + SNDPKTPLAY_overhead(packets);
}

// [driving] 0x00309a08 SNDSTRM_overhead
int32_t SNDSTRM_overhead(int32_t requests, int32_t packets) {
    return SNDSTRM_overheadtap(requests, packets) + STREAM_overhead(requests + 2, 1, 1);
}

// [driving] 0x00309318 SNDSTRMI_create (pelo SNDSTRM_create 0x00309700; tap = 0)
int32_t SNDSTRM_create(const SndPlayOpts* opts, int32_t requests, int32_t packets, uint8_t* mem, int32_t size) {
    if (max_streams() == 0) return -9;
    int32_t idx = 0;
    while (g.table[idx] != nullptr) {
        ++idx;
        if (max_streams() <= idx) return -9;
    }
    SndStreamChannel* c = reinterpret_cast<SndStreamChannel*>(mem);
    std::memset(static_cast<void*>(c), 0, 0x138);
    const int32_t ov = SNDPKTPLAY_overhead(packets);
    SNDLINKI_init(&c->active);
    SNDLINKI_init(&c->free_list);
    SndStreamRequest* reqs = reinterpret_cast<SndStreamRequest*>(mem + 0x138);
    for (int i = 0; i < requests; ++i) SNDLINKI_push(&c->free_list, &reqs[i]);
    uint8_t* pmem = mem + 0x138 + requests * 0x28;
    c->player = SNDPKTPLAY_create(0x00308978u, 0x003089a0u, c, pmem, SNDPKTPLAY_overhead(packets));
    if (c->player < 0) return c->player;
    const uint32_t st = STREAM_create(requests + 2, 1, 1, pmem + ov, (size - 0x138 - requests * 0x28) - ov);
    if (st == 0) {
        SNDPKTPLAY_destroy(c->player);
        return -9;
    }
    c->tap = 0;
    c->stream = st;
    SNDSTRM_setgreedylevel(int32_t(c->stream), STREAM_buffersize(st) / 3);  // (o original passa o STREAM como índice)
    c->gen = 0;
    c->voice = -1;
    c->opts = *opts;
    if (SNDSTRMI_numcreated() == 0) {
        iSNDserveraddclient(SNDSTRMI_service);
        snd_state().exit_hooks[6] = SNDSTRMI_destroyall;
    }
    c->attr_new.stretch[0] = nullptr;
    g.table[idx] = c;
    SNDSTRM_purge(idx);
    return idx;
}

// [driving] 0x00309720 SNDSTRM_destroy
int32_t SNDSTRM_destroy(int32_t stream) {
    SndStreamChannel* c = SNDSTRMI_getstreamptr(stream);
    if (c == nullptr) return -8;
    SNDSTRM_purge(stream);
    if (SNDSTRMI_numcreated() == 1) {
        iSNDserverremoveclient(SNDSTRMI_service);
        snd_state().exit_hooks[6] = nullptr;
    }
    SNDPKTPLAY_destroy(c->player);
    const uint8_t tap = c->tap;
    const uint32_t st = c->stream;
    g.table[stream] = nullptr;
    if (tap == 0) STREAM_destroy(st);
    return 0;
}

// [driving] 0x003097c0 SNDSTRM_queuefile
int32_t SNDSTRM_queuefile(int32_t stream, int32_t hold, const char* name, int32_t offset) {
    return SNDSTRMI_queue(stream, hold, name, uint32_t(offset), 0);
}

// [driving] 0x003097e0 SNDSTRM_purge
int32_t SNDSTRM_purge(int32_t stream) {
    SNDSYS_entercritical();
    SndStreamChannel* c = SNDSTRMI_getstreamptr(stream);
    if (c == nullptr) {
        SNDSYS_leavecritical();
        return -8;
    }
    if (c->voice > -1) SNDPKTPLAY_stop(c->player);
    c->voice = -1;
    if (c->tap == 0) STREAM_kill(c->stream);
    if (c->attr_new.stretch[0] != nullptr && c->fmt_new.channels != 0)
        for (int i = 0; i < c->fmt_new.channels; ++i) SNDMEMI_free(nf::raw(c->attr_new.stretch[i]));
    while (SndStreamRequest* r = SNDLINKI_pop(&c->active)) SNDLINKI_push(&c->free_list, r);
    c->state = 0;
    c->header_req = nullptr;
    std::memset(&c->fmt, 0, 4);
    std::memset(&c->fmt_new, 0, 4);
    std::memset(&c->attr, 0, 0x68);
    std::memset(&c->attr_new, 0, 0x68);
    SNDSYS_leavecritical();
    return 0;
}

// [driving] 0x00309a50 SNDSTRM_status
int32_t SNDSTRM_status(int32_t stream, SndStreamStatus* st) {
    st->buffered_ms = 0;
    st->current = 0;
    st->requests = 0;
    SndStreamChannel* c = SNDSTRMI_getstreamptr(stream);
    if (c == nullptr) return -8;
    st->requests = c->active.count;
    if (st->requests == 0) return 0;
    st->current = nf::raw(c->active.head)->id;
    if (c->fmt.rate == 0) return 0;
    int32_t b = (SNDPKTPLAY_framesoutstanding(c->player) * 1000) / int32_t(uint32_t(c->fmt.rate));
    st->buffered_ms = b;
    if (b == 0) {
        SndStreamRequest* r = SNDSTRMI_getrequestptr(st->current);
        if (r->rate == 0) return 0;
        uint32_t t = uint32_t(STREAM_gettable(c->stream));
        if (4000000u < t) t = 4000000u;
        st->buffered_ms = int32_t((t * 1000u) / r->rate);
    }
    return 0;
}

// [driving] 0x00309b48 SNDSTRM_requeststatus
int32_t SNDSTRM_requeststatus(int32_t id, SndRequestStatus* st) {
    st->state = 0;
    st->played_ms = 0;
    st->left_ms = 0;
    st->outstanding_ms = 0;
    SndStreamChannel* c;
    if (id < 0 || (c = SNDSTRMI_getstreamptr(id & 0xff)) == nullptr) return -8;
    SndStreamRequest* r = SNDSTRMI_getrequestptr(id);
    if (r == nullptr) {
        st->state = 3;
    } else if (r->started == 0) {
        st->state = 0;
    } else {
        uint16_t rate;
        if (nf::raw(c->active.head) == r) {
            st->state = 2;
            rate = c->fmt.rate;
        } else {
            st->state = 1;
            rate = c->fmt_new.rate;
        }
        // iSNDmulu64 / iSNDdivu64
        st->played_ms = int32_t(uint32_t((uint64_t(r->played) * 1000u) / (rate != 0 ? rate : 1)));
        st->left_ms = int32_t(uint32_t((uint64_t(r->total - r->played) * 1000u) / (rate != 0 ? rate : 1)));
        if (rate == 0) guard();  // (o original para no "break 7")
        st->outstanding_ms = rate != 0 ? int32_t((r->outstanding * 1000u) / rate) : 0;
    }
    return 0;
}

// [driving] 0x00309c88 SNDSTRM_modifyhold
int32_t SNDSTRM_modifyhold(int32_t id, int16_t hold) {
    SNDSYS_entercritical();
    SndStreamRequest* r = SNDSTRMI_getrequestptr(id);
    int32_t result = -8;
    if (r != nullptr) {
        result = 0;
        r->hold = hold;
    }
    SNDSYS_leavecritical();
    return result;
}

// [driving] 0x00309d50 SNDSTRM_vol
int32_t SNDSTRM_vol(int32_t stream, int32_t vol) {
    SndStreamChannel* c = SNDSTRMI_getstreamptr(stream);
    if (c == nullptr) return -8;
    c->opts.vol = int8_t(vol);
    SNDvol(c->voice, vol);
    return 0;
}

// [driving] 0x00309d98 SNDSTRM_autovol
int32_t SNDSTRM_autovol(int32_t stream, int32_t ms, int32_t vol) {
    SndStreamChannel* c = SNDSTRMI_getstreamptr(stream);
    if (c == nullptr) return -8;
    c->opts.vol = int8_t(vol);
    SNDautovol(c->voice, ms / 10, vol);
    return 0;
}

// [driving] 0x00309e00 SNDSTRM_3dpos
int32_t SNDSTRM_3dpos(int32_t stream, int32_t azimuth, int32_t elevation) {
    SndStreamChannel* c = SNDSTRMI_getstreamptr(stream);
    if (c == nullptr) return -8;
    c->opts.azimuth = uint16_t(azimuth);
    c->opts.elevation = uint16_t(elevation);
    SND3dpos(c->voice, azimuth, elevation);
    return 0;
}

// [driving] 0x00309e60 SNDSTRM_pitchmult
int32_t SNDSTRM_pitchmult(int32_t stream, int32_t mult) {
    SndStreamChannel* c = SNDSTRMI_getstreamptr(stream);
    if (c == nullptr) return -8;
    c->opts.pitch_mult = uint16_t(mult);
    SNDpitchmult(c->voice, mult);
    return 0;
}

// [driving] 0x00309ea8 SNDSTRM_lowpass
int32_t SNDSTRM_lowpass(int32_t stream, int32_t cutoff) {
    SndStreamChannel* c = SNDSTRMI_getstreamptr(stream);
    if (c == nullptr) return -8;
    c->opts.lowpass = uint16_t(cutoff);
    SNDCTRL_lowpass(c->voice, cutoff);
    return 0;
}

// [driving] 0x00309ef0 SNDSTRM_setgreedylevel
int32_t SNDSTRM_setgreedylevel(int32_t stream, int32_t level) {
    SndStreamChannel* c = SNDSTRMI_getstreamptr(stream);
    if (c == nullptr) return -8;
    STREAM_setgreedylevel(c->stream, level);
    return 0;
}

// [driving] 0x00309088 SNDSTRMI_service
void SNDSTRMI_service() {
    SNDSYS_entercritical();
    for (int i = 0; i < max_streams(); ++i) {
        SndStreamChannel* c = ch_ptr(i);
        if (c == nullptr || c->active.count == 0) continue;
        if (c->state == 2) {
            if (0 < SNDPKTPLAY_framesoutstanding(c->player)) continue;
            c->fmt = c->fmt_new;
            std::memcpy(&c->attr, &c->attr_new, 0x68);
            c->attr_new.stretch[0] = nullptr;
            SNDPKTPLAY_stop(c->player);
            SNDSTRMI_startstream(c);
        }
        if (SNDSTRMI_isheld(c) != 0) continue;
        int32_t budget = 10;
        int32_t more = 0;
        if (c->state == 1) {
            budget = SNDPKTPLAY_submitspace(c->player);
            if (budget == 0) continue;
        }
        while (true) {
            --budget;
            uint8_t* chunk = STREAM_get(c->stream);
            if (chunk != nullptr) more = SNDSTRMI_parsechunk(i, chunk);
            if (more == 0 || budget < 1) break;
        }
    }
    SNDSYS_leavecritical();
}

void snd_stream_power_on() { g = StreamGlobals{}; }

void snd_test_patchtohdr(uint32_t base, const uint8_t* patch, SndSampleFormat* fmt, SndSampleAttr* attr,
                         uint32_t* info) {
    SNDI_patchtohdr(base, patch, fmt, attr, info);
}

SndStreamChannel* snd_stream_channel(int32_t stream) {
    return stream >= 0 && stream < 0x20 ? nf::raw(g.table[stream]) : nullptr;
}
SndPacketPlayer* snd_packet_player(int32_t player) {
    return player >= 0 && player < 0x20 ? nf::raw(g.players[player]) : nullptr;
}
int32_t snd_callback_queue_count() { return g.cb_count; }
uint64_t snd_stream_guards() { return g.guards; }

}  // namespace drv
