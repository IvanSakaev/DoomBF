static inline uint8_t CMD_cmd(uint8_t *inst) {
        return inst[0];
}

static inline uint8_t CMD_simple_arg(uint8_t *inst) {
        return inst[1];
}

static inline uint16_t CMD_rol_arg(uint8_t *inst) {
        return ((uint16_t)inst[1])+1;
}

static inline int16_t CMD_copy_offset(uint8_t *inst) {
        return ntohs(*(uint16_t*)&inst[1]);
}

static inline int8_t CMD_copy_val(uint8_t *inst) {
        return inst[3];
}

static inline uint64_t CMD_wide_arg(uint8_t *inst) {
        return ntohll((*(uint64_t*)&inst[0]))&0x00ffffffffffffff;
}
