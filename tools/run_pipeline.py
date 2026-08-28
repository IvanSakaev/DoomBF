#!/usr/bin/env python3
"""Run the bidirectional ibf <-> frontend protocol without a named FIFO."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ibf", required=True, type=Path)
    parser.add_argument("--frontend", required=True, type=Path)
    parser.add_argument("--program", required=True, type=Path)
    parser.add_argument("--ibf-option", action="append", default=[])
    return parser.parse_args()


def require_file(path: Path, label: str) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file():
        raise FileNotFoundError(f"{label} not found: {resolved}")
    return resolved


def terminate_process(process: subprocess.Popen[bytes] | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def main() -> int:
    args = parse_args()
    ibf_path = require_file(args.ibf, "ibf executable")
    frontend_path = require_file(args.frontend, "frontend executable")
    program_path = require_file(args.program, "BF program")

    ibf_to_frontend_read, ibf_to_frontend_write = os.pipe()
    frontend_to_ibf_read, frontend_to_ibf_write = os.pipe()
    ibf: subprocess.Popen[bytes] | None = None
    frontend: subprocess.Popen[bytes] | None = None

    try:
        ibf_command = [str(ibf_path), "-c", *args.ibf_option, str(program_path)]
        ibf = subprocess.Popen(
            ibf_command,
            stdin=frontend_to_ibf_read,
            stdout=ibf_to_frontend_write,
            stderr=None,
            close_fds=True,
        )
        frontend = subprocess.Popen(
            [str(frontend_path)],
            stdin=ibf_to_frontend_read,
            stdout=frontend_to_ibf_write,
            stderr=None,
            close_fds=True,
        )

        for descriptor in (
            ibf_to_frontend_read,
            ibf_to_frontend_write,
            frontend_to_ibf_read,
            frontend_to_ibf_write,
        ):
            os.close(descriptor)

        while True:
            ibf_result = ibf.poll()
            frontend_result = frontend.poll()
            if ibf_result is not None or frontend_result is not None:
                break
            time.sleep(0.05)

        if frontend_result is not None:
            terminate_process(ibf)
            return frontend_result
        terminate_process(frontend)
        return ibf_result if ibf_result is not None else 1
    except KeyboardInterrupt:
        return 130
    finally:
        terminate_process(frontend)
        terminate_process(ibf)
        for descriptor in (
            ibf_to_frontend_read,
            ibf_to_frontend_write,
            frontend_to_ibf_read,
            frontend_to_ibf_write,
        ):
            try:
                os.close(descriptor)
            except OSError:
                pass


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"run_pipeline.py: {error}", file=sys.stderr)
        raise SystemExit(1)
