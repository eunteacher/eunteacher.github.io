---
layout: post
title:  "[UE5 SmileShooter MultiPlayer 01] GameModeBase: OnPostLogin()"
date:   2026-01-13 22:00:00 +0900
categories: [Dev, Unreal, SubProject]
tags: [UE5, C++, GameMode, Multiplayer]
---

# [UE5 SmileShooter MultiPlayer] GameModeBase: OnPostLogin의 역할과 생명주기

---
## OnPostLogin()

### 1.1 OnPostLogin의 정의
* **개념**: 플레이어가 서버 접속 절차를 완료하고 유효한 `PlayerController`를 발급받은 직후, **GameMode(Server Only)**에서 호출되는 함수
* **비유 (공항 입국 심사)**:
  > * **PreLogin**: 입국 심사 (밴 유저 확인, 서버 인원 확인)
  > * **PostLogin (Current)**: 심사 통과 후 여권(`PlayerController`)을 받고 공항 로비에 막 도착한 상태. 아직 수하물(`Pawn`)은 찾지 못함

### 1.2 서버 접속 및 호출 흐름 (Login Flow)
플레이어 접속 시 서버 함수 호출 순서는 다음과 같음

| 순서 | 함수명 | 역할 및 상태 |
| :--- | :--- | :--- |
| 1 | `PreLogin` | **접속 승인/거절 결정**. 아직 `PlayerController` 생성 전임. |
| 2 | `Login` | **PlayerController 생성**. ID 및 기본 네트워크 설정 완료됨. |
| 3 | **`PostLogin`** | **플레이어 초기화의 골든 타임**. 팀 배정, 데이터 로드 수행함. ($\text{Pawn} \approx \text{nullptr}$) |
| 4 | `RestartPlayer` | **Pawn 스폰 및 빙의(Possess)**. 캐릭터가 월드에 생성되는 시점임. |

### 1.3 주요 수행 작업
* **데이터 로드**: DB 등에서 인벤토리, 레벨, 경험치 정보를 가져와 `PlayerState`에 저장
* **팀 배정**: 게임 룰에 따라 아군/적군을 나누거나 `Spectator`(관전자)로 설정
* **컴포넌트 초기화**: `AbilitySystemComponent` 등 주요 컴포넌트의 초기 태그를 부여
* **브로드캐스팅**: "OOO님이 입장하셨습니다"와 같은 시스템 메시지를 전송

## 핵심 요약 및 주의사항
* **서버 전용 함수 (Server Only)**: `PostLogin`은 오직 서버(Authority)에서만 실행. 클라이언트(내 컴퓨터)에서는 호출되지 않으므로, UI 출력을 위해서는 반드시 **ClientRPC**를 호출해야 함
* **Pawn 접근 불가**: 이 단계에서 `NewPlayer->GetPawn()` 호출 시 `nullptr`일 가능성이 높음. 캐릭터(`Pawn`)의 위치나 스킨 등을 설정하려면 `PostLogin`이 아니라 `RestartPlayer`를 오버라이드하거나 그 이후 시점을 노려야 함
* **Blueprint 연동**: C++의 `PostLogin` 로직이 실행된 후, 블루프린트의 `Event OnPostLogin` 노드가 호출됨.

## Tip
* **최적화 및 구조**: `PostLogin`은 **"신분증(PlayerState) 발급 및 소속 결정"** 단계임. 무거운 리소스 로딩이나 캐릭터 물리 설정은 여기서 하지 말고, 데이터 세팅에 집중하는 것이 좋음
* **초기화 순서**: `GameMode::PostLogin` $\rightarrow$ `PlayerController::BeginPlay` $\rightarrow$ `Pawn::BeginPlay` 순서로 이어지는 라이프사이클을 이해하고 로직을 분산 배치해야 함

---

# 실습

OnPostLogin과 Spawner(Platform)를 연동한 캐릭터 생성

## SpawnerBase 소스 코드

```cpp
// ASSSpawnerBase.h
UFUNCTION(BlueprintCallable)
void SpawnPlayerCharacter(ASSPlayerController* InPlayerController);

UFUNCTION(BlueprintCallable)
void PlayerClear();

// ASSSpawnerBase.cpp
void ASSSpawnerBase::SpawnPlayerCharacter(ASSPlayerController* InPlayerController)
{
    // 필수 컴포넌트 유효성 검사
    if (!IsValid(ArrowComponent) || !IsValid(CharacterClass)) return;

    // 컨트롤러 설정 및 기존 캐릭터 정리
    PlayerController = InPlayerController;
    if (!IsValid(PlayerController)) return;

    if (IsValid(CurrentCharacter))
    {
        CurrentCharacter->Destroy(true);
    }

    if (UWorld* World = GetWorld())
    {
        // ArrowComponent를 기준점으로 스폰 위치/회전 계산
        const FVector SpawnLocation = ArrowComponent->GetComponentLocation();
        const FRotator SpawnRotation = ArrowComponent->GetComponentRotation();

        // 스폰 파라미터 설정
        FActorSpawnParameters SpawnParams;
        SpawnParams.Owner = this;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

        // 실제 액터(Character) 생성
        CurrentCharacter = World->SpawnActor<ASSCharacter>
        (
            CharacterClass,
            SpawnLocation,
            SpawnRotation,
            SpawnParams
        );
    }
}

void ASSSpawnerBase::PlayerClear()
{
    PlayerController = nullptr;
    if (IsValid(CurrentCharacter))
    {
        CurrentCharacter->Destroy(true);
        CurrentCharacter = nullptr;
    }
}
```

## LobbyGameModeBase: 플레이어 입장

### 01. 접속 감지 및 Setup

![OnPostLogin()](assets/img/SmileShooter_Lobby_GameMode_01.png)
![SetupPlatforms()](assets/img/SmileShooter_Lobby_GameMode_02.png)

* **Event OnPostLogin**: 접속한 플레이어의 `PlayerController`를 확인하고, 이를 관리하기 위해 배열(Array)에 추가
* **SetupPlatforms**: 레벨에 배치된 Platform 액터들을 `GetAllActorsOfClass` 으로 찾아와서 게임 모드에서 사용할 수 있도록 참조를 캐싱(Caching)

### 02. 초기화 대기
![SetupPlatformsDone](assets/img/SmileShooter_Lobby_GameMode_04.png)

* **상태 확인**: Platform 설정이 완전히 끝났는지 확인하는 로직. 비동기 로딩 등으로 인해 플랫폼이 아직 준비되지 않았을 때 로직이 실행되는 것을 방지
* **UpdatePlayerOnPlatforms 호출**: 준비가 완료된 경우에만 플레이어 갱신 함수를 호출하여 불필요한 연산을 줄임

### 03. 플레이어 스폰 및 정리

![SetupPlatformsDone](assets/img/SmileShooter_Lobby_GameMode_03.png)

* **신규 유저 스폰**: 현재 접속자 목록(Array)을 순회하며 아직 캐릭터가 생성되지 않은 플레이어가 있다면, 비어 있는 플랫폼 위치에 `SpawnActor`를 수행
* **나간 유저 정리**: `LobbyGameMode`의 관리 목록(Array)에는 없는데 플랫폼 위에 덩그러니 남겨진 캐릭터가 있다면, 이를 감지하여 `Destroy` 처리(데이터 무결성 유지)

# 결과

![SmileShooter_Lobby](assets/img/SmileShooter_Lobby.png)
