#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "lib/kernel/console.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void sys_exit (int status);

static bool sys_create (const char *file, unsigned initial_size);
static bool sys_remove (const char *file);
static int sys_open (const char *file_name);
static int sys_filesize (int fd);
static int sys_read (int fd, void *buffer, unsigned size);
int sys_write (int argsys, int fd, char *buffer, size_t size);
static void sys_seek (int fd, unsigned position);
static unsigned sys_tell (int fd);
static void sys_close (int fd);

struct file *file = process_get_file(fd); //fd담당자가 helper이름을 뭐라했는지 알아야함

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
	int argsys = f->R.rax;
	uint64_t arg1 = f->R.rdi;
	uint64_t arg2 = f->R.rsi;
	uint64_t arg3 = f->R.rdx;
	uint64_t arg4 = f->R.r10;
	uint64_t arg5 = f->R.r8;
	uint64_t arg6 = f->R.r9;

	switch (argsys) {
	case SYS_CREATE:
		f->R.rax = sys_create ((const char *) arg1, (unsigned) arg2);
		break;

	case SYS_REMOVE:
		f->R.rax = sys_remove ((const char *) arg1);
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
		f->R.rax = sys_write (argsys, (int) arg1, (char *) arg2, (size_t) arg3);
		break;

	case SYS_SEEK:
		sys_seek ((int) arg1, (unsigned) arg2);
		break;

	case SYS_TELL:
		f->R.rax = sys_tell ((int) arg1);
		break;

	case SYS_CLOSE:
		sys_close ((int) arg1);
		break;

	default:
		thread_exit ();
		break;
	}
}

void
sys_exit (int status) {
	struct thread *cur = thread_current ();

	cur->exit_status = status;
	printf ("%s: exit(%d)\n", thread_name (), status);

	thread_exit ();
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

/* SYS_CREATE: 유저가 넘긴 파일 이름과 초기 크기로 새 파일을 만든다. */
static bool
sys_create (const char *file, unsigned initial_size) {
	validate_ptr (file, 1);

	char kernel_buf[NAME_MAX + 1];
	if (!copy_in (kernel_buf, file, sizeof kernel_buf)) {
		return false;
	}

	lock_acquire (&filesys_lock);
	bool success = filesys_create (kernel_buf, initial_size);
	lock_release (&filesys_lock);

	return success;
}

/* SYS_REMOVE: 유저가 넘긴 파일 이름에 해당하는 파일을 삭제한다. */
static bool
sys_remove (const char *file) {
	validate_ptr (file, 1);

	lock_acquire (&filesys_lock);
	bool success = filesys_remove (file);
	lock_release (&filesys_lock);

	return success;
}

/* SYS_OPEN: 파일을 열고 현재 프로세스의 fd table에 등록한 뒤 fd 번호를 반환한다. */
static int
sys_open (const char *file_name) {
	validate_ptr (file_name, 1);

	char kernel_buf[NAME_MAX + 1];
	if (!copy_in (kernel_buf, file_name, sizeof kernel_buf)) {
		return -1;
	}

	lock_acquire (&filesys_lock);

	struct file *file = filesys_open (kernel_buf);
	if (file == NULL) {
		lock_release (&filesys_lock);
		return -1;
	}

	int fd = fd_add_file (file);
	if (fd == -1) {
		file_close (file);
	}

	lock_release (&filesys_lock);

	return fd;
}

/* SYS_FILESIZE: fd가 가리키는 열린 파일의 전체 크기를 반환한다. */
static int
sys_filesize(int fd)
{
	struct file *file = fd_get_file(fd);

	if (file == NULL) {
		return -1;
	}

	lock_acquire(&filesys_lock);
	int size = file_length(file);
	lock_release(&filesys_lock);

	return size;
}

/* SYS_READ: stdin 또는 열린 파일에서 데이터를 읽어 유저 버퍼에 채운다. */
static int
sys_read(int fd, void *buffer, unsigned size)
{
	check_user_buffer(buffer, size);

	if (size == 0) {
		return 0;
	}

	if (fd == 0) {
		char *ptr = buffer;

		for (unsigned i = 0; i < size; i++) {
			ptr[i] = input_getc();
		}

		return size;
	}

	if (fd == 1) {
		return -1;
	}

	struct file *file = fd_get_file(fd);
	if (file == NULL) {
		return -1;
	}

	lock_acquire(&filesys_lock);
	int bytes_read = file_read(file, buffer, size);
	lock_release(&filesys_lock);

	return bytes_read;
}

/* SYS_WRITE: stdout 또는 열린 파일에 유저 버퍼의 데이터를 쓴다. */
static int
sys_write(int fd, const void *buffer, unsigned size)
{
	check_user_buffer(buffer, size);

	if (size == 0) {
		return 0;
	}

	if (fd == 0) {
		return -1;
	}

	if (fd == 1) {
		putbuf(buffer, size);
		return size;
	}

	struct file *file = fd_get_file(fd);
	if (file == NULL) {
		return -1;
	}

	lock_acquire(&filesys_lock);
	int bytes_write = file_write(file, buffer, size);
	lock_release(&filesys_lock);

	if (bytes_write < 0) {
		return -1;
	}

	return bytes_write;
}

/* SYS_SEEK: fd가 가리키는 파일의 현재 읽기/쓰기 위치를 지정한 위치로 옮긴다. */
static void
sys_seek(int fd, unsigned position)
{
	struct file *file = fd_get_file(fd);

	if (file == NULL) {
		return;
	}

	lock_acquire(&filesys_lock);
	file_seek(file, position);
	lock_release(&filesys_lock);
}

/* SYS_TELL: fd가 가리키는 파일의 현재 읽기/쓰기 위치를 반환한다. */
static unsigned
sys_tell(int fd)
{
	struct file *file = fd_get_file(fd);

	if (file == NULL) {
		return 0;
	}

	lock_acquire(&filesys_lock);
	unsigned pos = file_tell(file);
	lock_release(&filesys_lock);

	return pos;
}

/* SYS_CLOSE: fd table에서 fd를 제거하고 연결된 파일을 닫는다. */
static void
sys_close(int fd)
{
	struct file *file = fd_get_file(fd);

	if (file == NULL) {
		return;
	}

	lock_acquire(&filesys_lock);
	fd_close(fd);
	lock_release(&filesys_lock);
}
