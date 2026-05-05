#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void sys_exit(int status);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081					/* Segment selector msr */
#define MSR_LSTAR 0xc0000082				/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
													((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
						FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f)
{
	// TODO: Your implementation goes here.
	int argsys = f->R.rax;
	uint64_t arg1 = f->R.rdi;
	uint64_t arg2 = f->R.rsi;
	uint64_t arg3 = f->R.rdx;
	uint64_t arg4 = f->R.r10;
	uint64_t arg5 = f->R.r8;
	uint64_t arg6 = f->R.r9;

	switch (f->R.rax)
	{
	case SYS_WRITE:
		// int fd;
		// char *buffer;
		// size_t size;
		// fd = f->R.rdi;
		// buffer = f->R.rsi;
		// size = f->R.rdx;
		// if (fd == 1)
		// {
		// 	putbuf((const char *)buffer, size);
		// 	argsys = size;
		// }
		// else
		// {
		// 	argsys = -1;
		// }
		f->R.rax = sys_write(argsys, (int)arg1, (char *)arg2, (size_t)arg3);
		break;
	case SYS_EXIT:
		sys_exit((int)(f->R.rdi));
		break;
	case SYS_HALT:
		power_off();
		break;
	default:
		thread_exit();
	};
}

void sys_exit(int status)
{
	struct thread *cur = thread_current();

	cur->exit_status = status;
	printf("%s: exit(%d)\n", thread_name(), status);

	thread_exit();
}

int sys_write(int argsys, int fd, char *buffer, size_t size)
{
	if (fd == 1)
	{
		putbuf((const char *)buffer, size);
		return argsys = size;
	}
	else
	{
		return argsys = -1;
	}
}
