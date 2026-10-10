#pragma once

#include <cstddef>
#include <optional>

namespace chunklet::synchronization::shared_mutex {

void install();
void remove() noexcept;
// Snapshot for shutdown diagnostics; never called on the lock-operation path.
std::optional<std::size_t> registry_size() noexcept;

}  // namespace chunklet::synchronization::shared_mutex
