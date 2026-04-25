# TIMER_SLEEP_FLOW.md

이 파일은 Pintos threads 과제 중 **`timer_sleep()`이 어떤 흐름으로 동작해야 하는지**를 정리한 학습용 문서입니다.

파일 이름 그대로 핵심 주제는 `timer_sleep`의 흐름입니다.

```text
thread 생성
    -> ready_list 대기
    -> scheduler가 실행
    -> 실행 중 timer_sleep() 호출
    -> sleeping_list로 이동
    -> timer_interrupt()가 시간이 된 thread를 깨움
    -> ready_list로 복귀
    -> 다시 scheduler가 실행
```

다만 `timer_sleep()`은 혼자 떨어져 있는 함수가 아니라 `thread_create()`, `ready_list`, `scheduler`, `timer_interrupt()`, `thread_block()`, `thread_unblock()`과 연결되어 있습니다. 그래서 이 문서는 먼저 큰 흐름을 잡고, 그다음 `timer_sleep()` 중심으로 내려갑니다.

## 먼저 잡아야 할 큰 그림

네가 든 예시처럼 클라이언트가 `www.ge.com:80` 같은 사이트를 열려고 한다고 생각해보면, 실제 웹 서버에서는 보통 이런 구조가 나옵니다.

```text
클라이언트 요청
    -> 서버가 연결 accept
    -> 요청 하나를 처리할 worker thread 생성 또는 기존 worker thread에게 작업 전달
    -> worker thread가 HTTP 요청 처리
    -> 파일 읽기, 네트워크 전송, DB 조회 같은 작업 수행
    -> 응답 HTML/CSS/JS/이미지 전송
```

하지만 지금 Pintos threads 과제는 아직 진짜 웹 서버나 네트워크를 구현하는 단계가 아닙니다. 이번 주에 우리가 보는 것은 그 아래에 있는 더 근본적인 OS 동작입니다.

```text
응용 프로그램 / 테스트 코드
    -> thread_create()
    -> ready_list
    -> scheduler
    -> thread 실행
    -> timer_sleep(), lock_acquire(), sema_down() 같은 이유로 block 가능
    -> timer interrupt 또는 unlock/sema_up으로 다시 ready_list 복귀
```

즉 이번 주의 핵심은 “웹 요청을 어떻게 처리하느냐”가 아니라, **여러 실행 흐름 thread 중 누가 CPU를 쓰고, 누가 기다리고, 누가 다시 깨어나는가**입니다.

## 헷갈리기 쉬운 용어 정리

```text
ready_list
    CPU를 바로 쓸 수 있는 thread들이 기다리는 큐
    상태는 THREAD_READY

running thread
    지금 CPU를 실제로 쓰고 있는 단 하나의 thread
    상태는 THREAD_RUNNING

sleeping_list
    특정 tick까지 시간이 지나기를 기다리는 thread들이 들어가는 큐
    상태는 THREAD_BLOCKED

lock waiters / semaphore waiters
    lock이나 semaphore를 기다리는 thread들이 들어가는 큐
    상태는 THREAD_BLOCKED

time slice
    running thread가 CPU를 연속으로 사용할 수 있는 최대 tick 수
    Pintos 기본값은 TIME_SLICE = 4

timer_sleep
    현재 thread가 "나는 N tick 뒤에 다시 실행되고 싶다"고 직접 요청하는 함수
```

중요한 차이는 이것입니다.

```text
timer_sleep()
    thread 자신이 명시적으로 호출한다.
    "나는 지금 할 일이 없으니 N tick 동안 재워줘"에 가깝다.

time slice
    timer interrupt가 자동으로 검사한다.
    "너 CPU 오래 썼으니 다른 thread에게 넘겨"에 가깝다.

lock
    공유 자원 보호용이다.
    ready_list나 sleeping_list에 넣는 모든 순간에 항상 lock을 거는 개념은 아니다.
    Pintos kernel 내부에서는 보통 interrupt를 잠깐 끄고 원자적으로 리스트를 조작한다.
```

## 최상위 실행 지도

```c
/*
 * ============================================================================
 * [Pintos Threads 전체 실행 지도]
 * ============================================================================
 *
 * [커널 부팅] ---> thread_init()
 *                  │
 *                  │ initial thread 설정
 *                  │ ready_list 초기화
 *                  │ tid_lock 초기화
 *                  ▼
 *              thread_start()
 *                  │
 *                  ├─▶ thread_create("idle", ...)
 *                  │      idle thread 생성
 *                  │
 *                  └─▶ intr_enable()
 *                         timer interrupt 시작
 *                         preemptive scheduling 가능해짐
 *
 * --------------------------------------------------------------------------
 *
 * [테스트 코드 / 커널 코드] ---> thread_create(name, priority, function, aux)
 *                                  │
 *                                  ├─▶ palloc_get_page()
 *                                  │      struct thread가 들어갈 페이지 할당
 *                                  │
 *                                  ├─▶ init_thread()
 *                                  │      status = THREAD_BLOCKED
 *                                  │      priority 설정
 *                                  │
 *                                  ├─▶ kernel_thread()로 시작하도록 context 준비
 *                                  │
 *                                  └─▶ thread_unblock(new_thread)
 *                                         status = THREAD_READY
 *                                         ready_list에 삽입
 *
 * --------------------------------------------------------------------------
 *
 * [scheduler] ---> next_thread_to_run()
 *                  │
 *                  │ ready_list에서 다음 thread 선택
 *                  │ 현재 기본 코드는 앞에서 하나 pop
 *                  │ priority scheduling 구현 후에는 가장 높은 priority 선택
 *                  ▼
 *              schedule()
 *                  │
 *                  │ next thread를 THREAD_RUNNING으로 변경
 *                  │ thread_ticks = 0
 *                  ▼
 *              thread_launch()
 *                  │
 *                  ▼
 *              선택된 thread 실행
 *
 * --------------------------------------------------------------------------
 *
 * [실행 중인 thread] ---> function(aux)
 *                         │
 *                         ├─▶ 일반 계산 계속
 *                         │
 *                         ├─▶ timer_sleep(ticks)
 *                         │      시간이 필요해서 sleeping_list로 이동
 *                         │
 *                         ├─▶ lock_acquire(lock)
 *                         │      lock이 없으면 lock waiters에서 대기
 *                         │
 *                         ├─▶ sema_down(sema)
 *                         │      semaphore 값이 0이면 waiters에서 대기
 *                         │
 *                         └─▶ thread_exit()
 *                                실행 종료
 *
 * ============================================================================
 */
```

## ready_list에서 sleeping_list로 가는 기준

thread가 sleeping_list로 가는 기준은 scheduler가 정하지 않습니다. 기준은 단순합니다.

```text
현재 running thread가 timer_sleep(ticks)를 직접 호출했는가?
    YES -> sleeping_list에 들어감
    NO  -> sleeping_list에 들어가지 않음
```

예를 들어 테스트 코드가 이런 식이면:

```c
msg ("sleeping");
timer_sleep (100);
msg ("woke up");
```

흐름은 이렇게 됩니다.

```text
running thread
    -> "sleeping" 출력
    -> timer_sleep(100) 호출
    -> wakeup_tick = 현재 tick + 100 계산
    -> sleeping_list에 들어감
    -> THREAD_BLOCKED
    -> schedule()
    -> 다른 thread 실행

100 tick 후 timer_interrupt()
    -> sleeping_list에서 해당 thread 발견
    -> thread_unblock()
    -> ready_list로 복귀

나중에 scheduler가 선택
    -> timer_sleep(100) 다음 줄부터 실행
    -> "woke up" 출력
```

## time slice 때문에 sleeping_list에 들어가는가?

아니요. time slice가 끝났다고 sleeping_list로 가는 것은 아닙니다.

```text
time slice 만료
    -> running thread가 CPU를 너무 오래 씀
    -> thread_yield()와 비슷하게 ready_list 뒤로 감
    -> 상태는 THREAD_READY
    -> 나중에 다시 바로 실행 가능

timer_sleep 호출
    -> thread가 특정 시간까지 실행되면 안 됨
    -> sleeping_list로 감
    -> 상태는 THREAD_BLOCKED
    -> timer_interrupt가 깨워줄 때까지 실행 불가
```

그림으로 보면:

```text
time slice 만료:

RUNNING thread
    -> timer_interrupt()
    -> thread_tick()
    -> intr_yield_on_return()
    -> ready_list로 이동
    -> THREAD_READY


timer_sleep 호출:

RUNNING thread
    -> timer_sleep(100)
    -> sleeping_list로 이동
    -> thread_block()
    -> THREAD_BLOCKED
```

## lock 때문에 sleeping_list에 들어가는가?

아니요. lock을 기다릴 때는 sleeping_list가 아니라 그 lock 또는 semaphore의 waiters 큐에서 기다립니다.

```text
lock_acquire(lock)
    if lock이 비어 있음:
        현재 thread가 lock 획득
        계속 실행

    if lock을 다른 thread가 들고 있음:
        현재 thread는 block
        lock/semaphore waiters에 들어감
        sleeping_list에는 안 들어감
```

sleeping_list는 오직 “시간” 때문에 기다리는 thread를 위한 큐입니다.

```text
시간 기다림       -> sleeping_list
lock 기다림       -> lock/semaphore waiters
CPU 차례 기다림   -> ready_list
```

## 이번 주 구현 목표 지도

```c
/*
 * ============================================================================
 * [이번 주 threads 코드 구현 목표]
 * ============================================================================
 *
 * 1. Alarm Clock
 *    timer_sleep()의 busy waiting 제거
 *
 *    현재:
 *      timer_sleep()
 *        -> while 시간이 안 지남
 *        -> thread_yield() 반복
 *
 *    목표:
 *      timer_sleep()
 *        -> wakeup_tick 저장
 *        -> sleeping_list 삽입
 *        -> thread_block()
 *
 *      timer_interrupt()
 *        -> ticks++
 *        -> sleeping_list에서 시간이 된 thread 깨움
 *        -> thread_unblock()
 *        -> thread_tick()
 *
 * --------------------------------------------------------------------------
 *
 * 2. Priority Scheduling
 *    ready_list에서 아무 thread나 고르는 것이 아니라,
 *    priority가 높은 thread를 먼저 실행
 *
 *    thread_unblock()
 *      -> ready_list에 priority 순서로 삽입
 *
 *    thread_yield()
 *      -> 현재 thread를 ready_list에 다시 넣을 때도 priority 순서 유지
 *
 *    next_thread_to_run()
 *      -> 가장 높은 priority thread 선택
 *
 * --------------------------------------------------------------------------
 *
 * 3. Priority Donation
 *    높은 priority thread가 낮은 priority thread의 lock을 기다리면,
 *    낮은 priority thread에게 priority를 잠시 빌려줌
 *
 *    상황:
 *      low thread가 lock 보유
 *      high thread가 같은 lock 요청
 *      high thread는 block
 *      low thread가 빨리 실행되어 lock을 놓아야 함
 *
 *    목표:
 *      high priority를 low에게 donation
 *      low가 lock_release()하면 donation 제거
 *
 * --------------------------------------------------------------------------
 *
 * 4. Synchronization Waiter Ordering
 *    semaphore, lock, condition variable에서 기다리는 thread들도
 *    priority 높은 순서로 먼저 깨어나야 함
 *
 * ============================================================================
 */
```

아래 지도는 `devices/timer.c`의 `timer_sleep()`을 중심으로, 스레드 생성부터 잠들기, 깨우기, 다시 실행되기까지의 전체 흐름을 함수 호출 구조로 정리한 것입니다.

```c
/*
 * ============================================================================
 * [Pintos Threads: timer_sleep() 동작 흐름 다이어그램]
 * ============================================================================
 *
 * [테스트 코드 또는 커널 코드] ---> thread_create()
 *                                   │
 *                                   │ 새 kernel thread 생성
 *                                   │ init_thread()로 struct thread 초기화
 *                                   │ thread_unblock() 호출
 *                                   ▼
 *                              ready_list에 삽입
 *                                   │
 *                                   │ scheduler가 나중에 선택
 *                                   ▼
 *                              schedule()
 *                                   │
 *                                   ▼
 *                              kernel_thread()
 *                                   │
 *                                   │ function(aux) 실행 시작
 *                                   ▼
 *                              worker 함수 실행
 *                                   │
 *                                   │ 중간에 timer_sleep(ticks) 호출
 *                                   ▼
 *                              timer_sleep(ticks)
 *                                   │
 *                                   ├─▶ ticks <= 0 검사
 *                                   │      0 이하라면 잠잘 필요가 없으므로 바로 return
 *                                   │
 *                                   ├─▶ intr_disable()
 *                                   │      sleeping_list와 현재 thread 상태를 안전하게 변경하기 위해
 *                                   │      timer interrupt가 중간에 끼어들지 못하게 함
 *                                   │
 *                                   ├─▶ thread_current()
 *                                   │      지금 CPU에서 실행 중인 thread를 가져옴
 *                                   │      timer_sleep()은 다른 thread가 아니라 자기 자신을 재움
 *                                   │
 *                                   ├─▶ timer_ticks()
 *                                   │      현재 전역 tick 값 확인
 *                                   │
 *                                   ├─▶ wakeup_tick 계산
 *                                   │      current->wakeup_tick = 현재 tick + 잠잘 tick 수
 *                                   │      예: 현재 tick 1000, ticks 50이면 wakeup_tick 1050
 *                                   │
 *                                   ├─▶ sleeping_list에 current 삽입
 *                                   │      current는 이제 "시간 때문에 기다리는 thread"가 됨
 *                                   │      wakeup_tick 기준 오름차순으로 넣으면
 *                                   │      timer_interrupt()가 앞에서부터 깨울 수 있어 효율적
 *                                   │
 *                                   ├─▶ thread_block()
 *                                   │      current 상태를 THREAD_RUNNING에서 THREAD_BLOCKED로 변경
 *                                   │      schedule() 호출
 *                                   │      ready_list에서 다음 thread 선택
 *                                   │      context switch 발생
 *                                   │
 *                                   └─▶ 나중에 다시 깨어난 뒤 복귀
 *                                          timer_interrupt()가 thread_unblock(current) 호출
 *                                          current가 ready_list로 이동
 *                                          scheduler가 current를 다시 선택
 *                                          thread_block() 다음 줄부터 이어서 실행
 *                                          intr_set_level(old_level) 후 timer_sleep() return
 *
 * --------------------------------------------------------------------------
 *
 * [하드웨어 타이머 인터럽트 발생] ---> timer_interrupt()
 *                                      │
 *                                      ├─▶ ticks++
 *                                      │      전역 tick 카운터 증가
 *                                      │
 *                                      ├─▶ sleeping_list 확인
 *                                      │      wakeup_tick <= ticks인 thread 탐색
 *                                      │
 *                                      ├─▶ thread_unblock()
 *                                      │      잠에서 깰 시간이 된 thread를 THREAD_READY로 변경
 *                                      │      ready_list에 다시 삽입
 *                                      │
 *                                      └─▶ thread_tick()
 *                                             현재 실행 중인 thread의 time slice 계산
 *                                             TIME_SLICE를 다 쓰면 선점 예약
 *
 * --------------------------------------------------------------------------
 *
 * [깨어난 thread] ---> ready_list 대기
 *                       │
 *                       │ scheduler가 다시 선택
 *                       ▼
 *                  timer_sleep() 다음 줄부터 이어서 실행
 *
 * ============================================================================
 * ※ 현재 기본 Pintos 코드의 timer_sleep()은 sleeping_list를 쓰지 않습니다.
 *    while 루프 안에서 thread_yield()를 반복하는 busy waiting 방식입니다.
 *
 * ※ 과제에서 목표로 하는 구현은:
 *    timer_sleep()
 *      -> sleeping_list에 넣기
 *      -> thread_block()
 *      -> timer_interrupt()에서 시간이 된 thread를 thread_unblock()
 *
 * ※ sleep과 time slice는 다릅니다.
 *    sleep      : 특정 tick까지 현재 thread를 BLOCKED 상태로 재움
 *    time slice : 실행 중인 thread가 CPU를 너무 오래 쓰면 선점되도록 함
 * ============================================================================
 */
```

## 현재 기본 코드의 의미

`devices/timer.c`의 기본 `timer_sleep()`은 아래처럼 구현되어 있습니다.

```c
void
timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks ();

	ASSERT (intr_get_level () == INTR_ON);
	while (timer_elapsed (start) < ticks)
		thread_yield ();
}
```

이 코드는 thread를 완전히 재우는 것이 아니라, 원하는 시간이 지날 때까지 계속 CPU를 양보하고 다시 깨어나 시간을 확인합니다. 그래서 효율적인 sleep 구현을 위해서는 현재 thread를 `sleeping_list`에 넣고 `thread_block()`으로 BLOCKED 상태로 바꾸는 방식이 필요합니다.

## timer_sleep() 목표 수도 코드

```c
timer_sleep(ticks):
    if ticks <= 0:
        return

    old_level = intr_disable()

    current = thread_current()
    current->wakeup_tick = timer_ticks() + ticks

    sleeping_list에 current 삽입
        가능하면 wakeup_tick 기준 오름차순 정렬

    thread_block()

    intr_set_level(old_level)
```

## timer_sleep() 수도 코드 상세 해설

`timer_sleep(ticks)`의 목표는 현재 실행 중인 thread를 `ticks`만큼 **CPU에서 빼고**, 시간이 지나면 다시 `ready_list`로 돌려보내는 것입니다.

중요한 점은 `timer_sleep()`이 직접 시간을 기다리며 반복문을 도는 함수가 아니라는 것입니다. `timer_sleep()`은 현재 thread를 `sleeping_list`에 넣고 잠들게 만들 뿐이고, 실제로 깨우는 일은 나중에 `timer_interrupt()`가 합니다.

```c
timer_sleep(ticks):
```

현재 running thread가 호출합니다.

```text
현재 상태:
    thread_current() == 지금 CPU를 쓰는 thread
    thread 상태 == THREAD_RUNNING
```

예를 들어 어떤 worker thread가 아래 코드를 실행하다가:

```c
timer_sleep(100);
```

를 만나면, 그 worker thread 자신이 잠들 대상입니다.

```c
    if ticks <= 0:
        return
```

0 이하 tick은 기다릴 필요가 없습니다.

```text
timer_sleep(0)
    -> 이미 기다릴 시간이 없음
    -> 바로 return

timer_sleep(-1)
    -> 과거 시간까지 기다리라는 말이므로 의미 없음
    -> 바로 return
```

이 처리를 해두면 불필요하게 현재 thread를 block하지 않습니다.

```c
    old_level = intr_disable()
```

여기서 인터럽트를 끄는 이유는 `sleeping_list`와 현재 thread 상태를 안전하게 바꾸기 위해서입니다.

`timer_sleep()` 안에서 해야 하는 작업은 여러 단계입니다.

```text
1. 현재 tick 읽기
2. wakeup_tick 계산
3. current thread에 wakeup_tick 저장
4. sleeping_list에 current thread 삽입
5. thread_block()으로 상태 변경
```

이 중간에 timer interrupt가 끼어들면 문제가 생길 수 있습니다.

예를 들어:

```text
current->wakeup_tick은 설정했는데
아직 sleeping_list에는 넣기 전
    -> timer_interrupt 발생
    -> timer_interrupt는 이 thread를 sleeping_list에서 찾지 못함
```

또는:

```text
sleeping_list에는 넣었는데
아직 thread_block() 하기 전
    -> timer_interrupt 발생
    -> 리스트 상태와 thread 상태가 엇갈릴 수 있음
```

그래서 이 중요한 구간은 interrupt를 꺼서 원자적으로 처리합니다.

```c
    current = thread_current()
```

지금 CPU에서 실행 중인 thread를 가져옵니다.

```text
timer_sleep()은 "다른 thread를 재우는 함수"가 아닙니다.
timer_sleep()은 "자기 자신, 즉 현재 실행 중인 thread를 재우는 함수"입니다.
```

그래서 `thread_current()`로 현재 thread 구조체를 얻습니다.

```c
    current->wakeup_tick = timer_ticks() + ticks
```

언제 깨어날지를 계산해서 thread 구조체에 저장합니다.

예를 들어:

```text
현재 ticks = 500
timer_sleep(100)

wakeup_tick = 500 + 100 = 600
```

그러면 이 thread는 전역 tick이 600 이상이 되었을 때 깨어나면 됩니다.

이 값을 저장하려면 `struct thread`에 필드가 하나 필요합니다.

```c
struct thread {
    ...
    int64_t wakeup_tick;
    ...
};
```

```c
    sleeping_list에 current 삽입
        가능하면 wakeup_tick 기준 오름차순 정렬
```

현재 thread를 `sleeping_list`에 넣습니다.

`sleeping_list`는 이렇게 생긴 개념입니다.

```text
sleeping_list
    [thread A: wakeup_tick 120]
    [thread B: wakeup_tick 140]
    [thread C: wakeup_tick 200]
```

오름차순으로 정렬해두면 `timer_interrupt()`가 편해집니다.

```text
현재 ticks = 130

sleeping_list 첫 번째 thread의 wakeup_tick = 120
    -> 깨워야 함

다음 thread의 wakeup_tick = 140
    -> 아직 아님
    -> 뒤쪽은 더 늦게 깨는 thread들이므로 검사 중단 가능
```

정렬하지 않고 그냥 넣으면 매 timer interrupt마다 리스트 전체를 훑어야 할 수 있습니다. Pintos 테스트 규모에서는 둘 다 가능하지만, 정렬 삽입이 더 깔끔합니다.

실제 C 코드 느낌은 이런 방향입니다.

```c
list_insert_ordered (&sleeping_list, &current->elem, wakeup_less, NULL);
```

여기서 `wakeup_less`는 두 thread의 `wakeup_tick`을 비교하는 함수입니다.

```c
    thread_block()
```

이 줄이 진짜로 현재 thread를 재우는 핵심입니다.

`thread_block()`은 현재 thread 상태를 바꿉니다.

```text
THREAD_RUNNING
    -> THREAD_BLOCKED
```

그리고 내부에서 `schedule()`을 호출합니다.

```text
thread_block()
    -> 현재 thread 상태를 BLOCKED로 변경
    -> schedule()
    -> ready_list에서 다른 thread 선택
    -> context switch
```

중요한 점:

```text
thread_block()을 호출하면,
현재 thread는 여기서 멈춘 것처럼 보입니다.

하지만 함수가 영원히 끝나는 것이 아닙니다.
나중에 timer_interrupt()가 thread_unblock()으로 깨우고,
scheduler가 이 thread를 다시 선택하면,
thread_block() 다음 줄부터 이어서 실행됩니다.
```

즉 흐름은 이렇게 됩니다.

```text
timer_sleep()
    -> thread_block()
        여기서 CPU를 잃음

나중에 깨어난 뒤
    -> thread_block()에서 return
    -> intr_set_level(old_level)
    -> timer_sleep() return
    -> 호출했던 코드의 다음 줄 실행
```

```c
    intr_set_level(old_level)
```

잠들기 전에 꺼두었던 interrupt 상태를 원래대로 복구합니다.

보통 `timer_sleep()`은 interrupt가 켜진 상태에서 호출되므로:

```text
old_level = INTR_ON
intr_disable()
...
intr_set_level(INTR_ON)
```

이 됩니다.

주의할 점은 `thread_block()` 이후에 바로 실행되는 것이 아니라는 점입니다.

```text
처음 timer_sleep() 호출 시점:
    intr_disable()
    sleeping_list 삽입
    thread_block()
        -> 다른 thread로 전환

나중에 깨어난 시점:
    thread_block() 다음 줄로 복귀
    intr_set_level(old_level)
```

## timer_sleep() 상세 실행 예시

```text
현재 ticks = 1000
현재 running thread = worker-1

worker-1이 timer_sleep(50) 호출
```

흐름:

```text
timer_sleep(50)
    -> ticks <= 0 ? 아니오
    -> interrupt off
    -> current = worker-1
    -> worker-1.wakeup_tick = 1050
    -> sleeping_list에 worker-1 삽입
    -> thread_block()
        worker-1 상태: THREAD_BLOCKED
        scheduler가 다른 thread 실행
```

시간이 흐르다가:

```text
timer_interrupt()
    ticks = 1050
    sleeping_list 확인
    worker-1.wakeup_tick <= ticks
    worker-1을 sleeping_list에서 제거
    thread_unblock(worker-1)
        worker-1 상태: THREAD_READY
        ready_list에 삽입
```

이후 scheduler가 worker-1을 다시 고르면:

```text
worker-1 재실행
    -> thread_block()에서 return
    -> intr_set_level(old_level)
    -> timer_sleep() return
    -> timer_sleep(50) 다음 줄부터 실행
```

## timer_interrupt() 목표 수도 코드

```c
timer_interrupt(args):
    ticks++

    while sleeping_list가 비어 있지 않음:
        t = sleeping_list의 첫 번째 thread

        if t->wakeup_tick > ticks:
            break

        sleeping_list에서 t 제거
        thread_unblock(t)

    thread_tick()
```

## 필요한 핵심 변경 지점

```text
include/threads/thread.h
    struct thread에 wakeup_tick 필드 추가

devices/timer.c
    sleeping_list 전역 리스트 추가
    timer_init()에서 list_init(&sleeping_list)
    timer_sleep()을 busy waiting 방식에서 block 방식으로 변경
    timer_interrupt()에서 시간이 된 thread를 깨우는 로직 추가

threads/thread.c
    thread_block()
        현재 thread를 THREAD_BLOCKED로 만들고 schedule()

    thread_unblock()
        BLOCKED thread를 READY 상태로 만들고 ready_list에 삽입

    thread_tick()
        timer interrupt마다 호출되어 time slice를 관리
```

---

## timer_sleep(ticks) 상세 지도 + 실제 코드 위치

아래 지도는 `timer_sleep(ticks)`가 목표 구현에서 어떤 순서로 동작해야 하는지, 그리고 그 흐름이 현재 코드의 어느 함수들과 연결되는지를 줄 번호와 함께 정리한 것입니다.

### 클릭 가능한 코드 링크

- 현재 `timer_sleep()` 시작: [devices/timer.c](/workspaces/week09-team-02-pintos-threads/devices/timer.c:92)
- 현재 `timer_sleep()`의 busy waiting 루프: [devices/timer.c](/workspaces/week09-team-02-pintos-threads/devices/timer.c:96)
- 현재 `timer_interrupt()` 시작: [devices/timer.c](/workspaces/week09-team-02-pintos-threads/devices/timer.c:126)
- 전역 `ticks` 변수: [devices/timer.c](/workspaces/week09-team-02-pintos-threads/devices/timer.c:21)
- `timer_ticks()` 함수: [devices/timer.c](/workspaces/week09-team-02-pintos-threads/devices/timer.c:75)
- `struct thread` 정의: [include/threads/thread.h](/workspaces/week09-team-02-pintos-threads/include/threads/thread.h:88)
- `struct thread`의 `elem` 필드: [include/threads/thread.h](/workspaces/week09-team-02-pintos-threads/include/threads/thread.h:96)
- `thread_current()` 함수: [threads/thread.c](/workspaces/week09-team-02-pintos-threads/threads/thread.c:257)
- `thread_block()` 함수: [threads/thread.c](/workspaces/week09-team-02-pintos-threads/threads/thread.c:219)
- `thread_unblock()` 함수: [threads/thread.c](/workspaces/week09-team-02-pintos-threads/threads/thread.c:235)
- `next_thread_to_run()` 함수: [threads/thread.c](/workspaces/week09-team-02-pintos-threads/threads/thread.c:419)
- `schedule()` 함수: [threads/thread.c](/workspaces/week09-team-02-pintos-threads/threads/thread.c:541)
- 실제 context switch 호출 지점: [threads/thread.c](/workspaces/week09-team-02-pintos-threads/threads/thread.c:575)
- `list_insert_ordered()` 구현: [lib/kernel/list.c](/workspaces/week09-team-02-pintos-threads/lib/kernel/list.c:419)
- `list_insert_ordered()` 선언: [include/lib/kernel/list.h](/workspaces/week09-team-02-pintos-threads/include/lib/kernel/list.h:154)

현재 `timer_sleep()`은 아직 busy waiting 방식입니다.

```text
현재 코드 위치:
    devices/timer.c:90
        timer_sleep() 설명 주석

    devices/timer.c:92
        timer_sleep(int64_t ticks) 함수 시작

    devices/timer.c:93
        int64_t start = timer_ticks();

    devices/timer.c:96-97
        while (...) thread_yield();
        즉, 아직 thread_block()으로 진짜 잠드는 구조가 아님
```

목표 구현은 아래처럼 바뀌어야 합니다.


void
timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks ();

	ASSERT (intr_get_level () == INTR_ON);
	while (timer_elapsed (start) < ticks)
		thread_yield ();
}

```c
/*
 * ============================================================================
 * [timer_sleep(ticks) 상세 내부 흐름]
 * ============================================================================
 *
 * timer_sleep(ticks)
 *     │
 *     ├─▶ 1. ticks <= 0 검사
 *     │      │
 *     │      ├─ 의미:
 *     │      │    0 tick 또는 음수 tick 동안은 기다릴 필요가 없음
 *     │      │
 *     │      ├─ 목표 코드:
 *     │      │    if (ticks <= 0)
 *     │      │        return;
 *     │      │
 *     │      └─ 들어갈 위치:
 *     │           devices/timer.c:92
 *     │           timer_sleep() 함수 시작 직후
 *     │
 *     ├─▶ 2. intr_disable()
 *     │      │
 *     │      ├─ 의미:
 *     │      │    sleeping_list 조작과 thread_block() 사이에
 *     │      │    timer interrupt가 끼어들지 못하게 막음
 *     │      │
 *     │      ├─ 목표 코드:
 *     │      │    enum intr_level old_level = intr_disable();
 *     │      │
 *     │      └─ 관련 코드:
 *     │           devices/timer.c:75-80
 *     │               timer_ticks()도 ticks 값을 안전하게 읽기 위해
 *     │               내부에서 interrupt를 잠깐 끔
 *     │
 *     ├─▶ 3. thread_current()
 *     │      │
 *     │      ├─ 의미:
 *     │      │    timer_sleep()은 다른 thread를 재우는 함수가 아님
 *     │      │    지금 CPU를 쓰고 있는 자기 자신을 재움
 *     │      │
 *     │      ├─ 목표 코드:
 *     │      │    struct thread *current = thread_current();
 *     │      │
 *     │      └─ 실제 코드 위치:
 *     │           threads/thread.c:257
 *     │               thread_current() 함수 시작
 *     │
 *     │           threads/thread.c:259
 *     │               running_thread()로 현재 thread 구조체를 찾음
 *     │
 *     │           threads/thread.c:266-267
 *     │               현재 thread가 유효하고 THREAD_RUNNING인지 검사
 *     │
 *     ├─▶ 4. timer_ticks()
 *     │      │
 *     │      ├─ 의미:
 *     │      │    현재 전역 tick 값을 읽음
 *     │      │
 *     │      ├─ 목표 코드:
 *     │      │    int64_t now = timer_ticks();
 *     │      │
 *     │      └─ 실제 코드 위치:
 *     │           devices/timer.c:20-21
 *     │               전역 ticks 변수
 *     │
 *     │           devices/timer.c:73-80
 *     │               timer_ticks() 함수
 *     │
 *     ├─▶ 5. wakeup_tick 계산
 *     │      │
 *     │      ├─ 의미:
 *     │      │    언제 다시 깨울지 thread 구조체에 저장
 *     │      │
 *     │      ├─ 예시:
 *     │      │    현재 ticks = 1000
 *     │      │    timer_sleep(50)
 *     │      │    current->wakeup_tick = 1050
 *     │      │
 *     │      ├─ 목표 코드:
 *     │      │    current->wakeup_tick = now + ticks;
 *     │      │
 *     │      └─ 추가해야 할 위치:
 *     │           include/threads/thread.h:88
 *     │               struct thread 시작
 *     │
 *     │           include/threads/thread.h:90-96 근처
 *     │               tid, status, priority, elem 필드 주변에
 *     │               int64_t wakeup_tick; 필드 추가 후보
 *     │
 *     ├─▶ 6. sleeping_list에 current 삽입
 *     │      │
 *     │      ├─ 의미:
 *     │      │    current thread를 "시간 때문에 기다리는 목록"으로 옮김
 *     │      │
 *     │      ├─ 목표 코드:
 *     │      │    list_insert_ordered(&sleeping_list,
 *     │      │                        &current->elem,
 *     │      │                        wakeup_less,
 *     │      │                        NULL);
 *     │      │
 *     │      ├─ 추가해야 할 위치:
 *     │      │    devices/timer.c:20-30 근처
 *     │      │        static struct list sleeping_list;
 *     │      │        static bool wakeup_less(...);
 *     │      │
 *     │      │    devices/timer.c:35-46
 *     │      │        timer_init() 안에서 list_init(&sleeping_list);
 *     │      │
 *     │      └─ 관련 리스트 함수:
 *     │           lib/kernel/list.c:419
 *     │               list_insert_ordered() 함수 시작
 *     │
 *     │           include/lib/kernel/list.h:154
 *     │               list_insert_ordered() 선언
 *     │
 *     ├─▶ 7. thread_block()
 *     │      │
 *     │      ├─ 의미:
 *     │      │    현재 thread를 THREAD_BLOCKED 상태로 만들고
 *     │      │    scheduler에게 CPU를 넘김
 *     │      │
 *     │      ├─ 목표 코드:
 *     │      │    thread_block();
 *     │      │
 *     │      └─ 실제 코드 위치:
 *     │           threads/thread.c:219
 *     │               thread_block() 함수 시작
 *     │
 *     │           threads/thread.c:221-222
 *     │               interrupt context가 아니고 interrupt off 상태인지 검사
 *     │
 *     │           threads/thread.c:223
 *     │               thread_current()->status = THREAD_BLOCKED;
 *     │
 *     │           threads/thread.c:224
 *     │               schedule();
 *     │
 *     ├─▶ 8. schedule()
 *     │      │
 *     │      ├─ 의미:
 *     │      │    ready_list에서 다음 실행할 thread를 고르고 context switch
 *     │      │
 *     │      └─ 실제 코드 위치:
 *     │           threads/thread.c:419
 *     │               next_thread_to_run() 함수 시작
 *     │
 *     │           threads/thread.c:421-424
 *     │               ready_list가 비었으면 idle_thread,
 *     │               아니면 ready_list 앞에서 하나 꺼냄
 *     │
 *     │           threads/thread.c:541
 *     │               schedule() 함수 시작
 *     │
 *     │           threads/thread.c:543-544
 *     │               현재 thread와 다음 thread를 잡음
 *     │
 *     │           threads/thread.c:550
 *     │               next->status = THREAD_RUNNING;
 *     │
 *     │           threads/thread.c:553
 *     │               thread_ticks = 0;
 *     │               새 time slice 시작
 *     │
 *     │           threads/thread.c:575
 *     │               thread_launch(next);
 *     │               실제 context switch
 *     │
 *     └─▶ 9. 나중에 다시 깨어난 뒤 복귀
 *            │
 *            ├─ 의미:
 *            │    thread_block()에서 영원히 끝나는 것이 아님
 *            │    timer_interrupt()가 깨우고 scheduler가 다시 고르면
 *            │    thread_block() 다음 줄부터 이어서 실행됨
 *            │
 *            ├─ 깨우는 목표 위치:
 *            │    devices/timer.c:124-129
 *            │        현재 timer_interrupt()는 ticks++와 thread_tick()만 함
 *            │        여기에 sleeping_list 검사와 thread_unblock()이 추가되어야 함
 *            │
 *            ├─ 실제 깨우기 함수:
 *            │    threads/thread.c:235
 *            │        thread_unblock() 함수 시작
 *            │
 *            │    threads/thread.c:241
 *            │        interrupt를 끄고 ready_list 조작 시작
 *            │
 *            │    threads/thread.c:243
 *            │        list_push_back(&ready_list, &t->elem);
 *            │
 *            │    threads/thread.c:244
 *            │        t->status = THREAD_READY;
 *            │
 *            └─ timer_sleep() 마무리 목표 코드:
 *                 intr_set_level(old_level);
 *                 return;
 *
 * ============================================================================
 */
```

## timer_sleep() 목표 코드 뼈대

아래 코드는 그대로 복붙용 완성본이라기보다, 위 상세 지도를 C 코드 모양으로 접은 뼈대입니다.

```c
void
timer_sleep (int64_t ticks) {
    if (ticks <= 0)
        return;

    enum intr_level old_level = intr_disable ();

    struct thread *current = thread_current ();
    int64_t now = timer_ticks ();
    current->wakeup_tick = now + ticks;

    list_insert_ordered (&sleeping_list,
                         &current->elem,
                         wakeup_less,
                         NULL);

    thread_block ();

    intr_set_level (old_level);
}
```
