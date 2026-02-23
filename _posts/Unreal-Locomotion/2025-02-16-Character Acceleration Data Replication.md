---
layout: post
title: '[UE5] 캐릭터 가속도 데이터 리플리케이션 최적화'
date: 2026-02-16
categories: [Dev, Unreal, Locomotion]
tags: [UE5, Optimization, Replication, Quantization]
use_math: true
---

# [UE5] 캐릭터 가속도 데이터 리플리케이션 최적화

네트워크 대역폭 절약을 위해 24바이트 FVector 가속도 데이터를 3바이트로 양자화(Quantization)하여 동기화하는 기법 정리


## 1. 소스 코드 (Source Code)

가속도 벡터를 직교 좌표계에서 극좌표계로 변환하여 압축(Pack)하고, 클라이언트에서 다시 복원(Unpack)하는 전체 로직임

```cpp
// 1. 데이터 구조체 정의 (3 Bytes)
USTRUCT()
struct FSSReplicatedAcceleration
{
    GENERATED_BODY()

    // XY 평면상의 가속도 방향 (0 ~ 2PI -> 0 ~ 255)
    UPROPERTY()
    uint8 AccelXYRadians = 0;

    // XY 평면상의 가속도 크기 비율 (0 ~ MaxAccel -> 0 ~ 255)
    UPROPERTY()
    uint8 AccelXYMagnitude = 0;

    // Z축 가속도 성분 (Signed, -MaxAccel ~ MaxAccel -> -127 ~ 127)
    UPROPERTY()
    int8 AccelZ = 0;
};

// 2. 서버 측: 데이터 압축 (PreReplication)
void ASSCharacter::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
    Super::PreReplication(ChangedPropertyTracker);

    if (USSCharacterMovementComponent* CMC = Cast<USSCharacterMovementComponent>(GetCharacterMovement()))
    {
        const double MaxAccel = CMC->MaxAcceleration;
        const FVector CurrentAccel = CMC->GetCurrentAcceleration();
        double AccelXYRadians, AccelXYMagnitude;

        // 직교 좌표계(Cartesian) -> 극좌표계(Polar) 변환
        // X, Y 성분을 크기(Magnitude)와 각도(Radians)로 분리
        FMath::CartesianToPolar(CurrentAccel.X, CurrentAccel.Y, AccelXYMagnitude, AccelXYRadians);

        // [방향 압축] 0 ~ 2PI(360도)를 8비트(0~255)로 매핑
        ReplicatedAcceleration.AccelXYRadians = FMath::FloorToInt((AccelXYRadians / TWO_PI) * 255.0);

        // [크기 압축] 0 ~ MaxAcceleration을 8비트(0~255)로 매핑
        ReplicatedAcceleration.AccelXYMagnitude = FMath::FloorToInt((AccelXYMagnitude / MaxAccel) * 255.0);

        // [Z축 압축] 부호가 있는 Z축 값을 8비트(-127~127)로 매핑
        ReplicatedAcceleration.AccelZ = FMath::FloorToInt((CurrentAccel.Z / MaxAccel) * 127.0);
    }
}

// 3. 클라이언트 측: 데이터 복원 (OnRep)
void ASSCharacter::OnRep_ReplicatedAcceleration()
{
    if (USSCharacterMovementComponent* CMC = Cast<USSCharacterMovementComponent>(GetCharacterMovement()))
    {
        const double MaxAccel = CMC->MaxAcceleration;

        // [역양자화] 8비트 정수를 다시 배정도 실수(double)로 복원
        const double AccelXYMagnitude = double(ReplicatedAcceleration.AccelXYMagnitude) * MaxAccel / 255.0;
        const double AccelXYRadians = double(ReplicatedAcceleration.AccelXYRadians) * TWO_PI / 255.0;

        // 극좌표계 -> 직교 좌표계(X, Y) 복원
        FVector UnpackedAcceleration(FVector::ZeroVector);
        FMath::PolarToCartesian(AccelXYMagnitude, AccelXYRadians, UnpackedAcceleration.X, UnpackedAcceleration.Y);

        // Z축 복원
        UnpackedAcceleration.Z = double(ReplicatedAcceleration.AccelZ) * MaxAccel / 127.0;

        // 무브먼트 컴포넌트에 최종 적용 (애니메이션 등에서 활용)
        CMC->SetReplicatedAcceleration(UnpackedAcceleration);
    }
}
```
---
## 2. 이론 정리 (Study Notes)

### 최적화 원리 및 데이터 절감 효과
기존 `FVector`는 `double`형(UE5 기준 8byte) 3개로 구성되어 총 **24 Bytes**를 차지함. 이를 극좌표계 변환 및 `uint8` 양자화를 통해 **3 Bytes**로 줄여 약 **87.5%**의 대역폭 절감 효과를 얻음

| 구분 | 원본 데이터 (FVector) | 최적화 데이터 (Compressed) | 비고 |
| :--- | :---: | :---: | :--- |
| **자료형** | `double` (x3) | `uint8` (x2) + `int8` (x1) | UE5 `FVector`는 `double` 기반 |
| **크기** | 24 Bytes | **3 Bytes** | **약 87.5% 압축** |
| **표현 방식** | 직교 좌표계 $(x, y, z)$ | 극좌표계 $(\theta, r)$ + $z$ | 방향과 크기로 분리하여 압축 효율 증대 |

### 양자화(Quantization) 수식
연속적인 실수 값을 제한된 비트 수의 정수로 매핑하는 과정임

* **방향 (Direction, XY Plane)**
  * 라디안 범위 $[0, 2\pi]$를 $[0, 255]$로 변환
  * $$Val_{enc} = \lfloor \frac{\theta_{rad}}{2\pi} \times 255 \rfloor$$

* **크기 (Magnitude, XY Plane)**
  * 가속도 크기 $[0, MaxAccel]$을 $[0, 255]$로 변환
  * $$Val_{enc} = \lfloor \frac{M_{current}}{M_{max}} \times 255 \rfloor$$

* **Z축 (Vertical)**
  * 부호 포함 $[-MaxAccel, MaxAccel]$을 $[-127, 127]$로 변환
  * $$Val_{enc} = \lfloor \frac{Z_{current}}{M_{max}} \times 127 \rfloor$$

## 3. 종합 의견 (Review)

이 기법은 네트워크 트래픽이 많은 MMO나 배틀로얄 장르에서 캐릭터의 움직임 데이터를 동기화할 때 필수적인 최적화 테크닉임. 가속도는 위치(Location) 데이터와 달리 약간의 오차가 발생하더라도 '티'가 덜 나기 때문에 과감한 압축(Lossy Compression)이 가능함

### 핵심 요약 및 주의사항
* **손실 압축**: 8비트 양자화 과정에서 정밀도 손실이 발생함. 따라서 정확한 물리 연산보다는 **애니메이션 블렌딩(Start/Stop/Pivot)** 용도로 사용하는 것이 적합함
* **동기화 필수**: 서버와 클라이언트의 `MaxAcceleration` 값이 다르면 엉뚱한 값으로 복원됨. 게임 플레이 도중 최대 가속도가 변경된다면 반드시 해당 변수도 리플리케이션 해야 함
* **비용**: CPU 연산(극좌표 변환) 비용을 지불하여 네트워크 대역폭(Bandwidth)을 아끼는 트레이드오프 전략임

### Tip
* **범용성 확장**: 가속도뿐만 아니라 **속도(Velocity)** 동기화에도 동일한 로직(방향 + 크기)을 적용하여 대역폭을 절약할 수 있음
* **디버깅**: `OnRep` 함수 내에서 `DrawDebugLine`을 사용하여 원본 가속도와 복원된 가속도의 벡터를 시각적으로 비교해보면 오차 범위를 눈으로 확인할 수 있음
* **엔진 기능**: 매우 높은 정밀도가 필요하지 않다면 언리얼 엔진 내장 `FVector_NetQuantize10` (10비트), `FVector_NetQuantizeNormal` 등의 타입을 활용하는 것도 방법이나, 본문의 커스텀 압축이 용량 측면에서는 가장 효율적임
