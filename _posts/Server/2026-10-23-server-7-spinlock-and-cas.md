---
layout: post
title: '[Server Day 07] 스핀락 구현과 CAS'
date: 2026-10-23
categories: [Dev, Server]
tags: [Server, C++, Thread, Spinlock, Atomic, CAS, Volatile]
---

# 서버 Day 07 - 스핀락 구현과 CAS

스핀락은 잠금을 얻을 때까지 반복해서 시도하는 잠금 방식이다. 잠금이 풀리는지 계속 확인하는 바쁜 대기를 사용하므로, 기다리는 동안에도 CPU를 사용한다.

직접 구현할 때 가장 중요한 것은 **잠금 상태를 확인하는 것과 잠긴 상태로 바꾸는 것을 하나의 원자적 연산으로 처리하는 것**이다.

단순한 구현을 먼저 살펴보고, 문제를 확인한 뒤 CAS로 해결해 보자.

## 1. 스핀락 구현 시도: 풀릴 때까지 기다리고 잠그기

잠금 상태를 `bool`로 나타낸다고 생각해 보자.

```text
false: 잠금이 풀려 있음
true : 누군가 잠금을 보유하고 있음
```

이 상태를 바탕으로 다음과 같이 구현을 시도할 수 있다. 아직 올바른 동기화를 제공하지 못하는 학습용 코드다.

```cpp
class SpinLock
{
public:
    void lock()
    {
        while (_locked)
        {
        }

        _locked = true;
    }

    void unlock()
    {
        _locked = false;
    }

private:
    volatile bool _locked = false;
};
```

`lock()`은 잠겨 있는 동안 반복하고, 풀린 것을 확인하면 `_locked`를 `true`로 바꾼다. `unlock()`은 다시 `false`로 바꾼다.

### volatile은 왜 등장할까?

컴파일러는 불필요하다고 판단한 읽기를 생략하거나, 이미 읽은 값을 재사용하는 최적화를 수행할 수 있다.

`volatile`은 해당 객체에 대한 접근을 이런 식으로 임의로 생략하지 않도록 하는 데 사용한다. 모든 최적화를 금지하는 기능은 아니다.

아주 짧은 예제로 보면 다음과 같다.

```cpp
volatile int value = 10;

int first = value;
int second = value;
```

위 코드에서는 `value`에 대한 두 번의 읽기 접근을 유지해야 한다. 첫 번째에 읽은 값을 그대로 재사용해 두 번째 `volatile` 읽기를 없애면 안 된다.

이를 반복문에 연결하면, `_locked`를 한 번 읽은 결과만 계속 사용하는 대신 반복할 때마다 읽기 접근을 수행하도록 한다는 의도로 이해할 수 있다.

다만 이것은 **항상 RAM에서 직접 읽는다거나 CPU 캐시를 사용하지 않는다는 뜻은 아니다.** 또한 표준 C++의 `volatile`은 원자성이나 스레드 사이의 동기화를 보장하지 않는다.

따라서 `volatile`을 사용한 위 코드는 안전한 스핀락이 아니다. 여러 스레드가 `_locked`를 동시에 읽고 수정하면 데이터 레이스가 발생하며, 표준 C++에서 동작이 정의되지 않는다.

## 2. 문제 분석: 확인과 변경 사이에 다른 스레드가 끼어든다

핵심 문제는 다음 두 작업이 분리되어 있다는 것이다.

```text
잠금이 풀려 있는지 확인
→ 잠긴 상태로 변경
```

두 스레드가 거의 동시에 접근하면 다음 상황을 생각할 수 있다.

| 순서 | 스레드 A | 스레드 B |
|---|---|---|
| 1 | false를 확인하고 반복문 통과 | |
| 2 | | false를 확인하고 반복문 통과 |
| 3 | true로 변경 | |
| 4 | | true로 변경 |
| 5 | 보호 구간 진입 | 보호 구간 진입 |

둘 다 잠금이 풀려 있는 것을 확인했기 때문에, 둘 다 자신이 잠금을 얻었다고 생각한다. 결국 보호해야 할 코드가 동시에 실행될 수 있다.

이 표는 문제의 원리를 단순화한 설명이다. 데이터 레이스가 있는 프로그램의 실제 동작이 이 순서로만 제한된다는 뜻은 아니다.

### atomic으로 읽고 쓰면 해결될까?

일반 변수 대신 `atomic`을 사용하되, 읽기와 쓰기를 분리해 보자.

```cpp
std::atomic<bool> _locked{false};

void lock()
{
    while (_locked.load())
    {
    }

    _locked.store(true);
}
```

각 읽기와 쓰기는 원자적이므로 `_locked` 자체의 데이터 레이스는 없어진다. 하지만 **확인과 변경 사이에 다른 스레드가 끼어들 수 있다는 문제는 그대로다.**

두 스레드가 각각 `false`를 읽은 다음 `true`를 저장하면, 여전히 동시에 보호 구간에 들어갈 수 있다.

필요한 것은 단순히 변수를 원자 타입으로 바꾸는 것이 아니라, **비교와 조건부 변경 전체를 하나의 원자적 연산으로 묶는 것**이다.

## 3. 해결책 구현: CAS로 비교와 변경을 하나로 묶기

CAS는 Compare-And-Swap, 즉 비교 후 교환이다. C++에서는 `compare_exchange_strong()`을 사용할 수 있다.

```cpp
bool expected = false;
bool desired = true;

bool success = _locked.compare_exchange_strong(expected, desired);
```

의미는 다음과 같다.

> 현재 값이 expected와 같으면 desired로 변경한다.

잠금에서는 `false`를 확인하고 `true`로 바꾸는 데 사용한다.

| 비교 결과 | 동작 | 반환값 |
|---|---|---|
| 현재 값이 false | true로 변경하여 잠금 획득 | true |
| 현재 값이 true | 잠금을 획득하지 못함 | false |

**비교와 조건부 변경이 하나의 원자적 연산**이므로, 여러 스레드가 같은 잠금을 동시에 획득하는 상황을 막을 수 있다.

### 실패하면 expected도 바뀐다

`expected`는 단순한 입력값이 아니다. 비교가 실패하면 비교 시 읽은 실제 값으로 바뀐다.

잠금이 이미 잡혀 있다면 다음과 같이 된다.

```text
현재 잠금 값: true
expected: false

비교 실패 → false 반환 → expected가 true로 변경
```

다음 시도에서도 풀린 상태를 검사하려면 `expected`를 다시 `false`로 설정해야 한다.

이 과정을 빠뜨리면 다음 시도에서 `true`와 비교해 `true`로 바꾸는 연산이 성공할 수 있다. 다른 스레드가 잠금을 보유하는데도 자신이 획득했다고 판단하는 것이다.

### CAS를 적용한 SpinLock

```cpp
class SpinLock
{
public:
    void lock()
    {
        bool expected = false;
        bool desired = true;

        while (_locked.compare_exchange_strong(expected, desired) == false)
        {
            expected = false;
        }
    }

    void unlock()
    {
        _locked.store(false);
    }

private:
    std::atomic<bool> _locked{false};
};
```

`_locked{false}`는 처음에는 잠금이 풀려 있음을 명시한다.

잠금 획득에 실패하면 반복문에서 `expected`를 초기화하고 재시도한다. 성공한 스레드만 `lock()`을 빠져나가 보호 구간으로 들어간다.

이 코드는 메모리 순서를 별도로 지정하지 않은 기본 원자 연산을 사용한다. 잠금 안에서 변경한 데이터를 다음 잠금 획득자가 올바르게 이어받도록 하는 동기화도 포함한다.

### lock_guard와 함께 사용하기

다음은 스핀락으로 일반 정수의 증가·감소를 보호하는 전체 예제다.

```cpp
#include <atomic>
#include <iostream>
#include <mutex>
#include <thread>

class SpinLock
{
public:
    void lock()
    {
        bool expected = false;
        bool desired = true;

        while (_locked.compare_exchange_strong(expected, desired) == false)
        {
            expected = false;
        }
    }

    void unlock()
    {
        _locked.store(false);
    }

private:
    std::atomic<bool> _locked{false};
};

int sum = 0;
SpinLock spinlock;

void Add()
{
    for (int i = 0; i < 100000; i++)
    {
        std::lock_guard<SpinLock> guard(spinlock);
        sum++;
    }
}

void Sub()
{
    for (int i = 0; i < 100000; i++)
    {
        std::lock_guard<SpinLock> guard(spinlock);
        sum--;
    }
}

int main()
{
    std::thread t1(Add);
    std::thread t2(Sub);

    t1.join();
    t2.join();

    std::cout << sum << "\n";
}
```

`lock_guard`는 생성할 때 `spinlock.lock()`을 호출하고, 소멸할 때 `spinlock.unlock()`을 호출한다.

모든 수정이 같은 스핀락으로 보호되므로 한 번에 하나의 스레드만 `sum`을 수정한다. 따라서 `sum` 자체를 `atomic`으로 만들 필요는 없다.

두 스레드가 각각 10만 번 증가·감소하고, 메인 스레드가 두 작업을 `join()`한 뒤 읽으므로 정상 완료 시 결과는 `0`이다.

### 스핀락에도 한계가 있다

이 구현은 기다리는 동안 계속 CAS를 시도한다. 잠금 유지 시간이 길거나 경쟁이 심하면 CPU 사용과 공유 데이터 접근 비용이 커질 수 있다.

또한 일반 뮤텍스와 마찬가지로, 같은 스레드가 이미 가진 잠금을 다시 얻으려고 하면 안 된다. 이 구현에서는 스스로 잠금을 풀 수 없는 채 반복하게 된다.

| 요소 | 역할 |
|---|---|
| volatile | 해당 객체의 접근을 임의로 생략하는 최적화를 제한하며, 스레드 동기화는 제공하지 않음 |
| atomic의 개별 읽기·쓰기 | 개별 접근의 원자성 보장 |
| CAS | 비교와 조건부 변경 전체의 원자성 보장 |
| lock_guard | 잠금 획득과 해제를 객체 수명으로 관리 |

스핀락 구현에서 핵심은 **풀린 상태를 확인한 스레드만 잠긴 상태로 바꾸도록, 확인과 변경을 하나로 묶는 것**이다.
