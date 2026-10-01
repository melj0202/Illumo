#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Compiles untrusted WASM in a job-limited helper process. engineOptions is
// encodeWasmEngineOptions() of the engine that will deserialize the artifact.
bool
compileWasmIsolated(std::span<const std::byte> input,
                    std::uint64_t memoryLimit,
                    std::uint32_t timeoutMilliseconds,
                    std::uint32_t engineOptions,
                    std::vector<std::byte>& artifact,
                    std::string& error);
