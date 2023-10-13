//
// Created by flassabe on 26/10/22.
//

#include "reducers.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include "global_defs.h"
#include "utility.h"

/*!
 * @brief add_source_to_list adds an e-mail to the sources list. If the e-mail already exists, do not add it.
 * @param list the list to update
 * @param source_email the e-mail to add as a string
 * @return a pointer to the updated beginning of the list
 */
sender_t *add_source_to_list(sender_t *list, char *source_email) {
    if(source_email == NULL) return list;
    sender_t *list_start = list;
    while(list_start != NULL){
        if(strcmp(list_start->sender_address, source_email)==0) return list;
        list_start = list_start->next;
    }

    sender_t *new_node = malloc(sizeof(sender_t));
    if(new_node == NULL) return list;
    new_node->next = list;
    new_node->prev = NULL;
    new_node->head = NULL;
    new_node->tail = NULL;
    strcpy(new_node->sender_address, source_email);
    return new_node;
}

/*!
 * @brief clear_sources_list clears the list of e-mail sources (therefore clearing the recipients of each source)
 * @param list a pointer to the list to clear
 */
void clear_sources_list(sender_t *list) {
    if(list == NULL) return;
    sender_t *next_sender;
    while (list != NULL){
        next_sender = list->next;
        free(list);
        list = next_sender;
        recipient_t *next_recipient;
        while(list->head != NULL){
            next_recipient = list->head->next;
            free(list->head);
            list->head = next_recipient;
        }
    }
}

/*!
 * @brief find_source_in_list looks for an e-mail address in the sources list and returns a pointer to it.
 * @param list the list to look into for the e-mail
 * @param source_email the e-mail as a string to look for
 * @return a pointer to the matching source, NULL if none exists
 */
sender_t *find_source_in_list(sender_t *list, char *source_email) {
    sender_t *list_cpy = list;
    while(list_cpy != NULL){
        if(strcmp(list_cpy->sender_address , source_email) != 0) return list_cpy;
        list_cpy = list_cpy->next;
    }
    return NULL;
}

/*!
 * @brief add_recipient_to_source adds or updates a recipient in the recipients list of a source. It looks for
 * the e-mail in the recipients list: if it is found, its occurrences is incremented, else a new recipient is created
 * with its occurrences = to 1.
 * @param source a pointer to the source to add/update the recipient to
 * @param recipient_email the recipient e-mail to add/update as a string
 */
void add_recipient_to_source(sender_t *source, char *recipient_email) {
    if(source == NULL) return;
    if(recipient_email == NULL) return;

    recipient_t *recipent_start = source->head;
    while(recipent_start != NULL){
        if(strcmp(recipent_start->recipient_address, recipient_email) == 0){
            ++recipent_start->occurrences;
            return;
        }
        recipent_start = recipent_start->next;
    }

    recipient_t *new_recipient = malloc(sizeof(recipient_t));
    if(new_recipient == NULL) return;
    strcpy(new_recipient->recipient_address, recipient_email);
    new_recipient->prev = NULL;
    new_recipient->next = source->head;
    new_recipient->occurrences = 1;
    source->head = new_recipient;
}

/*!
 * @brief files_list_reducer is the first reducer. It uses concatenates all temporary files from the first step into
 * a single file. Don't forget to sync filesystem before leaving the function.
 * @param data_source the data source directory (its directories have the same names as the temp files to concatenate)
 * @param temp_files the temporary files directory, where to read files to be concatenated
 * @param output_file path to the output file (default name is step1_output, but we'll keep it as a parameter).
 */
void files_list_reducer(char *data_source, char *temp_files, char *output_file) {
    DIR* data_dir = opendir(data_source);
    if(data_dir == NULL){
        fprintf(stderr, "[ERROR] Could not open %s : %s\n",data_source, strerror(errno));
        return;
    }

    FILE *out_file = fopen(output_file, "w");
    if(out_file == NULL){
        fprintf(stderr, "[ERROR] Could not open %s : %s\n", output_file, strerror(errno));
        closedir(data_dir);
        return;
    }

    struct dirent *entry = NULL;
    while((entry = next_dir(entry, data_dir)) != NULL){
        char file_name[STR_MAX_LEN] = "";
        concat_path(temp_files, entry->d_name, file_name);
        FILE* temp_file = fopen(file_name, "r");
        if(temp_file == NULL){
            fprintf(stderr, "[ERROR] Could not open %s : %s\n", file_name, strerror(errno));
            fclose(out_file);
            closedir(data_dir);
            return;
        }

        char line[STR_MAX_LEN] = "";
        while(fgets(line, STR_MAX_LEN, temp_file) != NULL){
            fputs(line, out_file);
        }
        fclose(temp_file);
    }

    sync_temporary_files(temp_files);

    fclose(out_file);
    closedir(data_dir);
}

/*!
 * @brief files_reducer opens the second temporary output file (default step2_output) and collates all sender/recipient
 * information as defined in the project instructions. Stores data in a double level linked list (list of source e-mails
 * containing each a list of recipients with their occurrences).
 * @param temp_file path to temp output file
 * @param output_file final output file to be written by your function
 */
void files_reducer(char *temp_file, char *output_file) {
    if(!path_to_file_exists(temp_file) || !path_to_file_exists(output_file)) return;
    FILE *step2 = fopen(temp_file, "r");
    if(step2 == NULL){
        fprintf(stderr, "[ERROR] Could not open %s : %s", temp_file, strerror(errno));
        return;
    }

    char sender_email[STR_MAX_LEN] = "";
    size_t c = 0;
    char *adress = NULL;
    char *line = NULL;
    ssize_t line_length = 0;
    sender_t *sources = NULL;
    uint64_t lines = 0;
    line_length = getline(&line, &c , step2);
    while (line_length != -1){
        line[line_length - 1] = '\0';
        char **line_cpy = &line;
        adress = strtok(*line_cpy, " ");
        
        while(adress != NULL) {
            if(sender_email[0] == '\0') {
                sources = add_source_to_list(sources, adress);
                strcpy(sender_email,adress);
            }else{
                sender_t *current_sender = find_source_in_list(sources, sender_email);
                if(current_sender != NULL)  add_recipient_to_source(current_sender, adress);
            }
            adress = strtok(NULL, " ");
        }
        memset(sender_email, '\0', strlen(sender_email));
        free(line);
        line = NULL;
        line_length = getline(&line, &c , step2);
        printf("%d\n",++lines);
    }

    printf("[D]INGOjrpgnsidf\n");
    fclose(step2);



    FILE *final_output = fopen(output_file, "w");
    if(final_output == NULL){
        clear_sources_list(sources);
        fclose(step2);
        fprintf(stderr, "[ERROR] Could not open %s : %s", temp_file, strerror(errno));
        return;
    }

    sender_t *sources_start = sources;
    while(sources != NULL){
        fwrite(sources->sender_address, 1, strlen(sources->sender_address), final_output);
        recipient_t *recipients_start = sources->head;
        while(recipients_start != NULL){
            fprintf(final_output, " %d: %s",recipients_start->occurrences, recipients_start->recipient_address);
            recipients_start = recipients_start->next;
        }
        fwrite("\n", 1, 1, final_output);
        sources = sources->next;
    }

    clear_sources_list(sources_start);
    fclose(final_output);
}
