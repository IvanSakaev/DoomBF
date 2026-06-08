#include <unistd.h>
#include <fcntl.h>

void dump_tape() {
        int fd = open("tape.bin", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        if (fd == -1) {
                perror("failed to open tape file");
                return;
        }
        uint64_t written;
        uint64_t block;
        char *buf = tape;
        while (written < HOT_TAPE) {
                block = write(fd, buf, HOT_TAPE - written);
                if (!block) {
                        perror("failed to write tape");
                        return;
                }
                written += block;
                buf += block;
        }
        printf("done\n");
}
