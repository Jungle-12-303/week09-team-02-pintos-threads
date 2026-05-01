/* Tests that the highest-priority thread waiting on a semaphore
   is the first to wake up. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static thread_func priority_sema_thread;
static void print_list_priority(struct list *);
static struct semaphore sema;

void test_priority_sema(void)
{
  int i;

  /* This test does not work with the MLFQS. */
  ASSERT(!thread_mlfqs);

  sema_init(&sema, 0);
  thread_set_priority(PRI_MIN);
  for (i = 0; i < 10; i++)
  {
    int priority = PRI_DEFAULT - (i + 3) % 10 - 1;
    char name[16];
    snprintf(name, sizeof name, "priority %d", priority);
    thread_create(name, priority, priority_sema_thread, NULL);
  }

  // print_list_priority(&sema.waiters);
  for (i = 0; i < 10; i++)
  {
    sema_up(&sema);
    msg("Back in main thread.");
  }
}

static void
priority_sema_thread(void *aux UNUSED)
{
  sema_down(&sema);
  msg("Thread %s woke up.", thread_name());
}

/* For Debugging */
static void print_list_priority(struct list *list)
{
  printf("wait list\n");
  ASSERT(!list_empty(list));

  struct list_elem *e = NULL;
  for (e = list_begin(list); e != list_end(list); e = list_next(e))
  {
    struct thread *t = list_entry(e, struct thread, elem);
    printf("%d ", t->priority);
  }
  printf("\n");
}
