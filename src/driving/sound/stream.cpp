// O STREAM e o FILESYS do PC (driving/sound/stream.h): só do port, no lugar do stream.o do librealfilez (a leitura do
// DVD em anel) e do FILESYS do PS2.

#include "driving/sound/stream.h"

#include <cstring>
#include <map>

#include "driving/bigf.h"
#include "platform/crt_compat.h"
#include "platform/ptr32.h"

namespace drv {

// ---- o modelo ------------------------------------------------------------------------------------------------------

uint32_t StreamModel::create(int32_t requests, int32_t taps, int32_t filters, uint32_t mem, int32_t size) {
    const int32_t room = size - overhead(requests, taps, filters);
    if (room < 0x1800) return 0;
    if (!(1 < requests && requests < 0x101)) return 0;
    if (0xf < uint32_t(taps - 1)) return 0;
    if (filters <= 0 || taps < filters) return 0;
    const uint32_t tables = mem + 0x180 + uint32_t(requests) * 0x124;
    const uint32_t start = ((tables + uint32_t(taps) * 0xc + uint32_t(filters) * 0x10) & ~0x3fu) + 0x40;
    capacity_ = int32_t((mem + uint32_t(size)) - start);
    handle_ = tables + uint32_t(taps) * 0xc;  // o que o original devolve (a tabela dos "taps")
    max_requests_ = requests;
    greedy_ = 0;
    usage_ = avail_ = state_ = 0;
    reqs_.clear();
    chunks_.clear();
    grave_.clear();
    loaded_total_ = 0;
    return handle_;
}

void StreamModel::destroy() {
    kill();
    grave_.clear();
    handle_ = 0;
}

uint32_t StreamModel::queuefile(const char* name, int32_t offset, uint32_t endtag) {
    if (int32_t(reqs_.size()) >= max_requests_) return 0;  // sem pedido livre (o FUN_00304f38 do original)
    auto r = std::make_unique<Request>();
    if (!load_ || !load_(name, r->data)) {
        ++missing_;
        return 0;
    }
    ++files_read_;
    r->id = next_id_++;
    r->endtag = endtag;
    r->next = offset > 0 ? size_t(offset) : 0;
    r->addr = place_ ? place_(r->data) : 0;
    if (r->next >= r->data.size()) r->loaded = true;
    reqs_.push_back(std::move(r));
    fill();
    return reqs_.back() ? reqs_.back()->id : 0;
}

// Os pedaços entram na ordem enquanto cabem (o DVD instantâneo).
void StreamModel::fill() {
    for (auto& rp : reqs_) {
        Request& r = *rp;
        while (!r.loaded) {
            const size_t off = r.next;
            if (off + 8 > r.data.size()) {
                r.loaded = true;
                break;
            }
            uint32_t tag, size;
            std::memcpy(&tag, r.data.data() + off, 4);
            std::memcpy(&size, r.data.data() + off + 4, 4);
            size &= 0xffffff;
            if (size < 8 || off + size > r.data.size()) {
                r.loaded = true;
                break;
            }
            if (usage_ + int32_t(size) > capacity_) {
                state_ = 2;
                return;
            }
            Chunk c;
            c.req = &r;
            c.addr = r.addr + uint32_t(off);
            c.size = int32_t(size);
            chunks_.push_back(c);
            loaded_total_ += size;
            while (!grave_.empty() && grave_.front().first < loaded_total_) grave_.pop_front();
            usage_ += int32_t(size);
            avail_ += int32_t(size);
            r.next = off + size;
            if (tag == r.endtag || r.next >= r.data.size()) r.loaded = true;
        }
    }
    state_ = 0;
}

uint32_t StreamModel::get() {
    for (Chunk& c : chunks_) {
        if (c.got) continue;
        c.got = true;
        avail_ -= c.size;
        return c.addr;
    }
    return 0;
}

// Os pedaços liberados saem da frente; um pedido cujos pedaços entraram e saíram todos sai também.
void StreamModel::trim() {
    while (!chunks_.empty() && chunks_.front().released) chunks_.pop_front();
    while (!reqs_.empty()) {
        Request* r = reqs_.front().get();
        if (!r->loaded) break;
        bool used = false;
        for (const Chunk& c : chunks_)
            if (c.req == r) {
                used = true;
                break;
            }
        if (used) break;
        grave_.emplace_back(loaded_total_ + uint64_t(capacity_), std::move(reqs_.front()));
        reqs_.pop_front();
    }
}

void StreamModel::release(uint32_t chunk) {
    for (Chunk& c : chunks_) {
        if (c.addr != chunk || c.released) continue;
        if (!c.got) avail_ -= c.size;
        c.released = true;
        usage_ -= c.size;
        trim();
        fill();
        return;
    }
}

void StreamModel::kill() {
    chunks_.clear();
    for (auto& r : reqs_) grave_.emplace_back(loaded_total_ + uint64_t(capacity_), std::move(r));
    reqs_.clear();
    usage_ = avail_ = 0;
    state_ = 0;
}

// ---- o FILESYS -----------------------------------------------------------------------------------------------------

namespace {

struct BigReg {
    int32_t id = 0;
    std::string path;
    std::unique_ptr<Bigf> big;
};
struct FsState {
    BigOpener open;
    std::vector<BigReg> bigs;
    int32_t next_id = 1;
};
FsState& fs() {
    static FsState s;
    return s;
}

std::string lower_bs(const std::string& in) {
    std::string r = in;
    for (char& ch : r) {
        if (ch == '/') ch = '\\';
        else if (ch >= 'A' && ch <= 'Z') ch = char(ch + 32);
    }
    return r;
}

}  // namespace

void filesys_set_opener(BigOpener open) { fs().open = std::move(open); }

void filesys_reset() {
    fs().bigs.clear();
    fs().next_id = 1;
}

int32_t FILESYS_existssync(const char* path) {
    if (!fs().open) return 0;
    return fs().open(path) != nullptr ? 1 : 0;
}

int32_t FILESYS_addbigsync(const char* path, int32_t* id) {
    if (!fs().open) return 0;
    BigReg r;
    r.big = fs().open(path);
    if (!r.big) return 0;
    r.id = fs().next_id++;
    r.path = path;
    if (id != nullptr) *id = r.id;
    fs().bigs.push_back(std::move(r));
    return 1;
}

void FILESYS_delbigsync(int32_t id) {
    auto& v = fs().bigs;
    for (auto it = v.begin(); it != v.end(); ++it)
        if (it->id == id) {
            v.erase(it);
            return;
        }
}

bool filesys_find(const std::string& name, std::vector<uint8_t>* out) {
    std::string key = name;
    if (!key.empty() && key[0] == '|') key = key.substr(1);
    key = lower_bs(key);
    for (const BigReg& r : fs().bigs) {
        const auto& es = r.big->entries();
        for (int i = 0; i < int(es.size()); ++i) {
            const std::string n = lower_bs(es[size_t(i)].name);
            const size_t bs = n.rfind('\\');
            if (n == key || (bs != std::string::npos && n.substr(bs + 1) == key)) {
                if (out != nullptr) r.big->read(i, *out);
                return true;
            }
        }
    }
    return false;
}

// ---- a API do porte ------------------------------------------------------------------------------------------------

namespace {

std::map<uint32_t, std::unique_ptr<StreamModel>>& models() {
    static std::map<uint32_t, std::unique_ptr<StreamModel>> m;
    return m;
}
StreamModel::Loader& loader_override() {
    static StreamModel::Loader l;
    return l;
}

}  // namespace

int32_t STREAM_overhead(int32_t requests, int32_t taps, int32_t filters) {
    return StreamModel::overhead(requests, taps, filters);
}

uint32_t STREAM_create(int32_t requests, int32_t taps, int32_t filters, uint8_t* mem, int32_t size) {
    StreamModel::Loader load = loader_override();
    if (!load) load = [](const std::string& name, std::vector<uint8_t>& out) { return filesys_find(name, &out); };
    auto m = std::make_unique<StreamModel>(
        load,
        [](std::vector<uint8_t>& data) { return nf::p32_u(data.data()); });
    const uint32_t h = m->create(requests, taps, filters, nf::p32_u(mem), size);
    if (h == 0) return 0;
    models()[h] = std::move(m);
    return h;
}

StreamModel* stream_model(uint32_t h) {
    auto it = models().find(h);
    return it == models().end() ? nullptr : it->second.get();
}

void STREAM_destroy(uint32_t h) {
    auto it = models().find(h);
    if (it == models().end()) return;
    it->second->destroy();
    models().erase(it);
}

int32_t STREAM_buffersize(uint32_t h) {
    StreamModel* m = stream_model(h);
    return m != nullptr ? m->buffersize() : 0;
}

void STREAM_setgreedylevel(uint32_t h, int32_t level) {
    if (StreamModel* m = stream_model(h)) m->setgreedylevel(level);
}

uint32_t STREAM_queuefile(uint32_t h, const char* name, int32_t offset, uint32_t endtag) {
    StreamModel* m = stream_model(h);
    return m != nullptr ? m->queuefile(name, offset, endtag) : 0;
}

void STREAM_kill(uint32_t h) {
    if (StreamModel* m = stream_model(h)) m->kill();
}

uint8_t* STREAM_get(uint32_t h) {
    StreamModel* m = stream_model(h);
    if (m == nullptr) return nullptr;
    const uint32_t a = m->get();
    return a != 0 ? nf::p32_ptr<uint8_t>(a) : nullptr;
}

void STREAM_release(uint32_t h, uint8_t* chunk) {
    if (StreamModel* m = stream_model(h)) m->release(nf::p32_u(chunk));
}

int32_t STREAM_gettable(uint32_t h) {
    StreamModel* m = stream_model(h);
    return m != nullptr ? m->gettable() : 0;
}

int32_t STREAM_state(uint32_t h) {
    StreamModel* m = stream_model(h);
    return m != nullptr ? m->state() : 0;
}

void stream_power_on() { models().clear(); }

void stream_set_loader(StreamModel::Loader load) { loader_override() = std::move(load); }

}  // namespace drv
