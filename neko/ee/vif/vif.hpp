#ifndef VIF_H
#define VIF_H

#include <cstdint>

#include "vif_command.hpp"

class VPU;

enum class VIFStreamWordKind : std::uint8_t
{
  Command,
  Payload
};

struct VIFStreamWord
{
  VIFStreamWordKind kind = VIFStreamWordKind::Command;
  VIFCommand command;
  std::uint32_t raw = 0;
  std::uint32_t payloadIndex = 0;
  std::uint32_t payloadWordCount = 0;
  bool packetComplete = false;
};

class VIF
{
  public:
    explicit VIF(VIFType type);

    VIFType unitType() const;
    void attachVPU(VPU *attachedVPU);
    VIFCommand processCode(std::uint32_t code);
    VIFStreamWord ingestWord(std::uint32_t word);

    std::uint16_t cycle() const;
    std::uint8_t cycleLength() const;
    std::uint8_t writeLength() const;
    std::uint8_t mode() const;
    std::uint16_t itops() const;
    std::uint16_t base() const;
    std::uint16_t offset() const;
    std::uint16_t tops() const;
    std::uint16_t mark() const;
    bool doubleBufferFlag() const;
    bool path3Masked() const;
    bool markDetected() const;
    std::uint32_t lastCode() const;
    bool awaitingPayload() const;
    std::uint32_t payloadWordsRemaining() const;
    std::uint64_t wordsIngested() const;

  private:
    std::uint32_t payloadWordCount(const VIFCommand &command) const;
    void validatePayloadAlignment(const VIFCommand &command) const;
    void preparePayload(const VIFCommand &command);
    void consumePayloadWord(std::uint32_t word);

    VIFType type;
    VPU *vpu = nullptr;
    std::uint16_t cycleRegister = 0;
    std::uint8_t modeRegister = 0;
    std::uint16_t itopsRegister = 0;
    std::uint16_t baseRegister = 0;
    std::uint16_t offsetRegister = 0;
    std::uint16_t topsRegister = 0;
    std::uint16_t markRegister = 0;
    bool dbf = false;
    bool path3Mask = false;
    bool markFlag = false;
    std::uint32_t codeRegister = 0;
    VIFCommand streamCommand;
    std::uint32_t streamPayloadWordCount = 0;
    std::uint32_t streamPayloadWordsRemaining = 0;
    std::uint64_t streamWordsIngested = 0;
    std::uint32_t mpgLowerInstruction = 0;
    bool mpgLowerInstructionPending = false;
};

#endif
