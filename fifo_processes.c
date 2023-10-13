//
// Created by flassabe on 27/10/22.
//

#include "fifo_processes.h"

#include "global_defs.h"
#include <malloc.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include "analysis.h"
#include "utility.h"

/*!
 * @brief make_fifos creates FIFOs for processes to communicate with their parent
 * @param processes_count the number of FIFOs to create
 * @param file_format the filename format, e.g. fifo-out-%d, used to name the FIFOs
 */
void make_fifos(uint16_t processes_count, char *file_format) {
    char file_name[STR_MAX_LEN] = "";
    for(int i = 0; i < processes_count; ++i){
        sprintf(file_name, file_format, i);
        if(mkfifo(file_name, 0666) == -1){
            // printf("%s can't be creating\n", file_name);
            // exit(-1);
        }
    }
}

/*!
 * @brief erase_fifos erases FIFOs used for processes communications with the parent
 * @param processes_count the number of FIFOs to destroy
 * @param file_format the filename format, e.g. fifo-out-%d, used to name the FIFOs
 */
void erase_fifos(uint16_t processes_count, char *file_format) {
    char file_name[STR_MAX_LEN] = "";
    for(int i = 0; i < processes_count; ++i){
        sprintf(file_name, file_format, i);
        remove(file_name);
    }
}

/*!
 * @brief make_processes creates processes and starts their code (waiting for commands)
 * @param processes_count the number of processes to create
 * @return a malloc'ed array with the PIDs of the created processes
 */
pid_t *make_processes(uint16_t processes_count) {
    // 1. Create PIDs array
    pid_t* pids = malloc(sizeof(pid_t)*processes_count);
    // 2. Loop over processes_count to fork
    for(int i = 0; i < processes_count; ++i){
        pids[i] = fork();
        if(pids[i] == 0){
            free(pids);
            // 2 bis. in fork child part, open reading and writing FIFOs, and start listening on reading FIFO
            char input_file_path[STR_MAX_LEN] = "./temp/fifo-in-%d";
            char output_file_path[STR_MAX_LEN] = "./temp/fifo-out-%d";
            sprintf(input_file_path, input_file_path, i);
            sprintf(output_file_path, output_file_path, i);
            int input_file = open(input_file_path, O_RDONLY);
            int output_file = open(output_file_path, O_WRONLY);
            // 3. Upon reception, apply task
            task_t* task = calloc(1, sizeof(task_t));
            do{
                if(read(input_file, task, sizeof(task_t)) == -1) perror("read");
                task->task_callback(task);
                bool temp = 1;
                if(write(output_file, &temp, sizeof(bool)) == -1) perror("write");
            }while(task->task_callback != NULL);
            // 3 bis. If task has a NULL callback, terminate process (don't forget cleanup).
            free(task);
            close(input_file);
            close(output_file);
            exit(0);
        }
    }
    return pids;
}

/*!
 * @brief open_fifos opens FIFO from the parent's side
 * @param processes_count the number of FIFOs to open (must be created before)
 * @param file_format the name pattern of the FIFOs
 * @param flags the opening mode for the FIFOs
 * @return a malloc'ed array of opened FIFOs file descriptors
 */
int *open_fifos(uint16_t processes_count, char *file_format, int flags) {
    int* files = malloc(sizeof(int)*processes_count);
    char file_name[STR_MAX_LEN] = "";
    for(int i = 0; i < processes_count; ++i){
        sprintf(file_name, file_format, i);
        if(access(file_name, F_OK) != 0){
            printf("%s doesn't exist\n", file_name);
            exit(-1);
        }
        files[i] = open(file_name, flags);
    }
    return files;
}

/*!
 * @brief close_fifos closes FIFOs opened by the parent
 * @param processes_count the number of FIFOs to close
 * @param files the array of opened FIFOs as file descriptors
 */
void close_fifos(uint16_t processes_count, int *files) {
    for(int i = 0; i < processes_count; ++i) close(files[i]);
}

/*!
 * @brief shutdown_processes terminates all worker processes by sending a task with a NULL callback
 * @param processes_count the number of processes to terminate
 * @param fifos the array to the output FIFOs (used to command the processes) file descriptors
 */
void shutdown_processes(uint16_t processes_count, int *fifos) {
    // 1. Loop over processes_count
    for(int i =0; i< processes_count; ++i){
        // 2. Create an empty task (with a NULL callback)
        task_t task = {.task_callback=NULL};
        // 3. Send task to current process
        if(write(fifos[i], &task, sizeof(task_t)) == -1) perror("write");
    }
}

/*!
 * @brief prepare_select prepares fd_set for select with all file descriptors to look at
 * @param fds the fd_set to initialize
 * @param filesdes the array of file descriptors
 * @param nb_proc the number of processes (elements in the array)
 * @return the maximum file descriptor value (as used in select)
 */
int prepare_select(fd_set *fds, const int *filesdes, uint16_t nb_proc) {
    if(filesdes == NULL) return -1;
    FD_ZERO(fds);
    int max = -1;
    for(int i = 0; i < nb_proc; ++i){
        if(filesdes[i] > max) max = filesdes[i];
        FD_SET(filesdes[i], fds);
    }
    return max;
}

void send_file_task(char *file_path, char *temp_files, int command_fd) {
    if(file_path == NULL || temp_files == NULL) return;
    file_task_t task = {.task_callback=process_file};
    strcpy(task.object_file, file_path);
    strcpy(task.temporary_directory, temp_files);
    if(write(command_fd, &task, sizeof(file_task_t)) == -1) perror("write");
}

/*!
 * @brief send_directory_task sends a directory task to a child process. Must send a directory command on object directory
 * data_source/dir_name, to write the result in temp_files/dir_name. Sends on FIFO with FD command_fd
 * @param data_source the data source with directories to analyze
 * @param temp_files the temporary output files directory
 * @param dir_name the current dir name to analyze
 * @param command_fd the child process command FIFO file descriptor
 */
void send_directory_task(char *data_source, char *temp_files, char *dir_name, int command_fd) {
    if(data_source == NULL || temp_files == NULL || dir_name == NULL) return;
    directory_task_t task = {.task_callback=process_directory};
    concat_path(data_source, dir_name, task.object_directory);
    strcpy(task.temporary_directory, temp_files);
    if(write(command_fd, &task, sizeof(directory_task_t)) == -1) perror("write");
}

int oneIsFree(bool* fifo_free, uint16_t nb_proc){
    for(int i = 0; i < nb_proc; ++i){
        if(fifo_free[i]) return i;
    }
    return -1;
}

int getFreeIndex(bool* fifo_free, uint16_t nb_proc, int* notify_fifos){
    // for(int i=0; i < nb_proc; ++i) printf("%i ", fifo_free[i]);
    // printf("\n");
    int indexOfFree = oneIsFree(fifo_free, nb_proc);
    if(indexOfFree != -1){
        return indexOfFree;
    }else{
        while(oneIsFree(fifo_free, nb_proc) == -1){
            for(int i = 0; i < nb_proc; ++i) if(read(notify_fifos[i], fifo_free+i, sizeof(bool)) == -1) perror("read");
        }
        return oneIsFree(fifo_free, nb_proc);
    }
}

/*!
 * @brief fifo_process_directory is the main function to distribute directory analysis to worker processes.
 * @param data_source the data source with the directories to analyze
 * @param temp_files the temporary files directory
 * @param notify_fifos the FIFOs on which to read for workers to notify end of tasks
 * @param command_fifos the FIFOs on which to send tasks to workers
 * @param nb_proc the maximum number of simultaneous tasks, = to number of workers
 * Uses @see send_directory_task
 */
void fifo_process_directory(char *data_source, char *temp_files, int *notify_fifos, int *command_fifos, uint16_t nb_proc) {
    // 1. Check parameters
    if(!directory_exists(data_source)) return;
    // 2. Iterate over directories (ignore . and ..)
    DIR* dir = opendir(data_source);
    struct dirent* current_dir;
    bool fifo_free[nb_proc];
    for(int i = 0; i < nb_proc; ++i) fifo_free[i] = 1;
    while((current_dir = next_dir(current_dir, dir)) != NULL){
        // 4. Iterate over remaining directories by waiting for a process to finish its task before sending a new one.
        int fifo_index = getFreeIndex(fifo_free, nb_proc, notify_fifos);
        fifo_free[fifo_index] = 0;
        // 3. Send a file task to each running worker process (you may create a utility function for this)
        send_directory_task(data_source, temp_files, current_dir->d_name, command_fifos[fifo_index]);
    }
    // 5. Cleanup
    free(current_dir);
    closedir(dir);
}


/*!
 * @brief fifo_process_files is the main function to distribute files analysis to worker processes.
 * @param data_source the data source with the files to analyze
 * @param temp_files the temporary files directory (step1_output is here)
 * @param notify_fifos the FIFOs on which to read for workers to notify end of tasks
 * @param command_fifos the FIFOs on which to send tasks to workers
 * @param nb_proc  the maximum number of simultaneous tasks, = to number of workers
 */
void fifo_process_files(char *data_source, char *temp_files, int *notify_fifos, int *command_fifos, uint16_t nb_proc) {
    // 1. Check parameters
    bool good_params = directory_exists(temp_files) && directory_exists(data_source) && nb_proc > 0;
    if(!good_params) return;

    char file_name[STR_MAX_LEN] = "";
    concat_path(temp_files, "step1_output", file_name);
    FILE *files_list = fopen(file_name, "r");
    if(files_list == NULL){
        fprintf(stderr, "[ERROR] Could not open : %s : %s\n",file_name, strerror(errno));
        return;
    }
    //init var
    char current_file[STR_MAX_LEN];
    bool fifo_free[nb_proc];
    for(int i = 0; i < nb_proc; ++i) fifo_free[i] = 1;

    //remove step2_output
    char step2_file[STR_MAX_LEN] = "";
    concat_path(temp_files, "step2_output", step2_file);
    remove(step2_file);

    // 2. Iterate over files in step1_output
    while(fgets(current_file, STR_MAX_LEN, files_list)){
        // 3. Send a file task to each running worker process (you may create a utility function for this)
        // 4. Iterate over remaining files by waiting for a process to finish its task before sending a new one.
        current_file[strlen(current_file)-1] = '\0';
        int fifo_index = getFreeIndex(fifo_free, nb_proc, notify_fifos);
        fifo_free[fifo_index] = 0;
        send_file_task(current_file, temp_files, command_fifos[fifo_index]);
    }
    // 5. Cleanup
    fclose(files_list);
}