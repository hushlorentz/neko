#include "save_state_diagnostics.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace
{
  class StreamFormatGuard
  {
    public:
      explicit StreamFormatGuard(std::ostream &stream) :
        output(stream),
        flags(stream.flags()),
        precision(stream.precision()),
        width(stream.width()),
        fill(stream.fill())
      {
        output.setf(std::ios::dec, std::ios::basefield);
        output.unsetf(std::ios::showbase | std::ios::showpos);
        output.width(0);
      }

      ~StreamFormatGuard()
      {
        output.flags(flags);
        output.precision(precision);
        output.width(width);
        output.fill(fill);
      }

    private:
      std::ostream &output;
      std::ios::fmtflags flags;
      std::streamsize precision;
      std::streamsize width;
      char fill;
  };

  bool retainsBytes(
    const SaveStateDiagnosticOptions &options,
    const std::string &path)
  {
    return std::find(
      options.retainedBytePaths.begin(),
      options.retainedBytePaths.end(),
      path) != options.retainedBytePaths.end();
  }

  const char *kindName(SaveStateFieldKind kind)
  {
    switch (kind)
    {
      case SaveStateFieldKind::U8:
        return "u8";
      case SaveStateFieldKind::U16:
        return "u16";
      case SaveStateFieldKind::U32:
        return "u32";
      case SaveStateFieldKind::U64:
        return "u64";
      case SaveStateFieldKind::Boolean:
        return "bool";
      case SaveStateFieldKind::Bytes:
        return "bytes";
    }
    return "unknown";
  }

  std::size_t scalarHexWidth(SaveStateFieldKind kind)
  {
    switch (kind)
    {
      case SaveStateFieldKind::U8:
        return 2;
      case SaveStateFieldKind::U16:
        return 4;
      case SaveStateFieldKind::U32:
        return 8;
      case SaveStateFieldKind::U64:
        return 16;
      case SaveStateFieldKind::Boolean:
      case SaveStateFieldKind::Bytes:
        return 0;
    }
    return 0;
  }

  std::string formatHex(
    std::uint64_t value,
    std::size_t width)
  {
    std::ostringstream output;
    output << "0x"
           << std::hex
           << std::nouppercase
           << std::setfill('0')
           << std::setw(static_cast<int>(width))
           << value;
    return output.str();
  }

  void writeBytes(
    std::ostream &output,
    const std::vector<std::uint8_t> &bytes)
  {
    static const char DIGITS[] = "0123456789abcdef";
    for (std::uint8_t value : bytes)
    {
      output.put(DIGITS[value >> 4]);
      output.put(DIGITS[value & 0x0f]);
    }
  }

  void writeValue(
    std::ostream &output,
    const SaveStateDiagnosticField &field)
  {
    if (field.kind == SaveStateFieldKind::Boolean)
    {
      output << (field.scalarValue == 0 ? "false" : "true");
      return;
    }
    if (field.kind == SaveStateFieldKind::Bytes)
    {
      output <<
        "size=" << field.size <<
        " fnv1a64=" << formatHex(field.byteHash, 16);
      if (field.bytes != nullptr)
      {
        output << " data=";
        writeBytes(output, *field.bytes);
      }
      return;
    }
    output << formatHex(
      field.scalarValue,
      scalarHexWidth(field.kind));
  }

  std::string formatValue(
    const SaveStateDiagnosticField &field)
  {
    std::ostringstream output;
    writeValue(output, field);
    return output.str();
  }

  bool metadataEqual(
    const SaveStateDiagnosticField &left,
    const SaveStateDiagnosticField &right)
  {
    return
      left.kind == right.kind &&
      left.containerOffset == right.containerOffset &&
      left.hasPayloadOffset == right.hasPayloadOffset &&
      left.payloadOffset == right.payloadOffset &&
      left.size == right.size;
  }

  bool valueEqual(
    const SaveStateDiagnosticField &left,
    const SaveStateDiagnosticField &right)
  {
    if (left.kind == SaveStateFieldKind::Bytes)
    {
      if (left.bytes != nullptr && right.bytes != nullptr)
      {
        return *left.bytes == *right.bytes;
      }
      return left.byteHash == right.byteHash;
    }
    return left.scalarValue == right.scalarValue;
  }

  bool isDerivedContainerField(const std::string &path)
  {
    return path == "container.checksum";
  }

  class RetainingObserver final : public SaveStateObserver
  {
    public:
      explicit RetainingObserver(
        const SaveStateDiagnosticOptions &retentionOptions) :
        options(retentionOptions)
      {
      }

      void observe(
        const SaveStateFieldObservation &field) override
      {
        SaveStateDiagnosticField retained;
        retained.path = field.path;
        retained.kind = field.kind;
        retained.containerOffset = field.containerOffset;
        retained.hasPayloadOffset = field.hasPayloadOffset;
        retained.payloadOffset = field.payloadOffset;
        retained.size = field.size;
        retained.scalarValue = field.scalarValue;
        for (std::size_t index = 0;
             index < field.size && field.bytes != nullptr;
             ++index)
        {
          retained.byteHash ^= field.bytes[index];
          retained.byteHash *= SAVE_STATE_FNV_PRIME;
        }
        if (field.kind == SaveStateFieldKind::Bytes &&
            retainsBytes(options, field.path))
        {
          auto bytes =
            std::make_shared<std::vector<std::uint8_t>>();
          if (field.bytes != nullptr)
          {
            bytes->assign(
              field.bytes,
              field.bytes + field.size);
          }
          retained.bytes = std::move(bytes);
        }
        fields.push_back(std::move(retained));
      }

      const SaveStateDiagnosticOptions &options;
      std::vector<SaveStateDiagnosticField> fields;
  };
}

const SaveStateDiagnosticField *SaveStateDiagnosticSnapshot::find(
  const std::string &path) const
{
  const auto field = std::find_if(
    fields.begin(),
    fields.end(),
    [&](const SaveStateDiagnosticField &candidate)
    {
      return candidate.path == path;
    });
  return field == fields.end() ? nullptr : &*field;
}

SaveStateDiagnosticSnapshot inspectSaveState(
  const std::vector<std::uint8_t> &state,
  const SaveStateDiagnosticOptions &options)
{
  RetainingObserver observer(options);
  NekoSystem system;
  NekoSaveStateCodec::load(&system, state, &observer);
  std::stable_sort(
    observer.fields.begin(),
    observer.fields.end(),
    [](const SaveStateDiagnosticField &left,
       const SaveStateDiagnosticField &right)
    {
      return left.containerOffset < right.containerOffset;
    });

  SaveStateDiagnosticSnapshot snapshot;
  snapshot.fields.swap(observer.fields);
  for (const std::string &path : options.retainedBytePaths)
  {
    const SaveStateDiagnosticField *field = snapshot.find(path);
    if (field == nullptr)
    {
      throw std::invalid_argument(
        "Save-state byte field not found: " + path);
    }
    if (field->kind != SaveStateFieldKind::Bytes)
    {
      throw std::invalid_argument(
        "Save-state field is not a byte range: " + path);
    }
  }
  return snapshot;
}

std::vector<SaveStateDifference> diffSaveStates(
  const SaveStateDiagnosticSnapshot &left,
  const SaveStateDiagnosticSnapshot &right)
{
  std::vector<const SaveStateDiagnosticField *> leftFields;
  std::vector<const SaveStateDiagnosticField *> rightFields;
  for (const SaveStateDiagnosticField &field : left.fields)
  {
    if (!isDerivedContainerField(field.path))
    {
      leftFields.push_back(&field);
    }
  }
  for (const SaveStateDiagnosticField &field : right.fields)
  {
    if (!isDerivedContainerField(field.path))
    {
      rightFields.push_back(&field);
    }
  }
  const auto byPath =
    [](const SaveStateDiagnosticField *leftField,
       const SaveStateDiagnosticField *rightField)
    {
      return leftField->path < rightField->path;
    };
  std::sort(leftFields.begin(), leftFields.end(), byPath);
  std::sort(rightFields.begin(), rightFields.end(), byPath);

  std::vector<SaveStateDifference> result;
  std::size_t leftIndex = 0;
  std::size_t rightIndex = 0;
  while (leftIndex < leftFields.size() ||
         rightIndex < rightFields.size())
  {
    if (rightIndex == rightFields.size() ||
        (leftIndex < leftFields.size() &&
         leftFields[leftIndex]->path <
           rightFields[rightIndex]->path))
    {
      SaveStateDifference difference;
      difference.path = leftFields[leftIndex]->path;
      difference.kind = SaveStateDifferenceKind::Removed;
      difference.hasLeft = true;
      difference.left = *leftFields[leftIndex++];
      result.push_back(std::move(difference));
      continue;
    }
    if (leftIndex == leftFields.size() ||
        rightFields[rightIndex]->path <
          leftFields[leftIndex]->path)
    {
      SaveStateDifference difference;
      difference.path = rightFields[rightIndex]->path;
      difference.kind = SaveStateDifferenceKind::Added;
      difference.hasRight = true;
      difference.right = *rightFields[rightIndex++];
      result.push_back(std::move(difference));
      continue;
    }

    const SaveStateDiagnosticField &leftField =
      *leftFields[leftIndex++];
    const SaveStateDiagnosticField &rightField =
      *rightFields[rightIndex++];
    if (!metadataEqual(leftField, rightField) ||
        !valueEqual(leftField, rightField))
    {
      SaveStateDifference difference;
      difference.path = leftField.path;
      difference.kind =
        metadataEqual(leftField, rightField)
          ? SaveStateDifferenceKind::Value
          : SaveStateDifferenceKind::Metadata;
      difference.hasLeft = true;
      difference.left = leftField;
      difference.hasRight = true;
      difference.right = rightField;
      result.push_back(std::move(difference));
    }
  }
  return result;
}

std::string formatSaveStateField(
  const SaveStateDiagnosticField &field)
{
  std::ostringstream output;
  writeSaveStateField(output, field);
  return output.str();
}

void writeSaveStateField(
  std::ostream &output,
  const SaveStateDiagnosticField &field)
{
  const StreamFormatGuard streamFormat(output);
  output <<
    field.path << ' ' <<
    kindName(field.kind) << ' ';
  writeValue(output, field);
  output <<
    " container=" <<
    field.containerOffset << ".." <<
    field.containerOffset + field.size <<
    " payload=";
  if (field.hasPayloadOffset)
  {
    output <<
      field.payloadOffset << ".." <<
      field.payloadOffset + field.size;
  }
  else
  {
    output << "n/a";
  }
}

std::string formatSaveStateDifference(
  const SaveStateDifference &difference)
{
  std::ostringstream output;
  writeSaveStateDifference(output, difference);
  return output.str();
}

void writeSaveStateDifference(
  std::ostream &output,
  const SaveStateDifference &difference)
{
  const StreamFormatGuard streamFormat(output);
  switch (difference.kind)
  {
    case SaveStateDifferenceKind::Added:
      output << "added " << difference.path << ": ";
      writeValue(output, difference.right);
      return;
    case SaveStateDifferenceKind::Removed:
      output << "removed " << difference.path << ": ";
      writeValue(output, difference.left);
      return;
    case SaveStateDifferenceKind::Metadata:
      output << "metadata changed " << difference.path << ": ";
      writeSaveStateField(output, difference.left);
      output << " -> ";
      writeSaveStateField(output, difference.right);
      return;
    case SaveStateDifferenceKind::Value:
      output << "changed " << difference.path << ": ";
      writeValue(output, difference.left);
      output << " -> ";
      writeValue(output, difference.right);
      return;
  }
}
