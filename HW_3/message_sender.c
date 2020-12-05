#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "message_slot.h"
#define ARGUMENT_NUMBER 4

int string_to_int(char* str);
int main(int argc, char **argv) {
    if (ARGUMENT_NUMBER != argc) {
        printf("INVALID NUMBER OF ARGUMENTS\n");
        exit(1);
    }
    int msg_len = strlen(argv[3]); // excluding the terminating null byte ('\0') in C
    long file_descriptor = open(argv[1], O_RDWR);
    if (SUCCESS > file_descriptor) {
        printf("OPEN ERROR RAISE: %s\n", strerror(errno));
        exit(1);
    }
    //The argument 'file_descriptor' must be an open file descriptor
    int ioctl_ret_val = ioctl(file_descriptor, MSG_SLOT_CHANNEL, string_to_int(argv[2]));
    if (SUCCESS != ioctl_ret_val) {
        printf("IOCTL ERROR RAISE: %s\n", strerror(errno));
        exit(1);
    }
    int write_ret_val = write(file_descriptor, argv[3], msg_len);
    if (1 > write_ret_val) { //does not succees to write even a single character
        printf("WRITE ERROR RAISE: %s\n", strerror(errno));
        exit(1);
    }
    close(file_descriptor);
    return 0;
}

