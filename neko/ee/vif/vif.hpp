#ifndef VIF_H
#define VIF_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "gif_path_arbiter.hpp"
#include "vif_command.hpp"
#include "vpu_vif_register_source.hpp"

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
  bool stalled = false;
  bool gifQuadwordDecoded = false;
  GIFDecodeResult gifResult;
};

class VIF : public VUVIFRegisterSource
{
  public:
    explicit VIF(VIFType type);

    VIFType unitType() const;
    void attachVPU(VPU *attachedVPU);
    void attachGIFDecoder(GIFDecoder *attachedGIFDecoder);
    void attachGIFPathArbiter(GIFPathArbiter *attachedArbiter);
    VIFCommand processCode(std::uint32_t code);
    VIFStreamWord ingestWord(std::uint32_t word);

    std::uint16_t cycle() const;
    std::uint8_t cycleLength() const;
    std::uint8_t writeLength() const;
    std::uint8_t mode() const;
    std::uint32_t mask() const;
    std::uint32_t row(std::size_t index) const;
    std::uint32_t column(std::size_t index) const;
    std::uint16_t top() const override;
    std::uint16_t itop() const override;
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
    bool interruptPending() const;
    void clearInterrupt();
    std::uint32_t payloadWordsRemaining() const;
    std::uint64_t wordsIngested() const;

  private:
    friend class NekoSaveStateCodec;

    std::uint32_t payloadWordCount(const VIFCommand &command) const;
    void validatePayloadAlignment(const VIFCommand &command) const;
    void preparePayload(const VIFCommand &command);
    bool consumePayloadWord(
      std::uint32_t word,
      VIFStreamWord *streamWord);
    bool commandReady(const VIFCommand &command) const;
    void completePayloadCommand();
    void startMicroProgram(const VIFCommand &command);
    void updateProgramStartRegisters();
    void executeUNPACK();
    std::uint32_t unpackElement(
      std::uint32_t bitOffset,
      std::uint8_t bitCount) const;
    std::array<std::uint32_t, 4> unpackInputVector(
      std::uint32_t inputVectorIndex) const;

    VIFType type;
    VPU *vpu = nullptr;
    std::unique_ptr<GIFPathArbiter> ownedGIFPathArbiter;
    GIFPathArbiter *gifPathArbiter = nullptr;
    std::uint16_t cycleRegister = 0;
    std::uint8_t modeRegister = 0;
    std::uint32_t maskRegister = 0;
    std::array<std::uint32_t, 4> rowRegisters = {};
    std::array<std::uint32_t, 4> columnRegisters = {};
    std::uint16_t topRegister = 0;
    std::uint16_t itopRegister = 0;
    std::uint16_t itopsRegister = 0;
    std::uint16_t baseRegister = 0;
    std::uint16_t offsetRegister = 0;
    std::uint16_t topsRegister = 0;
    std::uint16_t markRegister = 0;
    bool dbf = false;
    bool path3Mask = false;
    bool markFlag = false;
    bool interruptFlag = false;
    std::uint32_t codeRegister = 0;
    VIFCommand streamCommand;
    std::uint32_t streamPayloadWordCount = 0;
    std::uint32_t streamPayloadWordsRemaining = 0;
    std::uint64_t streamWordsIngested = 0;
    std::uint32_t mpgLowerInstruction = 0;
    bool mpgLowerInstructionPending = false;
    GIFQuadword directQuadword = {};
    std::vector<std::uint32_t> unpackPayload;
};

#endif
