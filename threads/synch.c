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
// lock_acquire 에서 실행 됨.
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	// seam->value가 0이면, 공유자원 누군가 사용하고 있는 상황. -> waiter에 넣어줘야함.
	while (sema->value == 0) {
		// list_push_back (&sema->waiters, &thread_current ()->elem);
		list_insert_ordered (&sema->waiters, &thread_current ()->elem, cmp_priority, NULL);
		thread_block ();
	}
	// 공유자원 사용여부와 관계없이 value는 -1. (공유자원 쓰는 스레드 끝나면 +1씩 해서 0되면 다시 사용가능상태)
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
// lock_release에서 실행
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)){
		list_sort (&sema->waiters, cmp_priority, NULL);
		thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
	}
	sema->value++;

	// 현재 쓰레드 priority < ready_list 맨 앞 쓰레드 priority 이면, 현재 쓰레드 thread_yield();
	priority_preemption();

	intr_set_level (old_level);
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

	struct thread *curr = thread_current();

	// lock의 holder가 존재하여 wait을 하게 되면
	if (lock->holder != NULL){
		// wait을 하게될 lock 자료구조 포인터 저장
		curr->wait_on_lock = lock;
		
		if (curr->priority > lock->holder->origin_priority){
			// lock의 현재 holder의 대기자 list에 추가
			if (list_empty(&lock->semaphore.waiters)) // waiters 처음이면
				list_push_back(&lock->holder->donations, &curr->d_elem);
			// lock에 새로들어온 스레드값이 waiters의 가장 큰값이면 donation 업데이트
			else if (list_entry(list_front(&lock->semaphore.waiters), struct thread, elem)->priority < curr->priority){ 
				struct list_elem *e;
				for (e = list_begin(&lock->holder->donations); e != list_end(&lock->holder->donations); e = list_next(e)){
					struct thread *t = list_entry(e, struct thread, d_elem);
					if (lock == t->wait_on_lock){ // lock이 wait_on_lock인 경우
						list_remove(&t->d_elem);
						list_insert_ordered(&t->donations, &curr->d_elem, donation_sort, NULL);
						break;
					} 
				}
			}
			// priority donation을 수행하는 코드 추가
			donate_priority();
		}	
	}
	// lock획득 후 lock의 holder 갱신
	sema_down (&lock->semaphore);
	lock->holder = curr;
	curr->wait_on_lock = NULL;
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

	// donation list 에서 스레드 제거
	remove_with_lock(lock);

	// 우선순위 다시 계산 필요
	refresh_priority();

	lock->holder = NULL;
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
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
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
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	// cond->waiters에 priority 순서대로 넣어줌 
	list_insert_ordered (&cond->waiters, &waiter.elem, cmp_sem_priority, NULL);
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

	if (!list_empty (&cond->waiters)){
		list_sort (&cond->waiters, cmp_sem_priority, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);
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

bool
cmp_sem_priority(const struct list_elem *a, const struct list_elem *b, void *aux) {
    // cond 에서 seamphore_elem 가져옴 
	struct semaphore_elem *sa = list_entry(a, struct semaphore_elem, elem);
    struct semaphore_elem *sb = list_entry(b, struct semaphore_elem, elem);

	// semaphore_elem 으로 waiters 접근 list 다 가져오기 
    struct list *la = &sa->semaphore.waiters;
    struct list *lb = &sb->semaphore.waiters;
    
	// list_begin() : list의 첫번째 반환
	// thread로 접근하여 priority 값 비교
    struct thread *ta = list_entry(list_begin(la), struct thread, elem);
    struct thread *tb = list_entry(list_begin(lb), struct thread, elem);
    
	return ta->priority > tb->priority;
 }

// lock을 가진 스레드에 priority donation을 수행하는 함수
void
donate_priority(void){
	struct thread *curr = thread_current();
	struct thread *holder; 

	//현재 스레드가 priority가 높으므로 donation요청온것
	int priority = curr->priority;

	// donation 수행 
	while(curr->wait_on_lock != NULL){
		holder = curr->wait_on_lock->holder;
		holder->priority = priority;
		curr = holder;
	}
}


bool donation_sort(const struct list_elem *a, const struct list_elem *b, void *aux) {
    struct thread *da = list_entry(a, struct thread, d_elem);
    struct thread *db = list_entry(b, struct thread, d_elem);

    return da->priority > db->priority;
}

// lock을 해지했을때, waiters 리스트에서 해당 엔트리 삭제를 위한 함수
void remove_with_lock(struct lock *lock){
	struct list *donation_list = &(thread_current()->donations);
	if(list_empty(donation_list))
		return;
	
	struct list_elem *hd_d_elem = list_front(donation_list);
	struct thread *donation_thread;

	while(hd_d_elem != list_tail(donation_list)){
		donation_thread = list_entry(hd_d_elem, struct thread, d_elem);
		if(donation_thread->wait_on_lock == lock){ // 삭제해야될 엔트리 이면
			list_remove(&donation_thread->d_elem);
		}
		hd_d_elem = list_next(hd_d_elem);
	}

}

// 쓰레드 priority 변경되면, donation 고려하여 우선순위 다시 결정
void refresh_priority(void){
	struct thread *curr = thread_current();
	struct list *donations = &(curr->donations);
	if(list_empty(donations)){
		// 우선순위 donate 받기전의 origin_priority로 변경
		curr->priority = curr->origin_priority;
		return;
	}
	// donation중 priority가 가장 높은 값
	list_sort(donations, donation_sort, NULL);
	curr->priority = list_entry(list_front(donations), struct thread, d_elem)->priority;
}