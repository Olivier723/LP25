#include <stdio.h>
#include <stdint.h>

#include "global_defs.h"
#include "configuration.h"
#include "fifo_processes.h"
#include "mq_processes.h"
#include "direct_fork.h"
#include "reducers.h"
#include "utility.h"
#include "analysis.h"

#include <sys/msg.h>
#include <sys/select.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <dirent.h>

// Choose a method below by uncommenting ONLY one of the following 3 lines:
// #define METHOD_MQ
#define METHOD_FIFO
// #define METHOD_DIRECT

#ifdef METHOD_MQ
#if (defined(METHOD_DIRECT) || defined(METHOD_FIFO))
#error "Only one method may be defined (METHOD_MQ already defined)"
#endif
#endif

#ifdef METHOD_DIRECT
#ifdef METHOD_FIFO
#error "Only one method may be defined (METHOD_DIRECT already defined)"
#endif
#endif

int main(int argc, char *argv[])
{
    struct timeval tv_init, tv_end;
    gettimeofday(&tv_init, NULL);

    // Don't forget to empty default config
    configuration_t config = {
        .data_path = "",
        .temporary_directory = "",
        .output_file = "",
        .is_verbose = false,
        .cpu_core_multiplier = 2,
    };
    make_configuration(&config, argv, argc);
    if (!is_configuration_valid(&config))
    {
        printf("[ERROR] Incorrect configuration:\n");
        display_configuration(&config);
        printf("\n[ERROR] Exiting\n");
        return -1;
    }
    config.process_count = get_nprocs() * config.cpu_core_multiplier;
    printf("[INFO] Running analysis on configuration:\n");
    display_configuration(&config);
    printf("\n[INFO] Please wait, it can take a while\n\n");

    // Running the analysis, based on defined method:

#ifdef METHOD_MQ
    // Initialization
    int mq = make_message_queue();
    if (mq == -1){
        printf("Could not create MQ, exiting\n");
        return -1;
    }
    pid_t *my_children = mq_make_processes(&config, mq);

    // Execution
    mq_process_directory(&config, mq, my_children);
    sync_temporary_files(config.temporary_directory);
    char temp_result_name[STR_MAX_LEN];
    concat_path(config.temporary_directory, "step1_output", temp_result_name);
    files_list_reducer(config.data_path, config.temporary_directory, temp_result_name);
    mq_process_files(&config, mq, my_children);
    sync_temporary_files(config.temporary_directory);
    char step2_file[STR_MAX_LEN];
    concat_path(config.temporary_directory, "step2_output", step2_file);
    files_reducer(step2_file, config.output_file);

    // Clean
    close_processes(&config, mq, my_children);
    free(my_children);
    close_message_queue(mq);
#endif

#ifdef METHOD_FIFO
    char input_file_format[STR_MAX_LEN];
    char output_file_format[STR_MAX_LEN];
    concat_path(config.temporary_directory, "fifo-in-%d", input_file_format);
    concat_path(config.temporary_directory, "fifo-out-%d", output_file_format);
    make_fifos(config.process_count, input_file_format);
    make_fifos(config.process_count, output_file_format);
    make_processes(config.process_count);
    int *command_fifos = open_fifos(config.process_count, input_file_format, O_WRONLY);
    int *notify_fifos = open_fifos(config.process_count, output_file_format, O_RDONLY);
    fifo_process_directory(config.data_path, config.temporary_directory, notify_fifos, command_fifos, config.process_count);
    sync_temporary_files(config.temporary_directory);
    char fifo_temp_result_name[STR_MAX_LEN];
    concat_path(config.temporary_directory, "step1_output", fifo_temp_result_name);
    files_list_reducer(config.data_path, config.temporary_directory, fifo_temp_result_name);
    fifo_process_files(config.data_path, config.temporary_directory, notify_fifos, command_fifos, config.process_count);
    sync_temporary_files(config.temporary_directory);
    char fifo_step2_file[STR_MAX_LEN];
    concat_path(config.temporary_directory, "step2_output", fifo_step2_file);
    //files_reducer(fifo_step2_file, config.output_file);
    shutdown_processes(config.process_count, command_fifos);
    close_fifos(config.process_count, command_fifos);
    close_fifos(config.process_count, notify_fifos);
    erase_fifos(config.process_count, input_file_format);
    erase_fifos(config.process_count, output_file_format);
#endif

#ifdef METHOD_DIRECT
    if (config.is_verbose)
        printf("[VERBOSE] Parsing the data source...\n");
    direct_fork_directories(config.data_path, config.temporary_directory, config.process_count);
    if (config.is_verbose){
        printf("[VERBOSE] Finished parsing the data source\n");
        printf("[VERBOSE] Syncing the temporary files\n");
    }
    sync_temporary_files(config.temporary_directory);
    char direct_temp_result_name[STR_MAX_LEN];
    concat_path(config.temporary_directory, "step1_output", direct_temp_result_name);
    files_list_reducer(config.data_path, config.temporary_directory, direct_temp_result_name);
    if (config.is_verbose) printf("[VERBOSE] Parsing the mails found...\n");
    // parse_file("/home/olivier/Documents/UTBM/TC3/LP25/lp25-project/maildir/horton-s/_sent_mail/9.", "./temp");
    direct_fork_files(config.data_path, config.temporary_directory, config.process_count);
    if(config.is_verbose) {
        printf("[VERBOSE] Finished parsing the mails\n");
        printf("[VERBOSE] Now compiling...\n");
    }
    sync_temporary_files(config.temporary_directory);
    char direct_step2_file[STR_MAX_LEN];
    concat_path(config.temporary_directory, "step2_output", direct_step2_file);
    gettimeofday(&tv_end, NULL);
    uint32_t exec_time = 1000000*(tv_end.tv_sec - tv_init.tv_sec) + (tv_end.tv_usec - tv_init.tv_usec);
    printf("Execution time: %lu microseconds\n", exec_time);

    files_reducer(direct_step2_file, config.output_file);
#endif

    return 0;
}
