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


def parse_stop(chunk: str):
    plain = clean(chunk)
    mnemonic = ""
    next_addr = None
    regs = {}
    nxt = re.search(r"next: (0x[0-9a-f]+(?:, 0x[0-9a-f]+){3})", plain)
    if nxt:
        cells = [int(x, 16) for x in re.findall(r"0x([0-9a-f]+)", nxt.group(1))]
        next_addr = sum((v & 0xFF) << (4 * i) for i, v in enumerate(cells))

    for m in re.finditer(r"^>?\s*(x\d+):\s*(.+)$", plain, re.M):
        cells = [int(x, 16) for x in re.findall(r"0x([0-9a-f]+)", m.group(2))]
        regs[m.group(1)] = sum((v & 0xFF) << (4 * i) for i, v in enumerate(cells))

    mnemonic = re.findall(r">.*\n", plain)[-1]
    mnemonic = mnemonic.strip().lstrip(">").strip()
    return mnemonic, next_addr, regs


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
    mnemonic, next_addr, regs = parse_stop(chunk)
    print(
        f"{command} #{i:3d}\n"
        f"  next=0x{next_addr or 0:04x}\n"
        f"  mnemonic={mnemonic}\n"
        f"  x1=0x{regs.get("x1", 0):08x}"
    )
    instr, args = parse_mnemonic(mnemonic)
    return instr, args, next_addr, regs



def parse_mnemonic(plain: str):
    l = plain.split()
    l = [a.strip().strip(",").strip().lower() for a in l]
    instr = l[0]
    args = []
    for a in l[1:]:
        if a in REGS_NAMES:
            args.append(REGS_NAMES[a])
        elif "(" in a:
            a, b = a.split("(")
            assert b.endswith(")")
            b = b[:-1]
            
            sign = 1
            if a.startswith("_"):
                sign = -1
                a = a[1:]
            a = int(a, 16) * sign
            args.append(a)

            args.append(REGS_NAMES[b])
        else:
            sign = 1
            if a.startswith("_"):
                sign = -1
                a = a[1:]
            a = int(a, 16) * sign
            args.append(a)
    return instr, args


current_addr = 0
regs = None
memory = [0] * (16 ** 7)


def run_command(instr, args, next_addr_predict, regs_predict):
    global current_addr, regs
    if regs is None:
        current_addr = next_addr_predict
        regs = {
            **regs_predict,
            "x0" : 0
        }
        return

    if instr == "add":
        regs[args[0]] = regs[args[1]] + regs[args[2]]
    elif instr == "addi":
        regs[args[0]] = regs[args[1]] + args[2]
    elif instr == "sub":
        regs[args[0]] = regs[args[1]] - regs[args[2]]
    elif instr == "li":
        regs[args[0]] = args[1]
    elif instr == "lui":
        regs[args[0]] = args[1] << 12
    elif instr == "mv":
        regs[args[0]] = regs[args[1]]
    elif instr == "neg":
        regs[args[0]] = -regs[args[1]]
    elif instr == "nop":
        pass
    elif instr == "sll":
        regs[args[0]] = regs[args[1]] << regs[args[2]]
    elif instr == "slli":
        regs[args[0]] = regs[args[1]] << args[2]
    else:
        raise NotImplementedError(instr)

    regs["x0"] = 0
    for i in range(1, 32):
        regs[f"x{i}"] = regs[f"x{i}"] & 0xffffffff
    



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
        command = "r" if i < 11 else "w"
        child.sendline(command)
        instr, args, next_addr_predict, regs_predict = get_output(child, command, i)
        if i >= 11:
            run_command(instr, args, next_addr_predict, regs_predict)
        i += 1
    child.sendline("q")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
