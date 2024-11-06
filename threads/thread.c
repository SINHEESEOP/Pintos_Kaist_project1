#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;
static struct list sleep_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);
static bool cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the global thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&sleep_list);
	list_init (&destruction_req);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* 인터럽트를 활성화하여 선점형 스레드 스케줄링을 시작합니다.
   또한 idle 스레드를 생성합니다. */
void
thread_start (void) {
    /* idle 스레드 생성을 위한 준비 */
    struct semaphore idle_started;    // idle 스레드 초기화 완료를 동기화할 세마포어
    sema_init (&idle_started, 0);     // 세마포어를 0으로 초기화 (처음에는 대기 상태)
    thread_create ("idle", PRI_MIN,   // 최소 우선순위로 idle 스레드 생성
                	idle, &idle_started); // idle 함수를 실행하고 세마포어를 전달

    /* 선점형 스레드 스케줄링 시작 */
    intr_enable ();                   // 인터럽트를 활성화하여 스케줄링 시작

    /* idle 스레드가 완전히 초기화될 때까지 대기 */
    sema_down (&idle_started);        // idle 스레드 초기화가 완료될 때까지 블록
}

/* 타이머 인터럽트 핸들러에 의해 각 타이머 틱마다 호출됩니다.
   따라서 이 함수는 외부 인터럽트 컨텍스트에서 실행됩니다. */
void
thread_tick (void) {
	struct thread *t = thread_current ();    // 현재 실행 중인 스레드 구조체 포인터 가져오기

	/* 통계 업데이트 */
	if (t == idle_thread)                    // idle 스레드인 경우
		idle_ticks++;                        // idle 틱 카운트 증가
#ifdef USERPROG
	else if (t->pml4 != NULL)               // 사용자 프로그램 스레드인 경우
		user_ticks++;                        // 사용자 틱 카운트 증가
#endif
	else
		kernel_ticks++;                      // 커널 스레드인 경우 커널 틱 카운트 증가

	/* 선점 강제 실행 */
	if (++thread_ticks >= TIME_SLICE)        // 현재 스레드의 실행 시간이 타임 슬라이스를 초과하면
		intr_yield_on_return ();             // 인터럽트 처리 후 다른 스레드에게 CPU 양보
}

/* 스레드 통계를 출력합니다. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* 주어진 초기 우선순위(PRIORITY)로 NAME이라는 이름의 새 커널 스레드를 생성하고,
   FUNCTION을 실행하며 AUX를 인자로 전달합니다.
   생성된 스레드를 실행 대기열에 추가하고 새 스레드의 식별자를 반환합니다.
   생성에 실패하면 TID_ERROR를 반환합니다.

   thread_start()가 호출된 경우, 새 스레드는 thread_create()가 반환되기 전에
   스케줄될 수 있습니다. thread_create()가 반환되기 전에 종료될 수도 있습니다.
   반대로 원래 스레드가 새 스레드가 스케줄되기 전에 임의의 시간 동안 실행될 수 있습니다.
   순서를 보장해야 하는 경우 세마포어나 다른 동기화 방식을 사용하세요.

   제공된 코드는 새 스레드의 'priority' 멤버를 PRIORITY로 설정하지만,
   실제 우선순위 스케줄링은 구현되지 않았습니다.
   우선순위 스케줄링은 Problem 1-3의 목표입니다. */
tid_t
thread_create (const char *name, int priority, 
				thread_func *function, void *aux) {
	struct thread *t;        // 새로 생성할 스레드 구조체 포인터
	tid_t tid;              // 스레드 ID를 저장할 변수

	ASSERT (function != NULL);  // function이 NULL이 아닌지 확인

	/* 스레드를 위한 메모리 할당 */
	t = palloc_get_page (PAL_ZERO);  // 페이지 크기(4KB)의 메모리를 0으로 초기화하여 할당
	if (t == NULL)                   // 메모리 할당 실패 시
		return TID_ERROR;            // 에러 반환

	/* 스레드 초기화 */
	init_thread (t, name, priority);  // 스레드의 기본 정보 초기화
	tid = t->tid = allocate_tid ();   // 새로운 스레드 ID 할당

	t->created_tick = timer_ticks();  // 스레드 생성 시간 기록

	/* 스레드가 스케줄되면 실행할 커널 스레드 설정
	 * 참고) rdi는 첫 번째 인자, rsi는 두 번째 인자 */
	t->tf.rip = (uintptr_t) kernel_thread;  // 실행할 함수의 시작 주소
	t->tf.R.rdi = (uint64_t) function;      // 첫 번째 인자로 전달할 함수
	t->tf.R.rsi = (uint64_t) aux;           // 두 번째 인자로 전달할 보조 데이터
	t->tf.ds = SEL_KDSEG;                   // 데이터 세그먼트 설정
	t->tf.es = SEL_KDSEG;                   // 추가 세그먼트 설정
	t->tf.ss = SEL_KDSEG;                   // 스택 세그먼트 설정
	t->tf.cs = SEL_KCSEG;                   // 코드 세그먼트 설정
	t->tf.eflags = FLAG_IF;                 // 인터럽트 플래그 활성화
	
	/* 실행 대기열에 추가 */
	thread_unblock (t);                     // 스레드를 실행 가능 상태로 변경

	// 생성된 스레드의 우선순위가 현재 실행 중인 스레드보다 높다면
	if (priority > thread_current()->priority)
		thread_yield();

	return tid;                             // 생성된 스레드의 ID 반환
}

/* 현재 실행 중인 스레드를 재우는(sleep) 함수입니다.
   thread_unblock()에 의해 깨워질 때까지 다시 스케줄되지 않습니다.

   이 함수는 반드시 인터럽트가 꺼진 상태에서 호출되어야 합니다.
   일반적으로는 synch.h에 있는 동기화 기본요소들 중 하나를 사용하는 것이 더 좋습니다. */
void
thread_block (void) {
	// 인터럽트 핸들러 내에서 호출되지 않았는지 확인
	ASSERT (!intr_context ());
	// 인터럽트가 비활성화되어 있는지 확인 
	ASSERT (intr_get_level () == INTR_OFF);
	// 현재 스레드의 상태를 BLOCKED로 변경
	thread_current ()->status = THREAD_BLOCKED;
	// 다음 실행할 스레드를 선택하고 컨텍스트 스위칭 수행
	schedule ();
}

/* 차단된 스레드 T를 실행 준비 상태로 전환합니다.
   T가 차단되지 않은 상태라면 에러입니다. (실행 중인 스레드를 준비 상태로 만들려면 
   thread_yield()를 사용하세요.) /* thread_yield()는 현재 실행 중인 스레드를 ready_list에 넣고 다음 스레드를 실행하는 함수입니다. */

/* 이 함수는 실행 중인 스레드를 선점하지 않습니다. 이는 중요할 수 있습니다:
   호출자가 직접 인터럽트를 비활성화했다면, 스레드를 원자적으로 차단 해제하고
   다른 데이터를 업데이트할 수 있을 것으로 예상할 수 있기 때문입니다. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;  // 이전 인터럽트 레벨을 저장할 변수

	ASSERT (is_thread (t));     // 유효한 스레드인지 확인

	old_level = intr_disable (); // 인터럽트 비활성화하고 이전 상태 저장
	ASSERT (t->status == THREAD_BLOCKED);  // 스레드가 차단 상태인지 확인

	// ready_list에 우선순위 순서대로 스레드 삽입
	// 1. 스레드 상태를 먼저 READY로 변경
	// - 스레드의 상태는 리스트 삽입 전에 변경되어야 함
	// - 리스트 삽입 중 인터럽트가 발생하면 BLOCKED 상태의 스레드가 ready_list에 있을 수 있음
	t->status = THREAD_READY;   

	// 2. ready_list에 우선순위 순서대로 삽입
	// - 스레드가 READY 상태가 된 후에 리스트에 삽입
	// - 우선순위에 따라 정렬된 상태를 유지하기 위함
	list_insert_ordered(&ready_list, &t->elem, cmp_priority, NULL); 

	// 3. 마지막으로 이전 인터럽트 레벨로 복원
	// - 모든 작업이 완료된 후 인터럽트 상태 복원
	// - 중간에 복원하면 원자성이 깨져서 race condition 발생 가능
	intr_set_level (old_level); 
}

bool
cmp_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
    struct thread *thread_a = list_entry(a, struct thread, elem);
    struct thread *thread_b = list_entry(b, struct thread, elem);
    
    // 우선순위가 같을 경우 생성 시간 순으로 정렬
    if (thread_a->priority == thread_b->priority)
        return thread_a->created_tick < thread_b->created_tick;
    return thread_a->priority > thread_b->priority;
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* 현재 실행 중인 스레드를 반환합니다.
   running_thread() 함수에 몇 가지 안전성 검사를 추가한 것입니다.
   자세한 내용은 thread.h 파일 상단의 큰 주석을 참조하세요. */
struct thread *
thread_current (void) {
	// running_thread() 함수를 호출하여 현재 실행 중인 스레드의 포인터를 가져옴
	struct thread *t = running_thread ();

	/* t가 실제로 유효한 스레드인지 확인합니다.
	   이 assertion들 중 하나라도 실패하면, 스레드의 스택이
	   오버플로우되었을 수 있습니다. 각 스레드는 4kB 미만의 
	   스택을 가지므로, 큰 자동 배열이나 중간 정도의 
	   재귀 호출로도 스택 오버플로우가 발생할 수 있습니다. */
	ASSERT (is_thread (t));     // t가 유효한 스레드 구조체인지 확인
	ASSERT (t->status == THREAD_RUNNING);   // t가 실행 중인 상태인지 확인

	return t;    // 현재 실행 중인 스레드 반환
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* CPU를 양보하고 ready_list에 우선순위 순서로 삽입 */
void
thread_yield (void) {
	struct thread *curr = thread_current ();		// 현재 실행 중인 스레드 가져오기
	enum intr_level old_level;						// 이전 인터럽트 상태 저장용 변수

	ASSERT (!intr_context ());						// 인터럽트 처리 중이 아닌지 확인

	old_level = intr_disable ();					// 인터럽트 비활성화하고 이전 상태 저장
	
	if (curr != idle_thread) {						// 현재 스레드가 idle 스레드가 아니면
		// list_push_back (&ready_list, &curr->elem);	// ready_list 끝에 현재 스레드 추가
		
		// list_push_back 대신 우선순위 순서로 삽입
        list_insert_ordered(&ready_list, 
							&curr->elem, 
							cmp_priority, NULL);
	}
	
	do_schedule (THREAD_READY);						// 스레드 상태를 READY로 변경하고 스케줄링
	intr_set_level (old_level);						// 이전 인터럽트 상태로 복구
}

void
thread_sleep(int64_t ticks) {					// ticks: 현재 쓰레드의 일어날 시간
	struct thread *cur;
	enum intr_level old_level;

	old_level = intr_disable();					// 스레드 리스트 조작 예정이므로 인터럽트 OFF
	cur = thread_current();
	ASSERT (cur != idle_thread);

	cur->wakeup_tick = ticks;											// 일어날 시간 저장
	list_insert_ordered(&sleep_list, &cur->elem, cmp_priority, NULL);	// sleep_list 에 추가
	thread_block ();													// block 상태로 변경

	intr_set_level (old_level);					// 인터럽트 ON

}

void
thread_awake(int64_t ticks) {
	/* 슬립 리스트에서 깨워야 할 스레드들을 찾아서 깨우는 함수
	   매개변수 ticks: 현재 시스템 틱
	   
	   동작 과정:
	   1. sleep_list를 순회하면서 깨워야 할 스레드 확인
	   2. 각 스레드의 wakeup_tick이 현재 ticks보다 작거나 같으면 깨워야 함
	   3. 깨워야 할 스레드는 sleep_list에서 제거하고 ready 상태로 변경
	   4. timer_interrupt()에서 주기적으로 이 함수를 호출하여 스레드들을 깨움 */

	// sleep_list의 첫 번째 요소부터 시작
	struct list_elem *elem = list_begin(&sleep_list);

	// sleep_list의 끝까지 순회
	while (elem != list_end(&sleep_list)) {
		// 현재 elem이 가리키는 스레드 구조체 가져오기
		struct thread *cur = list_entry(elem, struct thread, elem);

		if (cur->wakeup_tick <= ticks) {
			// 깨워야 할 시간이 되었다면
			elem = list_remove(elem);		// sleep_list에서 제거하고
			thread_unblock(cur);				// 스레드를 깨움(READY 상태로 변경)
		} else {
			// 아직 깨울 시간이 아니라면 다음 요소로 이동
			elem = list_next(elem);
		}
	}
}

/* 우선순위 변경 및 선점 스케줄링 */
void 
thread_set_priority (int new_priority) {

	// 현재 스레드의 우선순위 변경
    thread_current()->priority = new_priority;
    
    /* 현재 스레드의 우선순위가 변경되었으므로
       ready_list의 최대 우선순위 스레드와 비교하여 필요시 yield */
    if (!list_empty(&ready_list)) {
		// ready_list의 최대 우선순위 스레드 가져오기
		struct thread *max_priority_thread = 
            list_entry(list_begin(&ready_list), struct thread, elem);
        
		// 현재 스레드보다 높은 우선순위의 스레드가 있다면 yield
		if (max_priority_thread->priority > thread_current()->priority)
		/* 현재 스레드가 자원을 다른 스레드에게 양도함.
			이 과정에서 현재 스레드는 블록 상태가 되고,
			우선순위가 높은 스레드가 실행됨. */		
            thread_yield();
    }
}

/* 현재 스레드의 우선순위를 반환합니다. */
int
thread_get_priority (void) {
	// 현재 실행 중인 스레드의 우선순위 값을 반환
	return thread_current ()->priority;
}

/* 현재 스레드의 nice 값을 NICE로 설정합니다. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: 구현이 필요합니다 */
}

/* 현재 스레드의 nice 값을 반환합니다. */
int
thread_get_nice (void) {
	/* TODO: 구현이 필요합니다 */
	return 0;
}

/* 시스템 부하 평균에 100을 곱한 값을 반환합니다. */
int
thread_get_load_avg (void) {
	/* TODO: 구현이 필요합니다 */
	return 0;
}

/* 현재 스레드의 recent_cpu 값에 100을 곱한 값을 반환합니다. */
int
thread_get_recent_cpu (void) {
	/* TODO: 구현이 필요합니다 */
	return 0;
}

/* Idle 스레드. 다른 실행 가능한 스레드가 없을 때 실행됩니다.

   Idle 스레드는 처음에 thread_start()에 의해 ready 리스트에 추가됩니다.
   처음 한 번 스케줄되어 실행될 때 idle_thread를 초기화하고,
   전달받은 세마포어를 "up"하여 thread_start()가 계속 진행되도록 하고,
   즉시 블록됩니다. 그 이후로는 idle 스레드가 ready 리스트에 나타나지 않습니다.
   ready 리스트가 비어있을 때 next_thread_to_run()에 의해 특별한 경우로 반환됩니다. */
static void
idle (void *idle_started_ UNUSED) {
	// idle 스레드 시작을 알리는 세마포어
	struct semaphore *idle_started = idle_started_;

	// 전역 변수 idle_thread에 현재 스레드(idle 스레드) 저장
	idle_thread = thread_current ();
	// 세마포어를 up하여 thread_start()가 계속 진행되도록 함
	sema_up (idle_started);

	for (;;) {
		/* 다른 스레드가 실행되도록 양보 */
		// 인터럽트 비활성화
		intr_disable ();
		// 현재 스레드(idle)를 블록 상태로 변경
		thread_block ();

		/* 인터럽트를 다시 활성화하고 다음 인터럽트를 기다립니다.

		   'sti' 명령어는 다음 명령어가 완료될 때까지 인터럽트를 비활성화합니다.
		   따라서 이 두 명령어는 원자적으로 실행됩니다.
		   이 원자성이 중요한 이유는 인터럽트 재활성화와 다음 인터럽트 대기 사이에
		   인터럽트가 처리되면 최대 한 클록 틱만큼의 시간이 낭비될 수 있기 때문입니다.

		   참고: [IA32-v2a] "HLT", [IA32-v2b] "STI", [IA32-v3a] 7.11.1 "HLT Instruction" */
		// sti로 인터럽트 활성화 후 hlt로 CPU를 대기 상태로 전환
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* 새로운 프로세스를 스케줄링합니다. 이 함수 진입 시에는 인터럽트가 비활성화되어 있어야 합니다.
 * 이 함수는 현재 스레드의 상태를 status로 변경하고
 * 다음에 실행할 스레드를 찾아서 전환합니다.
 * schedule() 함수 내에서는 printf() 호출이 안전하지 않습니다. */
static void
do_schedule(int status) {
	// 인터럽트가 비활성화되어 있는지 확인
	ASSERT (intr_get_level () == INTR_OFF);
	// 현재 스레드가 실행 중인 상태인지 확인
	ASSERT (thread_current()->status == THREAD_RUNNING);

	// 제거 요청된 스레드들을 정리
	while (!list_empty (&destruction_req)) {
		// destruction_req 리스트에서 제거할 스레드를 가져옴
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		// 해당 스레드의 페이지를 해제
		palloc_free_page(victim);
	}

	// 현재 스레드의 상태를 전달받은 status로 변경
	thread_current ()->status = status;
	// 실제 스케줄링 수행
	schedule ();
}

/* 스케줄러 함수: 다음에 실행할 스레드를 선택하고 컨텍스트 스위칭을 수행합니다. */
static void
schedule (void) {
	// 현재 실행 중인 스레드와 다음에 실행할 스레드 가져오기
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	// 인터럽트가 비활성화되어 있는지 확인
	ASSERT (intr_get_level () == INTR_OFF);
	// 현재 스레드가 실행 중이 아닌지 확인 
	ASSERT (curr->status != THREAD_RUNNING);
	// next가 유효한 스레드인지 확인
	ASSERT (is_thread (next));

	/* 다음 스레드를 실행 중 상태로 표시 */
	next->status = THREAD_RUNNING;

	/* 새로운 타임 슬라이스 시작 */
	thread_ticks = 0;

#ifdef USERPROG
	/* 새로운 주소 공간 활성화 */
	process_activate (next);
#endif

	// 현재 스레드와 다음 스레드가 다른 경우에만 스위칭
	if (curr != next) {
		/* 현재 스레드가 종료 상태이고 초기 스레드가 아닌 경우,
		   해당 스레드의 구조체를 나중에 제거하기 위해 큐에 추가합니다.
		   스택이 현재 사용 중이므로 페이지 해제 요청만 큐에 넣습니다.
		   실제 메모리 해제는 다음 schedule() 호출 시작 시 수행됩니다. */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* 스레드를 전환하기 전에 현재 실행 중인 스레드의 정보를 저장합니다. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}
