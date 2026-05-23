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

uint8_t proc_rol_small(string program_in, uint64_t *ind, struct vector *program_out, struct loop_data *ld) {
        uint8_t inst = program_in[*ind];
        uint8_t cur;
        uint8_t count;

        (*ind)++;
#ifdef COMPRESSED
        if (option_c) {
                uint64_t number = parse_decimal(program_in, ind);
                if (number == 0) {
                        count = 1;
                } else {
                        count = number;
                }
        } else {
                count = 1;
        }
#else
        count = 1;
#endif

        while (1) {
                cur = program_in[*ind];

                if (!cur)
                        break; // If reached EOF, exit
                if (is_ignored(cur)) {
                        (*ind)++;
                        continue;
                }
                if (cur != inst)
                        break;

                (*ind)++;
#ifdef COMPRESSED
                if (option_c) {
                        uint64_t number = parse_decimal(program_in, ind);
                        if (number == 0) {
                                count++;
                        } else {
                                count += number;
                        }
                } else {
                        count++;
                }
#else
                count++;
#endif
        }
        if (count != 0) {
                vector_push(program_out, inst);
                vector_push(program_out, count);
        }
        return 0;
}

uint8_t proc_rol_inst(string program_in, uint64_t *ind, struct vector *program_out, struct loop_data *ld) {
        uint8_t inst = program_in[*ind];
        uint8_t cur;
        uint64_t count;

        (*ind)++;
#ifdef COMPRESSED
        if (option_c) {
                uint64_t number = parse_decimal(program_in, ind);
                if (number == 0) {
                        count = 1;
                } else {
                        count = number;
                }
        } else {
                count = 1;
        }
#else
        count = 1;
#endif
        while (1) {
                cur = program_in[*ind];

                if (!cur)
                        break; // If reached EOF, exit
                if (is_ignored(cur)) {
                        (*ind)++;
                        continue;
                }
                if (cur != inst)
                        break;

                (*ind)++;
#ifdef COMPRESSED
                if (option_c) {
                        uint64_t number = parse_decimal(program_in, ind);
                        if (number == 0) {
                                count++;
                        } else {
                                count += number;
                        }
                } else {
                        count++;
                }
#else
                count++;
#endif
        }
        while (count >= ROLLING_TYPE_MAX) {
                vector_push(program_out, inst);
                _vector_push_multibyte_little(program_out, ROLLING_TYPE_MAX, sizeof(ROLLING_TYPE));
                count -= ROLLING_TYPE_MAX;
        }
        if (count > 0) {
                vector_push(program_out, inst);
                _vector_push_multibyte_little(program_out, count, sizeof(ROLLING_TYPE));
        }
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
        if (*ind & 0xff00000000000000) {
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

        while (is_ignored(program_in[++wind])) {}

        if (
                program_in[wind] != '-' &&
                program_in[wind] != '+'
        ) return -1;

        wind++;
        uint8_t number = parse_decimal(program_in, &wind);
        if ((number != 0) && (number != 1) && (number != 255)) return -1;

        while (is_ignored(program_in[wind]))
                wind++;
        if (program_in[wind] != ']') return -1;

        vector_push(program_out, '0');

        *ind = wind+1;
        return 0;
}

#define write_change { \
        int8_t key_found = 0; \
        uint64_t key_ind = 0; \
        for (uint64_t i = 0; i < offset_values.length; i++) { \
                if (vector_read_ex(&offset_keys, ROLLING_TYPE, i) == offset) { \
                        key_found = 1; \
                        key_ind = i; \
                        break; \
                } \
        } \
        if (!key_found) { \
                vector_push_ex(&offset_keys, ROLLING_TYPE, offset); \
                vector_push_ex(&offset_values, int8_t, change); \
        } else { \
                change += vector_read_ex(&offset_values, int8_t, key_ind); \
                vector_write_ex(&offset_values, int8_t, key_ind, change); \
        } \
        change = 0; \
}

uint8_t proc_move(string program_in, uint64_t *ind, struct vector *program_out, struct loop_data *ld) {
        uint64_t wind = *ind;

        struct vector offset_keys;   // ROLLING_TYPE (signed)
        struct vector offset_values; // char (signed)

#define destruct { \
        vector_drop(&offset_keys); \
        vector_drop(&offset_values); \
}

#define check_translate_left { \
        if (offset <= ROLLING_TYPE_MIN) { \
                destruct \
                return -1; \
        } \
}
#define check_translate_right { \
        if (offset >= ROLLING_TYPE_MAX) { \
                destruct \
                return -1; \
        } \
}

        vector_init(&offset_keys, 0);
        vector_init(&offset_values, 0);

        vector_push_ex(&offset_keys, ROLLING_TYPE, 0);
        vector_push(&offset_values, 0);

        int64_t offset = 0;

        wind++;  // skipping the loop opening

        int8_t change = 0;
        while (program_in[wind] != ']') {
                switch (program_in[wind]) {
                        case '<':
                                if (change != 0) write_change
                                wind++;
#ifdef COMPRESSED
                                if (option_c) {
                                        uint64_t number = parse_decimal(program_in, &wind);
                                        if (number == 0) {
                                                offset--;
                                                check_translate_left
                                        } else {
                                                offset -= number;
                                                check_translate_left
                                        }
                                } else {
                                        offset--;
                                        check_translate_left
                                }
#else
                                offset--;
                                check_translate_left
#endif
                                break;
                        case '>':
                                if (change != 0) write_change
                                wind++;
#ifdef COMPRESSED
                                if (option_c) {
                                        ROLLING_TYPE number = parse_decimal(program_in, &wind);
                                        if (number == 0) {
                                                offset++;
                                                check_translate_right
                                        } else {
                                                offset += number;
                                                check_translate_right
                                        }
                                } else {
                                        offset++;
                                        check_translate_right
                                }
#else
                                offset++;
                                check_translate_right
#endif
                                break;
                        case '+':
                                wind++;
                                change++;
                                break;
                        case '-':
                                wind++;
                                change--;
                                break;
                        case '[':
                        case ']':
                        case ',':
                        case '.':
                                destruct
                                return -1;
                        default:
                                wind++;
                }
        }
        if (change != 0) write_change
        wind++;

        if (offset != 0) { /* unbalanced loop */
                destruct
                return -1;
        }


        /* output the instructions */

        if ((int8_t)offset_values.ptr[0] != -1) {
                destruct
                return -1;
        }

        for (uint32_t i = 1; i < offset_values.length; i++) {
                vector_push(program_out, '^');
                vector_push_ex(program_out, ROLLING_TYPE, vector_read_ex(&offset_keys, ROLLING_TYPE, i));
                vector_push(program_out, offset_values.ptr[i]);
        }
        vector_push(program_out, '0');

        *ind = wind;
        return 0;
}

uint8_t process_instruction(string program_in, uint64_t *ind, struct vector *program_out, struct loop_data *ld) {
#ifdef DEBUGGER
if (option_d)
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
                case '+': case '-':
                        CALL_PROC(proc_rol_small);
                case '>': case '<':
                        CALL_PROC(proc_rol_inst);
                case '.': case ',':
                        CALL_PROC(proc_unrol_inst);
#ifdef DEBUGGER
                case '#':
                        if (!option_d) goto ignore;
                        CALL_PROC(proc_unrol_inst);
#endif
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
                        if (!option_a) goto ignore;
                        CALL_PROC(proc_assert);
#endif
ignore:
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
if (option_d) {
        printf("constructing program...\n");
}
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
if (option_d) {
        printf("done\n");
        sourcemap_end(program_out.length-1);
}
#endif
        for (int32_t i = 0; i < 8; i++) {
                vector_push(&program_out, 0);
        }
        return vector_unwrap(&program_out);
}
