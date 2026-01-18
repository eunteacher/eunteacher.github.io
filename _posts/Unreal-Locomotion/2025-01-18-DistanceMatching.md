---
layout: post
title:  "[UE5 Distance Matching] Distance Matching을 위한 커브 데이터 생성 및 애니메이션 제작 가이드"
date:   2026-01-13 22:00:00 +0900
categories: [Dev, Unreal, Locomotion]
tags: [UE5, Locomotion, DistanceMatching]
use_math : true
---

# [UE5] Distance Matching을 위한 커브 데이터 생성 및 애니메이션 제작 가이드

# For Programmer

## 소스 코드

Distance Matching 기술의 핵심인 '목표 지점까지 남은 거리'를 계산하여 커브(Curve)에 베이킹하는 C++ 로직

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

---
## 핵심 원리 및 이론

### Distance Matching (거리 매칭) 개요
* **정의**: 캐릭터의 이동 애니메이션을 단순히 시간 흐름(Time)이 아니라, **목표 지점까지 남은 거리(Distance)**에 맞춰 재생하는 기법
* **목적**: 발 미끄러짐(Sliding) 방지 및 정교한 정지(Stop), 방향 전환(Pivot) 등 구현

### 알고리즘 핵심 원리
엔진이 애니메이션을 분석하여 데이터를 생성하는 과정은 다음과 같음

1.  **샘플링 (Sampling)**: 애니메이션을 $120Hz$ ($\Delta t \approx 0.008s$) 단위로 쪼개어 분석
2.  **기준점 선정**: **가장 속도가 느린 지점** 탐색
    * **가정**: "이동 중 속도가 가장 느린 순간 = 멈추거나 방향을 바꾸는 순간"
    * **수식**: $v_{min} = \min \left( \frac{\Delta d}{\Delta t} \right)$
3.  **거리 데이터 생성**:
    * $Time_{stop}$ (정지 시점)을 기준으로 현재 시간 $t$에서의 거리 $D(t)$를 계산
    * $$D(t) = \int_{t}^{Time_{stop}} |v(t)| dt$$

### 기준점(Reference Point) 선정 원리 쉽게 풀이 
알고리즘은 애니메이션 전체를 통틀어 **가장 속도가 느린 지점**을 찾음
* **가정**: "이동 애니메이션에서 속도가 가장 느린 순간이 바로 캐릭터가 멈추거나(Stop) 방향을 바꾸는(Pivot) 순간일 것이다."
* **방법**: 애니메이션 전체를 아주 짧은 시간 간격(예: 1/120초)으로 쪼개어 구간별 이동 속도를 측정하고, 그중 최솟값을 가진 시간을 **'도착 예정지'**로 설정

### 주의사항
* **알고리즘의 맹점**
  * 코드는 단순히 **"가장 적게 움직인 구간"**을 정지 지점으로 인식
* **문제점**
    * 애니메이션 **시작(0프레임)**에 캐릭터가 잠시 멈춰있거나(Idle), 움직임이 미미한 경우 문제가 발생함
    * 알고리즘은 "어? 시작하자마자 멈췄네? 여기가 목표점이구나!"라고 오판하여, 0프레임을 기준점으로 잡음
    * 결과적으로 거리 그래프가 산 모양($\Lambda$)이나 뒤틀린 형태가 되어 인게임 동작이 망가짐

# For Animators

## 애니메이션 제작 시 문제점
Distance Matching이 완벽하게 작동하는 애니메이션을 만들기 위해 다음 규칙을 지켜야 함
* **문제 상황**: 그래프가 튀거나 꼬이는 이유알고리즘은 멍청하게도 **"제일 조금 움직인 곳 = 멈춘 곳"**이라고 믿음
* 만약 애니메이션 시작 부분(0프레임)에 캐릭터가 잠시 멈춰있는 프레임(Idle)이 포함되어 있다면, 알고리즘은 "어? 시작하자마자 멈췄네? 여기가 목표점이구나!" 하고 0프레임을 기준점으로 잡아버림
* 이 경우 그래프가 우상향($/$)이 아니라 산 모양($\Lambda$)이나 뒤틀린 모양이 되어 인게임에서 캐릭터가 버벅거리게 됨

## 해결 방법: 제작 가이드라인
1. **시작 시 '정지 구간(Pre-Idle)' 제거 필수**
   * Distance Matching용 애니메이션(Stop, Pivot 등)은 0프레임부터 즉시 움직임이 발생해야 함
   * 이미 달리고 있는 상태에서 녹화된 것처럼, 0프레임부터 1프레임 루트 모션 이동값이 0이 아니어야 함
2. **'확실한 감속' 표현**
   * 전환점(Pivot) 혹은 정지 지점(Stop)**의 이동 거리가 다른 어떤 구간보다도 확연히 작아야(느려야) 함
   * 예를 들어, 시작 부분 속도가 500이고 멈추는 부분 속도가 10이라면 컴퓨터는 헷갈리지 않음. 하지만 시작 부분 속도가 10이고 멈추는 부분이 5라면, 오차 범위 내에서 잘못된 지점을 선택할 수 있음

3. **(참고) 프로그래머/TA의 보정 옵션**
   * 애니메이션 수정이 어렵다면, 코드 상에서 StartStep을 조정하여 "앞부분 0.2초는 무조건 무시하고 탐색해라"라는 로직을 추가하면 0프레임 정지 문제를 강제로 해결할 수 있으나, **추천하지 않음**

# 결론
* **제작 가이드**: Distance Matching용 소스(Stop, Pivot)는 **0프레임부터 즉시 유의미한 속도로 움직여야 함** (이미 달리고 있는 상태에서 녹화된 것처럼 제작)
* **감속 표현**: 정지 지점(Target)의 이동 거리는 다른 어떤 구간보다 확실하게 작아야(느려야) 컴퓨터가 혼동하지 않음
* **프로그래머의 보정**: 애니메이션 수정이 불가능할 경우, 코드에서 `StartStep` 오프셋을 두어 앞부분(예: 0.2초)을 분석에서 강제로 제외하는 로직 추가를 고려할 것
