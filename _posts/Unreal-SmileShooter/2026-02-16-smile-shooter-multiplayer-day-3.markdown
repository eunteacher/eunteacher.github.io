---
layout: post
title: '[UE5 SmileShooter MultiPlayer 03] GAS 멀티플레이어 ASC 초기화 분석'
date: 2026-02-16
categories: [ Dev, Unreal, SubProject ]
tags: [UE5, Multiplayer, ASC, C++]
use_math: true
---

# [UE5] GAS: 서버와 클라이언트의 ASC 초기화 분석

Unreal Engine 5의 **GAS(Gameplay Ability System)**에서 서버(`PossessedBy`)와 클라이언트(`OnRep_PlayerState`) 양쪽에서 호출해야 하는 구조적 이유

## 소스 코드 (Source Code)

```cpp
/**
 * 서버 측 소유권 설정 시 호출
 * 서버 권한(Authority) 확립 및 데이터 복제의 기점
 */
void ASSCharacter::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);

    ASSMatchPlayerState* PS = GetPlayerState<ASSMatchPlayerState>();
    if (PS)
    {
       // 서버 측 ASC 참조 포인터 설정
       AbilitySystemComponent = Cast<USSAbilitySystemComponent>(PS->GetAbilitySystemComponent());

       // [중요] 서버 측 ASC 초기화: Owner(PlayerState)와 Avatar(Character) 관계 설정
       PS->GetAbilitySystemComponent()->InitAbilityActorInfo(PS, this);

       // 어트리뷰트 세트 캐싱 및 초기화
       HealthSet = PS->GetHealthSet();
       LocomotionSet = PS->GetLocomotionSet();

       InitializeAttributes();

       // 서버 전용: 초기 효과 및 어빌리티 부여 (Authority 전용 로직)
       AddStartupEffects();
       AddCharacterAbilities();

       ASSMatchPlayerController* PC = Cast<ASSMatchPlayerController>(GetController());
		   if (PC)
		   {
		   	 // TODO : UI 생성
		   }

       // 리스폰 시 상태 복구 (체력 및 속도 초기화)
       SetHealth(GetMaxHealth());
       SetMaxSpeed(GetMaxSpeed());
       SetSpeedRate(GetSpeedRate());
    }
}

/**
 * 클라이언트 측 PlayerState 복제 시 호출
 * 네트워크 프록시의 예측(Prediction) 활성화 시점
 */
void ASSCharacter::OnRep_PlayerState()
{
    Super::OnRep_PlayerState();

    ASSMatchPlayerState* PS = GetPlayerState<ASSMatchPlayerState>();
    if (PS)
    {
       // 클라이언트 측 ASC 참조 포인터 설정
       AbilitySystemComponent = Cast<USSAbilitySystemComponent>(PS->GetAbilitySystemComponent());

       // [중요] 클라이언트 측 ASC 초기화: 서버와 동일한 Actor 정보 등록
       // 이 과정이 완료되어야 클라이언트의 '예측(Prediction)' 시스템이 가동됨
       PS->GetAbilitySystemComponent()->InitAbilityActorInfo(PS, this);

       // 어트리뷰트 세트 연결
       HealthSet = PS->GetHealthSet();
       LocomotionSet = PS->GetLocomotionSet();

       InitializeAttributes();

       // 로컬 컨트롤러인 경우 UI/HUD 생성 로직 실행
       ASSMatchPlayerController* PC = Cast<ASSMatchPlayerController>(GetController());
		   if (PC)
		   {
		   	 // TODO : UI 생성
		   }

       // 리스폰 시 클라이언트 측 데이터 보정
       SetHealth(GetMaxHealth());
       SetMaxSpeed(GetMaxSpeed());
       SetSpeedRate(GetSpeedRate());
    }
}
```
---
## 내용 정리

### 1. 역할의 명확화 (Owner vs Avatar)
GAS에서 ASC는 두 가지 중요한 개념
* OwnerActor: 이 능력을 실제로 소유한 주체 (예: PlayerState)
* AvatarActor: 이 능력이 물리적으로 표현되는 주체 (예: Character)

이 두 액터가 누구인지 ASC에게 알려주는 함수가 InitAbilityActorInfo()

| 개념 | 역할 | 비고 |
| :--- | :--- | :--- |
| **Owner Actor** | 능력 소유 주체 (데이터 중심) | 주로 PlayerState (Level 전환 시 데이터 유지) |
| **Avatar Actor** | 능력 표현 주체 (물리 중심) | 주로 Character (월드 내 물리적 상호작용) |

### 2. 서버에서 호출하는 이유 (PossessedBy)
* **권한 부여**: 서버는 게임의 절대적인 기준입니다. PossessedBy는 서버에서 컨트롤러가 캐릭터를 제어하기 시작할 때 호출
* **어빌리티 부여 가능 상태**: 서버 ASC가 자신이 어떤 캐릭터(Avatar)를 조종하는지 알아야만, 서버에서 어빌리티를 실행(Activate)하거나 어트리뷰트(HP 등)를 변경할 때 그 결과를 올바른 캐릭터에게 적용하고 클라이언트로 리플리케이션(복제)할 수 있음

### 3. 클라이언트에서 호출하는 이유 (OnRep_PlayerState)
서버에서 호출했다고 해서 클라이언트의 ASC 정보가 자동으로 마법처럼 채워지지 않음

* **네트워크 타이밍 문제**: 멀티플레이어에서 Character가 먼저 스폰되고, 그 뒤에 PlayerState가 복제되어 들어옴. 클라이언트 입장에서는 Character가 처음 생겼을 때 자신의 주인(PlayerState)이 누구인지 모르는 상태일 수 있음
* **예측(Prediction) 시스템**: GAS의 가장 큰 장점은 클라이언트가 서버의 응답을 기다리지 않고 즉시 기술을 실행하는 '예측' 기능임. 클라이언트 ASC가 "내 주인은 PlayerState이고, 내 몸은 이 Character다"라는 것을 알고 있어야만 입력(Input)을 받았을 때 서버에 요청을 보내는 동시에 로컬에서 애니메이션을 재생하고 수치를 미리 깎는 등의 예측 시뮬레이션을 수행할 수 있음
* **UI 및 시각 효과**: 클라이언트에서 HP 바를 그리거나 이펙트를 생성할 때, ASC와 어트리뷰트 세트가 올바르게 연결되어 있어야 동기화된 데이터를 화면에 보여줄 수 있음

### 4. 왜 OnRep_PlayerState인가?
클라이언트에서는 PossessedBy가 호출되지 않음. 대신 서버로부터 PlayerState라는 변수가 복제되어 클라이언트에 도착하는 순간(OnRep_PlayerState)이 **"내가 누구인지 확실히 알게 되는 시점"**. 따라서 이때 초기화를 해주는 것이 가장 안전하고 확실


| 구분 | 서버 (PossessedBy) | 클라이언트 (OnRep_PlayerState) |
| :--- | :--- | :--- |
| **호출 시점** | 컨트롤러가 폰을 점유했을 때 | 서버로부터 PlayerState 복제가 완료됐을 때 |
| **주요 목적** | 어빌리티 실행 권한 부여 및 결과 복제 | 로컬 예측(Prediction) 활성화 및 UI 갱신 |
| **데이터 흐름** | 서버 메모리에 Actor 정보 등록 | 클라이언트 메모리에 Actor 정보 등록 |
