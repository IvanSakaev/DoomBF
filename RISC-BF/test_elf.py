#!/usr/bin/env python3
"""Сборка chess → bpk и пошаговый ibf -d до assert / паники / лимита шагов."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path
import pexpect

ANSI = re.compile(r"\x1b\[[0-9;]*m")
PROMPT = re.compile(r"\x1b\[32m\$ \x1b\[0m")
ROOT = Path(__file__).resolve().parent

REGS_NAMES = {
    **{f"x{i}": f"x{i}" for i in range(32)},
    "zero" : "x0",
    "ra" : "x1",
    "sp" : "x2",
    "gp" : "x3",
    "tp" : "x4",
    "t0" : "x5",
    "t1" : "x6",
    "t2" : "x7",
    "s0" : "x8",
    "fp" : "x8",
    "s1" : "x9",
    "a0" : "x10",
    "a1" : "x11",
    "a2" : "x12",
    "a3" : "x13",
    "a4" : "x14",
    "a5" : "x15",
    "a6" : "x16",
    "a7" : "x17",
    "s2" : "x18",
    "s3" : "x19",
    "s4" : "x20",
    "s5" : "x21",
    "s6" : "x22",
    "s7" : "x23",
    "s8" : "x24",
    "s9" : "x25",
    "s10" : "x26",
    "s11" : "x27",
    "t3" : "x28",
    "t4" : "x29",
    "t5" : "x30",
    "t6" : "x31",
}


def clean(s: str) -> str:
    return ANSI.sub("", s)


BLOCK_SIZE = 256


def pack_next_bytes(components: list[int]) -> int:
    return sum((v & 0xFF) << (8 * i) for i, v in enumerate(components))


def parse_stop(chunk: str):
    plain = clean(chunk)
    mnemonic = ""
    next_addr = None
    next_components = None
    regs = {}
    nxt = re.search(r"next: (0x[0-9a-f]+(?:, 0x[0-9a-f]+){3})", plain)
    if nxt:
        next_components = [int(x, 16) for x in re.findall(r"0x([0-9a-f]+)", nxt.group(1))]
        next_addr = sum((v & 0xFF) << (8 * i) for i, v in enumerate(next_components))

    for m in re.finditer(r"^>?\s*(x\d+):\s*(.+)$", plain, re.M):
        cells = [int(x, 16) for x in re.findall(r"0x([0-9a-f]+)", m.group(2))]
        regs[m.group(1)] = sum((v & 0xF) << (4 * i) for i, v in enumerate(cells))

    mnemonic = re.findall(r">.*\n", plain)[-1]
    mnemonic = mnemonic.strip().lstrip(">").strip()
    return mnemonic, next_addr, next_components, regs, plain


def get_output(child: pexpect.spawn, command: str, i: int):
    idx = child.expect([PROMPT, "assertion failed", pexpect.EOF], timeout=10)
    chunk = child.before or ""
    if idx == 1:
        chunk += "assertion failed"
        child.expect(pexpect.EOF, timeout=10)
        chunk += child.before or ""
        print(chunk)
        sys.exit(1)
    elif idx == 2:
        print("ibf завершился")
        sys.exit(1)
    mnemonic, next_addr, next_components, regs, plain = parse_stop(chunk)
    print(
        f"{command} #{i:5d}\n"
        f"  next=0x{next_addr or 0:04x}\n"
        f"  mnemonic={mnemonic}\n"
        f"  x10=0x{regs.get("x10", 0):08x}"
    )
    instr, args = parse_mnemonic(mnemonic)
    return instr, args, next_addr, next_components, regs, plain



def parse_imm(token: str) -> int:
    sign = 1
    if token.startswith("_"):
        sign = -1
        token = token[1:]
    if token.startswith("0x"):
        val = int(token[2:], 16)
    else:
        val = int(token)
    return val * sign


def parse_mnemonic(plain: str):
    l = plain.split()
    l = [a.strip().strip(",").lower() for a in l]
    instr = l[0]
    args = []
    for a in l[1:]:
        if a in REGS_NAMES:
            args.append(REGS_NAMES[a])
        elif "(" in a:
            a, b = a.split("(")
            assert b.endswith(")")
            b = b[:-1]
            args.append(parse_imm(a))
            args.append(REGS_NAMES[b])
        else:
            args.append(parse_imm(a))
    return instr, args


MASK32 = 0xFFFFFFFF
PROGRAM_START_ADDRESS = 16

JUMP_INSTRS = {
    "j", "jr", "jal", "jalr", "call", "ret",
    "beq", "beqz", "bne", "bnez", "blt", "bltu", "bge", "bgeu",
    "blez", "bgez", "bltz", "bgtz", "bgt", "ble", "bgtu", "bleu",
}

current_addr = 0
current_linear = 0
regs = {}
memory = [0] * (16 ** 7)


def u32(v: int) -> int:
    return v & MASK32


def s32(v: int) -> int:
    v = u32(v)
    return v if v < 0x80000000 else v - 0x100000000


def shamt(v: int) -> int:
    return v & 0x1F


def imm12(v: int) -> int:
    v &= 0xFFF
    return v if v < 0x800 else v - 0x1000


def components_to_linear(components: list[int]) -> int:
    return sum((components[i] & 0xFF) * (BLOCK_SIZE ** i) for i in range(4))


def linear_to_components(linear: int) -> list[int]:
    linear = u32(linear)
    return [(linear // (BLOCK_SIZE ** i)) % BLOCK_SIZE for i in range(4)]


def linear_to_packed(linear: int) -> int:
    return pack_next_bytes(linear_to_components(linear))


def reg_to_next(val: int) -> int:
    val = u32(val) & ~1
    digits = [(val >> (4 * i)) & 0xF for i in range(8)]
    components = [digits[2 * j] | (digits[2 * j + 1] << 4) for j in range(4)]
    return pack_next_bytes(components)


def next_addr_add(linear: int, offset: int) -> int:
    linear += offset
    assert linear >= 0, f"negative next addr: {linear:#x} (offset={offset:#x})"
    return linear_to_packed(linear)


def write_reg(rd: str, value: int) -> None:
    if rd != "x0":
        regs[rd] = u32(value)


def mem_write(addr: int, value: int, size: int) -> None:
    addr = u32(addr)
    value = u32(value)
    for i in range(size):
        memory[addr + i] = (value >> (8 * i)) & 0xFF


def mem_read(addr: int, size: int, signed: bool) -> int:
    addr = u32(addr)
    raw = 0
    for i in range(size):
        raw |= memory[addr + i] << (8 * i)
    if signed and raw >= (1 << (size * 8 - 1)):
        raw -= 1 << (size * 8)
    return u32(raw)


def zero_jalr_next(offset: int) -> int:
    components = [0, 0, 0, PROGRAM_START_ADDRESS]
    off = offset
    for i in range(4):
        components[i] = (components[i] + (off % BLOCK_SIZE)) % BLOCK_SIZE
        off //= BLOCK_SIZE
    return pack_next_bytes(components)


def branch_taken(instr: str, args: list) -> bool:
    if instr == "beq":
        return regs[args[0]] == regs[args[1]]
    if instr == "bne":
        return regs[args[0]] != regs[args[1]]
    if instr == "blt":
        return s32(regs[args[0]]) < s32(regs[args[1]])
    if instr == "bltu":
        return u32(regs[args[0]]) < u32(regs[args[1]])
    if instr == "bge":
        return s32(regs[args[0]]) >= s32(regs[args[1]])
    if instr == "bgeu":
        return u32(regs[args[0]]) >= u32(regs[args[1]])
    if instr == "beqz":
        return regs[args[0]] == 0
    if instr == "bnez":
        return regs[args[0]] != 0
    if instr == "blez":
        return s32(regs[args[0]]) <= 0
    if instr == "bgez":
        return s32(regs[args[0]]) >= 0
    if instr == "bltz":
        return s32(regs[args[0]]) < 0
    if instr == "bgtz":
        return s32(regs[args[0]]) > 0
    if instr == "bgt":
        return s32(regs[args[0]]) > s32(regs[args[1]])
    if instr == "ble":
        return s32(regs[args[0]]) <= s32(regs[args[1]])
    if instr == "bgtu":
        return u32(regs[args[0]]) > u32(regs[args[1]])
    if instr == "bleu":
        return u32(regs[args[0]]) <= u32(regs[args[1]])
    raise ValueError(instr)


def run_command(instr, args, next_addr_predict, next_components, regs_predict, plain):
    global current_addr, current_linear, regs
    if regs == {}:
        assert next_components is not None
        current_addr = next_addr_predict
        current_linear = components_to_linear(next_components)
        regs = {
            **regs_predict,
            "x0": 0,
        }
        return

    pc = current_addr
    pc_linear = current_linear
    predicted_next = None
    prev_regs = regs.copy()

    if instr == "add":
        write_reg(args[0], regs[args[1]] + regs[args[2]])
    elif instr == "addi":
        write_reg(args[0], regs[args[1]] + imm12(args[2]))
    elif instr == "sub":
        write_reg(args[0], regs[args[1]] - regs[args[2]])
    elif instr == "li":
        write_reg(args[0], args[1])
    elif instr == "lui":
        write_reg(args[0], args[1] << 12)
    elif instr == "mv":
        write_reg(args[0], regs[args[1]])
    elif instr == "neg":
        write_reg(args[0], -regs[args[1]])
    elif instr == "nop":
        pass
    elif instr == "unimp":
        pass
    elif instr == "ebreak":
        pass
    elif instr == "ecall":
        pass

    elif instr == "sll":
        write_reg(args[0], u32(regs[args[1]] << shamt(regs[args[2]])))
    elif instr == "slli":
        write_reg(args[0], u32(regs[args[1]] << shamt(args[2])))
    elif instr == "srl":
        write_reg(args[0], u32(regs[args[1]] >> shamt(regs[args[2]])))
    elif instr == "srli":
        write_reg(args[0], u32(regs[args[1]] >> shamt(args[2])))
    elif instr == "sra":
        write_reg(args[0], u32(s32(regs[args[1]]) >> shamt(regs[args[2]])))
    elif instr == "srai":
        write_reg(args[0], u32(s32(regs[args[1]]) >> shamt(args[2])))

    elif instr == "or":
        write_reg(args[0], regs[args[1]] | regs[args[2]])
    elif instr == "and":
        write_reg(args[0], regs[args[1]] & regs[args[2]])
    elif instr == "xor":
        write_reg(args[0], regs[args[1]] ^ regs[args[2]])
    elif instr == "not":
        write_reg(args[0], ~regs[args[1]])
    elif instr == "ori":
        write_reg(args[0], regs[args[1]] | u32(imm12(args[2])))
    elif instr == "andi":
        write_reg(args[0], regs[args[1]] & u32(imm12(args[2])))
    elif instr == "xori":
        write_reg(args[0], regs[args[1]] ^ u32(imm12(args[2])))

    elif instr == "slt":
        write_reg(args[0], 1 if s32(regs[args[1]]) < s32(regs[args[2]]) else 0)
    elif instr == "slti":
        write_reg(args[0], 1 if s32(regs[args[1]]) < s32(imm12(args[2])) else 0)
    elif instr == "sltu":
        write_reg(args[0], 1 if u32(regs[args[1]]) < u32(regs[args[2]]) else 0)
    elif instr == "sltiu":
        write_reg(args[0], 1 if u32(regs[args[1]]) < u32(imm12(args[2])) else 0)
    elif instr == "seqz":
        write_reg(args[0], 1 if regs[args[1]] == 0 else 0)
    elif instr == "snez":
        write_reg(args[0], 1 if regs[args[1]] != 0 else 0)
    elif instr == "sltz":
        write_reg(args[0], 1 if s32(regs[args[1]]) < 0 else 0)
    elif instr == "sgtz":
        write_reg(args[0], 1 if s32(regs[args[1]]) > 0 else 0)

    elif instr == "sw":
        mem_write(regs[args[2]] + args[1], regs[args[0]], 4)
    elif instr == "sh":
        mem_write(regs[args[2]] + args[1], regs[args[0]], 2)
    elif instr == "sb":
        mem_write(regs[args[2]] + args[1], regs[args[0]], 1)
    elif instr == "lw":
        write_reg(args[0], mem_read(regs[args[2]] + args[1], 4, signed=True))
    elif instr == "lh":
        write_reg(args[0], mem_read(regs[args[2]] + args[1], 2, signed=True))
    elif instr == "lhu":
        write_reg(args[0], mem_read(regs[args[2]] + args[1], 2, signed=False))
    elif instr == "lb":
        write_reg(args[0], mem_read(regs[args[2]] + args[1], 1, signed=True))
    elif instr == "lbu":
        write_reg(args[0], mem_read(regs[args[2]] + args[1], 1, signed=False))

    elif instr == "auipc":
        write_reg(args[0], u32(pc_linear + (args[1] << 12)))
    elif instr == "j":
        predicted_next = next_addr_add(pc_linear, args[0])
    elif instr == "jr":
        predicted_next = reg_to_next(regs[args[0]])
    elif instr == "jal":
        write_reg(args[0], u32(pc_linear + 4))
        predicted_next = next_addr_add(pc_linear, args[-1])
    elif instr == "jalr":
        base, offset = args[2], args[1]
        if base == "x0":
            predicted_next = zero_jalr_next(offset)
        else:
            predicted_next = reg_to_next(regs[base] + offset)
        write_reg(args[0], u32(pc_linear + 4))
    elif instr == "call":
        write_reg("x1", u32(pc_linear + 4))
        predicted_next = next_addr_add(pc_linear, args[-1])
    elif instr == "ret":
        predicted_next = reg_to_next(regs["x1"])
    elif instr in JUMP_INSTRS:
        offset = args[-1]
        predicted_next = (
            next_addr_add(pc_linear, offset) if branch_taken(instr, args)
            else next_addr_add(pc_linear, 4)
        )

    else:
        raise NotImplementedError(instr)

    if predicted_next is None:
        predicted_next = next_addr_add(pc_linear, 4)

    got = u32(next_addr_predict or 0)
    assert predicted_next == got, (
        f"{instr} {args}: predicted next=0x{predicted_next:04x}, "
        f"got=0x{got:04x} (pc=0x{pc:04x})"
        f"\n\n{plain}"
    )

    assert next_components is not None
    current_addr = next_addr_predict
    current_linear = components_to_linear(next_components)
    regs["x0"] = 0
    for i in range(1, 32):
        name = f"x{i}"
        regs[name] = u32(regs[name])
        assert regs[name] == regs_predict[name], (f"expected=0x{regs[name]:08x} got=0x{regs_predict[name]:08x} {name}\n\n{prev_regs}\n\n{regs}\n\n{regs_predict}")
    



def main() -> int:
    ap = argparse.ArgumentParser(description="Отладка chess через ibf -d")
    ap.add_argument("--bpk", type=Path, default=Path("./out.bpk"))
    ap.add_argument("--ibf", type=Path, default=Path("./bin/ibf"))
    ap.add_argument("--tmp", type=Path, default=Path("./tmp"))
    ap.add_argument("--build", action="store_true", help="cargo build + risc_bf.py -c")
    ap.add_argument("--no-asserts", action="store_true", help="ibf -a")
    cmd_args = ap.parse_args()

    cmd = f"{Path.absolute(cmd_args.ibf)} -cd {Path.absolute(cmd_args.bpk)}" + ("" if cmd_args.no_asserts else " -a")
    child = pexpect.spawn(cmd, cwd=str(cmd_args.tmp), encoding="utf-8", timeout=600)
    child.expect("loading addrmap", timeout=10)
    child.expect(re.compile(r"next:.*?0x[0-9a-f]+"), timeout=120)
    child.expect(PROMPT, timeout=120)

    i = 0
    while True:
        command = "r" if i < 0 else "w"
        child.sendline(command)
        instr, args, next_addr_predict, next_components, regs_predict, plain = get_output(child, command, i)
        if i >= 11:
            run_command(instr, args, next_addr_predict, next_components, regs_predict, plain)
        i += 1
    child.sendline("q")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
