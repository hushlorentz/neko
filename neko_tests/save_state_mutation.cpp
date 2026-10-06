#include "save_state_mutation.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
  constexpr std::uint64_t FNV_OFFSET_BASIS =
    UINT64_C(14695981039346656037);
  constexpr std::uint64_t FNV_PRIME =
    UINT64_C(1099511628211);
}

SaveStateMutation::ByteReference::ByteReference(
  SaveStateMutation *mutation,
  std::string fieldPath,
  std::size_t relativeOffset) :
  owner(mutation),
  path(std::move(fieldPath)),
  byteOffset(relativeOffset)
{
}

SaveStateMutation::ByteReference::operator std::uint8_t() const
{
  return owner->readByte(path, byteOffset);
}

SaveStateMutation::ByteReference &
SaveStateMutation::ByteReference::operator=(std::uint8_t value)
{
  owner->writeByte(path, byteOffset, value);
  return *this;
}

SaveStateMutation::ByteReference &
SaveStateMutation::ByteReference::operator=(
  const ByteReference &other)
{
  return *this = static_cast<std::uint8_t>(other);
}

SaveStateMutation::ByteReference &
SaveStateMutation::ByteReference::operator^=(std::uint8_t mask)
{
  owner->xorByte(path, byteOffset, mask);
  return *this;
}

SaveStateMutation::ByteReference &
SaveStateMutation::ByteReference::operator|=(std::uint8_t mask)
{
  owner->writeByte(
    path,
    byteOffset,
    owner->readByte(path, byteOffset) | mask);
  return *this;
}

SaveStateMutation::SaveStateMutation(
  std::vector<std::uint8_t> initialState) :
  state(std::move(initialState)),
  schema(inspectSaveState(state))
{
}

const std::vector<std::uint8_t> &SaveStateMutation::bytes() const
{
  return state;
}

SaveStateMutation::operator const std::vector<std::uint8_t> &() const
{
  return state;
}

SaveStateMutation::ByteReference SaveStateMutation::operator[](
  const std::string &path)
{
  return ByteReference(this, path, 0);
}

void SaveStateMutation::writeScalar(
  const std::string &path,
  std::uint64_t value)
{
  const SaveStateDiagnosticField &target = field(path);
  if (target.kind == SaveStateFieldKind::Bytes ||
      target.size == 0 ||
      target.size > sizeof(value))
  {
    throw std::invalid_argument(
      "Save-state mutation field is not a scalar: " + path);
  }
  if (target.size < sizeof(value) &&
      value >= (UINT64_C(1) << (target.size * 8)))
  {
    throw std::invalid_argument(
      "Save-state mutation value does not fit field: " + path);
  }
  for (std::size_t index = 0; index < target.size; ++index)
  {
    state[target.containerOffset + index] =
      static_cast<std::uint8_t>(value >> (index * 8));
  }
}

void SaveStateMutation::writeByte(
  const std::string &path,
  std::size_t byteOffset,
  std::uint8_t value)
{
  const SaveStateDiagnosticField &target = field(path);
  if (byteOffset >= target.size)
  {
    throw std::out_of_range(
      "Save-state mutation byte is outside field: " + path);
  }
  state[target.containerOffset + byteOffset] = value;
}

void SaveStateMutation::xorByte(
  const std::string &path,
  std::size_t byteOffset,
  std::uint8_t mask)
{
  const SaveStateDiagnosticField &target = field(path);
  if (byteOffset >= target.size)
  {
    throw std::out_of_range(
      "Save-state mutation byte is outside field: " + path);
  }
  state[target.containerOffset + byteOffset] ^= mask;
}

std::uint8_t SaveStateMutation::readByte(
  const std::string &path,
  std::size_t byteOffset) const
{
  const SaveStateDiagnosticField &target = field(path);
  if (byteOffset >= target.size)
  {
    throw std::out_of_range(
      "Save-state mutation byte is outside field: " + path);
  }
  return state[target.containerOffset + byteOffset];
}

void SaveStateMutation::fillPrefix(
  const std::string &pathPrefix,
  std::uint8_t value)
{
  bool found = false;
  for (const SaveStateDiagnosticField &target : schema.fields)
  {
    if (target.path.compare(
          0,
          pathPrefix.size(),
          pathPrefix) == 0)
    {
      std::fill(
        state.begin() + target.containerOffset,
        state.begin() + target.containerOffset + target.size,
        value);
      found = true;
    }
  }
  if (!found)
  {
    throw std::invalid_argument(
      "Save-state mutation prefix not found: " + pathPrefix);
  }
}

void SaveStateMutation::swapFields(
  const std::string &leftPath,
  const std::string &rightPath)
{
  const SaveStateDiagnosticField &left = field(leftPath);
  const SaveStateDiagnosticField &right = field(rightPath);
  if (left.size != right.size)
  {
    throw std::invalid_argument(
      "Save-state mutation fields have different sizes.");
  }
  for (std::size_t index = 0; index < left.size; ++index)
  {
    std::swap(
      state[left.containerOffset + index],
      state[right.containerOffset + index]);
  }
}

void SaveStateMutation::refreshChecksum()
{
  std::size_t payloadOrigin = state.size();
  for (const SaveStateDiagnosticField &candidate : schema.fields)
  {
    if (candidate.hasPayloadOffset)
    {
      payloadOrigin = std::min(
        payloadOrigin,
        candidate.containerOffset - candidate.payloadOffset);
    }
  }
  if (payloadOrigin == state.size())
  {
    throw std::logic_error(
      "Save-state mutation schema has no payload.");
  }

  std::uint64_t checksum = FNV_OFFSET_BASIS;
  for (std::size_t index = payloadOrigin;
       index < state.size();
       ++index)
  {
    checksum ^= state[index];
    checksum *= FNV_PRIME;
  }

  const SaveStateDiagnosticField &checksumField =
    field("container.checksum");
  if (checksumField.size != sizeof(checksum))
  {
    throw std::logic_error(
      "Save-state checksum field has an unexpected size.");
  }
  for (std::size_t index = 0;
       index < checksumField.size;
       ++index)
  {
    state[checksumField.containerOffset + index] =
      static_cast<std::uint8_t>(checksum >> (index * 8));
  }
}

const SaveStateDiagnosticField &SaveStateMutation::field(
  const std::string &path) const
{
  const SaveStateDiagnosticField *result = schema.find(path);
  if (result == nullptr)
  {
    throw std::invalid_argument(
      "Save-state mutation field not found: " + path);
  }
  return *result;
}
