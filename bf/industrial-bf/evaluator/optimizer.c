// bfm - Converts BrainF to BrainFMacros

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct loop_data {
        uint32_t sp;
        uint64_t stack[0x1000];
};

#define LD_PUSH(ld, i) \
        if (ld->sp == 0xfff) { \
                printf("error: stack overflow\n"); \
                return 1; \
        } \
        ld->stack[ld->sp] = i; \
        ld->sp++

#define LD_POP(ld, i) \
        if (ld->sp == 0) { \
                printf("error: stack underflow\n"); \
                return 1; \
        } \
        ld->sp--; \
        i = ld->stack[ld->sp];

uint8_t proc_rol_inst(string program_in, uint64_t *ind, struct vector *program_out, struct loop_data *ld) {
        uint8_t inst = program_in[*ind];
        unsigned ind1 = *ind;
        uint8_t cur;
        uint32_t count = 1;

#ifdef DEBUGGER
#define is_ignored(a) (is_whitespace(a) && a != '\n')
#else
#define is_ignored is_comment
#endif

        while (1) {
                (*ind)++;
                cur = program_in[*ind];

                if (!cur)
                        break; // If reached EOF, exit

                if (cur != inst
                 && !is_ignored(cur)) 
                        break;

                if (count >= 256)
                        break;

                if (!is_ignored(cur))
                        count++;
        }
        vector_push(program_out, inst);
        vector_push(program_out, (uint8_t)count-1);
        return 0;
}

uint8_t proc_unrol_inst(string program_in, uint64_t *ind, struct vector *program_out, struct loop_data *ld) {
        vector_push(program_out, program_in[*ind]);
        (*ind)++;
        return 0;
}

uint8_t proc_open_loop(string program_in, uint64_t *ind, struct vector *program_out, struct loop_data *ld) {
        LD_PUSH(ld, program_out->length);
        vector_push_ex(
                program_out,
                uint64_t,
                0xaaaaaaaaaaaaaaaa
        ); // Mock instruction
        (*ind)++;
        return 0;
}

uint8_t proc_close_loop(string program_in, uint64_t *ind, struct vector *program_out, struct loop_data *ld) {
        uint64_t start_ind;
        if (*ind & 0xff000000) {
                printf("error: index overflow\n");
                return 1;
        }

        LD_POP(ld, start_ind);

        write_long(
                program_out->ptr + start_ind,
                program_out->length | (((int64_t)'[') << (8*7))
        );

        vector_push_ex(
                program_out,
                uint64_t,
                start_ind | (((int64_t)']') << (8*7))
        );
        (*ind)++;
        return 0;
}

uint8_t proc_assert(string program_in, uint64_t *ind, struct vector *program_out, struct loop_data *ld) {
        uint8_t inst = program_in[*ind];
        int8_t digit;
        (*ind)++;
        uint64_t val = parse_number(program_in, ind);
        if (val&0xff00000000000000) {
                printf("error: `%c` assert value overflow: %lx\n", inst, val);
                return 1;
        }

        vector_push_ex(
                program_out,
                uint64_t,
                val | (((int64_t)inst) << (8*7))
        );
        return 0;
}

uint8_t proc_zero(string program_in, uint64_t *ind, struct vector *program_out, struct loop_data *ld) {
        uint64_t wind = *ind;

        wind++;
        if (
                program_in[wind] != '-' &&
                program_in[wind] != '+'
        ) return -1;

        if (program_in[++wind] != ']') return -1;

        vector_push(program_out, '0');

        *ind = wind+1;
        return 0;
}

uint8_t proc_move(string program_in, uint64_t *ind, struct vector *program_out, struct loop_data *ld) {
        uint64_t wind = *ind;

        struct vector offset_keys;   // short (signed)
        struct vector offset_values; // char (signed)

        vector_init(&offset_keys, 0);
        vector_init(&offset_values, 0);

        vector_push_ex(&offset_keys, int16_t, 0);
        vector_push(&offset_values, 0);

        int16_t offset = 0;

        while (program_in[++wind/*skipping the loop opening*/] != ']') {
                int8_t change;
                int8_t key_found;
                uint64_t key_ind;
                switch (program_in[wind]) {
                        case '<': offset--; break;
                        case '>': offset++; break;
                        case '+':
                                change = 1;
                                goto write_change;
                        case '-':
                                change = -1;
                                goto write_change;
                        write_change:
                                key_found = 0;

                                for (uint64_t i = 0; i < offset_keys.length/2; i++) {
                                        if (vector_read_ex(&offset_keys, int16_t, i) == offset) {
                                                key_found = 1;
                                                key_ind = i;
                                                break;
                                        }
                                }
                                if (!key_found) {
                                        vector_push_ex(&offset_keys, int16_t, offset);
                                        vector_push_ex(&offset_values, int8_t, change);
                                } else {
                                        change += vector_read_ex(&offset_values, int8_t, key_ind);
                                        vector_write_ex(&offset_values, int8_t, key_ind, change);
                                }

                                break;
                        case '[':
                        case ']':
                        case ',':
                        case '.':
                                return -1;
                }
        }
        wind++;

        if (offset != 0) /* unbalanced loop */
                return -1;


        /* output the instructions */

        if ((int8_t)offset_values.ptr[0] < -1) {
                vector_push(program_out, '/');
                vector_push(program_out, -offset_values.ptr[0]);
        }

        if ((int8_t)offset_values.ptr[0] > 1) {
                vector_push(program_out, '\\');
                vector_push(program_out, offset_values.ptr[0]);
        }

        for (uint32_t i = 1; i < offset_values.length; i++) {
                vector_push(program_out, '^');
                vector_push_ex(program_out, int16_t, vector_read_ex(&offset_keys, int16_t, i));
                vector_push(program_out, offset_values.ptr[i]);
        } 
        vector_push(program_out, '0');

        *ind = wind;
        return 0;
}

uint8_t process_instruction(string program_in, uint64_t *ind, struct vector *program_out, struct loop_data *ld) {
#ifdef DEBUGGER
        sourcemap_process(
                program_in[*ind],
                program_out->length
        );
#endif

#define CALL_PROC(fn) { \
        int8_t out = fn(program_in, ind, program_out, ld); \
        if (out != -1) { \
                return out; \
        } \
}

        switch (program_in[*ind]) {
                case '+': case '-': case '>': case '<':
                        CALL_PROC(proc_rol_inst);

                case '.': case ',':
#ifdef DEBUGGER
                case '#':
#endif
                        CALL_PROC(proc_unrol_inst);
                case '[':
#ifndef DISABLE_ROLLING
                        CALL_PROC(proc_zero);
                        CALL_PROC(proc_move);
#endif
                        CALL_PROC(proc_open_loop);
                case ']':
                        CALL_PROC(proc_close_loop);
#ifdef ASSERTS
                case '@':
                case '!':
                        CALL_PROC(proc_assert);
#endif
                default:
                        // Comment
                        (*ind)++;
                        return 0;
        }
}

uint8_t *optimize(string program_in) {
        struct vector program_out = vector_create(0);

        uint64_t ind = 0;
        character last_char = 0;
        character cur_char;
        int32_t count = -1;

        // Loop optimization
        struct loop_data ld;
        ld.sp = 0;

#ifdef DEBUGGER
        printf("constructing program...\n");
#endif
        while (program_in[ind]) {
                uint8_t out = process_instruction(program_in, &ind, &program_out, &ld);
                if (out) {
                        exit(out);
                }
        }
        if (ld.sp) {
                printf("error: nonempty stack");
                exit(1);
        }
#ifdef DEBUGGER
        printf("done\n");
        sourcemap_end(program_out.length-1);
#endif
        for (int32_t i = 0; i < 8; i++) {
                vector_push(&program_out, 0);
        }
        return vector_unwrap(&program_out);
}
