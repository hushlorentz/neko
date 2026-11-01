#ifndef IPU_HPP
#define IPU_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include "ee_types.hpp"

class EEInterruptController;

class IPU
{
  public:
    explicit IPU(EEInterruptController &interrupts);
    std::uint32_t control() const;
    std::uint8_t bitPosition() const;
    std::uint32_t bitPositionStatus() const;
    bool inputWriteReady() const;
    bool submitInput(const EEQuadword &value);
    std::uint64_t stateHash() const;
    bool writeControl(std::uint32_t value);
    bool writeCommand(std::uint32_t value);

  private:
    friend class NekoSaveStateCodec;

    enum class Command : std::uint8_t
    {
      Idle,
      SetIQIntra,
      SetIQNonIntra,
      SetVQ
    };

    struct CommandTable
    {
      std::uint8_t *bytes;
      std::size_t size;
    };

    CommandTable commandTable();
    void clearInput();
    void clearCommand();
    bool bufferStreamBits(std::uint8_t bits);
    void advanceStream(std::uint8_t bits);
    std::uint8_t streamByte(std::size_t index) const;
    bool readStreamByte(std::uint8_t &value);
    void progressCommand();

    EEInterruptController &interruptController;
    std::uint8_t bitPositionRegister = 0;
    std::uint8_t inputFIFOCount = 0;
    std::array<EEQuadword, 8> inputFIFO{};
    std::uint8_t streamBufferCount = 0;
    std::array<EEQuadword, 2> streamBuffer{};
    Command pendingCommand = Command::Idle;
    std::uint8_t forwardBits = 0;
    std::uint8_t tableBytesLoaded = 0;
    std::array<std::uint8_t, 64> intraIQ{};
    std::array<std::uint8_t, 64> nonIntraIQ{};
    std::array<std::uint8_t, 32> vqCLUT{};
    std::uint16_t transparentThreshold = 0;
    std::uint16_t translucentThreshold = 0;
};

#endif
