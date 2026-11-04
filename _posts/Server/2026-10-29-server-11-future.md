---
layout: post
title: '[Server Day 11] Future로 작업 결과 받기'
date: 2026-10-29
categories: [Dev, Server]
tags: [Server, C++, Thread, Future, Async, Promise, Packaged Task]
---

# 서버 Day 11 - Future로 작업 결과 받기

다른 스레드에 계산을 맡겼다면 그 결과는 어떻게 받아야 할까? 공유 변수에 결과를 저장할 수도 있지만, 언제 계산이 끝났는지와 데이터를 어떻게 안전하게 전달할지도 함께 관리해야 한다.

`std::future`는 **나중에 준비될 작업 결과를 받는 통로**다. `async`, `promise`, `packaged_task`와 연결해 결과를 기다리거나 가져올 수 있다.

먼저 동기·지연·비동기 실행의 차이를 살펴보고, 결과를 전달하는 방법을 알아본다.

## 동기 실행: 계산이 끝나야 다음으로 진행한다

다음은 0부터 99,999까지 더하는 함수다. 별도의 프로젝트 타입 없이 사용할 수 있도록 표준 정수 타입을 사용한다.

```cpp
#include <cstdint>

std::int64_t Calculate()
{
    std::int64_t sum = 0;

    for (int i = 0; i < 100'000; i++)
    {
        sum += i;
    }

    return sum;
}
```

이 함수를 직접 호출하면 현재 스레드가 계산을 수행하고 반환값을 받는다.

```cpp
std::int64_t sum = Calculate();
std::cout << sum << "\n";
```

```text
메인: Calculate 실행 ────── 완료 → 결과 출력 → 다음 작업
```

구조가 단순하고 결과를 바로 사용할 수 있지만, 계산이 오래 걸리면 해당 스레드는 다음 작업으로 넘어가지 못한다.

동기 방식이 항상 나쁜 것은 아니다. 짧은 계산이라면 스레드를 추가하는 것보다 단순하고 효율적일 수 있다.

## thread만 만들면 반환값을 받기 불편하다

```cpp
std::thread t(Calculate);
t.join();
```

새 스레드에서 함수를 실행할 수 있지만, `std::thread`는 함수의 반환값을 호출자에게 전달하지 않는다. 위 코드에서는 계산 결과를 받아 사용할 수 없다.

`future`는 이런 결과 전달을 도와준다. 다만 **`future` 자체가 스레드를 만들거나, 작업을 항상 더 가볍게 실행해 주는 것은 아니다.**

## future는 나중에 결과를 받는 통로다

```cpp
std::future<std::int64_t> result = std::async(std::launch::async, Calculate);

// 결과가 필요 없는 다른 작업

std::int64_t sum = result.get();
```

`future<std::int64_t>`는 나중에 `std::int64_t` 결과를 받을 수 있는 객체다. 관련 도구는 `<future>` 헤더에서 제공한다.

`get()`은 결과가 준비되었다면 가져오고, 아직 준비되지 않았다면 준비될 때까지 기다린다.

```text
작업 요청 → 다른 작업 진행 → 결과가 필요한 시점에 get()
```

일반적인 `std::future`의 결과는 한 번만 꺼낸다. `get()` 이후 같은 객체에서 다시 `get()`하면 안 된다.

또한 결과 대신 예외가 저장되어 있다면 `get()`에서 그 예외가 다시 발생한다.

## deferred: 실행을 나중으로 미룬다

```cpp
auto result = std::async(std::launch::deferred, Calculate);

// 다른 작업

std::int64_t sum = result.get();
```

`deferred`는 `async()`를 호출하는 시점에 계산을 시작하지 않는다.

처음으로 `get()`이나 `wait()`처럼 시간 제한 없이 기다리는 함수를 호출하면, 그 함수를 호출한 스레드에서 계산을 실행한다.

```text
메인: 작업 등록 → 다른 작업 → get()에서 Calculate 실행 → 결과 받기
```

**지연 실행은 실행 시점을 미루는 것이며, 별도 스레드에서 동시에 실행한다는 뜻은 아니다.** 끝까지 실행을 요청하지 않으면 지연된 계산이 실행되지 않을 수도 있다.

## async: 별도의 실행 흐름에서 계산한다

```cpp
auto result = std::async(std::launch::async, Calculate);

// 계산과 함께 진행할 다른 작업

std::int64_t sum = result.get();
```

표준 의미에서 `launch::async`는 새 스레드에서 함수를 실행하는 방식이다.

```text
메인: 작업 시작 → 다른 작업 ────── get() → 결과 사용
계산:           Calculate 실행 ── 완료
```

계산이 이미 끝났다면 결과를 바로 받을 수 있고, 아직 끝나지 않았다면 `get()`에서 기다린다. [C++ 표준 초안의 async 설명](https://eel.is/c++draft/futures.async)

작업을 시작하자마자 `get()`을 호출하면 메인에서 다른 일을 함께 진행하는 이점을 거의 활용하지 못한다.

### 두 정책을 함께 지정하면?

```cpp
auto result = std::async(std::launch::async | std::launch::deferred, Calculate);
```

구현이 두 정책 중 하나를 선택할 수 있다. 정책을 생략한 다음 코드도 기본적으로 이 선택을 허용한다.

```cpp
auto result = std::async(Calculate);
```

따라서 `async()`라는 함수 이름만 보고 반드시 별도 스레드에서 실행된다고 가정하면 안 된다.

| 실행 방식 | 계산 시작 시점 | 계산하는 스레드 |
|---|---|---|
| 직접 호출 | 함수를 호출할 때 | 호출한 스레드 |
| deferred | 처음 get()이나 wait()로 실행을 요청할 때 | 해당 대기 함수를 호출한 스레드 |
| async | 비동기 작업을 시작할 때 | 별도 스레드 |

## get(), wait(), wait_for()의 차이

| 함수 | 역할 |
|---|---|
| get() | 결과를 기다리고 꺼냄 |
| wait() | 결과가 준비될 때까지 기다리지만 꺼내지는 않음 |
| wait_for() | 지정한 시간 동안 준비 여부를 기다리고 상태 반환 |

```cpp
auto status = result.wait_for(std::chrono::milliseconds(10));

if (status == std::future_status::ready)
{
    std::int64_t sum = result.get();
}
```

시간 단위를 사용할 때는 `<chrono>` 헤더를 포함한다.

반환 상태는 세 가지다.

| 상태 | 의미 |
|---|---|
| ready | 결과 또는 예외가 준비됨 |
| timeout | 지정 시간 안에 준비되지 않음 |
| deferred | 아직 실행하지 않은 지연 작업 |

**지연 작업에서 `wait_for()`는 계산을 시작하지 않고 `deferred`를 즉시 반환한다.** 이 경우 `get()`이나 `wait()`로 실행을 요청해야 한다.

타임아웃이 발생하더라도 작업이 취소되는 것은 아니다. 스케줄링 등의 영향으로 지정한 시간보다 대기가 길어질 수도 있다.

## promise: 결과를 직접 넣어 전달한다

`promise`와 `future`는 결과를 보내는 쪽과 받는 쪽으로 연결된다.

```text
promise: 결과를 넣는 쪽
future : 결과를 받는 쪽
```

작업 함수에서는 `set_value()`로 결과를 설정한다.

```cpp
void PromiseWorker(std::promise<std::string>&& sender)
{
    sender.set_value("Secret Message");
}
```

호출하는 쪽에서는 연결된 `future`를 먼저 얻은 뒤 작업 스레드에 `promise`를 전달한다.

```cpp
std::promise<std::string> sender;
std::future<std::string> result = sender.get_future();

std::thread t(PromiseWorker, std::move(sender));

std::string message = result.get();
std::cout << message << "\n";

t.join();
```

`promise`는 복사할 수 없으므로 `std::move()`를 통해 작업 쪽으로 이동시킨다. 여기서는 `<future>` 외에 `<string>`, `<thread>`, `<utility>`, `<iostream>` 헤더를 사용한다.

```text
메인: future 확보 → 작업 스레드 생성 → get()으로 결과 받기
작업: set_value()로 결과 전달
```

`promise` 자체가 스레드를 만드는 것은 아니다. 이 예제에서는 `thread`가 실행을 담당하고, `promise`가 결과 전달을 담당한다.

`set_value()`는 반환값을 직접 지정할 때 사용한다. 실패를 전달해야 한다면 `set_exception()`으로 예외를 저장할 수도 있다.

## packaged_task: 함수와 결과 전달을 묶는다

`promise`는 결과를 직접 설정해야 한다. `packaged_task`는 함수를 감싸서, 함수를 실행했을 때 반환값이나 예외가 연결된 `future`에 전달되도록 한다.

```cpp
std::packaged_task<std::int64_t()> task(Calculate);
std::future<std::int64_t> result = task.get_future();

std::thread t(std::move(task));

std::int64_t sum = result.get();
std::cout << sum << "\n";

t.join();
```

`std::int64_t()`는 인자 없이 호출하고 `std::int64_t`를 반환하는 함수 형태를 뜻한다.

```text
Calculate를 task로 포장
→ 연결된 future 얻기
→ 작업 스레드에서 task 실행
→ 반환값을 future로 받기
```

`packaged_task`를 만드는 것만으로 함수가 실행되지는 않는다. 위 예제에서는 `thread`가 전달받은 작업을 호출한다. 직접 `task()`를 호출하면 그 호출을 수행하는 스레드에서 실행된다.

### get()과 join()은 역할이 다르다

결과를 `get()`으로 받았어도, 직접 만든 `std::thread`의 정리는 별도로 필요하다.

| 함수 | 역할 |
|---|---|
| future의 get() | 결과를 기다리고 수신 |
| thread의 join() | 작업 스레드의 종료를 기다리고 연결 정리 |

`joinable()`인 `std::thread` 객체가 소멸하면 `std::terminate()`가 호출된다. 따라서 위 예제에서는 `t.join()`을 빠뜨리면 안 된다.

결과 수신 중 예외가 발생하는 경우에도 스레드가 정리되도록 설계해야 한다. 위 코드는 정상 실행의 기본 흐름을 보여주는 예제다.

## async로 만든 future는 소멸할 때 기다릴 수도 있다

`launch::async`로 시작한 작업과 연결된 마지막 `future`가 사라질 때, 작업이 아직 끝나지 않았다면 소멸 과정에서 기다릴 수 있다.

따라서 **get()을 호출하지 않으면 기다리지 않는다고 가정하면 안 된다.** [C++ 표준 초안의 공유 상태 설명](https://eel.is/c++draft/futures.state)

이 동작을 모든 `future`의 소멸자에 일반화해서도 안 된다. `promise`나 `packaged_task`에서 얻은 `future`가 직접 만든 스레드의 종료까지 자동으로 관리해 주는 것은 아니다.

## 결과를 전달하는 세 가지 방법 비교

| 도구 | 역할 | 실행 시작 여부 |
|---|---|---|
| async | 정책에 따라 함수를 실행하고 future 반환 | 정책에 따라 시작하거나 지연 |
| promise | 결과를 직접 설정해 future로 전달 | 자체적으로 실행하지 않음 |
| packaged_task | 함수의 반환값·예외를 future와 연결 | 별도로 호출해야 실행 |
| future | 결과 확인·대기·수신 | deferred 작업의 실행을 대기 호출로 유발할 수 있음 |

이 도구들은 한 번의 결과나 완료를 전달할 때 유용하다. 반복적으로 데이터가 들어오는 큐나, 여러 공유 데이터를 함께 보호하는 작업까지 모두 대신하는 것은 아니다.

## 비동기와 멀티스레드는 같은 뜻이 아니다

동기 실행에서는 함수를 직접 실행하고 완료 후 다음 코드로 진행한다. 비동기 방식에서는 작업을 요청한 뒤 다른 일을 진행하고 완료 결과를 나중에 받도록 구성할 수 있다.

멀티스레딩은 여러 스레드로 실행 흐름을 구성하는 방법이다. 비동기 처리는 운영체제의 비동기 입출력이나 이벤트 루프 등으로도 구현할 수 있으므로, 반드시 작업마다 새 스레드가 필요한 것은 아니다.

작업을 이해할 때는 **어디서 언제 실행할 것인가와 결과를 어떻게 받을 것인가를 나눠서 생각하는 것**이 중요하다. `future`는 그중 결과를 받는 통로를 담당한다.
