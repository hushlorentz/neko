#pragma once

#include "save_state_internal.hpp"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

struct SaveStateDiagnosticOptions
{
  std::vector<std::string> retainedBytePaths;
};

struct SaveStateDiagnosticField
{
  std::string path;
  SaveStateFieldKind kind = SaveStateFieldKind::U8;
  std::size_t containerOffset = 0;
  bool hasPayloadOffset = false;
  std::size_t payloadOffset = 0;
  std::size_t size = 0;
  std::uint64_t scalarValue = 0;
  std::uint64_t byteHash = SAVE_STATE_FNV_OFFSET_BASIS;
  std::shared_ptr<const std::vector<std::uint8_t>> bytes;
};

struct SaveStateDiagnosticSnapshot
{
  std::vector<SaveStateDiagnosticField> fields;

  const SaveStateDiagnosticField *find(
    const std::string &path) const;
};

enum class SaveStateDifferenceKind : std::uint8_t
{
  Added,
  Removed,
  Metadata,
  Value
};

struct SaveStateDifference
{
  std::string path;
  SaveStateDifferenceKind kind = SaveStateDifferenceKind::Value;
  bool hasLeft = false;
  SaveStateDiagnosticField left;
  bool hasRight = false;
  SaveStateDiagnosticField right;
};

SaveStateDiagnosticSnapshot inspectSaveState(
  const std::vector<std::uint8_t> &state,
  const SaveStateDiagnosticOptions &options = {});

std::vector<SaveStateDifference> diffSaveStates(
  const SaveStateDiagnosticSnapshot &left,
  const SaveStateDiagnosticSnapshot &right);

std::string formatSaveStateField(
  const SaveStateDiagnosticField &field);

void writeSaveStateField(
  std::ostream &output,
  const SaveStateDiagnosticField &field);

std::string formatSaveStateDifference(
  const SaveStateDifference &difference);

void writeSaveStateDifference(
  std::ostream &output,
  const SaveStateDifference &difference);
