// SAFETY: capacity is always greater than or equal to length
struct vector {
        uint8_t *ptr;
        uint64_t capacity;
        uint64_t length;
};

void vector_init(struct vector *vec, uint64_t capacity) {
        if (capacity) {
                vec->ptr = safe_malloc(capacity);
        } else {
                vec->ptr = 0;
        }
        vec->capacity = capacity;
        vec->length = 0;
}

struct vector vector_create(uint64_t capacity) {
        struct vector vec;
        vector_init(&vec, 0);
        return vec;
}

void vector_extend(struct vector *vec, uint64_t new_capacity) {
        if (new_capacity < vec->capacity) return;

        vec->capacity = round_up_to_power_of_two(new_capacity);
        vec->ptr = safe_realloc(vec->ptr, vec->capacity);
}

void vector_push(struct vector *vec, uint8_t val) {
        vec->length++;
        vector_extend(vec, vec->length);
        *(vec->ptr+(vec->length-1)) = val;
}

void vector_pop(struct vector *vec, uint64_t ind) {
        vec->length--;
        for (uint64_t i = ind; i < vec->length; i++) {
                *(vec->ptr+i) = *(vec->ptr+i+1);
        }
}

void vector_drop(struct vector *vec) {
        free(vec->ptr);
}

void *vector_unwrap(struct vector *vec) {
        uint8_t *p = safe_realloc(vec->ptr, vec->length);
        return p;
}

#define vector_push_ex(vec, type, val) _vector_push_multibyte(vec, val, sizeof (type))

void _vector_push_multibyte(struct vector *out, uint64_t item, uint8_t len) {
        for (int32_t i = 0; i < len; i++) {
                uint8_t c = item>>(8*(len-1-i));
                vector_push(out, c);
        }
}

#define vector_read_ex(vec, type, ind) (type)_ntoh_custom(*(type*)&(((vec)->ptr)[(ind)*(sizeof(type))]), sizeof(type))
#define vector_write_ex(vec, type, ind, val) *(type*)&(((vec)->ptr)[(ind)*(sizeof(type))]) = (type)_hton_custom((val), sizeof(type))
