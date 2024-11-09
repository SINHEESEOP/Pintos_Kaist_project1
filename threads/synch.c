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

/* 세마포어 SEMA를 VALUE로 초기화합니다. 세마포어는
   음이 아닌 정수와 이를 조작하기 위한 두 개의 원자적 연산자로 구성됩니다:

   - down 또는 "P": 값이 양수가 될 때까지 대기한 후
   값을 감소시킵니다.

   - up 또는 "V": 값을 증가시키고 (대기 중인 스레드가 있다면
   그 중 하나를 깨웁니다). */
void sema_init(struct semaphore *sema, unsigned value)
{
	// sema가 NULL이 아닌지 확인
	ASSERT(sema != NULL);

	// 세마포어를 기다리는 스레드들의 리스트 초기화
	sema->value = value;
	list_init (&sema->entry);


/* 세마포어에 대한 Down 또는 "P" 연산입니다. SEMA의 값이 양수가 될 때까지
   대기한 후 원자적으로 값을 감소시킵니다.

   이 함수는 sleep 상태가 될 수 있으므로 인터럽트 핸들러 내에서 호출되어서는 안 됩니다.
   인터럽트가 비활성화된 상태에서 이 함수를 호출할 수 있지만, 만약 sleep 상태가 되면
   다음에 스케줄링되는 스레드가 인터럽트를 다시 활성화할 것입니다.
   이것이 sema_down 함수입니다. */
void sema_down(struct semaphore *sema) {
	// 인터럽트 상태를 저장할 변수 선언
	enum intr_level old_level;

	// sema가 NULL이 아닌지 확인
	ASSERT(sema != NULL);
	// 인터럽트 핸들러 내에서 호출되지 않았는지 확인
	ASSERT(!intr_context());

	// 인터럽트를 비활성화하고 이전 상태를 저장
	old_level = intr_disable();

	// 세마포어 값이 0이면 대기
	// while (sema->value == 0) {
	// 	// 현재 스레드를 대기 리스트 끝에 추가
	// 	list_push_back(&sema->waiters, &thread_current()->elem);
	// 	// 현재 스레드를 블록 상태로 변경
	// 	thread_block();
	// }

	// 세마포어 값이 0이면 대기
	while (sema->value == 0) {
   // list_push_back 대신 우선순위 순서로 삽입
		list_insert_ordered(&sema->entry, 
                        &thread_current()->elem, 
                        cmp_priority, NULL);
		// list_push_back (&sema->entry, &thread_current ()->elem);
		thread_block ();
	}

	// 세마포어 값을 감소
	sema->value--;

	// 이전 인터럽트 상태로 복원
	intr_set_level(old_level);
}

/* 세마포어가 이미 0이 아닌 경우에만 세마포어에 대한 Down 또는 "P" 연산을 수행합니다.
   세마포어가 감소되면 true를 반환하고, 그렇지 않으면 false를 반환합니다.

   이 함수는 인터럽트 핸들러에서 호출될 수 있습니다. */
bool sema_try_down(struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level(old_level);

	return success;
}

/* 세마포어에 대한 Up 또는 "V" 연산을 수행합니다.
   세마포어의 값을 증가시키고, 대기 중인 스레드가 있다면 그 중 하나를 깨웁니다.

   이 함수는 인터럽트 핸들러 내에서 호출될 수 있습니다. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

    old_level = intr_disable();
    if (!list_empty(&sema->entry)) {
        list_sort(&sema->entry, cmp_priority, NULL);
        thread_unblock(list_entry(list_pop_front(&sema->entry), struct thread, elem));
    }

    sema->value++;
    intr_set_level(old_level);

	thread_preemption();
}


static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void sema_self_test(void)
{
	struct semaphore sema[2];
	int i;

	printf("Testing semaphores...");
	sema_init(&sema[0], 0);
	sema_init(&sema[1], 0);
	thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up(&sema[0]);
		sema_down(&sema[1]);
	}
	printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void *sema_)
{
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down(&sema[0]);
		sema_up(&sema[1]);
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
void lock_init(struct lock *lock)
{
	ASSERT(lock != NULL);

	lock->holder = NULL;
	sema_init(&lock->semaphore, 1);
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

    struct thread *cur = thread_current();

    if (lock->holder != NULL) {
        cur->waiting_lock = lock;
        donate_priority();
    }

    sema_down (&lock->semaphore);
    cur->waiting_lock = NULL;
    lock->holder = cur;
}

/* 우선순위 기부 로직 */
void donate_priority(void) {
    struct thread *cur = thread_current();
    struct thread *t = cur;

    while (t->waiting_lock != NULL) {
        struct thread *holder = t->waiting_lock->holder;
        if (holder == NULL || holder->priority >= t->priority) break;
        
        holder->priority = t->priority;
        if (list_empty(&holder->donations) || 
            list_entry(list_front(&holder->donations), struct thread, donation_elem) != t) {
            list_insert_ordered(&holder->donations, &t->donation_elem, cmp_priority, NULL);
        }

        t = holder;
    }
}


/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire(struct lock *lock)
{
	bool success;

	ASSERT(lock != NULL);
	ASSERT(!lock_held_by_current_thread(lock));

	success = sema_try_down(&lock->semaphore);
	if (success)
		lock->holder = thread_current();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void lock_release(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock));

    struct thread *cur = thread_current();
    
    struct list_elem *e = list_begin(&cur->donations);
    while (e != list_end(&cur->donations)) {
        struct thread *donor = list_entry(e, struct thread, donation_elem);
        struct list_elem *next = list_next(e);
        if (donor->waiting_lock == lock) {
            list_remove(e);
        }
        e = next;
    }

    cur->priority = cur->original_pri;
    if (!list_empty(&cur->donations)) {
        struct thread *highest_donor = list_entry(list_front(&cur->donations), struct thread, donation_elem);
        if (highest_donor->priority > cur->priority)
            cur->priority = highest_donor->priority;
    }

    lock->holder = NULL;
    sema_up(&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool lock_held_by_current_thread(const struct lock *lock)
{
	ASSERT(lock != NULL);

	return lock->holder == thread_current();
}

/* 리스트에서 하나의 세마포어를 나타내는 구조체입니다.
   조건 변수의 대기자 리스트에서 사용됩니다.
   각 대기자는 자신만의 세마포어를 가지고 있어서
   개별적으로 블록/깨우기가 가능합니다. */
struct semaphore_elem {
	struct list_elem elem;		/* 리스트 요소. 대기자 리스트에서 이 구조체를 연결하는데 사용됨 */
	struct semaphore semaphore;	/* 이 대기자의 세마포어. 스레드를 블록하고 깨우는데 사용됨 */
	struct thread *thread;  // 우선순위 비교를 위한 현재 스레드 포인터 추가
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void cond_init(struct condition *cond)
{
	ASSERT(cond != NULL);

	list_init(&cond->waiters);
}

/* 조건 변수(COND)에 대해 대기하고 LOCK을 원자적으로 해제합니다.
   다른 코드에 의해 COND가 시그널되면, 반환하기 전에 LOCK을 다시 획득합니다.
   이 함수를 호출하기 전에 반드시 LOCK을 보유하고 있어야 합니다.

   이 함수가 구현하는 모니터는 "Hoare" 방식이 아닌 "Mesa" 방식입니다.
   즉, 시그널을 보내고 받는 것이 원자적 연산이 아닙니다.
   따라서 일반적으로 호출자는 대기가 완료된 후 조건을 다시 확인하고,
   필요한 경우 다시 대기해야 합니다.

   하나의 조건 변수는 단 하나의 락과만 연관되지만,
   하나의 락은 여러 개의 조건 변수와 연관될 수 있습니다.
   즉, 락에서 조건 변수로의 일대다 매핑이 가능합니다.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */

bool
cmp_priority_s(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
    struct semaphore_elem *sema_a = list_entry(a, struct semaphore_elem, elem);
    struct semaphore_elem *sema_b = list_entry(b, struct semaphore_elem, elem);

    struct list *entry_a = &(sema_a->semaphore.entry);
    struct list *entry_b = &(sema_b->semaphore.entry);

    struct thread *root_a = list_entry(list_begin(entry_a), struct thread, elem);
    struct thread *root_b = list_entry(list_begin(entry_b), struct thread, elem);

    return root_a->priority > root_b->priority;
}

void 
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_insert_ordered(&cond->waiters, &waiter.elem, cmp_priority_s, NULL);
	// list_push_back (&cond->waiters, &waiter.elem);
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
		list_sort(&cond->waiters, cmp_priority_s, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);
	}
}

/* COND(조건 변수)에서 대기 중인 모든 스레드를 깨웁니다.
   이 함수를 호출하기 전에 반드시 LOCK을 보유하고 있어야 합니다.

   인터럽트 핸들러는 락을 획득할 수 없으므로,
   인터럽트 핸들러 내에서 조건 변수에 시그널을 보내는 것은 의미가 없습니다.
   
   동작 과정:
   1. 매개변수의 유효성을 검사합니다.
   2. 대기자 리스트가 빌 때까지 반복하여 모든 대기 스레드를 깨웁니다.
   3. 각 스레드를 깨울 때는 cond_signal 함수를 사용합니다. */
// void cond_broadcast(struct condition *cond, struct lock *lock)
// {
// 	// 매개변수 유효성 검사
// 	ASSERT(cond != NULL);		// 조건 변수가 NULL이 아닌지 확인
// 	ASSERT(lock != NULL);		// 락이 NULL이 아닌지 확인
// 	// 대기자 리스트가 빌 때까지 모든 스레드를 깨움
// 	while (!list_empty(&cond->waiters))
// 		cond_signal(cond, lock);	// cond_signal을 호출하여 한 번에 하나씩 깨움
// }

/* 조건 변수 브로드캐스트 - 모든 대기자에게 신호를 전송합니다.
   condition variable broadcast - sends signal to all waiters

   매개변수:
   - cond: 시그널을 보낼 조건 변수
   - lock: 조건 변수와 연관된 락

   동작 과정:
   1. 매개변수의 유효성을 검사합니다.
   2. 대기자 리스트가 빌 때까지 반복하여 모든 대기 스레드를 깨웁니다.
   3. 각 스레드를 깨울 때는 cond_signal 함수를 사용합니다. */
void 
cond_broadcast (struct condition *cond, struct lock *lock) {

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	list_sort(&cond->waiters, cmp_priority, NULL); 		// signal 전 우선순위에 따라 waiters 정렬
	while (!list_empty (&cond->waiters)) {
		cond_signal (cond, lock);
	}
}
