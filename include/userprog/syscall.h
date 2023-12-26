#include "stdbool.h"
#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

typedef int tid_t;

// void check_address(void *addr);
struct page *check_address(void *addr);
void validate_buffer(void *buffer, size_t size, bool to_write);
/* file descriptor */
int add_file_to_fd_table (struct file *file);
// struct file *get_file_from_fd_table (int fd);

void halt(void);
void exit (int status);
tid_t fork (const char *thread_name, struct intr_frame *f);
int exec (const char *file);
int wait (tid_t tid);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

/* project 3 */
void *mmap(void *addr, size_t length, int writable, int fd, off_t offset);
void munmap(void *addr);

void syscall_init (void);


#endif /* userprog/syscall.h */
