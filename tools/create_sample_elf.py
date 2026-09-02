#!/usr/bin/env python3

"""Create tiny synthetic ELF guests for Neko runtime diagnostics."""

import argparse
import struct
from pathlib import Path
from typing import List


ELF_HEADER_SIZE = 52
PROGRAM_HEADER_SIZE = 32
CODE_OFFSET = 0x100
ENTRY_POINT = 0x80001000


def return_program(exit_code: int) -> List[int]:
    if exit_code < 0 or exit_code > 0xFFFF:
        raise ValueError("Exit code must be between 0 and 65535.")
    return [
        0x34020000 | exit_code,  # ori $v0, $zero, exit_code
        0x03E00008,  # jr $ra
        0x00000000,  # nop delay slot
    ]


def program_words(kind: str, exit_code: int) -> List[int]:
    if kind == "return":
        return return_program(exit_code)
    if exit_code != 0:
        raise ValueError("--exit-code is only valid for the return sample.")
    if kind == "loop":
        return [
            0x1000FFFF,  # beq $zero, $zero, -1
            0x00000000,  # nop delay slot
        ]
    if kind == "exception":
        return [0x0000000C]  # syscall
    raise ValueError(f"Unknown sample kind: {kind}")


def create_elf(words: List[int]) -> bytes:
    code_size = len(words) * 4
    image = bytearray(CODE_OFFSET + code_size)
    image[0:16] = bytes(
        [
            0x7F,
            ord("E"),
            ord("L"),
            ord("F"),
            1,  # ELFCLASS32
            1,  # ELFDATA2LSB
            1,  # EV_CURRENT
            0,  # System V ABI
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
        ]
    )
    struct.pack_into(
        "<HHIIIIIHHHHHH",
        image,
        16,
        2,  # ET_EXEC
        8,  # EM_MIPS
        1,  # EV_CURRENT
        ENTRY_POINT,
        ELF_HEADER_SIZE,
        0,
        0,
        ELF_HEADER_SIZE,
        PROGRAM_HEADER_SIZE,
        1,
        0,
        0,
        0,
    )
    struct.pack_into(
        "<IIIIIIII",
        image,
        ELF_HEADER_SIZE,
        1,  # PT_LOAD
        CODE_OFFSET,
        ENTRY_POINT,
        ENTRY_POINT,
        code_size,
        code_size,
        5,  # PF_R | PF_X
        0x100,
    )
    for index, word in enumerate(words):
        struct.pack_into("<I", image, CODE_OFFSET + index * 4, word)
    return bytes(image)


def default_output(kind: str, exit_code: int) -> Path:
    name = f"return-{exit_code}" if kind == "return" else kind
    return Path("out") / "sample_elves" / f"{name}.elf"


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Create a synthetic ELF32 MIPS guest for Neko CLI diagnostics."
        )
    )
    parser.add_argument(
        "kind",
        choices=("return", "loop", "exception"),
        help="Guest behavior to generate.",
    )
    parser.add_argument(
        "--exit-code",
        type=int,
        default=0,
        help="Return sample exit code, from 0 through 65535.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Output path; defaults under out/sample_elves/.",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        words = program_words(arguments.kind, arguments.exit_code)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    output = arguments.output or default_output(
        arguments.kind,
        arguments.exit_code,
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    image = create_elf(words)
    output.write_bytes(image)
    print(
        f"created {output}: kind={arguments.kind} "
        f"bytes={len(image)} instructions={len(words)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
