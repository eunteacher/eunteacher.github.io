---
layout: post
title: '[DX12 02] 장치 초기화와 배경색 출력'
date: 2026-10-23
categories: [Dev, DX12]
tags: [DX12, C++, Device, CommandQueue, SwapChain, DescriptorHeap, Fence]
---

이전 단계에서는 Client와 Engine을 연결하고 DX12 헤더와 라이브러리를 준비했다. 이번에는 실제 DX12 객체를 생성하고, 창의 배경을 하나의 색으로 채워 화면에 출력한다.

장치 초기화라는 이름이지만 Device만 생성하는 것은 아니다. GPU에 명령을 전달하고, 화면에 표시할 버퍼를 준비하고, GPU 작업이 끝났는지 확인하는 흐름까지 구성한다.

## 1. 이전 프로젝트에서 달라진 점

| 항목 | SetProject | DeviceInitialization |
|---|---|---|
| 초기화 | `HelloEngine()` 호출 | `GEngine->Init(info)` 호출 |
| 업데이트 | 비어 있음 | `GEngine->Render()` 호출 |
| 창 정보 | Client 내부에서 사용 | Engine에 전달 |
| DX12 객체 | 생성하지 않음 | Device, 명령 큐, 스왑 체인 등 생성 |
| 렌더링 | 없음 | 배경색을 채우고 화면에 표시 |

새로 추가된 클래스의 역할은 다음과 같다.

| 클래스 | 역할 |
|---|---|
| `Engine` | 초기화와 렌더링 순서 관리 |
| `Device` | DX12 Device와 DXGI Factory 생성 |
| `CommandQueue` | 명령 기록·제출과 GPU 작업 완료 대기 |
| `SwapChain` | 화면 출력용 버퍼 관리 |
| `DescriptorHeap` | 백 버퍼의 RTV 생성과 관리 |

## 2. 초기화의 전체 흐름

Client는 창을 만든 뒤 창 정보를 Engine에 전달한다.

```cpp
void Game::Init(const WindowInfo& info)
{
  GEngine->Init(info);
}
```

`WindowInfo`에는 창 핸들, 너비, 높이, 창 모드 여부가 들어 있다. Engine은 이 정보를 바탕으로 출력 영역과 스왑 체인을 설정한다.

초기화 순서는 다음과 같다.

```text
창 정보 저장과 크기 설정
  → Viewport와 Scissor Rect 설정
  → 관리 클래스 객체 생성
  → Device 초기화
  → CommandQueue 초기화
  → SwapChain 초기화
  → DescriptorHeap 초기화
```

순서는 객체 사이의 의존 관계에 따라 정해진다.

Device가 있어야 명령 큐를 만들 수 있고, 이 코드에서는 명령 큐를 스왑 체인 생성에 전달한다. 스왑 체인의 버퍼를 얻은 다음에는 각 버퍼를 렌더 타깃으로 사용할 RTV를 만든다.

## 3. Device와 DXGI Factory

`Device::Init()`은 다음 작업을 수행한다.

```text
Debug 빌드에서 디버그 레이어 활성화
  → DXGI Factory 생성
  → DX12 Device 생성
```

디버그 레이어는 DX12 사용 과정의 오류를 찾는 데 도움을 준다. Device를 만들기 전에 활성화한다.

DXGI는 그래픽 어댑터와 화면 출력 관련 기능을 제공한다. 이번 코드에서는 DXGI Factory를 만들어 두고, 나중에 스왑 체인을 생성할 때 사용한다.

Device 생성 코드는 다음과 같다.

```cpp
::D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&_device));
```

첫 번째 인수에 `nullptr`를 전달해 기본 어댑터를 사용한다. `D3D_FEATURE_LEVEL_11_0`은 요구하는 최소 하드웨어 기능 수준이다.

**Feature Level이 `11_0`이어도 생성하는 것은 DX12 Device다.** API 버전과 하드웨어 기능 수준은 구분해야 한다.

이렇게 만든 `ID3D12Device`를 통해 명령 큐, 리소스, Descriptor Heap 등 필요한 객체를 생성한다.

DX12 객체는 `ComPtr`로 보관한다. `ComPtr`는 COM 객체의 참조 수 관리를 도와주어 객체 수명을 관리하기 쉽게 해준다.

## 4. GPU에 명령을 전달하는 세 객체

DX12에서는 CPU가 GPU 작업을 명령 리스트에 기록한 뒤, 명령 큐를 통해 제출한다.

| 객체 | 역할 |
|---|---|
| Command List | GPU가 수행할 명령을 기록 |
| Command Allocator | 명령 기록에 필요한 메모리를 관리 |
| Command Queue | 기록한 명령 리스트를 GPU에 제출 |

흐름으로 보면 다음과 같다.

```text
CPU에서 명령 기록
  → Command List 기록 종료
  → Command Queue에 제출
  → GPU에서 실행
```

예를 들어 다음 코드는 백 버퍼를 특정 색으로 지우라는 명령을 기록한다.

```cpp
_cmdList->ClearRenderTargetView(backBufferView, Colors::LightSteelBlue, 0, nullptr);
```

**이 호출이 반환되었다고 GPU의 색 지우기 작업까지 끝난 것은 아니다.**

이후 `ExecuteCommandLists()`로 명령을 제출하고, GPU가 제출된 명령을 실행한다.

## 5. Swap Chain과 백 버퍼

스왑 체인은 화면 출력에 사용할 버퍼들을 관리한다. 이번 프로젝트는 버퍼 두 개를 사용하는 구성을 준비한다.

화면에 표시할 이미지와 다음 이미지를 그릴 공간을 번갈아 사용하는 방식으로 이해할 수 있다.

```text
버퍼 0에 렌더링 → Present()
버퍼 1에 렌더링 → Present()
버퍼 0에 렌더링 → ...
```

`Present()`는 렌더링한 버퍼를 화면 표시 과정에 넘기는 역할을 한다.

이 예제에서는 현재 백 버퍼 인덱스를 직접 관리하고, 프레임 끝에서 다음과 같이 변경한다.

```cpp
_backBufferIndex = (_backBufferIndex + 1) % SWAP_CHAIN_BUFFER_COUNT;
```

버퍼 개수가 2이므로 인덱스는 `0 → 1 → 0` 순서로 반복된다.

## 6. 백 버퍼와 RTV의 차이

백 버퍼가 준비되면, 해당 버퍼를 렌더 타깃으로 사용할 RTV를 만든다.

RTV는 Render Target View의 약자다. 리소스를 렌더링 결과를 기록할 대상으로 사용하기 위한 설명 정보다.

| 구성 요소 | 의미 |
|---|---|
| 백 버퍼 | 실제 이미지 데이터를 담는 리소스 |
| RTV | 해당 리소스를 렌더 타깃으로 사용하는 데 필요한 설명 정보 |
| RTV Descriptor Heap | RTV들을 저장하는 공간 |

이번 프로젝트에서는 버퍼 두 개에 대응하는 RTV 두 개를 만든다.

```text
RTV Heap
  ├─ RTV 0 → 백 버퍼 0
  └─ RTV 1 → 백 버퍼 1
```

**RTV 자체가 이미지 데이터를 저장하는 것은 아니다.**

현재 백 버퍼에 맞는 RTV를 선택해서 색 지우기와 렌더 타깃 설정에 사용한다.

## 7. 버퍼 상태를 전환하는 Resource Barrier

같은 백 버퍼라도 현재 무엇에 사용하는지에 따라 필요한 상태가 다르다.

| 상태 | 용도 |
|---|---|
| `D3D12_RESOURCE_STATE_PRESENT` | 화면 표시 |
| `D3D12_RESOURCE_STATE_RENDER_TARGET` | 렌더링 결과 기록 |

이번 코드에서는 렌더링을 시작하기 전에 그리기용 상태로 바꾸고, 작업이 끝나면 화면 표시용 상태로 되돌린다.

```text
PRESENT
  → Resource Barrier
RENDER_TARGET
  → 배경색 지우기와 렌더링
  → Resource Barrier
PRESENT
```

Resource Barrier로 이 상태 전환을 명령 리스트에 기록한다.

## 8. Viewport와 Scissor Rect

Viewport와 Scissor Rect는 렌더링 영역을 설정하지만 역할이 다르다.

- **Viewport**는 렌더링 결과를 화면의 어느 위치와 크기로 배치할지 설정한다.
- **Scissor Rect**는 픽셀을 그릴 수 있는 사각형 영역을 제한한다.

이번에는 둘 다 요청한 창 크기를 기준으로 설정한다.

명령 리스트를 Reset한 뒤에는 이 설정들을 다시 기록한다.

## 9. Fence로 GPU 작업 완료 기다리기

CPU는 명령을 제출한 뒤에도 계속 실행할 수 있다. 따라서 명령 제출과 GPU 작업 완료는 같은 시점이 아니다.

GPU가 사용 중인 명령 기록용 메모리를 CPU가 너무 일찍 초기화하면 문제가 생길 수 있다. 이번 코드는 Fence로 이전 작업이 끝났는지 확인한다.

```text
GPU에 명령 제출
  → 명령 큐에 Fence 값 갱신 예약
  → CPU에서 Fence 완료 값 확인
  → 아직 완료되지 않았다면 이벤트 대기
  → GPU가 지정한 지점까지 처리하면 대기 종료
```

`WaitSync()`는 Fence 값을 증가시키고 명령 큐에 `Signal()`을 요청한다. 큐가 앞선 명령들을 처리한 뒤 해당 값에 도달하면 완료 여부를 확인할 수 있다.

이 예제는 매 프레임 GPU 작업이 끝날 때까지 기다린다. 동작을 이해하기 쉬운 대신 CPU와 GPU의 작업을 여러 프레임에 걸쳐 겹치는 최적화는 아직 적용하지 않았다.

## 10. 한 프레임의 전체 흐름

`Game::Update()`에서 호출하는 `Engine::Render()`는 다음 과정을 반복한다.

```text
Command Allocator와 Command List Reset
  → 백 버퍼를 RENDER_TARGET 상태로 전환
  → Viewport와 Scissor Rect 설정
  → 백 버퍼를 LightSteelBlue로 지우기
  → 렌더 타깃 지정
  → 백 버퍼를 PRESENT 상태로 전환
  → Command List Close
  → ExecuteCommandLists()
  → Present()
  → Fence로 GPU 작업 완료 대기
  → 다음 백 버퍼 인덱스 선택
```

현재는 도형을 그리는 명령이 없으므로, 코드가 정상적으로 실행되면 연한 파란색 배경만 출력된다.

이번 단계에서는 **GPU 작업을 준비하고, 명령을 기록·제출하고, 그 결과를 화면에 표시하는 기본 흐름**을 구성했다.
