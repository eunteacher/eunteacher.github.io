---
layout: post
title: 'Distance Matching을 위한 거리 커브 생성하기'
date: 2026-01-13 22:00:00 +0900
categories: [Dev, UE5]
tags: [UE5, Locomotion, DistanceMatching, RootMotion, AnimationModifier]
use_math: true
---

일반적인 애니메이션은 시간에 따라 재생된다. 하지만 정지 애니메이션의 이동 거리와 캐릭터가 목표 지점까지 실제로 남겨 둔 거리가 다르면 발이 미끄러지거나 정지 위치가 어긋날 수 있다.

Distance Matching은 애니메이션의 재생 위치를 단순한 시간이 아니라 **목표 지점까지 남은 거리**에 맞추는 방식이다. 이를 사용하려면 애니메이션의 각 시점에서 기준점까지의 거리를 기록한 커브가 필요하다.

이 글에서는 Root Motion을 분석해 정지 지점을 찾고, 해당 지점을 기준으로 거리 커브를 생성하는 과정을 정리한다.

## 거리 커브의 역할

정지 시점을 $t_s$, 현재 애니메이션 시점을 $t$라고 하면 거리 커브는 두 시점 사이의 Root Motion 이동 거리를 나타낸다.

$$D(t) = \left|P(t) - P(t_s)\right|$$

코드에서는 정지 지점 전후를 구분하기 위해 정지 이전 거리에 음수, 정지 이후 거리에 양수를 붙인다.

```text
애니메이션 시간        정지 지점
0 -----------------------●---------------- 끝

거리 커브          음수  →  0  →  양수
```

런타임에서는 캐릭터의 남은 이동 거리와 이 커브를 비교하여 알맞은 애니메이션 시점을 선택할 수 있다.

## 거리 커브 생성 코드

아래 코드는 기존에 사용한 엔진 코드를 주석까지 포함하여 그대로 옮긴 것이다.

```cpp
void UDistanceCurveModifier::OnApply_Implementation(UAnimSequence* Animation)
{
    // 1. 유효성 검사: 애니메이션 및 루트 모션 존재 여부 확인
    if (Animation == nullptr || !Animation->HasRootMotion()) return;

    // 2. 초기화: 기존 커브 데이터를 초기화하거나 새로 생성
    UAnimationBlueprintLibrary::AddCurve(Animation, CurveName, ERawCurveTrackTypes::RCT_Float, false);

    const float AnimLength = Animation->GetPlayLength();
    float TimeOfMinSpeed = 0.f;

    // 3. 정지 지점(Stop/Pivot Point) 탐색 단계
    // 알고리즘: 전체 구간 중 속도가 가장 느린 지점(Global Minimum Speed)을 목표점으로 가정함
    if(bStopAtEnd)
    { 
       TimeOfMinSpeed = AnimLength; // 옵션 활성화 시, 끝점을 강제로 정지 지점으로 설정
    }
    else
    {
       float MinSpeedSq = FMath::Square(StopSpeedThreshold); // 속도 비교를 위한 임계값(초기값)

       // 120Hz(약 0.008초) 간격으로 애니메이션 전체를 고해상도 샘플링
       float SampleInterval = 1.f / 120.f;
       int32 NumSteps = AnimLength / SampleInterval;

       for (int32 Step = 0; Step < NumSteps; ++Step)
       {
          const float Time = Step * SampleInterval;

          // 현재 시점(Time)에서 아주 짧은 구간(SampleInterval) 동안의 루트 모션 추출
          const FAnimExtractContext Context(static_cast<double>(Time), true, FDeltaTimeRecord(SampleInterval), false);
          const FVector RootMotionTranslation = Animation->ExtractRootMotion(Context).GetTranslation();
          
          // 순간 속도 계산 (비교 연산 최적화를 위해 제곱값 사용)
          // 속도 ≈ 이동 거리 / 시간
          const float RootMotionSpeedSq = CalculateMagnitudeSq(RootMotionTranslation, Axis) / SampleInterval;

          // 최솟값 갱신: 가장 느린 지점을 '정지 예정 지점'으로 판단
          if (RootMotionSpeedSq < MinSpeedSq)
          {
             MinSpeedSq = RootMotionSpeedSq;
             TimeOfMinSpeed = Time; 
          }
       }
    }

    // 4. 거리 데이터 베이킹 (Baking)
    // 탐색된 정지 지점(TimeOfMinSpeed)을 기준으로 거리를 역산하여 커브에 기록
    float SampleInterval = 1.f / SampleRate; // 실제 게임 런타임용 해상도 적용
    int32 NumSteps = FMath::CeilToInt(AnimLength / SampleInterval);
    float Time = 0.0f;

    for (int32 Step = 0; Step <= NumSteps && Time < AnimLength; ++Step)
    {
       Time = FMath::Min(Step * SampleInterval, AnimLength);

       // 부호 결정: 정지 지점 도달 전(-), 도달 후(+)
       const float ValueSign = (Time < TimeOfMinSpeed) ? -1.0f : 1.0f;

       // 거리 계산 (Distance Calculation)
       // 현재 위치(Time)부터 목표 정지 지점(TimeOfMinSpeed)까지의 누적 이동 거리 계산
       const FVector RootMotionTranslation = Animation->ExtractRootMotionFromRange(TimeOfMinSpeed, Time, FAnimExtractContext()).GetTranslation();
       
       // 커브 키 추가: X축(Time) -> Y축(Remaining Distance)
       UAnimationBlueprintLibrary::AddFloatCurveKey(Animation, CurveName, Time, ValueSign * CalculateMagnitude(RootMotionTranslation, Axis));
    }
}
```

## 코드의 처리 과정

### 1. 애니메이션과 Root Motion 확인

거리 계산은 애니메이션의 Root Motion을 기준으로 한다. 애니메이션이 유효하지 않거나 Root Motion이 없으면 Modifier를 종료한다. 이후 지정한 이름의 Float Curve를 준비한다.

### 2. 정지 지점 탐색

`bStopAtEnd`가 활성화되어 있으면 애니메이션의 마지막 시점을 정지 지점으로 사용한다. 그렇지 않으면 전체 구간을 120Hz 간격으로 샘플링한다.

각 구간에서 Root Motion 이동량을 추출하고 `CalculateMagnitudeSq`로 비교값을 계산한다. 가장 작은 값이 발견될 때마다 `TimeOfMinSpeed`를 갱신한다. 이 과정에서 선택된 가장 느린 시점이 거리 커브의 기준점이 된다.

### 3. 거리 커브 베이킹

정지 지점을 찾은 다음에는 `SampleRate`에 맞춰 애니메이션을 다시 순회한다. 현재 시점과 `TimeOfMinSpeed` 사이의 Root Motion을 추출하고, 선택한 축의 이동 거리를 커브 값으로 기록한다.

정지 지점 이전 값에는 음수를 붙이고 이후 값에는 양수를 붙이므로, 기준점을 중심으로 애니메이션의 어느 구간인지 구분할 수 있다.

## 가장 느린 지점을 기준으로 삼는 이유

Stop이나 Pivot 애니메이션은 목표 지점에 가까워질수록 Root Motion의 이동량이 작아진다. 따라서 전체 애니메이션에서 가장 느린 구간을 찾으면 정지하거나 방향을 전환하는 시점을 자동으로 선택할 수 있다.

이 방식은 애니메이션마다 기준 프레임을 직접 입력하지 않아도 된다는 장점이 있다. 하지만 코드는 애니메이션의 의미를 이해하는 것이 아니라 **가장 적게 움직인 구간**만 찾기 때문에 소스 애니메이션의 구성에 영향을 받는다.

## 시작 프레임이 기준점으로 선택되는 문제

애니메이션 시작 부분에 Idle 프레임이 포함되어 있으면 0초 부근의 이동량이 실제 정지 지점보다 작을 수 있다. 그러면 알고리즘은 시작 부분을 목표 지점으로 선택하고, 의도와 다른 거리 커브를 만든다.

```text
의도한 흐름
큰 이동량 ─────────→ 감속 ─────────→ 정지

문제가 되는 흐름
정지 프레임 → 이동 ────────────────→ 실제 정지
     ↑
잘못 선택될 수 있는 기준점
```

이 문제는 코드만의 문제가 아니라 Distance Matching용 애니메이션의 제작 조건과 연결되어 있다.

## 애니메이션 제작 시 확인할 조건

### 시작부터 Root Motion 이동이 있어야 한다

Stop과 Pivot 애니메이션 앞에 불필요한 Idle 구간을 넣지 않는다. 이미 움직이는 상태에서 애니메이션이 시작된 것처럼 첫 구간부터 Root Motion 변화가 있어야 한다.

### 목표 지점의 감속이 명확해야 한다

정지 또는 방향 전환 지점의 이동량이 다른 구간보다 충분히 작아야 한다. 여러 구간의 이동량이 비슷하면 의도하지 않은 위치가 최솟값으로 선택될 수 있다.

### 분석할 이동 축을 일관되게 사용해야 한다

`CalculateMagnitude`와 `CalculateMagnitudeSq`는 `Axis` 설정에 따라 분석할 성분을 결정한다. 전진 축만 사용할지, 평면 이동 전체를 사용할지 애니메이션 제작 방향과 맞춰야 한다.

## 애니메이션을 수정하기 어려운 경우

소스 애니메이션의 시작 정지 구간을 제거할 수 없다면 검색 시작 위치를 뒤로 옮겨 앞부분을 탐색 대상에서 제외할 수 있다. 다만 모든 애니메이션에 동일한 고정 시간을 적용하면 다른 애니메이션의 실제 기준점을 건너뛸 수 있다.

필요하다면 다음 방법도 고려할 수 있다.

- 정지 애니메이션은 항상 마지막 프레임을 기준으로 사용한다.
- Anim Notify나 별도의 메타데이터로 기준 시점을 지정한다.
- 애니메이션별로 탐색 가능한 시간 범위를 설정한다.
- 속도가 임계값 아래로 내려간 구간을 별도의 규칙으로 선택한다.

## 생성 결과 확인하기

Modifier를 적용한 뒤에는 거리 커브와 Root Motion을 함께 확인해야 한다.

1. 의도한 정지 지점에서 커브가 0이 되는지 확인한다.
2. 정지 지점 이전의 값이 자연스럽게 0에 가까워지는지 확인한다.
3. 커브가 갑자기 꺾이면 Root Motion이 왕복하는 구간이 있는지 확인한다.
4. 애니메이션 시작 부분이 기준점으로 잘못 선택되지 않았는지 확인한다.
5. 런타임에서 계산한 남은 거리와 커브가 같은 축과 단위를 사용하는지 확인한다.

## 정리

Distance Matching용 거리 커브는 애니메이션의 정지 지점을 찾고, 각 시점에서 해당 지점까지의 Root Motion 이동 거리를 기록하여 만든다.

가장 느린 지점을 자동으로 선택하는 방식은 여러 애니메이션을 처리하기 편하지만 시작 프레임의 정지 구간과 감속 표현에 영향을 받는다. 따라서 커브 생성 코드와 함께 애니메이션 제작 규칙을 정해야 안정적인 결과를 얻을 수 있다.
