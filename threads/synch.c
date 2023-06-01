/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		// list_push_back (&sema->waiters, &thread_current ()->elem);
		list_insert_ordered(&sema->waiters, &thread_current ()->elem, cmp_priority, NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)) {
		// wait list를 정렬한다.
		list_sort(&sema->waiters, cmp_priority, NULL);
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	}
	sema->value++;
	intr_set_level (old_level);
	
	thread_preempt();
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	// holder가 존재하여 wait 하게 되면
	if (lock->holder != NULL) {
		struct thread *curr_t = thread_current();
		// lock 주소 저장
		curr_t->wait_on_lock = lock;
		// lock을 들고 있는 스레드의 donation_list에 현재 스레드 삽입
		list_insert_ordered(&lock->holder->donation_list, &curr_t->d_elem, cmp_don_priority, NULL);
		// 현재 스레드의 우선순위가 더 높다면 우선순위 기부
		donate_priority();
	}

	// sempaphore의 value가 0이라면 semaphore의 waiters list에 들어가게 된다.
	sema_down (&lock->semaphore);

	// lock 획득 후 lock holder의 갱신
	lock->holder = thread_current ();
	// lock을 얻은 스레드는 기다리는 lock이 없다.
	thread_current()->wait_on_lock = NULL;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));
	// 락을 해제할 스레드의 donation list 갱신
	remove_with_lock(lock);
	// priority donation을 받았을 수 있으므로 원래의 priority로 초기화(refresh_priority())
	refresh_priority();

	lock->holder = NULL;
	// semaphore의 value를 1 올리고,
	// semaphore의 waiters list에서 가장 우선순위가 높은 스레드를 ready_list에 넣는다. 
	// 이후 스레드의 우선순위를 비교해서 더 높은 스레드를 실행
	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
/* 조건 변수 COND를 초기화합니다. 조건 변수를 사용하면 한 코드 조각이 조건을 신호로 보내고,
   협력하는 코드가 해당 신호를 수신하여 작동할 수 있습니다. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

bool cmp_sem_priority(
	const struct list_elem *a,
	const struct list_elem *b,
	void *aux
) {
    struct semaphore_elem *sa = list_entry(a, struct semaphore_elem, elem);
    struct semaphore_elem *sb = list_entry(b, struct semaphore_elem, elem);
    
	struct list *la = &sa->semaphore.waiters;
    struct list *lb = &sb->semaphore.waiters;
    
	//list_begin() : list의 첫번째 반환
    struct thread *ta = list_entry(list_begin(la), struct thread, elem);
    struct thread *tb = list_entry(list_begin(lb), struct thread, elem);
    
	return ta->priority > tb->priority;
}

bool cmp_don_priority(
	const struct list_elem *a,
	const struct list_elem *b,
	void *aux
) {
	struct thread *t1 = list_entry(a, struct thread, d_elem);
	struct thread *t2 = list_entry(b, struct thread, d_elem);

	return t1->priority > t2->priority;
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
/* 이 함수는 원자적으로 LOCK을 해제하고, 다른 코드 조각에서 COND가 신호를 보낼 때까지 
   대기합니다. COND가 신호를 보낸 후, 이 함수는 반환하기 전에 LOCK을 재획득합니다.
   이 함수를 호출하기 전에 LOCK이 보유되어야 합니다.

   이 함수에 의해 구현된 모니터는 "Hoare" 스타일이 아닌 "Mesa" 스타일입니다.
   즉, 신호를 보내는 것과 받는 것이 원자적인 연산이 아닙니다.
   따라서 대기가 완료된 후에 일반적으로 호출자는 조건을 다시 확인해야 하며,
   필요한 경우 다시 대기해야 합니다.

   주어진 조건 변수는 단일 lock에만 연관되지만,
   하나의 lock은 여러 개의 조건 변수와 연관될 수 있습니다.
   즉, lock에서 조건 변수로의 매핑은 일대다 관계입니다.

   이 함수는 잠들 수 있으므로 인터럽트 핸들러 내에서 호출해서는 안 됩니다.
   이 함수는 인터럽트를 비활성화한 상태에서 호출할 수 있지만,
   잠들어야 한다면 인터럽트가 다시 켜집니다. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	// list_push_back (&cond->waiters, &waiter.elem);
	// semaphore_elem
	list_insert_ordered(&cond->waiters, &waiter.elem, cmp_sem_priority, NULL);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)) {
		// 대기 중 우선순위 변경 가능성이 있어 재 정렬
		list_sort(&cond->waiters, cmp_sem_priority, NULL);
		// 여기 코드를 좀 분석
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

// Priority donation
void donate_priority(void) {
	// 현재 쓰레드가 기다리고 있는 lock과 연결된 모든 쓰레드들을 순회하며
	// 현재 쓰레드의 우선순위를 lock을 보유하고 있는 쓰레드에게 기부
		// 현재 쓰레드가 기다리고 있는 락의 holder -> holder가 기다리고 있는
		// lock의 holder
	// nested depth는 8로 제한

	struct thread *curr_t = thread_current();
	struct lock *lock = curr_t->wait_on_lock;

	for (int i = 0; i < 8; i++) {
		if (!lock || !lock->holder) { // null check가 아닌가?
			return;
		}
		if (lock->holder->priority >= curr_t->priority) {
			return;
		}

		lock->holder->priority = curr_t->priority;
		curr_t = curr_t->wait_on_lock->holder;
		lock = curr_t->wait_on_lock;
	}
}

// 락을 해제하는 스레드의 donation list에서 해당 락을 대기하는 스레드들을 제거한다.
void remove_with_lock(struct lock *lock) {
	struct list_elem *li;
	struct thread *t;

	if (!list_empty(&lock->holder->donation_list)) {
		li = list_front(&lock->holder->donation_list);
	}

	while (!list_empty(&lock->holder->donation_list)) {
		if (li == list_tail(&lock->holder->donation_list)) {
			break;
		}

		t = list_entry(li, struct thread, d_elem);

		if (t->wait_on_lock == lock) {
			list_remove(li);
		}
		li = list_next(li);
	}

}

// 스레드의 우선순위가 변경 됐을 때, donation을 고려하여 우선순위를 다시 결정하는 함수
// 현재 스레드의 우선순위를 기부 받기 전의 우선 순위로 변경
// 현재 쓰레드의 waiters리스트에서 가장 높은 우선순위를
// 현재 쓰레드의 우선순위와 비교 후 우선순위 결정
void refresh_priority(void) {
	// 우선순위 복원
	struct thread *curr_t = thread_current();
	// 주소 값을 넘겨야 하는거얐어..?
	struct list *donation_list = &(curr_t->donation_list);
	curr_t->priority = curr_t->original_priority;

	if (!list_empty(donation_list)) {
		// sort 해야 하는지 확인하기
		list_sort(donation_list, cmp_don_priority, NULL);
		
		struct thread *first_thread = list_entry(list_front(donation_list), struct thread, d_elem);
		
		if (curr_t->priority < first_thread->priority) {
			curr_t->priority = first_thread->priority;
		}
	}
}