#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifndef S_IRUSR
#define S_IRUSR _S_IREAD
#endif
#ifndef S_IWUSR
#define S_IWUSR _S_IWRITE
#endif
#else
#include <unistd.h>
#endif
#include <fcntl.h>

void dump_tape() {
        int fd = open("tape.bin", O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
        if (fd == -1) {
                perror("failed to open tape file");
                return;
        }
        uint64_t written = 0;
        int64_t block;
        char *buf = tape;
        while (written < HOT_TAPE) {
                block = write(fd, buf, HOT_TAPE - written);
                if (block <= 0) {
                        close(fd);
                        perror("failed to write tape");
                        return;
                }
                written += block;
                buf += block;
        }
        close(fd);
        printf("done\n");
}
