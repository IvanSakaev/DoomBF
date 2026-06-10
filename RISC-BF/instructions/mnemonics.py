from __future__ import annotations
from instructions.arithmeticInstructions import *
from instructions.bitwiseInstructions import *
from instructions.comparingInstructions import *
from instructions.jumpInstructions import *
from instructions.storeInstructions import *
from instructions.specialInstructions import *

from instructions.baseInstructions import Instruction

if TYPE_CHECKING:
    from risc_bf import Program




MNEMONICS: dict[str, type[Instruction]] = dict()

# Pseudo-instructions
MNEMONICS["li"] = LoadI
MNEMONICS["lui"] = LoadUpperI
MNEMONICS["mv"] = Move
MNEMONICS["neg"] = Neg
MNEMONICS["nop"] = Nop

# Arithmetic
MNEMONICS["add"] = Add
MNEMONICS["addi"] = AddI
MNEMONICS["sub"] = Sub
# MNEMONICS["mul"] = Mul
# MNEMONICS["mulhu"] = MulHighUnsigned
# MNEMONICS["divu"] = DivUnsigned
# MNEMONICS["remu"] = ReminderUnsigned

# bitwise
MNEMONICS["sll"] = ShiftLeft
MNEMONICS["slli"] = ShiftLeftI
MNEMONICS["srl"] = ShiftRight
MNEMONICS["srli"] = ShiftRightI
MNEMONICS["sra"] = ShiftRightArithmetic
MNEMONICS["srai"] = ShiftRightArithmeticI
MNEMONICS["or"] = Or
MNEMONICS["and"] = And
MNEMONICS["xor"] = Xor
MNEMONICS["not"] = Not
MNEMONICS["ori"] = OrI
MNEMONICS["andi"] = AndI
MNEMONICS["xori"] = XorI

# comparing
MNEMONICS["slt"] = SetLessThan
MNEMONICS["slti"] = SetLessThanI
MNEMONICS["sltu"] = SetLessThanUnsigned
MNEMONICS["sltiu"] = SetLessThanIUnsigned
MNEMONICS["seqz"] = SetEqualToZero
MNEMONICS["snez"] = SetNotEqualToZero
MNEMONICS["sltz"] = SetLessThanZero
MNEMONICS["sgtz"] = SetGreaterThanZero

# store/load
MNEMONICS["sw"] = StoreWord
MNEMONICS["sh"] = StoreHalfword
MNEMONICS["sb"] = StoreByte
MNEMONICS["lw"] = LoadWord
MNEMONICS["lh"] = LoadHalfword
MNEMONICS["lhu"] = LoadHalfwordUnsigned
MNEMONICS["lb"] = LoadByte
MNEMONICS["lbu"] = LoadByteUnsigned

# jump
MNEMONICS["auipc"] = AddUpperImmToPC
MNEMONICS["j"] = Jump
MNEMONICS["jr"] = JumpRegister
MNEMONICS["jal"] = JumpAndLink
MNEMONICS["jalr"] = JumpAndLinkRegister
MNEMONICS["call"] = Call
MNEMONICS["ret"] = Ret
# conditional jump
MNEMONICS["beq"] = BranchIfEqual
MNEMONICS["bne"] = BranchIfNotEqual
MNEMONICS["blt"] = BranchIfLessThan
MNEMONICS["bltu"] = BranchIfLessThanUnsigned
MNEMONICS["bge"] = BranchIfGreaterThanOrEqual
MNEMONICS["bgeu"] = BranchIfGreaterThanOrEqualUnsigned
MNEMONICS["beqz"] = BranchIfEqualToZero
MNEMONICS["bnez"] = BranchIfNotEqualToZero
# conditional jump pseudo-instructions
MNEMONICS["blez"] = BranchIfLessThanOrEqualToZero
MNEMONICS["bgez"] = BranchIfGreaterThanOrEqualToZero
MNEMONICS["bltz"] = BranchIfLessThanZero
MNEMONICS["bgtz"] = BranchIfGreaterThanZero
MNEMONICS["bgt"] = BranchIfGreaterThan
MNEMONICS["ble"] = BranchIfLessThanOrEqual
MNEMONICS["bgtu"] = BranchIfGreaterThanUnsigned
MNEMONICS["bleu"] = BranchIfLessThanOrEqualUnsigned

# special
MNEMONICS["ebreak"] = Debug
MNEMONICS["ecall"] = Ecall


def is_jump_instruction(instr):
    return isinstance(
        instr,
        (
            Jump,
            JumpRegister,
            JumpAndLink,
            JumpAndLinkRegister,
            Call,
            Ret,

            # branches
            BranchIfEqual,
            BranchIfNotEqual,
            BranchIfLessThan,
            BranchIfLessThanUnsigned,
            BranchIfGreaterThanOrEqual,
            BranchIfGreaterThanOrEqualUnsigned,
            BranchIfEqualToZero,
            BranchIfNotEqualToZero,

            # pseudo-instructions
            BranchIfLessThanOrEqualToZero,
            BranchIfGreaterThanOrEqualToZero,
            BranchIfLessThanZero,
            BranchIfGreaterThanZero,
            BranchIfGreaterThan,
            BranchIfLessThanOrEqual,
            BranchIfGreaterThanUnsigned,
            BranchIfLessThanOrEqualUnsigned,
        ),
    )
