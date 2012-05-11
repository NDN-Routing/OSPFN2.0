/* Thread management routine
 * Copyright (C) 1998, 2000 Kunihiro Ishiguro <kunihiro@zebra.org>
 *
 * This file is part of GNU Zebra.
 *
 * GNU Zebra is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * GNU Zebra is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Zebra; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.  
 */

/* #define DEBUG */

#include <zebra.h>

#include "thread.h"
#include "memory.h"
#include "log.h"
#include "hash.h"
#include "command.h"
#include "sigevent.h"

/* Recent absolute time of day */
struct timeval recent_time;
static struct timeval last_recent_time;
/* Relative time, since startup */
static struct timeval relative_time;
static struct timeval relative_time_base;
/* init flag */
static unsigned short timers_inited;

static struct hash *cpu_record = NULL;

/* Struct timeval's tv_usec one second value.  */
#define TIMER_SECOND_MICRO 1000000L

/* Adjust so that tv_usec is in the range [0,TIMER_SECOND_MICRO).
   And change negative values to 0. */
static struct timeval
timeval_adjust (struct timeval a)
{
  while (a.tv_usec >= TIMER_SECOND_MICRO)
    {
      a.tv_usec -= TIMER_SECOND_MICRO;
      a.tv_sec++;
    }

  while (a.tv_usec < 0)
    {
      a.tv_usec += TIMER_SECOND_MICRO;
      a.tv_sec--;
    }

  if (a.tv_sec < 0)
      /* Change negative timeouts to 0. */
      a.tv_sec = a.tv_usec = 0;

  return a;
}

static struct timeval
timeval_subtract (struct timeval a, struct timeval b)
{
  struct timeval ret;

  ret.tv_usec = a.tv_usec - b.tv_usec;
  ret.tv_sec = a.tv_sec - b.tv_sec;

  return timeval_adjust (ret);
}

static long
timeval_cmp (struct timeval a, struct timeval b)
{
  return (a.tv_sec == b.tv_sec
	  ? a.tv_usec - b.tv_usec : a.tv_sec - b.tv_sec);
}

static unsigned long
timeval_elapsed (struct timeval a, struct timeval b)
{
  return (((a.tv_sec - b.tv_sec) * TIMER_SECOND_MICRO)
	  + (a.tv_usec - b.tv_usec));
}

#ifndef HAVE_CLOCK_MONOTONIC
static void
quagga_gettimeofday_relative_adjust (void)
{
  struct timeval diff;
  if (timeval_cmp (recent_time, last_recent_time) < 0)
    {
      relative_time.tv_sec++;
      relative_time.tv_usec = 0;
    }
  else
    {
      diff = timeval_subtract (recent_time, last_recent_time);
      relative_time.tv_sec += diff.tv_sec;
      relative_time.tv_usec += diff.tv_usec;
      relative_time = timeval_adjust (relative_time);
    }
  last_recent_time = recent_time;
}
#endif /* !HAVE_CLOCK_MONOTONIC */

/* gettimeofday wrapper, to keep recent_time updated */
static int
quagga_gettimeofday (struct timeval *tv)
{
  int ret;
  
  assert (tv);
  
  if (!(ret = gettimeofday (&recent_time, NULL)))
    {
      /* init... */
      if (!timers_inited)
        {
          relative_time_base = last_recent_time = recent_time;
          timers_inited = 1;
        }
      /* avoid copy if user passed recent_time pointer.. */
      if (tv != &recent_time)
        *tv = recent_time;
      return 0;
    }
  return ret;
}

static int
quagga_get_relative (struct timeval *tv)
{
  int ret;

#ifdef HAVE_CLOCK_MONOTONIC
  {
    struct timespec tp;
    if (!(ret = clock_gettime (CLOCK_MONOTONIC, &tp)))
      {
        relative_time.tv_sec = tp.tv_sec;
        relative_time.tv_usec = tp.tv_nsec / 1000;
      }
  }
#else /* !HAVE_CLOCK_MONOTONIC */
  if (!(ret = quagga_gettimeofday (&recent_time)))
    quagga_gettimeofday_relative_adjust();
#endif /* HAVE_CLOCK_MONOTONIC */

  if (tv)
    *tv = relative_time;

  return ret;
}

/* Get absolute time stamp, but in terms of the internal timer
 * Could be wrong, but at least won't go back.
 */
static void
quagga_real_stabilised (struct timeval *tv)
{
  *tv = relative_time_base;
  tv->tv_sec += relative_time.tv_sec;
  tv->tv_usec += relative_time.tv_usec;
  *tv = timeval_adjust (*tv);
}

/* Exported Quagga timestamp function.
 * Modelled on POSIX clock_gettime.
 */
int
quagga_gettime (enum quagga_clkid clkid, struct timeval *tv)
{
  switch (clkid)
    {
      case QUAGGA_CLK_REALTIME:
        return quagga_gettimeofday (tv);
      case QUAGGA_CLK_MONOTONIC:
        return quagga_get_relative (tv);
      case QUAGGA_CLK_REALTIME_STABILISED:
        quagga_real_stabilised (tv);
        return 0;
      default:
        errno = EINVAL;
        return -1;
    }
}

/* time_t value in terms of stabilised absolute time. 
 * replacement for POSIX time()
 */
time_t
quagga_time (time_t *t)
{
  struct timeval tv;
  quagga_real_stabilised (&tv);
  if (t)
    *t = tv.tv_sec;
  return tv.tv_sec;
}

/* Public export of recent_relative_time by value */
struct timeval
recent_relative_time (void)
{
  return relative_time;
}

static unsigned int
cpu_record_hash_key (struct cpu_thread_history *a)
{
  return (uintptr_t) a->func;
}

static int 
cpu_record_hash_cmp (const struct cpu_thread_history *a,
		     const struct cpu_thread_history *b)
{
  return a->func == b->func;
}

static void *
cpu_record_hash_alloc (struct cpu_thread_history *a)
{
  struct cpu_thread_history *new;
  new = XCALLOC (MTYPE_THREAD_STATS, sizeof (struct cpu_thread_history));
  new->func = a->func;
  new->funcname = XSTRDUP(MTYPE_THREAD_FUNCNAME, a->funcname);
  return new;
}

static void
cpu_record_hash_free (void *a)
{
  struct cpu_thread_history *hist = a;
 
  XFREE (MTYPE_THREAD_FUNCNAME, hist->funcname);
  XFREE (MTYPE_THREAD_STATS, hist);
}

static inline void 
vty_out_cpu_thread_history(struct vty* vty,
			   struct cpu_thread_history *a)
{
#ifdef HAVE_RUSAGE
  vty_out(vty, "%7ld.%03ld %9d %8ld %9ld %8ld %9ld",
	  a->cpu.total/1000, a->cpu.total%1000, a->total_calls,
	  a->cpu.total/a->total_calls, a->cpu.max,
	  a->real.total/a->total_calls, a->real.max);
#else
  vty_out(vty, "%7ld.%03ld %9d %8ld %9ld",
	  a->real.total/1000, a->real.total%1000, a->total_calls,
	  a->real.total/a->total_calls, a->real.max);
#endif
  vty_out(vty, " %c%c%c%c%c%c %s%s",
	  a->types & (1 << THREAD_READ) ? 'R':' ',
	  a->types & (1 << THREAD_WRITE) ? 'W':' ',
	  a->types & (1 << THREAD_TIMER) ? 'T':' ',
	  a->types & (1 << THREAD_EVENT) ? 'E':' ',
	  a->types & (1 << THREAD_EXECUTE) ? 'X':' ',
	  a->types & (1 << THREAD_BACKGROUND) ? 'B' : ' ',
	  a->funcname, VTY_NEWLINE);
}

static void
cpu_record_hash_print(struct hash_backet *bucket, 
		      void *args[])
{
  struct cpu_thread_history *totals = args[0];
  struct vty *vty = args[1];
  thread_type *filter = args[2];
  struct cpu_thread_history *a = bucket->data;
  
  a = bucket->data;
  if ( !(a->types & *filter) )
       return;
  vty_out_cpu_thread_history(vty,a);
  totals->total_calls += a->total_calls;
  totals->real.total += a->real.total;
  if (totals->real.max < a->real.max)
    totals->real.max = a->real.max;
#ifdef HAVE_RUSAGE
  totals->cpu.total += a->cpu.total;
  if (totals->cpu.max < a->cpu.max)
    totals->cpu.max = a->cpu.max;
#endif
}

static void
cpu_record_print(struct vty *vty, thread_type filter)
{
  struct cpu_thread_history tmp;
  void *args[3] = {&tmp, vty, &filter};

  memset(&tmp, 0, sizeof tmp);
  tmp.funcname = "TOTAL";
  tmp.types = filter;

#ifdef HAVE_RUSAGE
  vty_out(vty, "%21s %18s %18s%s",
  	  "", "CPU (user+system):", "Real (wall-clock):", VTY_NEWLINE);
#endif
  vty_out(vty, "Runtime(ms)   Invoked Avg uSec Max uSecs");
#ifdef HAVE_RUSAGE
  vty_out(vty, " Avg uSec Max uSecs");
#endif
  vty_out(vty, "  Type  Thread%s", VTY_NEWLINE);
  hash_iterate(cpu_record,
	       (void(*)(struct hash_backet*,void*))cpu_record_hash_print,
	       args);

  if (tmp.total_calls > 0)
    vty_out_cpu_thread_history(vty, &tmp);
}

DEFUN(show_thread_cpu,
      show_thread_cpu_cmd,
      "show thread cpu [FILTER]",
      SHOW_STR
      "Thread information\n"
      "Thread CPU usage\n"
      "Display filter (rwtexb)\n")
{
  int i = 0;
  thread_type filter = (thread_type) -1U;

  if (argc > 0)
    {
      filter = 0;
      while (argv[0][i] != '\0')
	{
	  switch ( argv[0][i] )
	    {
	    case 'r':
	    case 'R':
	      filter |= (1 << THREAD_READ);
	      break;
	    case 'w':
	    case 'W':
	      filter |= (1 << THREAD_WRITE);
	      break;
	    case 't':
	    case 'T':
	      filter |= (1 << THREAD_TIMER);
	      break;
	    case 'e':
	    case 'E':
	      filter |= (1 << THREAD_EVENT);
	      break;
	    case 'x':
	    case 'X':
	      filter |= (1 << THREAD_EXECUTE);
	      break;
	    case 'b':
	    case 'B':
	      filter |= (1 << THREAD_BACKGROUND);
	      break;
	    default:
	      break;
	    }
	  ++i;
	}
      if (filter == 0)
	{
	  vty_out(vty, "Invalid filter \"%s\" specified,"
                  " must contain at least one of 'RWTEXB'%s",
		  argv[0], VTY_NEWLINE);
	  return CMD_WARNING;
	}
    }

  cpu_record_print(vty, filter);
  return CMD_SUCCESS;
}

/* List allocation and head/tail print out. */
static void
thread_list_debug (struct thread_list *list)
{
  printf ("count [%d] head [%p] tail [%p]\n",
	  list->count, list->head, list->tail);
}

/* Debug print for thread_master. */
static void  __attribute__ ((unused))
thread_master_debug (struct thread_master *m)
{
  printf ("-----------\n");
  printf ("readlist  : ");
  thread_list_debug (&m->read);
  printf ("writelist : ");
  thread_list_debug (&m->write);
  printf ("timerlist : ");
  thread_list_debug (&m->timer);
  printf ("eventlist : ");
  thread_list_debug (&m->event);
  printf ("unuselist : ");
  thread_list_debug (&m->unuse);
  printf ("bgndlist : ");
  thread_list_debug (&m->background);
  printf ("total alloc: [%ld]\n", m->alloc);
  printf ("-----------\n");
}

/* Allocate new thread master.  */
struct thread_master *
thread_master_create ()
{
  if (cpu_record == NULL) 
    cpu_record 
      = hash_create_size (1011, (unsigned int (*) (void *))cpu_record_hash_key, 
                          (int (*) (const void *, const void *))cpu_record_hash_cmp);
    
  return (struct thread_master *) XCALLOC (MTYPE_THREAD_MASTER,
					   sizeof (struct thread_master));
}

/* Add a new thread to the list.  */
static void
thread_list_add (struct thread_list *list, struct thread *thread)
{
  thread->next = NULL;
  thread->prev = list->tail;
  if (list->tail)
    list->tail->next = thread;
  else
    list->head = thread;
  list->tail = thread;
  list->count++;
}

/* Add a new thread just before the point.  */
static void
thread_list_add_before (struct thread_list *list, 
			struct thread *point, 
			struct thread *thread)
{
  thread->next = point;
  thread->prev = point->prev;
  if (point->prev)
    point->prev->next = thread;
  else
    list->head = thread;
  point->prev = thread;
  list->count++;
}

/* Delete a thread from the list. */
static struct thread *
thread_list_delete (struct thread_list *list, struct thread *thread)
{
  if (thread->next)
    thread->next->prev = thread->prev;
  else
    list->tail = thread->prev;
  if (thread->prev)
    thread->prev->next = thread->next;
  else
    list->head = thread->next;
  thread->next = thread->prev = NULL;
  list->count--;
  return thread;
}

/* Move thread to unuse list. */
static void
thread_add_unuse (struct thread_master *m, struct thread *thread)
{
  assert (m != NULL && thread != NULL);
  assert (thread->next == NULL);
  assert (thread->prev == NULL);
  assert (thread->type == THREAD_UNUSED);
  thread_list_add (&m->unuse, thread);
  /* XXX: Should we deallocate funcname here? */
}

/* Free all unused thread. */
static void
thread_list_free (struct thread_master *m, struct thread_list *list)
{
  struct thread *t;
  struct thread *next;

  for (t = list->head; t; t = next)
    {
      next = t->next;
      if (t->funcname)
        XFREE (MTYPE_THREAD_FUNCNAME, t->funcname);
      XFREE (MTYPE_THREAD, t);
      list->count--;
      m->alloc--;
    }
}

/* Stop thread scheduler. */
void
thread_master_free (struct thread_master *m)
{
  thread_list_free (m, &m->read);
  thread_list_free (m, &m->write);
  thread_list_free (m, &m->timer);
  thread_list_free (m, &m->event);
  thread_list_free (m, &m->ready);
  thread_list_free (m, &m->unuse);
  thread_list_free (m, &m->background);
  
  XFREE (MTYPE_THREAD_MASTER, m);

  if (cpu_record)
    {
      hash_clean (cpu_record, cpu_record_hash_free);
      hash_free (cpu_record);
      cpu_record = NULL;
    }
}

/* Thread list is empty or not.  */
static inline int
thread_empty (struct thread_list *list)
{
  return  list->head ? 0 : 1;
}

/* Delete top of the list and return it. */
static struct thread *
thread_trim_head (struct thread_list *list)
{
  if (!thread_empty (list))
    return thread_list_delete (list, list->head);
  return NULL;
}

/* Return remain time in second. */
unsigned long
thread_timer_remain_second (struct thread *thread)
{
  quagga_get_relative (NULL);
  
  if (thread->u.sands.tv_sec - relative_time.tv_sec > 0)
    return thread->u.sands.tv_sec - relative_time.tv_sec;
  else
    return 0;
}

/* Trim blankspace and "()"s */
static char *
strip_funcname (const char *funcname) 
{
  char buff[100];
  char tmp, *ret, *e, *b = buff;

  strncpy(buff, funcname, sizeof(buff));
  buff[ sizeof(buff) -1] = '\0';
  e = buff +strlen(buff) -1;

  /* Wont work for funcname ==  "Word (explanation)"  */

  while (*b == ' ' || *b == '(')
    ++b;
  while (*e == ' ' || *e == ')')
    --e;
  e++;

  tmp = *e;
  *e = '\0';
  ret  = XSTRDUP (MTYPE_THREAD_FUNCNAME, b);
  *e = tmp;

  return ret;
}

/* Get new thread.  */
static struct thread *
thread_get (struct thread_master *m, u_char type,
	    int (*func) (struct thread *), void *arg, const char* funcname)
{
  struct thread *thread;

  if (!thread_empty (&m->unuse))
    {
      thread = thread_trim_head (&m->unuse);
      if (thread->funcname)
        XFREE(MTYPE_THREAD_FUNCNAME, thread->funcname);
    }
  else
    {
      thread = XCALLOC (MTYPE_THREAD, sizeof (struct thread));
      m->alloc++;
    }
  thread->type = type;
  thread->add_type = type;
  thread->master = m;
  thread->func = func;
  thread->arg = arg;
  
  thread->funcname = strip_funcname(funcname);

  return thread;
}

/* Add new read thread. */
struct thread *
funcname_thread_add_read (struct thread_master *m, 
		 int (*func) (struct thread *), void *arg, int fd, const char* funcname)
{
  struct thread *thread;

  assert (m != NULL);

  if (FD_ISSET (fd, &m->readfd))
    {
      zlog (NULL, LOG_WARNING, "There is already read fd [%d]", fd);
      return NULL;
    }

  thread = thread_get (m, THREAD_READ, func, arg, funcname);
  FD_SET (fd, &m->readfd);
  thread->u.fd = fd;
  thread_list_add (&m->read, thread);

  return thread;
}

/* Add new write thread. */
struct thread *
funcname_thread_add_write (struct thread_master *m,
		 int (*func) (struct thread *), void *arg, int fd, const char* funcname)
{
  struct thread *thread;

  assert (m != NULL);

  if (FD_ISSET (fd, &m->writefd))
    {
      zlog (NULL, LOG_WARNING, "There is already write fd [%d]", fd);
      return NULL;
    }

  thread = thread_get (m, THREAD_WRITE, func, arg, funcname);
  FD_SET (fd, &m->writefd);
  thread->u.fd = fd;
  thread_list_add (&m->write, thread);

  return thread;
}

static struct thread *
funcname_thread_add_timer_timeval (struct thread_master *m,
                                   int (*func) (struct thread *), 
                                  int type,
                                  void *arg, 
                                  struct timeval *time_relative, 
                                  const char* funcname)
{
  struct thread *thread;
  struct thread_list *list;
  struct timeval alarm_time;
  struct thread *tt;

  assert (m != NULL);

  assert (type == THREAD_TIMER || type == THREAD_BACKGROUND);
  assert (time_relative);
  
  list = ((type == THREAD_TIMER) ? &m->timer : &m->background);
  thread = thread_get (m, type, func, arg, funcname);

  /* Do we need jitter here? */
  quagga_get_relative (NULL);
  alarm_time.tv_sec = relative_time.tv_sec + time_relative->tv_sec;
  alarm_time.tv_usec = relative_time.tv_usec + time_relative->tv_usec;
  thread->u.sands = timeval_adjust(alarm_time);

  /* Sort by timeval. */
  for (tt = list->head; tt; tt = tt->next)
    if (timeval_cmp (thread->u.sands, tt->u.sands) <= 0)
      break;

  if (tt)
    thread_list_add_before (list, tt, thread);
  else
    thread_list_add (list, thread);

  return thread;
}


/* Add timer event thread. */
struct thread *
funcname_thread_add_timer (struct thread_master *m,
		           int (*func) (struct thread *), 
		           void *arg, long timer, const char* funcname)
{
  struct timeval trel;

  assert (m != NULL);

  trel.tv_sec = timer;
  trel.tv_usec = 0;

  return funcname_thread_add_timer_timeval (m, func, THREAD_TIMER, arg, 
                                            &trel, funcname);
}

/* Add timer event thread with "millisecond" resolution */
struct thread *
funcname_thread_add_timer_msec (struct thread_master *m,
                                int (*func) (struct thread *), 
                                void *arg, long timer, const char* funcname)
{
  struct timeval trel;

  assert (m != NULL);

  trel.tv_sec = timer / 1000;
  trel.tv_usec = 1000*(timer % 1000);

  return funcname_thread_add_timer_timeval (m, func, THREAD_TIMER, 
                                            arg, &trel, funcname);
}

/* Add a background thread, with an optional millisec delay */
struct thread *
funcname_thread_add_background (struct thread_master *m,
                                int (*func) (struct thread *),
                                void *arg, long delay, 
                                const char *funcname)
{
  struct timeval trel;
  
  assert (m != NULL);
  
  if (delay)
    {
      trel.tv_sec = delay / 1000;
      trel.tv_usec = 1000*(delay % 1000);
    }
  else
    {
      trel.tv_sec = 0;
      trel.tv_usec = 0;
    }

  return funcname_thread_add_timer_timeval (m, func, THREAD_BACKGROUND,
                                            arg, &trel, funcname);
}

/* Add simple event thread. */
struct thread *
funcname_thread_add_event (struct thread_master *m,
		  int (*func) (struct thread *), void *arg, int val, const char* funcname)
{
  struct thread *thread;

  assert (m != NULL);

  thread = thread_get (m, THREAD_EVENT, func, arg, funcname);
  thread->u.val = val;
  thread_list_add (&m->event, thread);

  return thread;
}

/* Cancel thread from scheduler. */
void
thread_cancel (struct thread *thread)
{
  struct thread_list *list;
  
  switch (thread->type)
    {
    case THREAD_READ:
      assert (FD_ISSET (thread->u.fd, &thread->master->readfd));
      FD_CLR (thread->u.fd, &thread->master->readfd);
      list = &thread->master->read;
      break;
    case THREAD_WRITE:
      assert (FD_ISSET (thread->u.fd, &thread->master->writefd));
      FD_CLR (thread->u.fd, &thread->master->writefd);
      list = &thread->master->write;
      break;
    case THREAD_TIMER:
      list = &thread->master->timer;
      break;
    case THREAD_EVENT:
      list = &thread->master->event;
      break;
    case THREAD_READY:
      list = &thread->master->ready;
      break;
    case THREAD_BACKGROUND:
      list = &thread->master->background;
      break;
    default:
      return;
      break;
    }
  thread_list_delete (list, thread);
  thread->type = THREAD_UNUSED;
  thread_add_unuse (thread->master, thread);
}

/* Delete all events which has argument value arg. */
unsigned int
thread_cancel_event (struct thread_master *m, void *arg)
{
  unsigned int ret = 0;
  struct thread *thread;

  thread = m->event.head;
  while (thread)
    {
      struct thread *t;

      t = thread;
      thread = t->next;

      if (t->arg == arg)
        {
          ret++;
          thread_list_delete (&m->event, t);
          t->type = THREAD_UNUSED;
          thread_add_unuse (m, t);
        }
    }
  return ret;
}

static struct timeval *
thread_timer_wait (struct thread_list *tlist, struct timeval *timer_val)
{
  if (!thread_empty (tlist))
    {
      *timer_val = timeval_subtract (tlist->head->u.sands, relative_time);
      return timer_val;
    }
  return NULL;
}

static struct thread *
thread_run (struct thread_master *m, struct thread *thread,
	    struct thread *fetch)
{
  *fetch = *thread;
  thread->type = THREAD_UNUSED;
  thread->funcname = NULL;  /* thread_call will free fetch's copied pointer */
  thread_add_unuse (m, thread);
  return fetch;
}

static int
thread_process_fd (struct thread_list *list, fd_set *fdset, fd_set *mfdset)
{
  struct thread *thread;
  struct thread *next;
  int ready = 0;
  
  assert (list);
  
  for (thread = list->head; thread; thread = next)
    {
      next = thread->next;

      if (FD_ISSET (THREAD_FD (thread), fdset))
        {
          assert (FD_ISSET (THREAD_FD (thread), mfdset));
          FD_CLR(THREAD_FD (thread), mfdset);
          thread_list_delete (list, thread);
          thread_list_add (&thread->master->ready, thread);
          thread->type = THREAD_READY;
          ready++;
        }
    }
  return ready;
}

/* Add all timers that have popped to the ready list. */
static unsigned int
thread_timer_process (struct thread_list *list, struct timeval *timenow)
{
  struct thread *thread;
  unsigned int ready = 0;
  
  for (thread = list->head; thread; thread = thread->next)
    {
      if (timeval_cmp (*timenow, thread->u.sands) < 0)
        return ready;
      thread_list_delete (list, thread);
      thread->type = THREAD_READY;
      thread_list_add (&thread->master->ready, thread);
      ready++;
    }
  return ready;
}

/* Fetch next ready thread. */
struct thread *
thread_fetch (struct thread_master *m, struct thread *fetch)
{
  struct thread *thread;
  fd_set readfd;
  fd_set writefd;
  fd_set exceptfd;
  struct timeval timer_val;
  struct timeval timer_val_bg;
  struct timeval *timer_wait;
  struct timeval *timer_wait_bg;

  while (1)
    {
      int num = 0;
      
      /* Signals are highest priority */
      quagga_sigevent_process ();
       
      /* Normal event are the next highest priority.  */
      if ((thread = thread_trim_head (&m->event)) != NULL)
        return thread_run (m, thread, fetch);
      
      /* If there are any ready threads from previous scheduler runs,
       * process top of them.  
       */
      if ((thread = thread_trim_head (&m->ready)) != NULL)
        return thread_run (m, thread, fetch);
      
      /* Structure copy.  */
      readfd = m->readfd;
      writefd = m->writefd;
      exceptfd = m->exceptfd;
      
      /* Calculate select wait timer if nothing else to do */
      quagga_get_relative (NULL);
      timer_wait = thread_timer_wait (&m->timer, &timer_val);
      timer_wait_bg = thread_timer_wait (&m->background, &timer_val_bg);
      
      if (timer_wait_bg &&
	  (!timer_wait || (timeval_cmp (*timer_wait, *timer_wait_bg) > 0)))
	timer_wait = timer_wait_bg;
      
      num = select (FD_SETSIZE, &readfd, &writefd, &exceptfd, timer_wait);
      
      /* Signals should get quick treatment */
      if (num < 0)
        {
          if (errno == EINTR)
            continue; /* signal received - process it */
          zlog_warn ("select() error: %s", safe_strerror (errno));
            return NULL;
        }

      /* Check foreground timers.  Historically, they have had higher
         priority than I/O threads, so let's push them onto the ready
	 list in front of the I/O threads. */
      quagga_get_relative (NULL);
      thread_timer_process (&m->timer, &relative_time);
      
      /* Got IO, process it */
      if (num > 0)
        {
          /* Normal priority read thead. */
          thread_process_fd (&m->read, &readfd, &m->readfd);
          /* Write thead. */
          thread_process_fd (&m->write, &writefd, &m->writefd);
        }

#if 0
      /* If any threads were made ready above (I/O or foreground timer),
         perhaps we should avoid adding background timers to the ready
	 list at this time.  If this is code is uncommented, then background
	 timer threads will not run unless there is nothing else to do. */
      if ((thread = thread_trim_head (&m->ready)) != NULL)
        return thread_run (m, thread, fetch);
#endif

      /* Background timer/events, lowest priority */
      thread_timer_process (&m->background, &relative_time);
      
      if ((thread = thread_trim_head (&m->ready)) != NULL)
        return thread_run (m, thread, fetch);
    }
}

unsigned long
thread_consumed_time (RUSAGE_T *now, RUSAGE_T *start, unsigned long *cputime)
{
#ifdef HAVE_RUSAGE
  /* This is 'user + sys' time.  */
  *cputime = timeval_elapsed (now->cpu.ru_utime, start->cpu.ru_utime) +
	     timeval_elapsed (now->cpu.ru_stime, start->cpu.ru_stime);
#else
  *cputime = 0;
#endif /* HAVE_RUSAGE */
  return timeval_elapsed (now->real, start->real);
}

/* We should aim to yield after THREAD_YIELD_TIME_SLOT milliseconds. 
   Note: we are using real (wall clock) time for this calculation.
   It could be argued that CPU time may make more sense in certain
   contexts.  The things to consider are whether the thread may have
   blocked (in which case wall time increases, but CPU time does not),
   or whether the system is heavily loaded with other processes competing
   for CPU time.  On balance, wall clock time seems to make sense. 
   Plus it has the added benefit that gettimeofday should be faster
   than calling getrusage. */
int
thread_should_yield (struct thread *thread)
{
  quagga_get_relative (NULL);
  return (timeval_elapsed(relative_time, thread->ru.real) >
  	  THREAD_YIELD_TIME_SLOT);
}

void
thread_getrusage (RUSAGE_T *r)
{
  quagga_get_relative (NULL);
#ifdef HAVE_RUSAGE
  getrusage(RUSAGE_SELF, &(r->cpu));
#endif
  r->real = relative_time;

#ifdef HAVE_CLOCK_MONOTONIC
  /* quagga_get_relative() only updates recent_time if gettimeofday
   * based, not when using CLOCK_MONOTONIC. As we export recent_time
   * and guarantee to update it before threads are run...
   */
  quagga_gettimeofday(&recent_time);
#endif /* HAVE_CLOCK_MONOTONIC */
}

/* We check thread consumed time. If the system has getrusage, we'll
   use that to get in-depth stats on the performance of the thread in addition
   to wall clock time stats from gettimeofday. */
void
thread_call (struct thread *thread)
{
  unsigned long realtime, cputime;
  RUSAGE_T ru;

 /* Cache a pointer to the relevant cpu history thread, if the thread
  * does not have it yet.
  *
  * Callers submitting 'dummy threads' hence must take care that
  * thread->cpu is NULL
  */
  if (!thread->hist)
    {
      struct cpu_thread_history tmp;
      
      tmp.func = thread->func;
      tmp.funcname = thread->funcname;
      
      thread->hist = hash_get (cpu_record, &tmp, 
                    (void * (*) (void *))cpu_record_hash_alloc);
    }

  GETRUSAGE (&thread->ru);

  (*thread->func) (thread);

  GETRUSAGE (&ru);

  realtime = thread_consumed_time (&ru, &thread->ru, &cputime);
  thread->hist->real.total += realtime;
  if (thread->hist->real.max < realtime)
    thread->hist->real.max = realtime;
#ifdef HAVE_RUSAGE
  thread->hist->cpu.total += cputime;
  if (thread->hist->cpu.max < cputime)
    thread->hist->cpu.max = cputime;
#endif

  ++(thread->hist->total_calls);
  thread->hist->types |= (1 << thread->add_type);

#ifdef CONSUMED_TIME_CHECK
  if (realtime > CONSUMED_TIME_CHECK)
    {
      /*
       * We have a CPU Hog on our hands.
       * Whinge about it now, so we're aware this is yet another task
       * to fix.
       */
      zlog_warn ("SLOW THREAD: task %s (%lx) ran for %lums (cpu time %lums)",
		 thread->funcname,
		 (unsigned long) thread->func,
		 realtime/1000, cputime/1000);
    }
#endif /* CONSUMED_TIME_CHECK */

  XFREE (MTYPE_THREAD_FUNCNAME, thread->funcname);
}

/* Execute thread */
struct thread *
funcname_thread_execute (struct thread_master *m,
                int (*func)(struct thread *), 
                void *arg,
                int val,
		const char* funcname)
{
  struct thread dummy; 

  memset (&dummy, 0, sizeof (struct thread));

  dummy.type = THREAD_EVENT;
  dummy.add_type = THREAD_EXECUTE;
  dummy.master = NULL;
  dummy.func = func;
  dummy.arg = arg;
  dummy.u.val = val;
  dummy.funcname = strip_funcname (funcname);
  thread_call (&dummy);

  XFREE (MTYPE_THREAD_FUNCNAME, dummy.funcname);

  return NULL;
}
