// Real LevelDB regression fixtures. Run through test-database-inspector.sh;
// argv[1] is a disposable test directory created by that script.
#define main inspector_main
#include "inspect_columns.cpp"
#undef main
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {
void le(std::string &out, unsigned value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) out.push_back((value >> (8 * i)) & 255);
}
void str(std::string &out, std::string_view value) {
    le(out, value.size(), 2); out += value;
}
std::string palette(std::string_view name = "minecraft:stone") {
    std::string out; out.push_back(10); str(out, "");
    out.push_back(8); str(out, "name"); str(out, name);
    out.push_back(10); str(out, "states"); out.push_back(0);
    out.push_back(3); str(out, "version"); le(out, 18168865, 4);
    out.push_back(0); return out;
}
std::string section(int y = 0, int version = 9, int bits = 0) {
    std::string out; out.push_back(version); out.push_back(1);
    if (version == 9) out.push_back(y);
    out.push_back(bits << 1);
    if (bits) {
        const unsigned per_word = 32 / bits;
        out.append(((4096 + per_word - 1) / per_word) * 4, '\0');
        le(out, 1, 4);
    }
    return out + palette();
}
struct Fixture {
    std::string name;
    std::string dimension{"overworld"};
    unsigned height{65};
    std::vector<std::pair<int, std::string>> sections{{0, section()}};
    bool expected{true};
    bool finalized{true};
    bool data3d{true};
    int alternate_height{-1};
};
void require(bool ok, std::string_view message) {
    if (!ok) throw std::runtime_error(std::string(message));
}
void write_fixture(const std::filesystem::path &path, const Fixture &f) {
    leveldb::Options options; options.create_if_missing = true;
    leveldb::DB *raw{};
    require(leveldb::DB::Open(options, path.string(), &raw).ok(), "fixture open failed");
    std::unique_ptr<leveldb::DB> db(raw);
    std::string base(8, '\0');
    if (f.dimension != "overworld") le(base, dimension_id(f.dimension), 4);
    const auto put = [&](std::string suffix, const std::string &value) {
        require(db->Put(leveldb::WriteOptions(), base + suffix, value).ok(), "fixture put failed");
    };
    if (f.finalized) put(std::string(1, '\x36'), std::string("\2\0\0\0", 4));
    if (f.data3d) {
        std::string heights;
        for (int i = 0; i < 256; ++i)
            le(heights, i == 0 && f.alternate_height >= 0 ? f.alternate_height : f.height, 2);
        put(std::string(1, '\x2b'), heights);
    }
    for (const auto &[y, payload] : f.sections) {
        std::string suffix(1, '\x2f'); suffix.push_back(y); put(suffix, payload);
    }
}
}

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) return 2;
    std::vector<Fixture> cases;
    const auto bad = [&](std::string name, std::string payload) {
        Fixture f{std::move(name)}; f.sections = {{0, std::move(payload)}};
        f.expected = false; cases.push_back(std::move(f));
    };
    bad("empty-payload", "");
    bad("truncated-header", std::string("\11\1", 2));
    bad("truncated-palette", section().substr(0, section().size() - 1));
    auto corrupt = section(); corrupt[3] = 1; bad("runtime-palette", corrupt);
    corrupt = section(); corrupt[0] = 7; bad("unknown-version", corrupt);
    corrupt = section(); corrupt[2] = 1; bad("index-mismatch", corrupt);
    corrupt = section(); corrupt[3] = 14; bad("unsupported-bits", corrupt);
    bad("trailing-bytes", section() + "x");
    bad("zero-storages-with-height", std::string("\11\0\0", 3));
    corrupt = section(0, 9, 1); corrupt[4] = 1; bad("out-of-palette-index", corrupt);
    corrupt = section(0, 9, 1); corrupt[4 + 512] = 0; bad("zero-palette", corrupt);
    corrupt = section(0, 9, 1); corrupt[4 + 512] = 3; bad("oversized-palette", corrupt);
    bad("truncated-word-array", section(0, 9, 16).substr(0, 100));
    corrupt = section(); corrupt[7] = 99; bad("invalid-nbt-tag", corrupt);
    corrupt = section(); corrupt[8] = 127; bad("truncated-nbt-name", corrupt);
    corrupt = section(); corrupt.replace(corrupt.size() - 1, 1, std::string("\7\0\0\377\377\377\177", 7));
    bad("oversized-nbt-array", corrupt);
    corrupt = section(); corrupt.pop_back();
    for (int i = 0; i < 70; ++i) corrupt += std::string("\12\0\0", 3);
    corrupt.append(71, '\0'); bad("excessive-nbt-depth", corrupt);
    corrupt = section(); corrupt.pop_back(); corrupt.push_back(8); str(corrupt, "name");
    str(corrupt, "minecraft:air"); corrupt.push_back(0); bad("duplicate-nbt-field", corrupt);
    corrupt = std::string("\11\1\0\0", 4) + palette("stone"); bad("invalid-block-name", corrupt);
    cases.push_back(Fixture{"valid-v9-singleton"});
    for (int bits : {1, 2, 3, 4, 5, 6, 8, 16}) {
        Fixture f{"valid-packed-" + std::to_string(bits)};
        f.sections = {{0, section(0, 9, bits)}}; cases.push_back(f);
    }
    Fixture f{"valid-v8"}; f.sections = {{0, section(0, 8)}}; cases.push_back(f);
    f = Fixture{"valid-two-storages"}; corrupt = section(); corrupt[1] = 2;
    corrupt += section().substr(3); f.sections = {{0, corrupt}}; cases.push_back(f);
    f = Fixture{"missing-height-section"}; f.sections = {{1, section(1)}};
    f.expected = false; cases.push_back(f);
    f = Fixture{"missing-lower-height-section"}; f.height = 81; f.alternate_height = 65;
    f.sections = {{1, section(1)}}; f.expected = false; cases.push_back(f);
    f = Fixture{"valid-multiple-height-sections"}; f.height = 81; f.alternate_height = 65;
    f.sections.push_back({1, section(1)}); cases.push_back(f);
    f = Fixture{"valid-no-contiguous-lower-sections"}; f.height = 97;
    f.sections = {{2, section(2)}}; cases.push_back(f);
    f = Fixture{"valid-negative-y"}; f.height = 1; f.sections = {{-4, section(-4)}}; cases.push_back(f);
    f = Fixture{"valid-empty-column"}; f.height = 0; f.sections.clear(); cases.push_back(f);
    f = Fixture{"invalid-height"}; f.height = 385; f.expected = false; cases.push_back(f);
    f = Fixture{"missing-finalized"}; f.finalized = false; f.expected = false; cases.push_back(f);
    f = Fixture{"missing-data3d"}; f.data3d = false; f.expected = false; cases.push_back(f);
    for (std::string dimension : {"nether", "the_end"}) {
        f = Fixture{"valid-" + dimension}; f.dimension = dimension; f.height = 1; cases.push_back(f);
        f = Fixture{"wrong-" + dimension + "-height-offset"}; f.dimension = dimension;
        f.height = 65; f.expected = false; cases.push_back(f);
    }
    if (argc == 3) {
        for (std::string name : {"oracle-v8", "oracle-v9", "oracle-multilayer"}) {
            std::ifstream input(std::filesystem::path(argv[2]) / (name + ".bin"), std::ios::binary);
            require(input.good(), "missing oracle fixture");
            std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            f = Fixture{name}; f.sections = {{0, std::move(bytes)}}; cases.push_back(f);
        }
    }
    unsigned failures{};
    for (const auto &f : cases) {
        const auto path = std::filesystem::path(argv[1]) / f.name;
        write_fixture(path, f);
        std::vector<std::string> args{"inspect-columns", path.string(), f.dimension, "0", "0", "0", "0", "--summary"};
        std::vector<char *> ptrs; for (auto &arg : args) ptrs.push_back(arg.data());
        std::ostringstream output; auto *previous = std::cout.rdbuf(output.rdbuf());
        const int result = inspector_main(ptrs.size(), ptrs.data());
        std::cout.rdbuf(previous);
        if ((result == 0) != f.expected) {
            ++failures; std::cerr << "FAIL " << f.name << ": " << output.str();
        }
    }
    std::cout << "database inspector: " << cases.size() << " cases, " << failures << " failures\n";
    unsigned prefixes{};
    for (int bits : {0, 1, 3, 6, 16}) {
        const auto bytes = section(0, 9, bits);
        for (std::size_t size = 0; size < bytes.size(); ++size) {
            bool storage{}; std::string error;
            require(!chunklet::database::decode_section(std::string_view(bytes).substr(0, size), 0, storage, error),
                    "truncated prefix accepted");
            ++prefixes;
        }
    }
    // Deterministic malformed-input smoke test; sanitizer mode checks memory/UB.
    std::uint32_t random = 0x1b873593;
    for (unsigned attempt = 0; attempt < 20000; ++attempt) {
        auto bytes = section(0, 9, attempt % 2 ? 0 : 3);
        for (unsigned mutation = 0; mutation < 4; ++mutation) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            bytes[random % bytes.size()] = random >> 24;
        }
        bool storage{}; std::string error;
        chunklet::database::decode_section(bytes, 0, storage, error);
    }
    std::cout << "decoder: " << prefixes << " truncated prefixes rejected, 20000 bounded mutations checked\n";
    return failures ? 1 : 0;
}
