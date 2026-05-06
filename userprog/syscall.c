#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "lib/kernel/console.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include <stddef.h>

#include "threads/vaddr.h"
#include "threads/mmu.h"
#include <stdint.h>
#include "threads/malloc.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void sys_exit (int status);
int sys_write (int argsys, int fd, char *buffer, size_t size);

static void check_user_ptr (const void *uaddr);
static void check_user_buffer (const void *buffer, size_t size);
static char *copy_in_string (const char *user_str);
static void exit_with_status (int status);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR         0xc0000081 /* Segment selector msr */
#define MSR_LSTAR        0xc0000082 /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr (MSR_STAR, ((uint64_t) SEL_UCSEG - 0x10) << 48 |
	                             ((uint64_t) SEL_KCSEG) << 32);
	write_msr (MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr (MSR_SYSCALL_MASK,
	           FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	int argsys = f->R.rax;
	uint64_t arg1 = f->R.rdi;
	uint64_t arg2 = f->R.rsi;
	uint64_t arg3 = f->R.rdx;

	switch (f->R.rax) {
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
		check_user_buffer ((const void *) arg2, (size_t) arg3);
		f->R.rax = sys_write (argsys, (int) arg1, (char *) arg2, (size_t) arg3);
		break;
	case SYS_EXIT:
		sys_exit ((int) (f->R.rdi));
		break;
	case SYS_HALT:
		power_off ();
		break;
	default:
		thread_exit ();
	};
}

void
sys_exit (int status) {
	printf ("%s: exit(%d)\n", thread_name (), status);
	exit_with_status (status);
}

int
sys_write (int argsys, int fd, char *buffer, size_t size) {
	if (fd == 1) {
		putbuf ((const char *) buffer, size);
		return argsys = size;
	} else {
		return argsys = -1;
	}
}

static void
check_user_ptr (const void *uaddr) {
	// 1. 유저가 NULL 포인터를 넘겼는지 먼저 확인한다.
	//    NULL은 아무 메모리도 가리키지 않으므로 커널이 읽으면 안 된다.
	if (uaddr == NULL) {
		exit_with_status (-1);
	}

	// 2. 주소가 유저 영역인지 확인한다.
	//    커널 영역 주소를 유저 프로그램이 넘기면 커널 내부 메모리를 건드릴 수 있으므로 막아야 한다.

	if (!is_user_vaddr (uaddr)) {
		exit_with_status (-1);
	}

	// 3. 주소가 페이지 테이블에 매핑되어 있는지 확인한다.
	//    유저 영역 주소라도 페이지 테이블에서 매핑이 안 된 주소는 읽으면 안 된다.
	if (pml4_get_page (thread_current ()->pml4, uaddr) == NULL) {
		exit_with_status (-1);
	}
}

static void
check_user_buffer (const void *buffer, size_t size) {
	const uint8_t *start = buffer;

	if (size == 0) {
		return;
	}

	for (size_t i = 0; i < size; i++) {
		check_user_ptr (start + i);
	}
}

static char *
copy_in_string (const char *uaddr) {
	size_t len = 0;
	char *kbuf;

	// 1. 문자열의 첫 주소가 유효한지 check_user_ptr()로 확인한다.
	check_user_ptr (uaddr);

	// 2. 문자열 끝 표시인 '\0'을 만날 때까지 한 글자씩 확인한다.
	//    strlen(uaddr)를 먼저 쓰면 안 된다. strlen 자체가 유저 메모리를 읽기 때문이다.
	while (true) {
		check_user_ptr (uaddr + len);

		if (uaddr[len] == '\0') {
			break;
		}

		len++;
	}

	// 4. 문자열 길이를 알았으면 malloc(len + 1)로 커널 메모리를 확보한다.
	//    +1은 마지막 '\0'까지 복사하기 위한 공간이다.
	kbuf = malloc (len + 1);
	if (kbuf == NULL) {
		exit_with_status (-1);
	}

	// 5. 유저 문자열을 커널 버퍼로 복사한다.
	//    이후 커널 내부 함수에는 원본 유저 포인터가 아니라 이 커널 버퍼를 넘긴다.
	for (size_t i = 0; i <= len; i++) {
		kbuf[i] = uaddr[i];
	}

	// 6. 복사한 커널 문자열 포인터를 반환한다.
	//    이 메모리는 사용이 끝난 뒤 free()로 해제해야 한다.
	return kbuf;
}
static void
exit_with_status (int status) {
	struct thread *cur = thread_current ();

	// 1. thread_current()로 현재 실행 중인 thread를 가져온다.
	//    Pintos에서는 유저 프로세스 하나가 thread 하나로 표현된다.

	// 2. 현재 thread의 exit_status에 status를 저장한다.
	//    부모가 wait(child)에서 자식의 종료 값을 받을 때 이 값이 필요하다.
	cur->exit_status = status;

	// 3. child_info 구조체를 쓰고 있다면 child_info->exit_status에도 복사한다.
	//    자식 thread는 종료 후 사라질 수 있으므로 부모가 읽을 값은 별도 구조체에 남기는 것이 안전하다.
	
	// if (cur->my_child_info != NULL) {
	// 	cur->my_child_info->exit_status = status;
	// 	cur->my_child_info->exited = true;
	// 	sema_up (&cur->my_child_info->wait_sema);
	// }

	// 4. thread_exit()을 호출한다.
	//    thread_exit() 흐름에서 process_exit()이 호출되고, 열린 파일과 페이지 테이블 같은 자원을 정리한다.
	thread_exit ();
}
