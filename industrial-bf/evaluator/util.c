#include "config.h"

uint64_t round_up_to_power_of_two(uint64_t n) {
        uint64_t power = 1;
        while (power < n) power *= 2;
        return power;
}

void *safe_malloc(uint64_t size) {
        void *p = calloc(1, size);
        if (!p) {
                printf("memory allocation failed\n");
                exit(1);
        }
        return p;
}

void *safe_realloc(void *ptr, uint64_t size) {
        void *p = realloc(ptr, size);
        if (!p) {
                printf("memory allocation failed\n");
                exit(1);
        }
        return p;
}

void write_long(uint8_t *ptr, uint64_t item) {
        for (int32_t i = 0; i < 8; i++) {
                *(ptr+i) = item>>(8*(7-i));
        }
}

string read_file(string filename, uint64_t *program_length) {
        FILE *f = fopen(filename, "rb");
        if (!f) {
                perror(filename);
                return NULL;
        }
        fseek(f, 0, SEEK_END);
        uint64_t fsize = ftell(f);
        fseek(f, 0, SEEK_SET);

        string string = safe_malloc(fsize + 1);
        fread(string, fsize, 1, f);
        fclose(f);

        string[fsize] = 0;
        *program_length = fsize;
        return string;
}

int8_t parse_digit(uint8_t digit) {
        int8_t out;
        if (digit >= '0' && digit <= '9') {
                out = digit-'0';
        } else if (digit >= 'a' && digit <= 'f'){
                out = digit-'a'+0xa;
        } else {
                out = -1;
        }
        return out;
}

uint64_t parse_number(string arr, uint64_t *ind) {
        uint64_t val = 0;
        int8_t digit;
        while (1) {
                digit = parse_digit(arr[*ind]);
                if (digit == -1)
                        break;
                val <<= 4;
                val += digit;
                (*ind)++;
        }
        return val;
}

#define htonll(x) ((1==htonl(1)) ? (x) : ((uint64_t)htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32))
#define ntohll(x) ((1==ntohl(1)) ? (x) : ((uint64_t)ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32))

uint64_t _ntoh_custom(uint64_t val, uint8_t len) {
        switch (len) {
                case 1:
                        return val;
                case 2:
                        return ntohs(val);
                case 4:
                        return ntohl(val);
                case 8:
                        return ntohll(val);
        }
        printf("INTERNAL ERROR: _ntoh_custom(%lx, %d)\n", val, len);
        abort();
}

uint64_t _hton_custom(uint64_t val, uint8_t len) {
        switch (len) {
                case 1:
                        return val;
                case 2:
                        return htons(val);
                case 4:
                        return htonl(val);
                case 8:
                        return htonll(val);
        }
        printf("INTERNAL ERROR: _hton_custom(%lx, %d)\n", val, len);
        abort();
}

static inline uint8_t is_comment(character inst) {
        return !(
                inst == '+'
             || inst == '-'
             || inst == '<'
             || inst == '>'
             || inst == '['
             || inst == ']'
             || inst == '.'
             || inst == ','
#ifdef DEBUGGER
             || (inst == '#' && option_d)
             || (inst == '*' && option_d)
#endif
#ifdef ASSERTS
             || (inst == '@' && option_a)
             || (inst == '!' && option_a)
#endif
        );
}

static inline uint8_t is_whitespace(character inst) {
        return (
                inst == ' '
             || inst == '\t'
             || inst == '\n'
             || inst == '\r'
        );
}

static inline uint8_t is_power_of_2(uint8_t v) {
        return (v == 0   ||
                v == 2   ||
                v == 4   ||
                v == 8   ||
                v == 16  ||
                v == 32  ||
                v == 64  ||
                v == 128);
}
