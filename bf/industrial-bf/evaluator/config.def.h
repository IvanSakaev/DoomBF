#define PAGE_SIZE_POWER 24 /* 1. The page size is 2**PAGE_SIZE_POWER \
                              2. This value must be greater than of equal to 8 */
#define CELL uint8_t  // Changing this value may cause bugs

// #define DISABLE_ROLLING /* Disables common statement shorthands, such as ^ or 0 */

// ROLLING_TYPE should be signed
typedef int32_t ROLLING_TYPE;
#define __ROLLING_TYPE_MAX INT32_MAX
#define __ROLLING_TYPE_MIN INT32_MIN

// #define DEBUGGER /* Enables the -d option
//                     Disabled by default because it
//                     slows down execution even if -d
//                     isn't used (mandelbrot 12s->15s) */
// #define   DEBUGGER_DEFAULT_STATE DBG_STEP // or DBG_RUN
// #define   DEBUGGER_TAPE_VIEW     8
// #define   CELL_FORMAT_STRING "%2x"

#define ASSERTS
#define COMPRESSED


// Don't change:
#define ROLLING_SIZE sizeof(ROLLING_TYPE)
#define ROLLING_TYPE_MAX ((1 << PAGE_SIZE_POWER) < __ROLLING_TYPE_MAX ? (1 << PAGE_SIZE_POWER) : __ROLLING_TYPE_MAX)
#define ROLLING_TYPE_MIN (-ROLLING_TYPE_MAX)
