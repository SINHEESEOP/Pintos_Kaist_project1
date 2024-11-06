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

	// 세마포어의 초기값을 설정
	sema->value = value;

	// 세마포어를 기다리는 스레드들의 리스트 초기화
	list_init(&sema->waiters);
}

/* 세마포어에 대한 Down 또는 "P" 연산입니다. SEMA의 값이 양수가 될 때까지
   대기한 후 원자적으로 값을 감소시킵니다.

   이 함수는 sleep 상태가 될 수 있으므로 인터럽트 핸들러 내에서 호출되어서는 안 됩니다.
   인터럽트가 비활성화된 상태에서 이 함수를 호출할 수 있지만, 만약 sleep 상태가 되면
   다음에 스케줄링되는 스레드가 인터럽트를 다시 활성화할 것입니다.
   이것이 sema_down 함수입니다. */
void sema_down(struct semaphore *sema)
{
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
		list_insert_ordered(&sema->waiters, 
							&thread_current()->elem,
							cmp_priority, NULL);

		thread_block();
	}

	// 세마포어 값을 감소
	sema->value--;

	// 이전 인터럽트 상태로 복원
	intr_set_level(old_level);
}

/* 세마포어가 이미 0이 아닌 경우에만 세마포어에 대한 Down 또는 "P" 연산을 수행합니다.
   세마포어가 감소되면 true를 반환하고, 그렇지 않으면 false를 반환합니다.

   이 함수는 인터럽트 핸들러에서 호출될 수 있습니다. */
bool sema_try_down(struct semaphore *sema)
{
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
sema_up(struct semaphore *sema) {
    // 인터럽트 상태를 저장할 변수 선언
    enum intr_level old_level;
    // 대기 리스트에서 가장 높은 우선순위를 가진 스레드를 저장할 포인터
    struct thread *max_priority_thread = NULL;

    // sema가 NULL이 아닌지 확인
    ASSERT(sema != NULL);

    // 인터럽트를 비활성화하고 이전 상태를 저장
    old_level = intr_disable();

    // 세마포어의 대기 리스트가 비어있지 않다면
    if (!list_empty(&sema->waiters)) {
        // 대기 리스트에서 가장 높은 우선순위를 가진 스레드를 찾음
        max_priority_thread = list_entry(
            list_max(&sema->waiters, cmp_priority, NULL),
            struct thread, elem);

        // 찾은 스레드를 대기 리스트에서 제거
        list_remove(&max_priority_thread->elem);
        // 해당 스레드를 블록 상태에서 깨움(실행 가능 상태로 변경)
        thread_unblock(max_priority_thread);
    }

    // 세마포어 값을 1 증가
    sema->value++;

    // 선점 스케줄링 처리
    // 깨워진 스레드의 우선순위가 현재 실행 중인 스레드보다 높다면
    if (max_priority_thread != NULL &&
        max_priority_thread->priority > thread_current()->priority)
        thread_yield();  // CPU를 양보하여 높은 우선순위 스레드가 실행되도록 함

    // 이전 인터럽트 상태로 복원
    intr_set_level(old_level);
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
void lock_acquire(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));

	sema_down(&lock->semaphore);
	lock->holder = thread_current();
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

/* One semaphore in a list. */
struct semaphore_elem
{
	struct list_elem elem;		/* List element. */
	struct semaphore semaphore; /* This semaphore. */
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

   이 함수는 sleep 상태가 될 수 있으므로 인터럽트 핸들러 내에서 호출될 수 없습니다.
   인터럽트가 비활성화된 상태에서 이 함수를 호출할 수 있지만,
   sleep이 필요한 경우 인터럽트가 다시 활성화됩니다. */
void 
cond_wait(struct condition *cond, struct lock *lock) {
    // 대기 중인 스레드를 관리하기 위한 세마포어 요소 구조체
    struct semaphore_elem waiter;

    // 매개변수 유효성 검사
    ASSERT(cond != NULL);                            // 조건 변수가 NULL이 아닌지 확인
    ASSERT(lock != NULL);                            // 락이 NULL이 아닌지 확인
    ASSERT(!intr_context());                         // 인터럽트 컨텍스트가 아닌지 확인
    ASSERT(lock_held_by_current_thread(lock));       // 현재 스레드가 락을 보유하고 있는지 확인

    // 세마포어 초기화 (초기값 0으로 설정)
    sema_init(&waiter.semaphore, 0);

    // 대기자 리스트에 현재 스레드를 우선순위 순서로 삽입
    // - 기존의 list_push_back 대신 우선순위 기반 정렬 삽입 사용
    // - 우선순위가 높은 스레드가 먼저 깨어날 수 있도록 함
    list_insert_ordered(&cond->waiters, &waiter.elem, cmp_priority, NULL);
    
    // 락을 해제하고 조건 변수에서 대기
    lock_release(lock);                  // 다른 스레드가 락을 획득할 수 있도록 해제
    sema_down(&waiter.semaphore);        // 세마포어에서 대기 (블록)
    lock_acquire(lock);                  // 깨어난 후 락을 다시 획득
}

/* COND(조건 변수)에서 대기 중인 스레드가 있는 경우,
   이 함수는 그 중 하나에게 깨어나라는 시그널을 보냅니다.
   이 함수를 호출하기 전에 반드시 LOCK을 보유하고 있어야 합니다.

   인터럽트 핸들러는 락을 획득할 수 없으므로,
   인터럽트 핸들러 내에서 조건 변수에 시그널을 보내는 것은 의미가 없습니다. */
void 
cond_signal(struct condition *cond, struct lock *lock UNUSED)  {
    /* 조건 변수에서 대기 중인 스레드 하나를 깨우는 함수입니다.
       매개변수:
       - cond: 시그널을 보낼 조건 변수
       - lock: 조건 변수와 연관된 락 (UNUSED 표시됨)
       
       동작 과정:
       1. 매개변수와 실행 컨텍스트의 유효성을 검사합니다.
       2. 대기자가 있는 경우, 우선순위가 가장 높은 스레드를 깨웁니다.
       3. 깨울 때는 해당 스레드의 세마포어를 up 연산합니다. */

    // 매개변수 유효성 검사
    ASSERT(cond != NULL);                            // 조건 변수가 NULL이 아닌지 확인
    ASSERT(lock != NULL);                            // 락이 NULL이 아닌지 확인
    ASSERT(!intr_context());                         // 인터럽트 컨텍스트가 아닌지 확인
    ASSERT(lock_held_by_current_thread(lock));       // 현재 스레드가 락을 보유하고 있는지 확인

    // 대기 중인 스레드가 있는 경우
    if (!list_empty(&cond->waiters)) {
        // 대기자 리스트를 우선순위 순으로 정렬
        list_sort(&cond->waiters, cmp_priority, NULL);
        
        // 가장 높은 우선순위의 대기 스레드를 깨움:
        // 1. list_pop_front로 첫 번째(최고 우선순위) 대기자를 제거
        // 2. list_entry로 세마포어 요소의 주소를 얻음
        // 3. sema_up으로 해당 스레드의 세마포어를 증가시켜 깨움
        sema_up(&list_entry(list_pop_front(&cond->waiters),
                            struct semaphore_elem, elem)
                    ->semaphore);
    }
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);

	while (!list_empty(&cond->waiters))
		cond_signal(cond, lock);
}
