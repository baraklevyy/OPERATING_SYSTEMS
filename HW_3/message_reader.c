#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "message_slot.h"
#define ARGUMENT_NUMBER 3

int main(int argc, char **argv) {
    if (ARGUMENT_NUMBER != argc) {
        printf("INVALID NUMBER OF ARGUMENTS\n");
        exit(1);
    }
    char buffer[BUFFER_MAX_LENGTH + 1]; //+1 for the terminating symbol
    long file_descriptor = open(argv[1], O_RDWR);
    if (SUCCESS > file_descriptor) {
        printf("OPEN ERROR RAISE: %s\n", strerror(errno));
        exit(1);
    }
    long ioctl_ret_val = ioctl(file_descriptor, MSG_SLOT_CHANNEL, string_to_int(argv[2]));
    if (ioctl_ret_val != 0) {
        printf("IOCTL ERROR RAISE: %s\n", strerror(errno));
        exit(1);
    }
    int read_ret_val = read(file_descriptor, buffer, BUFFER_MAX_LENGTH);
    if (SUCCESS > read_ret_val) {
        printf("READ ERROR RAISE: %s\n", strerror(errno));
        exit(1);
    }
    buffer[read_ret_val] = '\0';
    close(file_descriptor);
    int write_ret_val = write(1, buffer, read_ret_val);
    if (1 > write_ret_val) {
        printf("WRITE ERROR RAISE: %s\n", strerror(errno));
        exit(1);
    }
    return 0;
}