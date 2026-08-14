---
layout: post
title: 'GameModeBase::PostLogin의 역할과 호출 시점'
date: 2026-01-18 22:00:00 +0900
categories: [Dev, UE5]
tags: [UE5, C++, GameModeBase, PostLogin, Multiplayer]
---

멀티플레이 게임에서 클라이언트가 서버에 접속하면 서버는 접속 가능 여부를 확인하고, 해당 플레이어를 나타낼 `PlayerController`를 생성한다. `AGameModeBase::PostLogin`은 이 로그인 절차가 성공한 뒤 호출되는 서버 측 함수다.

공식 문서에서는 `PostLogin`을 새로 생성된 `PlayerController`에서 복제 함수를 안전하게 호출할 수 있는 첫 지점으로 설명한다. 따라서 접속을 마친 플레이어를 서버의 게임 규칙에 편입시키는 초기화 지점으로 사용할 수 있다.

## GameModeBase는 서버에만 존재한다

`AGameModeBase` 인스턴스는 서버에만 생성되고 원격 클라이언트에는 복제되지 않는다. 그러므로 `PostLogin`도 서버에서만 실행된다.

```text
Server
└─ GameModeBase
   └─ PostLogin(NewPlayer)

Client
└─ GameModeBase 인스턴스 없음
```

`PostLogin`에서 서버 변수만 변경했다고 해서 클라이언트 화면에 그 값이 자동으로 나타나는 것은 아니다. 클라이언트도 알아야 하는 정보는 목적에 따라 다음 객체에 두어야 한다.

| 정보의 범위 | 적합한 위치 |
|---|---|
| 서버만 알아야 하는 게임 규칙 | `GameModeBase` |
| 모든 플레이어가 알아야 하는 경기 상태 | `GameStateBase` |
| 특정 플레이어의 공개 상태 | `PlayerState` |
| 해당 플레이어에게만 전달할 동작 | 소유 `PlayerController`의 Client RPC |

GameMode에서 접속을 감지하고, 실제 공유 데이터는 복제 가능한 객체에 저장하는 식으로 책임을 나눌 수 있다.

## 로그인 흐름에서의 위치

새 플레이어가 접속할 때의 주요 흐름은 다음과 같다.

```text
PreLogin
   ↓
Login
   ↓
PostLogin
   ↓
HandleStartingNewPlayer
   ↓
RestartPlayer
   ↓
Pawn 생성 및 Possess
```

각 단계의 역할은 서로 다르다.

| 단계 | 역할 |
|---|---|
| `PreLogin` | 접속을 승인하거나 거절한다 |
| `Login` | 플레이어와 연결할 `PlayerController`를 생성하고 기본 정보를 설정한다 |
| `PostLogin` | 로그인이 성공한 플레이어를 서버 게임 로직에 등록한다 |
| `HandleStartingNewPlayer` | 새 플레이어가 게임에 들어갈 준비를 처리한다 |
| `RestartPlayer` | 시작 지점을 고르고 기본 `Pawn`의 스폰을 시도한다 |

`PreLogin`에서 `ErrorMessage`를 비어 있지 않게 설정하면 로그인을 거절할 수 있다. `Login` 단계의 `PlayerController`는 네트워크 관점에서 아직 완전히 초기화되지 않았으므로 복잡한 게임 로직은 `PostLogin` 이후에 두는 편이 맞다.

## C++에서 PostLogin을 오버라이드한다

```cpp
// MyGameModeBase.h
virtual void PostLogin(APlayerController* NewPlayer) override;
```

```cpp
// MyGameModeBase.cpp
void AMyGameModeBase::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    if (!IsValid(NewPlayer))
    {
        return;
    }

    // 서버에서 새 플레이어에게 필요한 초기화 작업을 수행한다.
}
```

기본 클래스가 수행하는 로그인 후속 처리를 보존하려면 `Super::PostLogin(NewPlayer)`를 호출해야 한다. `AGameModeBase`의 기본 구현은 블루프린트의 `OnPostLogin` 이벤트와 이후 플레이어 시작 흐름에도 관여하므로 특별한 이유 없이 생략하면 안 된다.

프로젝트에서 C++ 초기화와 블루프린트 `OnPostLogin`을 함께 사용한다면 둘의 실행 순서도 중요하다. 위 코드처럼 함수 시작 부분에서 `Super`를 호출하면 기본 구현과 블루프린트 이벤트가 먼저 처리되고, 그 뒤에 작성한 C++ 코드가 실행된다. C++ 준비가 먼저 필요하다면 필요한 작업을 한 뒤 `Super`를 호출하도록 순서를 설계해야 한다.

## PostLogin에서 하기 적합한 작업

`PostLogin`에는 `PlayerController`가 만들어진 직후 서버가 처리해야 하는 작업이 잘 맞는다.

- 서버의 접속자 관리 목록에 플레이어 등록
- 팀 또는 진영 배정
- `PlayerState`에 초기 플레이어 정보 설정
- 관전자 여부 결정
- 소유 클라이언트에 초기 안내를 보내는 Client RPC 호출
- 모든 플레이어가 알아야 하는 입장 정보를 `GameState`에 반영

예를 들어 서버에서 플레이어의 팀을 정하고 `PlayerState`에 저장할 수 있다.

```cpp
void AMyGameModeBase::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    AMyPlayerState* NewPlayerState = NewPlayer ? NewPlayer->GetPlayerState<AMyPlayerState>() : nullptr;

    if (!IsValid(NewPlayerState))
    {
        return;
    }

    NewPlayerState->SetTeamId(ChooseTeam());
}
```

`PlayerState`의 값이 클라이언트에 복제되기 전에 서버에서 초기값을 설정하는 흐름이다. 실제 복제 변수에는 `UPROPERTY(Replicated)` 설정과 `GetLifetimeReplicatedProps` 등록이 별도로 필요하다.

## Pawn이 필요하다면 시점을 분리한다

`PostLogin`의 인자로 보장되는 객체는 로그인에 성공한 `PlayerController`다. 기본 흐름에서는 이후 `HandleStartingNewPlayer`와 `RestartPlayer`를 거쳐 `Pawn` 스폰과 빙의가 진행될 수 있다.

따라서 다음 코드는 `Pawn`이 항상 존재한다고 가정해서는 안 된다.

```cpp
void AMyGameModeBase::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    APawn* Pawn = NewPlayer ? NewPlayer->GetPawn() : nullptr;

    if (!IsValid(Pawn))
    {
        // 아직 Pawn이 생성되거나 빙의되지 않았을 수 있다.
        return;
    }
}
```

캐릭터 스폰 위치, 기본 `Pawn` 클래스 선택, 스폰 직후 설정처럼 `Pawn`이 필요한 로직은 목적에 맞는 함수를 사용한다.

| 필요한 작업 | 고려할 함수 |
|---|---|
| 플레이어 시작 가능 여부 변경 | `HandleStartingNewPlayer` |
| 시작 지점 선택 | `ChoosePlayerStart` 또는 `FindPlayerStart` |
| 플레이어 재시작과 Pawn 스폰 제어 | `RestartPlayer` 계열 |
| 기본 Pawn 클래스 선택 | `GetDefaultPawnClassForController` |
| Pawn 스폰 직후 추가 처리 | `OnRestartPlayer` 또는 Pawn의 초기화 지점 |

프로젝트가 기본 자동 스폰을 비활성화했거나 이미 존재하는 Pawn을 연결하는 구조라면 실제 순서는 달라질 수 있다. `PostLogin`이라는 이름만 보고 Pawn의 존재 여부를 단정하지 말고 해당 GameMode의 스폰 정책을 함께 확인해야 한다.

## 비동기 데이터 로드는 완료 시점을 따로 다룬다

계정이나 저장 데이터를 불러오는 작업이 오래 걸린다면 `PostLogin` 안에서 동기적으로 완료될 때까지 막아 두는 방식은 피하는 편이 좋다.

```text
PostLogin
   ↓
플레이어를 초기화 중 상태로 표시
   ↓
비동기 데이터 요청
   ↓
완료 콜백에서 PlayerState 갱신
   ↓
플레이 가능 상태로 전환
```

비동기 작업이 끝나기 전에 플레이어가 접속을 종료할 수도 있다. 완료 콜백에서는 `PlayerController`와 `PlayerState`가 여전히 유효한지 다시 확인해야 한다. 서버 전체가 알아야 하는 준비 상태라면 `GameState`, 개별 플레이어의 준비 상태라면 `PlayerState`에 두는 식으로 범위를 구분한다.

## PostLogin과 Logout을 한 쌍으로 본다

`PostLogin`에서 서버의 별도 배열이나 관리자 객체에 플레이어를 등록했다면 접속 종료 시 정리할 책임도 생긴다.

```cpp
void AMyGameModeBase::Logout(AController* Exiting)
{
    // PostLogin에서 등록한 서버 측 참조를 정리한다.

    Super::Logout(Exiting);
}
```

단순 접속자 목록이 필요하다면 별도 배열을 중복 관리하기 전에 `GameStateBase::PlayerArray`로 해결할 수 있는지도 확인한다. 별도 캐시가 필요하다면 끊긴 컨트롤러의 참조가 남지 않도록 `Logout`에서 제거한다.

## 정리

`PostLogin`은 로그인이 성공하고 `PlayerController`가 발급된 직후, 서버가 새 플레이어를 게임 규칙에 편입시키는 지점이다. 이 시점부터 해당 `PlayerController`의 복제 함수를 안전하게 호출할 수 있다.

핵심은 `PostLogin`에 모든 초기화를 몰아넣는 것이 아니다.

```text
접속 승인       → PreLogin
Controller 생성 → Login
서버 등록       → PostLogin
게임 참가 준비   → HandleStartingNewPlayer
Pawn 스폰       → RestartPlayer
공유 상태 복제   → GameState / PlayerState
```

각 객체가 존재하는 시점과 서버·클라이언트 중 어디에 존재하는지를 기준으로 초기화 책임을 나누면 멀티플레이 접속 흐름을 안정적으로 구성할 수 있다.

## 참고 자료

- [AGameModeBase::PostLogin](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/AGameModeBase/PostLogin)
- [Game Mode and Game State](https://dev.epicgames.com/documentation/en-us/unreal-engine/game-mode-and-game-state-in-unreal-engine)
