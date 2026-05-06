# SYSCALL_HANDLER1_FLOW.md

이 파일은 Pintos Project 2에서 **`syscall_handler()`의 입구와 프로세스 관련 syscall 흐름**을 정리한 학습용 문서입니다.

파일 이름 그대로 핵심 주제는 `Syscall Handler 담당 1`의 구현 흐름입니다.

```text
유저 프로그램 실행
    -> write(), exit(), exec(), wait(), fork() 같은 syscall 호출
    -> CPU가 syscall entry로 진입
    -> syscall_handler() 호출
    -> syscall 번호 확인
    -> 인자 꺼내기
    -> 유저 포인터 검증
    -> halt/exit/fork/exec/wait 중 맞는 backend 호출
    -> 반환값을 rax에 저장
    -> 유저 모드로 복귀
```

다만 `syscall_handler()`는 혼자 동작하지 않습니다. `process_exec()`, `process_wait()`, `process_fork()`, `thread_exit()`, `power_off()`와 연결되어 있습니다. 그래서 이 문서는 먼저 큰 그림을 잡고, 그다음 담당 1의 실제 작업 순서로 내려갑니다.

## 먼저 잡아야 할 큰 그림

Project 2의 핵심은 유저 프로그램이 커널에게 "직접" 접근하지 못하고, 반드시 syscall 창구를 통해 부탁해야 한다는 점입니다.

```text
유저 프로그램
    -> open(), read(), write(), fork(), exec(), wait() 호출
    -> 실제로는 syscall instruction 발생
    -> 커널의 syscall_handler()로 진입
    -> 커널이 요청 종류와 인자를 검사
    -> 안전하면 내부 커널 함수 호출
    -> 결과를 유저 프로그램에게 반환
```

여기서 담당 1은 파일 syscall 전체를 맡는 사람이 아니라, **이 입구 자체와 프로세스 관련 syscall의 첫 관문**을 담당합니다.

```text
Syscall Handler 담당 1의 범위
    -> syscall_handler() 기본 switch 만들기
    -> 포인터 검증 helper 만들기
    -> halt 연결
    -> exit 연결
    -> exec 연결
    -> wait 연결
    -> fork 연결
```

즉 이 담당의 핵심은 “유저 프로그램이 커널로 들어오는 입구를 안전하게 만들고, 프로세스 관련 요청을 올바른 backend로 넘기는 것”입니다.

## 헷갈리기 쉬운 용어 정리

```text
syscall_handler
    모든 syscall의 공용 입구 함수
    syscall 번호를 보고 무슨 요청인지 분기한다

intr_frame
    유저 모드에서 커널로 들어올 때 CPU 레지스터 상태를 저장한 구조체
    syscall 번호와 인자도 여기 레지스터 값으로 들어 있다

rax
    syscall 번호가 들어오는 레지스터
    syscall 반환값도 다시 여기에 넣는다

rdi, rsi, rdx, r10, r8, r9
    syscall 인자들이 순서대로 들어오는 레지스터

user pointer
    유저 프로그램이 커널에 넘긴 주소
    커널은 이 주소를 절대 바로 믿으면 안 된다

backend
    handler가 직접 다 하지 않고 실제 로직을 맡는 함수
    예: process_exec(), process_wait(), process_fork()
```

중요한 차이는 이것입니다.

```text
syscall_handler()
    "무슨 요청이 들어왔는지"를 해석하는 입구
    너무 많은 로직을 넣기보다 분기와 검증에 집중하는 게 좋다

process_exec(), process_wait(), process_fork()
    실제 프로세스 로직을 처리하는 backend
    handler는 보통 여기로 연결만 해 준다

유저 포인터 검증
    논리 실패와 다르다
    잘못된 주소면 대개 exit(-1)

파일 없음, 자식 아님, 이미 wait 함
    주소 문제는 아니므로 syscall 실패값을 반환
```

## 최상위 실행 지도

```c
/*
 * ============================================================================
 * [Project 2 - Syscall Handler 담당 1 전체 실행 지도]
 * ============================================================================
 *
 * [유저 프로그램] ---> exit(57), exec("child-simple"), wait(pid), fork("x")
 *                      │
 *                      │ lib/user/syscall.c wrapper를 통해 syscall 발생
 *                      ▼
 *                  syscall entry
 *                      │
 *                      ▼
 *                  syscall_handler(struct intr_frame *f)
 *                      │
 *                      ├─▶ f->R.rax 읽기
 *                      │      어떤 syscall인지 번호 확인
 *                      │
 *                      ├─▶ 레지스터에서 인자 꺼내기
 *                      │      rdi, rsi, rdx ...
 *                      │
 *                      ├─▶ 유저 포인터 검사
 *                      │      NULL 여부
 *                      │      유저 주소 영역 여부
 *                      │      매핑 여부
 *                      │      문자열/버퍼 끝까지 접근 가능한지
 *                      │
 *                      ├─▶ SYS_HALT  -> power_off()
 *                      ├─▶ SYS_EXIT  -> sys_exit(status)
 *                      ├─▶ SYS_EXEC  -> copy_in_string() -> process_exec()
 *                      ├─▶ SYS_WAIT  -> process_wait(pid)
 *                      └─▶ SYS_FORK  -> process_fork(name, f)
 *                                      │
 *                                      └─▶ 결과를 f->R.rax에 저장
 *
 * ============================================================================
 */
```

## 먼저 통과시켜야 할 기준 흐름

처음부터 `fork`, `exec`, `wait`를 다 붙이려고 하면 헷갈리기 쉽습니다.  
가장 먼저 확인해야 할 기준선은 `hello`처럼 단순한 프로그램이 syscall을 타고 정상 종료하는지입니다.

```text
hello 실행
    -> 유저 모드 진입
    -> write(1, "hello\n")
    -> syscall_handler()
    -> 출력 처리
    -> exit(0)
    -> syscall_handler()
    -> thread_current()->exit_status = 0
    -> process_exit()
    -> thread_exit()
```

담당 1 입장에서 중요한 건 아래 두 줄입니다.

```text
exit(0)
    -> syscall_handler()
    -> sys_exit(0)
```

즉 초반에는 `SYS_HALT`, `SYS_EXIT`, 포인터 검증 helper부터 잡는 게 가장 안정적입니다.

## `hello` 프로그램을 기준으로 보면 흐름이 어떻게 보이는가

Project 2를 처음 구현할 때는 `fork`, `exec`, `wait`보다 `hello`를 먼저 기준으로 잡는 것이 훨씬 이해하기 쉽습니다.

왜냐하면 `hello`는 가장 단순한 유저 프로그램이지만, 그래도 syscall handler 입장에서는 중요한 두 가지를 이미 보여주기 때문입니다.

```text
1. 유저 프로그램이 커널로 들어오는 순간
2. 유저 프로그램이 종료하는 순간
```

즉 `hello` 하나만 실행해도 담당 1은 아래 흐름을 따라가게 됩니다.

```text
pintos -p tests/userprog/hello -a hello -- -q run 'hello'
    -> 커널이 hello 실행 준비
    -> load("hello")
    -> 유저 모드 진입
    -> hello 프로그램 코드 실행 시작
    -> hello가 화면 출력용 syscall 호출
    -> syscall_handler() 진입
    -> 출력 처리 후 유저 모드 복귀
    -> hello가 exit(0) 호출
    -> syscall_handler() 진입
    -> sys_exit(0)
    -> process_exit()
    -> thread_exit()
```

여기서 핵심은 `hello`가 단순해 보여도, **유저 모드에서 커널 모드로 들어왔다가 다시 돌아가는 기본 왕복 구조**를 그대로 보여준다는 점입니다.

### `hello` 기준으로 syscall handler 1이 맡는 부분

`hello`를 실행할 때 담당 1이 직접 보게 되는 부분만 다시 뽑아보면 아래와 같습니다.

```text
hello 실행
    -> 유저 모드에서 프로그램 시작
    -> printf 또는 write 호출
    -> syscall entry
    -> syscall_handler(f)
    -> syscall 번호 확인
    -> 필요한 인자 확인
    -> 처리 후 유저 모드 복귀

hello 종료
    -> exit(0)
    -> syscall entry
    -> syscall_handler(f)
    -> SYS_EXIT case
    -> sys_exit(0)
    -> exit_status 저장
    -> process_exit()
    -> thread_exit()
```

즉 `hello` 기준으로 보면 담당 1은 최소한 아래 두 syscall 흐름을 안정적으로 잡아야 합니다.

```text
출력 syscall이 handler까지 잘 들어오는가
exit syscall이 handler에서 종료 경로로 잘 연결되는가
```

### `hello.c`가 대충 이런 코드라고 생각하면 된다

테스트 `hello`는 본질적으로 아래처럼 이해하면 충분합니다.

```c
int
main (void) {
  printf ("hello\n");
  return 0;
}
```

혹은 syscall 관점으로 더 단순화하면:

```text
main()
    -> write(1, "hello\n", 6)
    -> exit(0)
```

그래서 handler 관점에서는 결국 아래 순서로 보입니다.

```text
hello 시작
    -> write syscall
    -> exit syscall
```

### `hello`의 출력 시점에는 무슨 일이 일어나는가

`printf("hello\n")`는 내부적으로 결국 stdout에 대한 `write` syscall로 이어집니다.

흐름:

```text
hello의 printf("hello\n")
    -> lib/user/stdio 쪽 코드
    -> lib/user/syscall.c의 write wrapper
    -> syscall 발생
    -> syscall_handler()
    -> SYS_WRITE 분기
    -> fd == 1 확인
    -> check_user_buffer(buffer, size, false)
    -> putbuf(buffer, size)
    -> 유저 모드 복귀
```

이 단계는 담당 2의 범위가 더 크지만, 담당 1 입장에서도 아주 중요합니다.  
왜냐하면 "syscall 번호가 handler까지 잘 들어온다"는 사실을 가장 처음 눈으로 확인할 수 있는 지점이기 때문입니다.

### `hello`의 종료 시점에는 무슨 일이 일어나는가

출력 후 `hello`는 결국 종료합니다. `main`이 `return 0;`을 하든 내부적으로 `exit(0)`을 하든, handler 관점에서는 종료 syscall로 들어온다고 보면 됩니다.

흐름:

```text
hello 종료
    -> exit(0)
    -> syscall 발생
    -> syscall_handler(f)
    -> f->R.rax == SYS_EXIT 확인
    -> status = f->R.rdi
    -> sys_exit(status)
    -> thread_current()->exit_status = status
    -> process_exit()
    -> thread_exit()
```

이 지점이 담당 1의 가장 첫 완성 목표입니다.

```text
유저 프로그램이 exit(0)을 호출하면
커널이 그 값을 저장하고
정상적으로 종료 경로로 들어가야 한다
```

### `hello`로 먼저 확인할 수 있는 것

`hello` 하나만으로도 아래 체크가 가능합니다.

```text
유저 프로그램이 실제로 로드되는가
syscall entry가 동작하는가
syscall_handler()가 불리는가
레지스터에서 syscall 번호를 읽을 수 있는가
최소한 exit syscall을 분기할 수 있는가
종료 시 exit_status를 저장할 수 있는가
thread_exit()로 정상 종료되는가
```

즉 `hello`는 단순한 테스트가 아니라, 담당 1 기준으로는 **syscall 입구가 살아 있는지 확인하는 첫 smoke test**에 가깝습니다.

### 왜 `hello` 다음에 `exec`, `wait`, `fork`로 가는가

`hello`는 단일 프로세스입니다. 그래서 아직 부모-자식 관계나 복잡한 문자열 복사, 동기화 문제는 크게 드러나지 않습니다.

하지만 `hello`로 아래 기본 구조를 먼저 확인할 수 있습니다.

```text
유저 모드 -> 커널 모드 진입 가능
syscall_handler() 분기 가능
exit 경로 동작 가능
```

이게 확인된 뒤에야 아래처럼 확장하는 게 자연스럽습니다.

```text
hello 통과
    -> exec("child-simple")
    -> 문자열 포인터 검증 필요

exec/wait 통과
    -> fork()
    -> 부모-자식 반환값, 동기화 필요
```

즉 `hello`는 끝이 아니라 출발점입니다.

### `hello` 기준 한 줄 flow

정말 짧게 압축하면 담당 1 입장에서는 이렇게 보면 됩니다.

```text
hello 실행
    -> 유저 모드 시작
    -> write syscall 한 번 들어옴
    -> exit syscall 한 번 들어옴
    -> syscall_handler()가 이를 받아 처리
    -> exit_status 저장 후 프로세스 종료
```

## syscall 번호와 인자는 어디서 읽는가

모든 syscall은 `struct intr_frame *f` 안에 들어온 레지스터 값을 통해 해석합니다.

```text
rax -> syscall 번호
rdi -> 1번째 인자
rsi -> 2번째 인자
rdx -> 3번째 인자
r10 -> 4번째 인자
r8  -> 5번째 인자
r9  -> 6번째 인자
```

즉 흐름은 단순합니다.

```text
syscall_handler(f)
    -> f->R.rax 확인
    -> switch 분기
    -> 필요한 인자는 f->R.rdi, f->R.rsi ... 에서 꺼냄
    -> 반환값은 다시 f->R.rax에 넣음
```

예를 들면:

```c
switch (f->R.rax) {
  case SYS_EXIT:
    sys_exit((int) f->R.rdi);
    break;
  case SYS_WAIT:
    f->R.rax = process_wait((tid_t) f->R.rdi);
    break;
}
```

## 유저 포인터 검증은 왜 필요한가

유저 프로그램은 문자열 주소나 버퍼 주소를 커널에 넘길 수 있습니다.

예:

```c
exec("child-simple");
exec(NULL);
exec((char *) 0x20101234);
```

커널이 이 주소를 그대로 따라가면, 잘못된 주소 때문에 커널이 죽을 수 있습니다. 그래서 handler 단계에서 먼저 걸러야 합니다.

확인해야 하는 기준은 아래와 같습니다.

```text
주소가 NULL인가?
    YES -> exit(-1)

유저 가상 주소 영역인가?
    NO -> exit(-1)

현재 프로세스의 페이지 테이블에 매핑되어 있는가?
    NO -> exit(-1)

문자열이면 '\0'까지 안전하게 읽을 수 있는가?
    NO -> exit(-1)

버퍼면 size 범위 전체가 접근 가능한가?
    NO -> exit(-1)
```

특히 많이 하는 실수는 이것입니다.

```text
buffer 시작 주소만 검사함
    -> 중간에 페이지 경계를 넘는 invalid 접근을 놓침

문자열 첫 글자만 검사함
    -> 문자열 끝으로 갈수록 invalid page를 밟을 수 있음
```

그래서 보통 helper를 아래처럼 나눠서 생각하면 편합니다.

```c
void check_user_ptr(const void *uaddr);
void check_user_buffer(const void *buffer, size_t size, bool writable);
char *copy_in_string(const char *uaddr);
```

이 helper들은 결국 담당 1이 직접 구현해야 하는 함수들입니다.  
즉 문서에서 이름만 보는 게 아니라, 실제로 아래 흐름을 코드로 만들게 됩니다.

```text
exec("child-simple")
    -> syscall_handler()
    -> check_user_ptr(file_name)
    -> copy_in_string(file_name)
    -> process_exec(kernel_copy)

read(fd, buffer, size)
    -> syscall_handler()
    -> check_user_buffer(buffer, size, true)
    -> file_read(...)

write(fd, buffer, size)
    -> syscall_handler()
    -> check_user_buffer(buffer, size, false)
    -> file_write(...) 또는 putbuf(...)
```

## handler에서 직접 다 구현해야 하는가

아니요. 담당 1의 핵심은 “입구와 연결”입니다.  
복잡한 프로세스 로직은 `process.c` 쪽 backend에 넘기는 편이 훨씬 안정적입니다.

```text
handler가 직접 해야 할 것
    -> syscall 번호 분기
    -> 인자 추출
    -> 포인터 검증
    -> 필요한 경우 문자열 복사
    -> backend 호출
    -> 반환값 저장

backend에 넘길 것
    -> 프로세스 생성/복제
    -> 부모-자식 wait 동기화
    -> exec 로딩
    -> 실제 종료 처리의 큰 흐름
```

즉 좋은 구조는 아래에 가깝습니다.

```text
syscall_handler()
    -> 얇고 명확한 dispatcher

process_fork(), process_exec(), process_wait()
    -> 실제 복잡한 로직 담당
```

## 포인터 검증 helper를 어떻게 구현하는가

여기부터는 "왜 필요한가"를 넘어서, **내가 실제로 어떤 helper를 구현해야 하는가**에 집중해서 보면 됩니다.

### 1. `check_user_ptr()`

이 함수는 "이 주소 한 칸을 커널이 읽어도 되는가"를 확인하는 가장 기본 단위입니다.

```c
void check_user_ptr(const void *uaddr) {
  // 1. 유저가 NULL 포인터를 넘겼는지 먼저 확인한다.
  //    NULL은 아무 메모리도 가리키지 않으므로 커널이 읽으면 안 된다.

  // 2. 주소가 유저 영역인지 확인한다.
  //    커널 영역 주소를 유저 프로그램이 넘기면 커널 내부 메모리를 건드릴 수 있으므로 막아야 한다.

  // 3. 현재 프로세스의 페이지 테이블에서 실제로 매핑된 주소인지 확인한다.
  //    주소 숫자가 유저 영역처럼 보여도 실제 메모리와 연결되어 있지 않을 수 있다.

  // 4. 하나라도 실패하면 exit_with_status(-1)을 호출한다.
  //    잘못된 포인터는 보통 현재 프로세스를 exit(-1)로 종료시키는 정책을 사용한다.
}
```

왜 필요한가:

```text
exec(NULL) 같은 입력을 막아야 함
유저가 커널 주소를 넘겨 커널 메모리를 건드리지 못하게 해야 함
유저 영역처럼 보여도 unmapped page일 수 있으므로 실제 매핑을 확인해야 함
```

보통 같이 쓰는 함수:

- `is_user_vaddr()`
- `pml4_get_page()`
- `thread_current()`
- `exit_with_status()`

사용 흐름:

```text
유저 포인터 전달
    -> check_user_ptr(uaddr)
    -> 안전하면 다음 단계 진행
    -> 아니면 exit_with_status(-1)
```

### 2. `check_user_buffer()`

이 함수는 버퍼 시작 주소만 보는 것이 아니라, `size` 범위 전체가 안전한지 확인하는 함수입니다.

```c
void check_user_buffer(const void *buffer, size_t size, bool writable) {
  // 1. size가 0이면 바로 통과시킬지 먼저 정한다.
  //    read/write에서 size 0은 보통 아무 일도 하지 않고 0을 반환한다.

  // 2. buffer 시작 주소가 유효한지 확인한다.

  // 3. buffer부터 buffer + size - 1까지 전체 범위를 검사한다.
  //    특히 페이지 경계를 넘는 경우를 놓치지 않도록 확인해야 한다.

  // 4. 구현은 보통 페이지 단위로 점프하면서 검사하거나,
  //    단순하게 한 바이트씩 검사하는 방식으로 시작할 수 있다.

  // 5. 하나라도 invalid면 exit_with_status(-1)을 호출한다.
}
```

왜 필요한가:

```text
버퍼 시작 주소만 정상이고
중간부터 다음 페이지가 invalid일 수 있음
```

예:

```text
read(fd, buf, 100)
    -> buf는 현재 페이지 마지막 20바이트 지점
    -> 나머지 80바이트는 다음 페이지로 넘어감
    -> 다음 페이지가 unmapped라면 커널이 중간에 죽을 수 있음
```

그래서 이 함수는 특히 아래 테스트와 연결됩니다.

- `read-boundary`
- `write-boundary`
- `create-bound`
- `exec-boundary`

사용 흐름:

```text
read(fd, buffer, size)
    -> syscall_handler()
    -> check_user_buffer(buffer, size, true)
    -> 안전하면 file_read()

write(fd, buffer, size)
    -> syscall_handler()
    -> check_user_buffer(buffer, size, false)
    -> 안전하면 file_write() 또는 putbuf()
```

### 3. `copy_in_string()`

이 함수는 유저 문자열을 커널 메모리로 안전하게 복사하는 함수입니다.

```c
char *copy_in_string(const char *uaddr) {
  // 1. 문자열의 첫 주소가 유효한지 check_user_ptr()로 확인한다.

  // 2. 문자열 끝 표시인 '\0'을 만날 때까지 한 글자씩 확인한다.
  //    strlen(uaddr)를 먼저 쓰면 안 된다. strlen 자체가 유저 메모리를 읽기 때문이다.

  // 3. 각 글자 주소마다 check_user_ptr(uaddr + i)를 호출한다.
  //    문자열이 페이지 경계를 넘어갈 수 있으므로 시작 주소만 보면 부족하다.

  // 4. 문자열 길이를 알았으면 malloc(len + 1)로 커널 메모리를 확보한다.
  //    +1은 마지막 '\0'까지 복사하기 위한 공간이다.

  // 5. 유저 문자열을 커널 버퍼로 복사한다.
  //    이후 커널 내부 함수에는 원본 유저 포인터가 아니라 이 커널 버퍼를 넘긴다.

  // 6. 복사한 커널 문자열 포인터를 반환한다.
  //    이 메모리는 사용이 끝난 뒤 free()로 해제해야 한다.

  // return kbuf;
}
```

왜 필요한가:

```text
exec(), open(), create()는 문자열 이름을 받는다
유저 문자열을 그대로 커널 함수에 넘기면 boundary와 invalid pointer 문제를 피하기 어렵다
안전한 커널 버퍼로 복사해 두면 이후 로직이 단순해진다
```

자주 연결되는 함수:

- `malloc()`
- `free()`
- `process_exec()`
- `filesys_open()`
- `filesys_create()`
- `filesys_remove()`

사용 흐름:

```text
exec(user_string)
    -> check_user_ptr(user_string)
    -> copy_in_string(user_string)
    -> process_exec(kernel_copy)
    -> 사용 후 free()
```

### 4. `exit_with_status()`

포인터 검증 helper는 실패 시 결국 현재 프로세스를 종료해야 합니다.  
그래서 보통 공통 종료 함수를 하나 두면 중복을 줄일 수 있습니다.

```c
void exit_with_status(int status) {
  // 1. thread_current()로 현재 실행 중인 thread를 가져온다.
  //    Pintos에서는 유저 프로세스 하나가 thread 하나로 표현된다.

  // 2. 현재 thread의 exit_status에 status를 저장한다.
  //    부모가 wait(child)에서 자식의 종료 값을 받을 때 이 값이 필요하다.

  // 3. child_info 구조체를 쓰고 있다면 child_info->exit_status에도 복사한다.
  //    자식 thread는 종료 후 사라질 수 있으므로 부모가 읽을 값은 별도 구조체에 남기는 것이 안전하다.

  // 4. thread_exit()을 호출한다.
  //    thread_exit() 흐름에서 process_exit()이 호출되고, 열린 파일과 페이지 테이블 같은 자원을 정리한다.
}
```

왜 필요한가:

```text
잘못된 포인터를 만날 때마다
handler 곳곳에서 exit 처리 코드를 중복해서 쓰지 않기 위해
```

사용 흐름:

```text
invalid user pointer 발견
    -> check_user_ptr() 실패 또는 check_user_buffer() 실패
    -> exit_with_status(-1)
    -> thread_current()->exit_status = -1
    -> process_exit()
    -> thread_exit()
```

### helper 구현 순서 추천

이 helper들은 아래 순서로 만들면 덜 꼬입니다.

```text
1. exit_with_status()
2. check_user_ptr()
3. copy_in_string()
4. check_user_buffer()
```

이 순서를 추천하는 이유:

```text
exit_with_status()가 있어야 나머지 helper가 실패 시 공통 경로를 쓸 수 있음
check_user_ptr()가 가장 기본 단위라서 나머지 함수가 재사용 가능
copy_in_string()은 check_user_ptr() 위에 쌓기 쉬움
check_user_buffer()는 페이지 경계 처리 때문에 가장 까다로운 편
```

## 작업 1. `syscall_handler()` 기본 뼈대 만들기

맨 처음에는 아래처럼 빈 뼈대부터 만드는 것이 좋습니다.

```c
static void
syscall_handler (struct intr_frame *f) {
  switch (f->R.rax) {
    case SYS_HALT:
      break;
    case SYS_EXIT:
      break;
    case SYS_EXEC:
      break;
    case SYS_WAIT:
      break;
    case SYS_FORK:
      break;
    default:
      sys_exit(-1);
  }
}
```

여기서 중요한 것은 처음부터 완성된 로직을 쓰는 게 아니라, **분기 위치를 먼저 잡는 것**입니다.

## 작업 2. `halt`부터 연결하기

`halt()`는 가장 단순한 syscall입니다.

```text
유저 프로그램이 halt() 호출
    -> syscall_handler()
    -> SYS_HALT case 진입
    -> power_off()
```

즉 handler 입장에서는 사실상 한 줄짜리 연결입니다.

```c
case SYS_HALT:
  power_off();
  break;
```

이 단계는 syscall 분기가 실제로 동작하는지 확인하는 첫 체크포인트입니다.

## 작업 3. `exit` 연결하기

`exit(status)`는 현재 프로세스를 종료하는 syscall입니다.

흐름:

```text
유저 프로그램이 exit(57) 호출
    -> syscall_handler()
    -> status = f->R.rdi
    -> sys_exit(status)
    -> thread_current()->exit_status = 57
    -> process_exit()
    -> thread_exit()
```

이때 handler 안에서 모든 종료 처리를 길게 쓰기보다는, 공통 함수로 빼 두는 편이 좋습니다.

```c
void sys_exit(int status);
```

이 함수가 보통 맡는 일:

```text
현재 thread의 exit_status 저장
부모가 나중에 wait할 수 있도록 종료 상태 반영
종료 메시지 출력 준비
thread_exit() 호출
```

초기 구현에서는 최소한 아래 두 개가 보장되면 됩니다.

```text
thread_current()->exit_status = status
thread_exit()
```

## 작업 4. `exec` 연결하기

`exec()`는 현재 프로세스를 다른 프로그램으로 교체합니다.

흐름은 아래처럼 이해하면 됩니다.

```text
현재 프로세스가 exec("child-simple") 호출
    -> syscall_handler()
    -> 유저 문자열 주소 검사
    -> 문자열을 커널 버퍼로 복사
    -> process_exec(copied_name)
    -> 성공하면 기존 흐름으로 돌아오지 않음
    -> 실패하면 -1 반환
```

여기서 제일 중요한 건 문자열 복사입니다.

왜냐하면:

```text
유저 메모리의 문자열 주소를 그대로 process_exec()에 넘기면
    -> 그 사이 주소가 invalid해질 수 있고
    -> boundary 문제도 놓칠 수 있기 때문
```

그래서 보통 이런 구조가 안전합니다.

```text
exec(user_ptr)
    -> check_user_vaddr(user_ptr)
    -> copy_in_string(user_ptr)
    -> process_exec(kernel_copy)
```

주의할 점:

```text
exec(NULL)
    -> 포인터 오류이므로 exit(-1)

문자열이 페이지 경계를 넘음
    -> '\0'까지 복사하면서 전부 검사해야 함

없는 파일 실행
    -> 보통 process_exec() 쪽에서 -1 반환
```

## 작업 5. `wait` 연결하기

`wait(pid)`는 부모가 자식 종료를 기다릴 때 쓰는 syscall입니다.

handler가 해야 하는 일은 비교적 단순합니다.

```text
부모가 wait(child_pid) 호출
    -> syscall_handler()
    -> pid = f->R.rdi
    -> process_wait(pid)
    -> 반환값을 f->R.rax에 저장
```

실제 복잡한 일은 backend가 맡습니다.

```text
내 자식인지 확인
이미 wait 했는지 확인
자식이 종료했는지 확인
필요하면 semaphore로 block
끝나면 exit_status 반환
```

즉 handler 수준에서는 보통 아래 정도로 끝납니다.

```c
case SYS_WAIT:
  f->R.rax = process_wait((tid_t) f->R.rdi);
  break;
```

반환 규칙은 보통 이렇게 맞춥니다.

```text
정상 종료한 자식 -> exit_status
내 자식이 아님 -> -1
이미 wait 함 -> -1
비정상 종료 -> -1
```

## 작업 6. `fork` 연결하기

`fork()`는 현재 프로세스를 복제합니다.  
이건 담당 1 syscall 중 가장 무거운 요청이지만, handler 자체는 여전히 “연결”에 집중하면 됩니다.

흐름:

```text
부모 프로세스가 fork("name") 호출
    -> syscall_handler()
    -> 필요하면 name 문자열 검사
    -> process_fork(name, f)
    -> 부모는 자식 pid 반환
    -> 자식은 0 반환
```

중요한 건 부모와 자식의 반환값이 다르다는 점입니다.

```text
부모 쪽 return value
    -> child pid

자식 쪽 return value
    -> 0
```

그리고 handler가 주소 공간 복제나 fd 복제를 직접 하면 너무 복잡해집니다. 그건 `process_fork()`가 맡아야 합니다.

handler 관점의 핵심은 이것입니다.

```c
case SYS_FORK:
  f->R.rax = process_fork(name, f);
  break;
```

여기서 backend는 보통 추가로 이런 일을 합니다.

```text
부모 intr_frame 복사
페이지 테이블 복제
fd 테이블 복제
fork 성공/실패 semaphore 동기화
자식의 반환값을 0으로 세팅
```

## 논리 실패와 포인터 실패를 구분해야 하는가

네. 이 차이를 안 잡으면 구현이 꼬이기 쉽습니다.

```text
포인터 자체가 잘못됨
    -> 커널이 유저 메모리를 안전하게 읽을 수 없음
    -> exit(-1)

논리적으로 요청이 실패함
    -> 주소는 정상인데 의미상 실패
    -> 보통 -1 반환
```

예를 들면:

```text
exec(NULL)
    -> exit(-1)

exec("없는파일")
    -> -1 반환

wait(내 자식이 아닌 pid)
    -> -1 반환
```

## 추천 구현 순서

실제로는 아래 순서로 가는 게 가장 덜 막힙니다.

```text
1. syscall_handler() switch 뼈대 만들기
2. SYS_HALT 연결
3. SYS_EXIT 연결
4. 포인터 검증 helper 만들기
5. SYS_EXEC 연결
6. SYS_WAIT 연결
7. SYS_FORK 연결
```

이 순서를 추천하는 이유는 다음과 같습니다.

```text
halt, exit
    -> 가장 단순해서 입구가 맞는지 확인하기 좋음

exec
    -> 문자열 포인터 검증 helper를 빨리 검증할 수 있음

wait
    -> backend 연결 구조를 단순하게 확인 가능

fork
    -> 제일 복잡하므로 마지막에 붙이는 편이 안정적
```

## 담당 1이 먼저 읽어야 할 파일

| 파일 | 보는 이유 |
| --- | --- |
| `userprog/syscall.c` | `syscall_handler()` 구현 위치 |
| `include/lib/syscall-nr.h` | syscall 번호 확인 |
| `lib/user/syscall.c` | 유저 쪽 syscall wrapper 확인 |
| `userprog/process.c` | `fork`, `exec`, `wait` backend 연결 위치 |
| `include/threads/thread.h` | `exit_status`, 부모-자식 상태 필드 확인 |
| `include/threads/init.h` | `power_off()` 확인 |
| `include/threads/vaddr.h` | 유저 주소 범위 확인 |
| `threads/mmu.c` | 주소 매핑 확인 흐름 이해 |

## 마지막 체크리스트

아래 체크리스트를 모두 만족하면 담당 1 뼈대는 꽤 안정적으로 잡힌 상태입니다.

```text
syscall_handler()가 switch 분기로 동작하는가
SYS_HALT가 power_off()로 연결되는가
SYS_EXIT가 exit_status 저장 후 thread_exit()로 이어지는가
유저 포인터를 바로 믿지 않고 helper로 검증하는가
문자열은 커널 메모리로 복사해서 process_exec()에 넘기는가
wait는 process_wait() 반환값을 그대로 rax에 넣는가
fork는 process_fork() 결과를 rax에 넣는가
반환값은 항상 f->R.rax로 되돌려주는가
```

## 한 줄 요약

`Syscall Handler 담당 1`의 핵심은 `syscall_handler()`를 안전한 입구로 만들고, `halt`, `exit`, `exec`, `wait`, `fork`를 포인터 검증과 함께 올바른 backend로 연결하는 것입니다.
