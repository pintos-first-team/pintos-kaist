#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void check_address(void *addr);
void get_argument(void *esp, int *arg, int count);

void halt(void);
void exit(int status);
void fork(void);

bool create(const char *file, unsigned initial_size);
bool remove(const char *file);

#endif /* userprog/syscall.h */
