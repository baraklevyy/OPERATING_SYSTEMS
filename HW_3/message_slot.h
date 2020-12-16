#ifndef MESSAGE_SLOT_H
#define MESSAGE_SLOT_H

#include <linux/ioctl.h>
#define MAJOR_NUMBER 240
// Set the message of the device driver
#define MSG_SLOT_CHANNEL _IOW(MAJOR_NUMBER, 0, unsigned long)
#define DEVICE_FILE_NAME "my_slot"
#define BUFFER_MAX_LENGTH 128
#define DEVICE_RANGE_NAME "message_slot"
#define SUCCESS 0
#define FAILURE -1
/*
int string_to_int(char* str){
    int res = 0;
    int i;
    for (i = 0; str[i] != '\0'; ++i)
        res = res * 10 + str[i] - '0';

    return res;
}
*/
#endif
