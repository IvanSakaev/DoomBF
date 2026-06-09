from __future__ import annotations

from instructions.baseInstructions import *
from dataclasses import dataclass


@dataclass
class ShiftLeft(Instruction):
    dst: Register
    src: Register
    shift: Register

    def evaluate(self, program: Program, cur_block: Block, comments: bool = False):
        concater.rem(f"sll {self.dst} {self.src} {self.shift}", comments)
        if self.dst == ZERO:
            return

        if self.src == ZERO:
            self.dst.clear_big()
            return

        if self.shift == ZERO:
            if self.src != self.dst:
                self.dst.clear_big()
                self.src.copy_big(self.dst)
                return

        shift_big = scraps[0]  # shift / 4
        shift_small = scraps[1]  # shift % 4
        shift_verybig_unused = shift_big_scrap = scraps[2]
        shift_scrap = scraps[3]
        mul_scrap = scraps[4]
        # scraps 3 and 4 are used for div_imm()

        self.shift.get_cell(0).div_imm(4, shift_small, shift_big)
        shift_big.copy(self.shift.get_cell(0), multiplier=4, scrap=shift_big_scrap)
        self.shift.get_cell(1).div_imm(2, shift_scrap, shift_verybig_unused)
        shift_verybig_unused.move(self.shift.get_cell(1), multiplier=2)
        shift_scrap.move(shift_big, self.shift.get_cell(1), multiplier=(4, 1))
        shift_big.copy(shift_big_scrap, scrap=shift_scrap)

        if self.src != self.dst:
            self.dst.clear_big()
            self.src.copy_big(self.dst, scrap=mul_scrap)

        for i in range(7, -1, -1):
            # custom ifnot
            shift_big_scrap.raw(">+<[->-]>[-", pos_offset=1)
            #                        ^
            small_dst = self.dst.get_cell(i)

            with shift_small.loop():
                small_dst.move(mul_scrap, multiplier=2)
                mul_scrap.move(small_dst)
                shift_scrap.change(1)
                shift_small.change(-1)
            shift_scrap.move(shift_small)
            shift_big_scrap.cell_rel(2).raw("]")
            # end of ifnot

        with shift_big.loop():
            self.dst.get_cell(7).clear()
            for i in range(6, -1, -1):
                small_src = self.dst.get_cell(i)
                small_dst = self.dst.get_cell(i + 1)
                small_src.move(small_dst)
            shift_big.change(-1)

        if self.dst != self.shift:
            shift_small.move(self.shift.get_cell(0))
        else:
            shift_small.clear()
        shift_big.clear()
        self.dst.normalize_big()


@dataclass
class ShiftLeftI(Instruction):
    dst: Register
    src: Register
    shift: Immediate

    def evaluate(self, program: Program, cur_block: Block, comments: bool = False):
        concater.rem(f"slli {self.dst} {self.src} {self.shift}", comments)
        if self.dst == ZERO:
            return

        if self.src == ZERO:
            self.dst.clear_big()
            return

        small_shift = self.shift % 4
        big_shift = (self.shift // 4) % 8

        for i in range(7 - big_shift, -1, -1):
            small_src = self.src.get_cell(i)
            small_dst = self.dst.get_cell(i + big_shift)
            if small_src != small_dst:
                small_dst.clear()
                if self.src == self.dst:
                    small_src.move(small_dst, multiplier=(2 ** small_shift))
                else:
                    small_src.copy(small_dst, multiplier=(2 ** small_shift))
            elif small_shift != 0:
                small_dst.move(scraps[0])
                scraps[0].move(small_dst, multiplier=(2 ** small_shift))

        if small_shift != 0:
            # normalize only changed digits
            mod = scraps[0]  # 2 scraps after MOD are used too in div_imm()
            output = scraps[3]
            for i in range(big_shift, 8):
                small = self.dst.get_cell(i)
                need_output = i < 7
                if need_output:
                    small.div_imm(16, mod, output)
                else:
                    small.div_imm(16, mod, output=None)
                mod.move(small)
                if need_output:
                    small2 = small.cell_rel(1)
                    output.move(small2)

        # set small digits to zero
        for i in range(big_shift):
            small_dst = self.dst.get_cell(i)
            small_dst.clear()


@dataclass
class ShiftRight(Instruction):
    dst: Register
    src: Register
    shift: Register

    def evaluate(self, program: Program, cur_block: Block, comments: bool = False):
        concater.rem(f"srl {self.dst} {self.src} {self.shift}", comments)
        if self.dst == ZERO:
            return

        if self.src == ZERO:
            self.dst.clear_big()
            return

        if self.shift == ZERO:
            if self.src != self.dst:
                self.dst.clear_big()
                self.src.copy_big(self.dst)
                return

        shift_big = scraps[0]  # shift / 4
        shift_small = scraps[1]  # shift % 4
        shift_verybig_unused = scrap1 = scraps[2]
        scrap2 = scraps[3]
        scrap3 = scraps[4]
        # scraps 3, 4, 5 are used for div_imm()

        self.shift.get_cell(0).div_imm(4, shift_small, shift_big)
        shift_small.copy(self.shift.get_cell(0), scrap=scrap1)
        shift_big.copy(self.shift.get_cell(0), multiplier=4, scrap=scrap1)

        self.shift.get_cell(1).div_imm(2, scrap2, shift_verybig_unused)
        shift_verybig_unused.move(self.shift.get_cell(1), multiplier=2)
        scrap2.move(shift_big, self.shift.get_cell(1), multiplier=(4, 1))

        if self.src != self.dst:
            self.dst.clear_big()
            self.src.copy_big(self.dst, scrap=scrap1)

        with shift_big.loop():
            self.dst.get_cell(0).clear()
            for i in range(1, 8):
                small_src = self.dst.get_cell(i)
                small_dst = self.dst.get_cell(i - 1)
                small_src.move(small_dst)
            shift_big.change(-1)

        with shift_small.loop():
            for i in range(7, -1, -1):
                small_dst = self.dst.get_cell(i)
                small_dst.div_imm(2, scrap3, scrap2)
                scrap2.move(small_dst)
                if i != 0:
                    scrap3.move(small_dst.cell_rel(-1), multiplier=16)
                else:
                    scrap3.clear()
            if self.dst != self.shift:
                scrap1.change(1)
            shift_small.change(-1)

        if self.dst != self.shift:
            scrap1.move(self.shift.get_cell(0))


@dataclass
class ShiftRightI(Instruction):
    dst: Register
    src: Register
    shift: Immediate

    def evaluate(self, program: Program, cur_block: Block, comments: bool = False):
        concater.rem(f"srli {self.dst} {self.src} {self.shift}", comments)
        if self.dst == ZERO:
            return

        if self.src == ZERO:
            self.dst.clear_big()
            return

        small_shift = self.shift % 4
        big_shift = (self.shift // 4) % 8

        for i in range(big_shift, 8):
            small_src = self.src.get_cell(i)
            small_dst = self.dst.get_cell(i - big_shift)
            if small_src != small_dst:
                small_dst.clear()
                if self.src == self.dst:
                    small_src.move(small_dst)
                else:
                    small_src.copy(small_dst)

        if small_shift != 0:
            mod = scraps[0]  # 2 scraps after MOD are used too in div_imm()
            output = scraps[3]
            translator = scraps[4]
            for i in range(7 - big_shift, -1, -1):
                small = self.dst.get_cell(i)
                small.div_imm(2 ** small_shift, mod, output)
                output.move(small)
                translator.move(small, multiplier=16 // (2 ** small_shift))
                if i == 0:
                    mod.clear()
                else:
                    mod.move(translator)

        # set big digits to zero
        for i in range(8 - big_shift, 8):
            small_dst = self.dst.get_cell(i)
            small_dst.clear()


@dataclass
class ShiftRightArithmetic(Instruction):
    dst: Register
    src: Register
    shift: Register

    def evaluate(self, program: Program, cur_block: Block, comments: bool = False):
        concater.rem(f"sra {self.dst} {self.src} {self.shift}", comments)
        if self.dst == ZERO:
            return

        if self.src == ZERO:
            self.dst.clear_big()
            return

        if self.shift == ZERO:
            if self.src != self.dst:
                self.dst.clear_big()
                self.src.copy_big(self.dst)
                return

        shift_big = scraps[0]  # shift / 4
        shift_small = scraps[1]  # shift % 4
        shift_verybig_unused = scrap1 = scraps[2]
        scrap2 = scraps[3]
        scrap3 = scraps[4]
        # scraps 3, 4, 5, 6 are used for div_imm()

        sign_bit = scraps[7]
        sign_scrap = scraps[8]

        self.shift.get_cell(0).div_imm(4, shift_small, shift_big)
        shift_small.copy(self.shift.get_cell(0), scrap=scrap1)
        shift_big.copy(self.shift.get_cell(0), multiplier=4, scrap=scrap1)

        self.shift.get_cell(1).div_imm(2, scrap2, shift_verybig_unused)
        shift_verybig_unused.move(self.shift.get_cell(1), multiplier=2)
        scrap2.move(shift_big, self.shift.get_cell(1), multiplier=(4, 1))

        if self.src != self.dst:
            self.dst.clear_big()
            self.src.copy_big(self.dst, scrap=scrap1)

        sign_digit = self.dst.get_cell(7)
        sign_digit.div_imm(8, scrap1, sign_bit)
        scrap1.move(sign_digit)
        sign_bit.copy(sign_digit, scrap=sign_scrap, multiplier=8)
        
        with shift_big.loop():
            self.dst.get_cell(0).clear()
            for i in range(1, 8):
                small_src = self.dst.get_cell(i)
                small_dst = self.dst.get_cell(i - 1)
                small_src.move(small_dst)
            sign_bit.copy(sign_digit, scrap=sign_scrap, multiplier=0xf)
            shift_big.change(-1)

        with shift_small.loop():
            for i in range(7, -1, -1):
                small_dst = self.dst.get_cell(i)
                small_dst.div_imm(2, scrap3, scrap2)
                scrap2.move(small_dst)
                if i != 0:
                    scrap3.move(small_dst.cell_rel(-1), multiplier=16)
                else:
                    scrap3.clear()
            sign_bit.copy(sign_digit, scrap=sign_scrap, multiplier=8)
            if self.dst != self.shift:
                scrap1.change(1)
            shift_small.change(-1)

        sign_bit.clear()
        if self.dst != self.shift:
            scrap1.move(self.shift.get_cell(0))


@dataclass
class ShiftRightArithmeticI(Instruction):
    dst: Register
    src: Register
    shift: Immediate

    def evaluate(self, program: Program, cur_block: Block, comments: bool = False):
        concater.rem(f"srai {self.dst} {self.src} {self.shift}", comments)
        if self.dst == ZERO:
            return

        if self.src == ZERO:
            self.dst.clear_big()
            return

        small_shift = self.shift % 4
        big_shift = (self.shift // 4) % 8

        for i in range(big_shift, 8):
            small_src = self.src.get_cell(i)
            small_dst = self.dst.get_cell(i - big_shift)
            if small_src != small_dst:
                small_dst.clear()
                if self.src == self.dst:
                    small_src.move(small_dst)
                else:
                    small_src.copy(small_dst)
        
        sign_cell = self.dst.get_cell(7 - big_shift)
        out = scraps[0]
        mod = scraps[5]
        sign_cell.div_imm(8, mod, out)
        mod.move(sign_cell)
        out.move(mod, sign_cell, multiplier=(1, 8))
        sign = mod

        if small_shift != 0:
            mod = scraps[0]  # 2 scraps after MOD are used too in div_imm()
            output = scraps[3]
            translator = scraps[4]
            for i in range(7 - big_shift, -1, -1):
                small = self.dst.get_cell(i)
                small.div_imm(2 ** small_shift, mod, output)
                output.move(small)
                translator.move(small, multiplier=16 // (2 ** small_shift))
                if i == 0:
                    mod.clear()
                else:
                    mod.move(translator)

        # set big digits to zero
        for i in range(8 - big_shift, 8):
            small_dst = self.dst.get_cell(i)
            small_dst.clear()
        with sign.loop():
            for i in range(8 - big_shift, 8):
                small_dst = self.dst.get_cell(i)
                small_dst.change(15)
            cell = self.dst.get_cell(7 - big_shift)
            mask = [0, 0b1000, 0b1100, 0b1110][small_shift]
            cell.change(mask)
            sign.change(-1)


@dataclass
class Or(Instruction):
    dst: Register
    src1: Register
    src2: Register

    def evaluate(self, program: Program, cur_block: Block, comments: bool = False):
        concater.rem(f"or {self.dst} {self.src1} {self.src2}", comments)
        if self.dst == ZERO:
            return
        if self.src1 == ZERO and self.src2 == ZERO:
            self.dst.clear_big()
            return
        if self.src1 == ZERO:
            if self.src2 != self.dst:
                self.dst.clear_big()
                self.src2.copy_big(self.dst)
            return
        if self.src2 == ZERO:
            if self.src1 != self.dst:
                self.dst.clear_big()
                self.src1.copy_big(self.dst)
            return
        if self.src1 == self.src2:
            self.dst.clear_big()
            self.src1.copy_big(self.dst)
            return

        if self.src2 == self.dst:
            self.src2 = self.src1
            self.src1 = self.dst

        mod = scraps[0]
        div_output = scraps[3]
        src1_scrap = scraps[4]
        src2_scrap = scraps[5]
        output = scraps[6]

        for i in range(8):
            src1_small = self.src1.get_cell(i)
            src2_small = self.src2.get_cell(i)
            dst_small = self.dst.get_cell(i)
            if self.src1 != self.dst:
                dst_small.clear()
            for j in range(4):
                # Move first bit to output
                if j == 3:
                    if self.src1 != self.dst:
                        src1_small.move(src1_scrap, output, multiplier=[2 ** j, 1])
                    else:
                        src1_small.move(output)
                else:
                    src1_small.div_imm(2, mod, div_output)
                    div_output.move(src1_small)
                    if self.src1 != self.dst:
                        mod.move(src1_scrap, output, multiplier=[2 ** j, 1])
                    else:
                        mod.move(output)

                # Move second bit to output
                if j == 3:
                    with src2_small.loop():
                        src2_scrap.change(2 ** 3)
                        output.clear()
                        output.change(1)
                        src2_small.change(-1)
                else:
                    src2_small.div_imm(2, mod, div_output)
                    div_output.move(src2_small)
                    with mod.loop():
                        src2_scrap.change(2 ** j)
                        output.clear()
                        output.change(1)
                        mod.change(-1)

                if self.src1 != self.dst:
                    output.move(dst_small, multiplier=2 ** j)
                else:
                    output.move(src1_scrap, multiplier=2 ** j)
            # Restoring value
            src1_scrap.move(src1_small)
            src2_scrap.move(src2_small)


@dataclass
class And(Instruction):
    dst: Register
    src1: Register
    src2: Register

    def evaluate(self, program: Program, cur_block: Block, comments: bool = False):
        concater.rem(f"and {self.dst} {self.src1} {self.src2}", comments)
        if self.dst == ZERO:
            return
        if self.src1 == ZERO or self.src2 == ZERO:
            self.dst.clear_big()
            return
        if self.src1 == self.src2:
            self.dst.clear_big()
            self.src1.copy_big(self.dst)
            return

        if self.src2 == self.dst:
            self.src2 = self.src1
            self.src1 = self.dst

        mod = scraps[0]
        div_output = scraps[3]
        src1_scrap = scraps[4]
        src2_scrap = scraps[5]
        output = scraps[6]

        for i in range(8):
            src1_small = self.src1.get_cell(i)
            src2_small = self.src2.get_cell(i)
            if self.src1 != self.dst:
                dst_small = self.dst.get_cell(i)
                dst_small.clear()
            else:
                dst_small = src1_scrap
            for j in range(4):
                # Move first bit to output
                if j == 3:
                    if self.src1 != self.dst:
                        src1_small.move(src1_scrap, output, multiplier=[2 ** j, 1])
                    else:
                        src1_small.move(output)
                else:
                    src1_small.div_imm(2, mod, div_output)
                    div_output.move(src1_small)
                    if self.src1 != self.dst:
                        mod.move(src1_scrap, output, multiplier=[2 ** j, 1])
                    else:
                        mod.move(output)

                # Move second bit to output
                if j == 3:
                    with src2_small.loop():
                        src2_scrap.change(2 ** 3)
                        with output.loop():
                            dst_small.change(2 ** j)
                            output.change(-1)
                        src2_small.change(-1)
                else:
                    src2_small.div_imm(2, mod, div_output)
                    div_output.move(src2_small)
                    with mod.loop():
                        src2_scrap.change(2 ** j)
                        with output.loop():
                            dst_small.change(2 ** j)
                            output.change(-1)
                        mod.change(-1)
                output.clear()

            # Restoring value
            src1_scrap.move(src1_small)
            src2_scrap.move(src2_small)


@dataclass
class Xor(Instruction):
    dst: Register
    src1: Register
    src2: Register

    def evaluate(self, program: Program, cur_block: Block, comments: bool = False):
        concater.rem(f"xor {self.dst} {self.src1} {self.src2}", comments)
        if self.dst == ZERO:
            return
        if self.src1 == self.src2:
            self.dst.clear_big()
            return
        if self.src1 == ZERO:
            if self.src2 != self.dst:
                self.dst.clear_big()
                self.src2.copy_big(self.dst)
            return
        if self.src2 == ZERO:
            if self.src1 != self.dst:
                self.dst.clear_big()
                self.src1.copy_big(self.dst)
            return

        if self.src2 == self.dst:
            self.src2 = self.src1
            self.src1 = self.dst

        mod = scraps[0]
        div_output = scraps[3]
        src1_scrap = scraps[4]
        src2_scrap = scraps[5]
        output = scraps[6]

        for i in range(8):
            src1_small = self.src1.get_cell(i)
            src2_small = self.src2.get_cell(i)
            if self.src1 != self.dst:
                dst_small = self.dst.get_cell(i)
                dst_small.clear()
            else:
                dst_small = src1_scrap
            for j in range(4):
                # Move first bit to output
                if j == 3:
                    if self.src1 != self.dst:
                        src1_small.move(src1_scrap, output, multiplier=[2 ** j, 1])
                    else:
                        src1_small.move(output)
                else:
                    src1_small.div_imm(2, mod, div_output)
                    div_output.move(src1_small)
                    if self.src1 != self.dst:
                        mod.move(src1_scrap, output, multiplier=[2 ** j, 1])
                    else:
                        mod.move(output)

                # Move second bit to output
                if j == 3:
                    src2_small.move(src2_scrap, output, multiplier=[2 ** j, -1])
                else:
                    src2_small.div_imm(2, mod, div_output)
                    div_output.move(src2_small)
                    mod.move(src2_scrap, output, multiplier=[2 ** j, -1])

                with output.loop():
                    if self.src1 != self.dst:
                        dst_small.change(2 ** j)
                    else:
                        src1_scrap.change(2 ** j)
                    output.clear()

            # Restoring value
            src1_scrap.move(src1_small)
            src2_scrap.move(src2_small)


@dataclass
class Not(Instruction):
    dst: Register
    src: Register

    def evaluate(self, program, cur_block, comments=False):
        concater.rem(f"not {self.dst} {self.src}", comments)
        if self.dst == ZERO:
            return
        if self.src == ZERO:
            self.dst.change_big(2 ** 32 - 1, clear=True)
            return
        self.invert_big(self.src, self.dst)

    @classmethod
    def invert_big(cls, src: Register, dst: Register, clear: bool = True):
        scrap = scraps[0]
        for i in range(8):
            small_src = src.get_cell(i)
            small_dst = dst.get_cell(i)
            if src != dst:
                if clear:
                    small_dst.clear()
                small_dst.change(15)
                small_src.move(scrap)
                scrap.move(small_src, small_dst, multiplier=[1, -1])
            else:
                small_src.move(scrap)
                small_src.change(15)
                scrap.move(small_src, multiplier=-1)


@dataclass
class OrI(Instruction):
    dst: Register
    src1: Register
    src2: Immediate

    def evaluate(self, program: Program, cur_block: Block, comments: bool = False):
        concater.rem(f"ori {self.dst} {self.src1} {self.src2}", comments)
        if self.dst == ZERO:
            return
        if self.src1 == ZERO:
            self.dst.change_big(self.src2, clear=True)
            return

        mod = scraps[0]
        div_output = scraps[3]
        src1_scrap = scraps[4]
        output = scraps[5]

        for i in range(8):
            src1_small = self.src1.get_cell(i)
            dst_small = self.dst.get_cell(i)
            if self.src1 != self.dst:
                dst_small.clear()
            src2_small = self.src2 // 2 ** (i * 4)
            src2_small %= 2 ** 4
            if src2_small == 0:
                if self.src1 != self.dst:
                    src1_small.copy(dst_small, scrap=src1_scrap)
            elif src2_small == 15:
                dst_small.clear()
                dst_small.change(15)
            else:
                for j in range(4):
                    need_value = (src2_small & (2 ** j)) == 0

                    # Move first bit to output
                    if j == 3:
                        if need_value:
                            if self.src1 != self.dst:
                                src1_small.move(
                                    src1_scrap, output, multiplier=[2 ** j, 1]
                                )
                            else:
                                src1_small.move(output)
                        else:
                            if self.src1 != self.dst:
                                src1_small.move(src1_scrap, multiplier=2 ** j)
                            else:
                                src1_small.clear()
                    else:
                        src1_small.div_imm(2, mod, div_output)
                        div_output.move(src1_small)
                        if need_value:
                            if self.src1 != self.dst:
                                mod.move(src1_scrap, output, multiplier=[2 ** j, 1])
                            else:
                                mod.move(output)
                        else:
                            if self.src1 != self.dst:
                                mod.move(src1_scrap, multiplier=2 ** j)
                            else:
                                mod.clear()

                    if need_value:
                        if self.src1 != self.dst:
                            output.move(dst_small, multiplier=2 ** j)
                        else:
                            output.move(src1_scrap, multiplier=2 ** j)
                    else:
                        if self.src1 != self.dst:
                            dst_small.change(2 ** j)
                        else:
                            src1_scrap.change(2 ** j)
                # Restoring value
                src1_scrap.move(src1_small)


@dataclass
class AndI(Instruction):
    dst: Register
    src1: Register
    src2: Immediate

    def evaluate(self, program: Program, cur_block: Block, comments: bool = False):
        concater.rem(f"andi {self.dst} {self.src1} {self.src2}", comments)
        if self.dst == ZERO:
            return
        if self.src1 == ZERO:
            self.dst.clear_big()
            return

        mod = scraps[0]
        div_output = scraps[3]
        src1_scrap = scraps[4]
        output = scraps[5]

        for i in range(8):
            src1_small = self.src1.get_cell(i)
            dst_small = self.dst.get_cell(i)
            if self.src1 != self.dst:
                dst_small.clear()
            src2_small = self.src2 // 2 ** (i * 4)
            src2_small %= 2 ** 4
            if src2_small == 0:
                dst_small.clear()
            elif src2_small == 15:
                if self.src1 != self.dst:
                    src1_small.copy(dst_small, scrap=src1_scrap)
            else:
                for j in range(4):
                    need_value = (src2_small & (2 ** j)) > 0

                    # Move first bit to output
                    if j == 3:
                        if need_value:
                            if self.src1 != self.dst:
                                src1_small.move(
                                    src1_scrap, output, multiplier=[2 ** j, 1]
                                )
                            else:
                                src1_small.move(output)
                        else:
                            if self.src1 != self.dst:
                                src1_small.move(src1_scrap, multiplier=2 ** j)
                            else:
                                src1_small.clear()
                    else:
                        src1_small.div_imm(2, mod, div_output)
                        div_output.move(src1_small)
                        if need_value:
                            if self.src1 != self.dst:
                                mod.move(src1_scrap, output, multiplier=[2 ** j, 1])
                            else:
                                mod.move(output)
                        else:
                            if self.src1 != self.dst:
                                mod.move(src1_scrap, multiplier=2 ** j)
                            else:
                                mod.clear()

                    if need_value:
                        if self.src1 != self.dst:
                            output.move(dst_small, multiplier=2 ** j)
                        else:
                            output.move(src1_scrap, multiplier=2 ** j)
                # Restoring value
                src1_scrap.move(src1_small)


@dataclass
class XorI(Instruction):
    dst: Register
    src1: Register
    src2: Immediate

    def evaluate(self, program: Program, cur_block: Block, comments: bool = False):
        concater.rem(f"xori {self.dst} {self.src1} {self.src2}", comments)
        if self.dst == ZERO:
            return
        if self.src1 == ZERO:
            self.dst.change_big(self.src2, clear=True)
            return

        mod = scraps[0]
        div_output = scraps[3]
        src1_scrap = scraps[4]
        output = scraps[5]

        for i in range(8):
            src1_small = self.src1.get_cell(i)
            dst_small = self.dst.get_cell(i)
            if self.src1 != self.dst:
                dst_small.clear()
            src2_small = self.src2 // 2 ** (i * 4)
            src2_small %= 2 ** 4
            if src2_small == 0:
                if self.src1 != self.dst:
                    src1_small.copy(dst_small, scrap=src1_scrap)
            elif src2_small == 15:
                dst_small.change(15)
                if self.src1 != self.dst:
                    src1_small.copy(dst_small, scrap=src1_scrap, multiplier=-1)
            else:
                for j in range(4):
                    invert = (src2_small & (2 ** j)) > 0

                    # Move first bit to output
                    if j == 3:
                        if self.src1 != self.dst:
                            src1_small.move(
                                src1_scrap,
                                output,
                                multiplier=[2 ** j, -1 if invert else 1],
                            )
                        else:
                            src1_small.move(output, multiplier=(-1 if invert else 1))
                    else:
                        src1_small.div_imm(2, mod, div_output)
                        div_output.move(src1_small)
                        if self.src1 != self.dst:
                            mod.move(
                                src1_scrap,
                                output,
                                multiplier=[2 ** j, -1 if invert else 1],
                            )
                        else:
                            mod.move(output, multiplier=(-1 if invert else 1))

                    if invert:
                        output.change(1)
                    if self.src1 != self.dst:
                        output.move(dst_small, multiplier=2 ** j)
                    else:
                        output.move(src1_scrap, multiplier=2 ** j)
                # Restoring value
                src1_scrap.move(src1_small)
