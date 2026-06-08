#! /bin/python
from __future__ import annotations

import argparse
from typing import get_type_hints

from capstone import *
from elftools.elf.elffile import ELFFile

import config
import instructions.mnemonics
from cell import concater, currents, memory_scraps, nexts, scraps
from config import (
    BLOCK_SIZE,
    MEMORY_ADDRESS_HALFBYTES,
    MEMORY_SCRAPS_COUNT,
    PRELOAD_MEMORY,
    WATCH_REGISTERS, PROGRAM_START_ADDRESS,
    MEMORY_ADDRESS_LAST_HALFBYTE_AS_BYTE
)
from instructions.baseInstructions import Instruction
from instructions.mnemonics import (
    MNEMONICS,
    Block,
    LoadI,
    is_jump_instruction,
)
from registers import SCRAP_COUNT, Immediate, OffsetRegister, Register, regs


def split_program_into_blocks(instrs: list[Instruction]):
    root_block = Block(None, [], None)
    instr_id = 0
    entry_point_block = None
    for instr in instrs:
        mother_block = root_block
        for i in range(3, -1, -1):
            if i == 0:
                val = instr_id % (BLOCK_SIZE // 4)
            else:
                val = instr_id // (BLOCK_SIZE ** (i - 1)) // (BLOCK_SIZE // 4)
                val %= BLOCK_SIZE

            idx = val
            if i == 3:
                idx += PROGRAM_START_ADDRESS
            elif i == 0:
                idx *= 4
            if len(mother_block.daughter_blocks) <= val:
                assert len(mother_block.daughter_blocks) == val
                mother_block.daughter_blocks.append(Block(idx, [], mother_block))
            mother_block = mother_block.daughter_blocks[val]

        mother_block.daughter_blocks.append(instr)
        if hasattr(instr, "is_entry_point") and instr.is_entry_point:
            if entry_point_block is not None:
                raise RuntimeError("Two entry points found")
            entry_point_block = mother_block
        if not is_jump_instruction(instr):
            mother_block.daughter_blocks.append(instructions.mnemonics.JumpNext())

        instr_id += 1

    if entry_point_block is None:
        raise RuntimeError("No entry point found")
    return root_block, entry_point_block


class Program:
    def __init__(self, instrs, memory):
        self.kiloblock, self.entry_point_block = split_program_into_blocks(instrs)
        self.memory = memory
        # print(self.memory)
        # print(self.entry_point_block.get_full_id())
        # print(list(MNEMONICS.values()))

    def preload_memory(self):
        first_mem_cell = memory_scraps[-1].cell_rel(1)
        for addr, value in self.memory.items():
            first_mem_cell.cell_rel(addr).change(value)

    def program_prologue(self):
        LoadI(
            regs["sp"],
            Immediate(16 ** (MEMORY_ADDRESS_HALFBYTES + (1 if MEMORY_ADDRESS_LAST_HALFBYTE_AS_BYTE else 0)) - 1)
        ).evaluate(self, None)
        if PRELOAD_MEMORY:
            self.preload_memory()

        entry_point_block_id = self.entry_point_block.get_full_id()
        for next_, new_next in zip(nexts, entry_point_block_id):
            next_.change(new_next)
        nexts[-1].raw("[")
        for i in range(4):
            nexts[i].move(currents[i])

    def program_epilogue(self):
        currents[-1].raw("-]")
        if config.BREAKPOINT_EVERY_CYCLE:
            concater.raw("*")
        nexts[-1].raw("]")

    def block_prologue(self, block: Block, deep: int):
        assert deep < 4
        name = f"block_{block.myid}"
        name_line = f"{concater.sanitize(name)}:"
        concater.raw("\n")
        concater.raw(f"{name_line}\n")
        if deep == 0:
            currents[-1].change(-PROGRAM_START_ADDRESS)
        elif block.myid != 0:
            currents[-1].change(-4 if deep == 3 else -1)
        currents[-1].raw(">+<[>-]>[-", pos_offset=1)
        if deep != 3:
            currents[-2 - deep].move(currents[-1])

    def block_epilogue(self, block: Block, deep: int):
        assert deep < 4
        concater.raw("\n")
        if len(block.daughter_blocks) > 0 and deep != 3:
            currents[-1].change(-1)
            currents[-1].raw("]" * len(block.daughter_blocks))
        currents[-1].cell_rel(2).raw("]")
        currents[-1].raw("[")

    def assemble_block(self, block: Block, deep: int = 0):
        self.block_prologue(block, deep)
        if isinstance(block.daughter_blocks[-1], Instruction):
            for inst in block.daughter_blocks:
                inst.evaluate(self, block, True)
                concater.assert_pos()
                for scrap in scraps:
                    scrap.assert_val(0)
            if config.BREAKPOINT_AFTER_EVERY_INSTRUCTION:
                concater.raw("*")
        else:
            for bl in block.daughter_blocks:
                self.assemble_block(bl, deep + 1)
        self.block_epilogue(block, deep)

    def assemble_program(self):
        self.program_prologue()
        for block in self.kiloblock.daughter_blocks:
            self.assemble_block(block)
        self.program_epilogue()
        out = concater.get_code()
        concater.reset_code()
        return out


def parse_arg(arg: str, expected_type: type):
    """Parse a single argument string to the expected type."""
    arg = arg.strip()
    if expected_type == Register:
        if arg in regs:
            return regs[arg]
        raise ValueError(f"Register {arg} is unavailable")
    elif expected_type == Immediate:
        return Immediate.from_str(arg)
    elif expected_type == OffsetRegister:
        if "(" in arg:
            imm, reg = arg.split("(")
            imm = Immediate.from_str(imm)
            assert reg.endswith(")")
            reg = reg[:-1]
        else:
            reg = arg
            imm = 0
        if reg in regs:
            reg = regs[reg]
        else:
            raise ValueError(f"Register {arg} is unavailable")
        return OffsetRegister(reg, imm)
    else:
        raise ValueError(f"Unknown expected type: {expected_type}")


def get_instruction_types(op: type[Instruction]):
    hints = get_type_hints(op)
    op_args = list(hints.values())
    return op_args


def parse_elf(path: str):
    memory = {}

    with open(path, "rb") as file:
        elf = ELFFile(file)

        for segment in elf.iter_segments():
            if segment['p_type'] != 'PT_LOAD':
                continue

            vaddr = segment['p_vaddr']
            data = segment.data()
            # segment['p_memsz']
            
            memsize = (16 ** 6)
            if MEMORY_ADDRESS_LAST_HALFBYTE_AS_BYTE:
                memsize *= 16
            if vaddr >= memsize:
                continue
            for i, b in enumerate(data):
                if (vaddr + i) >= memsize:
                    continue
                memory[vaddr + i] = b

        text = elf.get_section_by_name(".text")
        code = text.data()
        base = text['sh_addr']

        entry_point_addr = elf.header['e_entry']

    md = Cs(CS_ARCH_RISCV, CS_MODE_RISCV32)
    instrs = []
    entry_point_found = False
    prev_addr = None
    for instr in md.disasm(code, base):
        # print(f"0x{instr.address:x}:\t{instr.mnemonic}\t{instr.op_str}")
        assert prev_addr is None or instr.address - prev_addr == 4
        prev_addr = instr.address
        mnemonic = MNEMONICS[instr.mnemonic]
        args = instr.op_str
        if args == "":
            args = []
        else:
            args = args.split(",")
        types_ = get_instruction_types(mnemonic)

        # Remove prettify from some instructions
        if instr.mnemonic == "jal" and len(args) == 1:
            args.insert(0, "ra")
        if instr.mnemonic == "jalr" and len(args) == 1:
            args.insert(0, "ra")
        if instr.mnemonic == "jalr" and len(args) == 3:
            rd, rs1, offset = (a.strip() for a in args)
            args = [rd, f"{offset}({rs1})"]

        if len(args) != len(types_):
            raise ValueError(
                f"Incorrect number of arguments for {mnemonic}. Got {len(args)}, expected {len(types_)}.\nGot {args}")
        for i in range(len(args)):
            try:
                args[i] = parse_arg(args[i], types_[i])
            except ValueError as e:
                raise ValueError(mnemonic, args, e)

        is_entry_point = (instr.address == entry_point_addr)
        if is_entry_point:
            if entry_point_found:
                raise ValueError("Two instructions are entry points")
            entry_point_found = True
        instruction_obj = mnemonic(*args)
        instruction_obj.is_entry_point = is_entry_point
        instrs.append(instruction_obj)
    if not entry_point_found:
        raise ValueError(f"Can't find entry point: No instruction on address 0x{entry_point_addr:x}")
    return instrs, memory


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)

    parser.add_argument("-c", action="store_true")
    parser.add_argument("input")
    parser.add_argument("output")

    args = parser.parse_args()
    concater.set_compressing_enabled(args.c)

    instrs, memory = parse_elf(args.input)
    prog = Program(instrs, memory)
    out_contents = prog.assemble_program()

    with open(args.output, "w") as f:
        f.write(out_contents)
        f.write("\n")

    # Generate addrmap
    with open(args.output + ".addr", "w") as f:
        f.write(f"a{nexts[0].addr:x}[{len(nexts)}] next\n")
        f.write(f"a{currents[0].addr:x}[{len(currents)}] current\n")
        f.write(f"a{scraps[0].addr:x}[{SCRAP_COUNT - MEMORY_SCRAPS_COUNT:x}] scraps\n")
        for reg_name in WATCH_REGISTERS:
            f.write(f"a{regs[reg_name].addr:x}[8] {reg_name}\n")
        f.write(f"a{memory_scraps[0].addr:x}[{MEMORY_SCRAPS_COUNT:x}] mem_scraps\n")
        f.write(f"a{memory_scraps[-1].cell_rel(1).addr:x}[{256:x}] memory\n")
