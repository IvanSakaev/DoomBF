#!/usr/bin/env python3
"""Invoke a RISC-V GCC through a response file for reliable Windows builds."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--linker-script", required=True, type=Path)
    parser.add_argument("--sources-file", required=True, type=Path)
    parser.add_argument("--include-dir", action="append", default=[], type=Path)
    return parser.parse_args()


def gcc_response_quote(value: str) -> str:
    normalized = value.replace("\\", "/")
    return '"' + normalized.replace('"', '\\"') + '"'


def load_sources(path: Path) -> list[Path]:
    sources: list[Path] = []
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if line and not line.startswith("#"):
            source = Path(line)
            if not source.is_file():
                raise FileNotFoundError(f"RISC-V source not found: {source}")
            sources.append(source.resolve())
    if not sources:
        raise ValueError(f"source manifest is empty: {path}")
    return sources


def main() -> int:
    args = parse_args()
    compiler = args.compiler.resolve()
    if not compiler.is_file():
        raise FileNotFoundError(f"RISC-V GCC not found: {compiler}")

    bindir = compiler.parent
    libgcc_dir = (
        bindir / ".." / "lib" / "gcc" / "riscv64-unknown-elf" / "10.2.0" / "rv32i" / "ilp32"
    ).resolve()
    libgcc_a = libgcc_dir / "libgcc.a"
    if not libgcc_a.is_file():
        raise FileNotFoundError(f"libgcc.a not found: {libgcc_a}")

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    linker_script = args.linker_script.resolve()
    sources = load_sources(args.sources_file.resolve())

    compiler_args = [
        "-O3",
        "-march=rv32i",
        "-mabi=ilp32",
        "-ffreestanding",
        "-nostdlib",
        "-mcmodel=medlow",
        "-fsigned-char",
        "-std=c11",
        "-D_BF",
        "-DDOOMBF_EXTERNAL_WAD_HEADER=1",
        "-B",
        str(bindir),
        "-L",
        str(libgcc_dir),
        "-T",
        str(linker_script),
    ]
    for include_dir in args.include_dir:
        compiler_args.extend(["-I", str(include_dir.resolve())])
    compiler_args.extend(str(source) for source in sources)
    compiler_args.extend(["-lgcc", "-o", str(output)])

    response_path = output.with_suffix(output.suffix + ".rsp")
    response_path.write_text(
        "\n".join(gcc_response_quote(argument) for argument in compiler_args) + "\n",
        encoding="utf-8",
    )

    print(f"[riscv] {compiler.name} @{response_path.name}")
    completed = subprocess.run([str(compiler), f"@{response_path}"], cwd=output.parent, check=False)
    if completed.returncode != 0:
        return completed.returncode
    if not output.is_file():
        raise RuntimeError(f"RISC-V GCC returned success but did not create {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
