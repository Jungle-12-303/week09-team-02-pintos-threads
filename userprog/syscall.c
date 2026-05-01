#include "userprog/syscall.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "lib/kernel/stdio.h"
#include "threads/interrupt.h"
#include "threads/init.h"
#include "threads/mmu.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include "threads/flags.h"
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

#define MAX_FD 128

struct fd_entry {
	tid_t owner;
	int fd;
	struct file *file;
	bool in_use;
};

static struct lock filesys_lock;
static struct fd_entry fd_table[MAX_FD];
static int next_fd = 2;

static bool check_uaddr (const void *uaddr);
static bool check_buffer (const void *buffer, unsigned size);
static char *copy_in_string (const char *us);
static struct file *lookup_file (int fd);
static int add_file (struct file *file);
static void close_fd_locked (int fd);
static void close_fd (int fd);
static void close_all_fds (void);
static void sys_exit (int status) NO_RETURN;

void
syscall_init (void) {
	lock_init (&filesys_lock);
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
syscall_handler (struct intr_frame *f) {
	switch (f->R.rax) {
		case SYS_HALT:
			power_off ();
			break;
		case SYS_EXIT:
			sys_exit ((int) f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = process_fork ((const char *) f->R.rdi, f);
			break;
		case SYS_EXEC: {
			char *file = copy_in_string ((const char *) f->R.rdi);
			f->R.rax = file == NULL ? -1 : process_exec (file);
			break;
		}
		case SYS_WAIT:
			f->R.rax = process_wait ((tid_t) f->R.rdi);
			break;
		case SYS_CREATE: {
			char *file = copy_in_string ((const char *) f->R.rdi);
			if (file == NULL) {
				f->R.rax = false;
				break;
			}
			lock_acquire (&filesys_lock);
			f->R.rax = filesys_create (file, (off_t) f->R.rsi);
			lock_release (&filesys_lock);
			palloc_free_page (file);
			break;
		}
		case SYS_REMOVE: {
			char *file = copy_in_string ((const char *) f->R.rdi);
			if (file == NULL) {
				f->R.rax = false;
				break;
			}
			lock_acquire (&filesys_lock);
			f->R.rax = filesys_remove (file);
			lock_release (&filesys_lock);
			palloc_free_page (file);
			break;
		}
		case SYS_OPEN: {
			char *name = copy_in_string ((const char *) f->R.rdi);
			if (name == NULL) {
				f->R.rax = -1;
				break;
			}
			lock_acquire (&filesys_lock);
			struct file *file = filesys_open (name);
			int fd = file == NULL ? -1 : add_file (file);
			if (file != NULL && fd == -1)
				file_close (file);
			f->R.rax = fd;
			lock_release (&filesys_lock);
			palloc_free_page (name);
			break;
		}
		case SYS_FILESIZE: {
			lock_acquire (&filesys_lock);
			struct file *file = lookup_file ((int) f->R.rdi);
			f->R.rax = file == NULL ? -1 : file_length (file);
			lock_release (&filesys_lock);
			break;
		}
		case SYS_READ: {
			int fd = (int) f->R.rdi;
			void *buffer = (void *) f->R.rsi;
			unsigned size = (unsigned) f->R.rdx;
			if (!check_buffer (buffer, size)) {
				sys_exit (-1);
			}
			if (fd == 0) {
				uint8_t *buf = buffer;
				for (unsigned i = 0; i < size; i++)
					buf[i] = input_getc ();
				f->R.rax = size;
			} else {
				lock_acquire (&filesys_lock);
				struct file *file = lookup_file (fd);
				f->R.rax = file == NULL ? -1 : file_read (file, buffer, size);
				lock_release (&filesys_lock);
			}
			break;
		}
		case SYS_WRITE: {
			int fd = (int) f->R.rdi;
			const void *buffer = (const void *) f->R.rsi;
			unsigned size = (unsigned) f->R.rdx;
			if (!check_buffer (buffer, size)) {
				sys_exit (-1);
			}
			if (fd == 1) {
				putbuf (buffer, size);
				f->R.rax = size;
			} else {
				lock_acquire (&filesys_lock);
				struct file *file = lookup_file (fd);
				f->R.rax = file == NULL ? -1 : file_write (file, buffer, size);
				lock_release (&filesys_lock);
			}
			break;
		}
		case SYS_SEEK: {
			lock_acquire (&filesys_lock);
			struct file *file = lookup_file ((int) f->R.rdi);
			if (file != NULL)
				file_seek (file, (off_t) f->R.rsi);
			lock_release (&filesys_lock);
			break;
		}
		case SYS_TELL: {
			lock_acquire (&filesys_lock);
			struct file *file = lookup_file ((int) f->R.rdi);
			f->R.rax = file == NULL ? -1 : file_tell (file);
			lock_release (&filesys_lock);
			break;
		}
		case SYS_CLOSE:
			close_fd ((int) f->R.rdi);
			break;
		case SYS_DUP2: {
			lock_acquire (&filesys_lock);
			struct file *old = lookup_file ((int) f->R.rdi);
			f->R.rax = -1;
			if ((int) f->R.rdi == (int) f->R.rsi) {
				f->R.rax = (int) f->R.rsi;
			} else if (old != NULL && f->R.rsi >= 2 && f->R.rsi < MAX_FD) {
				close_fd_locked ((int) f->R.rsi);
				struct file *dup = file_duplicate (old);
				if (dup != NULL) {
					fd_table[f->R.rsi].owner = thread_tid ();
					fd_table[f->R.rsi].fd = (int) f->R.rsi;
					fd_table[f->R.rsi].file = dup;
					fd_table[f->R.rsi].in_use = true;
					f->R.rax = (int) f->R.rsi;
				}
			}
			lock_release (&filesys_lock);
			break;
		}
		default:
			sys_exit (-1);
	}
}

static bool
check_uaddr (const void *uaddr) {
	return uaddr != NULL
		&& is_user_vaddr (uaddr)
		&& pml4_get_page (thread_current ()->pml4, uaddr) != NULL;
}

static bool
check_buffer (const void *buffer, unsigned size) {
	const uint8_t *start = buffer;
	const uint8_t *end = start + size;

	if (size == 0)
		return true;
	if (start == NULL || end < start)
		return false;

	for (const uint8_t *p = pg_round_down (start); p < end; p += PGSIZE) {
		if (!check_uaddr (p))
			return false;
	}
	return check_uaddr (end - 1);
}

static char *
copy_in_string (const char *us) {
	char *ks = palloc_get_page (PAL_ZERO);
	if (ks == NULL)
		return NULL;

	for (size_t i = 0; i < PGSIZE; i++) {
		if (!check_uaddr (us + i)) {
			palloc_free_page (ks);
			return NULL;
		}
		ks[i] = us[i];
		if (ks[i] == '\0')
			return ks;
	}
	palloc_free_page (ks);
	return NULL;
}

static struct file *
lookup_file (int fd) {
	tid_t tid = thread_tid ();
	if (fd < 2 || fd >= MAX_FD || !fd_table[fd].in_use)
		return NULL;
	if (fd_table[fd].owner != tid)
		return NULL;
	return fd_table[fd].file;
}

static int
add_file (struct file *file) {
	tid_t tid = thread_tid ();

	for (int n = 0; n < MAX_FD - 2; n++) {
		int fd = next_fd++;
		if (next_fd >= MAX_FD)
			next_fd = 2;
		if (!fd_table[fd].in_use) {
			fd_table[fd].owner = tid;
			fd_table[fd].fd = fd;
			fd_table[fd].file = file;
			fd_table[fd].in_use = true;
			return fd;
		}
	}
	return -1;
}

static void
close_fd_locked (int fd) {
	struct file *file = lookup_file (fd);
	if (file != NULL) {
		fd_table[fd].in_use = false;
		fd_table[fd].file = NULL;
		file_close (file);
	}
}

static void
close_fd (int fd) {
	lock_acquire (&filesys_lock);
	close_fd_locked (fd);
	lock_release (&filesys_lock);
}

static void
close_all_fds (void) {
	tid_t tid = thread_tid ();

	lock_acquire (&filesys_lock);
	for (int fd = 2; fd < MAX_FD; fd++) {
		if (fd_table[fd].in_use && fd_table[fd].owner == tid) {
			struct file *file = fd_table[fd].file;
			fd_table[fd].in_use = false;
			fd_table[fd].file = NULL;
			file_close (file);
		}
	}
	lock_release (&filesys_lock);
}

static void
sys_exit (int status) {
	printf ("%s: exit(%d)\n", thread_name (), status);
	close_all_fds ();
	thread_exit ();
}
