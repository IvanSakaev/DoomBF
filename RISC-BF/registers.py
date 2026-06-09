from __future__ import annotations

from cell import Cell, scraps
from config import SCRAP_COUNT, MEMORY_SCRAPS_COUNT


class Register:
    def __init__(self, addr: int | Cell | Register):
        if isinstance(addr, Register):
            addr = addr.addr
        if isinstance(addr, Cell):
            addr = addr.addr
        self.addr: int = addr

    def get_cell(self, number):
        assert 0 <= number < 8
        return Cell(self.addr + number)

    def get_cells(self):
        return [Cell(self.addr + i) for i in range(8)]

    def move_big(self, *dsts: Cell | Register, multiplier: int | tuple | list = 1):
        dsts2 = [Register(dst) for dst in dsts]
        assert self not in dsts2
        for i in range(8):
            small_src = self.get_cell(i)
            small_dsts = [d.get_cell(i) for d in dsts2]
            small_src.move(*small_dsts, multiplier=multiplier)

    def copy_big(
            self,
            *dsts: Register,
            scrap: Cell | None = None,
            multiplier: int | tuple | list = 1,
    ):
        if scrap is None:
            scrap = scraps[0]
        assert self not in dsts
        for dst in dsts:
            assert scrap not in dst.get_cells()
        if isinstance(multiplier, int):
            multiplier = [multiplier] * len(dsts)
        multiplier = list(multiplier) + [1]
        for i in range(8):
            small_src = self.get_cell(i)
            small_dsts = [d.get_cell(i) for d in dsts]
            small_dsts.append(scrap)
            small_src.move(*small_dsts, multiplier=multiplier)
            scrap.move(small_src)

    def clear_big(self):
        for cell in self.get_cells():
            cell.clear()

    def change_big(self, a: int, clear=False):
        if a < 0:
            a += 2 ** 32
        assert 0 <= a < (2 ** 32)
        for cell in self.get_cells():
            if clear:
                cell.clear()
            cell.change(a % 16)
            a //= 16

    def normalize_big(self):
        """
        Normalize big register (8 cells).
        Before normalization every cell SHOULD BE <= 0xf0.
        After every cell store only one hex number (value <= 0xf).

        It uses scraps 0, 1, 2, 3
        """
        mod = scraps[0]  # 2 scraps after MOD are used too in div_imm()
        output = scraps[3]

        for i in range(8):
            small = self.get_cell(i)
            need_output = i < 7
            if need_output:
                small.div_imm(16, mod, output)
            else:
                small.div_imm(16, mod, output=None)
            mod.move(small)
            if need_output:
                small2 = small.cell_rel(1)
                output.move(small2)

    def normalize_big_fast(self):
        """
        Normalize big register (8 cells).
        Before normalization every cell except one SHOULD BE normalized. This one cell SHOULD BE <= 0x10. For other cases use normalize_big().
        After every cell store only one hex number (value <= 0xf).

        It uses scraps 0, 1
        """
        transfer = scraps[0]
        mod = scraps[1]
        for i in range(8):
            small = self.get_cell(i)

            transfer.change(1)
            small.change(-16)

            with small.loop():
                small.move(mod)
                transfer.change(-1)

            small.change(16)
            mod.move(small)

            with transfer.loop():
                small.change(-16)
                if i < 7:
                    small.cell_rel(1).change(1)
                transfer.change(-1)

    def __eq__(self, other):
        if not isinstance(other, Register):
            return NotImplemented
        return self.addr == other.addr

    def __repr__(self):
        for key, value in regs.items():
            if not key.startswith("x"):
                if self == value:
                    return key
        for key, value in regs.items():
            if self == value:
                return key
        return f"REG{self.addr}"


class Immediate(int):
    def move(self, *dsts: Cell, multiplier: int | list = 1):
        if isinstance(multiplier, int):
            multiplier = [multiplier] * len(dsts)
        for dst, mult in zip(dsts, multiplier):
            dst.change(self * mult)

    def copy(self, *dsts: Cell, multiplier: int | list = 1):
        self.move(*dsts, multiplier=multiplier)

    @classmethod
    def from_str(cls, text: str):
        sign = 1
        if text.startswith("-"):
            sign = -1
            text = text[1:]
        text = text.lower()
        if text.startswith("0x"):
            imm = int(text[2:], 16)
        elif text.startswith("0b"):
            imm = int(text[2:], 2)
        elif text.startswith("0") and len(text) > 1:
            imm = int(text[1:], 8)
        else:
            imm = int(text)
        return Immediate(sign * imm)


class OffsetRegister:
    def __init__(self, register, offset):
        self.register = Register(register)
        self.offset = Immediate(offset)

    def __repr__(self):
        return f"{self.offset}({self.register})"


ZERO = Register(
    -1000
)  # IMPORTANT! It isn't a physical register, it mustn't be used for data storage

# Variable is only the first cell of register. 7 cells after that are register too.
# Register cells SHOULD BE only 4 bits (values between 0 and 15)
# Operations with registers should be little-endian
regs = {f"x{i + 1}": Register(i * 8 + SCRAP_COUNT - MEMORY_SCRAPS_COUNT + 4) for i in range(31)}
regs["x0"] = ZERO

regs["zero"] = regs["x0"]
regs["ra"] = regs["x1"]
regs["sp"] = regs["x2"]
regs["gp"] = regs["x3"]
regs["tp"] = regs["x4"]
regs["t0"] = regs["x5"]
regs["t1"] = regs["x6"]
regs["t2"] = regs["x7"]
regs["s0"] = regs["fp"] = regs["x8"]
regs["s1"] = regs["x9"]
regs["a0"] = regs["x10"]
regs["a1"] = regs["x11"]
regs["a2"] = regs["x12"]
regs["a3"] = regs["x13"]
regs["a4"] = regs["x14"]
regs["a5"] = regs["x15"]
regs["a6"] = regs["x16"]
regs["a7"] = regs["x17"]
regs["s2"] = regs["x18"]
regs["s3"] = regs["x19"]
regs["s4"] = regs["x20"]
regs["s5"] = regs["x21"]
regs["s6"] = regs["x22"]
regs["s7"] = regs["x23"]
regs["s8"] = regs["x24"]
regs["s9"] = regs["x25"]
regs["s10"] = regs["x26"]
regs["s11"] = regs["x27"]
regs["t3"] = regs["x28"]
regs["t4"] = regs["x29"]
regs["t5"] = regs["x30"]
regs["t6"] = regs["x31"]
