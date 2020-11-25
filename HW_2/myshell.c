#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <wait.h>
#include <errno.h>

typedef enum _Process_status {PIPE, BACKGROUND, NEITHER} p_STATUS;
/*#################################### SIGNALS HANDLERS ############################################################*/
static struct sigaction zombie_reaper;      // reaping zombie processes
static struct sigaction default_signal;    // default SIGINT handler for the foreground processes
/*############################# FUNCTION DECLARATION ###############################################################*/
p_STATUS status_detect_prepare(int count, char** argv, int *splitting_index);
int regular_process(char **argv);
int background_process(char **argv);
int piped_process(int splitting_index, char **argv);
/*##################################################################################################################*/
int prepare(){
    struct sigaction _sigint_handler;
    //thumb rule : always clear and initialize all structures that are passed to library functions.
    memset(&_sigint_handler, 0, sizeof(_sigint_handler));
    memset(&zombie_reaper, 0, sizeof(zombie_reaper));
    //handling SIGINT signal in the shell process to ignore SIGINT
    _sigint_handler.sa_handler = SIG_IGN; //indicating ignore for sigint signal
    if (sigaction(SIGINT, &_sigint_handler, NULL) == -1) {
        fprintf(stderr, "SIGACTION ERROR");
        return 1;
    }
    zombie_reaper.sa_flags = SA_NOCLDWAIT; // children processes doesnt become zombies instead, releasing immediately at exit
    return 0;
}
int process_arglist(int count, char **arglist){
    int return_value;
    int splitting_index;
    p_STATUS p_status = status_detect_prepare(count, arglist, &splitting_index);
    if(p_status == PIPE){
        return_value = piped_process(splitting_index, arglist);
    }
    else if (p_status == BACKGROUND){
        return_value = background_process(arglist);
    }
    else {
        return_value = regular_process(arglist);
    }
    return return_value;
}
int finalize(void){
    return 0;
}
p_STATUS status_detect_prepare(int count, char** argv, int *splitting_index){
    // Assume no pipe or & will be found.
    p_STATUS status = NEITHER;
    if (strcmp(argv[count - 1], "&") == 0){
        status = BACKGROUND;
        *(argv + count - 1) = NULL;
        *splitting_index = count -1;
    }
    if(status != BACKGROUND){
        // Seeking for '|' symbol
        for (int i = 0; i < count; i++) {
            // Pipe found!
            if (strcmp(argv[i], "|") == 0) {
                status = PIPE;
                *splitting_index = i;
                *(argv + (*(splitting_index))) = NULL;
            }
        }
    }
    // Return an enum showing whether a PIPE, BACKGROUND or NEITHER
    return status;

}
int regular_process(char **argv){
    int p_exit_code;
    pid_t p1_id = fork();
    if (p1_id == -1) {
        fprintf(stderr, "FORK ERROR");
        return 0;
    }
    // child process #1
    if (p1_id == 0) {
        if (sigaction(SIGINT, &default_signal, NULL) == -1) { // reinstalling sigint handler to default one
            fprintf(stderr, "SIGACTION ERROR");
            exit(1);
        }
        execvp(*argv, argv);
        fprintf(stderr,"FAIL TO EXECUTE %s\n",*argv);
        exit(1);
    }
    else{ //father should reap the zombies
        int wait_err = waitpid(p1_id, &p_exit_code, 0);
        if(-1 == wait_err && (ECHILD != errno) && (EINTR != errno)) {
            fprintf(stderr, "WAITPID_ERROR\n");
            exit(1);
        }
    }
    return 1;
}
int background_process(char **argv){
    pid_t p1_id = fork();
    if (p1_id == -1) {
        fprintf(stderr, "FORK ERROR");
        return 0;
    }
    // child process #1
    if (p1_id == 0) {
        execvp(*argv, argv);
        fprintf(stderr,"FAIL TO EXECUTE %s\n",*argv);
        exit(1);
    }
    else{ //father should reap the zombies
        if(sigaction(SIGCHLD, &zombie_reaper, NULL) == -1){
            fprintf(stderr, "SIGACTION ERROR");
            exit(1);
        }
    }
    return 1;
}
int piped_process(int splitting_index, char **argv){
    int fds[2];
    int p1_status, p2_status;
    if (pipe(fds) == -1) {
        fprintf(stderr, "PIPE ERROR");
        return 0;
    }
    int readerfd = fds[0];
    int writerfd = fds[1];
    pid_t p1_id, p2_id;
    p1_id = fork();
    if (p1_id == -1) {
        fprintf(stderr, "FORK ERROR");
        return 0;
    }
    // child process #1
    if (p1_id == 0) {
        if (sigaction(SIGINT, &default_signal, NULL) == -1) { // reinstalling sigint handler to default one
            fprintf(stderr, "SIGACTION ERROR");
            exit(1);
        }
        close(readerfd);
        //redirecting the output of this process into the pipe
        if(dup2(writerfd, STDOUT_FILENO) == -1){
            fprintf(stderr,"DUP2 ERROR\n");
            exit(1);
        }
        close(writerfd);
        execvp(*argv, argv);
        fprintf(stderr,"FAIL TO EXECUTE %s\n",*(argv));
        exit(1);
    }
    else{
        // child process #2
        p2_id = fork();
        if (p2_id == -1) {
            fprintf(stderr, "FORK ERROR\n");
            return 0;
        }
        if (p2_id == 0) {
            if (sigaction(SIGINT, &default_signal, NULL) == -1) { // reinstalling sigint handler to default one
                fprintf(stderr, "SIGACTION ERROR\n");
                exit(1);
            }
            close(writerfd);
            //redirecting the input of this process into the pipe
            if(dup2(readerfd, STDIN_FILENO) == -1){
                fprintf(stderr,"DUP2 ERROR\n");
                exit(1);
            }
            execvp(*(argv + splitting_index + 1), (argv + splitting_index + 1));
            //should not reach out of here
            fprintf(stderr,"FAIL TO EXECUTE %s\n",*(argv + splitting_index + 1));
            exit(1);
        }
        else{
            //shell process
            close(readerfd);
            close(writerfd);
            //shell waiting for the foreground child processes
            int wait_err1 = waitpid(p1_id, &p1_status, 0);
            if(-1 == wait_err1 && (ECHILD != errno) && (EINTR != errno)) {
                fprintf(stderr, "WAITPID_ERROR\n");
                exit(1);
            }
            int wait_err2 = waitpid(p2_id, &p2_status, 0);
            if(-1 == wait_err2 && (ECHILD != errno) && (EINTR != errno)){
                fprintf(stderr,"WAITPID_ERROR\n");
                exit(1);
            }

        }
    }
    return 1;
}
