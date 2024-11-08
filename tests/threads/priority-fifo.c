/* Creates several threads all at the same priority and ensures
   that they consistently run in the same round-robin order.

   Based on a test originally submitted for Stanford's CS 140 in
   winter 1999 by by Matt Franklin
   <startled@leland.stanford.edu>, Greg Hutchins
   <gmh@leland.stanford.edu>, Yu Ping Hu <yph@cs.stanford.edu>.
   Modified by arens. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "devices/timer.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* 단순 스레드 데이터를 저장하는 구조체 */
struct simple_thread_data 
  {
    int id;                     /* 스레드의 고유 ID */
    int iterations;             /* 현재까지의 반복 횟수 */
    struct lock *lock;          /* 출력을 위한 락 */
    int **op;                   /* 출력 버퍼의 위치를 가리키는 포인터 */
  };

/* 스레드와 반복 횟수 상수 정의 */
#define THREAD_CNT 16          /* 생성할 스레드의 수 */
#define ITER_CNT 16            /* 각 스레드가 반복할 횟수 */

/* 스레드 함수 선언 */
static thread_func simple_thread_func;

/* 우선순위 FIFO 테스트 함수 */
void
test_priority_fifo (void) 
{
  struct simple_thread_data data[THREAD_CNT];  /* 각 스레드의 데이터 배열 */
  struct lock lock;                            /* 동기화를 위한 락 */
  int *output, *op;                           /* 출력 버퍼와 현재 위치 포인터 */
  int i, cnt;                                 /* 반복문 변수들 */

  /* MLFQS 스케줄러와는 호환되지 않음을 확인 */
  ASSERT (!thread_mlfqs);

  /* 현재 스레드의 우선순위가 기본값인지 확인 */
  ASSERT (thread_get_priority () == PRI_DEFAULT);

  /* 테스트 시작 메시지 출력 */
  msg ("%d개의 스레드가 매번 동일한 순서로 %d번씩 반복 실행됩니다.",
       THREAD_CNT, ITER_CNT);
  msg ("순서가 변경된다면 버그가 있는 것입니다.");

  /* 출력 버퍼 메모리 할당 */
  output = op = malloc (sizeof *output * THREAD_CNT * ITER_CNT * 2);
  ASSERT (output != NULL);      /* 메모리 할당 성공 확인 */
  lock_init (&lock);            /* 락 초기화 */

  /* 현재 스레드의 우선순위를 높임 */
  // 34가 됨.
  thread_set_priority (PRI_DEFAULT + 2);
  
  /* THREAD_CNT 개수만큼 스레드 생성 */
  for (i = 0; i < THREAD_CNT; i++) 
    {
      char name[16];                          /* 스레드 이름 버퍼 */
      struct simple_thread_data *d = data + i;/* 현재 스레드의 데이터 구조체 */
      snprintf (name, sizeof name, "%d", i);  /* 스레드 이름 설정 */
      d->id = i;                              /* 스레드 ID 설정 */
      d->iterations = 0;                      /* 반복 횟수 초기화 */
      d->lock = &lock;                        /* 락 포인터 설정 */
      d->op = &op;                           /* 출력 버퍼 포인터 설정 */
      /* 새로운 스레드 생성 */
      // 33 ~ 48 사이의 우선순위로 설정 후 총 16개의 스레드 생성
      thread_create (name, PRI_DEFAULT + 1, simple_thread_func, d);
    }

  /* 현재 스레드의 우선순위를 기본값으로 되돌림 */
  thread_set_priority (PRI_DEFAULT);
  
  /* 다른 모든 스레드들이 여기서 종료될 때까지 대기 */
  ASSERT (lock.holder == NULL);

  /* 결과 출력 */
  cnt = 0;
  for (; output < op; output++) 
    {
      struct simple_thread_data *d;

      /* 유효한 스레드 ID인지 확인 */
      ASSERT (*output >= 0 && *output < THREAD_CNT);
      d = data + *output;
      /* 각 반복의 시작에서 메시지 출력 */
      if (cnt % THREAD_CNT == 0)
        printf ("(우선순위-선입선출) 반복:");
      printf (" %d", d->id);
      /* 각 반복의 끝에서 줄바꿈 */
      if (++cnt % THREAD_CNT == 0)
        printf ("\n");
      d->iterations++;
    }
}

/* 각 스레드가 실행하는 함수 */
static void 
simple_thread_func (void *data_) 
{
  struct simple_thread_data *data = data_;    /* 스레드 데이터 캐스팅 */
  int i;
  
  /* 지정된 횟수만큼 반복 */
  for (i = 0; i < ITER_CNT; i++) 
    {
      lock_acquire (data->lock);              /* 락 획득 */
      *(*data->op)++ = data->id;             /* 출력 버퍼에 스레드 ID 기록 */
      lock_release (data->lock);              /* 락 해제 */
      thread_yield ();                        /* 다른 스레드에게 CPU 양보 */
    }
}
