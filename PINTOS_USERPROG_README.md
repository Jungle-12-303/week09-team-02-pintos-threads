# PINTOS Userprog README

## 0챕터. 이번 주 의미

이번 주의 핵심은 **유저 프로그램을 안전하게 실행시키고, 위험한 작업은 직접 하지 못하게 막은 뒤 시스템 콜을 통해서만 커널에 부탁하게 만드는 것**입니다.  
그래서 `printf`를 먼저 보는 이유는 단순 출력 구현이 아니라, **user mode -> kernel mode -> 다시 user mode**로 이어지는 가장 짧은 성공 경로이기 때문입니다.  
즉 이번 주는 넓게 보면 `User Program`, `System Call`, `Virtual Memory Layout`을 배우지만, 실제 코드 시작점은 **`write` 시스템 콜을 통한 `printf` 경로 열기**입니다.

이번 주 구현 순서:

```text
1. 유저 프로그램이 어디서 시작되는지 본다
2. printf 경로를 먼저 뚫는다
3. argument passing을 붙인다
4. syscall을 일반화하고 fd table을 붙인다
5. user pointer 검증과 예외 처리를 붙인다
6. wait / fork / exec / rox 로 확장한다
```

---

## 1챕터. 유저 프로그램이 어디서 시작되는지 본다

### Flow

```text
커널 부팅
   │
   ▼
threads/init.c
   │
   ▼
userprog/process.c
   │
   ├─▶ process_create_initd()
   ├─▶ initd()
   ├─▶ process_exec()
   ├─▶ load()
   └─▶ do_iret()
   │
   ▼
[USER MODE]
main() 시작
```

### 이번 단계의 봐야 할 포인트

- `main()`은 저절로 시작되지 않습니다.
- 커널이 유저 프로그램을 메모리에 올리고, 마지막에 user mode로 넘겨줘야 합니다.
- 나중에 `printf`, `argv`, `exec`를 볼 때도 결국 다시 여기로 돌아오게 됩니다.

### 1-1. `process_create_initd()`부터 본다

파일:
- [userprog/process.c](./userprog/process.c#L42)

줄 번호:
- [`userprog/process.c:42`](./userprog/process.c#L42)

#### 1-1-1. 이 함수의 현재 역할

- 첫 번째 유저 프로그램을 시작시키는 함수입니다.
- 실행 파일 이름을 복사하고 새 thread를 만듭니다.

#### 1-1-2. 지금 여기서 볼 것

- 유저 프로그램 시작이 `main()`이 아니라 `process_create_initd()`에서 출발한다는 점
- 새 thread를 만들어 `initd()`로 넘긴다는 점

#### 1-1-3. 이 함수에서 사용되는 함수

- `thread_create(...)`
- `initd()`

#### 1-1-4. 수정 포인트

- 이 단계에서는 보통 아직 직접 수정하지 않습니다.
- 흐름만 정확히 읽어두면 됩니다.

#### 1-1-5. 수정할 수 있는 힌트

- “유저 프로그램도 처음엔 커널이 실행시킨다”라고 생각하면 이해가 쉽습니다.

---

### 1-2. `initd()`를 본다

파일:
- [userprog/process.c](./userprog/process.c#L62)

줄 번호:
- [`userprog/process.c:62`](./userprog/process.c#L62)

#### 1-2-1. 이 함수의 현재 역할

- 새로 생성된 thread가 처음 들어가는 함수입니다.
- `process_exec()`를 호출해 실제 유저 프로그램 실행 준비를 맡깁니다.

#### 1-2-2. 지금 여기서 볼 것

- 새 thread가 바로 `main()`으로 가는 게 아니라 `initd()`를 거친다는 점
- `process_exec()`가 진짜 핵심 준비 함수라는 점

#### 1-2-3. 이 함수에서 사용되는 함수

- `process_init()`
- `process_exec()`

#### 1-2-4. 수정 포인트

- 이 단계에서도 보통 바로 수정하지 않습니다.
- 대신 `process_create_initd -> initd -> process_exec` 흐름을 기억합니다.

#### 1-2-5. 수정할 수 있는 힌트

- `initd()`는 “새 thread의 시작 함수”라고 보면 됩니다.

---

### 1-3. `process_exec()`를 본다

파일:
- [userprog/process.c](./userprog/process.c#L164)

줄 번호:
- [`userprog/process.c:164`](./userprog/process.c#L164)

#### 1-3-1. 이 함수의 현재 역할

- 유저 프로그램 실행 직전의 핵심 함수입니다.
- `load()`를 호출하고, `intr_frame`을 준비하고, 마지막에 `do_iret()`로 user mode에 들어갑니다.

#### 1-3-2. 지금 여기서 볼 것

- 여기서부터 `printf`, `argv`, `exec`가 다 연결된다는 점
- `do_iret()`가 user mode 진입 직전 마지막 단계라는 점

#### 1-3-3. 이 함수에서 사용되는 함수

- `process_cleanup()`
- `load()`
- `do_iret()`

#### 1-3-4. 수정 포인트

- 지금은 흐름만 읽습니다.
- 하지만 3챕터 argument passing에서 다시 여기로 돌아와 수정 흐름을 보게 됩니다.

#### 1-3-5. 수정할 수 있는 힌트

- `main()`보다 먼저 `process_exec()`를 떠올리면 전체가 덜 헷갈립니다.

---

### 1-4. `process_activate()`를 본다

파일:
- [userprog/process.c](./userprog/process.c#L252)

줄 번호:
- [`userprog/process.c:252`](./userprog/process.c#L252)

#### 1-4-1. 이 함수의 현재 역할

- 지금 실행할 thread의 유저 주소 공간을 활성화합니다.
- kernel stack 관련 정보도 갱신합니다.

#### 1-4-2. 지금 여기서 볼 것

- 왜 user stack과 kernel stack이 나뉘는지
- 왜 프로세스마다 주소 공간이 따로 필요한지

#### 1-4-3. 이 함수에서 사용되는 함수

- `pml4_activate()`
- `tss_update()`

#### 1-4-4. 수정 포인트

- 이 단계에서는 보통 수정하지 않습니다.
- virtual memory layout을 이해할 때 다시 중요해집니다.

#### 1-4-5. 수정할 수 있는 힌트

- “유저 프로그램마다 자기 주소 공간을 켠다” 정도로 먼저 이해하면 충분합니다.

### 중간 테스트

- 아직 테스트를 통과시키는 단계는 아닙니다.
- 대신 아래 문장을 말로 설명할 수 있으면 됩니다.

```text
process_create_initd()
-> initd()
-> process_exec()
-> load()
-> do_iret()
-> main()
```

---

## 2챕터. `printf` 경로를 먼저 뚫는다

### Flow

```text
[USER MODE]
main()
   │
   ▼
printf()
   │
   ▼
lib/user/console.c
   │
   └─▶ write(fd=1, buffer, size)
   │
   ▼
lib/user/syscall.c
   │
   ├─▶ rax = SYS_WRITE
   ├─▶ rdi = fd
   ├─▶ rsi = buffer
   ├─▶ rdx = size
   └─▶ syscall
   │
   ▼
[KERNEL MODE]
userprog/syscall.c
   │
   ▼
syscall_handler()
   │
   ├─▶ syscall 번호 확인
   ├─▶ SYS_WRITE 분기
   ├─▶ fd / buffer / size 읽기
   └─▶ fd == 1 이면 putbuf() 호출
   │
   ▼
lib/kernel/console.c
   │
   ▼
putbuf(buffer, size)
   │
   ▼
화면 출력
```

### 이번 단계의 봐야 할 포인트

- `printf`는 직접 화면에 찍는 함수가 아닙니다.
- 유저 프로그램은 출력도 직접 못 하고, 커널에 system call로 부탁해야 합니다.
- 이번 단계의 진짜 목표는 **출력 기능**이 아니라 **user -> kernel 진입 경로 확인**입니다.

### 2-1. `printf`가 어디로 가는지 먼저 본다

파일:
- [lib/user/console.c](./lib/user/console.c#L30)

줄 번호:
- [`lib/user/console.c:30`](./lib/user/console.c#L30)

#### 2-1-1. 이 함수의 현재 역할

- 유저 프로그램 쪽 출력 시작점입니다.
- 결국 `write(STDOUT_FILENO, ...)`로 연결됩니다.

#### 2-1-2. 지금 여기서 볼 것

- `printf`가 결국 `write`를 호출한다는 점
- 유저 프로그램은 출력도 syscall 경유라는 점

#### 2-1-3. 이 함수에서 사용되는 함수

- `write()`

#### 2-1-4. 수정 포인트

- 보통 여기 자체를 수정하지는 않습니다.
- 대신 흐름 출발점으로 봅니다.

#### 2-1-5. 수정할 수 있는 힌트

- `printf`를 보면 바로 아래에서 `write`를 찾는 습관을 들이면 좋습니다.

---

### 2-2. `write()`가 어떻게 syscall로 바뀌는지 본다

파일:
- [lib/user/syscall.c](./lib/user/syscall.c#L123)

줄 번호:
- [`lib/user/syscall.c:123`](./lib/user/syscall.c#L123)

같이 볼 위치:
- [lib/user/syscall.c](./lib/user/syscall.c#L6)

줄 번호:
- [`lib/user/syscall.c:6`](./lib/user/syscall.c#L6)

#### 2-2-1. 이 함수의 현재 역할

- 유저 프로그램이 커널에게 출력 요청을 보내는 래퍼 함수입니다.
- `SYS_WRITE` 번호와 인자를 레지스터에 실어 `syscall` 명령을 실행합니다.

#### 2-2-2. 지금 여기서 볼 것

- `rax`에는 syscall 번호가 들어간다는 점
- `rdi`, `rsi`, `rdx`에 인자가 들어간다는 점

#### 2-2-3. 이 함수에서 사용되는 함수

- `syscall()`
- `SYS_WRITE`

#### 2-2-4. 수정 포인트

- 보통 여기 자체는 수정하지 않습니다.
- 대신 커널에서 이 값을 어떻게 받아야 하는지 이해합니다.

#### 2-2-5. 수정할 수 있는 힌트

- 지금 단계에서는 “번호는 `rax`, 첫 세 인자는 `rdi/rsi/rdx`”만 기억해도 충분합니다.

---

### 2-3. `SYS_WRITE` 번호표를 확인한다

파일:
- [include/lib/syscall-nr.h](./include/lib/syscall-nr.h#L17)

줄 번호:
- [`include/lib/syscall-nr.h:17`](./include/lib/syscall-nr.h#L17)

#### 2-3-1. 이 파일의 현재 역할

- 시스템 콜 번호표입니다.
- 유저와 커널이 같은 번호를 공유할 때 씁니다.

#### 2-3-2. 지금 여기서 볼 것

- `SYS_WRITE` 번호
- 나중에 붙일 `SYS_EXIT`, `SYS_EXEC`, `SYS_WAIT` 번호

#### 2-3-3. 이 파일에서 사용되는 함수

- `write()`
- `syscall_handler()`

#### 2-3-4. 수정 포인트

- 보통 여기서는 수정하지 않습니다.
- 번호를 확인하는 용도입니다.

#### 2-3-5. 수정할 수 있는 힌트

- syscall 구현 중 번호가 헷갈리면 항상 여기로 돌아오면 됩니다.

---

### 2-4. `syscall_handler()`를 실제로 수정한다

파일:
- [userprog/syscall.c](./userprog/syscall.c#L42)

줄 번호:
- [`userprog/syscall.c:42`](./userprog/syscall.c#L42)

#### 2-4-1. 이 함수의 현재 역할

- 커널이 system call 요청을 실제로 받는 중앙 함수입니다.
- 지금은 `"system call!"`만 찍고 끝나는 상태입니다.

#### 2-4-2. 이 함수에서 해야 할 일

1. syscall 번호를 읽습니다.
2. `SYS_WRITE`인지 확인합니다.
3. `fd`, `buffer`, `size`를 읽습니다.
4. `fd == 1`이면 `putbuf(buffer, size)`를 호출합니다.
5. 반환값에 `size`를 저장합니다.

#### 2-4-3. 이 함수에서 사용되는 함수

- `putbuf()`
- 나중에 `check_user_ptr()` 같은 helper도 여기 붙습니다.

#### 2-4-4. 수정 포인트

- **이번 주 첫 번째 진짜 수정 포인트**입니다.
- 처음에는 `fd == 1`만 처리하면 됩니다.

#### 2-4-5. 수정할 수 있는 힌트

- 처음부터 일반 파일 쓰기까지 다 하지 마세요.
- stdout만 되면 됩니다.
- `buffer`는 시작 주소, `size`는 몇 글자인지만 먼저 생각하면 됩니다.

---

### 2-5. 실제 출력이 끝나는 곳을 본다

파일:
- [lib/kernel/console.c](./lib/kernel/console.c#L143)

줄 번호:
- [`lib/kernel/console.c:143`](./lib/kernel/console.c#L143)

#### 2-5-1. 이 함수의 현재 역할

- 커널이 실제 콘솔에 문자열을 출력하는 함수입니다.

#### 2-5-2. 지금 여기서 볼 것

- 유저 프로그램의 `printf`가 결국 여기까지 와야 한다는 점

#### 2-5-3. 이 함수에서 사용되는 함수

- `syscall_handler()`

#### 2-5-4. 수정 포인트

- 보통 여기 자체는 수정하지 않습니다.
- “출력의 마지막 목적지”로 이해합니다.

#### 2-5-5. 수정할 수 있는 힌트

- `printf -> write -> syscall_handler -> putbuf` 한 줄 흐름을 꼭 기억하세요.

### 중간 테스트

추천 테스트:
- 먼저 `syscall_handler()`가 `SYS_WRITE`를 받았을 때 진짜 출력이 되는지 확인
- `tests/userprog`의 간단한 출력 프로그램을 돌려 문자열이 보이는지 확인

완료 기준:
- `"system call!"`만 찍고 끝나지 않음
- 유저 프로그램의 출력 문자열이 실제로 보임

---

## 3챕터. Argument Passing을 붙인다

### Flow

```text
process_exec(file_name)
   │
   ▼
load()
   │
   ├─▶ ELF 읽기
   ├─▶ segment 적재
   ├─▶ setup_stack()
   └─▶ argv / argc를 user stack에 배치
   │
   ▼
do_iret()
   │
   ▼
[USER MODE]
main(argc, argv)
```

### 이번 단계의 봐야 할 포인트

- 유저 프로그램이 실행만 되면 끝이 아닙니다.
- `main(argc, argv)`에서 인자를 읽을 수 있어야 합니다.
- 이 단계 핵심은 문자열 파싱보다 **스택에 무엇을 어떤 순서로 놓는지**입니다.

### 3-1. `process_exec()`를 다시 본다

파일:
- [userprog/process.c](./userprog/process.c#L164)

줄 번호:
- [`userprog/process.c:164`](./userprog/process.c#L164)

#### 3-1-1. 이 함수의 현재 역할

- 유저 프로그램 실행 직전 준비 함수입니다.
- `load()`를 호출하고 마지막에 `do_iret()`로 넘깁니다.

#### 3-1-2. 지금 여기서 볼 것

- argument passing이 결국 `load()`와 연결된다는 점
- `process_exec()`가 전체 흐름의 시작점이라는 점

#### 3-1-3. 이 함수에서 사용되는 함수

- `load()`
- `do_iret()`

#### 3-1-4. 수정 포인트

- 여기 자체를 크게 바꾸기보다 `load()`로 이어지는 흐름을 이해합니다.

#### 3-1-5. 수정할 수 있는 힌트

- “인자 처리는 `process_exec` 안이 아니라 `load`와 stack 쪽에서 한다”라고 기억하면 됩니다.

---

### 3-2. `load()`를 중심으로 수정한다

파일:
- [userprog/process.c](./userprog/process.c)

줄 번호:
- `userprog/process.c` 안의 `load()` 정의 부분

#### 3-2-1. 이 함수의 현재 역할

- ELF 헤더를 읽습니다.
- Program Header를 따라 segment를 메모리에 적재합니다.
- stack 생성 흐름도 여기와 연결됩니다.

#### 3-2-2. 이 함수에서 해야 할 일

1. 실행 파일 이름과 인자 문자열을 나눕니다.
2. 문자열들을 user stack에 복사합니다.
3. `argv[]` 포인터 배열을 올립니다.
4. `argc`를 저장합니다.
5. fake return address를 올립니다.
6. stack alignment를 맞춥니다.

#### 3-2-3. 이 함수에서 사용되는 함수

- `setup_stack()`
- 문자열 복사 로직
- stack 정렬 로직

#### 3-2-4. 수정 포인트

- 이 챕터의 핵심 수정 포인트입니다.
- `args-*` 테스트는 거의 여기서 갈립니다.

#### 3-2-5. 수정할 수 있는 힌트

- 문자열을 먼저 쌓고, 그 문자열들을 가리키는 포인터를 나중에 쌓는 방식으로 생각하면 쉽습니다.
- `argv[argc] = NULL`을 빼먹지 마세요.
- 스택은 아래 방향으로 자랍니다.

---

### 3-3. `setup_stack()`을 본다

파일:
- [userprog/process.c](./userprog/process.c)

줄 번호:
- `userprog/process.c` 안의 `setup_stack()` 정의 부분

#### 3-3-1. 이 함수의 현재 역할

- 비어 있는 유저 스택 페이지를 만들고 초기 stack pointer를 잡아줍니다.

#### 3-3-2. 지금 여기서 볼 것

- argument passing의 실제 작업 공간이 user stack이라는 점
- 스택 페이지가 준비된 뒤 그 위에 `argv`, `argc`가 올라간다는 점

#### 3-3-3. 이 함수에서 사용되는 함수

- 페이지 할당 함수
- 페이지 매핑 함수

#### 3-3-4. 수정 포인트

- 필요하면 stack 준비 로직과 argument pushing 로직이 잘 맞도록 같이 봅니다.

#### 3-3-5. 수정할 수 있는 힌트

- `setup_stack()`은 “빈 도화지 준비”, `load()` 쪽은 “그 위에 인자 배치”라고 생각하면 됩니다.

### 중간 테스트

추천 테스트:
- `args-none`
- `args-single`
- `args-multiple`

완료 기준:
- `main(argc, argv)`에서 인자가 정상적으로 보임
- `args-*` 테스트 결과를 말로 설명할 수 있음

---

## 4챕터. syscall을 일반화하고 fd table을 붙인다

### Flow

```text
[USER MODE]
write / read / exit / open / close / exec / wait
   │
   ▼
syscall_handler()
   │
   ├─▶ 번호별 분기
   ├─▶ 인자 해석
   ├─▶ fd 사용
   └─▶ 결과 반환
```

### 이번 단계의 봐야 할 포인트

- `printf`는 사실 `write(fd=1)`의 특별한 경우일 뿐입니다.
- 이제는 `read`, `open`, `close`, `exit` 같은 다른 syscall도 같은 방식으로 처리해야 합니다.
- file descriptor는 “열린 파일을 가리키는 번호”입니다.

### 4-1. `syscall_handler()`를 확장한다

파일:
- [userprog/syscall.c](./userprog/syscall.c#L42)

줄 번호:
- [`userprog/syscall.c:42`](./userprog/syscall.c#L42)

#### 4-1-1. 이 함수의 현재 역할

- 모든 system call 요청이 모이는 중앙 dispatcher입니다.

#### 4-1-2. 이 함수에서 해야 할 일

- `SYS_HALT`
- `SYS_EXIT`
- `SYS_READ`
- `SYS_WRITE`
- `SYS_OPEN`
- `SYS_CLOSE`
- `SYS_EXEC`
- `SYS_WAIT`

같은 번호별 분기를 추가합니다.

#### 4-1-3. 이 함수에서 사용되는 함수

- `power_off()`
- `thread_exit()`
- 파일 시스템 함수들
- `process_exec()`
- `process_wait()`

#### 4-1-4. 수정 포인트

- `write` 하나만 처리하던 상태에서 일반적인 syscall 분기 구조로 키웁니다.

#### 4-1-5. 수정할 수 있는 힌트

- 처음부터 다 붙이지 말고 아래 순서로 가면 편합니다.

```text
exit
-> read
-> write
-> open
-> close
-> exec
-> wait
```

---

### 4-2. `thread.h`에 fd table 필드를 추가한다

파일:
- [include/threads/thread.h](./include/threads/thread.h)

줄 번호:
- `include/threads/thread.h`

#### 4-2-1. 이 구조체의 현재 역할

- thread / process가 들고 다니는 상태를 저장합니다.

#### 4-2-2. 이 구조체에서 해야 할 일

- 프로세스마다 열린 파일 목록을 관리할 필드를 추가합니다.

예:
- `fd_table`
- `next_fd`

#### 4-2-3. 이 구조체를 사용하는 함수

- `open()`
- `read()`
- `write()`
- `close()`

#### 4-2-4. 수정 포인트

- file descriptor는 프로세스별 상태라서 여기 저장해야 합니다.

#### 4-2-5. 수정할 수 있는 힌트

- `fd == 0`은 stdin, `fd == 1`은 stdout, `2`부터 일반 파일이라고 생각하면 정리가 쉽습니다.

---

### 4-3. `open/read/write/close` 로직을 붙인다

파일:
- [userprog/syscall.c](./userprog/syscall.c#L42)

줄 번호:
- [`userprog/syscall.c:42`](./userprog/syscall.c#L42)

#### 4-3-1. 이 함수의 현재 역할

- syscall 분기 중심 함수입니다.

#### 4-3-2. 이 함수에서 해야 할 일

- `fd == 0`이면 stdin 처리
- `fd == 1`이면 stdout 처리
- 그 외에는 `fd_table`에서 실제 파일 찾기
- `close()` 시 파일 닫기와 table 정리

#### 4-3-3. 이 함수에서 사용되는 함수

- `filesys_open()`
- `file_read()`
- `file_write()`
- `file_close()`

#### 4-3-4. 수정 포인트

- stdout 출력에서 일반 파일 입출력으로 확장하는 단계입니다.

#### 4-3-5. 수정할 수 있는 힌트

- `write(fd=1)`이 이미 되면, 그다음은 “일반 파일이면 어디서 찾지?”만 추가로 생각하면 됩니다.

### 중간 테스트

추천 테스트:
- `halt`
- `exit`
- `open-*`
- `read-*`
- `write-*`
- `close-*`

완료 기준:
- stdout 말고 일반 파일도 fd를 통해 다루는 구조가 보임
- fd table이 왜 필요한지 설명 가능

---

## 5챕터. user pointer 검증 / 예외 처리를 붙인다

### Flow

```text
[USER MODE]
유저 포인터 전달
   │
   ▼
syscall_handler()
   │
   ├─▶ 포인터 검사
   ├─▶ 잘못되면 exit(-1)
   └─▶ 정상일 때만 처리
   │
   ▼
page_fault() / kill()
```

### 이번 단계의 봐야 할 포인트

- 유저 프로그램은 절대 믿을 수 없습니다.
- 잘못된 주소를 넘길 수 있고, 커널 주소를 넘길 수도 있습니다.
- 커널은 그런 입력 때문에 죽으면 안 되고, 그 유저 프로세스만 종료시켜야 합니다.

### 5-1. `syscall_handler()`에 포인터 검증을 붙인다

파일:
- [userprog/syscall.c](./userprog/syscall.c#L42)

줄 번호:
- [`userprog/syscall.c:42`](./userprog/syscall.c#L42)

#### 5-1-1. 이 함수의 현재 역할

- 유저 요청을 받는 중앙 함수입니다.

#### 5-1-2. 이 함수에서 해야 할 일

- 인자로 들어온 포인터가
  - `NULL`인지
  - kernel address인지
  - 유저 주소 공간에 매핑되어 있는지
확인합니다.

#### 5-1-3. 이 함수에서 사용되는 함수

- `check_user_ptr()`
- `check_user_buffer()`
- `check_user_string()`

#### 5-1-4. 수정 포인트

- syscall 처리 전에 먼저 안전한 포인터인지 확인하도록 바꿉니다.

#### 5-1-5. 수정할 수 있는 힌트

- 포인터 검증을 `syscall_handler()` 안에 길게 늘어놓지 말고 helper로 빼는 게 좋습니다.

---

### 5-2. helper 함수를 만든다

파일:
- [userprog/syscall.c](./userprog/syscall.c#L42)

줄 번호:
- [`userprog/syscall.c:42`](./userprog/syscall.c#L42) 근처에 추가

#### 5-2-1. 이 함수들의 현재 역할

- 아직 없는 경우가 많습니다.
- 새로 만들 helper 함수입니다.

#### 5-2-2. 이 함수들에서 해야 할 일

- `check_user_ptr(const void *uaddr)`
- `check_user_buffer(const void *buffer, size_t size)`
- `check_user_string(const char *str)`

같은 helper를 만들어 주소 검증을 분리합니다.

#### 5-2-3. 이 함수들에서 사용되는 함수

- `is_user_vaddr()`
- 페이지 테이블 조회 함수
- 실패 시 `exit(-1)` 흐름

#### 5-2-4. 수정 포인트

- 포인터 검증 로직을 재사용 가능하게 만듭니다.

#### 5-2-5. 수정할 수 있는 힌트

- 버퍼와 문자열은 시작 주소만 보면 부족합니다.
- 끝까지 확인해야 한다는 점이 핵심입니다.

---

### 5-3. `page_fault()`를 본다

파일:
- [userprog/exception.c](./userprog/exception.c#L120)

줄 번호:
- [`userprog/exception.c:120`](./userprog/exception.c#L120)

#### 5-3-1. 이 함수의 현재 역할

- fault 주소와 원인을 읽고 예외를 처리합니다.

#### 5-3-2. 지금 여기서 볼 것

- 잘못된 주소 접근이 결국 여기와 연결된다는 점
- segmentation fault, invalid pointer와 연결된다는 점

#### 5-3-3. 이 함수에서 사용되는 함수

- `rcr2()`
- `kill()`

#### 5-3-4. 수정 포인트

- 지금 단계에서는 먼저 역할을 이해하는 데 집중합니다.

#### 5-3-5. 수정할 수 있는 힌트

- syscall helper에서 미리 막는 것과, 실제 page fault가 난 뒤 처리하는 것은 연결된 두 단계라고 보면 됩니다.

---

### 5-4. `kill()`을 본다

파일:
- [userprog/exception.c](./userprog/exception.c#L71)

줄 번호:
- [`userprog/exception.c:71`](./userprog/exception.c#L71)

#### 5-4-1. 이 함수의 현재 역할

- user context에서 난 예외면 프로세스를 죽이고
- kernel context에서 난 예외면 panic 합니다.

#### 5-4-2. 지금 여기서 볼 것

- 왜 user mode와 kernel mode를 구분해야 하는지
- 왜 유저 잘못과 커널 버그를 다르게 처리하는지

#### 5-4-3. 이 함수에서 사용되는 함수

- `thread_exit()`
- `PANIC()`

#### 5-4-4. 수정 포인트

- 보통 여기서 직접 큰 구현을 하진 않더라도, 종료 정책을 이해해야 합니다.

#### 5-4-5. 수정할 수 있는 힌트

- “유저 잘못이면 그 유저만 종료, 커널 잘못이면 전체가 문제”라고 생각하면 됩니다.

### 중간 테스트

추천 테스트:
- `create-null`
- `open-bad-ptr`
- `read-bad-ptr`
- `write-bad-ptr`
- `bad-read`
- `bad-write`

완료 기준:
- 잘못된 유저 포인터 때문에 커널이 바로 죽지 않음
- 유저 프로세스만 종료시키는 흐름 이해

---

## 6챕터. wait / fork / exec / rox 로 확장한다

### 이번 단계의 봐야 할 포인트

- 이 단계는 앞 단계들이 어느 정도 된 뒤에 보는 마지막 확장입니다.
- `printf`, `argv`, `fd`, 포인터 검증이 먼저 잡혀 있어야 덜 어렵습니다.

### 6-1. `process_wait()`를 구현한다

파일:
- [userprog/process.c](./userprog/process.c#L203)

줄 번호:
- [`userprog/process.c:203`](./userprog/process.c#L203)

#### 6-1-1. 이 함수의 현재 역할

- 아직 `-1`만 반환하는 경우가 많습니다.

#### 6-1-2. 이 함수에서 해야 할 일

- 자식 종료 기다리기
- exit status 받기
- 한 번만 wait 허용

#### 6-1-3. 이 함수에서 사용되는 함수/구조

- semaphore
- child status 구조
- child list

#### 6-1-4. 수정 포인트

- 부모/자식 관계를 실제로 연결하는 첫 단계입니다.

#### 6-1-5. 수정할 수 있는 힌트

- wait는 “자식이 끝났는지 저장할 구조부터 필요하다”라고 생각하면 편합니다.

---

### 6-2. `process_fork()` / `__do_fork()`를 구현한다

파일:
- [userprog/process.c](./userprog/process.c#L77)
- [userprog/process.c](./userprog/process.c#L120)

줄 번호:
- [`userprog/process.c:77`](./userprog/process.c#L77)
- [`userprog/process.c:120`](./userprog/process.c#L120)

#### 6-2-1. 이 함수들의 현재 역할

- 새 thread 생성 뼈대만 있고, 주소공간이나 fd 복제는 미완성인 경우가 많습니다.

#### 6-2-2. 이 함수들에서 해야 할 일

- 부모 주소공간 복제
- fd table 복제
- 자식 `rax = 0`
- 부모는 자식 pid 반환

#### 6-2-3. 이 함수들에서 사용되는 함수

- `pml4_create()`
- `pml4_for_each()`
- `file_duplicate()`

#### 6-2-4. 수정 포인트

- “프로세스 복제”가 실제로 되게 만드는 단계입니다.

#### 6-2-5. 수정할 수 있는 힌트

- fork는 “부모를 거의 그대로 복사하되, 자식의 반환값만 0”이라고 먼저 이해하면 쉽습니다.

---

### 6-3. `exec()` / `process_exec()`를 다시 완성한다

파일:
- [userprog/process.c](./userprog/process.c#L164)
- [userprog/syscall.c](./userprog/syscall.c#L42)

줄 번호:
- [`userprog/process.c:164`](./userprog/process.c#L164)
- [`userprog/syscall.c:42`](./userprog/syscall.c#L42)

#### 6-3-1. 이 함수들의 현재 역할

- 새 실행 파일로 교체하는 뼈대입니다.

#### 6-3-2. 이 함수들에서 해야 할 일

- 현재 주소공간 정리
- 새 ELF 적재
- 새 argument stack 구성
- 성공하면 원래 코드로 돌아오지 않게 만들기

#### 6-3-3. 이 함수들에서 사용되는 함수

- `process_exec()`
- `load()`
- argument passing 로직

#### 6-3-4. 수정 포인트

- exec 성공 시 완전히 새 프로그램으로 바뀌도록 만들어야 합니다.

#### 6-3-5. 수정할 수 있는 힌트

- `exec()`는 성공하면 “현재 코드로 돌아오면 안 된다”는 규칙이 제일 중요합니다.

---

### 6-4. 실행 파일 write deny를 붙인다

파일:
- [userprog/process.c](./userprog/process.c)

줄 번호:
- `userprog/process.c` 안의 실행 파일 open / close 흐름

#### 6-4-1. 이 흐름의 현재 역할

- 실행 파일을 적재하지만, 실행 중 쓰기 금지를 유지하는 구조는 직접 챙겨야 합니다.

#### 6-4-2. 이 흐름에서 해야 할 일

- 실행 파일 open
- `file_deny_write()` 호출
- 종료 시 `file_allow_write()` 후 close

#### 6-4-3. 이 흐름에서 사용되는 함수

- `file_deny_write()`
- `file_allow_write()`
- `file_close()`

#### 6-4-4. 수정 포인트

- `rox-*` 테스트를 위해 필요합니다.

#### 6-4-5. 수정할 수 있는 힌트

- load가 끝났다고 실행 파일을 바로 닫아버리면 `rox-*`가 잘 깨집니다.

### 중간 테스트

추천 테스트:
- `wait-simple`
- `wait-twice`
- `fork-once`
- `fork-multiple`
- `exec-once`
- `rox-simple`

완료 기준:
- 기본 userprog 위에 프로세스 확장 기능이 얹히기 시작함

---

## 마지막 체크

이 문서는 정의를 외우는 문서가 아니라, **지금 어떤 파일을 열고, 어떤 함수를 보고, 어디를 수정하고, 어느 테스트로 확인할지** 따라가기 위한 문서입니다.  
막히면 항상 위에서 아래 순서로 내려오면 되고, 특히 첫 구현은 **2챕터의 `syscall_handler()`에서 `SYS_WRITE`를 처리하는 것**부터 시작하면 됩니다.
