#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

bool
compileWasmIsolated(std::span<const std::byte> input,
                    std::uint64_t memoryLimit,
                    std::uint32_t timeoutMilliseconds,
                    std::vector<std::byte>& artifact,
                    std::string& error);
