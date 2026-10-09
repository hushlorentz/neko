#ifndef IOP_TYPES_HPP
#define IOP_TYPES_HPP

#include <cstdint>

using IOPWord = std::uint32_t;
using IOPSignedWord = std::int32_t;
using IOPAddress = std::uint32_t;
using IOPCycleCount = std::uint64_t;

enum class IOPExecutionState : std::uint8_t
{
  Halted,
  Running
};

enum class IOPStopReason : std::uint8_t
{
  None,
  HostHalt,
  FetchFailure,
  ReservedInstruction,
  ExecutionException
};

enum class IOPException : std::uint8_t
{
  None,
  Interrupt,
  AddressErrorLoadOrFetch,
  AddressErrorStore,
  InstructionBusError,
  DataBusError,
  SystemCall,
  Breakpoint,
  ReservedInstruction,
  CoprocessorUnusable,
  ArithmeticOverflow
};

enum class IOPDelayedResultSource : std::uint8_t
{
  None,
  Load,
  COP0Transfer
};

struct IOPBranchContinuation
{
  bool active = false;
  IOPAddress instructionAddress = 0;
  IOPAddress targetAddress = 0;
};

struct IOPDelayedResultContinuation
{
  IOPDelayedResultSource source =
    IOPDelayedResultSource::None;
  std::uint8_t destination = 0;
  IOPWord value = 0;
};

struct IOPPendingException
{
  IOPException kind = IOPException::None;
  IOPAddress badVirtualAddress = 0;
  bool inBranchDelaySlot = false;
  std::uint8_t coprocessor = 0;
};

struct IOPCOP0State
{
  IOPAddress badVirtualAddress = 0;
  IOPWord status = 0;
  IOPWord cause = 0;
  IOPAddress exceptionProgramCounter = 0;
};

namespace IOPCOP0
{
  constexpr IOPWord PROCESSOR_REVISION =
    UINT32_C(0x0000001f);
}

namespace IOPCOP0Status
{
  constexpr IOPWord TLB_SHUTDOWN =
    UINT32_C(1) << 21;
  constexpr IOPWord BOOT_EXCEPTION_VECTORS =
    UINT32_C(1) << 22;
}

namespace IOPReset
{
  constexpr IOPAddress VECTOR = UINT32_C(0xbfc00000);
}

#endif
