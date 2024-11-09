#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
void
timer_init (void) {
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable (); // 하드웨어 인터럽트(전체) 비활성화
	int64_t t = ticks; // 현재 틱 수
	intr_set_level (old_level); // 하드웨어 인터럽트(전체) 활성화
	barrier (); // 최적화 장벽
	return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* 스레드한테 재우라고 알람 울리기 */
void
timer_sleep (int64_t ticks) { // ticks: 대기 시간

	// 현재 틱 == OS가 부팅된 이후 현재까지의 총 틱 수 == 현재 시간

	int64_t start = timer_ticks (); // 현재 틱
	thread_sleep(start + ticks); // 현재 틱 + 대기 시간
}

/* Suspends execution for approximately MS milliseconds. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. 
	-> 매 tick마다 호출되며, 
	대기 중인 스레드 중에서 깨어날 시간이 된 스레드를 찾아 ready_list로 이동시킴 */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++; // 틱 증가
	thread_tick ();		// 실행 중인 프로세스의 CPU 사용량 업데이트
	
	// 대기 중인 스레드 중에서 깨어날 시간이 된 스레드를 찾아 ready_list로 이동시킴
	thread_awake(ticks);
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) {
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait (loops);

	/* If the tick count changed, we iterated too long. */
	barrier ();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* NUM/DENOM 초 동안 대략적으로 sleep 합니다. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* NUM/DENOM 초를 timer tick으로 변환합니다. (내림)
	   
	   (NUM / DENOM) 초
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks
	   1초 / TIMER_FREQ ticks
	*/
	
	// num/denom 초가 몇 tick인지 계산
	// TIMER_FREQ는 1초당 발생하는 tick 수
	int64_t ticks = num * TIMER_FREQ / denom;

	// 인터럽트가 켜져있는지 확인, 인터럽트가 꺼져 있으면 강제 종료
	ASSERT (intr_get_level () == INTR_ON);
	
	if (ticks > 0) {
		/* 최소 1 tick 이상 대기해야 하는 경우
		   다른 프로세스에게 CPU를 양보하기 위해 timer_sleep() 사용 */
		timer_sleep (ticks);
	} else {
		/* 1 tick 미만으로 대기하는 경우 busy-wait 루프 사용
		   더 정확한 sub-tick 타이밍을 위해
		   오버플로우 방지를 위해 분자와 분모를 1000으로 나눔 */
		ASSERT (denom % 1000 == 0);
		// busy_wait으로 정확한 시간 대기
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000)); 
	}
}