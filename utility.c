//
// Created by flassabe on 26/10/22.
//

#include "utility.h"

#include <string.h>
#include <dirent.h>
#include <libgen.h>
#include <unistd.h>
#include <errno.h>

#include <stdio.h> //Debug do not forget to remove

#include "global_defs.h"

/*!
 * @brief cat_path concatenates two file system paths into a result. It adds the separation /  if required.
 * @param prefix first part of the complete path
 * @param suffix second part of the complete path
 * @param full_path resulting path
 * @return pointer to full_path if operation succeeded, NULL else
 */
char *concat_path(char *prefix, char *suffix, char *full_path) {
    if(full_path == NULL) return NULL;
    memset(full_path, '\0', STR_MAX_LEN);
    strcpy(full_path, prefix);
    uint16_t last_char_pos = strlen(prefix) - 1;
    if(prefix[last_char_pos] != '/' && suffix[0] != '/') strcat(full_path, "/");
    strcat(full_path, suffix);
    return full_path;
}

/*!
 * @brief directory_exists tests if directory located at path exists
 * @param path the path whose existence to test
 * @return true if directory exists, false else
 */
bool directory_exists(char *path) {
    if(path == NULL) return false;
    DIR* dir_to_test = opendir(path);
    bool exists = dir_to_test != NULL;
    if(exists)
        closedir(dir_to_test);
    return exists;
}

/*!
 * @brief path_to_file_exists tests if a path leading to a file exists. It separates the path to the file from the
 * file name itself. For instance, path_to_file_exists("/var/log/Xorg.0.log") will test if /var/log exists and is a
 * directory.
 * @param path the path to the file
 * @return true if path to file exists, false else
 */
bool path_to_file_exists(char *path) {
    char dir_path[STR_MAX_LEN] = "";
    strcpy(dir_path, path);
    strcpy(dir_path, dirname(dir_path));
    if(strcmp(dir_path, ".") == 0 && path[0] != '.') return false;
    return directory_exists(dir_path);
}

/*!
 * @brief sync_temporary_files waits for filesystem syncing for a path
 * @param temp_dir the path to the directory to wait for
 * Use fsync and dirfd
 */
void sync_temporary_files(char *temp_dir) {
    DIR * dir = opendir(temp_dir);
    if (dir == NULL){
        fprintf(stderr, "[ERROR] Cannot open %s : %s\n", temp_dir, strerror(errno));
        return;
    }
    if (fsync(dirfd(dir)) == -1){
        fprintf(stderr, "[ERROR] Cannot sync %s : %s\n", temp_dir, strerror(errno));
        closedir(dir);
        return;
    }
    closedir(dir);
}

/*!
 * @brief next_dir returns the next directory entry that is not . or ..
 * @param entry a pointer to the current struct dirent in caller
 * @param dir a pointer to the already opened directory
 * @return a pointer to the next not . or .. directory, NULL if none remain
 */
struct dirent *next_dir(struct dirent *entry, DIR *dir) {
    if(dir == NULL) return NULL;
    do{
        entry = readdir(dir);
        if(entry == NULL) return NULL;
    }while (entry->d_type != DT_DIR || strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0);
    return entry;
}