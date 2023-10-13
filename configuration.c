//
// Created by flassabe on 14/10/22.
//

#include "configuration.h"

#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

#include "utility.h"

/*!
 * @brief make_configuration makes the configuration from the program parameters. CLI parameters are applied after
 * file parameters. You shall keep two configuration sets: one with the default values updated by file reading (if
 * configuration file is used), a second one with CLI parameters, to overwrite the first one with its values if any.
 * @param base_configuration a pointer to the base configuration to be updated
 * @param argv the main argv
 * @param argc the main argc
 * @return the pointer to the updated base configuration
 */
configuration_t *make_configuration(configuration_t *base_configuration, char *argv[], int argc) {
    int opt = 0;
    char data_path[STR_MAX_LEN] = "";
    char temporary_directory[STR_MAX_LEN] = "";
    char output_file[STR_MAX_LEN] = "";
    bool is_verbose = false;
    int cpu_core_multiplier = 0;

    while((opt = getopt(argc, argv, "d:t:o:n:vf:")) != -1){
        switch (opt){
        case 'd':
            strcpy(data_path, optarg);
            break;
        case 't':
            strcpy(temporary_directory, optarg);
            break;
        case 'o':
            strcpy(output_file, optarg);
            break;
        case 'v':
            is_verbose = !base_configuration->is_verbose;
            break;
        case 'n':
            uint8_t task_per_thread = atoi(optarg);
            if((task_per_thread < 1) || (task_per_thread > 10)){
                fprintf(stderr, "[WARN] Invalid task number, keeping default : %d\n", base_configuration->cpu_core_multiplier);
                break;
            }
            cpu_core_multiplier = base_configuration->cpu_core_multiplier;
            break;
        case 'f':
            read_cfg_file(base_configuration, optarg);
            break;
        }
    }
    if(data_path[0] != '\0'){
        strcpy(base_configuration->data_path, data_path);
    }
    if(temporary_directory[0] != '\0'){
        strcpy(base_configuration->temporary_directory, temporary_directory);
    }
    if(output_file[0] != '\0'){
        strcpy(base_configuration->output_file, output_file);
    }
    if(is_verbose){
        base_configuration->is_verbose = is_verbose;
    }
    if(cpu_core_multiplier != base_configuration->cpu_core_multiplier && cpu_core_multiplier != 0){
        base_configuration->cpu_core_multiplier = cpu_core_multiplier;
    }
    return base_configuration;
}

/*!
 * @brief skip_spaces advances a string pointer to the first non-space character
 * @param str the pointer to advance in a string
 * @return a pointer to the first non-space character in str
 */
char *skip_spaces(char *str) {
    if(str == NULL) return NULL;
    while(*str==' ') ++str;
    return str;
}

/*!
 * @brief check_equal looks for an optional sequence of spaces, an equal '=' sign, then a second optional sequence
 * of spaces
 * @param str the string to analyze
 * @return a pointer to the first non-space character after the =, NULL if no equal was found
 */
char *check_equal(char *str) {
    if(str == NULL) return NULL;
    str = skip_spaces(str);
    while(*str=='='){
        if(*str == '\0') return NULL;
        ++str;
    } 
    ++str;
    str = skip_spaces(str);
    return str;
}

/*!
 * @brief get_word extracts a word (a sequence of non-space characters) from the source
 * @param source the source string, where to find the word
 * @param target the target string, where to copy the word
 * @return a pointer to the character after the end of the extracted word
 */
char *get_word(char *source, char *target) {
    if(source == NULL || target == NULL) return NULL;
    int index = 0;
    while(source[index] != ' ' && source[index+1] != '\n'){
        index++;
    }
    strncpy(target, source, index);
    target[index + 1] = '\0';
    return source+index;
}

/*!
 * @brief read_cfg_file reads a configuration file (with key = value lines) and extracts all key/values for
 * configuring the program (data_path, output_file, temporary_directory, is_verbose, cpu_core_multiplier)
 * @param base_configuration a pointer to the configuration to update and return
 * @param path_to_cfg_file the path to the configuration file
 * @return a pointer to the base configuration after update, NULL is reading failed.
 */
configuration_t *read_cfg_file(configuration_t *base_configuration, char *path_to_cfg_file) {
    FILE *configuration_file = fopen(path_to_cfg_file, "r");
    if(configuration_file == NULL){
        fprintf(stderr, "[WARN] Failed to open : %s %s\nKeeping default parameters\n",path_to_cfg_file, strerror(errno));
        return NULL;
    }
    char line[STR_MAX_LEN] = "";
    while(fgets(line, STR_MAX_LEN, configuration_file) != NULL){
        char key[STR_MAX_LEN] = "";
        char value[STR_MAX_LEN] = "";
        char *line_modify = line;
        line_modify = get_word(line_modify, key);
        line_modify = check_equal(line_modify);
        if(line_modify == NULL){
            fprintf(stderr, "[ERROR] Incorrect file for config !\n");
            return NULL;
        }
        line_modify = get_word(line_modify, value);
        if(strcmp(key, "is_verbose") == 0){
            if(strcmp(value, "yes") == 0){
                base_configuration->is_verbose = true;
            }else{
                base_configuration->is_verbose = false;
            }
        }else if(strcmp(key, "data_path") == 0){
            strcpy(base_configuration->data_path, value);
        }else if(strcmp(key, "temporary_directory") == 0){
            strcpy(base_configuration->temporary_directory, value);
        }else if(strcmp(key, "output_file") == 0){
            strcpy(base_configuration->output_file, value);
        }else if(strcmp(key, "cpu_core_multiplier") == 0){
            base_configuration->cpu_core_multiplier = atoi(value);
        }
        memset(key, 0, STR_MAX_LEN); //reset string to empty
        memset(value, 0, STR_MAX_LEN);
    }
    fclose(configuration_file);
    return base_configuration;
}

/*!
 * @brief display_configuration displays the content of a configuration
 * @param configuration a pointer to the configuration to print
 */
void display_configuration(configuration_t *configuration) {
    printf("[INFO] Current configuration:\n");
    printf("\tData source: %s\n", configuration->data_path);
    printf("\tTemporary directory: %s\n", configuration->temporary_directory);
    printf("\tOutput file: %s\n", configuration->output_file);
    printf("\tVerbose mode is %s\n", configuration->is_verbose?"on":"off");
    printf("\tCPU multiplier is %d\n", configuration->cpu_core_multiplier);
    printf("\tProcess count is %d\n", configuration->process_count);
    printf("End configuration\n");
}

/*!
 * @brief is_configuration_valid tests a configuration to check if it is executable (i.e. data directory and temporary
 * directory both exist, and path to output file exists @see directory_exists and path_to_file_exists in utility.c)
 * @param configuration the configuration to be tested
 * @return true if configuration is valid, false else
 */
bool is_configuration_valid(configuration_t *configuration) {
    if(!directory_exists(configuration->data_path)) return false;
    if(!directory_exists(configuration->temporary_directory)) return false;
    if(!path_to_file_exists(configuration->output_file)) return false;
    return true;
}