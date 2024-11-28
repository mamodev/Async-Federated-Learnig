#ifndef FS_H
#define FS_H

#include <stddef.h>
#include <stdint.h>

int load_file_fd(int fd, char **file_data, size_t *file_size);
int load_file(const char *filename, char **file_data, size_t *file_size);
int ensure_dir_exists(const char *dir);
int remove_directory(const char *path);
int recover_update_folder(const char *folder);

#endif // FS_H