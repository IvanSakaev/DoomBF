// bfm - Converts BrainF to BrainFMacros

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct loop_data {
        uint32_t sp;
        uint64_t stack[0x1000];
};

struct revertable_stream {
        FILE *fd;
        int fildes;
        struct vector *pushback;
        size_t pushback_pos;
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

#define READ_BLOCK_SIZE2 ((uint64_t)READ_BLOCK_SIZE)

static inline size_t read_revertable_direct(character *ptr, struct revertable_stream *stream) {
        size_t new_capacity = stream->pushback->length+READ_BLOCK_SIZE2;
        vector_extend(stream->pushback, new_capacity);
        uint8_t *buf = &stream->pushback->ptr[stream->pushback->length];

        size_t readb = 0;
        while (1) {
                readb = read(stream->fildes, buf, READ_BLOCK_SIZE2);
                if (readb) break;

                if (errno == EAGAIN) {
                        continue;
                }
                return 0;
        }
        if (readb == 0) return 0;
        stream->pushback->length += readb;
        *ptr = buf[0];
        return 1;
}

static inline size_t read_revertable(character *ptr, struct revertable_stream *stream) {
        if (stream->pushback_pos >= (stream->pushback)->length) {
                if (!read_revertable_direct(ptr, stream)) {
                        return 0;
                }
                stream->pushback_pos++;
                return 1;
        }
        *ptr = stream->pushback->ptr[stream->pushback_pos++];
        return 1;
}

static inline size_t peek_revertable(character *ptr, struct revertable_stream *stream) {
        if (stream->pushback_pos >= (stream->pushback)->length) {
                if (!read_revertable_direct(ptr, stream)) {
                        return 0;
                }
                return 1;
        }
        *ptr = stream->pushback->ptr[stream->pushback_pos];
        return 1;
}

#define READ_CHAR(ptr, stream) { \
        if (read_revertable(ptr, stream) != 1) { \
                if (feof(stream->fd) || !errno) { \
                        *ptr = EOF; \
                } else { \
                        perror("reading file"); \
                        return 1; \
                } \
        } \
}

#define READ_CHAR_NOEOF(ptr, stream) { \
        READ_CHAR(ptr, stream); \
        if (*(ptr) == EOF) { \
                printf("error: unexpected EOF\n"); \
                return 1; \
        } \
}

#define PEEK_CHAR(ptr, stream) { \
        if (peek_revertable(ptr, stream) != 1) { \
                if (feof(stream->fd) || !errno) { \
                        *ptr = EOF; \
                } else { \
                        perror("peeking file"); \
                        return 1; \
                } \
        } \
}

uint64_t stream_read_number(struct revertable_stream *program_in) {
        uint64_t val = 0;
        int8_t digit;
        character chr;
        while (1) {
                PEEK_CHAR(&chr, program_in);
                digit = parse_digit(chr);
                if (digit == -1)
                        break;
                val <<= 4;
                val += digit;
                program_in->pushback_pos++;
        }
        return val;
}

#include "compression.c"

#ifdef FAST_ROL
#include "fast_proc_rol.c"
#endif

static inline uint8_t proc_rol_inst(struct revertable_stream *program_in, struct vector *program_out, struct loop_data *ld) {
        character inst;
        READ_CHAR_NOEOF(&inst, program_in);
        uint8_t allow_wide = inst == '>' || inst == '<';
        uint64_t wide_limit = 1<<PAGE_SIZE_POWER;

#ifdef FAST_ROL
        if (allow_wide) {
                program_in->pushback_pos--;
                return fast_proc_rol_inst(program_in, program_out, ld);
        }
#endif

        character cur;
        uint32_t count = 1;

#ifdef DEBUGGER
#define is_ignored(a) (is_whitespace(a) && a != '\n')
#else
#define is_ignored is_comment
#endif

        while (1) {
                PEEK_CHAR(&cur, program_in);

                if (cur == EOF)
                        break; // If reached EOF, exit

                if (cur != inst
                 && !is_ignored(cur)) 
                        break;

                if (count >= 256 && !allow_wide)
                        break;

                if (count >= wide_limit)
                        break;

                READ_CHAR(&cur, program_in);

                if (!is_ignored(cur))
                        count++;
        }
        if (count <= 256) {
                vector_push(program_out, inst);
                vector_push(program_out, (uint8_t)count-1);
        } else {
                if (inst == '>') inst = 'r';
                if (inst == '<') inst = 'l';
                vector_push_ex(
                        program_out,
                        uint64_t,
                        count | (((int64_t)inst) << (8*7))
                );
        }
        return 0;
}

static inline uint8_t proc_unrol_inst(struct revertable_stream *program_in, struct vector *program_out, struct loop_data *ld) {
        character chr;
        READ_CHAR_NOEOF(&chr, program_in);
        vector_push(program_out, chr);
        return 0;
}

static inline uint8_t proc_open_loop(struct revertable_stream *program_in, struct vector *program_out, struct loop_data *ld) {
        character chr;
        READ_CHAR_NOEOF(&chr, program_in);

        LD_PUSH(ld, program_out->length);
        vector_push_ex(
                program_out,
                uint64_t,
                0xaaaaaaaaaaaaaaaa
        ); // Mock instruction
        return 0;
}

static inline uint8_t proc_close_loop(struct revertable_stream *program_in, struct vector *program_out, struct loop_data *ld) {
        character chr;
        READ_CHAR_NOEOF(&chr, program_in);

        uint64_t start_ind;
        if (program_out->length & 0xff00000000000000) {
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
        return 0;
}

static inline uint8_t proc_assert(struct revertable_stream *program_in, struct vector *program_out, struct loop_data *ld) {
        character inst;
        character cur;
        READ_CHAR_NOEOF(&inst, program_in);

        uint64_t val = stream_read_number(program_in);
        uint64_t comment = 0;
        if (val&0xff00000000000000) {
                printf("error: `%c` assert value overflow: %lx\n", inst, val);
                return 1;
        }

        PEEK_CHAR(&cur, program_in);
        if (cur == '/') {
                program_in->pushback_pos++;
                comment = stream_read_number(program_in);
        }

        vector_push_ex(
                program_out,
                uint64_t,
                val | (((int64_t)inst) << (8*7))
        );
        vector_push_ex(
                program_out,
                uint64_t,
                comment
        );
        return 0;
}

static inline uint8_t proc_zero(struct revertable_stream *program_in, struct vector *program_out, struct loop_data *ld) {
        character chr;
        READ_CHAR_NOEOF(&chr, program_in);

        READ_CHAR_NOEOF(&chr, program_in);
        if (
                chr != '-' &&
                chr != '+'
        ) return -1;

        READ_CHAR_NOEOF(&chr, program_in);
        if (chr != ']') return -1;

        vector_push(program_out, '0');
        return 0;
}

static inline int64_t i64abs(int64_t v) {
        if (v < 0)
                return -v;
        return v;
}

static inline uint8_t proc_move(struct revertable_stream *program_in, struct vector *program_out, struct loop_data *ld) {
        character chr;
        READ_CHAR_NOEOF(&chr, program_in);
        uint64_t limit = 1<<PAGE_SIZE_POWER;

        /* static temporarely removed */
        uint8_t offset_init = 0;
        struct vector offset_keys;   // short (signed)
        struct vector offset_values; // char (signed)
        
        if (!offset_init) {
                vector_init(&offset_keys, 0);
                vector_init(&offset_values, 0);
                offset_init = 1;
        } else {
                offset_keys.length = 0;
                offset_values.length = 0;
        }

        vector_push_ex(&offset_keys, int64_t, 0);
        vector_push(&offset_values, 0);

        int64_t offset = 0;

        while (1) {
                READ_CHAR_NOEOF(&chr, program_in);
                if (chr == ']') break;

                int8_t change;
                int8_t key_found;
                uint64_t key_ind;
#define CR() (option_c ? (comp_read(program_in) + 1) : 1)
                switch (chr) {
                        case '<':
                                offset -= CR();
                                break;
                        case '>':
                                offset += CR();
                                break;
                        case '+':
                                change = CR();
                                goto write_change;
                        case '-':
                                change = -CR();
                                goto write_change;
                        write_change:
                                if (i64abs(offset) > limit)
                                        return -1;
                                key_found = 0;

                                for (uint64_t i = 0; i < offset_keys.length/8; i++) {
                                        if (vector_read_ex(&offset_keys, int64_t, i) == offset) {
                                                key_found = 1;
                                                key_ind = i;
                                                break;
                                        }
                                }
                                if (!key_found) {
                                        vector_push_ex(&offset_keys, int64_t, offset);
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
                        default:
#ifdef ASSERTS
                                if (chr == '@'
                                 || chr == '!')
                                        return -1;
#endif
#ifdef DEBUGGER
                                if (chr == '#' || chr == '*')
                                        return -1;
#endif
                }
        }

        if (offset != 0) { /* unbalanced loop */
                return -1;
        }


        /* output the instructions */

        if ((int8_t)offset_values.ptr[0] != -1) {
                return -1;
        }

        for (uint32_t i = 1; i < offset_values.length; i++) {
                uint64_t v = vector_read_ex(&offset_keys, int64_t, i);
                vector_push_ex(
                        program_out,
                        uint64_t,
                          v&0x0000ffffffffffff
                        | (((int64_t)offset_values.ptr[i]) << (8*6))
                        | (((int64_t)'^') << (8*7))
                );
        } 
        vector_push(program_out, '0');

        return 0;
}

/* return values: -1 -- EOF
 *                 0 -- success
 *             other -- error */
static inline uint8_t process_instruction(struct revertable_stream *program_in, struct vector *program_out, struct loop_data *ld) {

/* fn return values: -1 -- no match occured
 *                    0 -- success
 *                other -- error */
#define CALL_PROC(fn) do { \
        size_t pos = program_in->pushback_pos; \
        int8_t out = fn(program_in, program_out, ld); \
        if (out != -1) { \
                if (program_in->pushback_pos > ROLLBACK_CLEAR_MIN_SIZE) { \
                        vector_truncate_start(program_in->pushback, program_in->pushback_pos); \
                        program_in->pushback_pos = 0; \
                } \
                return out; \
        } else { \
                program_in->pushback_pos = pos; \
        } \
} while(0)

        character chr;
        uint8_t rout = peek_revertable(&chr, program_in);
        if (rout == 0 && (feof(program_in->fd) || !errno)) {
                /* EOF */
                return -1;
        }
        if (rout != 1) {
                perror("reading file");
                return 1;
        }

#ifdef DEBUGGER
if (option_d)
        sourcemap_process(
                chr,
                program_out->length
        );
#endif
        switch (chr) {
                case '+': case '-': case '>': case '<':
                        if (!option_c)
                        CALL_PROC(proc_rol_inst);
                        else
                        CALL_PROC(proc_rol_inst_comp);

                case '.': case ',':
                        CALL_PROC(proc_unrol_inst);
#ifdef DEBUGGER
                case '#':
                case '*':
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
                        if (option_c && chr == '{') {
                                printf("unexpected {\n");
                                return 1;
                        }
                        // Comment
                        read_revertable(&chr, program_in);
                        return 0;
        }
}

uint8_t *optimize(FILE *program_in) {
        struct vector program_out = vector_create(0);

        uint64_t ind = 0;
        character last_char = 0;
        character cur_char;
        int32_t count = -1;

        // Loop optimization
        struct loop_data ld;

        struct vector pushback_vec;
        vector_init(&pushback_vec, 0);
        struct revertable_stream stream;
        stream.fd = program_in;
        stream.fildes = fileno(program_in);
        stream.pushback = &pushback_vec;
        stream.pushback_pos = 0;

        ld.sp = 0;

#ifdef DEBUGGER
if (option_d) {
        printf("constructing program...\n");
}
#endif
        while (1) {
                int8_t out = process_instruction(&stream, &program_out, &ld);
                if (out == -1) {
                        break;
                }
                if (out) {
                        printf("unable to process file: %d\n", out);
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
        vector_drop(&pushback_vec);
        return vector_unwrap(&program_out);
}
