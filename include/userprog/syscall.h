#include "stdbool.h"
#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

typedef int tid_t;
struct lock file_lock;

// void check_address(void *addr);
struct page *check_address(void *addr);
void validate_buffer(void *buffer, size_t size, bool to_write);
/* file descriptor */
int add_file_to_fd_table (struct file *file);
// struct file *get_file_from_fd_table (int fd);

void syscall_init (void);


#endif /* userprog/syscall.h */
