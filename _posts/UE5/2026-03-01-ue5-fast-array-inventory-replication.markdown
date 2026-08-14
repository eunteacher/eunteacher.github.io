---
layout: post
title: 'Fast Array Serializer로 인벤토리 변경분 복제하기'
date: 2026-03-01
categories: [Dev, UE5]
tags: [UE5, Replication, FastArraySerializer, Inventory, Multiplayer]
---

멀티플레이 인벤토리는 서버가 가진 아이템 목록을 클라이언트와 동기화해야 한다. 일반 배열을 복제하는 방식도 사용할 수 있지만, 아이템이 하나 추가되거나 변경될 때마다 목록 전체를 기준으로 처리하는 것은 항목이 많아질수록 부담이 커질 수 있다.

`FFastArraySerializer`는 배열에서 **추가·변경·제거된 항목을 추적하여 변경분 중심으로 복제**하기 위한 Unreal Engine의 구조다. 이 글에서는 인벤토리 항목 하나를 나타내는 `FSSInventoryEntry`와 전체 목록을 관리하는 `FSSInventoryList`가 각각 어떤 역할을 하는지 정리한다.

## 헤더 코드

먼저 사용한 헤더 코드를 그대로 살펴본다.

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "SSInventoryList.generated.h"

class USSItemDefinition;
class USSItemInstance;
class USSInventoryManagerComponent;
struct FSSInventoryList;

// 인벤토리 FastArray의 단일 아이템 항목
USTRUCT(BlueprintType)
struct FSSInventoryEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

private:
	friend FSSInventoryList;
	friend USSInventoryManagerComponent;

public:
	FSSInventoryEntry()
	{
	}

	// 실제 인벤토리에 들어 있는 아이템 인스턴스
	UPROPERTY()
	TObjectPtr<USSItemInstance> Instance = nullptr;

	// 클라이언트에서 항목이 제거되기 직전에 호출
	void PreReplicatedRemove(const FSSInventoryList& InArraySerializer);

	// 클라이언트에서 새 항목이 추가된 직후 호출
	void PostReplicatedAdd(const FSSInventoryList& InArraySerializer);

	// 클라이언트에서 기존 항목이 변경된 직후 호출
	void PostReplicatedChange(const FSSInventoryList& InArraySerializer);
};

// 인벤토리 항목 배열을 FastArray 방식으로 복제
USTRUCT(BlueprintType)
struct FSSInventoryList : public FFastArraySerializer
{
	GENERATED_BODY()

public:
	FSSInventoryList() : OwnerComponent(nullptr)
	{
	}

	FSSInventoryList(UActorComponent* InOwnerComponent) : OwnerComponent(InOwnerComponent)
	{
	}

	// 실제 아이템 항목 목록
	UPROPERTY()
	TArray<FSSInventoryEntry> Entries;

	// 아이템 인스턴스 Outer와 변경 알림에 사용할 소유 컴포넌트
	UPROPERTY()
	TObjectPtr<UActorComponent> OwnerComponent = nullptr;

	// 새 아이템 인스턴스를 생성하고 인벤토리에 추가
	USSItemInstance* AddEntry(TSubclassOf<USSItemDefinition> ItemDef, int32 StackCount);

	// 지정한 아이템 인스턴스를 인벤토리에서 제거
	void RemoveEntry(USSItemInstance* Instance);

	// 두 아이템 인스턴스의 인벤토리 위치를 교환
	bool SwapEntries(USSItemInstance* SourceItem, USSItemInstance* TargetItem);

	// FastArray 변경분을 네트워크로 직렬화
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FSSInventoryEntry, FSSInventoryList>(Entries, DeltaParms, *this);
	}
};

// FSSInventoryList가 NetDeltaSerialize를 사용하도록 등록
template <>
struct TStructOpsTypeTraits<FSSInventoryList> : public TStructOpsTypeTraitsBase2<FSSInventoryList>
{
	enum { WithNetDeltaSerializer = true };
};
```

## 구현 코드

```cpp
#include "SSInventoryList.h"
#include "SSItemDefinition.h"
#include "SSItemInstance.h"
#include "SmileShooter/Components/SSInventoryManagerComponent.h"

void FSSInventoryEntry::PreReplicatedRemove(const FSSInventoryList& InArraySerializer)
{
	// 제거 직후 UI가 이전 항목을 참조하지 않도록 다음 틱에 갱신
	if (InArraySerializer.OwnerComponent)
	{
		if (USSInventoryManagerComponent* InventoryManager = Cast<USSInventoryManagerComponent>(InArraySerializer.OwnerComponent))
		{
			InventoryManager->BroadcastInventoryChangedNextTick();
		}
	}
}

void FSSInventoryEntry::PostReplicatedAdd(const FSSInventoryList& InArraySerializer)
{
	// 클라이언트에 새 인벤토리 항목이 추가되면 UI 갱신
	if (InArraySerializer.OwnerComponent)
	{
		Cast<USSInventoryManagerComponent>(InArraySerializer.OwnerComponent)->OnInventoryChanged.Broadcast();
	}
}

void FSSInventoryEntry::PostReplicatedChange(const FSSInventoryList& InArraySerializer)
{
	// 클라이언트에 복제된 항목 정보가 바뀌면 UI 갱신
	if (InArraySerializer.OwnerComponent)
	{
		Cast<USSInventoryManagerComponent>(InArraySerializer.OwnerComponent)->OnInventoryChanged.Broadcast();
	}
}

USSItemInstance* FSSInventoryList::AddEntry(TSubclassOf<USSItemDefinition> ItemDef, int32 StackCount)
{
	check(ItemDef);
	check(OwnerComponent);

	AActor* OwningActor = OwnerComponent->GetOwner();
	check(OwningActor->HasAuthority());

	// OwnerComponent를 Outer로 사용해 새 아이템 인스턴스 생성
	USSItemInstance* NewInstance = NewObject<USSItemInstance>(OwnerComponent);
	NewInstance->ItemDef = ItemDef;

	// 아이템 기본값 기준으로 스택과 탄약 상태 초기화
	NewInstance->SetStackCount(StackCount);
	NewInstance->InitializeAmmoFromDefinition();

	// FastArray에 새 항목을 추가하고 인스턴스 연결
	FSSInventoryEntry& NewEntry = Entries.AddDefaulted_GetRef();
	NewEntry.Instance = NewInstance;

	// 새 항목이 클라이언트에 복제되도록 표시
	MarkItemDirty(NewEntry);

	return NewInstance;
}

void FSSInventoryList::RemoveEntry(USSItemInstance* Instance)
{
	for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
	{
		if (Entries[EntryIndex].Instance != Instance) continue;

		// 항목 제거 후 배열 변경분을 복제 대상으로 표시
		Entries.RemoveAt(EntryIndex);
		MarkArrayDirty();
		break;
	}
}

bool FSSInventoryList::SwapEntries(USSItemInstance* SourceItem, USSItemInstance* TargetItem)
{
	int32 SourceIndex = INDEX_NONE;
	int32 TargetIndex = INDEX_NONE;

	// 현재 배열에서 이동할 아이템과 대상 아이템 위치를 찾음
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		if (Entries[i].Instance == SourceItem)
		{
			SourceIndex = i;
		}
		else if (TargetItem != nullptr && Entries[i].Instance == TargetItem)
		{
			TargetIndex = i;
		}
	}

	// 이동할 아이템이 인벤토리에 없으면 실패
	if (SourceIndex == INDEX_NONE) return false;

	// 대상 아이템이 있으면 두 항목의 위치를 교환
	if (TargetIndex != INDEX_NONE)
	{
		Entries.Swap(SourceIndex, TargetIndex);

		// 배열 순서 변경은 전체 배열 변경으로 표시
		MarkArrayDirty();
		return true;
	}

	// 빈 슬롯으로 이동하면 항목을 리스트 끝으로 보냄
	if (TargetItem == nullptr)
	{
		FSSInventoryEntry TempEntry = Entries[SourceIndex];
		Entries.RemoveAt(SourceIndex);
		Entries.Add(TempEntry);

		MarkArrayDirty();
		return true;
	}

	return false;
}
```

## Fast Array의 두 계층

Fast Array는 항목과 목록을 서로 다른 구조체로 나눈다.

```text
USSInventoryManagerComponent
└─ FSSInventoryList              전체 목록과 복제 상태 관리
   ├─ FSSInventoryEntry          첫 번째 아이템
   │  └─ USSItemInstance
   ├─ FSSInventoryEntry          두 번째 아이템
   │  └─ USSItemInstance
   └─ FSSInventoryEntry          세 번째 아이템
      └─ USSItemInstance
```

`FSSInventoryEntry`는 인벤토리 슬롯 하나이고, `FSSInventoryList`는 항목 배열 전체를 보관하면서 어떤 항목이 바뀌었는지 Fast Array 시스템에 알려 주는 컨테이너다.

## FSSInventoryEntry: 복제되는 항목 하나

```cpp
struct FSSInventoryEntry : public FFastArraySerializerItem
```

Fast Array의 각 항목은 `FFastArraySerializerItem`을 상속한다. 이 기반 구조체에는 엔진이 항목을 식별하고 변경 상태를 추적할 때 사용하는 내부 정보가 들어 있다. 게임 코드에서 해당 값을 직접 관리하기보다 `MarkItemDirty`와 같은 Fast Array API를 통해 변경 사실을 알리는 방식으로 사용한다.

### Item Definition과 Item Instance

코드에는 두 가지 아이템 개념이 등장한다.

- `USSItemDefinition`: 아이템 종류를 설명하는 데이터 또는 클래스
- `USSItemInstance`: 실제 인벤토리에 들어간 개별 아이템 객체

같은 아이템 정의를 사용하는 두 아이템이라도 내구도, 탄약, 옵션, 스택 수처럼 각자 다른 런타임 상태를 가질 수 있다. `FSSInventoryEntry`는 그중 실제 객체인 `USSItemInstance`를 가리킨다.

```cpp
UPROPERTY()
TObjectPtr<USSItemInstance> Instance = nullptr;
```

`UPROPERTY`와 `TObjectPtr`는 Unreal의 객체 추적과 가비지 컬렉션이 이 참조를 인식하도록 한다. 하지만 이 선언만으로 `USSItemInstance` 내부 프로퍼티까지 자동으로 네트워크 복제되는 것은 아니다. 이 부분은 뒤에서 따로 확인한다.

## 항목 복제 콜백

Fast Array는 클라이언트가 변경분을 적용하는 시점에 항목별 콜백을 호출할 수 있다.

### PreReplicatedRemove

```cpp
void PreReplicatedRemove(const FSSInventoryList& InArraySerializer);
```

클라이언트 배열에서 항목이 실제로 제거되기 직전에 호출된다. 제거될 `Instance`를 아직 확인할 수 있으므로 UI 슬롯 제거, 로컬 캐시 정리 또는 변경 알림 전달에 사용할 수 있다.

현재 구현은 이 콜백에서 UI를 즉시 갱신하지 않고 `BroadcastInventoryChangedNextTick`을 호출한다. 콜백 이름처럼 이 시점에는 Entry가 아직 제거되기 전이므로, 다음 틱에 갱신하여 UI가 제거 이전 배열을 다시 읽지 않게 한다.

### PostReplicatedAdd

```cpp
void PostReplicatedAdd(const FSSInventoryList& InArraySerializer);
```

서버에서 추가된 항목이 클라이언트 배열에 반영된 직후 호출된다. 새 아이템을 UI에 표시하거나 인벤토리 갱신 이벤트를 전달하기 좋은 시점이다.

### PostReplicatedChange

```cpp
void PostReplicatedChange(const FSSInventoryList& InArraySerializer);
```

이미 존재하던 항목의 복제 데이터가 변경된 직후 호출된다. 스택 수처럼 항목 안의 복제 대상 데이터가 바뀌었을 때 UI를 갱신하는 데 활용할 수 있다.

이 콜백들은 **복제 결과를 수신한 클라이언트에서 실행되는 알림 지점**이다. 서버에서 `Entries.Add()`를 호출하는 순간 바로 실행되는 일반 컨테이너 이벤트는 아니다.

## FSSInventoryList: 배열과 변경 상태 관리

```cpp
struct FSSInventoryList : public FFastArraySerializer
```

목록 구조체는 `FFastArraySerializer`를 상속하고 실제 항목 배열을 가진다.

```cpp
UPROPERTY()
TArray<FSSInventoryEntry> Entries;
```

Fast Array의 직렬화 함수는 이 `Entries`를 기준으로 어떤 항목이 추가되고 수정되거나 제거되었는지 처리한다.

### OwnerComponent가 필요한 이유

`FSSInventoryList`는 `USTRUCT`이므로 그 자체로는 월드에 존재하는 Actor나 Component가 아니다. 콜백에서 UI 이벤트를 전달하거나 새 `USSItemInstance`의 Outer를 정하려면 실제 시스템을 소유한 객체로 돌아갈 경로가 필요하다.

```cpp
FSSInventoryList(UActorComponent* InOwnerComponent) : OwnerComponent(InOwnerComponent)
```

이 생성자는 목록을 만들 때 소유 컴포넌트를 함께 저장한다. `OwnerComponent`는 Fast Array 항목의 변경 알림을 인벤토리 컴포넌트로 전달하는 문맥으로 사용할 수 있다.

## 변경 사항을 Fast Array에 알리기

Fast Array는 `TArray`를 감시해서 모든 수정을 자동으로 발견하는 구조가 아니다. 서버에서 배열을 변경한 뒤 어떤 항목이 달라졌는지 명시적으로 표시해야 한다.

### AddEntry: 인스턴스 생성과 항목 추가

`AddEntry`는 먼저 Item Definition과 Owner Component가 유효한지 확인하고, 소유 Actor가 서버 권한을 가지고 있는지도 검사한다. 이 함수는 서버 인벤토리를 변경하는 함수이므로 클라이언트에서 직접 실행되는 경로를 허용하지 않는다.

새 `USSItemInstance`는 `OwnerComponent`를 Outer로 사용하여 생성된다. 이후 다음 순서로 초기화된다.

1. `ItemDef`를 새 인스턴스에 저장한다.
2. 전달받은 `StackCount`를 설정한다.
3. Item Definition을 기준으로 탄창과 예비 탄약을 초기화한다.
4. `Entries`에 새 Entry를 추가한다.
5. Entry가 새 Item Instance를 가리키게 한다.
6. `MarkItemDirty`로 새 항목을 복제 대상으로 표시한다.

따라서 헤더의 `StackCount`는 사용되지 않는 인자가 아니라 `USSItemInstance::SetStackCount`로 전달된다. 탄약 정보도 `InitializeAmmoFromDefinition`에서 해당 아이템 정의를 기준으로 초기화된다.

### RemoveEntry: 항목 제거

`RemoveEntry`는 배열을 순회하여 같은 Item Instance를 가진 Entry를 찾는다. 발견하면 `RemoveAt`으로 제거하고 `MarkArrayDirty`를 호출한 뒤 반복을 종료한다.

추가할 때는 새 Entry 하나를 표시할 수 있으므로 `MarkItemDirty`를 사용하지만, 제거한 뒤에는 표시할 Entry가 배열에 남아 있지 않다. 이 경우 배열 구조가 변경됐다는 사실을 `MarkArrayDirty`로 알린다.

### SwapEntries: 위치 교환과 빈 슬롯 이동

이 구현에서 인벤토리 위치는 별도의 슬롯 번호가 아니라 `Entries`의 배열 순서로 표현된다.

- `SourceItem`과 `TargetItem`이 모두 존재하면 `Entries.Swap`으로 두 인덱스를 교환한다.
- `TargetItem`이 `nullptr`이면 Source Entry를 제거한 뒤 배열 끝에 다시 추가한다.
- Source Item을 찾지 못하면 `false`를 반환한다.
- Target Item 인자가 유효하지만 배열에서 찾지 못하면 교환하지 않고 `false`를 반환한다.

두 경우 모두 배열 순서가 변하므로 성공 시 `MarkArrayDirty`를 호출한다. 즉, 여기서 “빈 슬롯으로 이동”은 임의의 빈 인덱스를 기록하는 것이 아니라 현재 Entry를 목록의 마지막 위치로 보내는 의미다.

## NetDeltaSerialize의 역할

```cpp
bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
{
	return FFastArraySerializer::FastArrayDeltaSerialize<FSSInventoryEntry, FSSInventoryList>(Entries, DeltaParms, *this);
}
```

이 함수가 Fast Array 복제의 중심이다.

템플릿 인자는 다음 의미를 가진다.

- `FSSInventoryEntry`: 배열 항목의 자료형
- `FSSInventoryList`: 배열을 소유한 Serializer 자료형
- `Entries`: 실제로 직렬화할 배열
- `DeltaParms`: 엔진이 전달하는 네트워크 직렬화 정보
- `*this`: 현재 Fast Array Serializer

엔진은 이 정보를 이용해 연결별로 필요한 변경분을 만들고, 수신 측 배열에 적용한다.

## TStructOpsTypeTraits가 필요한 이유

`NetDeltaSerialize` 함수를 작성하는 것만으로는 Unreal의 구조체 시스템이 해당 함수를 사용해야 한다는 사실을 알 수 없다.

```cpp
template <>
struct TStructOpsTypeTraits<FSSInventoryList> : public TStructOpsTypeTraitsBase2<FSSInventoryList>
{
	enum { WithNetDeltaSerializer = true };
};
```

`WithNetDeltaSerializer = true`는 `FSSInventoryList`가 일반적인 구조체 직렬화 대신 자체 `NetDeltaSerialize` 경로를 제공한다고 등록한다. 이 선언이 빠지면 작성한 Fast Array 직렬화 함수가 의도대로 사용되지 않는다.

## Inventory Manager Component와 연결하기

Fast Array 구조체를 정의한 것만으로 복제가 시작되지는 않는다. 실제 프로젝트에서는 `USSInventoryManagerComponent`가 목록을 소유하고 복제한다.

```cpp
USSInventoryManagerComponent::USSInventoryManagerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer), InventoryList(this)
{
	SetIsReplicatedByDefault(true);
}

void USSInventoryManagerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, InventoryList);
}
```

컴포넌트의 생성자에서 `InventoryList(this)`를 호출하므로 서버와 클라이언트의 List가 자신의 Inventory Manager Component를 `OwnerComponent`로 갖는다. `SetIsReplicatedByDefault(true)`는 컴포넌트 복제를 활성화하고, `DOREPLIFETIME`은 Fast Array 프로퍼티를 복제 대상으로 등록한다.

```cpp
UPROPERTY(Replicated)
FSSInventoryList InventoryList;
```

## Item Instance는 Subobject로 복제한다

Fast Array의 Entry는 `USSItemInstance` 포인터를 전달하지만, 포인터가 가리키는 UObject의 내부 프로퍼티까지 Fast Array가 대신 복제하지는 않는다. 실제 프로젝트에서는 Inventory Manager Component가 `ReplicateSubobjects`를 재정의하여 각 Item Instance를 별도로 복제한다.

```cpp
bool USSInventoryManagerComponent::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
	bool bWroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);

	for (const FSSInventoryEntry& Entry : InventoryList.Entries)
	{
		if (IsValid(Entry.Instance))
		{
			bWroteSomething |= Channel->ReplicateSubobject(Entry.Instance, *Bunch, *RepFlags);
		}
	}

	return bWroteSomething;
}
```

`USSItemInstance`도 `IsSupportedForNetworking()`에서 `true`를 반환하고, `GetLifetimeReplicatedProps`에서 다음 런타임 상태를 복제하도록 등록한다.

- Item Definition
- Stack Count
- 현재 탄창 탄약
- 현재 예비 탄약

따라서 두 복제 경로의 책임은 서로 다르다.

```text
Fast Array
└─ 어떤 Item Instance가 목록에 있는지, 순서가 어떻게 바뀌었는지 복제

ReplicateSubobjects
└─ 각 Item Instance의 스택과 탄약 같은 내부 상태 복제
```

현재 코드는 `ReplicateSubobjects`를 사용하는 기존 방식이다. Iris를 사용하며 registered subobjects list를 활성화하는 구조라면 `AddReplicatedSubObject`를 사용하는 방식으로 별도 설계가 필요하다.

## 서버 권한을 기준으로 변경하기

복제 배열의 기준 상태는 서버가 관리해야 한다. 클라이언트가 로컬 `Entries`를 직접 변경해도 그 결과가 서버로 올라가는 것은 아니며, 다음 서버 복제에서 다시 덮어써질 수 있다.

일반적인 흐름은 다음과 같다.

```text
클라이언트 입력
    ↓
서버 RPC 또는 서버 게임 로직
    ↓
서버의 AddEntry / RemoveEntry / SwapEntries
    ↓
MarkItemDirty / MarkArrayDirty
    ↓
Fast Array 변경분 복제
    ↓
클라이언트의 PostReplicatedAdd / Change 또는 PreReplicatedRemove
    ↓
UI와 로컬 표현 갱신
```

아이템 획득, 제거, 위치 교환이 게임 규칙과 관련된다면 요청 값도 서버에서 다시 검증해야 한다.

## 전체 동작 흐름

아이템 하나를 서버 인벤토리에 추가했을 때의 흐름은 다음과 같다.

```text
서버: AddItemDefinition
    ↓
FSSInventoryList::AddEntry
    ↓
USSItemInstance 생성
    ↓
Item Definition, Stack Count, 탄약 초기화
    ↓
Entry 추가 + MarkItemDirty
    ↓
Fast Array가 목록 변경분 복제
    ↓
ReplicateSubobjects가 Item Instance 내부 상태 복제
    ↓
클라이언트: PostReplicatedAdd
    ↓
OnInventoryChanged.Broadcast()
    ↓
UI 갱신
```

제거할 때는 `RemoveEntry`가 배열에서 Entry를 삭제하고 `MarkArrayDirty`를 호출한다. 클라이언트에서는 실제 제거 직전 `PreReplicatedRemove`가 실행되고, 다음 틱에 Inventory Changed 이벤트를 전달한다.

위치를 바꿀 때는 Entry 배열의 순서를 변경하고 `MarkArrayDirty`를 호출한다. 변경이 성공하면 Inventory Manager Component가 서버 측 UI 갱신 이벤트도 방송한다.

## 정리

이 인벤토리 구조는 역할을 다음처럼 나눈다.

- `FSSInventoryEntry`: 아이템 하나와 항목별 복제 콜백
- `FSSInventoryList`: Entry 배열과 Fast Array 변경 상태
- `NetDeltaSerialize`: 변경분 직렬화 실행
- `TStructOpsTypeTraits`: 커스텀 Delta Serializer 사용 등록
- `OwnerComponent`: UObject 생성과 변경 알림을 위한 소유 문맥
- `USSInventoryManagerComponent`: 실제 복제 프로퍼티를 소유하고 서버 변경을 수행하는 객체

Fast Array를 사용할 때 기억해야 할 핵심은 배열을 선언하는 것보다 **변경 직후 Dirty 상태를 올바르게 표시하는 것**이다. 여기에 Item Instance의 subobject 복제까지 연결해야 인벤토리 항목과 각 아이템의 내부 상태가 클라이언트에 함께 전달된다.

## 참고 자료

- [Epic Games: FFastArraySerializerItem API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/NetCore/FFastArraySerializerItem)
- [Epic Games: UObject 복제](https://dev.epicgames.com/documentation/unreal-engine/replicating-uobjects-in-unreal-engine)
