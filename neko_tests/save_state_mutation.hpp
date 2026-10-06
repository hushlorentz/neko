#pragma once

#include "save_state_diagnostics.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class SaveStateMutation
{
  public:
    class ByteReference
    {
      public:
        ByteReference(
          SaveStateMutation *owner,
          std::string path,
          std::size_t byteOffset);

        operator std::uint8_t() const;
        ByteReference &operator=(std::uint8_t value);
        ByteReference &operator=(const ByteReference &other);
        ByteReference &operator^=(std::uint8_t mask);
        ByteReference &operator|=(std::uint8_t mask);

      private:
        SaveStateMutation *owner;
        std::string path;
        std::size_t byteOffset;
    };

    explicit SaveStateMutation(std::vector<std::uint8_t> state);

    const std::vector<std::uint8_t> &bytes() const;
    operator const std::vector<std::uint8_t> &() const;

    ByteReference operator[](const std::string &path);

    void writeScalar(
      const std::string &path,
      std::uint64_t value);
    void writeByte(
      const std::string &path,
      std::size_t byteOffset,
      std::uint8_t value);
    void xorByte(
      const std::string &path,
      std::size_t byteOffset,
      std::uint8_t mask);
    std::uint8_t readByte(
      const std::string &path,
      std::size_t byteOffset = 0) const;
    void fillPrefix(
      const std::string &pathPrefix,
      std::uint8_t value);
    void swapFields(
      const std::string &leftPath,
      const std::string &rightPath);
    void refreshChecksum();

  private:
    const SaveStateDiagnosticField &field(
      const std::string &path) const;

    std::vector<std::uint8_t> state;
    SaveStateDiagnosticSnapshot schema;
};
