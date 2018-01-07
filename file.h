#ifndef _COFS_FILE_H
#define _COFS_FILE_H

ssize_t cofs_file_read(struct file *file, char *buffer, size_t max, loff_t *offset);

ssize_t cofs_file_write(struct file *file, const char *buffer, size_t max, loff_t *offset);

#endif
