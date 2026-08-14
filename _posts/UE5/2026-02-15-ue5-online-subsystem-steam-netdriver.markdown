---
layout: post
title: 'OnlineSubsystemSteam과 NetDriver 연결 실패 해결'
date: 2026-02-15 22:00:00 +0900
categories: [Dev, UE5]
tags: [UE5, C++, Multiplayer, OnlineSubsystemSteam, NetDriver, SteamSockets]
---

Unreal Engine에서 Steam 멀티플레이를 구성할 때는 온라인 서비스와 실제 네트워크 연결을 구분해서 이해해야 한다.

`OnlineSubsystemSteam`은 Steam의 로비, 세션, 친구, 업적과 같은 온라인 기능을 Unreal의 공통 인터페이스로 사용할 수 있게 한다. `NetDriver`는 월드의 액터와 RPC를 서버와 클라이언트 사이에 전달할 실제 연결 방식을 담당한다.

```text
OnlineSubsystemSteam
└─ 로그인, 친구, 로비, 세션, 업적 등

NetDriver
└─ Listen, Connect, Actor Replication, RPC 등
```

두 시스템은 함께 사용되는 경우가 많지만 같은 역할을 수행하지 않는다. 세션 생성에 성공했더라도 NetDriver 설정이 잘못되면 서버를 열거나 접속할 수 없고, NetDriver가 정상이어도 Steam 초기화에 실패하면 Steam 세션과 로비 기능을 사용할 수 없다.

## Online Subsystem의 역할

Online Subsystem은 플랫폼마다 다른 온라인 서비스를 공통 인터페이스로 감싼다.

```cpp
IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();

if (OnlineSubsystem)
{
    IOnlineSessionPtr SessionInterface = OnlineSubsystem->GetSessionInterface();
}
```

`DefaultPlatformService=Steam`으로 설정하면 이름을 지정하지 않은 `IOnlineSubsystem::Get()` 호출은 기본적으로 Steam 구현을 요청한다. Steam 모듈이 로드되지 않았거나 초기화에 실패하면 유효한 인터페이스를 얻지 못할 수 있으므로 포인터의 유효성을 확인해야 한다.

세션 생성과 검색 같은 원격 작업은 즉시 완료된다고 가정해서는 안 된다. Online Subsystem은 비동기 작업의 결과를 델리게이트로 전달하므로 요청 성공 여부와 완료 결과를 나눠 처리해야 한다.

## NetDriver의 역할

`UNetDriver`는 서버와 클라이언트의 네트워크 연결을 관리한다. 리슨 서버를 열 때 `GameNetDriver` 정의를 찾고, 설정된 클래스로 드라이버를 생성한다.

```text
OpenLevel(Map, "listen")
          ↓
GameNetDriver 정의 검색
          ↓
DriverClassName 클래스 로드
          ↓
NetDriver 초기화 및 Listen 시작
```

다음 로그는 드라이버를 생성하지 못했다는 뜻이다.

```text
CreateNamedNetDriver failed to create driver from definition GameNetDriver
NetDriverCreateFailure
```

이 메시지만으로 원인을 하나로 확정할 수는 없다. 클래스 경로 오류, 플러그인 비활성화, 모듈 로드 실패, Steam 초기화 실패, 현재 플랫폼과 맞지 않는 드라이버 선택 등 여러 원인이 같은 최종 오류로 나타날 수 있다. 바로 앞에 출력된 `LogSteam`, `LogNet`, `LogSockets`, `LogModuleManager` 로그를 함께 확인해야 한다.

## SteamNetDriver와 SteamSocketsNetDriver는 선택지다

UE5에서 Steam 연결을 구성하는 대표적인 방법은 두 가지다.

| 구성 | NetDriver | 특징 |
|---|---|---|
| OnlineSubsystemSteam 기본 구성 | `OnlineSubsystemSteam.SteamNetDriver` | Steam OSS 공식 설정 예제에서 사용하는 방식 |
| Steam Sockets 플러그인 구성 | `SteamSockets.SteamSocketsNetDriver` | Steam의 새로운 네트워크 프로토콜 계층을 사용하는 별도 플러그인 |

`SteamSockets`는 모든 Steam 프로젝트에 무조건 필요한 모듈이 아니다. 해당 플러그인의 NetDriver를 선택했을 때만 플러그인 활성화와 전용 드라이버 설정이 필요하다.

두 방식의 Driver와 NetConnection을 섞으면 안 된다.

```text
SteamNetDriver
└─ SteamNetConnection

SteamSocketsNetDriver
└─ SteamSocketsNetConnection
```

Steam Sockets를 사용하는 빌드는 같은 프로토콜을 사용하는 빌드끼리 연결해야 한다. 프로젝트가 어떤 방식을 사용할지 먼저 정하고 설정, 플러그인, 테스트 빌드를 일치시켜야 한다.

## 기본 OnlineSubsystemSteam 구성

Steam Sockets 플러그인을 별도로 사용하지 않는다면 Epic 공식 문서의 기본 구성은 다음 형태다.

```ini
[/Script/Engine.GameEngine]
!NetDriverDefinitions=ClearArray
+NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="/Script/OnlineSubsystemSteam.SteamNetDriver",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")

[OnlineSubsystem]
DefaultPlatformService=Steam

[OnlineSubsystemSteam]
bEnabled=true
SteamDevAppId=480

[/Script/OnlineSubsystemSteam.SteamNetDriver]
NetConnectionClassName="/Script/OnlineSubsystemSteam.SteamNetConnection"
```

`SteamDevAppId=480`은 Valve가 제공하는 테스트용 App ID인 Spacewar다. 개발 과정의 기능 확인에는 사용할 수 있지만 출시할 프로젝트는 발급받은 App ID와 Steamworks 설정을 사용해야 한다.

`bInitServerOnClient=true`는 세션 방식으로 서버를 만들고 참가하는 구성에서 필요하다. Steam 로비를 사용하는 경우에는 필요하지 않으므로 온라인 흐름을 정한 뒤 추가한다.

```ini
[OnlineSubsystemSteam]
bEnabled=true
SteamDevAppId=480
bInitServerOnClient=true
```

인터넷에서 찾은 Steam 설정을 전부 합치는 방식은 피해야 한다. 엔진 버전과 세션·로비 방식에 따라 필요한 항목이 다르고, 서로 다른 전송 방식을 위한 설정이 섞일 수 있다.

## Steam Sockets 플러그인 구성

Steam Sockets를 사용하려면 에디터의 `Edit → Plugins → Networking → Steam Sockets`에서 플러그인을 활성화하고 에디터를 재시작한다. 그다음 대상 플랫폼의 Engine 설정에서 전용 NetDriver를 지정한다.

```ini
[/Script/Engine.GameEngine]
!NetDriverDefinitions=ClearArray
+NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="/Script/SteamSockets.SteamSocketsNetDriver",DriverClassNameFallback="/Script/SteamSockets.SteamNetSocketsNetDriver")
```

Steam Sockets는 Windows, macOS, Linux 빌드에서 사용할 수 있다. 다른 플랫폼을 함께 지원하거나 Steam 외의 PC 스토어에도 같은 빌드를 배포한다면 플랫폼별 설정을 분리해야 한다.

`bUseSteamNetworking`은 SteamSockets SocketSubsystem을 기본으로 사용할지 제어하며 기본값은 `true`다. 일반적인 신규 구성에서는 굳이 같은 값을 반복해서 적을 필요가 없다. 기존 Steam Networking 방식으로 이전해야 하는 특별한 상황에서 명시적으로 검토한다.

## Build.cs에는 실제로 사용하는 모듈만 추가한다

온라인 세션 코드를 직접 작성한다면 보통 공통 인터페이스를 제공하는 모듈이 필요하다.

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "Core",
    "CoreUObject",
    "Engine",
    "OnlineSubsystem",
    "OnlineSubsystemUtils"
});

DynamicallyLoadedModuleNames.Add("OnlineSubsystemSteam");
```

정확한 의존성 구성은 코드에서 포함하는 헤더와 사용하는 API에 따라 달라진다. 플러그인을 활성화했다는 이유만으로 게임 모듈이 `SteamSockets`의 C++ API를 직접 참조하는 것은 아니다. 전용 타입이나 헤더를 직접 사용하지 않는다면 무조건 `PublicDependencyModuleNames`에 추가할 필요는 없다.

모듈 설정을 바꾼 뒤에는 프로젝트 파일을 갱신하고 C++ 프로젝트를 다시 빌드해야 한다. 에디터에서 플러그인만 켠 상태와 패키징된 실행 파일에 모듈이 포함된 상태는 구분해서 확인한다.

## listen 옵션은 서버를 열 때만 사용한다

맵 이름 뒤의 `?listen`은 단순한 레벨 이동 옵션이 아니다. 현재 프로세스를 리슨 서버로 열도록 요청한다.

```cpp
UGameplayStatics::OpenLevel(this, FName("Lobby"), true, TEXT("listen"));
```

이 호출이 실행되면 엔진은 `GameNetDriver` 생성을 시도한다. Steam과 NetDriver 설정이 준비되지 않은 상태에서 타이틀 화면이나 일반 맵 이동에 `?listen`을 붙이면 필요하지 않은 네트워크 초기화가 발생한다.

```text
메인 메뉴 진입
└─ 일반적인 로컬 레벨 로드

방 만들기 성공
└─ 호스트 맵을 ?listen으로 오픈

방 참가 성공
└─ 검색 결과의 접속 주소로 ClientTravel
```

다만 `?listen`을 제거하는 것은 NetDriver 설정 오류의 근본 해결이 아니다. 해당 맵을 서버로 열 필요가 없을 때 잘못된 호출을 제거하는 것이다. 실제 호스트 기능에서는 NetDriver가 정상적으로 생성돼야 한다.

## 실제 사례: Steam NetDriver를 생성하지 못한 문제

멀티플레이 기능을 구현하면서 타이틀에서 로비를 거쳐 매치로 이동하는 흐름을 구성했다.

```text
Title
  ↓
Lobby
  ↓
Match
```

그런데 맵을 전환하거나 호스트를 시작할 때 `GameNetDriver` 생성에 실패하면서 맵 로딩이 중단됐다.

```text
LogNet: CreateNamedNetDriver failed to create driver from definition GameNetDriver
LogNet: Error: UEngine::BroadcastNetworkFailure: FailureType = NetDriverCreateFailure
LogNet: Error: LoadMap: failed to Listen(/Game/Level/Title?listen)
```

마지막 `NetDriverCreateFailure`만 보면 네트워크 전체가 고장 난 것처럼 보이지만, 로그에는 두 문제가 함께 드러나 있었다.

### 네트워크가 필요 없는 타이틀 맵에서 listen을 시도했다

타이틀은 메뉴를 표시하는 로컬 레벨이었지만 맵을 열 때 `?listen`이 붙어 있었다. 이 옵션 때문에 타이틀 진입 단계부터 `GameNetDriver`를 생성하려 했다.

```text
기존 흐름
Title?listen → 타이틀 진입과 동시에 NetDriver 생성 시도

수정한 흐름
Title        → 로컬 메뉴로 진입
방 만들기    → 호스트 맵을 ?listen으로 오픈
```

타이틀과 일반 로비 진입에서는 `?listen`을 제거하고, 사용자가 실제로 방을 만들었을 때만 리슨 서버를 열도록 맵 이동 책임을 분리했다. 이 수정으로 네트워크가 필요 없는 단계에서 발생하던 불필요한 초기화 실패를 제거했다.

### 설정한 SteamSockets 드라이버를 프로젝트가 로드하지 못했다

프로젝트는 `DefaultEngine.ini`에서 `SteamSockets.SteamSocketsNetDriver`를 사용하도록 설정했지만, 당시 프로젝트 구성에는 그 클래스를 제공하는 플러그인과 모듈이 제대로 포함되지 않은 상태였다.

```text
DefaultEngine.ini
└─ SteamSocketsNetDriver 사용 요청

프로젝트 빌드
└─ Steam Sockets 구성 요소가 준비되지 않음

결과
└─ 지정한 NetDriver 클래스 생성 실패
```

이 프로젝트에서는 기본 `SteamNetDriver`로 되돌리는 대신 Steam Sockets 방식을 사용하기로 결정했다. 따라서 설정에 맞춰 다음 구성 요소를 활성화했다.

1. 에디터의 Plugins 메뉴에서 `Steam Sockets` 플러그인 활성화
2. 에디터 재시작
3. 프로젝트가 사용하던 온라인 모듈과 SteamSockets 모듈을 Build.cs에 포함
4. `GameNetDriver`와 `NetConnectionClassName`을 SteamSockets 계열로 통일
5. 프로젝트 파일 갱신 후 전체 C++ 빌드

당시 Build.cs에는 다음 모듈을 추가했다.

```csharp
PublicDependencyModuleNames.AddRange(new string[]
{
    "OnlineSubsystem",
    "OnlineSubsystemSteam",
    "SteamSockets"
});
```

이 설정은 당시 프로젝트에서 Steam 관련 타입과 모듈을 직접 사용하는 구성에 맞춘 것이다. 모든 프로젝트에서 세 모듈을 `PublicDependencyModuleNames`에 넣어야 한다는 뜻은 아니다. 게임 코드가 사용하는 헤더와 API에 따라 `PrivateDependencyModuleNames` 또는 `DynamicallyLoadedModuleNames`로 범위를 조정할 수 있다.

### NetDriver와 NetConnection을 같은 계열로 맞췄다

당시 프로젝트의 핵심 설정은 다음과 같은 형태였다.

```ini
[/Script/Engine.GameEngine]
!NetDriverDefinitions=ClearArray
+NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="/Script/SteamSockets.SteamSocketsNetDriver",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")

[OnlineSubsystem]
DefaultPlatformService=Steam

[OnlineSubsystemSteam]
bEnabled=true
SteamDevAppId=480
bInitServerOnClient=true
bUseSteamNetworking=true
bAllowP2PPacketRelay=true

[/Script/SteamSockets.SteamSocketsNetDriver]
NetConnectionClassName="/Script/SteamSockets.SteamSocketsNetConnection"
ConnectionTimeout=80.0
InitialConnectTimeout=120.0
```

원래 설정에 포함돼 있던 음성 채팅, Presence, HTTP, XMPP 등의 옵션은 NetDriver 생성 실패를 해결하는 핵심 조건이 아니므로 여기서는 제외했다. 문제 해결에 직접 관계있는 설정만 남기면 어떤 변경이 실제 원인에 영향을 줬는지 구분하기 쉽다.

현재 Epic의 Steam Sockets 문서 예제는 `SteamSockets.SteamNetSocketsNetDriver`를 전용 fallback으로 사용한다. 위의 `IpNetDriver` fallback은 당시 프로젝트에서 적용했던 구성이다. 엔진 버전을 올리거나 새 프로젝트에 적용할 때는 기존 설정을 그대로 복사하지 말고 사용하는 UE 버전의 공식 문서와 대상 플랫폼을 기준으로 fallback을 다시 선택해야 한다.

### 해결 결과를 두 단계로 나눠 확인했다

수정 결과도 하나의 성공 여부로 묶지 않고 두 단계로 확인했다.

```text
타이틀 진입
└─ NetDriver를 만들지 않고 정상 로드

호스트 생성
└─ SteamSocketsNetDriver 생성
   └─ 호스트 맵을 Listen 상태로 정상 로드
```

타이틀이 열렸다는 사실만으로 Steam 연결이 해결됐다고 판단하지 않았다. 실제 호스트 생성 시점의 로그에서 `GameNetDriver`가 생성되는지, 지정한 드라이버 클래스가 사용되는지 확인해야 설정 문제까지 해결됐다고 볼 수 있다.

이 사례에서 `?listen` 제거는 잘못된 실행 시점을 바로잡은 수정이고, 플러그인·모듈·ini 정리는 실제 호스트에 필요한 드라이버를 로드할 수 있게 만든 수정이다. 서로 다른 두 문제를 함께 고쳤기 때문에 증상과 원인을 나눠 기록하는 것이 중요했다.

## NetDriverCreateFailure를 확인하는 순서

### 1. 서버를 열 의도가 있었는지 확인한다

오류가 발생한 `OpenLevel`, `ServerTravel`, 콘솔 명령에 `listen`이 붙어 있는지 확인한다. 네트워크가 필요 없는 화면이라면 호출 흐름부터 수정한다.

### 2. GameNetDriver 정의를 확인한다

```ini
[/Script/Engine.GameEngine]
!NetDriverDefinitions=ClearArray
+NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="...",DriverClassNameFallback="...")
```

같은 이름의 정의가 여러 ini 파일에서 중복되거나 배열 초기화 순서가 꼬이지 않았는지 확인한다. 최종 병합 설정은 에디터에서 보는 원본 파일 하나와 다를 수 있다.

### 3. 클래스 경로와 플러그인을 맞춘다

`OnlineSubsystemSteam.SteamNetDriver`를 사용한다면 Online Subsystem Steam이 활성화되어야 한다. `SteamSockets.SteamSocketsNetDriver`를 사용한다면 Steam Sockets 플러그인과 전용 설정이 필요하다.

### 4. Steam 초기화 로그를 확인한다

Steam 클라이언트 실행 여부, App ID, 사용자 로그인 상태, 서브시스템 로드 결과를 확인한다. `IOnlineSubsystem::Get()`이 반환한 서브시스템의 이름을 출력하면 현재 기본 서비스가 무엇인지도 알 수 있다.

```cpp
IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get();

if (OnlineSubsystem)
{
    UE_LOG(LogTemp, Log, TEXT("Online subsystem: %s"), *OnlineSubsystem->GetSubsystemName().ToString());
}
else
{
    UE_LOG(LogTemp, Error, TEXT("Online subsystem is unavailable"));
}
```

### 5. Standalone 또는 패키징 빌드에서도 확인한다

PIE의 여러 플레이어 창은 동일한 Steam 계정과 프로세스 환경을 공유하므로 실제 두 사용자 접속과 조건이 다르다. Steam 기능은 별도 프로세스의 Standalone 실행이나 패키징 빌드, 서로 다른 Steam 계정으로 최종 확인한다.

### 6. Fallback 성공을 Steam 성공으로 오해하지 않는다

기본 설정의 `DriverClassNameFallback`은 주 드라이버가 실패했을 때 `IpNetDriver`로 대체할 수 있게 한다. 연결이 열렸다는 사실만으로 Steam NetDriver가 사용됐다고 단정하지 말고 로그에 생성된 실제 드라이버 클래스를 확인한다.

## 설정을 최소 단위로 검증한다

Steam 연동에 문제가 생기면 음성 채팅, Presence, 로비, P2P Relay 같은 설정을 한꺼번에 추가하기보다 작은 단계로 확인하는 편이 원인을 찾기 쉽다.

```text
1. 플러그인과 모듈 로드 확인
2. 기본 Online Subsystem이 Steam인지 확인
3. GameNetDriver 생성 확인
4. Listen 서버 시작 확인
5. 세션 또는 로비 생성 확인
6. 다른 계정에서 검색과 참가 확인
7. 필요한 부가 기능을 하나씩 추가
```

기능마다 성공 로그를 남기면 오류가 온라인 서비스 초기화, 세션 처리, NetDriver 생성, 맵 이동 중 어느 단계에서 발생했는지 분리할 수 있다.

## 정리

`OnlineSubsystemSteam`은 Steam의 온라인 기능을 Unreal 인터페이스로 제공하고, `NetDriver`는 실제 게임 네트워크 연결과 복제를 담당한다.

```text
Steam 서비스 선택     → OnlineSubsystemSteam
세션 또는 로비 처리    → Session Interface
리슨 서버 생성         → GameNetDriver
게임 상태와 RPC 전송   → 선택한 NetDriver
```

Steam 연동 설정에서 중요한 것은 많은 옵션을 넣는 것이 아니라 하나의 네트워크 방식을 선택하고 관련 구성 요소를 일치시키는 것이다. `SteamNetDriver`와 `SteamSocketsNetDriver`를 구분하고, 실제로 서버를 여는 시점에만 `?listen`을 사용하며, 최종 실패 로그보다 먼저 출력된 모듈과 소켓 로그에서 원인을 찾아야 한다.

## 참고 자료

- [Online Subsystem Steam](https://dev.epicgames.com/documentation/en-us/unreal-engine/online-subsystem-steam-interface-in-unreal-engine)
- [Using Steam Sockets](https://dev.epicgames.com/documentation/unreal-engine/using-steam-sockets-in-unreal-engine)
- [Online Subsystem](https://dev.epicgames.com/documentation/unreal-engine/online-subsystem-in-unreal-engine)
