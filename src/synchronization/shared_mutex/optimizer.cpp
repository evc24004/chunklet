#include "synchronization/shared_mutex/optimizer.h"

#include "native/layout.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <pthread.h>
#include <stdexcept>
#include <unordered_map>
#include <sys/mman.h>

namespace chunklet::synchronization::shared_mutex {
namespace {
struct Lock {
    Lock()
    {
        const int result = pthread_rwlock_init(&value, nullptr);
        if (result != 0) {
            throw std::runtime_error(std::strerror(result));
        }
    }

    ~Lock() { pthread_rwlock_destroy(&value); }
    pthread_rwlock_t value{};
};
struct CacheEntry {
    const void *key{};
    Lock *lock{};
};


pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;
std::unordered_map<const void *, std::unique_ptr<Lock>> registry;
thread_local std::array<CacheEntry, 64> cache;
std::uintptr_t base;
bool installed;

Lock *resolve(const void *key)
{
    const auto address = reinterpret_cast<std::uintptr_t>(key);
    auto &cached = cache[((address >> 4) ^ (address >> 17)) &
                         (cache.size() - 1)];
    if (cached.key == key) {
        return cached.lock;
    }
    const int locked = pthread_mutex_lock(&registry_mutex);
    if (locked != 0) {
        throw std::runtime_error(std::strerror(locked));
    }
    Lock *result{};
    try {
        auto position = registry.find(key);
        if (position == registry.end()) {
            position = registry.emplace(key, std::make_unique<Lock>()).first;
        }
        result = position->second.get();
    } catch (...) {
        pthread_mutex_unlock(&registry_mutex);
        throw;
    }
    pthread_mutex_unlock(&registry_mutex);
    cached = {key, result};
    return result;
}

void require_success(int result)
{
    if (result != 0) {
        throw std::runtime_error(std::strerror(result));
    }
}

}  // namespace

extern "C" void chunklet_shared_lock(void *key)
{
    require_success(pthread_rwlock_wrlock(&resolve(key)->value));
}

extern "C" bool chunklet_shared_try_lock(void *key)
{
    const int result = pthread_rwlock_trywrlock(&resolve(key)->value);
    if (result == 0) {
        return true;
    }
    if (result == EBUSY) {
        return false;
    }
    require_success(result);
    return false;
}

extern "C" void chunklet_shared_unlock(void *key)
{
    require_success(pthread_rwlock_unlock(&resolve(key)->value));
}

extern "C" void chunklet_shared_lock_reader(void *key)
{
    require_success(pthread_rwlock_rdlock(&resolve(key)->value));
}

extern "C" bool chunklet_shared_try_lock_reader(void *key)
{
    const int result = pthread_rwlock_tryrdlock(&resolve(key)->value);
    if (result == 0) {
        return true;
    }
    if (result == EBUSY || result == EAGAIN) {
        return false;
    }
    require_success(result);
    return false;
}

extern "C" void chunklet_shared_unlock_reader(void *key)
{
    require_success(pthread_rwlock_unlock(&resolve(key)->value));
}

namespace {
struct Target {
    std::uintptr_t offset;
    std::size_t size;
    std::array<unsigned char, 19> original;
    std::uintptr_t helper;
};

std::array<Target, 6> targets{{
    {0x3caf350, 16,
     {0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x83, 0xec, 0x10, 0x48, 0x89,
      0xfb, 0x48, 0x89, 0x3c, 0x24},
     reinterpret_cast<std::uintptr_t>(&chunklet_shared_lock)},
    {0x3caf3f0, 18,
     {0x53, 0x48, 0x89, 0xfb, 0xe8, 0x87, 0x01, 0x00, 0x00, 0x48, 0x89,
      0xdf, 0x8b, 0x83, 0x88, 0x00, 0x00, 0x00},
     reinterpret_cast<std::uintptr_t>(&chunklet_shared_try_lock)},
    {0x3caf420, 19,
     {0x53, 0x48, 0x89, 0xfb, 0xe8, 0x57, 0x01, 0x00, 0x00, 0xc7, 0x83,
      0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
     reinterpret_cast<std::uintptr_t>(&chunklet_shared_unlock)},
    {0x3caf450, 16,
     {0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x83, 0xec, 0x10, 0x48, 0x89,
      0xfb, 0x48, 0x89, 0x3c, 0x24},
     reinterpret_cast<std::uintptr_t>(&chunklet_shared_lock_reader)},
    {0x3caf4d0, 18,
     {0x53, 0x48, 0x89, 0xfb, 0xe8, 0xa7, 0x00, 0x00, 0x00, 0x48, 0x89,
      0xdf, 0x8b, 0x83, 0x88, 0x00, 0x00, 0x00},
     reinterpret_cast<std::uintptr_t>(&chunklet_shared_try_lock_reader)},
    {0x3caf510, 15,
     {0x53, 0x48, 0x89, 0xfb, 0xe8, 0x67, 0x00, 0x00, 0x00, 0x8b, 0x8b,
      0x88, 0x00, 0x00, 0x00},
     reinterpret_cast<std::uintptr_t>(&chunklet_shared_unlock_reader)},
}};

std::pair<void *, std::size_t> target_pages()
{
    constexpr std::uintptr_t page_mask = 4095;
    const auto begin = (base + targets.front().offset) & ~page_mask;
    const auto end = (base + targets.back().offset + targets.back().size +
                      page_mask) & ~page_mask;
    return {reinterpret_cast<void *>(begin), end - begin};
}

void write_targets(bool optimize)
{
    for (const auto &target : targets) {
        auto *destination =
            reinterpret_cast<unsigned char *>(base + target.offset);
        if (!optimize) {
            std::memcpy(destination, target.original.data(), target.size);
            continue;
        }
        std::array<unsigned char, 19> patch{};
        patch.fill(0x90);
        patch[0] = 0xff;
        patch[1] = 0x25;
        std::memset(patch.data() + 2, 0, sizeof(std::uint32_t));
        std::memcpy(patch.data() + 6, &target.helper, sizeof(target.helper));
        std::memcpy(destination, patch.data(), target.size);
    }
}
}  // namespace

void install()
{
    if (installed) {
        return;
    }
    base = native::executable_base();
    for (const auto &target : targets) {
        const auto *actual = reinterpret_cast<const unsigned char *>(
            base + target.offset);
        if (std::memcmp(actual, target.original.data(), target.size) != 0) {
            throw std::runtime_error(
                "BDS shared mutex does not match the pinned build");
        }
    }
    registry.reserve(4096);
    const auto [page, size] = target_pages();
    if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        throw std::runtime_error(std::strerror(errno));
    }
    write_targets(true);
    __builtin___clear_cache(static_cast<char *>(page),
                            static_cast<char *>(page) + size);
    if (mprotect(page, size, PROT_READ | PROT_EXEC) != 0) {
        const int failure = errno;
        write_targets(false);
        mprotect(page, size, PROT_READ | PROT_EXEC);
        throw std::runtime_error(std::strerror(failure));
    }
    installed = true;
}

void remove() noexcept
{
    if (!installed) {
        return;
    }
    const auto [page, size] = target_pages();
    if (mprotect(page, size, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
        write_targets(false);
        __builtin___clear_cache(static_cast<char *>(page),
                                static_cast<char *>(page) + size);
        mprotect(page, size, PROT_READ | PROT_EXEC);
    }
    installed = false;
    registry.clear();
}

}  // namespace chunklet::synchronization::shared_mutex
