struct sourcemap {
        uint8_t comment_line;
        uint64_t er_ind;
        uint64_t line_ind;
        struct vector building_text;
        struct vector entries;
};

struct sourcemap_entry {
        uint64_t ind;
        string text;
};

struct sourcemap sourcemap;

void sourcemap_init() {
        vector_init(&sourcemap.building_text, 0);
        vector_init(&sourcemap.entries, 0);
        sourcemap.line_ind = -1;
        sourcemap.er_ind = -1;
        sourcemap.comment_line = 0;
}

void sourcemap_end(uint64_t ind) {
        struct vector *bt;
        bt = &sourcemap.building_text;

        if (!bt->length) return;
        vector_push(bt, '\0');

        struct sourcemap_entry *e;
        e = safe_malloc(sizeof (struct sourcemap_entry));

        e->ind = ind;
        e->text = vector_unwrap(bt);

        vector_push_ex(&sourcemap.entries, uint64_t, (uint64_t)e);

        sourcemap.line_ind = -1;
        sourcemap.er_ind = -1;
        sourcemap.comment_line = 0;
        vector_init(bt, 0);
}

void sourcemap_process(character chr, uint64_t ind) {
        struct vector *bt;
        bt = &sourcemap.building_text;

        if (is_whitespace(chr)) {
                if (bt->length
                 && !is_whitespace(
                        bt->ptr[bt->length-1]
                ))
                        vector_push(bt, ' ');
        } else if (is_comment(chr)) {
                if (sourcemap.er_ind != -1) {
                        sourcemap_end(sourcemap.er_ind);
                        sourcemap.er_ind = -1;
                }
                sourcemap.comment_line = 1;
                vector_push(bt, chr);
        } else {
                if (sourcemap.line_ind == -1)
                        sourcemap.line_ind = ind;
                if (sourcemap.comment_line) {
                        if (sourcemap.er_ind == -1)
                                sourcemap.er_ind = ind;
                }
        }

        if (chr == '\n' && sourcemap.line_ind != -1)
                sourcemap_end(sourcemap.line_ind);

        if (chr == '\n') {
                sourcemap.line_ind = -1;
                sourcemap.comment_line = 0;
        }
}
