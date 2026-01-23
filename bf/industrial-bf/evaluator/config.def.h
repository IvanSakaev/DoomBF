#define PAGE_SIZE_POWER 24 /* 1. The page size is 2**PAGE_SIZE_POWER
                              2. This value must be greater than of equal to 8 */
#define CELL uint8_t

// #define DISABLE_ROLLING /* Disables common statement shorthands, such as ^ or 0 */

// #define DEBUGGER /* Enables the -d option
//                     Disabled by default because it
//                     slows down execution even if -d
//                     isn't used (mandelbrot 12s->15s) */
// #define   DEBUGGER_DEFAULT_STATE DBG_STEP // or DBG_RUN
// #define   DEBUGGER_TAPE_VIEW     3
#define ASSERTS
#define CELL_FORMAT_STRING "%2x"
