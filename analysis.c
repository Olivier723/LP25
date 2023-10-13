//
// Created by flassabe on 26/10/22.
//

#include "analysis.h"

#include <dirent.h>
#include <stddef.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/file.h>
#include <errno.h>

#include "utility.h"

/*!
 * @brief parse_dir parses a directory to find all files in it and its subdirs (recursive analysis of root directory)
 * All files must be output with their full path into the output file.
 * @param path the path to the object directory
 * @param output_file a pointer to an already opened file
 */
void parse_dir(char *path, FILE *output_file){
    // 1. Check parameters
    if (output_file == NULL || !directory_exists(path)) return;

    // 2. Go through all entries: if file, write it to the output file; if a dir, call parse dir on it
    DIR *dir = opendir(path);
    if(dir == NULL){
        fprintf(stderr, "Could not open the directory %s : %s", path, strerror(errno));
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL){
        if (entry->d_type == DT_DIR && (strcmp(entry->d_name, ".") != 0) && (strcmp(entry->d_name, "..") != 0)){
            char next_dir_name[STR_MAX_LEN] = "";
            concat_path(path, entry->d_name, next_dir_name);
            parse_dir(next_dir_name, output_file);
        }
        else if (entry->d_type != DT_DIR){
            char file_path[STR_MAX_LEN] = "";
            concat_path(path, entry->d_name, file_path);
            fprintf(output_file, "%s\n", file_path);
        }
    }
    // 3. Clear all allocated resources
    closedir(dir);
}

/*!
 * @brief clear_recipient_list clears all recipients in a recipients list
 * @param list the list to be cleared
 */
void clear_recipient_list(simple_recipient_t *list){
    if(list == NULL) return;
    simple_recipient_t *next;
    while (list != NULL){
        next = list->next;
        free(list);
        list = next;
    }
}

/*!
 * @brief add_recipient_to_list adds a recipient to a recipients list (as a pointer to a recipient)
 * @param recipient_email the string containing the e-mail to add
 * @param list the list to add the e-mail to
 * @return a pointer to the new recipient (to update the list with)
 */
simple_recipient_t *add_recipient_to_list(char *recipient_email, simple_recipient_t *list){
    if(recipient_email == NULL) return list;   
    simple_recipient_t *new_recipient = malloc(sizeof(simple_recipient_t));
    if(new_recipient == NULL) return list;
    strcpy(new_recipient->email, recipient_email);
    new_recipient->next = list;
    return new_recipient;
}

bool is_mail(char *mail){
    bool is_mail = false;
    uint32_t cur = 0;
    uint32_t mail_len = strlen(mail);
    for(; mail[cur] != '@' && cur < mail_len; ++cur)   if(mail[cur] == '\0') return false;
    for(; mail[cur] != '\0' && cur < mail_len; ++cur)  if(mail[cur] == '.') is_mail = true;
    return is_mail;
}

/*!
 * @brief extract_emails extracts all the e-mails from a buffer and put the e-mails into a recipients list
 * @param buffer the buffer containing one or more e-mails
 * @param list the resulting list
 * @return the updated list
 */
simple_recipient_t *extract_emails(char *buffer, simple_recipient_t *list){
    // 1. Check parameters
    if(buffer == NULL)return list;
    // 2. Go through buffer and extract e-mails
    uint16_t cur = 0;

    uint32_t buffer_len = strlen(buffer);
    if(buffer[cur] != '\t') for(; buffer[cur] != ' ' && cur < buffer_len; ++cur);
    ++cur;

    //Initialize the variables for loop
    char mail[STR_MAX_LEN] = "";
    uint16_t mail_index = 0;

    for (; cur < buffer_len ; ++cur){
        // 3. Add each e-mail to list
        if (buffer[cur] == ','){
            //terminate the mail with '\0'
            mail[mail_index + 1] = '\0';
            if(is_mail(mail))   list = add_recipient_to_list(mail, list);

            //Empty the string
            memset(mail, 0, STR_MAX_LEN);
            mail_index = 0;
            ++cur;
        }
        else if (buffer[cur + 1] == '\0')
        {
            mail[mail_index] = buffer[cur];
            mail[mail_index + 1] = '\0';

            if(is_mail(mail))   list = add_recipient_to_list(mail, list);

            memset(mail, 0, STR_MAX_LEN);
            mail_index = 0;
            ++cur;
        }
        else{
            mail[mail_index] = buffer[cur];
            ++mail_index;
        }
    }
    // 4. Return list
    return list;
}

/*!
 * @brief extract_e_mail extracts an e-mail from a buffer
 * @param buffer the buffer containing the e-mail
 * @param destination the buffer into which the e-mail is copied
 */
void extract_e_mail(char buffer[], char destination[]) {
    uint32_t offset = 0;
    while(buffer[offset] != ' ' && buffer[offset] != '\0') ++offset;
    ++offset;
    if(is_mail(buffer+offset))  strcpy(destination, buffer+offset);
}

// char* get_line(char* file, off_t file_len, off_t* cur, char* line){
//     for(off_t offset = 0; file[*cur] != '\n' && file[*cur-1] != '\r' && file[*cur] != 0 && *cur <= file_len; ++offset, ++(*cur)){
//         line[offset] = file[*cur];
//     }
//     return line;

//     // for(off_t offset = 0; cur <= file_len; ++offset, ++cur){
//     //     if(file[cur] == '\n' || file[cur-1] == '\r' || file[cur] == 0)
//     // }
// }

// Used to track status in e-mail (for multi lines To, Cc, and Bcc fields)
typedef enum { IN_DEST_FIELD, OUT_OF_DEST_FIELD } read_status_t;

/*!
 * @brief parse_file parses mail file at filepath location and writes the result to
 * file whose location is on path output
 * @param filepath name of the e-mail file to analyze
 * @param output path to output file
 * Uses previous utility functions: extract_email, extract_emails, add_recipient_to_list,
 * and clear_recipient_list
 */
void parse_file(char *filepath, char *output){
    // 1. Check parameters
    if (!(path_to_file_exists(filepath) && directory_exists(output))) return;

    FILE *email = fopen(filepath, "r");
    if(email == NULL){
        fprintf(stderr, "[ERROR]Could not open %s : %s\n", filepath, strerror(errno));
        return ;
    }

    char email_line[STR_MAX_LEN] = "";
    char sender[STR_MAX_LEN] = "";
    bool state = OUT_OF_DEST_FIELD;
    bool from_extracted = false;
    bool to_extracted = false;
    bool cc_extracted = false;
    bool bcc_extracted = false;
    simple_recipient_t *recipients_list = NULL;
    while(fgets(email_line, STR_MAX_LEN, email) != NULL){
        //Get rid of the new line char at the end
        email_line[strlen(email_line) - 2] = '\0';
        // 2. Go through e-mail and extract From: address into a buffer
        if(!from_extracted && strncmp(email_line, "From:", 5) == 0){
            from_extracted = true;
            extract_e_mail(email_line ,sender);
        }
        else if(!to_extracted && strncmp(email_line, "To:", 3) == 0){
            state = IN_DEST_FIELD;
            to_extracted = true;
        }
        else if(!cc_extracted && strncmp(email_line, "Cc:", 3) == 0){
            state = IN_DEST_FIELD;
            cc_extracted = true;
        }
        else if(!bcc_extracted && strncmp(email_line, "Bcc:", 4) == 0){
            state = IN_DEST_FIELD;
            bcc_extracted = true;
        }
        else if(email_line[0] != '\t' && state == IN_DEST_FIELD){
            state = OUT_OF_DEST_FIELD;
        }
        if(state == IN_DEST_FIELD){
            recipients_list = extract_emails(email_line, recipients_list);
        }
        if(to_extracted && from_extracted && cc_extracted && bcc_extracted) break;
    }
    fclose(email);
    char output_file_path[STR_MAX_LEN] = "";
    concat_path(output, "step2_output", output_file_path);

    FILE *step2_output = fopen(output_file_path, "a");
    if(step2_output == NULL){
        fprintf(stderr, "[ERROR] Could not open %s : %s\n", output_file_path, strerror(errno));
        clear_recipient_list(recipients_list);
        return;
    }
    int fd = fileno(step2_output);
    // 4. Lock output file
    while(flock(fd, LOCK_EX) < 0);

    // 5. Write to output file according to project instructions
    simple_recipient_t *list_start = recipients_list;
    fwrite(sender, 1, strlen(sender), step2_output);
    while(recipients_list != NULL){
        fwrite(" ", 1, 1, step2_output);
        fwrite(recipients_list->email,1, strlen(recipients_list->email), step2_output);
        recipients_list = recipients_list->next;
    }
    fputs("\n", step2_output);
    
    // 6. Unlock file
    if(flock(fd, LOCK_UN) < 0){
        fclose(step2_output);
        clear_recipient_list(list_start);
        fprintf(stderr, "[ERROR] Could not unlock %s : %s\n", output_file_path, strerror(errno));
        return;
    }
    // 7. Close file
    fclose(step2_output);
    // 8. Clear all allocated resources
    clear_recipient_list(list_start);
}

/*!
 * @brief process_directory goes recursively into directory pointed by its task parameter object_directory
 * and lists all of its files (with complete path) into the file defined by task parameter temporary_directory/name of
 * object directory
 * @param task the task to execute: it is a directory_task_t that shall be cast from task pointer
 * Use parse_dir.
 */
void process_directory(task_t *task)
{
    // 1. Check parameters
    if(task == NULL) return;
    directory_task_t *dir_task = (directory_task_t*)task;
    if (!(directory_exists(dir_task->object_directory) && directory_exists(dir_task->temporary_directory))) return;

    // 2. Go through dir tree and find all regular files
    char usr_name[STR_MAX_LEN] = "";
    strcpy(usr_name, dir_task->object_directory);
    strcpy(usr_name, basename(usr_name));
    strcat(dir_task->temporary_directory, usr_name);
    FILE *out_file = fopen(dir_task->temporary_directory, "w");
    if (out_file == NULL)
    {
        fprintf(stderr, "[ERROR] Cannot open file %s, %s\n", dir_task->temporary_directory, strerror(errno));
        return;
    }
    // 3. Write all file names into output file
    parse_dir(dir_task->object_directory, out_file);
    // 4. Clear all allocated resources
    fclose(out_file);
}

/*!
 * @brief process_file processes one e-mail file.
 * @param task a file_task_t as a pointer to a task (you shall cast it to the proper type)
 * Uses parse_file
 */
void process_file(task_t *task){
    // 1. Check parameters
    if(task == NULL) return;
    // 2. Build full path to all parameters
    file_task_t *file_task = (file_task_t*)task;
    // 3. Call parse_file
    parse_file(file_task->object_file, file_task->temporary_directory);
}
