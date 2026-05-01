# PINTOS Flow README

## 1. 이번 주 핵심 요약

이번 주의 핵심은 **유저 프로그램을 안전하게 실행시키고, 위험한 작업은 직접 하지 못하게 막은 뒤 시스템 콜을 통해서만 커널에 부탁하게 만드는 것**입니다.  
그래서 `printf`를 먼저 보는 이유는 단순 출력 구현이 아니라, **user mode -> kernel mode -> 다시 user mode** 로 이어지는 가장 짧은 성공 경로이기 때문입니다.  
즉 이번 주는 넓게 보면 `User Program`, `System Call`, `Virtual Memory Layout`을 배우지만, 실제 코드 시작점은 **`write` 시스템 콜을 통한 `printf` 경로 열기**입니다.

---

## 2. 중요 키워드

- `user mode`
- `kernel mode`
- `system call`
- `printf`
- `write`
- `rax`
- `intr_frame`
- `ELF`
- `load`
- `do_iret`
- `page fault`
- `user stack`
- `kernel stack`
- `fd`
- `putbuf`

---

## 3. 가장 먼저 볼 전체 흐름

```text
유저 printf
-> lib/user/console.c
-> write(fd=1, buffer, size)
-> lib/user/syscall.c
-> syscall instruction
-> userprog/syscall.c 의 syscall_handler()
-> SYS_WRITE 처리
-> putbuf(buffer, size)
-> 화면 출력
```

이 흐름이 먼저 보이면:

- 유저 프로그램이 실행되고 있는지
- system call 경로가 열렸는지
- 커널이 유저 요청을 받을 수 있는지

를 한 번에 확인할 수 있습니다.

---

## 4. 우리가 공부해야 하는 함수 순서

### 1단계: `printf`가 어떻게 커널까지 가는지 본다

1. [lib/user/console.c](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/lib/user/console.c:30)  
   줄 번호: `lib/user/console.c:30`
2. [lib/user/syscall.c의 write](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/lib/user/syscall.c:123)  
   줄 번호: `lib/user/syscall.c:123`
3. [include/lib/syscall-nr.h](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/include/lib/syscall-nr.h:17)  
   줄 번호: `include/lib/syscall-nr.h:17`
4. [userprog/syscall.c의 syscall_init](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/syscall.c:27)  
   줄 번호: `userprog/syscall.c:27`
5. [userprog/syscall.c의 syscall_handler](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/syscall.c:41)  
   줄 번호: `userprog/syscall.c:41`
6. [lib/kernel/console.c의 putbuf](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/lib/kernel/console.c:143)  
   줄 번호: `lib/kernel/console.c:143`

### 2단계: 유저 프로그램이 어떻게 시작되는지 본다

1. [process_create_initd](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/process.c:41)  
   줄 번호: `userprog/process.c:41`
2. [initd](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/process.c:61)  
   줄 번호: `userprog/process.c:61`
3. [process_exec](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/process.c:163)  
   줄 번호: `userprog/process.c:163`
4. [process_activate](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/process.c:251)  
   줄 번호: `userprog/process.c:251`

### 3단계: 잘못된 주소 접근과 예외 처리를 본다

1. [exception_init](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/exception.c:30)  
   줄 번호: `userprog/exception.c:30`
2. [kill](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/exception.c:70)  
   줄 번호: `userprog/exception.c:70`
3. [page_fault](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/exception.c:119)  
   줄 번호: `userprog/exception.c:119`

---

## 5. 함수별 정리

## 5-1. `lib/user/console.c`

관련 위치:

- [putstr / 출력 시작점](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/lib/user/console.c:30)  
  줄 번호: `lib/user/console.c:30`

핵심 3문장:

`printf`나 문자열 출력은 결국 유저 라이브러리 안에서 `write` 호출로 바뀝니다.  
즉 유저 프로그램은 직접 화면에 출력하지 않고, `fd=1`인 표준출력을 통해 커널에게 부탁합니다.  
그래서 이번 주 첫 디버깅 시작점은 "유저 출력이 결국 `write` 시스템 콜로 간다"는 흐름을 보는 것입니다.

중요 키워드:

- `printf`
- `write`
- `STDOUT_FILENO`
- `user library`

이 함수를 위한 함수들 / 연계성 이유:

- [lib/user/syscall.c의 write](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/lib/user/syscall.c:123)  
  이유: 실제로 `printf`가 결국 호출하게 되는 시스템 콜 래퍼입니다.

---

## 5-2. `lib/user/syscall.c`의 `write`

관련 위치:

- [write](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/lib/user/syscall.c:123)  
  줄 번호: `lib/user/syscall.c:123`
- [syscall 공통 함수](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/lib/user/syscall.c:6)  
  줄 번호: `lib/user/syscall.c:6`

핵심 3문장:

이 `write` 함수는 실제 파일 시스템 코드를 수행하는 것이 아니라, `SYS_WRITE` 번호와 인자를 레지스터에 넣어서 `syscall` 명령으로 커널에 진입합니다.  
여기서 중요한 것은 `rax`에는 시스템 콜 번호가, `rdi`, `rsi`, `rdx` 같은 레지스터에는 인자가 실린다는 점입니다.  
즉 이 함수는 "유저 코드가 커널에게 요청서를 쓰는 입구"라고 보면 됩니다.

중요 키워드:

- `SYS_WRITE`
- `rax`
- `rdi`
- `rsi`
- `rdx`
- `syscall instruction`

이 함수를 위한 함수들 / 연계성 이유:

- [include/lib/syscall-nr.h](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/include/lib/syscall-nr.h:17)  
  이유: `SYS_WRITE` 번호가 정의된 곳입니다.
- [userprog/syscall.c의 syscall_handler](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/syscall.c:41)  
  이유: 유저가 보낸 요청을 실제로 받는 커널 쪽 함수입니다.

---

## 5-3. `include/lib/syscall-nr.h`

관련 위치:

- [SYS_WRITE](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/include/lib/syscall-nr.h:17)  
  줄 번호: `include/lib/syscall-nr.h:17`

핵심 3문장:

이 파일은 시스템 콜 번호표입니다.  
유저 쪽과 커널 쪽이 같은 번호를 보고 있어야 `write` 요청인지 `exit` 요청인지 서로 알아들을 수 있습니다.  
즉 이 파일은 system call 세계의 "약속된 프로토콜"입니다.

중요 키워드:

- `SYS_HALT`
- `SYS_EXIT`
- `SYS_WRITE`
- `protocol`

이 함수를 위한 함수들 / 연계성 이유:

- [lib/user/syscall.c의 write](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/lib/user/syscall.c:123)  
  이유: 유저 쪽이 이 번호를 사용합니다.
- [userprog/syscall.c의 syscall_handler](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/syscall.c:41)  
  이유: 커널 쪽도 이 번호를 해석해야 합니다.

---

## 5-4. `userprog/syscall.c`의 `syscall_init`

관련 위치:

- [syscall_init](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/syscall.c:27)  
  줄 번호: `userprog/syscall.c:27`

핵심 3문장:

`syscall_init()`은 CPU에게 "유저 프로그램이 `syscall` 명령을 실행하면 어디로 점프해야 하는지"를 알려주는 초기화 함수입니다.  
즉 user mode에서 kernel mode로 들어오는 문 주소를 등록하는 단계입니다.  
이 단계가 없으면 유저 프로그램은 시스템 콜을 호출해도 커널로 정상 진입할 수 없습니다.

중요 키워드:

- `MSR_STAR`
- `MSR_LSTAR`
- `syscall_entry`
- `kernel entry`

이 함수를 위한 함수들 / 연계성 이유:

- [userprog/syscall.c의 syscall_handler](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/syscall.c:41)  
  이유: 진입 후 실제로 요청을 처리하는 메인 함수입니다.

---

## 5-5. `userprog/syscall.c`의 `syscall_handler`

관련 위치:

- [syscall_handler](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/syscall.c:41)  
  줄 번호: `userprog/syscall.c:41`

핵심 3문장:

`syscall_handler()`는 유저 프로그램이 보낸 시스템 콜 요청을 실제로 해석하는 커널의 메인 함수입니다.  
여기서 시스템 콜 번호를 읽고, `SYS_WRITE`라면 `fd`, `buffer`, `size` 같은 인자를 읽어서 적절한 커널 함수를 호출해야 합니다.  
즉 이번 주 첫 구현 목표는 이 함수 안에서 `SYS_WRITE`와 `fd == 1`을 처리해 `printf`가 화면에 찍히게 만드는 것입니다.

중요 키워드:

- `intr_frame`
- `system call number`
- `return value`
- `fd`
- `buffer`
- `size`

이 함수를 위한 함수들 / 연계성 이유:

- [lib/kernel/console.c의 putbuf](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/lib/kernel/console.c:143)  
  이유: 표준 출력으로 들어온 문자열을 실제 화면에 찍는 함수입니다.
- [userprog/exception.c의 page_fault](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/exception.c:119)  
  이유: 잘못된 유저 포인터를 받았을 때 결국 예외 처리와 연결됩니다.

---

## 5-6. `lib/kernel/console.c`의 `putbuf`

관련 위치:

- [putbuf](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/lib/kernel/console.c:143)  
  줄 번호: `lib/kernel/console.c:143`

핵심 3문장:

`putbuf()`는 커널이 콘솔에 문자열을 출력할 때 사용하는 실제 함수입니다.  
즉 `printf`의 최종 목적지는 결국 이 함수라고 볼 수 있습니다.  
이번 주 첫 성공 기준은 `syscall_handler()`가 여기까지 도달하게 만드는 것입니다.

중요 키워드:

- `console`
- `kernel output`
- `stdout`

이 함수를 위한 함수들 / 연계성 이유:

- [userprog/syscall.c의 syscall_handler](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/syscall.c:41)  
  이유: `SYS_WRITE` 처리 시 `fd == 1`이면 결국 이 함수가 호출됩니다.

---

## 5-7. `userprog/process.c`의 `process_create_initd`

관련 위치:

- [process_create_initd](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/process.c:41)  
  줄 번호: `userprog/process.c:41`

핵심 3문장:

이 함수는 첫 번째 유저 프로그램 `initd`를 시작시키는 출발점입니다.  
즉 디스크에 있는 프로그램 이름을 복사해 두고, 새로운 스레드를 만들어 유저 프로그램 실행 흐름으로 넘깁니다.  
이번 주에서 이 함수는 "커널이 언제 유저 프로그램 실행을 시작하는가"를 보여주는 첫 관문입니다.

중요 키워드:

- `initd`
- `thread_create`
- `first user process`

이 함수를 위한 함수들 / 연계성 이유:

- [initd](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/process.c:61)  
  이유: 실제로 새로 만들어진 스레드가 들어가 실행하는 함수입니다.

---

## 5-8. `userprog/process.c`의 `initd`

관련 위치:

- [initd](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/process.c:61)  
  줄 번호: `userprog/process.c:61`

핵심 3문장:

`initd()`는 유저 프로그램 실행을 위한 초기 세팅을 한 뒤 `process_exec()`를 호출합니다.  
즉 "이제 진짜 유저 프로그램을 메모리에 올리고 실행해라"라고 넘겨주는 중간 다리입니다.  
첫 유저 프로그램이 왜 갑자기 실행되는지 궁금하면 이 함수가 연결 고리입니다.

중요 키워드:

- `initd`
- `process_exec`
- `launch`

이 함수를 위한 함수들 / 연계성 이유:

- [process_exec](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/process.c:163)  
  이유: 실제 ELF 로딩과 유저 모드 진입이 이어지는 핵심 함수입니다.

---

## 5-9. `userprog/process.c`의 `process_exec`

관련 위치:

- [process_exec](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/process.c:163)  
  줄 번호: `userprog/process.c:163`

핵심 3문장:

`process_exec()`는 기존 실행 문맥을 정리하고, 새 유저 프로그램을 메모리에 로드한 뒤 유저 모드로 넘기는 함수입니다.  
여기서 `_if`라는 `intr_frame`을 세팅하는데, 이 값들이 곧 유저 모드에서 시작할 레지스터 상태가 됩니다.  
즉 argument passing, user stack, user entry point 같은 주제는 결국 이 함수와 `load()` 흐름에서 연결됩니다.

중요 키워드:

- `intr_frame`
- `load`
- `user stack`
- `do_iret`
- `exec`

이 함수를 위한 함수들 / 연계성 이유:

- [process_activate](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/process.c:251)  
  이유: 유저 프로그램용 페이지 테이블과 커널 스택을 활성화합니다.
- `load()`  
  이유: ELF 실행파일을 메모리에 실제로 적재합니다.
- `do_iret()`  
  이유: kernel mode에서 user mode로 실제 전환하는 마지막 단계입니다.

---

## 5-10. `userprog/process.c`의 `process_activate`

관련 위치:

- [process_activate](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/process.c:251)  
  줄 번호: `userprog/process.c:251`

핵심 3문장:

`process_activate()`는 다음에 실행할 스레드의 유저 주소 공간과 커널 스택을 CPU가 사용하도록 바꾸는 함수입니다.  
여기서 `pml4_activate(next->pml4)`가 유저 메모리 맵을, `tss_update(next)`가 커널 스택 정보를 준비합니다.  
즉 user stack과 kernel stack이 왜 분리되는지 코드로 확인하려면 이 함수를 보면 됩니다.

중요 키워드:

- `pml4`
- `tss`
- `kernel stack`
- `user address space`

이 함수를 위한 함수들 / 연계성 이유:

- [page_fault](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/exception.c:119)  
  이유: 잘못된 가상 주소 접근은 활성화된 주소 공간 기준으로 예외가 납니다.

---

## 5-11. `userprog/exception.c`의 `exception_init`

관련 위치:

- [exception_init](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/exception.c:30)  
  줄 번호: `userprog/exception.c:30`

핵심 3문장:

`exception_init()`은 유저 프로그램이 잘못된 명령이나 잘못된 주소 접근을 했을 때 어떤 핸들러로 보낼지 등록하는 함수입니다.  
즉 인터럽트와 예외가 실제 코드에서 어떻게 연결되는지 보여주는 시작점입니다.  
이번 주에서 이 함수는 "왜 유저 프로그램이 함부로 커널을 죽이면 안 되는가"를 구조적으로 보게 해줍니다.

중요 키워드:

- `interrupt`
- `exception`
- `handler registration`
- `page fault`

이 함수를 위한 함수들 / 연계성 이유:

- [kill](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/exception.c:70)  
  이유: 유저 프로그램을 종료시키는 기본 예외 처리 함수입니다.
- [page_fault](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/exception.c:119)  
  이유: 잘못된 메모리 접근을 처리하는 핵심 함수입니다.

---

## 5-12. `userprog/exception.c`의 `kill`

관련 위치:

- [kill](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/exception.c:70)  
  줄 번호: `userprog/exception.c:70`

핵심 3문장:

`kill()`은 예외가 user mode에서 발생했는지, kernel mode에서 발생했는지 구분해서 처리합니다.  
유저 코드에서 잘못된 동작이 일어나면 해당 프로세스를 죽이고, 커널 코드에서 같은 일이 일어나면 커널 버그로 보고 panic합니다.  
즉 user mode와 kernel mode를 왜 구분하는지 가장 직관적으로 드러나는 함수입니다.

중요 키워드:

- `SEL_UCSEG`
- `SEL_KCSEG`
- `thread_exit`
- `panic`

이 함수를 위한 함수들 / 연계성 이유:

- [page_fault](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/exception.c:119)  
  이유: 최종적으로 잘못된 접근은 이 함수로 연결될 수 있습니다.

---

## 5-13. `userprog/exception.c`의 `page_fault`

관련 위치:

- [page_fault](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/exception.c:119)  
  줄 번호: `userprog/exception.c:119`

핵심 3문장:

`page_fault()`는 잘못된 가상 주소 접근이 일어났을 때 fault 주소와 원인을 읽어 분석하는 함수입니다.  
지금 단계에서는 대체로 정보를 출력하고 유저 프로세스를 죽이는 역할을 하지만, 나중에는 가상 메모리 처리로 확장됩니다.  
즉 segmentation fault, user pointer 검증, virtual memory layout의 필요성은 결국 이 함수와 연결됩니다.

중요 키워드:

- `fault_addr`
- `not_present`
- `write`
- `user`
- `virtual address`

이 함수를 위한 함수들 / 연계성 이유:

- [syscall_handler](/Users/heejunkim/Desktop/krafton/pintos_1/week09-team-02-pintos-threads/userprog/syscall.c:41)  
  이유: 시스템 콜 인자로 받은 유저 포인터가 잘못되면 결국 여기와 연결됩니다.

---

## 6. `printf`를 먼저 구현하는 이유

핵심 3문장:

`printf`는 단순 출력 기능이 아니라, 유저 프로그램이 커널에 안전하게 요청을 보내는 첫 번째 성공 경로입니다.  
`printf`가 동작하면 최소한 유저 프로그램 실행, system call 진입, 커널 처리, 다시 복귀의 뼈대가 연결되었다는 뜻입니다.  
그래서 이번 주는 모든 system call을 한꺼번에 구현하기보다, 먼저 `write(fd=1, buffer, size)` 하나를 열어두는 것이 가장 좋은 시작점입니다.

중요 키워드:

- `printf`
- `write`
- `stdout`
- `system call path`
- `debugging entry`

---

## 7. 이번 주 최소 구현 목표

```text
1. 유저 printf가 write를 호출하는 것을 이해한다.
2. syscall_handler()에서 system call 번호를 읽는다.
3. SYS_WRITE일 때 fd, buffer, size를 읽는다.
4. fd == 1이면 putbuf(buffer, size)를 호출한다.
5. 출력한 길이를 반환값으로 저장한다.
```

즉 첫 목표는:

**모든 기능 구현이 아니라, 유저 출력 하나가 user mode에서 kernel mode로 들어왔다가 성공적으로 처리되는 길을 여는 것**

입니다.

---

## 8. 한 줄 전체 요약

```text
유저 프로그램 실행
-> printf 호출
-> write system call
-> syscall_handler()
-> putbuf()
-> 출력 성공
-> 이후 exec, wait, argument passing, pointer validation으로 확장
```
