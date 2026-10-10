#pragma once

// Strict, bounded decoder for persistent Bedrock v8/v9 block palettes.
// It proves structural decoding, not membership in the engine's block registry.
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

namespace chunklet::database {
struct Invalid : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Reader {
public:
    explicit Reader(std::string_view bytes) : bytes_(bytes) {
        if (bytes.size() > 16 * 1024 * 1024) throw Invalid("subchunk exceeds 16 MiB");
    }
    std::size_t remaining() const { return bytes_.size() - position_; }
    std::string_view take(std::size_t count) {
        if (count > remaining()) throw Invalid("truncated payload");
        auto result = bytes_.substr(position_, count); position_ += count; return result;
    }
    std::uint8_t byte() { return static_cast<std::uint8_t>(take(1)[0]); }
    std::uint32_t integer(unsigned width) {
        auto bytes = take(width); std::uint32_t result{};
        for (unsigned i = 0; i < width; ++i)
            result |= std::uint32_t(static_cast<std::uint8_t>(bytes[i])) << (8 * i);
        return result;
    }
    std::string_view string() { return take(integer(2)); }
    std::uint32_t count() {
        const auto value = integer(4);
        if (value > 0x7fffffffU) throw Invalid("negative NBT count");
        return value;
    }
    void node(unsigned depth) {
        if (depth > 64 || ++nodes_ > 262144) throw Invalid("NBT complexity limit");
    }
private:
    std::string_view bytes_;
    std::size_t position_{};
    std::size_t nodes_{};
};

inline bool block_name(std::string_view name) {
    const auto colon = name.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 == name.size()) return false;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c == ':' && i == colon) continue;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '.' || c == '-' || c == '/')) return false;
    }
    return true;
}

inline void nbt_payload(Reader &reader, unsigned type, unsigned depth,
                        bool block = false, bool states = false) {
    reader.node(depth);
    switch (type) {
    case 1: reader.take(1); break;
    case 2: reader.take(2); break;
    case 3: case 5: reader.take(4); break;
    case 4: case 6: reader.take(8); break;
    case 8: reader.string(); break;
    case 7: case 11: case 12: {
        const auto count = reader.count();
        const unsigned width = type == 7 ? 1 : type == 11 ? 4 : 8;
        if (count > reader.remaining() / width) throw Invalid("truncated NBT array");
        reader.take(std::size_t(count) * width); break;
    }
    case 9: {
        const unsigned subtype = reader.byte();
        const auto count = reader.count();
        if (subtype > 12 || (subtype == 0 && count != 0) || count > 262144)
            throw Invalid("invalid NBT list");
        for (std::uint32_t i = 0; i < count; ++i) nbt_payload(reader, subtype, depth + 1);
        break;
    }
    case 10: {
        bool has_name{}, has_states{};
        std::unordered_set<std::string_view> names;
        for (;;) {
            const unsigned child_type = reader.byte();
            if (child_type == 0) break;
            const auto name = reader.string();
            if (!names.insert(name).second) throw Invalid("duplicate NBT compound field");
            if (states && child_type != 1 && child_type != 3 && child_type != 8)
                throw Invalid("unsupported block-state type");
            if (block && name == "name") {
                reader.node(depth + 1);
                if (child_type != 8 || !block_name(reader.string())) throw Invalid("invalid block name");
                has_name = true;
            } else if (block && name == "states") {
                if (child_type != 10) throw Invalid("block states must be a compound");
                nbt_payload(reader, child_type, depth + 1, false, true); has_states = true;
            } else {
                if (block && name == "version" && child_type != 3) throw Invalid("invalid block version type");
                nbt_payload(reader, child_type, depth + 1);
            }
        }
        if (block && (!has_name || !has_states)) throw Invalid("missing modern block palette fields");
        break;
    }
    default: throw Invalid("invalid NBT tag");
    }
}

// Retains only the first bounded diagnostic, never raw database bytes.
inline bool decode_section(std::string_view bytes, int section_y, bool &has_storage,
                           std::string &error) {
    try {
        Reader reader(bytes);
        const auto version = reader.byte();
        if (version != 8 && version != 9) throw Invalid("unsupported subchunk version");
        const unsigned storages = reader.byte();
        if (storages > 16) throw Invalid("storage count exceeds 16");
        if (version == 9 && static_cast<std::int8_t>(reader.byte()) != section_y)
            throw Invalid("subchunk index differs from key");
        for (unsigned layer = 0; layer < storages; ++layer) {
            const unsigned header = reader.byte();
            if (header & 1) throw Invalid("runtime palette in persistent data");
            const unsigned bits = header >> 1;
            if (bits != 0 && bits != 1 && bits != 2 && bits != 3 && bits != 4 &&
                bits != 5 && bits != 6 && bits != 8 && bits != 16)
                throw Invalid("unsupported palette bit width");
            const unsigned per_word = bits ? 32 / bits : 0;
            const auto words = reader.take(bits ? ((4096 + per_word - 1) / per_word) * 4 : 0);
            const unsigned count = bits ? reader.integer(4) : 1;
            if (count == 0 || count > 4096 || (bits && count > (1U << bits)))
                throw Invalid("invalid palette cardinality");
            if (bits) {
                const unsigned mask = (1U << bits) - 1;
                unsigned decoded{};
                for (std::size_t offset = 0; offset < words.size(); offset += 4) {
                    std::uint32_t word{};
                    for (unsigned i = 0; i < 4; ++i)
                        word |= std::uint32_t(static_cast<std::uint8_t>(words[offset + i])) << (8 * i);
                    for (unsigned i = 0; i < per_word && decoded < 4096; ++i, ++decoded) {
                        if ((word & mask) >= count) throw Invalid("block index outside palette");
                        word >>= bits;
                    }
                }
            }
            for (unsigned i = 0; i < count; ++i) {
                if (reader.byte() != 10) throw Invalid("palette root must be NBT compound");
                reader.string();
                nbt_payload(reader, 10, 0, true);
            }
        }
        if (reader.remaining()) throw Invalid("trailing subchunk bytes");
        has_storage = storages != 0;
        return true;
    } catch (const Invalid &failure) {
        error = failure.what();
        has_storage = false;
        return false;
    }
}
} // namespace chunklet::database
