// CsfFile.cpp

#include "data/CsfFile.h"

#include <cctype>
#include <cstring>

namespace ra2 {
namespace {

std::string Lower(const char* s) {
    std::string o;
    if (s == nullptr) {
        return o;
    }
    for (; *s; ++s) {
        o.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*s))));
    }
    return o;
}

uint32_t Ru32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

bool CsfFile::Load(const uint8_t* data, size_t size) {
    entries_.clear();
    if (data == nullptr || size < 0x18) {
        return false;
    }
    // @0x734799：cmp [hdr], 0x43534620
    if (Ru32(data) != 0x43534620u) {
        return false;
    }
    const uint32_t num_labels = Ru32(data + 0x8);
    size_t off = 0x18;
    for (uint32_t li = 0; li < num_labels && off + 12 <= size; ++li) {
        // @0x734A0A：cmp id, 0x4C424C20
        if (Ru32(data + off) != 0x4C424C20u) {
            return false;
        }
        const uint32_t pairs = Ru32(data + off + 4);
        const uint32_t name_len = Ru32(data + off + 8);
        off += 12;
        if (off + name_len > size) {
            return false;
        }
        std::string name(reinterpret_cast<const char*>(data + off), name_len);
        off += name_len;
        std::string value;
        for (uint32_t p = 0; p < pairs && off + 8 <= size; ++p) {
            const uint32_t sid = Ru32(data + off);
            off += 4;
            // @0x734ADE：' RTS' / WRTS
            if (sid != 0x53545220u && sid != 0x53545257u) {
                return false;
            }
            const uint32_t nchars = Ru32(data + off);
            off += 4;
            const size_t nbytes = static_cast<size_t>(nchars) * 2u;
            if (off + nbytes > size) {
                return false;
            }
            // @0x734B35：not 解码 UTF-16LE → UTF-8
            value.clear();
            value.reserve(nchars * 3u);
            for (uint32_t i = 0; i < nchars; ++i) {
                const uint8_t lo = static_cast<uint8_t>(~data[off + i * 2u]);
                const uint8_t hi = static_cast<uint8_t>(~data[off + i * 2u + 1u]);
                const uint32_t cp = lo | (static_cast<uint32_t>(hi) << 8);
                if (cp == 0) {
                    break;
                }
                if (cp < 0x80u) {
                    value.push_back(static_cast<char>(cp));
                } else if (cp < 0x800u) {
                    value.push_back(static_cast<char>(0xC0u | (cp >> 6)));
                    value.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                } else {
                    value.push_back(static_cast<char>(0xE0u | (cp >> 12)));
                    value.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                    value.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                }
            }
            off += nbytes;
            if (sid == 0x53545257u) {
                if (off + 4 > size) {
                    return false;
                }
                const uint32_t extra = Ru32(data + off);
                off += 4;
                if (off + extra > size) {
                    return false;
                }
                off += extra;
            }
        }
        entries_[Lower(name.c_str())] = std::move(value);
    }
    return !entries_.empty();
}

std::string CsfFile::Get(const char* label) const {
    const auto it = entries_.find(Lower(label));
    if (it == entries_.end()) {
        return {};
    }
    return it->second;
}

}  // namespace ra2
