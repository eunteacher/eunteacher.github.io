---
layout: post
title: '캐릭터 가속도를 양자화하여 리플리케이션하기'
date: 2026-02-16
categories: [Dev, UE5]
tags: [UE5, Replication, Quantization, CharacterMovement, Locomotion]
use_math: true
---

멀티플레이 환경에서 다른 캐릭터의 가속도는 이동 애니메이션을 결정하는 데 유용하다. 현재 속도만으로는 캐릭터가 가속 중인지, 감속 중인지, 반대 방향으로 전환하려는지 정확히 구분하기 어렵기 때문이다.

그렇다고 `FVector` 가속도를 매번 그대로 전송할 필요는 없다. 애니메이션에 사용할 값이라면 작은 오차를 허용하고, 방향과 크기를 제한된 정수 범위로 바꾸어 전송할 수 있다. 이 글에서는 XY 가속도를 극좌표로 변환하고 Z 가속도와 함께 3개의 8비트 정수로 양자화하는 과정을 정리한다.

## 가속도가 필요한 이유

속도는 현재 어느 방향으로 얼마나 빠르게 움직이는지를 나타내고, 가속도는 속도가 어떻게 변하고 있는지를 나타낸다.

예를 들어 캐릭터의 속도가 아직 앞쪽을 향하고 있더라도 입력을 뒤쪽으로 바꾸면 가속도는 이미 반대 방향을 가리킨다. 따라서 가속도는 다음과 같은 로코모션 상태를 판단할 때 활용할 수 있다.

- 이동 시작과 정지
- 급격한 방향 전환
- 피벗 애니메이션
- 가속과 감속 상태의 블렌딩

서버가 계산한 가속도를 원격 클라이언트의 애니메이션에서도 사용하려면 별도의 리플리케이션 경로가 필요하다.

## 전송 형태 설계

XY 평면의 벡터는 X와 Y를 각각 보내는 대신 다음 두 값으로 표현할 수 있다.

- 방향: $0 \leq \theta < 2\pi$
- 크기: $0 \leq r \leq MaxAcceleration$

여기에 Z축 성분을 더하면 가속도를 세 값으로 표현할 수 있다.

```cpp
USTRUCT()
struct FReplicatedAcceleration
{
    GENERATED_BODY()

    UPROPERTY()
    uint8 AccelXYRadians = 0;

    UPROPERTY()
    uint8 AccelXYMagnitude = 0;

    UPROPERTY()
    int8 AccelZ = 0;
};
```

`AccelXYRadians`와 `AccelXYMagnitude`는 0부터 255까지 사용하고, 부호가 필요한 `AccelZ`는 -127부터 127까지 사용한다. `int8`의 최솟값인 -128을 제외하면 양수와 음수에 동일한 비율을 적용할 수 있다.

리플리케이션할 프로퍼티에는 `ReplicatedUsing`을 지정한다.

```cpp
UPROPERTY(ReplicatedUsing = OnRep_ReplicatedAcceleration)
FReplicatedAcceleration ReplicatedAcceleration;

UFUNCTION()
void OnRep_ReplicatedAcceleration();
```

프로퍼티도 다른 리플리케이션 변수와 마찬가지로 등록해야 한다.

```cpp
void AMyCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AMyCharacter, ReplicatedAcceleration);
}
```

## 서버에서 압축하기

`PreReplication`에서 현재 가속도를 읽어 전송용 값으로 변환할 수 있다.

```cpp
void AMyCharacter::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
    Super::PreReplication(ChangedPropertyTracker);

    UMyCharacterMovementComponent* MovementComponent = Cast<UMyCharacterMovementComponent>(GetCharacterMovement());
    if (!MovementComponent)
    {
        return;
    }

    const double MaxAcceleration = MovementComponent->GetMaxAcceleration();
    if (MaxAcceleration <= UE_SMALL_NUMBER)
    {
        ReplicatedAcceleration = FReplicatedAcceleration{};
        return;
    }

    const FVector CurrentAcceleration = MovementComponent->GetCurrentAcceleration();
    double AccelXYMagnitude = 0.0;
    double AccelXYRadians = 0.0;
    FMath::CartesianToPolar(CurrentAcceleration.X, CurrentAcceleration.Y, AccelXYMagnitude, AccelXYRadians);

    AccelXYRadians = FMath::Fmod(AccelXYRadians + TWO_PI, TWO_PI);

    ReplicatedAcceleration.AccelXYRadians = static_cast<uint8>(FMath::RoundToInt(AccelXYRadians / TWO_PI * 255.0));
    ReplicatedAcceleration.AccelXYMagnitude = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(AccelXYMagnitude / MaxAcceleration * 255.0), 0, 255));
    ReplicatedAcceleration.AccelZ = static_cast<int8>(FMath::Clamp(FMath::RoundToInt(CurrentAcceleration.Z / MaxAcceleration * 127.0), -127, 127));
}
```

여기서 중요한 부분은 각 값을 정해진 범위 안으로 제한하는 것이다.

`CartesianToPolar`로 얻은 각도는 음수가 될 수 있으므로 `Fmod`를 이용해 $[0, 2\pi)$ 범위로 정규화한다. 크기와 Z축 성분도 예상 범위를 벗어났을 때 정수형 값이 순환하지 않도록 `Clamp`를 적용한다. `MaxAcceleration`이 0인 경우에는 나눗셈을 수행하지 않는다.

양자화 식은 다음과 같다.

$$Q_{direction} = round\left(\frac{\theta}{2\pi} \times 255\right)$$

$$Q_{magnitude} = round\left(\frac{r}{MaxAcceleration} \times 255\right)$$

$$Q_z = round\left(\frac{z}{MaxAcceleration} \times 127\right)$$

## 클라이언트에서 복원하기

프로퍼티가 갱신되면 `OnRep`에서 양자화된 값을 다시 가속도 벡터로 복원한다.

```cpp
void AMyCharacter::OnRep_ReplicatedAcceleration()
{
    UMyCharacterMovementComponent* MovementComponent = Cast<UMyCharacterMovementComponent>(GetCharacterMovement());
    if (!MovementComponent)
    {
        return;
    }

    const double MaxAcceleration = MovementComponent->GetMaxAcceleration();
    const double AccelXYMagnitude = static_cast<double>(ReplicatedAcceleration.AccelXYMagnitude) * MaxAcceleration / 255.0;
    const double AccelXYRadians = static_cast<double>(ReplicatedAcceleration.AccelXYRadians) * TWO_PI / 255.0;

    FVector UnpackedAcceleration = FVector::ZeroVector;
    FMath::PolarToCartesian(AccelXYMagnitude, AccelXYRadians, UnpackedAcceleration.X, UnpackedAcceleration.Y);
    UnpackedAcceleration.Z = static_cast<double>(ReplicatedAcceleration.AccelZ) * MaxAcceleration / 127.0;

    MovementComponent->SetReplicatedAcceleration(UnpackedAcceleration);
}
```

복원은 압축의 역순이다.

1. 8비트 방향을 라디안으로 복원한다.
2. 8비트 크기를 실제 가속도 범위로 복원한다.
3. 극좌표의 방향과 크기를 X, Y 성분으로 변환한다.
4. Z축 값을 복원하고 Movement Component에 전달한다.

복원된 값에는 양자화 오차가 존재한다. 따라서 물리 판정의 기준값보다는 원격 캐릭터의 애니메이션 상태를 결정하는 용도로 사용하는 편이 적합하다.

## 구현하면서 확인할 부분

### 양쪽의 최대 가속도가 같아야 한다

압축과 복원 모두 `MaxAcceleration`을 기준으로 계산한다. 서버와 클라이언트의 값이 다르면 같은 정수를 서로 다른 가속도로 해석한다. 런타임에 최대 가속도를 변경한다면 그 값이 양쪽에서 일관되게 유지되는지도 확인해야 한다.

### 입력 가속도와 물리 가속도를 구분해야 한다

`GetCurrentAcceleration()`이 반환하는 값은 Character Movement가 현재 이동에 사용하는 가속도다. 중력이나 외부 힘까지 모두 포함한 월드 공간의 물리 가속도라고 가정해서는 안 된다. Z축이 실제로 필요한 이동 모드인지 먼저 판단할 필요가 있다.

### `PreReplication`은 전송 시점을 뜻하지 않는다

`PreReplication`은 리플리케이션 직전에 값을 준비하는 위치다. 이 함수에서 값을 갱신하더라도 네트워크 업데이트 빈도, 프로퍼티 변경 여부, relevancy 같은 리플리케이션 조건에 따라 실제 전송 시점은 달라진다.

### 구조체 크기와 실제 패킷 크기는 다르다

세 필드의 데이터 크기만 더하면 3바이트지만, 실제 네트워크 전송에는 프로퍼티 식별과 직렬화에 필요한 정보가 추가될 수 있다. 반대로 Unreal의 기본 벡터 직렬화 역시 메모리 표현을 그대로 전송한다고 단정할 수 없다.

따라서 “24바이트가 3바이트가 되어 정확히 87.5% 절감된다”라고 표현하기보다는, **가속도의 표현에 필요한 payload를 세 개의 8비트 값으로 제한했다**고 이해하는 것이 정확하다. 실제 절감량은 Unreal Insights의 Networking Insights나 패킷 분석 도구로 측정해야 한다.

## 정리

가속도 양자화의 핵심은 애니메이션에 필요한 정밀도만 남기는 것이다. XY 성분을 방향과 크기로 나누고 Z축을 별도로 저장하면 작은 정수 세 개로 가속도의 흐름을 전달할 수 있다.

다만 압축률만 보고 적용하기보다는 다음 조건을 먼저 확인해야 한다.

- 원격 캐릭터가 실제로 가속도 정보를 필요로 하는가
- 양자화 오차가 애니메이션 품질에 영향을 주지 않는가
- 서버와 클라이언트가 같은 범위로 압축하고 복원하는가
- 실제 패킷을 측정했을 때 의미 있는 절감 효과가 있는가

이 조건을 만족한다면 가속도 양자화는 네트워크 비용과 원격 로코모션 표현 사이의 균형을 맞추는 방법으로 사용할 수 있다.
