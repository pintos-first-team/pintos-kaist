#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "userprog/process.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_address(void *addr);
void get_argument(void *esp, int *arg, int count);
void halt(void);
int exec(char *file_name);
void exit(int status);
tid_t fork(const char *thread_name, struct intr_frame *f);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int wait (int child_tid UNUSED);
int open(const char *file);
int add_file_to_fdt(struct file *file);
struct file *find_file_by_fd(int fd);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd) ;
void process_close_file(int fd);

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
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.

	switch (f->R.rdx)
	{
	case SYS_HALT : 
		halt();
		break;
	case SYS_EXIT : 
		exit(f->R.rdi);
		break;
	case SYS_FORK :
		f->R.rax = fork(f->R.rdi, f);
		break;
	case SYS_EXEC :
		if (exec(f->R.rdi) == -1) {
			exit(-1);
		}
		break; 
	case SYS_WAIT :
		f->R.rax = wait(f->R.rdi);
        break;
	case SYS_CREATE :
		f->R.rdx = create(f->R.rdi, f->R.rsi);
		break; 
	case SYS_REMOVE :
		f->R.rdx = remove(f->R.rdi);
		break; 
	case SYS_OPEN :
		f->R.rax = open(f->R.rdi);
		break; 
	case SYS_FILESIZE :
		f->R.rax = filesize(f->R.rdi);
		break; 
	case SYS_READ :
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE :
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK :
		// seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL :
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE :
		close(f->R.rdi);
			break;
	default:
		exit(-1);
		break;
	}

	thread_exit ();
}
void check_address(void *addr){
	// 커널 베이스보다 addr 이 아래인지 검사후 역참조,
	// 포인터가 가리키는 주소가 유저영역의 주소인지 확인 / 포함되지 않으면 -1 리턴
	struct thread *cur = thread_current();
	if (addr == NULL || !(is_user_vaddr(addr)) || pml4_get_page(cur->pml4, addr) == NULL) {
		exit(-1);
	}
}
void get_argument(void *esp, int *arg, int count){
	// 스택 포인터(esp)에 count 만큼의 데이터를 arg에 저장
	// 스택 포인터를 참조하여 count 만큼 스택에 저장된 인자들을 arg 배열로 복사 
	// 인자가 저장된 위치가 유저영역인지 확인 (check_address)
}

void halt(void){
	// 핀토스를 종료시키는 시스템 콜
	power_off();
}
void exit(int status){
	struct thread *curr = thread_current();
	curr->exit_status = status;		
	printf("%s: exit(%d)\n", thread_name(), status); 
	thread_exit();
}
int exec(char *file_name) {
	check_address(file_name);

	int file_size = strlen(file_name)+1;
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL) {
		exit(-1);
	}
	strlcpy(fn_copy, file_name, file_size);

	if (process_exec(fn_copy) == -1) {
		return -1;
	}

	NOT_REACHED();
	return 0;
}
tid_t fork(const char *thread_name, struct intr_frame *f) {
	return process_fork(thread_name, f);
}

bool create(const char *file, unsigned initial_size){
	check_address(file);
	return filesys_create(file,initial_size);
}
bool remove(const char *file){
	check_address(file);
	return filesys_remove(file);
}
int wait (tid_t child_tid UNUSED) {
	return process_wait(child_tid);
}
int open(const char *file) {
	check_address(file);
	struct file *open_file = filesys_open(file);

	if (open_file == NULL) {
		return -1;
	}

	int fd = add_file_to_fdt(open_file);

	// fd table 가득 찼다면
	if (fd == -1) {
		file_close(open_file);
	}
	return fd;
}

int add_file_to_fdt(struct file *file) {
	struct thread *cur = thread_current();
	struct file **fdt = cur->fd_table;

	// fd의 위치가 제한 범위를 넘지 않고, fdtable의 인덱스 위치와 일치한다면
	while (cur->fd_idx < FDT_COUNT_LIMIT && fdt[cur->fd_idx]) {
		cur->fd_idx++;
	}

	// fdt이 가득 찼다면
	if (cur->fd_idx >= FDT_COUNT_LIMIT)
		return -1;

	fdt[cur->fd_idx] = file;
	return cur->fd_idx;
}

int filesize(int fd) {
	struct file *open_file = find_file_by_fd(fd);
	if (open_file == NULL) {
		return -1;
	}
	return file_length(open_file);
}

struct file *find_file_by_fd(int fd) {
	struct thread *cur = thread_current();

	if (fd < 0 || fd >= FDT_COUNT_LIMIT) {
		return NULL;
	}
	return cur->fd_table[fd];
}

int read(int fd, void *buffer, unsigned size) {
	check_address(buffer);

	int read_result;
	struct thread *cur = thread_current();
	struct file *file_fd = find_file_by_fd(fd);

	if (fd == 0) {
		// read_result = i;
		*(char *)buffer = input_getc();		// 키보드로 입력 받은 문자를 반환하는 함수
		read_result = size;
	}
	else {
		if (find_file_by_fd(fd) == NULL) {
			return -1;
		}
		else {
			lock_acquire(&filesys_lock);
			read_result = file_read(find_file_by_fd(fd), buffer, size);
			lock_release(&filesys_lock);
		}
	}
	return read_result;
}

int write(int fd, const void *buffer, unsigned size) {
	check_address(buffer);

	int write_result;
	lock_acquire(&filesys_lock);
	if (fd == 1) {
		putbuf(buffer, size);		// 문자열을 화면에 출력하는 함수
		write_result = size;
	}
	else {
		if (find_file_by_fd(fd) != NULL) {
			write_result = file_write(find_file_by_fd(fd), buffer, size);
		}
		else {
			write_result = -1;
		}
	}
	lock_release(&filesys_lock);
	return write_result;
}
// void seek(int fd, unsigned position) {
// 	struct file *seek_file = find_file_by_fd(fd);
// 	if (seek_file <= 2) {		// 초기값 2로 설정. 0: 표준 입력, 1: 표준 출력
		
// 	}
// 	else {
// 		seek_file->pos = position;
// 	}
// }

unsigned tell(int fd) {
	struct file *tell_file = find_file_by_fd(fd);
	if (tell_file <= 2) {
		return;
	}
	return file_tell(tell_file);
}

void close(int fd) {
	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj == NULL) {
		return;
	}
	process_close_file(fd);
}
void process_close_file(int fd)
{
    struct thread *curr = thread_current();
    struct file **fdt = curr->fd_table;
    if (fd < 2 || fd >= FDT_COUNT_LIMIT)
        return NULL;
    fdt[fd] = NULL;
}