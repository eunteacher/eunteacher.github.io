---
layout: post
title: '[DX12 01] 프로젝트 구성과 Engine 라이브러리 연결'
date: 2026-10-23
categories: [Dev, DX12]
tags: [DX12, C++, VisualStudio, StaticLibrary, PCH]
---

DX12 공부의 첫 단계로 Client와 Engine 프로젝트를 구성한다. 이번 단계의 목표는 **Client에서 Engine의 함수를 호출할 수 있도록 연결하고, DX12를 사용할 공통 환경을 준비하는 것**이다.

분석 대상은 `1_SetProject` 예제다. 아직 GPU 장치를 만들거나 화면을 그리는 코드는 없고, 윈도우 창과 게임 반복문까지 준비되어 있다.

## Client와 Engine의 역할

하나의 솔루션 안에 두 프로젝트가 있다.

```text
Game.sln
├─ Client       → Client.exe
├─ Engine       → Engine.lib
├─ Output       → Engine.lib 저장
└─ Resources
```

| 프로젝트 | 역할 | 빌드 결과물 |
|---|---|---|
| Client | 창 생성, 윈도우 메시지 처리, Game 실행 | 실행 파일 `.exe` |
| Engine | 공통 타입과 DX12 관련 설정 제공 | 정적 라이브러리 `.lib` |

Engine은 독립적으로 실행하는 프로그램이 아니다. **Client를 빌드할 때 필요한 엔진 구현을 연결해서 사용하는 정적 라이브러리**다.

앞으로 게임 실행 코드는 Client에, 여러 곳에서 사용할 그래픽 기능은 Engine에 추가하는 구조로 볼 수 있다.

## 헤더 포함과 라이브러리 연결

Client에서 Engine의 함수를 사용하려면 두 가지가 필요하다.

- 컴파일할 때 함수의 선언을 알아야 한다.
- 링크할 때 함수의 구현을 찾아야 한다.

이 예제에서는 `HelloEngine()`으로 연결을 확인한다.

**Engine/EnginePch.h — 함수 선언**

```cpp
void HelloEngine();
```

**Engine/EnginePch.cpp — 함수 정의**

```cpp
void HelloEngine()
{
}
```

**Client/Game.cpp — 엔진 함수 호출**

```cpp
void Game::Init()
{
  HelloEngine();
}
```

함수 내부가 비어 있으므로 호출해도 눈에 보이는 변화는 없다. 여기서는 Client가 Engine 쪽 구현을 사용할 수 있는지가 핵심이다.

### 헤더를 찾는 경로

Client의 x64 구성에는 다음 포함 경로가 설정되어 있다.

```text
$(SolutionDir)Engine\;$(IncludePath)
```

`$(SolutionDir)`는 솔루션이 있는 폴더다. 여기에 `Engine` 폴더를 추가해서 Client에서도 `EnginePch.h`를 찾을 수 있게 한다. 뒤의 `$(IncludePath)`는 기존 포함 경로를 이어받는 부분이다.

### 라이브러리를 찾는 경로

Engine의 출력 폴더와 Client의 라이브러리 검색 경로는 다음과 같이 연결된다.

```text
Engine 출력 폴더:       $(SolutionDir)Output\
Client 라이브러리 경로: $(SolutionDir)Output\;$(LibraryPath)
```

Engine을 빌드하면 `Output` 폴더에 `Engine.lib`가 만들어지고, Client는 이 폴더에서 라이브러리를 찾는다.

### 어떤 라이브러리를 연결할지 지정하기

Client의 `pch.h`에는 다음 코드가 있다.

```cpp
#pragma once

#pragma comment(lib, "Engine.lib")

#include "EnginePch.h"
```

`#include`는 엔진의 선언을 읽어오고, `#pragma comment(lib, "Engine.lib")`는 MSVC 링커에 해당 라이브러리를 연결하도록 지시한다.

설정의 역할을 정리하면 다음과 같다.

| 설정 | 역할 |
|---|---|
| 포함 경로 | 헤더를 찾을 폴더 지정 |
| `#include` | 헤더의 선언을 읽어옴 |
| 라이브러리 경로 | `.lib` 파일을 찾을 폴더 지정 |
| `#pragma comment(lib, ...)` | 연결할 라이브러리 지정 |

**헤더를 찾을 수 있다는 것과 함수 구현까지 연결되었다는 것은 별개**다. `HelloEngine()`도 선언만 읽고 구현을 연결하지 못하면 링크 오류가 발생한다.

## DX12 공통 환경 준비

`EnginePch.h`에는 DX12와 관련된 헤더와 라이브러리 설정을 모아 두었다.

| 헤더 | 용도 |
|---|---|
| `d3d12.h` | DX12 장치, 명령, 리소스 관련 API |
| `d3dx12.h` | DX12 구조체 설정 등을 돕는 보조 코드 |
| `dxgi.h` | 그래픽 어댑터와 화면 출력 관련 기능 |
| `wrl.h` | `ComPtr` 등 COM 객체 관리 도구 |
| `d3dcompiler.h` | 셰이더 컴파일 관련 기능 |
| `DirectXMath.h` | 벡터와 행렬 연산 |

라이브러리도 같은 헤더에서 연결하도록 지정한다.

```cpp
#pragma comment(lib, "d3d12")
#pragma comment(lib, "dxgi")
#pragma comment(lib, "dxguid")
#pragma comment(lib, "d3dcompiler")
```

게임 수학에 사용할 타입 별칭도 준비되어 있다.

```cpp
using Vec2 = XMFLOAT2;
using Vec3 = XMFLOAT3;
using Vec4 = XMFLOAT4;
using Matrix = XMMATRIX;
```

예를 들어 `Vec3`는 좌표 세 개를 저장하는 `XMFLOAT3`의 다른 이름이다. 새로운 벡터 클래스를 구현한 것은 아니며, 이번 단계에는 이 타입을 사용하는 계산 코드도 없다.

## pch.h의 역할

PCH는 자주 사용하는 헤더를 미리 컴파일해 두는 기능이다. Windows와 DX12 관련 헤더를 여러 소스 파일에서 사용할 때 빌드 부담을 줄이기 위해 사용한다.

이 예제의 포함 관계는 다음과 같다.

```text
Client 소스 → Client/pch.h → EnginePch.h
Engine 소스 → Engine/pch.h → EnginePch.h
```

두 프로젝트는 공통 헤더인 `EnginePch.h`를 읽지만, 미리 컴파일된 헤더는 각 프로젝트에서 따로 생성한다. 프로젝트 설정에서는 `pch.cpp`가 PCH를 생성하고, 다른 소스 파일은 이를 사용하도록 지정되어 있다.

## 프로그램 실행 흐름

`Client.cpp`의 시작점인 `wWinMain()`부터 보면 다음 순서로 진행된다.

```text
wWinMain()
  → 창 클래스 등록
  → 창 생성 및 표시
  → Game 객체 생성
  → Game::Init()
      → HelloEngine()
  → 반복문
      → 윈도우 메시지 확인 및 처리
      → Game::Update()
  → WM_QUIT 메시지를 받으면 종료
```

메시지 확인에는 `PeekMessage()`를 사용한다. 메시지가 없을 때도 바로 반환하므로 게임 업데이트를 계속 진행할 수 있다.

현재 `Game::Update()`는 비어 있고, 화면을 그리는 코드와 프레임 제한도 없다. 이번 단계에서 준비한 것은 **창이 열리고 게임 코드를 반복해서 실행할 수 있는 기본 흐름**이다.

## 예제 설정에서 확인할 점

Client의 Engine 포함 경로와 라이브러리 경로는 x64 구성에 설정되어 있다. 따라서 이 예제를 따라갈 때는 먼저 솔루션 플랫폼이 x64인지 확인한다.

또한 **라이브러리 연결과 프로젝트 빌드 순서는 별도 설정**이다. 제공된 파일에는 Client가 Engine을 먼저 빌드하도록 하는 프로젝트 참조나 의존성이 없고, `Output` 폴더에는 기존 `Engine.lib`가 들어 있다. 처음부터 빌드하거나 Engine 코드를 바꿀 때는 Engine의 빌드 결과가 먼저 준비되어야 한다.

Engine의 Debug/Release 및 x86/x64 구성은 모두 같은 `Output/Engine.lib`에 출력한다. 구성을 바꿔 빌드할 때는 이 파일이 다른 구성의 결과로 덮어써질 수 있다는 점도 알아두자.

이번 단계까지는 프로젝트 연결과 공통 환경 준비를 마쳤다. 실제 DX12 화면 출력을 위해서는 이후에 Device, Command Queue, Swap Chain 등을 생성하고 연결해야 한다.
