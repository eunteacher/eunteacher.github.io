---
layout: post
title: 'PlayerState가 소유한 ASC를 서버와 클라이언트에서 초기화하기'
date: 2026-02-16
categories: [Dev, UE5]
tags: [UE5, C++, GAS, AbilitySystemComponent, PlayerState, Multiplayer]
---

Gameplay Ability System에서 `AbilitySystemComponent`를 어디에 둘지는 캐릭터의 생명주기와 연결된다. 캐릭터가 죽고 다시 생성돼도 어빌리티, 어트리뷰트, 쿨다운처럼 플레이어에게 귀속된 상태를 유지해야 한다면 ASC를 `Character`가 아니라 `PlayerState`가 소유하도록 구성할 수 있다.

```text
PlayerState
└─ AbilitySystemComponent
   ├─ Gameplay Ability
   ├─ Gameplay Effect
   ├─ Gameplay Tag
   └─ Attribute Set

Character
└─ 현재 월드에서 ASC가 행동할 대상
```

이 구조에서는 ASC의 실제 소유자와 월드에서 움직이는 캐릭터가 서로 다르다. 새로운 캐릭터를 빙의하거나 클라이언트에 `PlayerState`가 복제됐을 때 두 객체의 관계를 ASC에 다시 알려줘야 한다.

## OwnerActor와 AvatarActor

`UAbilitySystemComponent::InitAbilityActorInfo`는 ASC가 사용할 `FGameplayAbilityActorInfo`를 초기화한다.

```cpp
AbilitySystemComponent->InitAbilityActorInfo(OwnerActor, AvatarActor);
```

두 인자는 다음 의미를 가진다.

| 구분 | 의미 | 이 구조에서의 객체 |
|---|---|---|
| `OwnerActor` | ASC를 논리적으로 소유하는 액터 | `PlayerState` |
| `AvatarActor` | 어빌리티가 현재 월드에서 행동할 물리적 대상 | `Character` |

`OwnerActor`는 어빌리티와 지속 상태의 주체다. `AvatarActor`는 애니메이션, 이동 컴포넌트, 위치와 같은 월드 표현을 제공한다.

```text
한 플레이어의 PlayerState
        │
        ├─ 첫 번째 Character  ← 사망 전 Avatar
        │
        └─ 두 번째 Character  ← 리스폰 후 Avatar
```

리스폰 뒤에도 같은 `PlayerState`와 ASC를 사용한다면 Owner는 유지되고 Avatar만 새 캐릭터로 바뀐다.

## 왜 양쪽에서 초기화해야 하는가

서버와 클라이언트는 각각 자신의 프로세스에 ASC와 ActorInfo를 가진다. 서버에서 `InitAbilityActorInfo`를 호출했다고 해서 클라이언트 메모리의 ActorInfo 함수 호출까지 복제되는 것은 아니다.

```text
Server process
└─ Server ASC ActorInfo

Client process
└─ Client ASC ActorInfo
```

서버는 권한 있는 어빌리티 실행과 Gameplay Effect 적용을 위해 올바른 Owner와 Avatar를 알아야 한다. 클라이언트도 로컬 입력과 예측, 애니메이션, Gameplay Cue, UI 바인딩 등에 사용할 ActorInfo가 필요하다.

모든 클라이언트 초기화의 목적을 예측 하나로만 설명할 수는 없다. 로컬 플레이어는 예측 실행에 ActorInfo를 사용하지만, 다른 플레이어를 나타내는 Simulated Proxy도 복제된 상태와 시각 표현을 처리하기 위해 올바른 Avatar 정보가 필요할 수 있다.

## 서버에서는 PossessedBy를 사용한다

`ACharacter::PossessedBy`는 서버에서 Controller가 Pawn을 빙의했을 때 호출된다. 이 시점에는 Character가 자신의 `PlayerState`를 찾을 수 있으므로 서버의 ASC를 초기화할 수 있다.

```cpp
void AMyCharacter::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);
    InitializeAbilitySystem();

    if (HasAuthority())
    {
        GrantStartupAbilities();
    }
}
```

`InitializeAbilitySystem`은 서버와 클라이언트에서 함께 사용할 공통 함수로 만든다.

```cpp
void AMyCharacter::InitializeAbilitySystem()
{
    AMyPlayerState* MyPlayerState = GetPlayerState<AMyPlayerState>();

    if (!IsValid(MyPlayerState))
    {
        return;
    }

    UAbilitySystemComponent* NewAbilitySystemComponent = MyPlayerState->GetAbilitySystemComponent();

    if (!IsValid(NewAbilitySystemComponent))
    {
        return;
    }

    AbilitySystemComponent = NewAbilitySystemComponent;
    AbilitySystemComponent->InitAbilityActorInfo(MyPlayerState, this);
}
```

Character가 `IAbilitySystemInterface`를 구현했다면 `GetAbilitySystemComponent`는 캐싱한 ASC를 반환하도록 구성할 수 있다.

```cpp
UAbilitySystemComponent* AMyCharacter::GetAbilitySystemComponent() const
{
    return AbilitySystemComponent;
}
```

ASC의 실제 소유권은 `PlayerState`에 있고 Character의 포인터는 접근을 위한 참조다. Character가 파괴됐다고 해서 PlayerState의 ASC까지 함께 파괴되는 구조는 아니다.

## 클라이언트에서는 OnRep_PlayerState를 사용한다

클라이언트의 Character는 네트워크를 통해 `PlayerState` 참조를 전달받는다. 해당 참조가 복제됐을 때 `APawn::OnRep_PlayerState`가 호출되므로 이 시점에 클라이언트의 ASC와 Avatar 관계를 초기화할 수 있다.

```cpp
void AMyCharacter::OnRep_PlayerState()
{
    Super::OnRep_PlayerState();
    InitializeAbilitySystem();

    if (IsLocallyControlled())
    {
        BindAbilitySystemToUI();
    }
}
```

이 패턴의 핵심은 `OnRep_PlayerState`라는 함수 이름 자체가 아니라 **클라이언트에서 PlayerState와 ASC 참조를 안전하게 얻을 수 있게 된 시점**을 사용하는 것이다.

복제 순서와 네트워크 지연 때문에 Character의 `BeginPlay`에서 PlayerState가 항상 준비됐다고 가정하면 안 된다.

```text
Character BeginPlay
        ↓
PlayerState가 아직 없을 수 있음
        ↓
PlayerState 참조 복제
        ↓
OnRep_PlayerState
        ↓
클라이언트 ASC 초기화
```

싱글플레이 또는 Standalone에서는 서버와 클라이언트 역할이 한 프로세스에 섞여 보여 타이밍 문제가 드러나지 않을 수 있다. 멀티플레이 PIE와 별도 프로세스 테스트에서도 확인해야 한다.

## 어빌리티 부여는 서버의 책임이다

ActorInfo 초기화와 어빌리티 부여는 다른 작업이다.

```text
InitAbilityActorInfo
└─ ASC가 누구의 것이며 어떤 Avatar를 사용하는지 연결

GiveAbility
└─ ASC에 실제 어빌리티 스펙 추가
```

Gameplay Ability의 부여와 제거는 서버에서 수행해야 한다.

```cpp
void AMyCharacter::GrantStartupAbilities()
{
    if (!HasAuthority() || !IsValid(AbilitySystemComponent))
    {
        return;
    }

    // 서버에서 필요한 어빌리티를 부여한다.
}
```

ASC가 PlayerState에 남아 있는 구조에서 Character가 리스폰할 때마다 같은 시작 어빌리티를 다시 부여하면 중복 스펙이 쌓일 수 있다. 다음과 같은 방법 중 프로젝트에 맞는 정책을 정해야 한다.

- PlayerState 또는 ASC에 초기 부여 완료 여부 저장
- 이미 같은 어빌리티가 있는지 확인한 뒤 부여
- 플레이어 최초 로그인과 Character 리스폰을 구분
- Pawn 전용 어빌리티라면 이전 Avatar에서 사용한 스펙을 제거한 뒤 다시 부여

지속돼야 하는 플레이어 어빌리티와 Pawn이 바뀔 때 교체돼야 하는 캐릭터 어빌리티를 같은 초기화 함수에 무조건 넣지 않는 편이 안전하다.

## 어트리뷰트 초기화 정책을 분리한다

ASC와 Attribute Set이 PlayerState에 있다면 Character의 재생성만으로 어트리뷰트 값이 자동 초기화되지 않는다. 이 특성은 상태 유지에 유리하지만, 리스폰 시 체력을 최대치로 되돌려야 하는 게임에서는 별도 정책이 필요하다.

```text
연결 단계
└─ InitAbilityActorInfo

최초 생성 단계
└─ 기본 Attribute Gameplay Effect 적용

리스폰 단계
└─ 체력처럼 초기화할 값만 Respawn Effect로 복구
```

`InitAbilityActorInfo`를 호출할 때마다 초기 Gameplay Effect를 다시 적용하면 영구 버프나 기본 수치가 중복될 수 있다. ActorInfo 연결과 게임 수치 초기화를 별도 함수와 별도 조건으로 관리해야 한다.

서버가 권한 있는 어트리뷰트 값을 변경하고 클라이언트는 복제된 결과를 표시하는 구조가 기본이다. 클라이언트에서 임의로 체력이나 이동속도를 다시 설정해 서버 값을 보정하려는 코드는 예측과 복제 상태를 오히려 어긋나게 만들 수 있다.

## UI 바인딩도 초기화와 분리한다

로컬 플레이어의 HUD는 ASC와 Attribute Set이 준비된 뒤 델리게이트에 연결할 수 있다. 하지만 `OnRep_PlayerState`나 Pawn 교체 과정에서 초기화 함수가 다시 호출될 수 있으므로 같은 델리게이트를 중복 등록하지 않도록 해야 한다.

```text
ASC ActorInfo 초기화
        ↓
로컬 플레이어인지 확인
        ↓
기존 UI 델리게이트 해제 또는 등록 여부 확인
        ↓
새 ASC에 UI 바인딩
```

UI는 로컬 클라이언트의 책임이고, 어빌리티 부여와 Gameplay Effect 적용은 서버의 책임이다. 두 작업이 같은 함수에서 우연히 연달아 실행되더라도 권한과 실행 위치는 구분해야 한다.

## 리스폰과 Pawn 교체를 고려한다

PlayerState가 ASC를 소유하는 가장 큰 이유 중 하나는 Pawn보다 긴 생명주기다. 새 Character가 생성되면 같은 ASC의 Avatar를 새 객체로 갱신해야 한다.

```text
기존 Character 제거
        ↓
새 Character 생성
        ↓
서버 PossessedBy
        ↓
InitAbilityActorInfo(PlayerState, NewCharacter)
        ↓
클라이언트 OnRep_PlayerState
        ↓
클라이언트도 NewCharacter를 Avatar로 연결
```

이전 Avatar에서 실행 중이던 어빌리티를 유지할지 취소할지도 게임 규칙에 따라 정해야 한다. 새 Avatar 연결만으로 모든 활성 상태가 원하는 형태로 정리된다고 가정하지 않는다.

Pawn이 ASC를 직접 소유하는 구조라면 Owner와 Avatar가 같은 Pawn이 될 수 있고 초기화 시점도 달라진다. `PossessedBy`와 `OnRep_PlayerState` 패턴은 **PlayerState가 ASC를 소유하고 Character가 Avatar가 되는 구성**에 맞춘 것이다.

## 점검할 항목

ASC 초기화에 문제가 있다면 다음 항목을 확인한다.

1. `PlayerState`가 `IAbilitySystemInterface`를 구현하고 올바른 ASC를 반환하는가?
2. ASC와 Attribute Set이 복제되도록 설정되어 있는가?
3. 서버의 `PossessedBy`에서 ActorInfo를 초기화하는가?
4. 클라이언트의 `OnRep_PlayerState`에서 ActorInfo를 초기화하는가?
5. `InitAbilityActorInfo`의 Owner와 Avatar 순서가 올바른가?
6. 시작 어빌리티를 서버에서만 부여하는가?
7. 리스폰할 때 같은 어빌리티나 초기 효과가 중복 적용되지 않는가?
8. UI 델리게이트가 중복 등록되지 않는가?
9. 새 Character가 ASC의 최신 Avatar로 설정됐는가?
10. 로컬 플레이어와 Simulated Proxy를 구분해서 테스트했는가?

## 정리

ASC를 PlayerState에 두면 플레이어의 어빌리티 상태를 Character의 생성과 파괴보다 오래 유지할 수 있다. 그 대신 ASC의 논리적 소유자와 물리적 표현이 분리되므로 서버와 클라이언트 각각에서 ActorInfo를 연결해야 한다.

```text
Server
└─ PossessedBy
   └─ InitAbilityActorInfo(PlayerState, Character)

Client
└─ OnRep_PlayerState
   └─ InitAbilityActorInfo(PlayerState, Character)
```

`PossessedBy`는 서버가 새 Avatar를 인식하는 시점이고, `OnRep_PlayerState`는 클라이언트가 복제된 PlayerState를 사용할 수 있게 된 시점이다. ActorInfo 초기화, 어빌리티 부여, 어트리뷰트 재설정, UI 바인딩을 서로 다른 책임으로 나누면 리스폰과 네트워크 타이밍에서 발생하는 중복 초기화 문제를 줄일 수 있다.

## 참고 자료

- [Ability System Component and Attributes](https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-ability-system-component-and-gameplay-attributes-in-unreal-engine)
- [UAbilitySystemComponent::InitAbilityActorInfo](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/GameplayAbilities/UAbilitySystemComponent/InitAbilityActorInfo)
- [Using Gameplay Abilities](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-gameplay-abilities-in-unreal-engine)
