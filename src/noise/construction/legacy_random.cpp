#include "noise/construction/legacy_random.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace chunklet::noise::construction::legacy_random {
namespace {
constexpr std::uintptr_t kNextInt = 0x4655d20;
constexpr std::uintptr_t kGenerate = 0xe184a50;

std::uintptr_t executable_base;

using Generate = std::uint32_t (*)(void *);

Generate generator() noexcept
{
    return reinterpret_cast<Generate>(executable_base + kGenerate);
}

void *engine(void *random) noexcept
{
    return static_cast<unsigned char *>(random) + 8;
}
}  // namespace

void configure(std::uintptr_t base) noexcept
{
    executable_base = base;
}

bool matches(void *random) noexcept
{
    auto **vtable = *reinterpret_cast<void ***>(random);
    return reinterpret_cast<std::uintptr_t>(vtable[3]) ==
           executable_base + kNextInt;
}

double next_double(void *random) noexcept
{
    return static_cast<double>(generator()(engine(random))) * 0x1p-32;
}

void shuffle(void *random, unsigned char *values) noexcept
{
    auto next = generator();
    auto *state = engine(random);
    for (std::uint32_t index = 0; index < 256; ++index) {
        const auto selected = index + next(state) % (256 - index);
        std::swap(values[index], values[selected]);
    }
}

void consume(void *random, int count) noexcept
{
    auto next = generator();
    auto *state = engine(random);
    while (count-- > 0) {
        next(state);
    }
}

}  // namespace chunklet::noise::construction::legacy_random
