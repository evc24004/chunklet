// Run: g++ -std=c++20 -Isrc tools/native_guard_test.cpp src/native/layout.cpp
//      -o .artifacts/native_guard_test && .artifacts/native_guard_test
// The actual production guard must reject this non-BDS executable before any
// private native pointer dereference. It must never return success or crash.
#include "native/layout.h"
#include <cstdio>
#include <stdexcept>
#include <string_view>

int main()
{
    try {
        chunklet::native::verify_layout();
    } catch (const std::runtime_error &error) {
        if (std::string_view(error.what()).starts_with("unsupported BDS GNU build ID:")) {
            std::puts("PASS: production ABI guard rejected unknown executable before pointer reads");
            return 0;
        }
        std::fprintf(stderr, "unexpected guard failure: %s\n", error.what());
        return 1;
    }
    std::fputs("guard accepted an unsupported executable\n", stderr);
    return 1;
}
