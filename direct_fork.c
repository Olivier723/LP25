//
// Created by flassabe on 26/10/22.
//

#include "direct_fork.h"

#include <dirent.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include "analysis.h"
#include "utility.h"

/*!
 * @brief direct_fork_directories runs the directory analysis with direct calls to fork
 * @param data_source the data source directory with 150 directories to analyze (parallelize with fork)
 * @param temp_files the path to the temporary files directory
 * @param nb_proc the maximum number of simultaneous processes
 */
void direct_fork_directories(char *data_source, char *temp_files, uint16_t nb_proc) {
    // 1. Check parameters
    if(nb_proc == 0 || !directory_exists(data_source) || !directory_exists(temp_files)) return;

    DIR *original_data_source = opendir(data_source);
    if(original_data_source == NULL){
        fprintf(stderr, "[ERROR] Failed to open : %s %s\n", data_source, strerror(errno));
        return;
    }

    int child_number = 0;
    struct dirent *read_file = NULL;
    // 2. Iterate over directories (ignore . and ..)
    while((read_file = next_dir(read_file, original_data_source)) != NULL){
        char data_path[STR_MAX_LEN] = "";
        task_t new_task = {
            .task_callback = process_directory,
        };
        concat_path(data_source, read_file->d_name, data_path);
        strcpy(new_task.argument, data_path);
        strcpy(new_task.argument+STR_MAX_LEN, temp_files);
        // 3. fork and start a task on current directory.
        if(child_number >= nb_proc){
            wait(NULL);
            --child_number;
        }
        pid_t pid = fork();
        if(pid != 0) ++child_number;
        else if (pid == 0){
            closedir(original_data_source);
            new_task.task_callback(&new_task);
            exit(0);
        }
        else perror("Could not create child");
    }
    // 4. Cleanup
    for(int i = 0; i < child_number; ++i){
        wait(NULL);
    }
    closedir(original_data_source);
}

/*!
 * @brief direct_fork_files runs the files analysis with direct calls to fork
 * @param data_source the data source containing the files
 * @param temp_files the temporary files to write the output (step2_output)
 * @param nb_proc the maximum number of simultaneous processes
 */
void direct_fork_files(char *data_source, char *temp_files, uint16_t nb_proc) {
    // 1. Check parameters
    if(!(directory_exists(temp_files) && directory_exists(data_source) && nb_proc > 0)) return;
    
    char file_name[STR_MAX_LEN] = "";
    concat_path(temp_files, "step1_output", file_name);
    FILE *files_list = fopen(file_name, "r");
    if(files_list == NULL){
        fprintf(stderr, "[ERROR] Could not open : %s : %s\n",file_name, strerror(errno));
        return;
    }

    // 2. Iterate over files in files list (step1_output)
    char line[STR_MAX_LEN] = "";
    int task_sent = 0;
    while(fgets(line, STR_MAX_LEN, files_list) != NULL){
        int last_char_pos = strlen(line) - 1;
        line[last_char_pos] = 0;
        task_t new_task = {
            .task_callback = process_file,
            .argument = "",
        };
        strcpy(new_task.argument, line);
        strcpy(new_task.argument+STR_MAX_LEN, temp_files);
        // 3 bis: if max processes count already run, wait for one to end before starting a task.
        if(task_sent >= nb_proc){
            wait(NULL);
            --task_sent;
        }
        // 3. fork and start a task on current file.
        pid_t pid = fork();
        if(pid != 0) ++task_sent;
        else if (pid == 0){
            fclose(files_list);
            new_task.task_callback(&new_task);
            exit(0);
        }
        else perror("Could not create child");

        memset(line, 0, STR_MAX_LEN);
    }
    // 4. Cleanup
    for(int i = 0; i < task_sent; ++i){
        wait(NULL);
    }
    fclose(files_list);
}