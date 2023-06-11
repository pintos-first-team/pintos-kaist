#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "userprog/process.h"
#include "vm/vm.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void halt(void);
int exec(const char *cmd_line);
int fork(const char *thread_name, struct intr_frame *f);
bool create(const char *file, unsigned initial_size);
void check_address(void *addr);
int wait(int pid);
int open(const char *file_name);
void seek(int fd, unsigned position);
unsigned tell (int fd) ;
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
bool remove(const char *file);
int filesize(int fd);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	int syscall_n = f->R.rax; /* 시스템 콜 넘버 */
	switch (syscall_n)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = fork(f->R.rdi, f);
		break;
	case SYS_EXEC:
		f->R.rax = exec(f->R.rdi);
		break;
	case SYS_WAIT:
		f->R.rax = wait(f->R.rdi);
		break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
	}
}
void check_address(void *addr) 
{
	if(addr == NULL){
		exit(-1);
	}
	if(!is_user_vaddr(addr)){
		exit(-1);
	}
	if (pml4_get_page(thread_current()->pml4, addr) == NULL)
        exit(-1);

}
void halt(void)
{
	power_off();
}
void exit(int status)
{
	struct thread *curr = thread_current();
	curr->exit_status = status;
	printf("%s: exit(%d)\n" , curr->name,status);
	thread_exit();
}

int exec(const char *cmd_line)
{
	check_address(cmd_line);
	char *cmd_line_copy;
	cmd_line_copy = palloc_get_page(0);
	if(cmd_line_copy == NULL) {
		exit(-1);
	}
	strlcpy(cmd_line_copy, cmd_line, PGSIZE);

	if(process_exec(cmd_line_copy) == -1){
		exit(-1);
	}
}

int fork(const char *thread_name, struct intr_frame *f)
{
	return process_fork(thread_name, f);
}

void close(int fd) 
{
	if (fd < 2){
		return;
	}
	struct file *file = process_get_file(fd);
	if(file == NULL)
	{
		return;
	}
	file_close(file);
	process_close_file(fd);
}

bool create(const char *file, unsigned initial_size)
{
	// lock_acquire(&filesys_lock);
	check_address(file);
	// bool success = filesys_create(file, initial_size);
	// lock_release(&filesys_lock);
	// return success;
	return filesys_create(file, initial_size);
}

int open(const char *file_name)
{
	check_address(file_name);
	struct file *file = filesys_open(file_name);
	if(file == NULL) {
	
		return -1;
	}
	int fd = process_add_file(file);
	if(fd == -1){
		file_close(file);
	}
	return fd;
}
void seek(int fd, unsigned position)
{
	if(fd <2) {
		return;
	}
	struct file *file = process_get_file(fd);
	if(file == NULL){
		return;
	}
	file_seek(file, position);
}
unsigned tell (int fd) 
{
	if(fd <2) {
		return;
	}
	struct file *file = process_get_file(fd);
	if(file == NULL){
		return;
	}
	return file_tell(file);
}

int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);
	char *ptr = (char *) buffer;
	int bytes_read = 0;
	lock_acquire(&filesys_lock);
	if(fd == STDIN_FILENO){
		for(int i = 0; i < size; i++){
			*ptr++ = input_getc();
			bytes_read++;
		}
		lock_release(&filesys_lock);
	}else {
		if (fd < 2){
			lock_release(&filesys_lock);
			return -1;
		}
		struct file *file = process_get_file(fd);
		if(file == NULL){
			lock_release(&filesys_lock);
			return -1;
		}
		
		bytes_read  = file_read(file, buffer, size);
		lock_release(&filesys_lock);
	}
	return bytes_read;
}

int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);
	int bytes_write = 0;
	if(fd == STDOUT_FILENO)
	{
		putbuf(buffer, size);
		bytes_write = size;
	}
	else {
		if(fd < 2){
			return -1;
		}
		struct file *file = process_get_file(fd);
		if(file == NULL){
			return -1;
		} 
		lock_acquire(&filesys_lock);
		bytes_write = file_write(file, buffer, size);
		lock_release(&filesys_lock);
	}
	return bytes_write;
}

bool remove(const char *file)
{
	check_address(file);
	return filesys_remove(file);

}

int filesize(int fd)
{
	struct file *file = process_get_file(fd);
	if(file == NULL) {
		return -1;
	}
	return file_length(file);
}
int wait(int pid) 
{
	return process_wait(pid);
}