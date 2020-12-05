#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/slab.h>
#include "message_slot.h"

MODULE_LICENSE("GPL");



/*************************************
 *           DATA STRUCTURES         *
 *************************************/

typedef struct _channel_list{
    int channel_id;
    char *message_buffer;
    int message_length;
    struct _channel_list* next;
}channel;

typedef struct _slot_list{
    int minor_number;
    channel *message_channel_list;
    struct _slot_list* next;
}slot;

static slot* global_slot_list = NULL;



/*************************************
 *           HELPER FUNCTIONS        *
 ************************************* */

/*return the slot with corresponding minor number or NULL if not exist*/
static slot* retrieve_slot_from_list(int minor_number){
    slot *head = global_slot_list;
    while(NULL != head){
        if(minor_number == head->minor_number) return head;
        head = head->next;
    }
    return NULL;
}
/*******************************************************/
/*return the channel with corresponding channel_id number or NULL if not exist*/
static channel* retrieve_channel(channel* ch_ls, int ch_id){
    channel *head = ch_ls;
    while(NULL != head) {
        if (ch_id == head->channel_id) return head;
        head = head->next;
    }
    return NULL;
}
/*******************************************************/
/*adding a new slot to the slot_list*/
static int add_new_slot(int minor_number){
    /*GFP_KERNEL - Allocate normal kernel ram. According to man_page.*/
    slot *new_slot = (slot*)kmalloc(sizeof(slot), GFP_KERNEL);
    if(NULL == new_slot) return -ENOMEM; //allocation failed
    new_slot->next = global_slot_list;
    global_slot_list = new_slot;
    new_slot->message_channel_list = NULL;
    new_slot->minor_number = minor_number;
    return SUCCESS;
}
/*******************************************************/
/*adding a new message_channel to a specific slot*/
static int add_channel_to_slot(slot* current_slot, int channel_id){
    /* kcalloc — allocate memory for an array. The memory is set to zero.*/
    channel *new_channel = (channel*)kmalloc(sizeof(channel), GFP_KERNEL);
    if(NULL == new_channel) return -ENOMEM;
    char *buffer = (char*)kcalloc(BUFFER_MAX_LENGTH, sizeof(char), GFP_KERNEL);
    if(NULL == buffer){
        kfree(new_channel);
        return -ENOMEM;
    }
    new_channel->message_buffer = buffer;
    new_channel->message_length = SUCCESS;
    new_channel->next = current_slot->message_channel_list;
    current_slot->message_channel_list = new_channel;
    new_channel->channel_id = channel_id;
    return SUCCESS;
}
/*******************************************************/
/*this function destroy all data structures that had been allocated dynamically */
static void destroy(){
    channel *current_channel, *current_channel_head;
    slot *current_slot;
    while(NULL != global_slot_list){
        current_slot = global_slot_list;
        current_channel_head = current_slot->message_channel_list;
        global_slot_list = global_slot_list->next; /*storing the incremented pointer in order not to lost the rest of the list*/
        while(NULL != current_channel_head){
            current_channel = current_channel_head;
            current_channel_head = current_channel_head->next;
            kfree(current_channel->message_buffer);
            kfree(current_channel);
            //kfree(current_slot);
        }
        kfree(current_slot);
    }
}
/*************************************
 *           DEVICE FUNCTIONS        *
 ************************************* */
static int device_open(struct inode* inode, struct file *file) {
    if (NULL == file || NULL == inode) return -EINVAL;
    file->private_data = NULL; //nullifying private member
    int minor_number = iminor(inode);
    if (NULL == retrieve_slot_from_list(minor_number)) {
        return add_new_slot(minor_number);
    }
    //already open
    return SUCCESS;
}
/*******************************************************/
static int device_release( struct inode* inode,
                           struct file*  file){
    return SUCCESS;
}
/*******************************************************/
static ssize_t device_read(struct file* file, char __user* buffer, size_t length, loff_t* offset){
    if (NULL == file || NULL == buffer) return -EINVAL;
    int minor_number = iminor(file_inode(file));
    slot *slot = retrieve_slot_from_list(minor_number);
    channel *channel = retrieve_channel(slot->message_channel_list, (uintptr_t)file->private_data);
    if (NULL == channel) return -EINVAL;
    int channel_length = channel->message_length;
    if (0 == channel_length) return -EWOULDBLOCK;
    if (length < channel_length) return -ENOSPC;
    for (int index = 0; index < channel_length; index++){
        //in order not to access illegal user space memory location
        if (SUCCESS != put_user(*(channel->message_buffer + index), &(*(buffer + index)))) return -EINVAL;
    }
    return channel_length;
}
/*******************************************************/
static ssize_t device_write(struct file*  file, const char __user* buffer, size_t  length, loff_t* offset){
    if (NULL == file || NULL == buffer) return -EINVAL;
    if (0 == length || BUFFER_MAX_LENGTH < length) return -EMSGSIZE;
    struct inode *current_inode = file_inode(file);
    int minor_number = iminor(current_inode);
    slot *slot = retrieve_slot_from_list(minor_number);
    if(NULL == slot) return -EINVAL;
    channel *channel = retrieve_channel(slot->message_channel_list, (uintptr_t)file->private_data);
    if (NULL == channel) return -EINVAL;
    channel->message_length = 0;
    for (int index = 0; index < length; index++){
        //in order not to access illegal user space memory location
        if (SUCCESS != get_user(*(channel->message_buffer + index), &(*(buffer + index)))) return -EINVAL;
        channel->message_length++;
    }
    return channel->message_length;
}
/*******************************************************/
static long device_ioctl(struct  file* file, unsigned int ioctl_command_id, unsigned long  ioctl_param){
    if ((0 == ioctl_param) || (MSG_SLOT_CHANNEL != ioctl_command_id) || NULL == file) return -EINVAL;
    struct inode *current_inode = file_inode(file);
    int minor_number = iminor(current_inode);
    slot *slot = retrieve_slot_from_list(minor_number);
    if(NULL == slot) return -EINVAL;
    if (NULL == (retrieve_channel(slot->message_channel_list, ioctl_param))) {
        if (SUCCESS != add_channel_to_slot(slot, ioctl_param)) return -ENOMEM;
    }
    file->private_data = (void*) ioctl_param; // storing the
    return SUCCESS;
}
/*************************************
 *           DEVICE SETUP            *
 ************************************* */
/*******************************************************/
/* This structure will hold the functions to be called
 * when a process does something to the device we created*/
struct file_operations Fops =
        {
                .owner	        = THIS_MODULE,
                .read           = device_read,
                .write          = device_write,
                .open           = device_open,
                .unlocked_ioctl = device_ioctl,
                .release        = device_release,
        };
/*******************************************************/
/* Initialization */
static int __init initialize() {
    int major_number = FAILURE;
    major_number = register_chrdev(MAJOR_NUMBER, DEVICE_RANGE_NAME, &Fops);
    if (major_number < SUCCESS){ //most kind of errors returning negative values
        printk(KERN_ERR "%s FAIL WHILE TO REGISTER %d\n", DEVICE_FILE_NAME, MAJOR_NUMBER);
        return major_number;
    }
    printk(KERN_INFO "MAJOR_NUMBER OF SLOT IS: %d 0\n", MAJOR_NUMBER);
    return SUCCESS;
}
/*******************************************************/
static void __exit cleanup(){
    destroy();
    unregister_chrdev(MAJOR_NUMBER, DEVICE_RANGE_NAME);
}
/*******************************************************/
module_init(initialize);
module_exit(cleanup);
/*************************END***************************/

