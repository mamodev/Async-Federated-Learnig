

#include "fs.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int load_file_fd(int fd, char **file_data, size_t *file_size)
{
    off_t fsize = lseek(fd, 0, SEEK_END);
    if (fsize < 0)
    {
        perror("Failed to get file size");
        return -1;
    }

    lseek(fd, 0, SEEK_SET);
    *file_data = (char *)malloc(fsize);
    if (*file_data == NULL)
    {
        perror("Failed to allocate memory for file");
        return -1;
    }

    ssize_t bytes_read = read(fd, *file_data, fsize);
    if (bytes_read == -1 || bytes_read != fsize)
    {
        perror("Failed to read file");
        free(*file_data);
        return -1;
    }

    *file_size = (size_t)fsize;
    return 1;
}

int load_file(const char *filename, char **file_data, size_t *file_size)
{
    int fd = open(filename, O_RDONLY);
    if (fd == -1)
    {
        perror("Failed to open file");
        return -1;
    }

    off_t fsize = lseek(fd, 0, SEEK_END);
    if (fsize < 0)
    {
        perror("Failed to get file size");
        close(fd);
        return -1;
    }

    lseek(fd, 0, SEEK_SET);
    *file_data = (char *)malloc(fsize);
    if (*file_data == NULL)
    {
        perror("Failed to allocate memory for file");
        close(fd);
        return -1;
    }

    ssize_t bytes_read = read(fd, *file_data, fsize);
    if (bytes_read == -1 || bytes_read != fsize)
    {
        perror("Failed to read file");
        close(fd);
        free(*file_data);
        return -1;
    }

    *file_size = (size_t)fsize;
    close(fd);
    return 1;
}

int ensure_dir_exists(const char *dir)
{
    struct stat st = {0};
    if (stat(dir, &st) == -1)
    {
        if (mkdir(dir, 0700) == -1)
        {
            perror("Failed to create directory");
            return -1;
        }
    }

    return 0;
}

int remove_directory(const char *path)
{
    struct dirent *entry;
    DIR *dir = opendir(path);
    if (!dir)
    {
        // If directory cannot be opened
        perror("opendir");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        char full_path[1024];
        struct stat statbuf;

        // Skip the "." and ".." entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        // Construct full path of the entry
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        // Get information about the entry
        if (stat(full_path, &statbuf) == -1)
        {
            perror("stat");
            closedir(dir);
            return -1;
        }

        if (S_ISDIR(statbuf.st_mode))
        {
            // If it's a directory, recursively remove it
            if (remove_directory(full_path) == -1)
            {
                closedir(dir);
                return -1;
            }
        }
        else
        {
            // Otherwise, it's a file, remove it
            if (remove(full_path) == -1)
            {
                perror("remove");
                closedir(dir);
                return -1;
            }
        }
    }

    closedir(dir);

    // Remove the directory itself
    if (rmdir(path) == -1)
    {
        perror("rmdir");
        return -1;
    }

    return 0;
}

int recover_update_folder(const char *folder)
{
    remove_directory(folder);
    return ensure_dir_exists(folder);
}
