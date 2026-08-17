#include "noise/construction/direct_kernel.h"

#include "noise/construction/legacy_random.h"
#include "noise/shuffle/optimizer.h"

#include <charconv>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace chunklet::noise::construction::direct_kernel {
namespace {
constexpr std::size_t kNoiseSize = 0x10c;

template <typename Value>
void store(unsigned char *target, std::size_t offset, Value value)
{
    std::memcpy(target + offset, &value, sizeof(value));
}

void construct_noise(void *random, unsigned char *noise)
{
    const bool legacy = legacy_random::matches(random);
    auto **vtable = *reinterpret_cast<void ***>(random);
    using NextDouble = double (*)(void *);
    auto next_double = reinterpret_cast<NextDouble>(vtable[7]);
    const auto next = [random, legacy, next_double] {
        return legacy ? legacy_random::next_double(random) : next_double(random);
    };
    store(noise, 0, static_cast<float>(next() * 256.0));
    store(noise, 4, static_cast<float>(next() * 256.0));
    store(noise, 8, static_cast<float>(next() * 256.0));
    for (std::size_t index = 0; index < 256; ++index) {
        noise[12 + index] = static_cast<unsigned char>(index);
    }
    if (legacy) {
        legacy_random::shuffle(random, noise + 12);
    } else {
        noise::shuffle::shuffle_values(random, noise + 12);
    }
}

void consume_noise(void *random)
{
    if (legacy_random::matches(random)) {
        legacy_random::consume(random, 262);
        return;
    }
    auto **vtable = *reinterpret_cast<void ***>(random);
    using Consume = void (*)(void *, int);
    reinterpret_cast<Consume>(vtable[9])(random, 262);
}

void fill_element(unsigned char *element, const unsigned char *noise, int octave,
                  std::size_t index, std::size_t count, float amplitude)
{
    std::memcpy(element, noise, kNoiseSize);
    const auto exponent = octave + static_cast<int>(index);
    const auto frequency = static_cast<float>(std::ldexp(1.0, exponent));
    const auto numerator = std::uint32_t{1} << (count - index - 1);
    const auto denominator = (std::uint32_t{1} << count) - 1;
    const auto normalized = static_cast<float>(numerator) /
                            static_cast<float>(denominator);
    store(element, 0x10c, exponent);
    store(element, 0x110, amplitude);
    store(element, 0x114, frequency);
    store(element, 0x118, normalized);
    store(element, 0x11c, normalized * amplitude);
    element[0x120] = 1;
}
}  // namespace

void construct_into(unsigned char *storage, void *random, int octave,
                    const float *amplitudes, std::size_t count)
{
    for (std::size_t index = 0; index < count; ++index) {
        storage[index * kElementSize] = 0;
        storage[index * kElementSize + 0x120] = 0;
    }
    std::array<unsigned char, kNoiseSize> initial{};
    construct_noise(random, initial.data());
    const int initial_index = -octave;
    if (octave < 0) {
        for (int index = initial_index - 1; index >= 0; --index) {
            if (index < static_cast<int>(count) && amplitudes[index] != 0.0f) {
                std::array<unsigned char, kNoiseSize> current{};
                construct_noise(random, current.data());
                fill_element(storage + index * kElementSize, current.data(), octave,
                             index, count, amplitudes[index]);
            } else {
                consume_noise(random);
            }
        }
    }
    if (octave <= 0 && initial_index < static_cast<int>(count) &&
        amplitudes[initial_index] != 0.0f) {
        fill_element(storage + initial_index * kElementSize, initial.data(), octave,
                     initial_index, count, amplitudes[initial_index]);
    }
}

bool construct_positional_into(unsigned char *storage, void *random, int octave,
                               const float *amplitudes, std::size_t count)
{
    for (std::size_t index = 0; index < count; ++index) {
        storage[index * kElementSize] = 0;
        storage[index * kElementSize + 0x120] = 0;
    }
    auto **random_vtable = *reinterpret_cast<void ***>(random);
    using Fork = void (*)(void **, void *);
    void *factory{};
    reinterpret_cast<Fork>(random_vtable[11])(&factory, random);
    if (factory == nullptr) {
        return false;
    }
    const auto destroy = [](void *object) {
        auto **vtable = *reinterpret_cast<void ***>(object);
        using Destroy = void (*)(void *);
        reinterpret_cast<Destroy>(vtable[1])(object);
    };
    auto **factory_vtable = *reinterpret_cast<void ***>(factory);
    using Derive = void (*)(void **, void *, const void *);
    auto derive = reinterpret_cast<Derive>(factory_vtable[3]);
    for (std::size_t index = 0; index < count; ++index) {
        if (amplitudes[index] == 0.0F) {
            continue;
        }
        const int exponent = octave + static_cast<int>(index);
        std::array<char, 16> digits{};
        const auto result = std::to_chars(
            digits.data(), digits.data() + digits.size(), exponent);
        std::array<unsigned char, 24> name{};
        constexpr char prefix[] = "octave_";
        constexpr std::size_t prefix_size = sizeof(prefix) - 1;
        const auto digit_count = static_cast<std::size_t>(result.ptr - digits.data());
        const auto name_size = prefix_size + digit_count;
        name[0] = static_cast<unsigned char>(name_size << 1);
        std::memcpy(name.data() + 1, prefix, prefix_size);
        std::memcpy(name.data() + 1 + prefix_size, digits.data(), digit_count);
        void *derived{};
        derive(&derived, factory, name.data());
        if (derived == nullptr) {
            destroy(factory);
            return false;
        }
        std::array<unsigned char, kNoiseSize> noise{};
        construct_noise(derived, noise.data());
        destroy(derived);
        fill_element(storage + index * kElementSize, noise.data(), octave,
                     index, count, amplitudes[index]);
    }
    destroy(factory);
    return true;
}

}  // namespace chunklet::noise::construction::direct_kernel
