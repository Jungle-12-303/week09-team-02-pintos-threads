#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "lib/kernel/stdio.h"
#include "threads/interrupt.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "threads/synch.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void sys_exit (int status);
bool sys_create (const char *file, unsigned initial_size);
int sys_open (const char *file);
int sys_filesize (int fd);
int sys_read (int fd, void *buffer, unsigned size);
int sys_write (int fd, char *buffer, size_t size);
void sys_close (int fd);

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
#define STDIN_FILENO     0

// 파일시스템 동시성을 위한 락
static struct lock filesys_lock;

/* 유저 프로그램이 넘긴 버퍼 주소가 커널에서 접근해도 안전한지 검사한다.
 * 잘못된 주소(NULL, 커널 주소, 매핑되지 않은 주소 등)이면 현재 프로세스를
 * exit(-1) 흐름으로 종료시키기 위한 helper다. */
static void check_user_ptr (const void *uaddr);
static void check_user_buffer (const void *uaddr, unsigned size);

/* 유저 영역에 있는 문자열을 끝의 '\0'까지 한 글자씩 검증한 뒤,
 * 커널 메모리로 복사해서 반환한다. exec/open/create처럼 문자열 인자를
 * 받는 syscall에서 원본 유저 포인터를 그대로 믿지 않기 위해 사용한다. */
static char *copy_in_string (const char *uaddr);

/* 현재 프로세스의 종료 상태를 저장하고 thread_exit()으로 종료시키는 helper다.
 * 잘못된 포인터를 만났을 때 exit(-1), 일반 exit syscall에서는 전달받은 status로
 * 프로세스를 끝내는 공통 종료 경로로 사용한다. */
static void exit_with_status (int status);
static int fd_add_file (struct file *file);
static struct file *process_get_file (int fd);
static void fd_close (int fd);

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

	lock_init (&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	uint64_t arg1 = f->R.rdi;
	uint64_t arg2 = f->R.rsi;
	uint64_t arg3 = f->R.rdx;

	switch (f->R.rax) {
	case SYS_CREATE:
		f->R.rax = sys_create ((const char *) arg1, (unsigned) arg2);
		break;
	case SYS_OPEN:
		f->R.rax = sys_open ((const char *) arg1);
		break;
	case SYS_FILESIZE:
		f->R.rax = sys_filesize ((int) arg1);
		break;
	case SYS_READ:
		f->R.rax = sys_read ((int) arg1, (void *) arg2, (unsigned) arg3);
		break;
	case SYS_WRITE:
		f->R.rax = sys_write ((int) arg1, (char *) arg2, (size_t) arg3);
		break;
	case SYS_EXIT:
		sys_exit ((int) (f->R.rdi));
		break;
	case SYS_HALT:
		power_off ();
		break;
	case SYS_CLOSE:
		sys_close ((int) arg1);
		break;
	default:
		thread_exit ();
	};
}

void
sys_exit (int status) {
	struct thread *cur = thread_current ();

	cur->exit_status = status;
	printf ("%s: exit(%d)\n", thread_name (), status);

	thread_exit ();
}

bool
sys_create (const char *file, unsigned initial_size) {
	char *kfile = copy_in_string (file);
	bool success;

	lock_acquire (&filesys_lock);
	success = filesys_create (kfile, initial_size);
	lock_release (&filesys_lock);

	free (kfile);
	return success;
}

int
sys_open (const char *file) {
	char *kfile = copy_in_string (file);
	struct file *opened;
	int fd;

	lock_acquire (&filesys_lock);
	opened = filesys_open (kfile);
	lock_release (&filesys_lock);

	free (kfile);
	if (opened == NULL) {
		return -1;
	}

	fd = fd_add_file (opened);
	if (fd == -1) {
		lock_acquire (&filesys_lock);
		file_close (opened);
		lock_release (&filesys_lock);
	}
	return fd;
}

int
sys_filesize (int fd) {
	struct file *file = process_get_file (fd);
	int size;

	if (file == NULL) {
		return -1;
	}

	lock_acquire (&filesys_lock);
	size = file_length (file);
	lock_release (&filesys_lock);
	return size;
}

int
sys_read (int fd, void *buffer, unsigned size) {
	check_user_buffer (buffer, size);

	// buffer를 char 포인터로 변환해서 문자 단위로 접근
	char *ptr = (char *) buffer;
	int bytes_read = 0;

	if (size == 0) {
		return 0;
	}

	if (fd == STDIN_FILENO) // 표준 입력인 경우
	{
		// 한 글자씩 키보드 입력 받아서 buffer에 저장
		for (unsigned i = 0; i < size; i++) {
			*ptr++ = input_getc (); // 키보드에서 한 글자 입력 받아 저장
			bytes_read++;           // 실제 읽은 바이트 수 증가
		}
	} else {
		// 잘못된 fd(1: stdout이거나 음수인 경우)는 읽을 수 없음 → 에러
		if (fd < 2) {
			return -1;
		}

		// 현재 프로세스의 fd 테이블에서 해당 파일 객체 조회
		struct file *file = process_get_file (fd);
		if (file == NULL) {
			return -1; // 파일이 없으면 실패
		}

		lock_acquire (&filesys_lock);
		// 파일에서 size만큼 읽어서 buffer에 저장
		bytes_read = file_read (file, buffer, size);

		lock_release (&filesys_lock); // 락 해제
	}

	// 실제로 읽은 바이트 수 반환 (0 이상)
	return bytes_read;
}

int
sys_write (int fd, char *buffer, size_t size) {
	check_user_buffer (buffer, size);

	if (size == 0) {
		return 0;
	}

	// stdin (fd == 0)은 write 대상이 아님 → 에러 반환
	if (fd == 0) {
		return -1;
	}

	// stdout (fd == 1): 콘솔 출력 → putbuf로 출력하고 size만큼 썼다고 리턴
	if (fd == 1) {
		putbuf (buffer, size); // 콘솔에 buffer 내용을 출력
		return size;           // 실제 쓴 바이트 수 반환
	}

	// 일반 파일에 대해 file descriptor 테이블에서 file 객체를 가져옴
	struct file *file = process_get_file (fd);
	if (file == NULL)
		return -1; // 해당 fd에 해당하는 파일이 없으면 에러 반환

	// 파일 시스템 접근 시 동시성 제어 위해 lock 획득
	lock_acquire (&filesys_lock);

	// 파일에 buffer 내용을 size 바이트만큼 write
	int bytes_write = file_write (file, buffer, size);

	// 파일 시스템 락 해제
	lock_release (&filesys_lock);

	// write 실패 시 음수 반환 (보통 -1)
	if (bytes_write < 0)
		return -1;

	// 성공 시 실제로 write한 바이트 수 반환
	return bytes_write;
}

void
sys_close (int fd) {
	fd_close (fd);
}

static void
check_user_ptr (const void *uaddr) {
	struct thread *cur = thread_current ();

	if (uaddr == NULL || !is_user_vaddr (uaddr) ||
	    pml4_get_page (cur->pml4, uaddr) == NULL) {
		exit_with_status (-1);
	}
}

static void
check_user_buffer (const void *uaddr, unsigned size) {
	const uint8_t *buffer = uaddr;

	for (unsigned i = 0; i < size; i++) {
		check_user_ptr (buffer + i);
	}
}

static char *
copy_in_string (const char *uaddr) {
	size_t len = 0;

	check_user_ptr (uaddr);
	while (true) {
		check_user_ptr (uaddr + len);
		if (uaddr[len] == '\0') {
			break;
		}
		len++;
	}

	char *kbuf = malloc (len + 1);
	if (kbuf == NULL) {
		exit_with_status (-1);
	}
	memcpy (kbuf, uaddr, len + 1);
	return kbuf;
}

static void
exit_with_status (int status) {
	sys_exit (status);
}

static int
fd_add_file (struct file *file) {
	struct thread *cur = thread_current ();
	struct fd_entry *entry;

	if (file == NULL) {
		return -1;
	}

	entry = malloc (sizeof *entry);
	if (entry == NULL) {
		return -1;
	}

	entry->fd = cur->next_fd++;
	entry->file = file;
	list_push_back (&cur->fd_list, &entry->elem);
	return entry->fd;
}

static struct file *
process_get_file (int fd) {
	struct thread *cur = thread_current ();
	struct list_elem *e;

	for (e = list_begin (&cur->fd_list); e != list_end (&cur->fd_list);
	     e = list_next (e)) {
		struct fd_entry *entry = list_entry (e, struct fd_entry, elem);

		if (entry->fd == fd) {
			return entry->file;
		}
	}
	return NULL;
}

static void
fd_close (int fd) {
	struct thread *cur = thread_current ();
	struct list_elem *e;

	for (e = list_begin (&cur->fd_list); e != list_end (&cur->fd_list);
	     e = list_next (e)) {
		struct fd_entry *entry = list_entry (e, struct fd_entry, elem);

		if (entry->fd == fd) {
			lock_acquire (&filesys_lock);
			file_close (entry->file);
			lock_release (&filesys_lock);
			list_remove (&entry->elem);
			free (entry);
			return;
		}
	}
}
