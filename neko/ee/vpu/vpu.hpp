#ifndef VPU_HPP
#define VPU_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <vector>

#include "clocked_component.hpp"
#include "fp_register.hpp"
#include "vpu_lower_instruction.hpp"
#include "vpu_pipeline_handler.hpp"
#include "vpu_pipeline_orchestrator.hpp"
#include "vpu_xgkick_handler.hpp"

#define VPU_STATE_READY 1
#define VPU_STATE_RUN 2
#define VPU_STATE_STOP 3
#define VPU_MODE_MICRO 1
#define VPU_MODE_MACRO 2

using namespace std;

enum class VPUType : uint8_t
{
  VU0,
  VU1
};

enum class VPUTraceEventType : uint8_t
{
  InstructionIssued,
  PipelineStall,
  PipelineWriteback,
  ForceBreak
};

struct VPUArithmeticLaneTrace
{
  uint32_t multiplyBits = 0;
  uint8_t multiplyFlags = 0;
  uint32_t accumulatorBits = 0;
  uint32_t resultBits = 0;
  uint8_t resultFlags = 0;
};

struct VPUArithmeticTrace
{
  bool present = false;
  array<VPUArithmeticLaneTrace, 4> lanes = {};
  uint8_t ignoredResultFields = FP_REGISTER_NO_FIELDS;
};

struct VPUTraceEvent
{
  VPUTraceEventType type;
  uint32_t cycle;
  uint16_t instructionAddress;
  uint32_t upperInstruction;
  uint32_t lowerInstruction;
  uint16_t opCode;
  uint8_t destinationRegister;
  uint8_t destinationFieldMask;
  VPUArithmeticTrace arithmetic;
};

using VPUTraceCallback = function<void(const VPUTraceEvent &)>;

class VPU : public ClockedComponent, public PipelineHandler
{
  public:
    explicit VPU(VPUType type = VPUType::VU0);
    FPRegister accumulator;
    uint64_t clippingFlags = 0;

    VPUType unitType() const;
    size_t microMemorySize() const;
    size_t dataMemorySize() const;
    uint8_t getState() const;
    uint16_t programCounter() const;
    bool hasTerminationPosition() const;
    uint16_t terminationPosition() const;
    bool dBitEnabled() const;
    bool tBitEnabled() const;
    void setDBitEnabled(bool enabled);
    void setTBitEnabled(bool enabled);
    void forceBreak();
    const FPRegister *fpRegisterValue(int registerID) const;
    uint16_t intRegisterValue(int registerID) const;
    void loadFPRegister(int registerID, double x, double y, double z, double w);
    void loadIntFPRegister(int registerID, int32_t x, int32_t y, int32_t z, int32_t w);
    void loadIntRegister(int registerID, int value);
    uint32_t elapsedCycles() const;
    void resetCycles();
    void setMode(uint8_t newMode);
    void initMicroMode();
    void startMicroMode(uint16_t startAddress = 0);
    bool clockActive() const override;
    void clock() override;
    bool tick();
    bool stepInstruction();
    uint32_t run(uint32_t maxCycles);
    void setTraceCallback(VPUTraceCallback callback);
    void setXGKICKHandler(VUXGKICKHandler *handler);
    void uploadMicroInstructions(const vector<uint8_t> &instructions);
    void writeDataMemory(size_t address, const vector<uint8_t> &data);
    vector<uint8_t> readDataMemory(size_t address, size_t byteCount) const;
    void pipelineStarted(Pipeline *p) override;
    bool pipelineCanAdvance(Pipeline *pipeline) override;
    void pipelineAdvanced(Pipeline *p) override;
    void pipelineFinished(Pipeline *p) override;
    bool hasMACFlag(uint16_t flag);
    bool hasStatusFlag(uint16_t flag);
    uint32_t qRegisterBits() const;
    uint32_t rRegisterBits() const;
    void loadIRegister(double value);
    void loadQRegister(double value);
    void loadAccumulator(double x, double y, double z, double w);
  private:
    VPUType type;
    vector<uint8_t> microMem;
    vector<uint8_t> vuMem;
    uint8_t state = VPU_STATE_READY;
    uint32_t cycles = 0;
    uint8_t mode = VPU_MODE_MACRO;
    uint16_t microMemPC = 0;
    uint16_t terminationPositionCounter = 0;
    bool terminationPositionValid = false;
    bool endDelaySlotPending = false;
    bool branchDelaySlotPending = false;
    bool pendingBranchTaken = false;
    uint16_t pendingBranchTarget = 0;
    bool pendingBranchLinkValid = false;
    uint8_t pendingBranchLinkRegister = 0;
    uint16_t pendingBranchLinkValue = 0;
    bool terminationRequested = false;
    bool haltAfterDrain = false;
    bool dEnabled = false;
    bool tEnabled = false;
    VPUTraceCallback traceCallback;
    VUXGKICKHandler *xgkickHandler = nullptr;
    bool xgkickWaiting = false;
    bool xgkickTransferStarted = false;
    vector<FPRegister> fpRegisters;
    vector<uint16_t> intRegisters;
    VUFloat iRegister;
    VUFloat qRegister;
    double pRegister = 0;
    uint32_t rRegister = 0;
    uint16_t MACFlags = 0;
    uint16_t statusFlags = 0;
    set<uint16_t> type0OpCodes;
    set<uint16_t> type1OpCodes;
    set<uint16_t> type2OpCodes;
    set<uint16_t> type3OpCodes;
    PipelineOrchestrator orchestrator;
    FPRegister virtualDestRegister;
    FPRegister accumulatorForwardValue;
    uint8_t pendingAccumulatorWrites = 0;
    bool accumulatorForwardValid = false;
    LowerInstruction pendingLowerInstruction;
    uint16_t pendingLowerInstructionAddress = 0;
    bool lowerInstructionPending = false;
    bool pendingLowerInstructionReady = false;
    bool pendingLowerWritebackDiscarded = false;
    array<uint8_t, 16> pendingIntegerWrites = {};
    array<uint8_t, 16> pendingIALUWrites = {};
    array<uint16_t, 16> bypassedIntegerValues = {};

    void initMemory();
    void initFPRegisters();
    void initIntRegisters();
    void initOpCodeSets();
    void initPipelineOrchestrator();
    void executeMicroInstructions();
    void emitTrace(const VPUTraceEvent &event) const;
    bool endBitSet(uint32_t instruction);
    bool haltBitSet(uint32_t instruction);
    uint32_t nextUpperInstruction();
    uint32_t nextLowerInstruction();
    uint16_t processUpperInstruction(uint32_t upperInstruction);
    uint16_t opCodeFromInstruction(uint32_t instruction);
    uint8_t regFromInstruction(uint32_t instruction, uint8_t shift);
    uint8_t src1RegFromOpCodeAndInstruction(uint16_t opCode, uint32_t instruction);
    uint8_t destRegFromOpCodeAndInstruction(uint16_t opCode, uint32_t instruction);
    uint8_t destinationMaskFromOpCode(uint16_t opCode, uint8_t encodedMask);
    uint8_t srcReg1MaskFromOpCode(uint16_t opCode, uint8_t destinationMask);
    uint8_t srcReg2MaskFromOpCode(uint16_t opCode, uint8_t destinationMask);
    void queueLowerInstruction(const LowerInstruction &lowerInstruction, uint16_t upperOpCode, uint32_t upperInstruction, uint16_t instructionAddress);
    void executePendingLowerInstruction();
    void startIRegisterInstruction(const LowerInstruction &instruction);
    void completeBranchDelaySlot();
    void startIALUInstruction(const LowerInstruction &instruction);
    void startBranchInstruction(const LowerInstruction &instruction);
    void startLSUInstruction(const LowerInstruction &instruction);
    void startLowerFMACInstruction(const LowerInstruction &instruction);
    void startFDIVInstruction(const LowerInstruction &instruction);
    void startWaitQInstruction(const LowerInstruction &instruction);
    void startFlagInstruction(const LowerInstruction &instruction);
    void startRandomInstruction(const LowerInstruction &instruction);
    void startXGKICKInstruction(const LowerInstruction &instruction);
    bool startXGKICKTransfer(Pipeline *pipeline);
    bool xgkickStallsIssue();
    uint16_t integerValueForExecution(uint8_t registerID) const;
    uint16_t integerSourceValueForPipeline(
      const Pipeline *pipeline,
      uint8_t registerID) const;
    bool hasPendingIntegerWrite(uint8_t registerID) const;
    bool lowerInstructionStalls(const LowerInstruction &instruction) const;
    bool lowerInstructionForbiddenInEndDelaySlot(const LowerInstruction &instruction) const;
    uint16_t qwordAddress(uint16_t base, int16_t offset = 0) const;
    uint32_t readDataWord(uint16_t address) const;
    void writeDataWord(uint16_t address, uint32_t value);
    void startIALUPipeline(Pipeline *pipeline);
    void executeIALUPipeline(Pipeline *pipeline);
    void finishIALUPipeline(Pipeline *pipeline);
    void startLSUPipeline(Pipeline *pipeline);
    void finishLSUPipeline(Pipeline *pipeline);
    void startFMACPipeline(Pipeline *pipeline);
    void executeRandomPipeline(Pipeline *pipeline);
    void executeFDIVPipeline(Pipeline *pipeline);
    void finishFDIVPipeline(Pipeline *pipeline);
    void prepareAccumulatorOperation(Pipeline *pipeline);
    uint8_t multiplicationOverflowFields(
      const Pipeline *pipeline) const;
    void evaluateBranchPipeline(Pipeline *pipeline);
    void setFlags(FPRegister * reg, uint8_t ignoredFields);
    void setMACFlagsFromRegister(FPRegister * reg, uint8_t ignoredFields);
    void setStatusFlagsFromMACFlags();
    void setStickyFlagsFromStatusFlags();
    void updateDestinationRegisterWithPipelineResult(FPRegister * destReg, Pipeline * p);
    void updateClippingFlags(uint32_t clip);
    int calculateNewClippingFlags(FPRegister * fsReg, FPRegister * ftReg);
    FPRegister * destinationRegisterFromPipeline(Pipeline * p);
    void handleMADDInstruction(Pipeline * p);
    void handleMSUBInstruction(Pipeline * p);
    void handleOPMSUBInstruction(Pipeline * p);
    void finishAccumulatorWrite(Pipeline *pipeline);
};

#endif
