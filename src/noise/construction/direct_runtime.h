#pragma once

#include <cstdint>

namespace chunklet::noise::construction {

using Constructor = void (*)(void *, void *, int, const void *, int);

extern "C" void direct_constructor(
    void *output, void *random, int octave, const void *amplitudes, int mode);
void configure_direct(std::uintptr_t base, Constructor original) noexcept;

}  // namespace chunklet::noise::construction
