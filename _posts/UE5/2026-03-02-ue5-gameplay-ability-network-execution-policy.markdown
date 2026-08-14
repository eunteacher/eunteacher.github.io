---
layout: post
title: 'Gameplay Ability 네트워크 실행 정책 선택하기'
date: 2026-03-02
categories: [Dev, UE5]
tags: [UE5, GAS, GameplayAbility, Multiplayer, Prediction]
---

Gameplay Ability System에서 Ability의 네트워크 실행 정책은 단순히 “서버에서 실행할지, 클라이언트에서 실행할지”만 정하는 옵션이 아니다. **누가 실행을 시작하고, 클라이언트가 결과를 예측할 수 있는지**를 함께 결정한다.

장비 전환 Ability를 구현하면서 `LocalPredicted`와 `ServerInitiated`의 차이를 제대로 구분하지 못해 서버에서는 장착 이벤트가 처리되지만 소유 클라이언트의 몽타주와 프레젠테이션이 실행되지 않는 문제가 있었다. 이 문제를 기준으로 네트워크 실행 정책의 차이와 선택 기준을 정리한다.

## 네 가지 실행 정책

`EGameplayAbilityNetExecutionPolicy`에는 네 가지 정책이 있다.

| 정책 | 실행 방식 | 어울리는 경우 |
| --- | --- | --- |
| `LocalPredicted` | 소유 클라이언트가 먼저 예측 실행하고 서버가 확정한다 | 발사, ADS처럼 즉각적인 입력 반응이 필요한 동작 |
| `LocalOnly` | 로컬 제어권을 가진 머신에서만 실행한다 | 네트워크 결과가 필요 없는 로컬 전용 표현 |
| `ServerInitiated` | 서버가 실행을 시작하고 소유 클라이언트에서도 실행한다 | 서버가 확정한 상태 전환을 클라이언트가 함께 표현해야 하는 동작 |
| `ServerOnly` | 서버에서만 실행한다 | 클라이언트에서 Ability 자체를 실행할 필요가 없는 서버 로직 |

정책을 고를 때는 다음 두 질문이 중요하다.

1. 이 상태를 최종적으로 결정하는 주체는 누구인가?
2. 네트워크 응답을 기다리지 않고 즉시 반응해야 하는가?

## 장비 전환에서 발생한 문제

장비 전환은 클라이언트 입력으로 시작하지만 실제 슬롯 변경과 장비 액터 생성은 서버가 결정하는 구조였다.

```text
Client Input
    ↓
Server_SetActiveSlot()
    ↓
QuickBarComponent
    ↓
EquipmentManager
    ↓
HandleGameplayEvent(Event_Weapon_Equip / Unequip)
    ↓
GA_EquipWeapon / GA_UnequipWeapon
```

처음에는 장착과 해제 Ability도 입력 Ability처럼 `LocalPredicted`에 가까운 방식으로 처리했다. 하지만 이 Ability는 소유 클라이언트가 예측 키를 만들어 먼저 실행하는 구조가 아니었다. 서버가 장비 상태를 확정한 다음 Gameplay Event를 보내 실행하고 있었다.

그 결과 서버 로그에서는 이벤트와 Ability 실행을 확인할 수 있었지만, 클라이언트에서는 장착 몽타주와 Anim Layer 같은 표현이 실행되지 않거나 활성화 검사에서 중단됐다.

## HasAuthorityOrPredictionKey의 의미

기존 활성화 검사는 다음과 같았다.

```cpp
if (!HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
{
	return;
}
```

`HasAuthorityOrPredictionKey`는 현재 실행이 서버 권한을 가졌거나 유효한 Prediction Key를 가진 경우 `true`를 반환한다. 따라서 소유 클라이언트가 먼저 예측 실행하는 `LocalPredicted` Ability와 잘 맞는다.

하지만 서버가 원격 Pawn의 Ability를 시작한 경우에는 클라이언트가 직접 만든 Prediction Key가 없다. 서버에서 시작한 Ability가 소유 클라이언트에 전달되면 클라이언트의 `ActivationMode`는 `Confirmed`가 될 수 있다.

`Confirmed`는 클라이언트가 임의로 시작했다는 뜻이 아니다. **서버가 해당 Ability의 활성화를 확정했다는 상태**다. 따라서 모든 Ability에 `HasAuthorityOrPredictionKey`만 적용하면 서버가 시작한 정상적인 클라이언트 실행까지 거부할 수 있다.

## 장비 전환을 ServerInitiated로 변경

장착과 해제는 서버가 장비 상태를 확정한 뒤 실행하므로 다음 정책을 사용한다.

```cpp
NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
```

그리고 활성화 검사에서는 서버 권한, Prediction Key, 서버가 확정한 클라이언트 실행을 허용한다.

```cpp
const bool bCanRunAbility = HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo) || ActivationInfo.ActivationMode == EGameplayAbilityActivationMode::Confirmed;
if (!bCanRunAbility)
{
	UE_LOG(LogTemp, Warning, TEXT("GA_EquipWeapon activation rejected: no authority, prediction key, or confirmed activation."));
	EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
	return;
}
```

`GA_UnequipWeapon`에도 같은 기준을 적용한다.

```text
소유 클라이언트: 장비 전환 요청
    ↓
서버: 슬롯과 장비 상태 확정
    ↓
서버: Equip 또는 Unequip Ability 실행
    ↓
소유 클라이언트: Confirmed 상태로 Ability 실행
    ↓
클라이언트: 몽타주와 프레젠테이션 적용
```

`ServerInitiated`는 서버 결과를 기다린 뒤 클라이언트에서 실행되므로 `LocalPredicted`보다 입력 반응에 지연이 생길 수 있다. 장비 전환처럼 서버 확정 상태가 중요한 동작에는 받아들일 수 있는 특성이지만, 빠른 입력 Ability에 무조건 적용하기에는 적합하지 않다.

## 상태 변경과 프레젠테이션 분리

Ability가 서버와 클라이언트에서 실행되더라도 모든 처리를 양쪽에서 수행하면 안 된다. 장비 액터 부착처럼 게임 상태를 바꾸는 처리는 서버 권한에서만 실행한다.

```cpp
bool USSGA_EquipWeapon::ApplyEquipmentAttach()
{
	if (bEquipmentAttachApplied) return true;

	AActor* AvatarActor = GetAvatarActorFromActorInfo();
	if (!IsValid(AvatarActor) || !AvatarActor->HasAuthority()) return false;

	USSEquipmentManagerComponent* EquipmentManager = AvatarActor->FindComponentByClass<USSEquipmentManagerComponent>();
	if (!IsValid(EquipmentManager) || !EquipItem.IsValid()) return false;

	if (!EquipmentManager->AttachEquippedItemToHand(EquipItem.Get()))
	{
		return false;
	}

	bEquipmentAttachApplied = true;
	return true;
}
```

반면 몽타주, Anim Layer, Rotation Mode처럼 플레이어에게 결과를 보여 주는 프레젠테이션은 클라이언트에서도 적용할 수 있어야 한다.

```cpp
void USSGA_EquipWeapon::ApplyEquipPresentation()
{
	ASSCharacter* Character = Cast<ASSCharacter>(GetAvatarActorFromActorInfo());
	if (!IsValid(Character) || !EquipItem.IsValid()) return;

	Character->ApplyEquippedWeaponPresentation(EquipItem.Get());
}
```

이를 역할로 나누면 다음과 같다.

```text
서버
└─ 슬롯, 장비 액터, EquippedItem 같은 권한 상태 확정

소유 클라이언트
└─ 서버가 확정한 Ability를 받아 몽타주와 로컬 표현 적용

다른 클라이언트
└─ 복제된 장비 액터와 상태를 기준으로 원격 캐릭터 표시
```

## 입력 Ability는 LocalPredicted

발사, 재장전, ADS처럼 버튼을 누르는 즉시 반응해야 하는 Ability는 성격이 다르다. 소유 클라이언트가 먼저 실행하고 서버가 나중에 승인하거나 보정하는 `LocalPredicted`가 적합하다.

```cpp
NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
```

```text
클라이언트 입력
    ↓
클라이언트: Ability 예측 실행
    ↓
몽타주, 카메라, UI 즉시 반응
    ↓
서버: 비용과 상태 검증
    ↓
클라이언트: 확정 또는 취소 결과에 맞춰 보정
```

이 경우에는 Prediction Key가 실행의 핵심이므로 다음 검사가 자연스럽다.

```cpp
if (!HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo)) return;
```

다만 재장전처럼 게임 설계에 따라 서버 확정을 기다리게 할 수도 있으므로, 동작 이름만으로 정책을 고정하기보다 반응성과 권한 모델을 기준으로 판단해야 한다.

## OnRep를 이용한 최종 상태 보정

Ability에서 프레젠테이션을 정상적으로 적용하더라도 복제된 최종 상태를 기준으로 다시 맞추는 경로가 있으면 안전하다.

```cpp
void USSQuickBarComponent::OnRep_EquippedItem()
{
	if (!EquippedItem)
	{
		ClearWeaponRuntimeState(GetOwner());

		if (ASSCharacter* Character = Cast<ASSCharacter>(GetOwner()))
		{
			Character->ApplyUnarmedPresentation();
		}
	}

	UnbindEquippedItemAmmoChanged(AmmoBoundItem);
	BindEquippedItemAmmoChanged(EquippedItem);
	BroadcastCurrentWeaponAmmo();
}
```

Ability 실행은 즉각적인 연출을 담당하고, `OnRep_EquippedItem`은 서버가 확정한 현재 장착 상태를 기준으로 최종 결과를 복구한다. 이 보정 경로는 다음 상황에서 특히 필요하다.

- 복제 지연으로 중간 표현을 놓친 경우
- 리스폰 이후 상태를 다시 구성하는 경우
- 늦게 접속한 클라이언트가 현재 상태를 받아야 하는 경우
- Ability 연출과 복제 상태가 일시적으로 어긋난 경우

```text
Ability 실행
    ↓
즉시 몽타주와 프레젠테이션 처리
    ↓
복제된 장착 상태의 OnRep
    ↓
최종 상태 보정
```

## 정책 선택 기준

### LocalPredicted를 고려할 때

- 소유 클라이언트 입력으로 시작한다.
- 네트워크 왕복을 기다리면 조작감이 나빠진다.
- 서버가 거부했을 때 취소하거나 보정할 수 있다.
- 예측 가능한 Gameplay Effect와 Gameplay Cue를 사용한다.

### ServerInitiated를 고려할 때

- 서버 로직 또는 서버 Gameplay Event가 실행을 시작한다.
- 서버가 먼저 상태를 확정해야 한다.
- 해당 Ability의 표현을 소유 클라이언트에서도 실행해야 한다.
- Local Prediction보다 서버 상태의 정확성이 중요하다.

### ServerOnly를 고려할 때

- Ability 자체는 서버에서만 실행하면 된다.
- 클라이언트는 Ability가 아닌 복제 프로퍼티나 Gameplay Cue의 결과만 보면 된다.

### LocalOnly를 고려할 때

- 네트워크 권한이나 다른 클라이언트와 무관한 로컬 동작이다.
- 서버의 게임 상태를 변경하지 않는다.

## 정리

이번 문제의 핵심은 `HasAuthorityOrPredictionKey()`를 모든 Ability에 동일하게 적용한 것이었다. 이 검사는 서버 권한 실행과 클라이언트 예측 실행에는 적합하지만, 서버가 시작한 Ability를 클라이언트가 `Confirmed` 상태로 실행하는 경우까지 자동으로 허용하지는 않는다.

장비 전환처럼 서버가 상태를 확정한 뒤 클라이언트가 표현을 따라가야 하는 Ability는 `ServerInitiated`로 두고, 프로젝트의 추가 활성화 검사에서도 `Confirmed` 상태를 고려해야 한다. 반대로 즉각적인 입력 반응이 중요한 Ability는 `LocalPredicted`와 Prediction Key를 사용하는 편이 자연스럽다.

정책의 이름을 외우는 것보다 **실행을 누가 시작하는지, 상태를 누가 확정하는지, 즉시 반응이 필요한지**를 먼저 구분하는 것이 중요하다.

## 참고 자료

- [Epic Games: Gameplay Ability Net Execution Policy](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/GameplayAbilities/EGameplayAbilityNetExecutionPoli-)
- [Epic Games: Gameplay Ability Activation Mode](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/GameplayAbilities/EGameplayAbilityActivationMode__-)
- [Epic Games: Gameplay Ability 사용 방법](https://dev.epicgames.com/documentation/unreal-engine/using-gameplay-abilities-in-unreal-engine)
