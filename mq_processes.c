//
// Created by flassabe on 10/11/22.
//

#include "mq_processes.h"

#include <sys/msg.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <bits/types/sig_atomic_t.h>
#include <stdio.h>
#include <signal.h>

#include "utility.h"
#include "analysis.h"


/*!
 * @brief make_message_queue creates the message queue used for communications between parent and worker processes
 * @return the file descriptor of the message queue
 */
int make_message_queue() {
    key_t my_key = ftok("lp25-project", 1);
    int mq = msgget(my_key, IPC_CREAT | 0600);
    return mq;
}

/*!
 * @brief close_message_queue closes a message queue
 * @param mq the descriptor of the MQ to close
 */
void close_message_queue(int mq) {
    if (mq < 0) {
        fprintf(stderr, "[ERROR] Invalid parameter close_message_queue\n");
        return;
    }
    msgctl(mq, IPC_RMID, NULL);
}

/*!
 * @brief child_process is the function handling code for a child
 * @param mq message queue descriptor used to communicate with the parent
 */
void child_process(int mq) {
    mq_message_t msg_received;
    task_t *task_received;
    long pid = getpid();

    // 1. Endless loop (interrupted by a task whose callback is NULL
    while (1){

        if ((msgrcv(mq, &msg_received, sizeof(mq_message_t), pid, 0)) == -1) {
            fprintf(stderr, "[ERROR] msgrcv child_process\n");
            return;
        }
        task_received = (task_t *)(msg_received.mtext);
        // 2. Upon reception of a task: check is not NULL

        if (task_received->task_callback != NULL) {
            // 2 bis. If not NULL -> execute it and notify parent

            // Execute
            task_received->task_callback((task_t *)msg_received.mtext);

            // Notify parent
            mq_reponse_t msg_sent;
            msg_sent.mtype = 1;
            msg_sent.pid_child = pid;

            if (msgsnd(mq,&msg_sent, sizeof(mq_message_t),0) == -1){
                fprintf(stderr, "[ERROR] msgsnd child_process\n");
                return;
            }

        } else {
            // 2 bis. If NULL -> leave loop
            break;
        }

    }
}

/*!
 * @brief mq_make_processes makes a processes pool used for tasks execution
 * @param config a pointer to the program configuration (with all parameters, inc. processes count)
 * @param mq the identifier of the message queue used to communicate between parent and children (workers)
 * @return a malloc'ed array with all children PIDs
 */
pid_t *mq_make_processes(configuration_t *config, int mq) {
    if (config == NULL || mq < 0) {
        fprintf(stderr, "[ERROR] Invalid parameters mq_make_processes\n");
        return NULL;
    }
    pid_t *my_children = (pid_t *)malloc(sizeof(pid_t)*config->process_count);

    for(int i = 0; i < config->process_count; i++){
        my_children[i] = fork();
        if(my_children[i] == 0){
            child_process(mq);
            free(my_children);
            exit (1);
        }
    }
    return my_children;
}

/*!
 * @brief close_processes commands all workers to terminate
 * @param config a pointer to the configuration
 * @param mq the message queue to communicate with the workers
 * @param children the array of children's PIDs
 */
void close_processes(configuration_t *config, int mq, pid_t children[]) {
    if (config == NULL || mq < 0 || children == NULL) {
        fprintf(stderr, "[ERROR] Invalid parameters close_processes\n");
        return;
    }
    task_t task = {.task_callback=NULL};
    mq_message_t msg;
    memcpy(msg.mtext, &task, sizeof(task_t));

    for(int i = 0; i < config->process_count; i++){
        msg.mtype = children[i];
        if (msgsnd(mq, &msg, sizeof(mq_message_t), 0) == -1) {
            fprintf(stderr, "[ERROR] msgsnd mq_process_directory\n");
            return;
        }
    }
}

/*!
 * @brief send_task_to_mq sends a directory task to a worker through the message queue. Directory task's object is
 * data_source/target_dir, temp output file is temp_files/target_dir. Task is sent through MQ with topic equal to
 * the worker's PID
 * @param data_source the data source directory
 * @param temp_files the temporary files directory
 * @param target_dir the name of the target directory
 * @param mq the MQ descriptor
 * @param worker_pid the worker PID
 */
void send_task_to_mq(char data_source[], char temp_files[], char target_dir[], int mq, pid_t worker_pid) {
    if ( mq < 0 || worker_pid == 0) {
        fprintf(stderr, "[ERROR] Invalid parameters send_task_to_mq\n");
        return;
    }
    directory_task_t *directoryTask;

    mq_message_t msg;
    msg.mtype = worker_pid;
    directoryTask = (directory_task_t *)&msg.mtext;

    directoryTask->task_callback = process_directory;
    concat_path(data_source,target_dir,directoryTask->object_directory);
    concat_path(temp_files,target_dir,directoryTask->temporary_directory);

    if (msgsnd(mq,&msg, sizeof(mq_message_t),0) == -1){
        fprintf(stderr, "[ERROR] msgsnd send_task_to_mq\n");
        return;
    }
}

/*!
 * @brief send_file_task_to_mq sends a file task to a worker. It operates similarly to @see send_task_to_mq
 * @param data_source the data source directory
 * @param temp_files the temporary files directory
 * @param target_file the target filename
 * @param mq the MQ descriptor
 * @param worker_pid the worker's PID
 */

void send_file_task_to_mq(char data_source[], char temp_files[], char target_file[], int mq, pid_t worker_pid) {
    if (!path_to_file_exists(data_source) || mq < 0 || worker_pid == 0) {
        fprintf(stderr, "[ERROR] Invalid parameters send_file_task_to_mq\n");
        return;
    }
    file_task_t *fileTask;
    mq_message_t msg;
    msg.mtype = worker_pid;
    fileTask = (file_task_t *)&msg.mtext;

    fileTask->task_callback = process_file;
    concat_path(data_source,target_file,fileTask->object_file);
    concat_path(temp_files,target_file,fileTask->temporary_directory);

    if (msgsnd(mq,&msg, sizeof(mq_message_t),0) == -1){
        fprintf(stderr, "[ERROR] msgsnd send_file_task_to_mq\n");
        return;
    }
}

/*!
 * @brief mq_process_directory root function for parallelizing directory analysis over workers. Must keep track of the
 * tasks count to ensure every worker handles one and only one task. Relies on two steps: one to fill all workers with
 * a task each, then, waiting for a child to finish its task before sending a new one.
 * @param config a pointer to the configuration with all relevant path and values
 * @param mq the MQ descriptor
 * @param children the children's PIDs used as MQ topics number
 */
void mq_process_directory(configuration_t *config, int mq, pid_t children[]) {

    // 1. Check parameters
    if (config == NULL || mq < 0 || children == NULL) {
        fprintf(stderr, "[ERROR] Invalid parameters mq_process_directory\n");
        return;
    }

    // Open email directory
    DIR *dir = opendir(config->data_path);
    if (dir == NULL){
        fprintf(stderr, "[ERROR] Invalid directory mq_process_directory\n");
        return;
    }
    struct dirent *current_dir;

    // 2. Iterate over children and provide one directory to each
    int i = 0;
    while ((current_dir = next_dir(current_dir, dir)) != NULL && i < config->process_count){
        send_task_to_mq(config->data_path, config->temporary_directory, current_dir->d_name, mq, children[i]);
        ++i;
    }


    // 3. Loop while there are directories to process, and while all workers are processing

    while ((current_dir = next_dir(current_dir, dir)) != NULL){
        // Wait for a worker to finish its task
        mq_reponse_t msg_received;

        // if a worker has finish send new task
        if ((msgrcv(mq, &msg_received, sizeof(mq_message_t), 1, 0)) == -1) {
            fprintf(stderr, "[ERROR] msgrcv mq_process_directory\n");
            return;
        }

        send_task_to_mq(config->data_path, config->temporary_directory, current_dir->d_name, mq, msg_received.pid_child);

    }

    // 4. Cleanup
    free(current_dir);
    closedir(dir);
}

/*!
 * @brief mq_process_files root function for parallelizing files analysis over workers. Operates as
 * @see mq_process_directory to limit tasks to one on each worker.
 * @param config a pointer to the configuration with all relevant path and values
 * @param mq the MQ descriptor
 * @param children the children's PIDs used as MQ topics number
 */
void mq_process_files(configuration_t *config, int mq, pid_t children[]) {
     // 1. Check parameters
      if (config == NULL || mq < 0 || children == NULL) {
          fprintf(stderr, "[ERROR] Invalid parameters\n");
          return;
      }

      // Open email file
      FILE *f = fopen(config->data_path,"r");
      if (f == NULL){
          fprintf(stderr, "[ERROR] Invalid file\n");
          return;
      }

      // 2. Iterate over children and provide one file to each
      char * line = NULL;
      size_t len = 2*STR_MAX_LEN;
      ssize_t read;
      int i = 0;
      while ((read = getline(&line, &len , f)) != -1 && i < config->process_count) {
          // Send the task
          send_file_task_to_mq((char *)read , config->temporary_directory, config->output_file, mq, children[i]);
          ++i;
      }

      // 3. Loop while there are files to process, and while all workers are processing

      while ((read = getline(&line, &len , f)) != -1) {
          mq_message_t msg_received;
          // if a worker has finish send new task
          do {
              if (msgsnd(mq, &msg_received, sizeof(task_t), 0) == -1) {
                  fprintf(stderr, "[ERROR] msgsnd\n");
                  return;
              }
              if (msg_received.mtext[0] != 1){
                  break;
              }
              if ((msgrcv(mq, &msg_received, sizeof(task_t), 0, 0)) == -1) {
                  fprintf(stderr, "[ERROR] msgrcv\n");
                  return;
              }
          } while (msg_received.mtext[0] != 1);
          send_file_task_to_mq((char *)read, config->temporary_directory, config->output_file, mq, msg_received.mtype);

      }
      if (line)
          free(line);

      // 4. Cleanup
      fclose(f);
}
