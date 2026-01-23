#include "config.h"

#define PAGE_SIZE (1<<PAGE_SIZE_POWER)
#define HOT_TAPE (PAGE_SIZE * 4)
#define PAGE_COUNT ((~((uint64_t)0)>>PAGE_SIZE_POWER)+1)

void load_page(CELL *tape, uint64_t page_uid);
void store_page(CELL *tape, uint64_t page_uid);

uint64_t get_page_uid(uint64_t dp) {
        return dp / PAGE_SIZE;
}

inline uint8_t get_page_n(uint64_t dp) {
        return dp / PAGE_SIZE % 4;
}

#define CHECK_PAGE_TRANSITION(tape, expected_direction, dp, last_page) \
{ \
        uint8_t cur_page = get_page_n(dp); \
        if (last_page != cur_page) { \
                last_page = cur_page; \
                perform_page_switch(tape, expected_direction, dp); \
        } \
}

void perform_page_switch(CELL tape[], int8_t expected_direction, uint64_t dp) {
        // 0      1      2      3  
        // store keep current load
        // current page ^
        // expected direction -->
        
        uint64_t current_page_uid = get_page_uid(dp);
        store_page(tape, (current_page_uid - (expected_direction * 2)) % PAGE_COUNT);
        load_page(tape, (current_page_uid + expected_direction) % PAGE_COUNT);
}

void store_page(CELL tape[], uint64_t page_uid) {
        uint8_t page_n = page_uid % 4;
        CELL *target = &tape[PAGE_SIZE*page_n];

        character filename_buf[17];
        string filename = filename_buf + (PAGE_SIZE_POWER / 4);
        sprintf(filename_buf, "%016lx", page_uid);
#ifdef DEBUG
        printf("storing page: 0x%s... ", filename);
#endif

        FILE *f = fopen(filename, "wb");
        if (!f) {
                printf("page store failed! (open)\n");
                exit(1);
        }

        if (fwrite(target, 1, PAGE_SIZE, f) < PAGE_SIZE) {
                printf("page store failed! (write)\n");
                exit(1);
        }
#ifdef DEBUG
        printf("ok\n");
#endif
        fclose(f);
}

void load_page(CELL tape[], uint64_t page_uid) {
        uint8_t page_n = page_uid % 4;
        CELL *target = &tape[PAGE_SIZE*page_n];

        character filename_buf[17];
        string filename = filename_buf + (PAGE_SIZE_POWER / 4);
        sprintf(filename_buf, "%016lx", page_uid);
#ifdef DEBUG
        printf("loading page: 0x%s... ", filename);
#endif

        FILE *f = fopen(filename, "rb");
        if (!f) {
                memset(target, 0, PAGE_SIZE);
#ifdef DEBUG
                printf("empty\n");
#endif
                return;
        }

        if (fread(target, 1, PAGE_SIZE, f) < PAGE_SIZE) {
                printf("page load failed!\n");
                exit(1);
        }
#ifdef DEBUG
        printf("ok\n");
#endif
        fclose(f);
}

uint8_t is_addr_loaded(uint64_t dp, uint64_t addr) {
        uint64_t cur_page = get_page_uid(dp);
        uint64_t target_page = get_page_uid(addr);

        return (
                target_page == ((cur_page - 1) % PAGE_COUNT)
             || target_page == cur_page
             || target_page == ((cur_page + 1) % PAGE_COUNT)
        );
}
