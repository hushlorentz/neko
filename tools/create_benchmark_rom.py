#!/usr/bin/env python3

"""Create original synthetic dual-CPU ROMs for bounded execution benchmarks."""

import argparse
from pathlib import Path
import struct
import sys


KINDS = ("cpu", "ram", "devices")
CONFORMANCE_KINDS = ("cop1",)
ROM_BYTES = 4194304


def constant(words, register, value):
    words.extend((0x3c000000 | register << 16 | value >> 16,
                  0x34000000 | register << 21 | register << 16 | value & 0xffff))


def store(words, address, value):
    constant(words, 9, address)
    constant(words, 10, value)
    words.append(0xad2a0000)  # sw r10, 0(r9)


def branch(words, opcode, source, target, destination):
    offset = destination - len(words) - 1
    if not -32768 <= offset <= 32767:
        raise ValueError("Benchmark branch exceeds signed 16-bit range")
    words.extend((opcode << 26 | source << 21 | target << 16 | offset & 0xffff, 0))


def wait_dma(words, address=0xb000d000):
    constant(words, 9, address)  # Channel CHCR through uncached EE MMIO.
    poll = len(words)
    words.extend((0x8d280000, 0, 0x31080100))  # lw r8; load delay; andi STR.
    branch(words, 5, 8, 0, poll)


def body(words, kind):
    if kind == "devices":
        wait_dma(words)
        store(words, 0xb000d010, 0x4000)  # D8_MADR
        store(words, 0xb000d020, 4)       # D8_QWC: four qwords
        store(words, 0xb000d080, 0)       # D8_SADR
        store(words, 0xb000d000, 0x100)   # D8_CHCR: normal fromSPR START
    words.extend((0x24420001, 0x24630003))  # addiu r2,1; addiu r3,3.
    if kind == "ram":
        words.extend((
            0x8c280000, 0,             # lw r8,0(r1); IOP load delay.
            0x01022026,               # xor r4,r8,r2
            0xac220000,               # sw r2,0(r1)
            0x24210004,               # advance the uncached RAM word.
            0x3021003f,               # wrap inside a sixteen-word window.
            0x00260825,               # or r1,r1,r6 (uncached base).
        ))
    else:
        words.append(0x00432026)  # xor r4,r2,r3


def loop(words, kind, iterations):
    start = len(words)
    body(words, kind)
    if iterations is None:
        branch(words, 4, 0, 0, start)
    else:
        words.append(0x24e7ffff)  # addiu r7,r7,-1
        branch(words, 5, 7, 0, start)


def publish_results(words):
    store(words, 0xa0002000, 0x4e454b4f)
    words.extend((0xad220004, 0xad230008, 0xad24000c))


def encode_rom(words):
    image = bytearray(ROM_BYTES)
    for index, word in enumerate(words):
        struct.pack_into("<I", image, index * 4, word)
    return bytes(image)


def cop1_conformance_rom(iterations):
    words = [0x34020000, 0x34030000, 0x34070000 | iterations]
    words.extend((0x40087800, 0, 0x3409001f))  # mfc0 PRId; delay; IOP revision.
    dispatch = len(words)
    words.extend((0, 0))
    store(words, 0xa0004000, 0x3f800000)
    constant(words, 1, 0xa0004000)
    constant(words, 8, 0x40000000)
    words.extend((0x44881000, 0, 0, 0))  # mtc1 r8,f2; drain the initial move.
    start = len(words)
    words.extend((
        0x24420001, 0x24630003,  # Integer work with no older COP1 producer.
        0xc4210000,             # lwc1 f1,0(r1)
        0x460208c0,             # add.s f3,f1,f2: 1.0 + 2.0
        0x44041800,             # mfc1 r4,f3
        0xac240004,             # sw r4,4(r1): wait for the GPR producer.
    ))
    words.extend([0] * 8)  # Return to empty work before the next iteration.
    words.append(0x24e7ffff)
    branch(words, 5, 7, 0, start)
    to_marker = len(words)
    words.extend((0, 0))
    iop_start = len(words)
    loop(words, "cpu", iterations)
    marker = len(words)
    words[dispatch] = 0x11090000 | (iop_start - dispatch - 1)
    words[to_marker] = 0x10000000 | (marker - to_marker - 1)
    publish_results(words)
    words.extend((0x1000ffff, 0))
    return encode_rom(words)


def create_rom(kind, iterations=None):
    if kind not in KINDS + CONFORMANCE_KINDS:
        raise ValueError(f"Unknown benchmark kind: {kind}")
    if iterations is not None and (type(iterations) is not int or not 1 <= iterations <= 65535):
        raise ValueError("Finite iterations must be between 1 and 65535")
    if kind == "cop1":
        if iterations is None:
            raise ValueError("COP1 conformance requires finite iterations")
        return cop1_conformance_rom(iterations)
    words = [0x34020000, 0x34030000]
    if iterations is not None:
        words.append(0x34070000 | iterations)
    if kind == "ram":
        constant(words, 6, 0xa0004000)
        words.append(0x00c00825)  # or r1,r6,r0
    if kind != "devices":
        loop(words, kind, iterations)
    else:
        words.extend((0x40087800, 0, 0x3409001f))  # mfc0 PRId; delay; IOP revision.
        dispatch = len(words)
        words.extend((0, 0))
        store(words, 0xb000e000, 1)       # D_CTRL: DMA enable
        store(words, 0xb0000020, 64)      # Timer0 COMP
        store(words, 0xb0000010, 0x1c0)   # Count enable, zero return, compare IRQ
        for index in range(16):
            store(words, 0xa0006000 + 4 * index, 0x12340000 + index)
        store(words, 0xb000d410, 0x6000)  # D9_MADR
        store(words, 0xb000d420, 4)       # D9_QWC
        store(words, 0xb000d480, 0)       # D9_SADR
        store(words, 0xb000d400, 0x100)   # D9_CHCR: normal toSPR START
        wait_dma(words, 0xb000d400)
        loop(words, "devices", iterations)
        if iterations is not None:
            wait_dma(words)
        to_marker = len(words)
        words.extend((0, 0))
        iop_start = len(words)
        store(words, 0xbf801108, 64)      # IOP Timer0 target
        store(words, 0xbf801104, 0x58)    # Reset/IRQ on target, repeat pulse
        loop(words, "cpu", iterations)
        marker = len(words)
        offset = iop_start - dispatch - 1
        words[dispatch] = 0x11090000 | offset  # beq r8,r9,IOP setup
        words[to_marker] = 0x10000000 | (marker - to_marker - 1)
    if iterations is not None:
        publish_results(words)
    words.extend((0x1000ffff, 0))  # Park; both CPUs remain bounded by the host ceiling.
    return encode_rom(words)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=KINDS + CONFORMANCE_KINDS)
    parser.add_argument("--iterations", type=int,
                        help="Finite correctness variant; omitted for sustained host-bounded execution.")
    parser.add_argument("--output", type=Path, required=True,
                        help="New ROM destination; existing files are refused.")
    arguments = parser.parse_args(argv)
    try:
        image = create_rom(arguments.kind, arguments.iterations)
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        with arguments.output.open("xb") as output:
            output.write(image)
        print(f"created {arguments.output}: kind={arguments.kind} bytes={len(image)}")
        return 0
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
