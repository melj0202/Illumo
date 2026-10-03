#include "Rendering/Gpu/GlslProgramCache.h"

#include <Illumo/Foundation/BuildInfo.h>
#include <Illumo/Foundation/Profile.h>

#include <cstdint>
#include <cstring>
#include <span>

// Little-endian scalars and length-prefixed strings and arrays.
class ProgramWriter
{
public:
  explicit ProgramWriter(std::vector<unsigned char>* bytes)
    : m_bytes(bytes)
  {
  }
  void u32(std::uint32_t value)
  {
    for (unsigned int index = 0u; index < 4u; ++index) {
      m_bytes->push_back(static_cast<unsigned char>(value >> (index * 8u)));
    }
  }
  void u8(unsigned char value) { m_bytes->push_back(value); }
  void text(const std::string& value)
  {
    u32(static_cast<std::uint32_t>(value.size()));
    m_bytes->insert(m_bytes->end(), value.begin(), value.end());
  }
  void words(const std::vector<uint32_t>& values)
  {
    u32(static_cast<std::uint32_t>(values.size()));
    for (uint32_t value : values) {
      u32(value);
    }
  }

private:
  std::vector<unsigned char>* m_bytes;
};

class ProgramReader
{
public:
  explicit ProgramReader(const std::vector<unsigned char>& bytes)
    : m_bytes(bytes)
  {
  }
  bool u32(std::uint32_t* value)
  {
    if (m_bytes.size() - m_offset < 4u) {
      return fail();
    }
    *value = 0u;
    for (unsigned int index = 0u; index < 4u; ++index) {
      *value |= static_cast<std::uint32_t>(m_bytes[m_offset + index])
                << (index * 8u);
    }
    m_offset += 4u;
    return true;
  }
  bool u8(unsigned char* value)
  {
    if (m_offset >= m_bytes.size()) {
      return fail();
    }
    *value = m_bytes[m_offset++];
    return true;
  }
  bool text(std::string* value)
  {
    std::uint32_t size = 0u;
    if (!u32(&size) || m_bytes.size() - m_offset < size) {
      return fail();
    }
    value->assign(reinterpret_cast<const char*>(m_bytes.data() + m_offset),
                  size);
    m_offset += size;
    return true;
  }
  bool words(std::vector<uint32_t>* values)
  {
    std::uint32_t count = 0u;
    if (!u32(&count) || (m_bytes.size() - m_offset) / 4u < count) {
      return fail();
    }
    values->resize(count);
    for (uint32_t& value : *values) {
      u32(&value);
    }
    return true;
  }
  // A count of elements each at least `minimum` bytes long.
  bool count(std::uint32_t minimum, std::uint32_t* value)
  {
    return u32(value) && (m_bytes.size() - m_offset) / minimum >= *value;
  }
  bool finished() const { return m_offset == m_bytes.size() && !m_failed; }

private:
  bool fail()
  {
    m_failed = true;
    return false;
  }
  const std::vector<unsigned char>& m_bytes;
  std::size_t m_offset = 0u;
  bool m_failed = false;
};

void
serializeGlslProgram(const GlslProgram& program,
                     std::vector<unsigned char>* bytes)
{
  bytes->clear();
  ProgramWriter writer(bytes);
  writer.words(program.vertexSpirv);
  writer.words(program.fragmentSpirv);
  writer.u32(static_cast<std::uint32_t>(program.bindings.size()));
  for (const GlslBinding& binding : program.bindings) {
    writer.u8(static_cast<unsigned char>(binding.kind));
    writer.u32(binding.binding);
    writer.u32(binding.size);
    writer.u32(binding.sampler);
    writer.text(binding.name);
  }
  writer.u32(static_cast<std::uint32_t>(program.uniforms.size()));
  for (const GlslUniform& uniform : program.uniforms) {
    writer.text(uniform.name);
    writer.u8(static_cast<unsigned char>(uniform.type));
    writer.u32(uniform.offset);
    writer.u32(uniform.arraySize);
    writer.u32(uniform.arrayStride);
  }
  writer.u32(static_cast<std::uint32_t>(program.samplerNames.size()));
  for (const std::string& name : program.samplerNames) {
    writer.text(name);
  }
  writer.u32(static_cast<std::uint32_t>(program.inputs.size()));
  for (const GlslVertexInput& input : program.inputs) {
    writer.u32(input.location);
    writer.u32(input.locationCount);
    writer.u8(input.integer ? 1u : 0u);
  }
  writer.u32(program.defaultBlockSize);
  writer.u32(program.fragmentOutputs);
}

bool
deserializeGlslProgram(const std::vector<unsigned char>& bytes,
                       GlslProgram* program)
{
  ProgramReader reader(bytes);
  GlslProgram parsed;
  std::uint32_t count = 0u;
  if (!reader.words(&parsed.vertexSpirv) ||
      !reader.words(&parsed.fragmentSpirv) || !reader.count(17u, &count)) {
    return false;
  }
  parsed.bindings.resize(count);
  for (GlslBinding& binding : parsed.bindings) {
    unsigned char kind = 0u;
    if (!reader.u8(&kind) ||
        kind > static_cast<unsigned char>(GlslBindingKind::SamplerCube) ||
        !reader.u32(&binding.binding) || !reader.u32(&binding.size) ||
        !reader.u32(&binding.sampler) || !reader.text(&binding.name)) {
      return false;
    }
    binding.kind = static_cast<GlslBindingKind>(kind);
  }
  if (!reader.count(17u, &count)) {
    return false;
  }
  parsed.uniforms.resize(count);
  for (GlslUniform& uniform : parsed.uniforms) {
    unsigned char type = 0u;
    if (!reader.text(&uniform.name) || !reader.u8(&type) ||
        type > static_cast<unsigned char>(GlslValueType::Other) ||
        !reader.u32(&uniform.offset) || !reader.u32(&uniform.arraySize) ||
        !reader.u32(&uniform.arrayStride)) {
      return false;
    }
    uniform.type = static_cast<GlslValueType>(type);
  }
  if (!reader.count(4u, &count)) {
    return false;
  }
  parsed.samplerNames.resize(count);
  for (std::string& name : parsed.samplerNames) {
    if (!reader.text(&name)) {
      return false;
    }
  }
  if (!reader.count(9u, &count)) {
    return false;
  }
  parsed.inputs.resize(count);
  for (GlslVertexInput& input : parsed.inputs) {
    unsigned char integer = 0u;
    if (!reader.u32(&input.location) || !reader.u32(&input.locationCount) ||
        !reader.u8(&integer) || integer > 1u) {
      return false;
    }
    input.integer = integer != 0u;
  }
  if (!reader.u32(&parsed.defaultBlockSize) ||
      !reader.u32(&parsed.fragmentOutputs) || !reader.finished() ||
      parsed.vertexSpirv.empty() || parsed.fragmentSpirv.empty()) {
    return false;
  }
  *program = std::move(parsed);
  return true;
}

bool
compileGlslProgramCached(GpuShaderCache& cache,
                         const std::string& vertexSource,
                         const std::string& fragmentSource,
                         GlslProgram* program,
                         std::string* error)
{
  if (!cache.enabled()) {
    return compileGlslProgram(vertexSource, fragmentSource, program, error);
  }
  ILLUMO_PROFILE_ZONE("GlslProgramCache.compile");
  const std::string_view version(BuildInfo::FullVersion);
  const std::span<const unsigned char> parts[] = {
    { reinterpret_cast<const unsigned char*>(version.data()), version.size() },
    { reinterpret_cast<const unsigned char*>(vertexSource.data()),
      vertexSource.size() },
    { reinterpret_cast<const unsigned char*>(fragmentSource.data()),
      fragmentSource.size() },
  };
  const Sha256::Digest key =
    GpuShaderCache::key(kGlslProgramCacheFormat, parts);
  std::vector<unsigned char> bytes;
  if (cache.load(key, &bytes) && deserializeGlslProgram(bytes, program)) {
    return true;
  }
  if (!compileGlslProgram(vertexSource, fragmentSource, program, error)) {
    return false;
  }
  serializeGlslProgram(*program, &bytes);
  cache.store(key, bytes);
  return true;
}
