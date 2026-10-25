#include <cstdint>

#include "catch.hpp"
#include "ee_serial.hpp"

namespace
{
  void requireQuietTicks(EESerial &serial, unsigned ticks, std::uint32_t status)
  {
    unsigned completions = 0;
    unsigned unexpectedStatus = 0;
    std::uint8_t payload = 0;
    for (unsigned tick = 0; tick < ticks; ++tick)
    {
      const auto result = serial.advance();
      completions += result.completed;
      payload |= result.byte;
      unexpectedStatus += serial.readStatus() != status;
    }
    REQUIRE(completions == 0);
    REQUIRE(payload == 0);
    REQUIRE(unexpectedStatus == 0);
  }

  void requireCompletion(EESerial &serial, std::uint8_t byte)
  {
    const auto result = serial.advance();
    REQUIRE(result.completed);
    REQUIRE(result.byte == byte);
    REQUIRE(serial.readStatus() == 0);
    requireQuietTicks(serial, 2, 0);
  }

  void requireFixedConfigurationAdmission(EESerial &serial)
  {
    for (const auto value : {0u, 1u, 7u, 0x20u, 0x21u, 0x10020u, UINT32_MAX})
    {
      CAPTURE(value);
      REQUIRE(serial.writeLineControl(value) == (value == 0x20));
      REQUIRE(serial.writeInterruptEnable(value) == (value == 0));
      REQUIRE(serial.writeBaudRate(value) == (value == 0));
      if (value != 0 && value != 7)
      {
        REQUIRE_FALSE(serial.writeFIFOControl(value));
      }
    }
  }
}

TEST_CASE("EE serial owner starts idle in its exact fixed profile",
  "[ee][serial-owner]")
{
  EESerial serial;
  REQUIRE(serial.readStatus() == 0);
  requireFixedConfigurationAdmission(serial);
  REQUIRE(serial.writeFIFOControl(0));
  requireQuietTicks(serial, 5120, 0);
  REQUIRE(serial.transmitByte(0x23) == EESerialTransmitResult::Accepted);
  REQUIRE(serial.readStatus() == 0x8000);
}

TEST_CASE("EE serial owner completes every raw byte at exactly 2560 ticks",
  "[ee][serial-owner]")
{
  EESerial serial;
  for (unsigned byte = 0; byte < 256; ++byte)
  {
    CAPTURE(byte);
    REQUIRE(serial.transmitByte(static_cast<std::uint8_t>(byte)) ==
      EESerialTransmitResult::Accepted);
    REQUIRE(serial.readStatus() == 0x8000);
    requireQuietTicks(serial, 2559, 0x8000);
    requireCompletion(serial, static_cast<std::uint8_t>(byte));
  }
}

TEST_CASE("EE serial owner full writes preserve the byte and its deadline",
  "[ee][serial-owner]")
{
  EESerial serial;
  REQUIRE(serial.transmitByte(0xa5) == EESerialTransmitResult::Accepted);
  for (const auto elapsed : {0u, 1u, 1278u, 1280u})
  {
    requireQuietTicks(serial, elapsed, 0x8000);
    for (unsigned byte = 0; byte < 256; ++byte)
    {
      CAPTURE(elapsed);
      CAPTURE(byte);
      REQUIRE(serial.transmitByte(static_cast<std::uint8_t>(byte)) ==
        EESerialTransmitResult::Full);
      REQUIRE(serial.readStatus() == 0x8000);
    }
  }
  requireCompletion(serial, 0xa5);
}

TEST_CASE("EE serial owner configuration and release do not restart a frame",
  "[ee][serial-owner]")
{
  EESerial serial;
  REQUIRE(serial.transmitByte(0) == EESerialTransmitResult::Accepted);
  requireQuietTicks(serial, 1279, 0x8000);
  requireFixedConfigurationAdmission(serial);
  for (unsigned repeat = 0; repeat < 3; ++repeat)
  {
    REQUIRE(serial.writeLineControl(0x20));
    REQUIRE(serial.writeInterruptEnable(0));
    REQUIRE(serial.writeBaudRate(0));
    REQUIRE(serial.writeFIFOControl(0));
    REQUIRE(serial.readStatus() == 0x8000);
  }
  requireQuietTicks(serial, 1280, 0x8000);
  requireCompletion(serial, 0);
}

TEST_CASE("EE serial owner FIFO reset cancels and holds until released",
  "[ee][serial-owner]")
{
  for (const auto elapsed : {0u, 1u, 1279u, 2559u})
  {
    CAPTURE(elapsed);
    EESerial serial;
    REQUIRE(serial.transmitByte(0x0d) == EESerialTransmitResult::Accepted);
    requireQuietTicks(serial, elapsed, 0x8000);
    for (unsigned repeat = 0; repeat < 3; ++repeat)
    {
      REQUIRE(serial.writeFIFOControl(7));
      REQUIRE(serial.readStatus() == 0);
      requireFixedConfigurationAdmission(serial);
      for (unsigned byte = 0; byte < 256; ++byte)
      {
        REQUIRE(serial.transmitByte(static_cast<std::uint8_t>(byte)) ==
          EESerialTransmitResult::ResetHeld);
      }
      requireQuietTicks(serial, 5120, 0);
    }
    REQUIRE(serial.writeFIFOControl(0));
    REQUIRE(serial.transmitByte(0x0a) == EESerialTransmitResult::Accepted);
    requireQuietTicks(serial, 2559, 0x8000);
    requireCompletion(serial, 0x0a);
  }
}

TEST_CASE("EE serial owner reset restores an empty released transmitter",
  "[ee][serial-owner]")
{
  for (const auto elapsed : {0u, 1u, 1279u, 2559u})
  {
    for (const bool held : {false, true})
    {
      CAPTURE(elapsed);
      CAPTURE(held);
      EESerial serial;
      REQUIRE(serial.transmitByte(0xff) == EESerialTransmitResult::Accepted);
      requireQuietTicks(serial, elapsed, 0x8000);
      if (held)
      {
        REQUIRE(serial.writeFIFOControl(7));
      }
      serial.reset();
      REQUIRE(serial.readStatus() == 0);
      requireQuietTicks(serial, 5120, 0);
      requireFixedConfigurationAdmission(serial);
      REQUIRE(serial.transmitByte(0x80) == EESerialTransmitResult::Accepted);
      requireQuietTicks(serial, 2559, 0x8000);
      requireCompletion(serial, 0x80);
    }
  }
  EESerial idle;
  REQUIRE(idle.writeFIFOControl(7));
  idle.reset();
  idle.reset();
  REQUIRE(idle.transmitByte(0) == EESerialTransmitResult::Accepted);
}
