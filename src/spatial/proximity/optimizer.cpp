#include "spatial/proximity/optimizer.h"

#include "native/layout.h"

#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <immintrin.h>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

namespace chunklet::spatial::proximity {
namespace {
constexpr std::uintptr_t kLoop = 0xca83484;
constexpr std::uintptr_t kNoMatch = 0xca833c6;
constexpr std::uintptr_t kMatch = 0xca834dc;
constexpr std::array<unsigned char, 88> kOriginal{
    0x49, 0x8b, 0x55, 0x18, 0x31, 0xf6, 0xf3, 0x0f, 0x10, 0x4c, 0x24,
    0x20, 0x48, 0x8b, 0x3c, 0xf2, 0x0f, 0x28, 0xc6, 0xf3, 0x0f, 0x5c,
    0x07, 0xf3, 0x0f, 0x5c, 0x4f, 0x04, 0xf3, 0x0f, 0x10, 0x54, 0x24,
    0x50, 0xf3, 0x0f, 0x5c, 0x57, 0x08, 0xf3, 0x0f, 0x59, 0xc9, 0xf3,
    0x0f, 0x59, 0xc0, 0xf3, 0x0f, 0x58, 0xc1, 0xf3, 0x0f, 0x59, 0xd2,
    0xf3, 0x0f, 0x58, 0xd0, 0xf3, 0x0f, 0x10, 0x47, 0x0c, 0x0f, 0x2e,
    0xc2, 0x77, 0x13, 0x48, 0xff, 0xc6, 0x48, 0x39, 0xf1, 0xf3, 0x0f,
    0x10, 0x4c, 0x24, 0x20, 0x75, 0xb9, 0xe9, 0xea, 0xfe, 0xff, 0xff};

std::uintptr_t base;
bool installed;
std::atomic<int> validation_state{};
std::atomic<std::size_t> validations{};
std::atomic<std::size_t> mismatches{};
constexpr std::size_t kValidationCalls = 64;

const float *find_nearby_scalar(const float *const *points,
                                std::size_t count, float x, float y,
                                float z) noexcept
{
    for (std::size_t index = 0; index < count; ++index) {
        const float *point = points[index];
        const __m128 dx = _mm_sub_ss(_mm_set_ss(x), _mm_load_ss(point));
        const __m128 dy = _mm_sub_ss(_mm_set_ss(y), _mm_load_ss(point + 1));
        const __m128 dz = _mm_sub_ss(_mm_set_ss(z), _mm_load_ss(point + 2));
        const __m128 distance = _mm_add_ss(
            _mm_add_ss(_mm_mul_ss(dy, dy), _mm_mul_ss(dx, dx)),
            _mm_mul_ss(dz, dz));
        if (_mm_comigt_ss(_mm_load_ss(point + 3), distance)) {
            return point;
        }
    }
    return nullptr;
}


extern "C" __attribute__((target("avx2")))
const float *chunklet_find_nearby(const float *const *points,
                                  std::size_t count,
                                  float x, float y, float z) noexcept
{
    const __m128 xs = _mm_set1_ps(x);
    const __m128 ys = _mm_set1_ps(y);
    const __m128 zs = _mm_set1_ps(z);
    std::size_t index = 0;
    for (; index + 4 <= count; index += 4) {
        const float *p0 = points[index];
        const float *p1 = points[index + 1];
        const float *p2 = points[index + 2];
        const float *p3 = points[index + 3];
        __m128 points_x = _mm_loadu_ps(p0);
        __m128 points_y = _mm_loadu_ps(p1);
        __m128 points_z = _mm_loadu_ps(p2);
        __m128 radius = _mm_loadu_ps(p3);
        _MM_TRANSPOSE4_PS(points_x, points_y, points_z, radius);
        const __m128 dx = _mm_sub_ps(xs, points_x);
        const __m128 dy = _mm_sub_ps(ys, points_y);
        const __m128 dz = _mm_sub_ps(zs, points_z);
        const __m128 distance = _mm_add_ps(
            _mm_add_ps(_mm_mul_ps(dy, dy), _mm_mul_ps(dx, dx)),
            _mm_mul_ps(dz, dz));
        const unsigned matches = static_cast<unsigned>(
            _mm_movemask_ps(_mm_cmpgt_ps(radius, distance)));
        if (matches != 0) {
            return points[index + std::countr_zero(matches)];
        }
    }
    for (; index < count; ++index) {
        const float *point = points[index];
        const float dx = x - point[0];
        const float dy = y - point[1];
        const float dz = z - point[2];
        const float distance = dy * dy + dx * dx + dz * dz;
        if (point[3] > distance) {
            return point;
        }
    }
    return nullptr;
}

extern "C" const float *checked_find_nearby(
    const float *const *points, std::size_t count,
    float x, float y, float z) noexcept
{
    const int state = validation_state.load(std::memory_order_acquire);
    if (state > 0) {
        return chunklet_find_nearby(points, count, x, y, z);
    }
    if (state < 0) {
        return find_nearby_scalar(points, count, x, y, z);
    }
    const float *expected = find_nearby_scalar(points, count, x, y, z);
    const float *actual = chunklet_find_nearby(points, count, x, y, z);
    if (actual != expected) {
        mismatches.fetch_add(1, std::memory_order_relaxed);
        validation_state.store(-1, std::memory_order_release);
        return expected;
    }
    if (validations.fetch_add(1, std::memory_order_relaxed) + 1 >=
        kValidationCalls) {
        int validating = 0;
        validation_state.compare_exchange_strong(
            validating, 1, std::memory_order_release,
            std::memory_order_relaxed);
    }
    return expected;
}

std::pair<void *, std::size_t> target_page()
{
    const auto page_size = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    const auto begin = (base + kLoop) & ~(page_size - 1);
    return {reinterpret_cast<void *>(begin), page_size};
}

void emit_jump(std::array<unsigned char, kOriginal.size()> &code,
               std::size_t &cursor, unsigned char opcode,
               std::uintptr_t target)
{
    if (opcode == 0x84) {
        code[cursor++] = 0x0f;
    }
    code[cursor++] = opcode;
    const auto next = base + kLoop + cursor + sizeof(std::int32_t);
    const auto displacement = static_cast<std::int32_t>(target - next);
    std::memcpy(code.data() + cursor, &displacement, sizeof(displacement));
    cursor += sizeof(displacement);
}

void write_patch()
{
    std::array<unsigned char, kOriginal.size()> code;
    code.fill(0x90);
    constexpr std::array<unsigned char, 27> prefix{
        0x49, 0x8b, 0x7d, 0x18, 0x89, 0xce, 0x0f, 0x28, 0xc6,
        0xf3, 0x0f, 0x10, 0x4c, 0x24, 0x20, 0xf3, 0x0f, 0x10,
        0x54, 0x24, 0x50, 0x50, 0x48, 0x83, 0xec, 0x08, 0x48};
    std::memcpy(code.data(), prefix.data(), prefix.size());
    std::size_t cursor = prefix.size();
    code[cursor++] = 0xb8;
    const auto helper = reinterpret_cast<std::uintptr_t>(&checked_find_nearby);
    std::memcpy(code.data() + cursor, &helper, sizeof(helper));
    cursor += sizeof(helper);
    constexpr std::array<unsigned char, 13> suffix{
        0xff, 0xd0, 0x48, 0x89, 0xc7, 0x48, 0x83,
        0xc4, 0x08, 0x58, 0x48, 0x85, 0xff};
    std::memcpy(code.data() + cursor, suffix.data(), suffix.size());
    cursor += suffix.size();
    emit_jump(code, cursor, 0x84, base + kNoMatch);
    emit_jump(code, cursor, 0xe9, base + kMatch);
    std::memcpy(reinterpret_cast<void *>(base + kLoop), code.data(), code.size());
}
}  // namespace

void install()
{
    if (installed) return;
    validation_state.store(0, std::memory_order_relaxed);
    validations.store(0, std::memory_order_relaxed);
    mismatches.store(0, std::memory_order_relaxed);
    base = native::executable_base();
    auto *target = reinterpret_cast<unsigned char *>(base + kLoop);
    if (std::memcmp(target, kOriginal.data(), kOriginal.size()) != 0) {
        throw std::runtime_error("BDS proximity loop does not match the pinned build");
    }
    const auto [page, size] = target_page();
    if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        throw std::runtime_error(std::strerror(errno));
    }
    write_patch();
    __builtin___clear_cache(reinterpret_cast<char *>(target),
                            reinterpret_cast<char *>(target + kOriginal.size()));
    if (mprotect(page, size, PROT_READ | PROT_EXEC) != 0) {
        std::memcpy(target, kOriginal.data(), kOriginal.size());
        mprotect(page, size, PROT_READ | PROT_EXEC);
        throw std::runtime_error(std::strerror(errno));
    }
    installed = true;
}

void remove() noexcept
{
    if (!installed) return;
    const auto [page, size] = target_page();
    if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
        auto *target = reinterpret_cast<unsigned char *>(base + kLoop);
        std::memcpy(target, kOriginal.data(), kOriginal.size());
        __builtin___clear_cache(reinterpret_cast<char *>(target),
                                reinterpret_cast<char *>(target + kOriginal.size()));
        mprotect(page, size, PROT_READ | PROT_EXEC);
    }
    installed = false;
}

std::size_t mismatch_count() noexcept
{
    return mismatches.load(std::memory_order_relaxed);
}

std::size_t validation_count() noexcept
{
    return validations.load(std::memory_order_relaxed);
}

}  // namespace chunklet::spatial::proximity
