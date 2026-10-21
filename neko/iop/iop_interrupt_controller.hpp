#ifndef IOP_INTERRUPT_CONTROLLER_HPP
#define IOP_INTERRUPT_CONTROLLER_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "iop_types.hpp"
#include "observation_order.hpp"

enum class IOPInterruptSource : std::uint8_t
{
  VBlankStart = 0,
  SBUS = 1,
  CDVD = 2,
  DMAC = 3,
  Timer0 = 4,
  Timer1 = 5,
  Timer2 = 6,
  SIO0 = 7,
  SIO1 = 8,
  SPU = 9,
  PIO = 10,
  VBlankEnd = 11,
  DVD = 12,
  DEV9 = 13,
  Timer3 = 14,
  Timer4 = 15,
  Timer5 = 16,
  SIO2 = 17,
  HTR0 = 18,
  HTR1 = 19,
  HTR2 = 20,
  HTR3 = 21,
  USB = 22,
  Expansion = 23,
  ILink = 24,
  FireWireDMA = 25
};

class IOPInterruptController final
{
  public:
    static constexpr std::uint8_t SOURCE_COUNT = 26;
    static constexpr IOPWord SOURCE_MASK = UINT32_C(0x03ffffff);

    void reset();
    void assertSource(IOPInterruptSource source);
    void writeStatus(IOPWord value);
    void writeMask(IOPWord value);
    IOPWord readControl();
    void writeControl(IOPWord value);

    // Inspection is side-effect free, unlike the guest control-register read.
    IOPWord status() const;
    IOPWord mask() const;
    IOPWord control() const;
    bool interruptPending() const;

  private:
    friend class NekoSystem;
    friend class NekoSaveStateCodec;
    friend struct IOPDeviceObservationTestAccess;

    struct Observation
    {
      IOPWord status = 0;
      IOPWord mask = 0;
      IOPWord control = 0;
      std::size_t sequence = 0;
    };

    // Six timers, an EE SBUS request, one DMA completion, and one IOP INTC transition.
    // A side-effecting INTC_CTRL fetch cannot decode to a memory instruction.
    static constexpr std::size_t OBSERVATION_CAPACITY = 9;
    std::array<Observation, OBSERVATION_CAPACITY> observations = {};
    std::size_t observationCount = 0;
    bool observationsEnabled = false;
    NekoObservationOrder *observationOrder = nullptr;

    void enableObservations(NekoObservationOrder *order = nullptr);
    void disableObservations();
    void clearObservations();
    Observation snapshot() const;
    void recordTransition(const Observation &before);

    IOPWord statusRegister = 0;
    IOPWord maskRegister = 0;
    IOPWord controlRegister = 0;
};

#endif
