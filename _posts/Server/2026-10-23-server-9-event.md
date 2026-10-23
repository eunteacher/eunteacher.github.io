---
layout: post
title: '[Server Day 09] 이벤트로 작업 통지하기'
date: 2026-10-23
categories: [Dev, Server]
tags: [Server, C++, Thread, Event, Windows, Synchronization]
---

# 서버 Day 09 - 이벤트로 작업 통지하기

공유 큐에 데이터가 들어왔는지 계속 확인하면, 할 일이 없는 동안에도 CPU를 사용하게 된다. 잠시 쉬었다가 확인하는 방법은 CPU 낭비를 줄일 수 있지만, 데이터가 들어온 직후에도 쉬는 시간이 끝나기를 기다릴 수 있다.

이벤트를 사용하면 **작업이 생겼다는 통지를 기다렸다가 처리하는 방식**으로 바꿀 수 있다. 식당에 비유하면 자리를 계속 확인하는 대신, 직원에게 자리가 나면 알려 달라고 부탁하는 방법이다.

## 생산자와 소비자

하나의 큐를 공유하는 두 스레드를 생각해 보자.

| 스레드 | 역할 |
|---|---|
| 생산자 Producer | 큐에 데이터를 넣는다 |
| 소비자 Consumer | 큐에서 데이터를 꺼내 처리한다 |

큐에 동시에 접근하면 문제가 생길 수 있으므로, 데이터를 넣거나 꺼낼 때는 같은 뮤텍스로 보호한다.

```cpp
std::mutex m;
std::queue<int> q;
```

## 첫 번째 구현: 데이터가 있는지 계속 확인하기

생산자는 큐에 데이터를 넣고 100밀리초 동안 쉰다.

```cpp
void Producer()
{
    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(m);
            q.push(100);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
```

소비자는 반복문에서 큐를 확인하고, 데이터가 있으면 꺼낸다.

```cpp
void Consumer()
{
    while (true)
    {
        std::unique_lock<std::mutex> lock(m);

        if (q.empty() == false)
        {
            int data = q.front();
            q.pop();
            std::cout << data << "\n";
        }
    }
}
```

큐가 비어 있어도 다음 작업을 계속 반복한다.

```text
잠금 획득 → 큐 확인 → 비어 있음 → 잠금 해제 → 다시 반복
```

이것은 대기 상태에서 잠든 것이 아니라, 할 일이 없는지 반복해서 확인하는 **바쁜 대기**다. 생산자가 다음 데이터를 준비하는 동안 소비자는 불필요하게 CPU를 사용한다.

## 이벤트는 작업이 생겼다는 사실을 알린다

Windows 이벤트 객체에는 두 가지 상태가 있다.

| 상태 | 의미 |
|---|---|
| Non-signaled | 신호가 없는 상태 |
| Signaled | 신호가 설정된 상태 |

생산자는 데이터를 넣은 뒤 이벤트를 Signaled로 바꾼다. 소비자는 이벤트를 기다렸다가 대기를 통과하면 큐를 확인한다.

```text
소비자: 이벤트 대기
생산자: 데이터 추가 → 신호 설정
소비자: 대기 통과 → 데이터 처리
```

이벤트는 데이터를 저장하는 큐가 아니다. **스레드가 진행해도 된다는 신호를 전달하는 동기화 도구**다.

## CreateEvent()로 이벤트 만들기

```cpp
HANDLE handle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
```

| 인자 | 설정 의미 |
|---|---|
| 첫 번째 nullptr | 기본 보안 설정 사용, 핸들 상속 안 함 |
| 두 번째 FALSE | 자동 리셋 이벤트 |
| 세 번째 FALSE | 초기 상태는 Non-signaled |
| 네 번째 nullptr | 이름 없는 이벤트 |

`HANDLE`은 이벤트 객체를 사용할 때 전달하는 식별 수단이다. 두 스레드가 시작되기 전에 이벤트를 만들고, 같은 핸들을 사용하도록 한다.

이 함수는 Windows API이므로 `<Windows.h>` 헤더가 필요하다.

## 생산자: 데이터를 넣은 뒤 신호 보내기

```cpp
{
    std::unique_lock<std::mutex> lock(m);
    q.push(100);
}

SetEvent(handle);
```

`SetEvent()`는 이벤트를 Signaled 상태로 바꾼다.

```text
뮤텍스 잠금
→ 큐에 데이터 추가
→ 뮤텍스 해제
→ 이벤트 신호 설정
```

데이터를 먼저 준비하고 통지한다. 중괄호를 사용해 큐에 넣는 구간까지만 잠금을 유지하므로, 소비자가 통지를 받고 실행될 때 생산자의 잠금 해제를 기다리는 상황을 줄일 수 있다.

## 소비자: 신호를 기다린 뒤 큐 확인하기

```cpp
WaitForSingleObject(handle, INFINITE);
```

이벤트가 Non-signaled라면 대기하고, Signaled라면 대기를 통과한다.

`INFINITE`는 시간 제한 없이 기다린다는 뜻이다. CPU를 사용하면서 무한 반복한다는 의미는 아니다.

대기를 통과한 다음에는 뮤텍스로 큐를 보호하면서 데이터를 꺼낸다.

```cpp
std::unique_lock<std::mutex> lock(m);

if (q.empty() == false)
{
    int data = q.front();
    q.pop();
    std::cout << data << "\n";
}
```

이벤트와 뮤텍스의 역할은 다르다.

| 도구 | 역할 |
|---|---|
| 이벤트 | 작업이 생겼음을 통지하고 대기를 제어 |
| 뮤텍스 | 큐에 대한 동시 접근 방지 |

**이벤트를 사용해도 큐를 보호하는 뮤텍스는 필요하다.** 통지를 받았다는 사실이 큐를 독점한다는 뜻은 아니다.

또한 이벤트를 기다리는 동안에는 큐의 뮤텍스를 보유하지 않아야 한다. 소비자가 잠금을 가진 채 기다리면 생산자가 데이터를 넣기 위한 잠금을 얻지 못할 수 있다.

## 자동 리셋과 수동 리셋

자동 리셋 이벤트는 대기 중인 스레드 하나를 통과시키면 자동으로 Non-signaled로 돌아간다.

```text
SetEvent()
→ Signaled
→ 대기 중인 스레드 하나 통과
→ 자동으로 Non-signaled
```

수동 리셋 이벤트는 `ResetEvent()`를 호출할 때까지 Signaled 상태를 유지한다. 그동안 여러 스레드가 대기를 통과할 수 있다. [Microsoft 이벤트 객체 문서](https://learn.microsoft.com/en-us/windows/win32/sync/event-objects)

| 구분 | 자동 리셋 | 수동 리셋 |
|---|---|---|
| 생성 시 두 번째 인자 | FALSE | TRUE |
| 신호 해제 | 대기 스레드 하나를 통과시키면 자동 해제 | ResetEvent()로 해제 |
| 특징 | 한 번의 신호로 대기 스레드 하나 통과 | 신호 유지 중 여러 스레드 통과 가능 |

자동 리셋 설정에서는 별도로 `ResetEvent()`를 호출할 필요가 없다. 대기를 통과한 뒤 수동으로 리셋하면, 그 사이 생산자가 설정한 새로운 신호를 지워 버릴 수 있으므로 주의해야 한다.

## 이벤트를 적용한 기본 코드

다음은 생산자와 소비자가 각각 무한 반복하는 학습용 코드다. 프로젝트 전용 타입 대신 표준 타입인 `int`를 사용한다.

```cpp
#include <Windows.h>
#include <chrono>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

std::mutex m;
std::queue<int> q;
HANDLE handle = nullptr;

void Producer()
{
    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(m);
            q.push(100);
        }

        SetEvent(handle);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void Consumer()
{
    while (true)
    {
        DWORD result = WaitForSingleObject(handle, INFINITE);

        if (result != WAIT_OBJECT_0)
        {
            return;
        }

        std::unique_lock<std::mutex> lock(m);

        if (q.empty() == false)
        {
            int data = q.front();
            q.pop();
            std::cout << data << "\n";
        }
    }
}

int main()
{
    handle = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    if (handle == nullptr)
    {
        return 1;
    }

    std::thread p(Producer);
    std::thread c(Consumer);

    p.join();
    c.join();

    CloseHandle(handle);
}
```

생성 실패와 대기 결과는 확인하지만, 작업 종료나 오류 복구까지 구현한 코드는 아니다.

## 이벤트는 신호 횟수를 세지 않는다

생산자가 소비자보다 먼저 `SetEvent()`를 호출해도, 대기 중인 스레드가 없다면 자동 리셋 이벤트는 Signaled 상태를 유지한다. 소비자는 이후에 기다릴 때 대기를 통과할 수 있다.

하지만 **이미 Signaled인 이벤트에 `SetEvent()`를 여러 번 호출해도 신호가 횟수만큼 쌓이지 않는다.** [Microsoft SetEvent 문서](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-setevent)

```text
생산자: 데이터 3개 추가, SetEvent() 3번 호출
이벤트: 소비자가 대기하기 전까지 Signaled 상태 하나만 유지

소비자: 대기 통과 → 데이터 1개 꺼냄 → 다시 대기
큐: 데이터 2개가 남아 있음
```

따라서 데이터 하나를 넣을 때마다 신호를 보냈다고 해서, 소비자가 반드시 그 횟수만큼 깨어나는 것은 아니다.

생산자가 계속 동작하면 이후 통지로 처리가 이어질 수 있지만, 생산이 멈추면 큐에 데이터가 남은 채 소비자가 기다릴 수도 있다. 위 기본 코드도 이 한계를 가진다.

소비자가 하나인 구조에서는 깨어났을 때 큐가 빌 때까지 처리하는 방법을 고려할 수 있다. 작업 개수를 세어야 하는 상황이라면 세마포어 같은 도구도 선택지가 된다.

## 커널 객체의 비용과 핸들 정리

Windows 이벤트는 운영체제가 관리하는 커널 객체다. 오래 기다릴 때 반복 확인으로 CPU를 낭비하지 않는 장점이 있지만, 커널 기능 사용과 대기·깨움, 스케줄링에는 비용이 든다.

사용을 마친 핸들은 다음 함수로 닫는다.

```cpp
CloseHandle(handle);
```

핸들을 닫는 것은 이벤트 신호를 끄는 작업과 다르다. 자신이 가진 객체 참조를 반납하는 것이다. 이벤트를 참조하는 모든 핸들이 닫히면 이벤트 객체가 정리되며, 이 참조의 수는 소비한 데이터 개수와 관계없다.

다만 위 코드는 생산자와 소비자가 모두 무한 반복하므로, 정상 흐름에서는 `join()`이 끝나지 않아 `CloseHandle()`까지 도달하지 않는다.

실제 프로그램에서는 종료 조건을 만들고, 대기 중인 소비자도 깨워 종료한 뒤 핸들을 닫아야 한다. 오류 처리 역시 한 스레드만 빠져나오는 데서 끝내지 않고, 나머지 작업의 종료와 자원 정리까지 함께 설계해야 한다.

이벤트를 사용할 때는 **뮤텍스로 데이터를 보호하고, 이벤트로 작업이 생겼다는 사실을 알리는 역할 분담**이 중요하다. 그리고 이벤트는 데이터 개수를 세는 장치가 아니라 신호 상태를 가진 장치라는 점을 기억해야 한다.
