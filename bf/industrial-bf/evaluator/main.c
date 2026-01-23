/* ibf -- The industrial brainfuck interpreter
 * 
 * This file is licensed under the BSD Zero Clause License
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#include <arpa/inet.h>

#else
#include "win_byteorder.c"
#endif

#include <errno.h>

#pragma GCC poison long
#pragma GCC poison int

typedef char* string;
typedef char character;

uint8_t option_d;
uint8_t option_a;

#include "util.c"
#include "vector.c"
#include "infinite-tape.c"
#include "command.c"

#ifdef DEBUGGER
#include "sourcemaps.c"
#include "debugger.c"
#endif

#include "optimizer.c"

#include "config.h"

string read_file(string filename, uint64_t *program_length);
void evaluate(uint8_t program[], CELL *tape);

void usage(string exec) {
        fprintf(stderr, "usage: %s [-da] [--] program.b\n",
                exec);
        exit(1);
}

int32_t main(int32_t argc, string argv[]) {
        string filename;
        string addrmap_filename;
        unsigned char opt;

        while ((opt = getopt(argc, argv, "da")) != 0xff) {
                switch (opt) {
                    case 'd':
#ifndef DEBUGGER
                        fprintf(stderr, "This program was compiled without debugger support\n");
                        exit(1);
#endif
                        option_d = 1;
                        break;
                    case 'a':
#ifndef ASSERTS
                        fprintf(stderr, "This program was compiled without assert support\n");
                        exit(1);
#endif
                        option_a = 1;
                        break;
                    default: /* '?' */
                        usage(argv[0]);
                }
        }

        if (optind != argc - 1) usage(argv[0]);
        filename = argv[optind];
        
        uint64_t program_length;
        string program_raw = (string) read_file(filename, &program_length);
        if (!program_raw) exit(1);

#ifdef DEBUGGER
if (option_d) {
        sourcemap_init();
}
#endif
        uint8_t *program = optimize(program_raw);

        CELL *tape = safe_malloc(HOT_TAPE * (sizeof (CELL)));
        memset(tape, 0, HOT_TAPE * (sizeof (CELL)));

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

        evaluate(program, tape);
}

const void* jumptable[0x100];

void evaluate(uint8_t program[], CELL tape[]) {
#ifdef DEBUGGER
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
#endif

        jumptable[0] = &&exit;
        jumptable['+'] = &&plus;
        jumptable['-'] = &&minus;
        jumptable['>'] = &&right;
        jumptable['<'] = &&left;
        jumptable['.'] = &&output;
        jumptable[','] = &&input;
        jumptable['['] = &&loopstart;
        jumptable[']'] = &&loopend;
        jumptable['/'] = &&divide;
        jumptable['\\'] = &&invdivide;
        jumptable['^'] = &&copy;
        jumptable['0'] = &&zero;
#ifdef DEBUGGER
        jumptable['#'] = &&breakinst;
#endif
#ifdef ASSERTS
        jumptable['@'] = &&assert_location;
        jumptable['!'] = &&assert_value;
#endif

#ifdef DEBUGGER

#define NEXT \
        inst = &program[pc]; \
        if (option_d && CMD_cmd(inst) != '#') \
                debugger_call(BREAK_REASON_INSTRUCTION, tape, program, dp, pc); \
        goto *(jumptable[CMD_cmd(inst)]);

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

left:
        dp-=CMD_rol_arg(inst);
        CHECK_PAGE_TRANSITION(tape, -1, dp, last_page);
        pc+=2;
        NEXT

output:
#ifdef DEBUGGER
if (option_d) {
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
        character buf[2];
        buf[0] = tape[dp%HOT_TAPE];
        fgets(buf, 2, stdin);
        tape[dp%HOT_TAPE] = buf[0];
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

divide:
        if (tape[dp%HOT_TAPE] % CMD_simple_arg(inst)) {
#ifdef DEBUGGER
if (option_d) {
                printf("warning: going into an infinite loop\n");
}
#endif
                while (1) {}
        }

        tape[dp%HOT_TAPE] /= CMD_simple_arg(inst);
        pc+=2;
        NEXT

invdivide:
        tape[dp%HOT_TAPE] = -tape[dp%HOT_TAPE];
        
        if (tape[dp%HOT_TAPE] % CMD_simple_arg(inst))
                while (1) {}

        tape[dp%HOT_TAPE] /= CMD_simple_arg(inst);

        pc+=2;
        NEXT

copy:
#define COPY(dir, invdir) \
        dp += CMD_copy_offset(inst); \
        CHECK_PAGE_TRANSITION(tape, dir, dp, last_page); \
        tape[dp%HOT_TAPE] += val; \
        dp -= CMD_copy_offset(inst); \
        CHECK_PAGE_TRANSITION(tape, invdir, dp, last_page); \
        pc+=4; \
        NEXT

        CELL val = tape[dp%HOT_TAPE] * CMD_copy_val(inst);

        if (val) {
                if (CMD_copy_offset(inst) > 0) {
                        COPY(1, -1)
                } else {
                        COPY(-1, 1)
                }
        }
        pc+=4;
        NEXT

zero:
        tape[dp%HOT_TAPE] = 0;
        pc+=1;
        NEXT

#ifdef DEBUGGER
breakinst:
        if (!option_d) { NEXT }
        debugger_call(BREAK_REASON_BREAKPOINT, tape, program, dp, pc);
        pc+=1;
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
        if (assert_expected != assert_got) {
                printf("assertion failed: %s\n", assert_name);
                printf("expected: 0x%lx\n", assert_expected);
                printf("got: 0x%lx\n", assert_got);
                exit(1);
        }
        pc+=8;
        NEXT
#endif

exit:
        return;
}
