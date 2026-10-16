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
  UndefinedOperation,
  ExecutionException,
  StoreFailure
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

enum class IOPInterruptInput : std::uint8_t
{
  Hardware0,
  Hardware1,
  Hardware2,
  Hardware3,
  Hardware4,
  Hardware5
};

enum class IOPAddressOutcome : std::uint8_t
{
  Translated,
  ProtectionFailure
};

enum class IOPCacheRoute : std::uint8_t
{
  None,
  Cached,
  Uncached
};

enum class IOPAccessOutcome : std::uint8_t
{
  Completed,
  ProtectionFailure,
  Misaligned,
  ReadOnly,
  Unmapped
};

struct IOPAddressClassification
{
  IOPAddressOutcome outcome =
    IOPAddressOutcome::ProtectionFailure;
  IOPAddress virtualAddress = 0;
  IOPAddress physicalAddress = 0;
  IOPCacheRoute cacheRoute = IOPCacheRoute::None;
};

struct IOPMemoryReadResult
{
  IOPAccessOutcome outcome = IOPAccessOutcome::Unmapped;
  IOPException exception = IOPException::None;
  IOPAddress virtualAddress = 0;
  IOPAddress physicalAddress = 0;
  IOPCacheRoute cacheRoute = IOPCacheRoute::None;
  IOPWord value = 0;
};

struct IOPMemoryWriteResult
{
  IOPAccessOutcome outcome = IOPAccessOutcome::Unmapped;
  IOPException exception = IOPException::None;
  IOPAddress virtualAddress = 0;
  IOPAddress physicalAddress = 0;
  IOPCacheRoute cacheRoute = IOPCacheRoute::None;
};

enum class IOPStoreWidth : std::uint8_t
{
  None = 0,
  Byte = 1,
  Halfword = 2,
  Word = 4
};

struct IOPStoreFailure
{
  // None means no rejected physical write has been recorded.
  IOPStoreWidth width = IOPStoreWidth::None;
  IOPWord value = 0;
  IOPMemoryWriteResult access;
  std::uint8_t mergeByteEnable = 0;
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

struct IOPExceptionRecord
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
  constexpr IOPWord INTERRUPT_ENABLE = UINT32_C(1);
  constexpr IOPWord INTERRUPT_MASK = UINT32_C(0x0000ff00);
  constexpr IOPWord COP0_USABLE =
    UINT32_C(1) << 28;
  constexpr IOPWord CURRENT_USER_MODE =
    UINT32_C(1) << 1;
  constexpr IOPWord CACHE_PROBE_RESULT =
    UINT32_C(1) << 19;
  constexpr IOPWord PARITY_ERROR =
    UINT32_C(1) << 20;
  constexpr IOPWord TLB_SHUTDOWN =
    UINT32_C(1) << 21;
  constexpr IOPWord BOOT_EXCEPTION_VECTORS =
    UINT32_C(1) << 22;
  constexpr IOPWord READ_MASK = UINT32_C(0xf27fff3f);
  constexpr IOPWord WRITE_MASK = UINT32_C(0xf247ff3f);
}

namespace IOPCOP0Cause
{
  constexpr IOPWord BRANCH_DELAY = UINT32_C(1) << 31;
  constexpr IOPWord INTERRUPT_PENDING = UINT32_C(0x0000ff00);
  constexpr IOPWord HARDWARE_INTERRUPT_PENDING = UINT32_C(0x0000fc00);
  constexpr IOPWord READ_MASK = UINT32_C(0xb000ff7c);
  constexpr IOPWord SOFTWARE_INTERRUPT_PENDING =
    UINT32_C(0x00000300);
}

namespace IOPExceptionVector
{
  constexpr IOPAddress GENERAL = UINT32_C(0x80000080);
  constexpr IOPAddress BOOT_GENERAL = UINT32_C(0xbfc00180);
}

namespace IOPReset
{
  constexpr IOPAddress VECTOR = UINT32_C(0xbfc00000);
}

#endif
