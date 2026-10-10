// Usage: g++ -O2 -std=c++20 -ffunction-sections -fdata-sections -Isrc
// tools/shared_mutex_stats_test.cpp -pthread -Wl,--gc-sections
// -o .artifacts/shared_mutex_stats_test && .artifacts/shared_mutex_stats_test
// Exercise the actual registry through replacement lock functions; no BDS patch.
#include "synchronization/shared_mutex/optimizer.cpp"
#include <cstdio>
#include <thread>
#include <vector>

int main()
{
    using namespace chunklet::synchronization::shared_mutex;
    std::array<int, 64> keys{};
    const auto before = registry_size();
    if (!before || *before != 0) throw std::runtime_error("registry must initially be empty");
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 8; ++worker) threads.emplace_back([&keys] {
        for (auto &key : keys) {
            chunklet_shared_lock(&key);
            chunklet_shared_unlock(&key);
            chunklet_shared_lock_reader(&key);
            chunklet_shared_unlock_reader(&key);
        }
    });
    for (auto &thread : threads) thread.join();
    const auto after = registry_size();
    if (!after || *after != 64) throw std::runtime_error("registry count must track distinct addresses across threads");
    if (registry_size() != after) throw std::runtime_error("reading count must not grow registry");
    std::puts("PASS: registry diagnostics report 64 distinct addresses across 8 threads without inserting entries");
}
