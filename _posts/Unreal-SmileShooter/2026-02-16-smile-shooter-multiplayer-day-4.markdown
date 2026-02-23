---
layout: post
title: '[UE5 SmileShooter MultiPlayer 04] 데이터 주도형 입력 시스템 구축: Enhanced Input과 GAS 태그 바인딩 가이드'
date: 2026-02-16
categories: [ Dev, Unreal, SubProject ]
tags: [UE5, Multiplayer, ASC, C++]
use_math: true
---

# [UE5] Enhanced Input과 GAS 태그 바인딩 가이드

대규모 프로젝트에서 `IA_Jump`, `IA_Fire` 같은 인풋 액션 변수를 캐릭터 클래스에 직접 선언하고 참조하는 방식은 객체 간의 결합도를 높이고 유지보수를 어렵게 만듬

오늘은 **Gameplay Tags**를 매개체로 활용하여 입력(Input)과 기능(Ability) 사이의 의존성을 끊고, 데이터 에셋만으로 조작을 확장할 수 있는 데이터 주도형(Data-Driven) 입력 아키텍처를 정리

---
## 1. 입력 매핑 데이터 정의: `InputConfig`

가장 먼저 "어떤 태그가 어떤 인풋 액션(InputAction)을 실행할지" 정의하는 청사진 역할을 할 데이터 에셋이 필요

```cpp
/**
* FTaggedInputAction
* 특정 게임플레이 입력 태그와 입력 액션(Input Action)을 1:1로 매핑하는 구조체
* 이 구조체를 통해 런타임에 태그를 기반으로 특정 입력 액션을 동적으로 찾아 바인딩할 수 있음
*/
USTRUCT(BlueprintType)
struct FTaggedInputAction
{
  GENERATED_BODY()

public:

	/** 실제 입력을 처리할 Enhanced Input Action 에셋 */
	UPROPERTY(EditDefaultsOnly)
	const UInputAction* InputAction = nullptr;

	/** 해당 입력 액션을 식별하기 위한 게임플레이 태그 (예: InputTag.Jump) */
	UPROPERTY(EditDefaultsOnly, Meta = (Categories = "InputTag"))
	FGameplayTag InputTag;
};

/**
* USSInputConfig
* 프로젝트에서 사용하는 입력 매핑 데이터를 보관하는 데이터 에셋 클래스
* 캐릭터나 컨트롤러에서 이 에셋을 참조하여 Enhanced Input 시스템에 바인딩을 수행
*/
UCLASS()
class SMILESHOOTER_API USSInputConfig : public UDataAsset
{
GENERATED_BODY()

public:
/**
* 주어진 InputTag에 매핑된 첫 번째 입력 액션을 찾아 반환
* @param InputTag 찾으려는 입력의 식별 태그
* @return 일치하는 UInputAction 포인터, 없을 경우 nullptr 반환
*/
const UInputAction* FindInputActionForTag(const FGameplayTag& InputTag) const;

public:
/**
* 소유자가 사용하는 입력 액션의 리스트
*/
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Meta = (TitleProperty = "InputAction"))
TArray<FTaggedInputAction> TaggedInputActions;
};
```
### 📋 `FTaggedInputAction` (구조체)
* **InputAction**: 에셋 포인터
* **InputTag**: 이 입력에 부여할 고유 Gameplay Tag (예: `InputTag.Ability.Fire`)

### 📂 `USSInputConfig` (Data Asset)
이 클래스는 매핑 정보를 배열로 관리하며, 특정 태그를 전달하면 매칭되는 UInputAction을 찾아주는 유틸리티를 제공합니다. 이를 통해 코드에서 특정 에셋을 하드코딩으로 참조할 필요가 없어집니다.

---

## 2. 유연한 바인딩을 위한 `USSEnhancedInputComponent`
기본 UEnhancedInputComponent를 확장하여 태그 기반 바인딩을 지원하는 커스텀 컴포넌트를 만듬

```cpp
UCLASS()
class SMILESHOOTER_API USSEnhancedInputComponent : public UEnhancedInputComponent
{
GENERATED_BODY()

public:
/**
* 특정 GameplayTag에 매핑된 InputAction을 찾아 함수에 바인딩
* @param InputConfig 입력 태그-액션 매핑 정보를 담고 있는 데이터 에셋
* @param InputTag 바인딩하려는 고유 게임플레이 태그
* @param TriggerEvent 입력 트리거 조건 (Started, Triggered, Completed 등)
* @param Object 함수를 실행할 객체 (보통 this)
* @param Func 실행될 멤버 함수 주소
*/
template<class UserClass, typename FuncType>
void BindActionByTag(const USSInputConfig* InputConfig, const FGameplayTag& InputTag, ETriggerEvent TriggerEvent, UserClass* Object, FuncType Func);
};

template <class UserClass, typename FuncType>
void USSEnhancedInputComponent::BindActionByTag(const USSInputConfig* InputConfig, const FGameplayTag& InputTag, ETriggerEvent TriggerEvent, UserClass* Object, FuncType Func)
{
if (!IsValid(InputConfig)) return;

	if (const UInputAction* IA = InputConfig->FindInputActionForTag(InputTag))
	{
		BindAction(IA, TriggerEvent, Object, Func);
	}
}
```
이 템플릿 함수는 InputConfig 데이터 에셋에서 태그에 맞는 액션을 찾아 자동으로 BindAction을 수행

---
## 3. GAS 어빌리티와 입력의 연결: `GameplayAbility`
각 어빌리티가 어떤 입력에 반응해야 하는지 스스로 알 수 있도록 기본 클래스를 확장
* `StartupInputTag`: 해당 어빌리티를 활성화할 때 쓰일 게임플레이 태그
* 이 태그를 통해 입력 태그 → 어빌리티로 이어지는 직관적인 흐름이 완성

```cpp

UCLASS()
class SMILESHOOTER_API USSGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	/** Enhanced Input 바인딩에 사용되는 게임플레이 태그 */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Ability")
	FGameplayTag StartupInputTag = FGameplayTag::EmptyTag;

public:
	USSGameplayAbility();

};
```
---
## 4. 어빌리티 시스템 컴포넌트(ASC)의 중계 로직
캐릭터로부터 전달된 입력 신호를 처리할 함수를 USSAbilitySystemComponent에 구현

```cpp
/**
 * Enhanced Input으로부터 전달받은 입력 태그에 해당하는 어빌리티를 실행
 * @param InputTag 발생한 입력의 고유 게임플레이 태그
 */
void AbilityInputTagPressed(FGameplayTag InputTag);
```
```cpp

void USSAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag InputTag)
{
	if (!InputTag.IsValid()) return;

	// 부여된 어빌리티들 중 입력 태그와 매칭되는 Spec을 찾음
	for (FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		// 어빌리티 인스턴스가 존재하고, 해당 어빌리티 스펙이 이 입력 태그를 포함하고 있는지 확인
		// Spec.GetDynamicSpecSourceTags()는 어빌리티 부여 시점에 등록된 태그 목록을 반환
		if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			// 어빌리티 실행
			TryActivateAbility(Spec.Handle);
		}
	}
}
```
---
## 5. 실전 캐릭터 구현 `Character`

### 🌉 브릿지 함수 구현
캐릭터는 입력을 감지하면 직접 로직을 수행하는 대신, ASC에 "이 태그가 눌렸다"는 사실만 전달
```cpp
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SS|Input", meta = (AllowPrivateAccess = "true"))
TObjectPtr<USSInputConfig> InputConfig = nullptr;

void OnConfirm();
```

### 🛠️ 입력 설정 `SetupPlayerInputComponent`
캐릭터는 특정 입력이 들어왔을 때 실행될 브릿지 함수를 바인딩
```cpp
void ASSCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (USSEnhancedInputComponent* EnhancedInputComponent = Cast<USSEnhancedInputComponent>(PlayerInputComponent))
	{
		if (InputConfig)
		{
			// Moving
			EnhancedInputComponent->BindActionByTag(InputConfig, SSGameplayTags::InputTag_Move, ETriggerEvent::Triggered, this, &ASSCharacter::Move);

			// Looking
			EnhancedInputComponent->BindActionByTag(InputConfig, SSGameplayTags::InputTag_Look, ETriggerEvent::Triggered, this, &ASSCharacter::Look);

			// Jumping
			EnhancedInputComponent->BindActionByTag(InputConfig, SSGameplayTags::InputTag_Jump, ETriggerEvent::Started, this, &ACharacter::Jump);
			EnhancedInputComponent->BindActionByTag(InputConfig, SSGameplayTags::InputTag_Jump, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

			// Fire
			EnhancedInputComponent->BindActionByTag(InputConfig, SSGameplayTags::InputTag_Fire, ETriggerEvent::Started, this, &ASSCharacter::OnPrimaryFirePressed);
			EnhancedInputComponent->BindActionByTag(InputConfig, SSGameplayTags::InputTag_Fire, ETriggerEvent::Completed, this, &ASSCharacter::OnPrimaryFireReleased);

			// Confirm
			EnhancedInputComponent->BindActionByTag(InputConfig, SSGameplayTags::InputTag_Confirm, ETriggerEvent::Triggered, this, &ASSCharacter::OnConfirm);

			// Cancel
			EnhancedInputComponent->BindActionByTag(InputConfig, SSGameplayTags::InputTag_Cancel, ETriggerEvent::Triggered, this, &ASSCharacter::OnCancel);
		}
	}
}

```

### ✅ 어빌리티 실행
```cpp
void ASSCharacter::OnConfirm()
{
	// [Debug] Confirm 입력 확인
	UE_LOG(LogTemp, Warning, TEXT("[%s] OnConfirm Input Received!"), *GetName());

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green, TEXT("✅ Confirm Pressed"));
	}

	if (!IsValid(AbilitySystemComponent)) return;
	AbilitySystemComponent->AbilityInputTagPressed(SSGameplayTags::InputTag_Confirm);
}

```

## ✨ 시스템의 핵심 장점
* **완벽한 데이터 주도**: 새로운 스킬이나 버튼을 추가할 때 C++ 코드를 수정하고 재컴파일할 필요가 없음. 데이터 에셋(Config)과 어빌리티 에셋만 수정하면 즉시 반영
* **느슨한 결합(Decoupling)**: 캐릭터 클래스는 "어떤 스킬"이 나가는지 알 필요가 없음. 단지 "어떤 태그"가 눌렸는지만 중계
* **확장성**: 무기가 바뀌거나 조작 모드가 바뀌어도 동일한 태그만 던져주면 시스템이 상황에 맞는 어빌리티를 알아서 찾아 실행
