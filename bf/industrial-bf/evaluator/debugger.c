#define BREAK_REASON_INSTRUCTION ((uint8_t)1)
#define BREAK_REASON_BREAKPOINT ((uint8_t)2)

#define TYPOGRAPHIC_CELL_WIDTH ((sizeof (CELL)) * 2)

#define B_FG "\033[36m"
#define G_FG "\033[32m"
#define FG_r "\033[0m"
#define FADE "\033[2m"
#define FADE_r "\033[22m"

enum dbg_state {
        DBG_STEP,
        DBG_RUN,
        DBG_EXIT_SEARCH,
        DBG_EXIT_RUN,
        DBG_SM_NEXT
};

enum dbg_state debugger_state = DBG_STEP;
uint64_t exit_search_depth;
uint64_t exit_target;
uint64_t sm_next_target;

enum addrmap_mode {
        MODE_HEX,
        MODE_DEC,
        MODE_STRING,
        MODE_BYTES
};

struct addrmap_value {
        character name[65];
        enum addrmap_mode mode;
        uint64_t len;
        uint64_t addr;
        uint64_t step;
};

struct addrmap {
        uint64_t count;
        struct addrmap_value *values;
};

struct addrmap active_addrmap = {0};

#define max_output_len 16
uint8_t output_len;
uint8_t output_buf[max_output_len];

void debugger_help() {
        printf(
                "? - display help\n"
                "r - run the program until a breakpoint is reached\n"
                "s - step one instruction forward\n"
                "x - run until the current loop ends\n"
                "n - run until the next sourcemap line\n"
                "a - load an addrmap file\n"
                "q - quit\n"
        );
}

void load_addrmap_line(character line[], struct addrmap_value *out) {
        enum addrmap_mode mode;
        uint64_t addr;
        uint64_t len = 1;
        uint64_t step = 1;

        switch (line[0]) {
                case 'x':
                        mode = MODE_HEX;
                        break;
                case 'd':
                        mode = MODE_DEC;
                        break;
                case 's':
                        mode = MODE_STRING;
                        break;
                case 'a':
                        mode = MODE_BYTES;
                        break;
                default:
                        printf("invalid mode: %c", line[0]);
                        return;
        }

        uint64_t ind = 1;
        addr = parse_number(line, &ind);

        if (line[ind] == '[') {
                ind++;
                len = parse_number(line, &ind);
                if (line[ind] == '/') {
                        ind++;
                        step = parse_number(line, &ind);
                }
                ind++;
        }
        ind++;

        strcpy(out->name, &line[ind]);
        out->mode = mode;
        out->len = len;
        out->addr = addr;
        out->step = step;

        printf("@%lx M: %x L: %lx S: %lx N: %s\n", addr, out->mode, out->len, out->step, out->name);
}

uint64_t count_nonempty_lines(string addrmap, uint64_t length) {
        uint8_t newline = 1;
        uint64_t lines = 0;
        for (uint64_t ind = 0; ind < length; ind++) {
                if (addrmap[ind] == '\n') {
                        if (!newline) {
                                lines++;
                        }
                        newline = 1;
                } else {
                        newline = 0;
                }
        }
        if (!newline)
                lines++;
        return lines;
}

void load_addrmap(string filename) {
        printf("loading addrmap: %s. ", filename);
        character line[65] = {0};
        uint64_t ind;
        uint64_t lineind = 0;
        uint64_t length;
        uint64_t lines;

        string addrmap = read_file(filename, &length);
        if (!addrmap) return;

        lines = count_nonempty_lines(addrmap, length);
        printf("%ld lines\n", lines);

        if (active_addrmap.values) {
                free(active_addrmap.values);
                printf("unloaded old attrmap\n");
        }
        active_addrmap.count = 0;
        active_addrmap.values = malloc(sizeof (struct addrmap_value) * lines);

        for (ind = 0; ind < length; ind++) {
                if (addrmap[ind] == '\n') {
load_line:
                        line[lineind] = 0;
                        if (line[0]) {
                                load_addrmap_line(
                                        line,
                                        &active_addrmap.values[
                                            active_addrmap.count
                                        ]
                                );
                                active_addrmap.count++;
                        }
                        lineind = 0;
                        line[0] = 0;
                } else {
                        line[lineind] = addrmap[ind];
                        lineind++;
                }
        }
        if (line[0])
                goto load_line;
}

struct sourcemap_entry *read_sm_entry(uint64_t ind) {
        return (
            (struct sourcemap_entry*)
            ntohll(
                *(uint64_t*)
                &sourcemap.entries.ptr[(ind)*8]
            )
        );
}

uint64_t get_current_sm_ind(uint64_t pc) {
        uint64_t count = sourcemap.entries.length/8;
        uint64_t ind = 0;
        while (1) {
                ind++;
                if (ind >= count)
                        break;
                if ((read_sm_entry(ind)->ind) > pc)
                        break;
        }
        ind--;
        return ind;
}

character readline(string out, uint32_t len) {
        character buf[2];
        uint8_t ret = 0;
        uint32_t pos = 0;
        do {
                fgets(buf, 2, stdin);
                if (buf[0] != '\n') {
                        if (pos >= len)
                                ret = 1;
                        else
                                out[pos++] = buf[0];
                }
        } while (buf[0] && buf[0] != '\n');
        out[pos] = 0;
        return ret;
}

uint8_t last_cmd = 0;

void debugger_cmd(uint64_t pc) {
        uint8_t buf[2];
        character filename[17];
        uint64_t len;

        while (1) {
                printf(G_FG "$ " FG_r);
skip_prompt:
                if (readline(buf, 1))
                        buf[0] = 0xff;

                if (!buf[0])
                        buf[0] = last_cmd;

                last_cmd = buf[0];

                switch (buf[0]) {
                        case '\n':
                        case '\r':
                                goto skip_prompt;
                        case '?':
                                debugger_help();
                                break;
                        case 'r':
                                debugger_state = DBG_RUN;
                                goto continue_execution;
                        case 's':
                                debugger_state = DBG_STEP;
                                goto continue_execution;
                        case 'x':
                                exit_search_depth = 0;
                                exit_target = 0;
                                debugger_state = DBG_EXIT_SEARCH;
                                goto continue_execution;
                        case 'n':
                                sm_next_target = get_current_sm_ind(pc)+1;
                                debugger_state = DBG_SM_NEXT;
                                goto continue_execution;
                        case 'a':
                                printf(G_FG "addrmap: " FG_r);
                                fgets(filename, 16, stdin);
                                len = strlen(filename);
                                if (filename[len-1] == 0xa) {
                                        filename[len-1] = 0;
                                }
                                load_addrmap(filename);
                                break;
                        case 'q':
                                exit(0);
                        default:
                                printf("'?' for help\n");
                                break;
                }
        }
continue_execution:
        return;
}

void debugger_init() {
        debugger_state = DEBUGGER_DEFAULT_STATE;
        output_len = 0;
}

void debugger_out(uint8_t chr) {
        output_len++;
        if (output_len > max_output_len) {
                output_len--;
                for (uint32_t i = 1; i < max_output_len; i++) {
                        output_buf[i-1] = output_buf[i];
                }
        }
        output_buf[output_len-1] = chr;
}

void show_char(uint8_t chr) {
        if (chr == '"')
                printf("\\\"");

        else if (chr == '\\')
                printf("\\\\");

        else if (chr == '\n')
                printf("\\n");

        else if (chr == '\r')
                printf("\\r");

        else if (chr == '\t')
                printf("\\t");

        else if (chr >= ' ' && chr <= '~')
                printf("%c", chr);

        else
                printf("\\%o", chr);
}

void show_string(CELL tape[], uint64_t addr, uint64_t length, uint64_t step, uint64_t dp) {
        uint8_t elipsis = 0;
        uint8_t string_open = 0;
        uint8_t start = 1;
        uint8_t val;
        uint64_t cur_addr;
        printf(FG_r);

#define OPEN_STRING_EX(s) { \
        if (!string_open) { \
                if (!start) \
                        printf(" "); \
                printf(s "\""); \
        } \
        string_open = 1; \
}

#define OPEN_STRING OPEN_STRING_EX("")

#define CLOSE_STRING { \
        if (string_open) { \
                printf("\""); \
                if (val) \
                        printf(B_FG "!" FG_r); \
        } \
        string_open = 0; \
}

#define ELIPSIS { \
        if (!elipsis) { \
                CLOSE_STRING \
                printf(FADE "..." FADE_r); \
        } \
        elipsis = 1; \
}

        for (uint64_t i = 0; i < length*step; i+=step) {
                cur_addr = addr + i;
                if (is_addr_loaded(dp, cur_addr)) {
                        val = tape[cur_addr%(PAGE_SIZE*4)];
                        if (!val)
                                OPEN_STRING_EX(FADE)
                        else
                                OPEN_STRING

                        if (dp == cur_addr)
                                printf(B_FG);
                        if (val)
                                show_char(val);
                        else {
                                CLOSE_STRING
                                printf(FADE_r);
                        }
                        if (dp == cur_addr)
                                printf(FG_r);
                        elipsis = 0;
                } else {
                        ELIPSIS
                        val = 0;
                }
                start = 0;
        }
        CLOSE_STRING
} 

void show_bytes(CELL tape[], uint64_t addr, uint64_t length, uint64_t step, uint64_t dp) {
        uint8_t comma = 0;
        uint8_t elipsis = 0;
        CELL val;
        printf(FG_r);
        uint64_t addr2;
        for (int32_t i = 0; i < length*step; i+=step) {
#define PRINT_COMMA \
                if (comma) \
                        printf(", " FADE_r); \
                comma = 1;

                addr2 = addr+i;
                if (is_addr_loaded(dp, addr2)) {
                        PRINT_COMMA
                        val = tape[addr2%(PAGE_SIZE*4)];
                        if (!val)
                                printf(FADE);
                        if (dp == addr2)
                                printf(B_FG);
                        printf("0x%x", val);
                        if (dp == addr2)
                                printf(FG_r);
                        elipsis = 0;
                } else {
                        if (!elipsis) {
                                PRINT_COMMA
                                printf(FADE "...");
                        }
                        elipsis = 1;
                }
        }
        printf(FADE_r);
}

void debugger_print_source(uint64_t pc) {
        if (!sourcemap.entries.length) return;

        uint64_t count = sourcemap.entries.length/8;
        uint64_t current;
        uint64_t start = get_current_sm_ind(pc);
        current = start;
        if (start < 1)
                start = 0;
        else
                start -= 1;

        printf(FADE "SOURCE: \n" FADE_r);


        for (uint64_t ind = start; ind < start+6 && ind < count; ind++) {
                if (ind == current)
                        printf("> " B_FG);
                else
                        printf("  ");
                printf("%s\n" FG_r, read_sm_entry(ind)->text);
        }
}

void debugger_print_addrmap_vals(CELL tape[], uint64_t dp) {
        if (!active_addrmap.values) return;

        struct addrmap_value *cur;
        CELL val;

        printf(FADE "VARIABLES:\n" FADE_r);
        for (int32_t ind = 0; ind < active_addrmap.count; ind++) {
                cur = &active_addrmap.values[ind];
                if ((dp-(cur->addr) < (cur->len*cur->step))
                 && !((dp-(cur->addr)) % cur->step))
                        printf("> " B_FG);
                else
                        printf("  ");
                printf("%s: ", cur->name);

#define ELIPSIS_IF_NOT_LOADED \
                if (!is_addr_loaded(dp, cur->addr)) { \
                        printf(FADE "...\n" FADE_r); \
                        continue; \
                }

                val = tape[cur->addr%(PAGE_SIZE*4)];

                switch(cur->mode) {
                        case MODE_HEX:
                                ELIPSIS_IF_NOT_LOADED
                                if (!val)
                                        printf(FADE);
                                printf("0x%x" FADE_r, val);
                                break;
                        case MODE_DEC:
                                ELIPSIS_IF_NOT_LOADED
                                if (!val)
                                        printf(FADE);
                                printf("%d" FADE_r, val);
                                break;
                        case MODE_BYTES:
                                show_bytes(tape, cur->addr, cur->len, cur->step, dp);
                                break;
                        case MODE_STRING:
                                show_string(tape, cur->addr, cur->len, cur->step, dp);
                                break;
                }

                printf(FG_r "\n");
        }
}

uint8_t debugger_print_instruction(uint8_t *inst) {
        uint8_t cmd = CMD_cmd(inst);

        switch (cmd) {
                case '+':
                case '-':
                case '>':
                case '<':
                        printf("%c %3u\n", cmd, CMD_rol_arg(inst));
                        return 2;
                case '/':
                case '\\':
                        printf("%c %3d\n", cmd, CMD_simple_arg(inst));
                        return 2;
                case '^':
                        printf("%c %5d %3d\n", cmd, CMD_copy_offset(inst), CMD_copy_val(inst));
                        return 4;
                case '[':
                case ']':
                        printf("%c %lx\n", cmd, CMD_wide_arg(inst)+8);
                        return 8;
                case '!':
                case '@':
                        printf("%c %lx\n", cmd, CMD_wide_arg(inst));
                        return 8;
                case '0':
                case '.':
                case ',':
                case '#':
                        printf("%c\n", cmd);
                        return 1;
                case 0:
                        printf("end\n");
                        return 0;
                default:
                        printf("??? %lx\n", *(uint64_t*)inst);
                        return 1;
        }
}

void debugger_print_output() {
        if (!output_len) return;

        printf(FADE "OUTPUT:" FADE_r);
        for (uint32_t i = 0; i < output_len; i++) {
                printf(" %2x", ((uint64_t)output_buf[i])&0xff);
        }
        printf("\n       ");

        for (uint32_t i = 0; i < output_len; i++) {
                uint8_t chr = output_buf[i];
                if (chr >= ' ' && chr <= '~')
                        printf("  %c", chr);
                else if (chr == '\n')
                        printf(" \\n");
        }
        printf("\n");
}

void debugger_call(uint8_t reason, CELL tape[], uint8_t program[], uint64_t dp, uint64_t pc) {
        uint8_t sourcemap_entry_found = 0;
        if (reason != BREAK_REASON_BREAKPOINT)
                switch (debugger_state) {
                        case DBG_RUN:
                                if (!program[pc])
                                        debugger_print_output();
                                return;
                        case DBG_STEP:
                                break;
                        case DBG_EXIT_SEARCH:
                                if (pc < exit_target)
                                        return;
                                if (program[pc] == '[') {
                                        exit_search_depth++;
                                }
                                if (program[pc] == ']') {
                                        exit_target = pc+8;
                                        if (!exit_search_depth) {
                                                debugger_state = DBG_EXIT_RUN;
                                        }
                                        exit_search_depth--;
                                }
                                return;
                        case DBG_SM_NEXT:
                                for (uint64_t i = 0; i < sourcemap.entries.length/8; i++) {
                                        struct sourcemap_entry* entry = read_sm_entry(i);
                                        if (entry->ind == pc) {
                                                sourcemap_entry_found = 1;
                                                break;
                                        }
                                }
                                if (!sourcemap_entry_found)
                                        return;
                                break;
                        case DBG_EXIT_RUN:
                                if (pc == exit_target)
                                        break;
                                return;
                }

        printf(FADE "PROGRAM: " FADE_r "0x%lx\n", pc);
        uint64_t offset = 0;
        for (int32_t i = 0; i < 5; i++) {
                if (!i) {
                        printf("> " B_FG);
                } else {
                        printf("  ");
                }
                printf("%04lx:\t", pc+offset);
                offset += debugger_print_instruction(&program[pc+offset]);
                printf(FG_r);
                if (!program[pc+offset]) break;
        }

        printf(FADE "TAPE: " FADE_r "0x%lx\n", dp);
        CELL val;
        for (int32_t offset = -DEBUGGER_TAPE_VIEW; offset < DEBUGGER_TAPE_VIEW+1; offset++) {
                val = tape[(dp+offset)%(PAGE_SIZE*4)];
                if (!offset)
                        printf(B_FG);
                if (!val)
                        printf(FADE);
                printf(CELL_FORMAT_STRING FG_r FADE_r, val);
                printf(" ");
        }

        printf("\n");
        for (int32_t i = 0; i < (TYPOGRAPHIC_CELL_WIDTH+1) * (DEBUGGER_TAPE_VIEW+1) - 2; i++) {
                printf(" ");
        }
        printf("^\n");

        debugger_print_addrmap_vals(tape, dp);
        debugger_print_source(pc);
        debugger_print_output();

        debugger_cmd(pc);
}
