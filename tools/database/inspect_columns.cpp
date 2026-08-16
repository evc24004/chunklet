#include <leveldb/db.h>
#include <leveldb/decompress_allocator.h>
#include <leveldb/snappy_compressor.h>
#include <leveldb/zlib_compressor.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <string_view>
#include <vector>

namespace {
struct Column {
    bool finalized{};
    std::size_t data3d_size{};
    int expected_top{-999};
    int minimum_section{-4};
    std::vector<int> subchunks;
};

std::int32_t read_i32(const char *data)
{
    std::int32_t value{};
    std::memcpy(&value, data, sizeof(value));
    return value;
}

int dimension_id(std::string_view name)
{
    if (name == "overworld") {
        return 0;
    }
    if (name == "nether") {
        return 1;
    }
    if (name == "the_end") {
        return 2;
    }
    return -1;
}

bool matches_dimension(const leveldb::Slice &key, int dimension)
{
    if (dimension == 0) {
        return key.size() == 9 || key.size() == 10;
    }
    return (key.size() == 13 || key.size() == 14) &&
        read_i32(key.data() + 8) == dimension;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 7 && argc != 8) {
        std::cerr << "usage: inspect-columns <db> <dimension> <min-x> <min-z> "
                     "<max-x> <max-z> [--summary]\n";
        return 2;
    }
    const int dimension = dimension_id(argv[2]);
    if (dimension < 0 || (argc == 8 && std::string_view(argv[7]) != "--summary")) {
        std::cerr << "dimension must be overworld, nether, or the_end\n";
        return 2;
    }
    const int min_x = std::stoi(argv[3]);
    const int min_z = std::stoi(argv[4]);
    const int max_x = std::stoi(argv[5]);
    const int max_z = std::stoi(argv[6]);
    const bool summary_only = argc == 8;

    leveldb::Options options;
    options.compressors[0] = new leveldb::ZlibCompressorRaw(-1);
    options.compressors[1] = new leveldb::ZlibCompressor();
    options.compressors[2] = new leveldb::SnappyCompressor();
    leveldb::DB *raw{};
    const auto open = leveldb::DB::Open(options, argv[1], &raw);
    if (!open.ok()) {
        std::cerr << open.ToString() << '\n';
        return 1;
    }
    std::unique_ptr<leveldb::DB> db(raw);
    std::map<std::pair<int, int>, Column> columns;
    std::size_t records{};
    std::uint64_t logical_bytes{};
    leveldb::DecompressAllocator allocator;
    leveldb::ReadOptions reads;
    reads.decompress_allocator = &allocator;
    std::unique_ptr<leveldb::Iterator> iterator(db->NewIterator(reads));
    for (iterator->SeekToFirst(); iterator->Valid(); iterator->Next()) {
        const auto key = iterator->key();
        if (!matches_dimension(key, dimension)) {
            continue;
        }
        const int x = read_i32(key.data());
        const int z = read_i32(key.data() + 4);
        if (x < min_x || x > max_x || z < min_z || z > max_z) {
            continue;
        }
        const std::size_t tag_offset = dimension == 0 ? 8 : 12;
        const auto tag = static_cast<unsigned char>(key[tag_offset]);
        auto &column = columns[{x, z}];
        if (dimension != 0) {
            column.minimum_section = 0;
        }
        const auto value = iterator->value();
        ++records;
        logical_bytes += key.size() + value.size();
        if (tag == 0x36) {
            column.finalized = !value.empty() &&
                static_cast<unsigned char>(value.data()[0]) == 2;
        } else if (tag == 0x2f && key.size() == tag_offset + 2) {
            column.subchunks.push_back(
                static_cast<std::int8_t>(key[tag_offset + 1]));
        } else if (tag == 0x2b) {
            column.data3d_size = value.size();
            unsigned maximum_height{};
            for (std::size_t i = 0;
                 i + 1 < std::min<std::size_t>(512, value.size()); i += 2) {
                const auto height = static_cast<unsigned char>(value.data()[i]) |
                    (static_cast<unsigned>(
                         static_cast<unsigned char>(value.data()[i + 1])) << 8);
                maximum_height = std::max(maximum_height, height);
            }
            const int top_y = static_cast<int>(maximum_height) - 65;
            column.expected_top = (top_y >= 0 ? top_y : top_y - 15) / 16;
        }
    }
    if (!iterator->status().ok()) {
        std::cerr << iterator->status().ToString() << '\n';
        return 1;
    }

    const std::size_t expected = static_cast<std::size_t>(max_x - min_x + 1) *
        static_cast<std::size_t>(max_z - min_z + 1);
    bool complete = columns.size() == expected;
    for (const auto &[position, column] : columns) {
        auto subchunks = column.subchunks;
        std::sort(subchunks.begin(), subchunks.end());
        const bool height_covered = column.expected_top < column.minimum_section ||
            (!subchunks.empty() && subchunks.back() >= column.expected_top);
        const bool serviceable = column.finalized && column.data3d_size >= 512 &&
            height_covered;
        complete = complete && serviceable;
        if (!summary_only) {
            std::cout << position.first << ',' << position.second
                      << " finalized=" << column.finalized
                      << " data3d=" << column.data3d_size << " subchunks=";
            for (const auto y : subchunks) {
                std::cout << y << ',';
            }
            std::cout << " serviceable=" << serviceable << '\n';
        }
    }
    std::cout << "complete_columns=" << columns.size() << '/' << expected
              << " all_serviceable=" << complete << " records=" << records
              << " logical_bytes=" << logical_bytes << '\n';
    return complete ? 0 : 1;
}
