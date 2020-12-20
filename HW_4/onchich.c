#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <errno.h>

/*
 * FIFO queue node that hold directory
 */
typedef struct que_node{
    char * dirName;
    struct que_node * next;
    struct que_node * prev;
}que_node;

/*
 * FIFO queue struct
 */
typedef struct que{
    struct que_node * head;
    struct que_node * tail;
    int size;
}que;

/*
 * Threads list node that hold pthread_t
 */
typedef struct td_node{
    pthread_t td;
    struct td_node * next;
}td_node;

/*
 * Threads list struct
 */
typedef struct thread_list{
    struct td_node * head;
    int size;
}thread_list;

static que Q; // FIFO queue for directories.
static thread_list td_list; // Threads list
/*
 * locks an cond ver.
 */
static pthread_mutex_t que_lock;
static pthread_mutex_t tds_lock;
static pthread_mutex_t found_lock;
static pthread_cond_t start_search;
static pthread_cond_t no_empty;
//
static struct sigaction sigint;
static char* search_term; // Keeps the search term from argv[2]
static int exit_sig = 0; // Flag for SIGINT
static int exit_flag = 0; // Flag for exit program
static int found = 0; // Counter for files containing search term
static int start  = 0; // Flag to start with search
static int wait  = 0; // Counter for threads that waiting

/*
 * Check if all threads are waiting and queue is empty.
 * If so exit_flag = 1;
 */
void stop_search(){
    if(wait== (td_list.size-1) && Q.size == 0){
        exit_flag =1 ;
    }
}

/*
 * Init FIFO queue - head and tail point to the root directory(argument).
 * Size field increase by 1.
 */
void make_que(que_node * node){
    Q.head = node;
    Q.tail = node;
    Q.size = 1;
}

/*
 * Create FIFO queue node - init next and prev pointers
 * and keep directory name in dir field.
 * Return the new node.
 */
que_node* make_que_node(char* dir){
    que_node * node;
    if((node=calloc(1, sizeof(que_node))) == 0){
        return NULL;
    }
    node->next = NULL;
    node->prev = NULL;
    node->dirName = dir;
    return node;
}

/*
 * Insert new node to the tail FIFO queue
 * and keep directory name in dir field.
 * Size field of queue increase by 1.
 * Put lock to ensure atomic enqueue.
 */
void enqueue(que_node* que_node){
    pthread_mutex_lock(&que_lock);
    if(Q.size == 0){
        Q.head = que_node;
        Q.tail = que_node;
        Q.size = 1;
    }
    else{
        que_node->prev = Q.tail;
        Q.tail->next = que_node;
        Q.tail = que_node;
        ++Q.size;
    }
    pthread_cond_signal(&no_empty); //signal to threads that queue not empty
    pthread_mutex_unlock(&que_lock);
}

/*
 * Dequeue node from the head FIFO queue
 * and keep directory name in dir field.
 * Size field of queue decrease by 1.
 * Put lock to ensure atomic dequeue.
 * If queue is empty threads waiting to "no_empty" signal (cond ver).
 * wait flag will increase by 1 if thread is waiting, and if he stop
 * waiting wait flag will decrease by 1.
 * Return the dequeue node.
 */
que_node* dequeue(td_node* thread_node){
    que_node* que_node;
    pthread_mutex_lock(&que_lock);
    while(Q.size == 0){
        stop_search();
        if(exit_flag == 1){
            pthread_mutex_unlock(&que_lock);
            return NULL;
        }
        ++wait;
        pthread_cond_wait(&no_empty,&que_lock);
        --wait;
        if(exit_flag == 1){
            pthread_mutex_unlock(&que_lock);
            return NULL;
        }
    }
    que_node = Q.head;
    if(Q.size==1){
        Q.head = NULL;
        Q.tail = NULL;
    }
    else{
        Q.head = que_node->next;
        Q.head->prev = NULL;
        que_node->next = NULL;
    }
    --Q.size;
    pthread_mutex_unlock(&que_lock);
    return que_node;
}

/*
 * Concat path and new directory/file name into one path.
 * Return the new path.
 */
char* all_path(char *str1, char *str2){
    char *res = NULL;
    if((res = calloc(2 + strlen(str1) + strlen(str2), sizeof(char)))== 0){
        perror("Error in calloc all_path");
        pthread_exit((void*)1);
    }
    strcpy(res, str1);
    strcat(res, "/");
    strcat(res, str2);
    return res;
}

/*
 * Check if the path is a directory or file.
 * Return 1 if it is a dir, else 0.
 */
int dir_check(char* path) {
    int res = 0;
    struct stat dir;
    lstat(path, &dir);
    res = S_ISDIR(dir.st_mode);
    return res;
}

/*
 * Threads function routine.
 * At start wait for signal to start search(cond var start_search),
 * main thread broadcast the signal when all threads are created.
 * While exit_flag = 0 the thread dequeue node(with dir name)
 * from the FIFO queue and search for files containing the search term.
 * If the file is a directory the thread enqueue the directory to the FIFO queue.
 * If the a file containing the search term the thread print the path of the file
 * and increase "found" by 1 (put lock on the increasing for atomic).
 * The thread also check if was a SIGINT signal and if the search is done (with stop_search()).
 */
void* td_func(void *a){
    DIR* dir;
    struct dirent *entry;
    que_node* que_nod;
    td_node* td_nod;
    que_node * new_dir;
    char* path;

    pthread_mutex_lock(&tds_lock);
    pthread_cond_wait(&start_search,&tds_lock);
    ++start;
    pthread_mutex_unlock(&tds_lock);

    while (exit_flag == 0){
        stop_search();
        if(exit_flag == 1){
            break;
        }
        td_nod = (td_node*)a;
        que_nod = dequeue(td_nod);
        if (que_nod == NULL){
            break;
        }
        if ((dir = opendir(que_nod->dirName)) == NULL){
            if(errno == EACCES){ // no access to the current dir;
                perror("Error in opendir");
                free(que_nod);
                continue;
            }
            else{
                perror("Error in opendir");
                pthread_exit((void*)1);
            };
        }
        while ((exit_sig == 0)&&(entry = readdir(dir)) != NULL){
            if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
                continue;
            }
            path = all_path(que_nod->dirName, entry->d_name);
            if(dir_check(path) != 0){
                if((new_dir = make_que_node(path))==NULL){
                    perror("Error in make_que_node() callloc");
                    pthread_exit((void*)1);
                }
                enqueue(new_dir);
            }
            else{
                if(strstr(entry->d_name, search_term)){ // if filename contains the search term
                    printf("%s\n",path);
                    pthread_mutex_lock(&found_lock);
                    ++found;
                    pthread_mutex_unlock(&found_lock);
                }
                free(path);
            }
        }
        closedir(dir);
        free(que_nod);
    }
    pthread_cond_broadcast(&no_empty);
    pthread_exit(0);
}

/*
 * Create the thread list-
 * The function create thread list nodes according to num of threads(argv[3])
 * and insert them to the thread list.
 * For each thread node the function create thread an save the thread number
 * in td field (uses pthread_create func).
 * After all threads are created the function(main thread) broadcast "start_search" signal.
 */
void make_thread_list(int numOfTds){
    int i = 0;
    td_node* thread_node;
    td_list.size = numOfTds;
    for (i = 0; i < numOfTds; ++i) {
        if ((thread_node = (td_node*)calloc(1, sizeof(td_node))) == 0) {
            perror("Error in calloc for td_node");
            exit(1);
        }
        if (i == 0) {
            td_list.head = thread_node;
        }
        else{
            thread_node->next = td_list.head;
        }
        td_list.head = thread_node;
        if ((pthread_create(&(thread_node->td), NULL, td_func, (void *)(td_node*)thread_node) != 0)){
            perror("Error in pthread_create");
            exit(1);
        }
    }
    while(start != numOfTds){
        pthread_cond_broadcast(&start_search);
    }

}

/*
 * Function for SIGINT signal
 * change exit_sig and exit_flag flags to 1.
 */
void sigint_exit(int signum, siginfo_t* info, void*ptr){
    exit_sig=1;
    exit_flag =1;
}

/*
 * Init SIGINT signal
 */
void sigint_init(){
    sigint.sa_flags = SA_SIGINFO;
    sigint.sa_sigaction = sigint_exit;
    if(sigaction(SIGINT, &sigint, NULL) != 0){
        perror("Error in sigaction\n");
        exit(1);
    }
}

/*
 * Init locks and cond var.
 */
void init_lock_cond(){
    if(pthread_mutex_init(&que_lock, NULL) != 0){
        perror("Error in que_lock mutex init");
        exit(1);
    }
    if(pthread_mutex_init(&tds_lock, NULL) != 0){
        perror("Error in tds_lock mutex init");
        exit(1);
    }
    if(pthread_mutex_init(&found_lock, NULL) != 0){
        perror("Error in found_lock mutex init");
        exit(1);
    }
    if(pthread_cond_init(&start_search, NULL) != 0){
        perror("Error in start_search cond init");
        exit(1);
    }
    if(pthread_cond_init(&no_empty, NULL) != 0){
        perror("Error in no_empty cond init");
        exit(1);
    }
}

/*
 * Destroy locks and cond var.
 */
void destroy_lock_cond(){
    if(pthread_mutex_destroy(&que_lock) != 0){
        perror("Error in que_lock mutex destroy");
        exit(1);
    }
    if(pthread_mutex_destroy(&tds_lock) != 0){
        perror("Error in tds_lock mutex destroy");
        exit(1);
    }
    if(pthread_mutex_destroy(&found_lock) != 0){
        perror("Error in found_lock mutex destroy");
        exit(1);
    }
    if(pthread_cond_destroy(&start_search) != 0){
        perror("Error in start_search cond destroy");
        exit(1);
    }
    if(pthread_cond_destroy(&no_empty) != 0){
        perror("Error in no_empty cond destroy");
        exit(1);
    }
}

/*
 * Main thread wait for other threads to finish.
 * Return 1 if on of the threads exit with error, else 0.
 */
int join_threads(){
    int res = 0;
    void* status;
    td_node* thread_node;
    thread_node = td_list.head;
    while(thread_node != NULL){
        if((pthread_join(thread_node->td, &status)) != 0){
            perror("Error in pthread_join");
            exit(1);
        }
        if((long)status != 0){
            res =1;
        }
        thread_node = thread_node->next;
    }
    return res;
};

/*
 * Free all FIFO queue and thread list nodes.
 */
void free_lists(){
    td_node* thread_node;
    td_node* tmp1;
    que_node* que_nod;
    que_node* tmp2;
    thread_node = td_list.head;
    que_nod= Q.head;
    while(thread_node != NULL){
        tmp1 =  thread_node;
        thread_node = thread_node->next;
        free(tmp1);
    }
    while(que_nod != NULL){
        tmp2 =  que_nod;
        que_nod = que_nod->next;
        free(tmp2);
    }
};

int main(int argc, char * argv[]){
    int err, numOfTds;
    DIR* dir;
    que_node* que_node;
    if(argc != 4){ // check for correct number of args
        fprintf(stderr,"Wrong number of args\n");
        exit(1);
    }

    if ((dir=opendir(argv[1])) == NULL){ // check that the root directory is searchable
        perror("Error in opening given search root directory");
        exit(1);
    }
    closedir(dir);

    sigint_init();
    init_lock_cond();
    search_term = argv[2];
    numOfTds = atoi(argv[3]);
    que_node= make_que_node(argv[1]);
    make_que(que_node);
    make_thread_list(numOfTds);
    err = join_threads();
    if(exit_sig){
        printf("Search stopped, found %d files\n", found);
    }
    else{
        printf("Done searching, found %d files\n", found);
    }
    destroy_lock_cond();
    free_lists();
    exit(err);
}