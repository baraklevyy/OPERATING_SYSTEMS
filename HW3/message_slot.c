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
 ************************************* */

typedef struct _channel_list{
    int channel_id;
    char *message_buffer;
    int message_length;
    struct _channel_list* next;
}channel;

typedef struct _slot_list{
    int minor_number;
    channel *message_channel_list;
    struct _slots_list* next;
}slot;

static slot* global_slot_list = NULL;



/*************************************
 *           HELPER FUNCTIONS        *
 ************************************* */
 /*return the slot with corresponding minor number or NULL if not exist*/
 static slot* retrive_slot_from_list(int minor_number){
     slot *head = global_slot_list;
     while(NULL != head){
         if(minor_number == head->minor_number){
             return head;
         }
         head = head->next;
     }
    return NULL;
 }
/*return the channel with corresponding channel_id number or NULL if not exist*/
 static channel* retreive_channel(channel* ch_ls, int ch_id){
     channel *head = ch_ls;
     while(NULL != head) {
         if (ch_id == head->channel_id) return head;
         head = head->next;
     }
     return NULL;
 }
/*adding a new slot to the slots_list*/
 static int add_new_slot(int minor_number){
     /*GFP_KERNEL - Allocate normal kernel ram.According to man_page.*/
     slot *new_slot = (slot*)kmalloc(sizeof(slot), GFP_KERNEL);
     if(NULL == new_slot){
         return -ENOMEM;
     }
     new_slot->next = global_slot_list;
     global_slot_list = new_slot;
     new_slot->message_channel_list = NULL;
     new_slot->minor_number = minor_number;
    return SUCCESS;
 }
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

    file->private_data = NULL;
    int minor_number = iminor(inode);
    if (	find_msg_slot(slot_list, minor) == NULL) {
        return add_msg_slot(minor);
    }
    return SUCCESS;
}

static int device_release( struct inode* inode,
                           struct file*  file){
    return SUCCESS;
}
//---------------------------------------------------------------
// a process which has already opened
// the device file attempts to read from it
static ssize_t device_read( struct file* file,
                            char __user* buffer,
size_t       length,
        loff_t*      offset )
{
// read doesnt really do anything (for now)
printk( "Invocing device_read(%p,%ld) - "
"operation not supported yet\n"
"(last written - %s)\n",
file, length, the_message );
//invalid argument error
return -EINVAL;
}

//---------------------------------------------------------------
// a processs which has already opened
// the device file attempts to write to it
static ssize_t device_write( struct file*       file,
                             const char __user* buffer,
size_t             length,
        loff_t*            offset)
{
int i;
printk("Invoking device_write(%p,%ld)\n", file, length);
for( i = 0; i < length && i < BUF_LEN; ++i )
{
get_user(the_message[i], &buffer[i]);
if( 1 == encryption_flag )
the_message[i] += 1;
}

// return the number of input characters used
return i;
}

//----------------------------------------------------------------
static long device_ioctl( struct   file* file,
                          unsigned int   ioctl_command_id,
                          unsigned long  ioctl_param )
{
    // Switch according to the ioctl called
    if( IOCTL_SET_ENC == ioctl_command_id )
    {
        // Get the parameter given to ioctl by the process
        printk( "Invoking ioctl: setting encryption "
                "flag to %ld\n", ioctl_param );
        encryption_flag = ioctl_param;
    }

    return SUCCESS;
}























/*************************************
 *           DEVICE SETUP            *
 ************************************* */


// This structure will hold the functions to be called
// when a process does something to the device we created
struct file_operations Fops =
        {
                .owner	        = THIS_MODULE,
                .read           = device_read,
                .write          = device_write,
                .open           = device_open,
                .unlocked_ioctl = device_ioctl,
                .release        = device_release,
        };

