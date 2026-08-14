---
layout: post
title: 'Enhanced Input과 GAS Ability를 Gameplay Tag로 연결하기'
date: 2026-02-16 12:00:00 +0900
categories: [Dev, UE5]
tags: [UE5, C++, GAS, EnhancedInput, GameplayTag, AbilitySystemComponent]
---

Enhanced Input의 `InputAction`과 GAS의 `GameplayAbility`를 직접 연결하기 시작하면 캐릭터 클래스에 입력별 코드가 계속 늘어난다.

```cpp
UPROPERTY(EditDefaultsOnly)
TObjectPtr<UInputAction> FireAction;

UPROPERTY(EditDefaultsOnly)
TObjectPtr<UInputAction> DashAction;

UPROPERTY(EditDefaultsOnly)
TObjectPtr<UInputAction> ConfirmAction;
```

어빌리티가 추가될 때마다 `InputAction` 변수, 바인딩 코드, 콜백 함수도 함께 추가해야 한다. Character가 Fire 입력은 어떤 Ability를 실행해야 하는지까지 알고 있기 때문에 입력과 게임 기능의 결합도 높아진다.

이 문제를 해결하기 위해 Enhanced Input과 GAS 사이에 Gameplay Tag를 둔다.

```text
IA_Fire
   ↓
Input.Ability.Fire
   ↓
같은 태그를 가진 AbilitySpec
   ↓
Fire Ability 활성화
```

Character는 어떤 어빌리티가 실행되는지 알 필요가 없다. 입력에서 변환된 태그를 ASC에 전달하고, ASC가 해당 태그를 가진 AbilitySpec을 찾는다.

## 세 시스템의 책임을 나눈다

입력 연결 구조는 세 단계로 나눌 수 있다.

| 단계 | 책임 |
|---|---|
| Enhanced Input | 키와 패드 입력을 `InputAction`으로 해석 |
| InputConfig | `InputAction`을 프로젝트의 입력 태그와 연결 |
| AbilitySystemComponent | 입력 태그와 일치하는 AbilitySpec 처리 |

Gameplay Tag는 두 시스템이 공유하는 식별자다.

```text
Enhanced Input은 Ability 클래스를 모름
GAS는 실제 키와 InputAction을 모름
두 시스템은 Input.* 태그만 공유
```

키 설정이 바뀌어도 Ability는 영향을 받지 않고, Ability 클래스가 교체돼도 같은 입력 태그를 사용하면 입력 계층은 그대로 유지된다.

## InputAction과 Gameplay Tag를 데이터로 연결한다

먼저 입력 액션 하나와 태그 하나를 묶는 구조체를 만든다.

```cpp
USTRUCT(BlueprintType)
struct FTaggedInputAction
{
    GENERATED_BODY()

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    TObjectPtr<UInputAction> InputAction = nullptr;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta = (Categories = "Input"))
    FGameplayTag InputTag;
};
```

입력 설정은 `UDataAsset`로 관리한다.

```cpp
UCLASS(BlueprintType)
class UMyInputConfig : public UDataAsset
{
    GENERATED_BODY()

public:
    const UInputAction* FindInputActionForTag(const FGameplayTag& InputTag) const;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    TArray<FTaggedInputAction> NativeInputActions;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    TArray<FTaggedInputAction> AbilityInputActions;
};
```

이동과 시점 회전처럼 Character가 직접 처리할 입력과 GAS로 전달할 입력을 분리했다.

```text
NativeInputActions
├─ Input.Move
├─ Input.Look
└─ Input.Jump

AbilityInputActions
├─ Input.Ability.Fire
├─ Input.Ability.Dash
└─ Input.Ability.Confirm
```

모든 입력을 GAS에 넣는 것이 목적은 아니다. 단순 이동처럼 어빌리티의 비용, 쿨다운, 태그 차단, 예측이 필요하지 않은 입력은 일반 입력으로 처리할 수 있다.

태그에 해당하는 액션을 찾는 함수는 다음과 같이 작성한다.

```cpp
const UInputAction* UMyInputConfig::FindInputActionForTag(const FGameplayTag& InputTag) const
{
    for (const FTaggedInputAction& Action : NativeInputActions)
    {
        if (Action.InputAction && Action.InputTag.MatchesTagExact(InputTag))
        {
            return Action.InputAction;
        }
    }

    for (const FTaggedInputAction& Action : AbilityInputActions)
    {
        if (Action.InputAction && Action.InputTag.MatchesTagExact(InputTag))
        {
            return Action.InputAction;
        }
    }

    return nullptr;
}
```

입력 식별에는 `MatchesTagExact`를 사용했다. `Input.Ability`와 `Input.Ability.Fire`처럼 부모·자식 태그까지 같은 입력으로 취급하려는 설계가 아니라면 정확히 일치시키는 편이 안전하다.

## 개별 Ability 입력을 한 번에 바인딩한다

기존에는 Fire, Confirm, Cancel 입력을 `SetupPlayerInputComponent`에서 하나씩 바인딩했다.

```cpp
BindActionByTag(InputConfig, InputTag_Fire, ETriggerEvent::Started, this, &AMyCharacter::OnFirePressed);
BindActionByTag(InputConfig, InputTag_Confirm, ETriggerEvent::Started, this, &AMyCharacter::OnConfirmPressed);
BindActionByTag(InputConfig, InputTag_Cancel, ETriggerEvent::Started, this, &AMyCharacter::OnCancelPressed);
```

이 방식은 액션 에셋의 하드코딩은 줄이지만, 새 입력 태그가 생길 때마다 C++ 바인딩을 추가해야 한다. 데이터 에셋의 `AbilityInputActions`를 순회해 모두 같은 중계 함수로 연결해야 데이터 주도 구조가 완성된다.

```cpp
UCLASS()
class UMyEnhancedInputComponent : public UEnhancedInputComponent
{
    GENERATED_BODY()

public:
    template<class UserClass, typename PressedFuncType, typename ReleasedFuncType>
    void BindAbilityActions(const UMyInputConfig* InputConfig, UserClass* Object, PressedFuncType PressedFunc, ReleasedFuncType ReleasedFunc);
};
```

```cpp
template<class UserClass, typename PressedFuncType, typename ReleasedFuncType>
void UMyEnhancedInputComponent::BindAbilityActions(const UMyInputConfig* InputConfig, UserClass* Object, PressedFuncType PressedFunc, ReleasedFuncType ReleasedFunc)
{
    if (!IsValid(InputConfig) || !IsValid(Object))
    {
        return;
    }

    for (const FTaggedInputAction& Action : InputConfig->AbilityInputActions)
    {
        if (!IsValid(Action.InputAction) || !Action.InputTag.IsValid())
        {
            continue;
        }

        BindAction(Action.InputAction, ETriggerEvent::Started, Object, PressedFunc, Action.InputTag);
        BindAction(Action.InputAction, ETriggerEvent::Completed, Object, ReleasedFunc, Action.InputTag);
        BindAction(Action.InputAction, ETriggerEvent::Canceled, Object, ReleasedFunc, Action.InputTag);
    }
}
```

`BindAction`의 추가 인자로 `InputTag`를 넘기면 모든 Ability 입력을 두 개의 공통 콜백으로 받을 수 있다.

`Started`, `Completed`, `Canceled`의 실제 발생 조건은 InputAction에 설정한 Trigger에 따라 달라진다. 특히 Hold나 Press And Release Trigger를 사용한다면 에디터의 Trigger 설정과 바인딩한 `ETriggerEvent`가 의도한 시점에 호출되는지 확인해야 한다.

## Character는 입력 태그만 전달한다

```cpp
void AMyCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    UMyEnhancedInputComponent* EnhancedInputComponent = Cast<UMyEnhancedInputComponent>(PlayerInputComponent);

    if (!IsValid(EnhancedInputComponent) || !IsValid(InputConfig))
    {
        return;
    }

    EnhancedInputComponent->BindAbilityActions(InputConfig, this, &AMyCharacter::AbilityInputTagPressed, &AMyCharacter::AbilityInputTagReleased);
}
```

```cpp
void AMyCharacter::AbilityInputTagPressed(FGameplayTag InputTag)
{
    if (IsLocallyControlled() && IsValid(AbilitySystemComponent))
    {
        AbilitySystemComponent->AbilityInputTagPressed(InputTag);
    }
}

void AMyCharacter::AbilityInputTagReleased(FGameplayTag InputTag)
{
    if (IsLocallyControlled() && IsValid(AbilitySystemComponent))
    {
        AbilitySystemComponent->AbilityInputTagReleased(InputTag);
    }
}
```

Character에는 Fire나 Dash 같은 구체적인 Ability 이름이 없다. 로컬 입력을 받은 뒤 태그를 ASC에 넘기는 역할만 수행한다.

입력 콜백이 실행될 때 ASC의 ActorInfo도 초기화돼 있어야 한다. ASC를 PlayerState에 두는 구조라면 서버의 `PossessedBy`와 클라이언트의 `OnRep_PlayerState`에서 `InitAbilityActorInfo`가 먼저 완료되는지 확인한다.

## Ability의 태그를 AbilitySpec에 등록한다

기본 Ability 클래스에는 입력을 식별할 태그를 둔다.

```cpp
UCLASS()
class UMyGameplayAbility : public UGameplayAbility
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (Categories = "Input.Ability"))
    FGameplayTag StartupInputTag;
};
```

여기서 처음 막혔던 부분은 `StartupInputTag`를 Ability에 설정하면 `FGameplayAbilitySpec`에도 자동으로 들어갈 것이라고 생각한 점이다.

```text
Ability CDO의 StartupInputTag
≠
AbilitySpec의 Dynamic Spec Source Tag
```

ASC의 입력 검색 코드는 `Spec.GetDynamicSpecSourceTags()`를 확인한다. 따라서 어빌리티를 부여할 때 Ability의 입력 태그를 Spec에 직접 복사해야 한다.

```cpp
void UMyAbilitySystemComponent::GrantAbility(TSubclassOf<UMyGameplayAbility> AbilityClass, int32 AbilityLevel)
{
    if (!IsOwnerActorAuthoritative() || !AbilityClass)
    {
        return;
    }

    const UMyGameplayAbility* AbilityCDO = AbilityClass->GetDefaultObject<UMyGameplayAbility>();
    FGameplayAbilitySpec AbilitySpec(AbilityClass, AbilityLevel);

    if (AbilityCDO->StartupInputTag.IsValid())
    {
        AbilitySpec.GetDynamicSpecSourceTags().AddTag(AbilityCDO->StartupInputTag);
    }

    GiveAbility(AbilitySpec);
}
```

어빌리티 부여는 서버에서만 수행한다. AbilitySpec과 Dynamic Spec Source Tag는 클라이언트로 복제되므로 클라이언트 ASC도 같은 입력 태그로 Spec을 찾을 수 있다.

```text
서버에서 Ability 부여
        ↓
StartupInputTag를 Spec에 추가
        ↓
AbilitySpec 복제
        ↓
클라이언트가 입력 태그로 Spec 검색 가능
```

입력은 정상적으로 감지되는데 `TryActivateAbility`까지 도달하지 않는다면 Ability 에셋뿐 아니라 런타임 Spec에 태그가 실제로 들어 있는지 확인해야 한다.

## ASC에서 Pressed와 Released를 처리한다

버튼을 누르는 순간 한 번 실행되는 Ability라면 `TryActivateAbility`만으로 동작하는 것처럼 보일 수 있다. 하지만 차징, 조준 유지, 연속 발사처럼 버튼을 놓는 시점이 필요한 Ability는 입력 해제까지 전달해야 한다.

```cpp
void UMyAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
    if (!InputTag.IsValid())
    {
        return;
    }

    for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
    {
        if (!AbilitySpec.Ability || !AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
        {
            continue;
        }

        AbilitySpec.InputPressed = true;

        if (AbilitySpec.IsActive())
        {
            AbilitySpecInputPressed(AbilitySpec);
            InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, AbilitySpec.Handle, AbilitySpec.ActivationInfo.GetActivationPredictionKey());
        }
        else
        {
            TryActivateAbility(AbilitySpec.Handle);
        }
    }
}
```

```cpp
void UMyAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag)
{
    if (!InputTag.IsValid())
    {
        return;
    }

    for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
    {
        if (!AbilitySpec.Ability || !AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
        {
            continue;
        }

        AbilitySpec.InputPressed = false;

        if (AbilitySpec.IsActive())
        {
            AbilitySpecInputReleased(AbilitySpec);
            InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, AbilitySpec.Handle, AbilitySpec.ActivationInfo.GetActivationPredictionKey());
        }
    }
}
```

이미 실행 중인 Ability에는 단순히 다시 활성화를 요청하지 않고 Input Pressed 또는 Released 이벤트를 전달한다. Ability 안에서는 `Wait Input Press`와 `Wait Input Release` 같은 Ability Task로 이 신호를 기다릴 수 있다.

프로젝트의 어빌리티 입력 정책이 더 복잡하다면 콜백에서 즉시 활성화하지 않고 Pressed, Released, Held Spec Handle을 모은 뒤 ASC의 별도 `ProcessAbilityInput` 단계에서 처리할 수도 있다. 입력 차단 태그나 한 프레임 내 우선순위가 필요할 때 유용하다.

## 실제로 막혔던 연결 지점

처음 구조를 만들었을 때 Enhanced Input의 콜백 로그는 정상적으로 출력됐지만 Ability가 활성화되지 않았다. 입력 자체가 들어오지 않는 문제처럼 보였지만 흐름을 단계별로 나눠 확인하니 원인은 중간 연결에 있었다.

```text
InputAction 발생              성공
Character 콜백 호출           성공
ASC AbilityInputTagPressed   호출 성공
태그와 일치하는 Spec 검색      실패
TryActivateAbility           도달하지 않음
```

Ability 에셋의 `StartupInputTag`는 설정돼 있었지만 어빌리티 부여 시 그 태그를 `FGameplayAbilitySpec`에 복사하지 않았다. ASC는 Spec의 Dynamic Tag를 검색하고 있었으므로 일치하는 항목이 하나도 없었다.

```cpp
AbilitySpec.GetDynamicSpecSourceTags().AddTag(AbilityCDO->StartupInputTag);
```

이 연결을 추가한 뒤 입력 태그로 AbilitySpec을 찾을 수 있었다. 이후 Pressed만 전달하던 구조에 Released 처리를 추가해 버튼을 누르고 있는 동안 실행되는 기능과 놓을 때 종료되는 기능도 같은 경로로 처리했다.

또한 Ability 입력을 하나씩 직접 바인딩하던 코드를 `AbilityInputActions` 순회 방식으로 바꾸면서 새 Ability 입력은 다음 데이터만 준비하면 연결할 수 있게 했다.

1. Gameplay Tag 등록
2. InputAction 생성
3. InputConfig에 InputAction과 Tag 추가
4. Ability 에셋의 `StartupInputTag` 설정
5. 서버에서 해당 Ability 부여

Ability 클래스와 입력 태그를 부여하는 로직 자체는 여전히 C++ 또는 별도 데이터 정의가 필요하다. 따라서 “어떤 입력도 C++ 수정 없이 무조건 추가된다”기보다, **공통 바인딩 코드의 수정 없이 데이터 항목을 확장할 수 있는 구조**라고 표현하는 편이 정확하다.

## 입력이 동작하지 않을 때 확인할 순서

### 1. Mapping Context가 등록됐는가

InputAction을 만들고 바인딩해도 로컬 플레이어의 `UEnhancedInputLocalPlayerSubsystem`에 Mapping Context가 없으면 입력이 발생하지 않는다.

### 2. InputAction의 Trigger와 ETriggerEvent가 맞는가

`Started`, `Triggered`, `Completed`, `Canceled`는 같은 의미가 아니다. Hold와 Tap처럼 Trigger 구성이 달라지면 이벤트 발생 시점도 달라진다.

### 3. InputConfig에 유효한 Action과 Tag가 들어 있는가

에셋 포인터가 비어 있거나 태그가 Gameplay Tag Dictionary에 등록되지 않았다면 바인딩 단계에서 건너뛴다.

### 4. 로컬 Pawn에서 입력을 전달하고 있는가

입력은 로컬로 제어하는 Pawn이나 PlayerController에서 ASC로 전달해야 한다. Simulated Proxy가 같은 입력 경로를 실행하지 않도록 확인한다.

### 5. ASC ActorInfo가 초기화됐는가

`InitAbilityActorInfo`가 완료되지 않았다면 로컬 제어 정보와 Avatar 참조가 준비되지 않아 Ability 활성화와 예측이 실패할 수 있다.

### 6. 서버가 Ability를 부여했는가

`GiveAbility`는 서버에서 수행해야 한다. 클라이언트의 `GetActivatableAbilities()`에 해당 Spec이 복제됐는지도 확인한다.

### 7. Spec에 입력 태그가 있는가

Ability CDO의 태그와 Spec의 Dynamic Tag를 각각 로그로 출력해 중간 복사 단계가 빠지지 않았는지 확인한다.

### 8. 활성화 조건에서 막히지 않았는가

Spec을 찾았더라도 비용, 쿨다운, 차단 태그, 네트워크 실행 정책 때문에 `TryActivateAbility`가 실패할 수 있다. Ability 실패 태그와 GAS 디버그 정보를 함께 확인한다.

### 9. Released 이벤트가 전달되는가

활성화는 되지만 차징이나 연속 동작이 끝나지 않는다면 `Completed`와 `Canceled`가 Released 경로로 들어오는지 확인한다.

## 정리

Enhanced Input과 GAS를 Gameplay Tag로 연결하면 입력 장치와 Ability 클래스 사이의 직접 의존성을 줄일 수 있다.

```text
Key / Gamepad
      ↓
Input Mapping Context
      ↓
InputAction
      ↓
InputConfig의 Gameplay Tag
      ↓
AbilitySpec의 Dynamic Spec Source Tag
      ↓
ASC 입력 처리
      ↓
GameplayAbility
```

이 구조에서 가장 중요한 부분은 태그 이름을 만드는 것이 아니라 각 단계에 같은 태그가 실제로 전달되는지 확인하는 것이다. Ability의 `StartupInputTag`를 Spec에 복사하고, Pressed와 Released를 모두 ASC에 전달하며, 입력 바인딩과 어빌리티 부여를 각각 데이터와 서버 권한에 맞게 구성해야 전체 연결이 완성된다.

## 참고 자료

- [Enhanced Input](https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine)
- [UAbilitySystemComponent](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/GameplayAbilities/UAbilitySystemComponent)
- [FGameplayAbilitySpec](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/GameplayAbilities/FGameplayAbilitySpec)
- [Gameplay Tags](https://dev.epicgames.com/documentation/unreal-engine/using-gameplay-tags-in-unreal-engine)
