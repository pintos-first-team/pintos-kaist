#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "filesys.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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
	case SYS_FORK :
		f->R.rdx = fork();
	case SYS_EXEC :
	case SYS_WAIT :
	case SYS_CREATE :
		f->R.rdx = create(f->R.rdi, f->R.rsi);
	case SYS_REMOVE :
		f->R.rdx = remove(f->R.rdi);
	case SYS_OPEN :
	case SYS_FILESIZE :
	case SYS_READ :
	case SYS_WRITE :
	case SYS_SEEK :
	case SYS_TELL :
	case SYS_CLOSE :
	
	default:
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
	struct thread *curr = thread_create();
	// cur->exit_status = status;		
	printf("%s: exit(%d)\n", thread_name(), status); 
	thread_exit();
}

void fork(void){

}

bool create(const char *file, unsigned initial_size){
	check_address(file);
	return filesys_create(file,initial_size);
}
bool remove(const char *file){
	check_address(file);
	return filesys_remove(file);
}
