#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>           /* Definition of AT_* constants */
#include <errno.h>
#include <stdatomic.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
/**************************
##        defines        ##
**************************/
#define SUCCESS 1
#define FAILURE -1
#define TRUE 1
#define FALSE 0
#define ARGUMENTS_NUM 4

//Those defines act like a functions - letting us to execute more than one line in a define
#define handle_thread_error(msg) \
        do { perror(msg); pthread_exit((void*)EXIT_FAILURE); } while (0)

#define handle_error(msg) \
        do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define print_permission_error(path) \
        do { printf("Directory %s: Permission denied.\n", path); } while (0)

#define handle_regular_perror(msg) \
        do { perror(msg); } while (0)

/**************************
##    data-structures    ##
**************************/
typedef struct _dir_node{
    char *name;
    struct _dir_node *next;
    struct _dir_node *previous;
}dir_node;

typedef struct _dir_queue{
    dir_node *head;
    dir_node *tail;
    int size;
}dir_queue;

typedef struct _thread_node{
    pthread_t thread;
    struct _thread_node *next;
}t_node;

typedef struct _threads_list{
    t_node *head;
    int size;
}t_list;

/**************************
##    global variable    ##
**************************/
static int starting_thread_counter;
static int waiting_threads_counter;
int EXIT_FLAG;									// indicating exiting condition initialize to zero because of the static keyword
static char *SEARCH_TERM;
static int THREAD_NUM;
static dir_queue queue;                        // global directory queue
static pthread_mutex_t mutex_thread_list;		// protecting critical segment while doing operations on thread_list
static pthread_mutex_t mutex_queue_operations;	// protecting critical segment while doing queue operations
static pthread_cond_t cv_start_searching;          // condition variable to start the threads
static pthread_cond_t cv_wait_for_work;             // condition variable to indicate waiting for work threads
static t_list th_list;							// global threads list
static atomic_int number_of_threads_err;        // threads that encountered error
static atomic_int number_of_found;              // number of terms found
/**************************
# function declarations   #
**************************/
int is_terminating();
void prepare_threads(void);
dir_node* create_dir_node(char *path);
dir_node* initial_state(char *path);
void enqueue(dir_node *new_node);
dir_node* dequeue();
int is_directory(char* path);
int is_searchable_directory(char *path);
int is_dot_or_double_dot(char *filename);
void* dir_worm(void *arg);
void initialize_lockers();
void destroy_lock_cond();
bool has_error_during_joining();
//void destroy_lists();

void prepare_threads(void){
    int rc;
    //th_list.size = THREAD_NUM;
    for(long thread_number = 0; thread_number < THREAD_NUM; thread_number++){
        t_node *current_t_node = (t_node*)calloc(1, sizeof(t_node));
        if(NULL == current_t_node) handle_error("calloc failed"); // this is error in the main thread - exiting
        if(FALSE == thread_number){ //first node to insert to list.
            th_list.head = current_t_node;
            th_list.size++;
        }
        else{ //not the first node to insert
            //inserting FIFO
            current_t_node->next = th_list.head;
            th_list.head = current_t_node;
            th_list.size++;
        }
        rc = pthread_create(&(current_t_node->thread), NULL, (void*)dir_worm, (void *)current_t_node);
        if(FALSE != rc) handle_error("pthread_create failed"); // error in main thread - exiting
    }
    //adding this thread to the 'start_searching cv . using while statement due to the fact that I want to wait until all threads will be ready to work in the dir_worm function and than execute them all simultaneously
    while(THREAD_NUM != starting_thread_counter) pthread_cond_broadcast(&cv_start_searching);
}

dir_node* create_dir_node(char *path){
    dir_node *new_dir_node = (dir_node*)calloc(1, sizeof(dir_node));
    if(NULL == new_dir_node) //calloc failed
        return NULL;
    new_dir_node->next = NULL;
    new_dir_node->previous = NULL;
    new_dir_node->name = path;
    return new_dir_node;
}

dir_node* initial_state(char *path){
    dir_node *new_dir_node = create_dir_node(path);
    queue.head = new_dir_node;
    queue.tail = new_dir_node;
    queue.size = 1;
    return new_dir_node;
}


void enqueue(dir_node *new_node){
    pthread_mutex_lock(&mutex_queue_operations);
    if(0 != queue.size){ // this is not the fist element to insert
        new_node->previous = queue.tail;
        queue.tail->next = new_node;
        queue.tail = new_node;
        queue.size++;
    }
    else{ // maybe this is the first element
        queue.head = new_node;
        queue.tail = new_node;
        queue.size = 1;
    }
    pthread_cond_signal(&cv_wait_for_work); // this cv signalling that new thread enqueue and someone can treat this directory
    pthread_mutex_unlock(&mutex_queue_operations);
}
/*this example taken from the web*/
/*The typical usage pattern of condition variables is
// safely examine the condition, prevent other threads from
// altering it
pthread_mutex_lock (&lock);
while ( SOME-CONDITION is false)
    pthread_cond_wait (&cond, &lock);

// Do whatever you need to do when condition becomes true
do_stuff();
pthread_mutex_unlock (&lock);


On the other hand, a thread, signaling the condition variable, typically looks like

// ensure we have exclusive access to whatever comprises the condition
pthread_mutex_lock (&lock);
ALTER-CONDITION
// Wakeup at least one of the threads that are waiting on the condition (if any)
pthread_cond_signal (&cond);

// allow others to proceed
 pthread_mutex_unlock (&lock)*/
/*
int broadcast_deadlock(){
    if(queue.size == 0 && waiting_threads_counter == THREAD_NUM - number_of_threads_err - 1) return TRUE;
    return FALSE;
}
*/
dir_node* dequeue(){
    pthread_mutex_lock(&mutex_queue_operations);
    //printf("dequeue\n");
    while(0 == queue.size){
        //checking if some other thread raise the EXIT_FLAG to check for termination signal
        //as well as conditioning on the queue size using cv in order to wait with threads while the queue is empty
        is_terminating();
        if(TRUE == EXIT_FLAG){
            pthread_mutex_unlock(&mutex_queue_operations);
            return NULL; //exiting this thread from being dequeue
        }
/*
        if(broadcast_deadlock() == TRUE){
            waiting_threads_counter++;
            break;
        }
*/
        waiting_threads_counter++; // protected under mutex_queue_operations from race conditions
        pthread_cond_wait(&cv_wait_for_work, &mutex_queue_operations); /* the wait function unlock the mutex. when signal is called on some thread it continue with the corresponding thread on the next line, furthermore when the thread that blocked by this cv get signalled he aquire the mutex from the signalled thread and lock the mutex again.*/
        waiting_threads_counter--; //here the thread continue and therefore decrement the waiting signal number by one
        //in this point things could be change since the last check for the exit status
        //is_terminating();
        if(TRUE == EXIT_FLAG){
            pthread_mutex_unlock(&mutex_queue_operations); //releasing lock before moving forward
            return NULL;
        }
    }
    dir_node *popped = queue.head;
    if(TRUE < queue.size){ // this is not the last element within the queue
        queue.size--;
        queue.head = queue.head->next; //pointing head to the next node
        queue.head->previous = NULL;   //this is the new head so no prev pointer
        popped->next = NULL;           //deleting irrelevant pointers
    }
    else{ //this is the first argument
        queue.size = 0;
        queue.head = NULL;
        queue.tail = NULL;
    }
    pthread_mutex_unlock(&mutex_queue_operations);
    return popped;

}
/*The program exits in one of the following cases: (1) there are no more directories in the queue and
all searching threads are idle (not searching for content within a directory), or (2) all searching
threads have died due to an error.*/
int is_terminating(){
    //not to include the main thread also in the waiting
    if((0 == queue.size) && (th_list.size == waiting_threads_counter + 1)){
        EXIT_FLAG = TRUE; // the program should exit if these conditions are true -  setting EXIT_FLAG to TRUE
        return TRUE;
    }
    if(THREAD_NUM == number_of_threads_err){
        EXIT_FLAG = TRUE;
        return TRUE;
    }
    return FALSE;
}
/********************************************************************
struct dirent {
    ino_t d_ino;  inode number
    off_t d_off;  offset to the next dirent
    unsigned short d_reclen; length of this record
    unsigned char d_type; type of file
    char d_name[256];  filename
};
***********************************************************************/
/**********************************************************************
 * struct stat {
               dev_t     st_dev;         ID of device containing file
               ino_t     st_ino;         inode number
               mode_t    st_mode;        file type and mode
               nlink_t   st_nlink;       number of hard links
               uid_t     st_uid;         user ID of owner
               gid_t     st_gid;         group ID of owner
               dev_t     st_rdev;        device ID (if special file)
               off_t     st_size;        total size, in bytes
               blksize_t st_blksize;     blocksize for filesystem I/O
               blkcnt_t  st_blocks;      number of 512B blocks allocated
           };
 ***********************************************************************/
/**********************************************************************
 The following flags are defined for the st_mode field:

   S_IFMT     0170000   bitmask for the file type bitfields
   S_IFSOCK   0140000   socket
   S_IFLNK    0120000   symbolic link
   S_IFREG    0100000   regular file
   S_IFBLK    0060000   block device
   S_IFDIR    0040000   directory
   S_IFCHR    0020000   character device
   S_IFIFO    0010000   FIFO
   S_ISUID    0004000   set UID bit
   S_ISGID    0002000   set-group-ID bit (see below)
   S_ISVTX    0001000   sticky bit (see below)
   S_IRWXU    00700     mask for file owner permissions
   S_IRUSR    00400     owner has read permission
   S_IWUSR    00200     owner has write permission
   S_IXUSR    00100     owner has execute permission
   S_IRWXG    00070     mask for group permissions
   S_IRGRP    00040     group has read permission
   S_IWGRP    00020     group has write permission
   S_IXGRP    00010     group has execute permission
   S_IRWXO    00007     mask for permissions for others (not in group)
   S_IROTH    00004     others have read permission
   S_IWOTH    00002     others have write permission
   S_IXOTH    00001     others have execute permission
   ***********************************************************************/
/*free this dynamically allocated linked lists*/
/*
void destroy_lists(){
    dir_node *current_node, *next_node;
    t_node *current_t_node, *next_t_node;
    current_node = queue.head;
    current_t_node = th_list.head;

    while(NULL != current_node){
        next_node = current_node;
        free(current_node);
        current_node = next_node;
    }

    while(NULL != current_t_node){
        next_t_node = current_t_node->next;
        free(current_t_node);
        current_t_node = next_t_node;
    }
}
*/

int is_directory(char* path) {
    struct stat sb;
    if (FAILURE == lstat(path, &sb)){
        handle_regular_perror("lstat ERROR");
        number_of_threads_err++;
        pthread_exit((void*)EXIT_FAILURE);
    }
    if(S_IFDIR == (sb.st_mode & S_IFMT)) return TRUE;
    return FALSE;
}


int is_searchable_directory(char *path){
    DIR *dir;
    if (NULL == (dir = opendir(path))) { // 'opendir' encounter an error
        if(EACCES == errno){ //EACCES Search permission is denied for one of the directories in the path prefix of pathname
            print_permission_error(path);
            handle_error("Error while opening directory"); //ALSO EXIT
        }
        else{
            handle_error("Error while opening directory"); //ALSO EXIT
        }
    }
    closedir(dir);
    return SUCCESS;
}

int is_dot_or_double_dot(char *filename){
    return (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) ? TRUE : FALSE;
}

// this is thread function. when a new thread is entering he got a new stack
void* dir_worm(void *arg){
    dir_node *dequeue_node;
    DIR *dir;
    struct dirent *file_information;
    //here im locking the thread list in order to call the cv for the accumulation of the threads that are being ready to work
    //Furthermore incrementing the counter of ready threads
    pthread_mutex_lock(&mutex_thread_list);
    pthread_cond_wait((&cv_start_searching), &mutex_thread_list);
    starting_thread_counter++;
    pthread_mutex_unlock(&mutex_thread_list);
    while(FALSE == EXIT_FLAG){ //iterating as long as exit_flag is FALSE == 0
        if(TRUE == is_terminating()) break; // first checking wheter I have to continue or quite from that thread
        //thread_node = (t_node*)arg; //threads taking void pointers as argument so I have to cast
        if(NULL == (dequeue_node = dequeue())) break;
        if (NULL == (dir = opendir(dequeue_node->name))) { // checking for opening succeed or permissions of current directory
            // 'opendir' encounter an error
            /* could not open directory */
            if(EACCES == errno){ //EACCES: Search permission is denied for one of the directories in the path prefix of pathname
                print_permission_error(dequeue_node->name);
                free(dequeue_node); //each node allocated dynamically using 'create_dir_node(char *path)'
                continue; //just permission access denied, I can continue and dont exit thread
            }
            perror("Cannot open directory..... exiting thread");
            number_of_threads_err++;
            pthread_exit((void*)TRUE);
        }
        /*The  readdir()  function returns a pointer to a dirent structure representing the next directory entry in the directory stream pointed to by dirp.  It returns NULL on reaching the end of the directory stream or if
       an error occurred.*/
        while(NULL != (file_information = readdir(dir))){ /*iterating the directories as long as EXIT_FLAG allow it and this is not efo as I stated in the man-page*/
            char *current_file_name = file_information->d_name;
            if(TRUE == is_dot_or_double_dot(current_file_name)) continue;
            char *fullpath;
            size_t length = strlen(current_file_name) + strlen(dequeue_node->name) + 1 + 1;
            if(NULL == (fullpath = (char*)calloc(length, sizeof(char)))) handle_thread_error("calloc Error");
            //concatenating into new path which is the whole path
            strcpy(fullpath, dequeue_node->name);
            strcat(fullpath, "/");
            strcat(fullpath, current_file_name);
            if(TRUE == (is_directory(fullpath))){ // this is a directory
                dir_node *enqueue_dir = create_dir_node(fullpath);
                if (NULL == enqueue_dir) handle_thread_error("calloc ERROR");
                enqueue(enqueue_dir);
            }
            else{
                if(NULL != strstr(file_information->d_name, SEARCH_TERM)){ /*The strstr() function finds the first occurrence of the substring int the second argument in the string located in teh first argument. The terminating null bytes ('\0') are not compared.
                    //printf is thread_safe                                                        These functions return a pointer to the beginning of the located substring, or NULL if the substring is not found.*/
                    printf("%s\n",fullpath);
                    number_of_found++; // atomic increment
                }
                free(fullpath);
            }}
        //cleaning after at each iteration
        closedir(dir);
        free(dequeue_node);
    }
    sleep(0.001); // here I found a deadlock. Can be while broadcasting to all threads but one threads isn't in the waiting list of the cv, so I add a short waiting in order to solve that
    pthread_cond_broadcast(&cv_wait_for_work); //everybody finished so signal to everybody to continue, and actually finish execution.
    pthread_exit(FALSE); // here the thread has finished its job
}

void initialize_lockers(){
    int rc;
    rc = pthread_mutex_init(&mutex_queue_operations, NULL);
    if(FALSE != rc) handle_error("mutex init ERROR");
    rc = pthread_mutex_init(&mutex_thread_list, NULL);
    if(FALSE != rc) handle_error("mutex init ERROR");
    rc = pthread_cond_init(&cv_start_searching, NULL);
    if (FALSE != rc) handle_error("condition variable init ERROR");
    rc = pthread_cond_init(&cv_wait_for_work, NULL);
    if (FALSE != rc) handle_error("condition variable init ERROR");
}

void destroy_lock_cond(){
    int rc;
    rc = pthread_mutex_destroy(&mutex_queue_operations);
    if (FALSE != rc) handle_error("mutex destroy ERROR");
    rc = pthread_mutex_destroy(&mutex_thread_list);
    if (FALSE != rc) handle_error("mutex destroy ERROR");
    rc = pthread_cond_destroy(&cv_start_searching);
    if (FALSE != rc) handle_error("condition variable destroy ERROR");
    rc = pthread_cond_destroy(&cv_wait_for_work);
    if (FALSE != rc) handle_error("condition variable destroy ERROR");
}

// calling this func from main which means that the main thread is the caller to join to all threads
bool has_error_during_joining(){
    int rc;
    bool has_error = false;
    void *thread_status;
    t_node *current_thread = th_list.head;
    while(NULL != current_thread){
        rc = pthread_join(current_thread->thread, &thread_status);
        //On success, pthread_join() returns 0; on error, it returns an error number
        if(FALSE != rc) handle_error("pthread_join() error");
        //checking the status of returning thread. value rather than zero indicating error in thread
        if(FALSE != (long)thread_status) has_error = true;
        current_thread = current_thread->next;
    }
    return has_error;
}


int main(int argc, char **argv){
    if(ARGUMENTS_NUM != argc) handle_error("Wrong number of arguments passed"); //ALSO EXIT
    char *ROOT = *(argv + 1);
    SEARCH_TERM = *(argv + 2);
    if(SUCCESS > (THREAD_NUM = atol(*(argv + 3)))) handle_error("wrong threads number"); //assuming this argument is integer grater than zero
    is_searchable_directory(ROOT); //also exit in case of failure to open directory
    initialize_lockers();
    initial_state(ROOT); /*this is the first directory as stated - genesis of things*/
    prepare_threads(); /*here im creating a linked-list of threads in order to free'd them comfortably.*/
    bool has_join_errors= has_error_during_joining();
    int exit_code = (has_join_errors == false) ? EXIT_SUCCESS : EXIT_FAILURE;
    if(EXIT_SUCCESS == exit_code)  printf("Done searching, found %d files\n" ,number_of_found); // no errors
    destroy_lock_cond();
    //destroy_lists();
    exit(exit_code);
}

