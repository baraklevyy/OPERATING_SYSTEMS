#ifndef MESSAGE_SLOT_H
#define MESSAGE_SLOT_H
#include <linux/ioctl.h>
#define MAJOR_NUMBER 240
// Set the message of the device driver
#define MSG_SLOT_CHANNEL _IOW(MAJOR_NUMBER, 0, unsigned long)
#define BUFFER_MAX_LENGTH 128
#define DEVICE_RANGE_NAME "message_slot"
#define DEVICE_FILE_NAME "simp_slot"
#define SUCCESS 0
#define FAILURE -1
#define TRUE 1
#define FALSE 0

#endif