// Os filmes do PS2 no PC, a parte sem decodificador: ver pss.h.

#include "platform/pss.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nf {

namespace {

uint64_t read_pts(const uint8_t* b) {
    return (uint64_t((b[0] >> 1) & 7) << 30) | (uint64_t(b[1]) << 22) | (uint64_t(b[2] >> 1) << 15) |
           (uint64_t(b[3]) << 7) | uint64_t(b[4] >> 1);
}

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

// Tira o que já foi lido do começo de um vetor quando passa de um limite (os buffers andam como filas).
template <typename T>
void compact(std::vector<T>& v, size_t& read, size_t limit) {
    if (read < limit) return;
    v.erase(v.begin(), v.begin() + std::ptrdiff_t(read));
    read = 0;
}

}  // namespace

// ---- o program stream ----

PssStep pss_next(const uint8_t* d, size_t n, size_t* used, PssPes* out) {
    *used = 0;
    if (n < 4) return PssStep::NeedMore;
    if (d[0] != 0 || d[1] != 0 || d[2] != 1) return PssStep::Error;
    const uint8_t code = d[3];
    if (code == 0xba) {
        if (n < 5) return PssStep::NeedMore;
        size_t size;
        if ((d[4] >> 6) == 1) {  // MPEG-2: 10 bytes e o enchimento
            if (n < 14) return PssStep::NeedMore;
            size = 14 + (d[13] & 7);
        } else {  // MPEG-1
            size = 12;
        }
        if (n < size) return PssStep::NeedMore;
        *used = size;
        return PssStep::Other;
    }
    if (code == 0xb9) {
        *used = 4;
        return PssStep::End;
    }
    if (code < 0xbb) return PssStep::Error;  // um código de vídeo solto não é um item do program stream
    if (n < 6) return PssStep::NeedMore;
    const size_t len = (size_t(d[4]) << 8) | d[5];
    if (n < 6 + len) return PssStep::NeedMore;
    *used = 6 + len;
    if (!(code == 0xbd || (code >= 0xc0 && code <= 0xef))) return PssStep::Other;  // sistema, enchimento, 0xbf...
    const uint8_t* body = d + 6;
    PssPes pes;
    pes.stream = code;
    size_t head;
    if (len >= 3 && (body[0] >> 6) == 2) {  // cabeçalho PES do MPEG-2
        const uint8_t flags = body[1];
        head = 3 + size_t(body[2]);
        if (head > len) return PssStep::Other;
        if ((flags & 0x80) && body[2] >= 5) {
            pes.has_pts = true;
            pes.pts = read_pts(body + 3);
        }
    } else {  // MPEG-1: enchimento, STD, PTS/DTS
        head = 0;
        while (head < len && body[head] == 0xff) ++head;
        if (head < len && (body[head] >> 6) == 1) head += 2;
        if (head < len && (body[head] >> 4) == 2) {
            if (head + 5 > len) return PssStep::Other;
            pes.has_pts = true;
            pes.pts = read_pts(body + head);
            head += 5;
        } else if (head < len && (body[head] >> 4) == 3) {
            if (head + 10 > len) return PssStep::Other;
            pes.has_pts = true;
            pes.pts = read_pts(body + head);
            head += 10;
        } else {
            ++head;
        }
        if (head > len) return PssStep::Other;
    }
    pes.data = body + head;
    pes.size = len - head;
    *out = pes;
    return PssStep::Pes;
}

// ---- o áudio ----

void PssAudio::reset() { *this = PssAudio{}; }

void PssAudio::push_pes(const uint8_t* payload, size_t size) {
    // 4 bytes: 0xff, o sub-stream (0xa0 = o primeiro stream de PCM), 0, 0.
    if (size < 4 || payload[1] != 0xa0) return;
    push_bytes(payload + 4, size - 4);
}

void PssAudio::push_bytes(const uint8_t* data, size_t size) {
    if (error_ || size == 0) return;
    compact(bytes_, bytes_read_, 1u << 16);
    bytes_.insert(bytes_.end(), data, data + size);
    parse();
}

void PssAudio::parse() {
    if (!header_ok_) {
        if (bytes_.size() - bytes_read_ < kHeaderBytes) return;
        const uint8_t* h = bytes_.data() + bytes_read_;
        const uint32_t hsize = le32(h + 4);
        if (std::memcmp(h, "SShd", 4) != 0 || hsize != 0x18 || std::memcmp(h + 0x20, "SSbd", 4) != 0) {
            error_ = true;
            return;
        }
        type_ = int32_t(le32(h + 8));
        rate_ = int32_t(le32(h + 12));
        channels_ = int32_t(le32(h + 16));
        interleave_ = int32_t(le32(h + 20));
        body_size_ = le32(h + 0x24);
        // Só o PCM de 16 bits (tipo 1) em 1 ou 2 canais e blocos pares; o tipo 0x10 (PS-ADPCM) não ocorre no disco.
        if (type_ != 1 || channels_ < 1 || channels_ > 2 || interleave_ < 2 || (interleave_ & 1) != 0 || rate_ <= 0) {
            error_ = true;
            return;
        }
        header_ok_ = true;
        bytes_read_ += kHeaderBytes;
    }
    const size_t group = size_t(interleave_) * size_t(channels_);
    const size_t per = size_t(interleave_) / 2;
    compact(pcm_, pcm_read_, 1u << 17);
    while (bytes_.size() - bytes_read_ >= group && size_t(body_used_) + group <= body_size_) {
        const uint8_t* g = bytes_.data() + bytes_read_;
        const size_t base = pcm_.size();
        pcm_.resize(base + per * 2);
        for (size_t k = 0; k < per; ++k) {
            for (int c = 0; c < 2; ++c) {
                const uint8_t* b = g + size_t(channels_ == 2 ? c : 0) * size_t(interleave_) + 2 * k;
                pcm_[base + 2 * k + size_t(c)] = int16_t(uint16_t(b[0] | (b[1] << 8)));
            }
        }
        bytes_read_ += group;
        body_used_ += uint32_t(group);
        decoded_ += per;
    }
}

size_t PssAudio::take(int16_t* out, size_t max_frames) {
    const size_t n = std::min(max_frames, frames_ready());
    if (n == 0) return 0;
    std::memcpy(out, pcm_.data() + pcm_read_, n * 2 * sizeof(int16_t));
    pcm_read_ += n * 2;
    return n;
}

// ---- o vídeo elementar ----

void Mpeg2Units::reset() { *this = Mpeg2Units{}; }

void Mpeg2Units::push(const uint8_t* data, size_t size) {
    if (finished_ || size == 0) return;
    // O que já virou unidade sai do começo (os índices andam junto).
    if (unit_start_ > (1u << 20)) {
        buf_.erase(buf_.begin(), buf_.begin() + std::ptrdiff_t(unit_start_));
        scanned_ -= unit_start_;
        unit_start_ = 0;
    }
    buf_.insert(buf_.end(), data, data + size);
    scan();
}

void Mpeg2Units::scan() {
    const size_t n = buf_.size();
    size_t i = scanned_;
    while (i + 4 <= n) {
        const uint8_t* p = buf_.data() + i;
        if (p[2] > 1) {
            i += 3;
            continue;
        }
        if (p[0] != 0 || p[1] != 0 || p[2] != 1) {
            ++i;
            continue;
        }
        const uint8_t c = p[3];
        if (c == 0xb3) {
            if (i + 12 > n) break;  // o cabeçalho ainda não chegou inteiro
            if (width_ == 0) {
                width_ = (p[4] << 4) | (p[5] >> 4);
                height_ = ((p[5] & 0xf) << 8) | p[6];
                aspect_ = p[7] >> 4;
                rate_code_ = p[7] & 0xf;
            }
        }
        if (c == 0xb3 || c == 0xb8 || c == 0x00) {
            if (in_picture_) {
                units_.emplace_back(buf_.begin() + std::ptrdiff_t(unit_start_), buf_.begin() + std::ptrdiff_t(i));
                unit_start_ = i;
                in_picture_ = false;
            }
            if (c == 0x00) {
                in_picture_ = true;
                ++pictures_;
            }
        }
        i += 4;
    }
    scanned_ = i;
}

void Mpeg2Units::finish() {
    if (finished_) return;
    scanned_ = buf_.size();
    if (unit_start_ < buf_.size() && in_picture_) {
        units_.emplace_back(buf_.begin() + std::ptrdiff_t(unit_start_), buf_.end());
    }
    unit_start_ = buf_.size();
    in_picture_ = false;
    finished_ = true;
}

bool Mpeg2Units::next(std::vector<uint8_t>& unit) {
    if (units_.empty()) return false;
    unit = std::move(units_.front());
    units_.pop_front();
    return true;
}

double Mpeg2Units::fps() const {
    switch (rate_code_) {
        case 1: return 24000.0 / 1001.0;
        case 2: return 24.0;
        case 3: return 25.0;
        case 4: return 30000.0 / 1001.0;
        case 5: return 30.0;
        case 6: return 50.0;
        case 7: return 60000.0 / 1001.0;
        case 8: return 60.0;
        default: return 0.0;
    }
}

// ---- o arquivo ----

PssFile::~PssFile() { close(); }

bool PssFile::open(DiscFile file) {
    close();
    file_ = std::move(file);
    return file_.is_open();
}

bool PssFile::open(const std::wstring& path) { return open(DiscFile::open_host(path)); }

void PssFile::close() {
    file_.close();
    buf_.clear();
    pos_ = 0;
    eof_ = false;
    error_ = false;
    bytes_read_ = 0;
    audio_pes_ = 0;
    audio_.reset();
    video_.reset();
}

bool PssFile::rewind() {
    if (!file_.is_open()) return false;
    file_.seek(0);
    buf_.clear();
    pos_ = 0;
    eof_ = false;
    error_ = false;
    bytes_read_ = 0;
    audio_pes_ = 0;
    audio_.reset();
    video_.reset();
    return true;
}

bool PssFile::read_more() {
    if (!file_.is_open()) return false;
    compact(buf_, pos_, 1u << 18);
    constexpr size_t kChunk = 0x10000;
    const size_t base = buf_.size();
    buf_.resize(base + kChunk);
    const size_t got = file_.read(buf_.data() + base, kChunk);
    buf_.resize(base + got);
    bytes_read_ += got;
    return got != 0;
}

bool PssFile::pump(size_t audio_frames, size_t video_units, size_t max_bytes) {
    if (!file_.is_open()) return false;
    const uint64_t start = bytes_read_;
    while (!eof_ && (audio_.frames_ready() < audio_frames || video_.units_ready() < video_units)) {
        if (bytes_read_ - start >= max_bytes && pos_ >= buf_.size()) break;
        size_t used = 0;
        PssPes pes;
        const PssStep step = pss_next(buf_.data() + pos_, buf_.size() - pos_, &used, &pes);
        if (step == PssStep::NeedMore) {
            if (!read_more()) {
                eof_ = true;
                video_.finish();
            }
            continue;
        }
        if (step == PssStep::Error) {
            error_ = true;
            eof_ = true;
            video_.finish();
            return false;
        }
        pos_ += used;
        if (step == PssStep::End) {
            eof_ = true;
            video_.finish();
        } else if (step == PssStep::Pes) {
            if (pes.stream == 0xe0) {
                video_.push(pes.data, pes.size);
            } else if (pes.stream == 0xbd) {
                ++audio_pes_;
                audio_.push_pes(pes.data, pes.size);
            }
        }
    }
    return true;
}

// ---- o relógio ----

int64_t movie_audio_heard(int64_t consumed_frames, int64_t queued_frames) {
    return std::max<int64_t>(0, consumed_frames - std::max<int64_t>(0, queued_frames));
}

int64_t movie_frame_at_audio(int64_t audio_frames, int rate, double fps) {
    if (audio_frames <= 0 || rate <= 0 || fps <= 0.0) return 0;
    // Em inteiros quando a taxa de quadros é inteira (30: um quadro a cada 1600 amostras exatas).
    const double rounded = std::round(fps);
    if (std::fabs(fps - rounded) < 1e-9) return audio_frames * int64_t(rounded) / rate;
    return int64_t(std::floor(double(audio_frames) * fps / double(rate)));
}

int64_t movie_frame_at_vblanks(double vblanks, double fps) {
    if (vblanks <= 0.0 || fps <= 0.0) return 0;
    return int64_t(std::floor(vblanks * fps / 60.0 + 1e-9));
}

int64_t MovieClock::position(int64_t heard_audio, int64_t heard_spu) const {
    if (!has_audio) return std::max<int64_t>(0, heard_spu - spu_start);
    if (!audio_done) return std::max<int64_t>(0, heard_audio);
    return audio_end + std::max<int64_t>(0, heard_spu - spu_audio_end);
}

}  // namespace nf
