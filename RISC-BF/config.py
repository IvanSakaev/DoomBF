BLOCK_SIZE = 256  # 256 for production

SCRAP_COUNT = 32  # TODO: reduce scrap count

PROGRAM_START_ADDRESS = 16  # program (not data) addresses start from PROGRAM_START_ADDRESS * 0x01000000

# Memory cell count will be 16^MEMORY_ADDRESS_HALFBYTES
# MEMORY_ADDRESS_HALFBYTES > 3 may NOT work properly with most interpretators except ibf
# MEMORY_ADDRESS_HALFBYTES > 5 will use too much RAM while generating file without option -c (compressing)
# MEMORY_ADDRESS_HALFBYTES > 6 is not supported
# Without option -c (compressing), the output file size will be heavily dependent on this value
MEMORY_ADDRESS_HALFBYTES = 6
# This flag increases memory size by 16 times and doesn't increase the output file size.
MEMORY_ADDRESS_LAST_HALFBYTE_AS_BYTE = True

# Input/output ecall max length value
MAX_OUTPUT_LENGTH_HALFBYTES = 4  # Max output length will be 16^MAX_OUTPUT_LENGTH_HALFBYTES

# Preload .data section
PRELOAD_MEMORY = True

########## Next constants are useful only for ibf brainfuck interpretator. ##########
# For other interpretators it's strictly recommended to disable them.
# ibf interpretator: https://github.com/sit-itmo/DoomBF/tree/master/bf/industrial-bf

# Using # symbol for breakpoints.
ALLOW_DEBUG = True  # Allow breakpoints in concater.debug()
# Using * symbol for weak breakpoints.
BREAKPOINT_EVERY_CYCLE = False
BREAKPOINT_EVERY_INSTRUCTION = False
BREAKPOINT_AFTER_EVERY_INSTRUCTION = True

# Generate .b.addr file with cells in "watch".
GENERATE_ADDRMAP = True
WATCH_REGISTERS = ["x1", "x2", "a0"]

# Allow asserts in brainfuck by using @hex and !hex for location and value assert.
ALLOW_ASSERTS = True

# Don't change!
MEMORY_SCRAPS_COUNT = 2 + MEMORY_ADDRESS_HALFBYTES + max(4, MAX_OUTPUT_LENGTH_HALFBYTES * 2)
assert PROGRAM_START_ADDRESS >= 1
assert PROGRAM_START_ADDRESS <= 0xff
assert BLOCK_SIZE <= 256
assert BLOCK_SIZE % 4 == 0
