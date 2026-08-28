/* ibf -- The industrial brainfuck interpreter
 * 
 * This file is licensed under the BSD Zero Clause License
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>

#ifndef _WIN32
#include <unistd.h>
#include <arpa/inet.h>

#else
#include <io.h>
#include <fcntl.h>
#include "win_byteorder.c"

/* MSVC does not provide POSIX getopt(). */
static int optind = 1;
static int ibf_optpos = 1;

static int getopt(int argc, char *const argv[], const char *options) {
        if (optind >= argc || argv[optind][0] != '-' || argv[optind][1] == '\0')
                return -1;
        if (strcmp(argv[optind], "--") == 0) {
                optind++;
                return -1;
        }

        int opt = (unsigned char)argv[optind][ibf_optpos++];
        if (argv[optind][ibf_optpos] == '\0') {
                optind++;
                ibf_optpos = 1;
        }
        return strchr(options, opt) ? opt : '?';
}
#endif

#include "ibf_config.h"

#ifdef JIT
#include <lightning.h>
#endif

typedef char* string;
typedef char character;

uint8_t option_d = 0;
uint8_t option_a = 0;
uint8_t option_o = 0;
uint8_t option_c = 0;
uint8_t option_x = 0;

CELL *tape;

#include "util.c"
#include "vector.c"
#include "infinite-tape.c"
#include "command.c"
#include "dump.c"

#ifdef DEBUGGER
#include "sourcemaps.c"
#include "debugger.c"
#endif

#include "optimizer.c"
#ifdef JIT
#include "jit.c"
#endif

string read_file(string filename, uint64_t *program_length);
void evaluate(uint8_t program[]);

void usage(string exec) {
        fprintf(stderr, "usage: %s [-daocx] [--] program.b\n", exec);
        exit(1);
}

int32_t main(int32_t argc, string argv[]) {
        string filename;
        string addrmap_filename;
        unsigned char opt;

        while ((opt = getopt(argc, argv, "daoc")) != 0xff) {
                switch (opt) {
                    case 'd':
                    case 'o':
#ifndef DEBUGGER
                        fprintf(stderr, "This program was compiled without debugger support\n");
                        exit(1);
#endif
                        if (opt == 'd')
                                option_d = 1;
                        else
                                option_o = 1;
                        break;
                    case 'a':
#ifndef ASSERTS
                        fprintf(stderr, "This program was compiled without assert support\n");
                        exit(1);
#endif
                        option_a = 1;
                        break;
                    case 'c':
                        option_c = 1;
                        break;
                    case 'x':
                        option_x = 1;
                        break;
                    default: /* '?' */
                        usage(argv[0]);
                }
        }

        if (optind != argc - 1) usage(argv[0]);
        filename = argv[optind];
        
#ifdef _WIN32
        if (_setmode(_fileno(stdin), _O_BINARY) == -1 ||
            _setmode(_fileno(stdout), _O_BINARY) == -1) {
                perror("setting binary stdio mode");
                exit(1);
        }
#endif
        
        FILE *fd = fopen(filename, "r");
        if (!fd) {
                perror("opening file");
                exit(1);
        }

#ifdef DEBUGGER
if (option_d) {
        sourcemap_init();
}
#endif
        uint8_t *program = optimize(fd);

        tape = safe_malloc(HOT_TAPE * (sizeof (CELL)));

        load_page(tape, PAGE_COUNT-1);
        load_page(tape, 0);
        load_page(tape, 1);

#ifdef DEBUGGER
if (option_d) {
        addrmap_filename = safe_malloc(strlen(filename)+6);
        strcpy(addrmap_filename, filename);
        strcat(addrmap_filename, ".addr");
        load_addrmap(addrmap_filename);
}
#endif

        setvbuf(stdout, NULL, _IONBF, 0);
#ifdef JIT
        jit_run(program);
#else
        evaluate(program);
#endif

        fclose(fd);
        free(tape);
        free(program);
        return 0;
}

#ifndef IBF_PORTABLE_DISPATCH
const void* jumptable[0x100];

void evaluate(uint8_t program[]) {
#ifdef DEBUGGER
        if (option_d)
                debugger_init();
#endif
        register uint64_t pc = 0;
        register uint64_t dp = 0;
        register uint8_t *inst;
        register uint8_t last_page = 0;
#ifdef ASSERTS
        string assert_name;
        uint64_t assert_expected;
        uint64_t assert_got;
        uint64_t assert_comment;
#endif

        jumptable[0] = &&exit;
        jumptable['+'] = &&plus;
        jumptable['-'] = &&minus;
        jumptable['>'] = &&right;
        jumptable['r'] = &&right_wide;
        jumptable['<'] = &&left;
        jumptable['l'] = &&left_wide;
        jumptable['.'] = &&output;
        jumptable[','] = &&input;
        jumptable['['] = &&loopstart;
        jumptable[']'] = &&loopend;
        jumptable['^'] = &&copy;
        jumptable['0'] = &&zero;
#ifdef DEBUGGER
        jumptable['#'] = &&breakinst;
        jumptable['*'] = &&weak_breakinst;
#endif
#ifdef ASSERTS
        jumptable['@'] = &&assert_location;
        jumptable['!'] = &&assert_value;
#endif

#ifdef DEBUGGER

#ifdef DEBUGGER_STEP

#define NEXT \
        inst = &program[pc]; \
        if (option_d && CMD_cmd(inst) != '#' && CMD_cmd(inst) != '*') \
                debugger_call(BREAK_REASON_INSTRUCTION, tape, program, dp, pc); \
        goto *(jumptable[CMD_cmd(inst)]);

#else

#define NEXT \
        inst = &program[pc]; \
        goto *(jumptable[CMD_cmd(inst)]);
#endif

#else

#define NEXT \
        inst = &program[pc]; \
        goto *(jumptable[CMD_cmd(inst)]);

#endif

        NEXT

plus:
        tape[dp%HOT_TAPE]+=CMD_rol_arg(inst);
        pc+=2;
        NEXT

minus:
        tape[dp%HOT_TAPE]-=CMD_rol_arg(inst);
        pc+=2;
        NEXT


right:
        dp+=CMD_rol_arg(inst);
        CHECK_PAGE_TRANSITION(tape, 1, dp, last_page);
        pc+=2;
        NEXT

right_wide:
        dp+=CMD_wide_arg(inst);
        CHECK_PAGE_TRANSITION(tape, 1, dp, last_page);
        pc+=8;
        NEXT

left:
        dp-=CMD_rol_arg(inst);
        CHECK_PAGE_TRANSITION(tape, -1, dp, last_page);
        pc+=2;
        NEXT

left_wide:
        dp-=CMD_wide_arg(inst);
        CHECK_PAGE_TRANSITION(tape, -1, dp, last_page);
        pc+=8;
        NEXT

output:
#ifdef DEBUGGER
if (option_d && !option_o) {
        debugger_out(tape[dp%HOT_TAPE]);
} else {
        putchar(tape[dp%HOT_TAPE]);
}
#else
        putchar(tape[dp%HOT_TAPE]);
#endif
        pc+=1;
        NEXT

input:
        read(STDIN_FILENO, &tape[dp%HOT_TAPE], 1);
        pc+=1;
        NEXT

loopstart:
        if (!tape[dp%HOT_TAPE])
                pc=CMD_wide_arg(inst);
        pc+=8;
        NEXT

loopend:
        if (tape[dp%HOT_TAPE])
                pc=CMD_wide_arg(inst);
        pc+=8;
        NEXT

copy:
#define COPY(dir, invdir) \
        dp += CMD_copy_offset(inst); \
        CHECK_PAGE_TRANSITION(tape, dir, dp, last_page); \
        tape[dp%HOT_TAPE] += val; \
        dp -= CMD_copy_offset(inst); \
        CHECK_PAGE_TRANSITION(tape, invdir, dp, last_page); \
        pc+=8; \
        NEXT

        CELL val = tape[dp%HOT_TAPE] * CMD_copy_val(inst);

        if (val) {
                if (CMD_copy_offset(inst) > 0) {
                        COPY(1, -1)
                } else {
                        COPY(-1, 1)
                }
        }
        pc+=8;
        NEXT

zero:
        tape[dp%HOT_TAPE] = 0;
        pc+=1;
        NEXT

#ifdef DEBUGGER
breakinst:
        if (!option_d) { pc+=1; NEXT }
        debugger_call(BREAK_REASON_BREAKPOINT, tape, program, dp, pc);
        pc+=1;
        NEXT
weak_breakinst:
        if (!option_d) { pc+=1; NEXT }
        pc+=1;
        debugger_call(BREAK_REASON_WEAK_BREAKPOINT, tape, program, dp, pc);
        NEXT
#endif


#ifdef ASSERTS
assert_location:
        assert_name = "location";
        assert_got = dp;
        goto assert_common;

assert_value:
        assert_name = "value";
        assert_got = tape[dp%HOT_TAPE];
        /* fallthrough */

assert_common:
        assert_expected = CMD_wide_arg(inst);
        assert_comment = CMD_assert_com(inst);
        if (assert_expected != assert_got) {
                printf("assertion failed: %s\n", assert_name);
                if (assert_comment)
                        printf("comment:  0x%016lx\n", assert_comment);
                printf("expected: 0x%016lx\n", assert_expected);
                printf("got:      0x%016lx\n", assert_got);
#ifdef DUMP_TAPE
                printf("dumping tape.bin...\n");
                dump_tape();
#endif
                exit(1);
        }
        pc+=16;
        NEXT
#endif

exit:
#ifdef DEBUGGER
        if (option_d && !option_o)
                debugger_print_output();
#endif
        return;
}
#else
void evaluate(uint8_t program[]) {
#ifdef DEBUGGER
        debugger_init();
#endif
        uint64_t pc = 0;
        uint64_t dp = 0;
        uint8_t last_page = 0;

        for (;;) {
                uint8_t *inst = &program[pc];
#ifdef DEBUGGER
                if (option_d && CMD_cmd(inst) != '#' && CMD_cmd(inst) != '*') {
                        debugger_call(BREAK_REASON_INSTRUCTION, tape, program, dp, pc);
                }
#endif
                switch (CMD_cmd(inst)) {
                        case 0:
                                return;
                        case '+':
                                tape[dp % HOT_TAPE] += CMD_rol_arg(inst);
                                pc += 2;
                                break;
                        case '-':
                                tape[dp % HOT_TAPE] -= CMD_rol_arg(inst);
                                pc += 2;
                                break;
                        case '>':
                                dp += CMD_rol_arg(inst);
                                CHECK_PAGE_TRANSITION(tape, 1, dp, last_page);
                                pc += 2;
                                break;
                        case 'r':
                                dp += CMD_wide_arg(inst);
                                CHECK_PAGE_TRANSITION(tape, 1, dp, last_page);
                                pc += 8;
                                break;
                        case '<':
                                dp -= CMD_rol_arg(inst);
                                CHECK_PAGE_TRANSITION(tape, -1, dp, last_page);
                                pc += 2;
                                break;
                        case 'l':
                                dp -= CMD_wide_arg(inst);
                                CHECK_PAGE_TRANSITION(tape, -1, dp, last_page);
                                pc += 8;
                                break;
                        case '.':
#ifdef DEBUGGER
                                if (option_d && !option_o) {
                                        debugger_out(tape[dp % HOT_TAPE]);
                                } else {
                                        putchar(tape[dp % HOT_TAPE]);
                                }
#else
                                putchar(tape[dp % HOT_TAPE]);
#endif
                                pc += 1;
                                break;
                        case ',':
                                if (fread(&tape[dp % HOT_TAPE], 1, 1, stdin) != 1)
                                        tape[dp % HOT_TAPE] = 0;
                                pc += 1;
                                break;
                        case '[':
                                if (!tape[dp % HOT_TAPE]) {
                                        pc = CMD_wide_arg(inst);
                                }
                                pc += 8;
                                break;
                        case ']':
                                if (tape[dp % HOT_TAPE]) {
                                        pc = CMD_wide_arg(inst);
                                }
                                pc += 8;
                                break;
                        case '^': {
                                const int64_t offset = CMD_copy_offset(inst);
                                const CELL value = (CELL)(tape[dp % HOT_TAPE] * CMD_copy_val(inst));
                                if (value) {
                                        dp += offset;
                                        CHECK_PAGE_TRANSITION(tape, offset > 0 ? 1 : -1, dp, last_page);
                                        tape[dp % HOT_TAPE] += value;
                                        dp -= offset;
                                        CHECK_PAGE_TRANSITION(tape, offset > 0 ? -1 : 1, dp, last_page);
                                }
                                pc += 8;
                                break;
                        }
                        case '0':
                                tape[dp % HOT_TAPE] = 0;
                                pc += 1;
                                break;
#ifdef DEBUGGER
                        case '#':
                                if (option_d) {
                                        debugger_call(BREAK_REASON_BREAKPOINT, tape, program, dp, pc);
                                }
                                pc += 1;
                                break;
                        case '*':
                                if (option_d) {
                                        debugger_call(BREAK_REASON_WEAK_BREAKPOINT, tape, program, dp, pc);
                                }
                                pc += 1;
                                break;                                
#endif
#ifdef ASSERTS
                        case '@':
                        case '!': {
                                const char *assert_name = CMD_cmd(inst) == '@' ? "location" : "value";
                                const uint64_t assert_got = CMD_cmd(inst) == '@' ? dp : tape[dp % HOT_TAPE];
                                const uint64_t assert_expected = CMD_wide_arg(inst);
                                const uint64_t assert_comment = CMD_assert_com(inst);
                                if (assert_expected != assert_got) {
                                        printf("assertion failed: %s\n", assert_name);
                                        if (assert_comment) {
                                                printf("comment:  0x%016" PRIx64 "\n", assert_comment);
                                        }
                                        printf("expected: 0x%016" PRIx64 "\n", assert_expected);
                                        printf("got:      0x%016" PRIx64 "\n", assert_got);
#ifdef DUMP_TAPE
                                        printf("dumping tape.bin...\n");
                                        dump_tape();
#endif
                                        exit(1);
                                }
                                pc += 16;
                                break;
                        }
#endif
                        default:
                                fprintf(stderr, "invalid optimized opcode 0x%02x at pc=0x%" PRIx64 "\n",
                                        CMD_cmd(inst), pc);
                                exit(1);
                }
        }
}
#endif
