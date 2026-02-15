---
layout: post
title: "[UE5 SmileShooter MultiPlayer 02] OnlineSubsystemSteam 네트워크 드라이버 연동 및 초기화 실패 해결"
date: 2026-02-15 22:00:00 +0900
categories: [ Dev, Unreal, SubProject ]
tags: [ UE5, C++, Multiplayer ]
use_math: true
---

# [UE5 SmileShooter MultiPlayer] OnlineSubsystemSteam 네트워크 드라이버 연동 및 오류 해결

---

## 📝 문제 해결 보고서: OnlineSubsystemSteam 네트워크 드라이버 연동

### 1-1. 발생했던 문제
* **프로세스 흐름**: Title (메인 메뉴) -> Lobby (대기실) -> Match (게임 시작)
* **현상**: 게임 실행 후 맵을 전환하거나 호스트를 시작하려 할 때, 네트워크 드라이버를 초기화하지 못해 맵 로딩이 실패하고 게임 진행이 불가능한 현상 발생
* **핵심 에러 로그**:
```
LogNet: CreateNamedNetDriver failed to create driver from definition GameNetDriver
LogNet: Error: UEngine::BroadcastNetworkFailure: FailureType = NetDriverCreateFailure ...
LogNet: Error: LoadMap: failed to Listen(/Game/Level/Title?listen)
```
### 1-2 원인 분석:
* 네트워크 드라이버 누락: 설정 파일(DefaultEngine.ini)에서는 **OnlineSubsystemSteam**를 사용하라고 지시했으나, 실제 엔진/프로젝트 빌드에 해당 모듈이나 플러그인이 포함되지 않아 클래스를 메모리에 로드할 수 없었음
* 불필요한 리슨 서버 시도: 단순 UI 레벨인 Title이나 초기 진입 단계에서 ?listen 옵션을 사용하여 불필요하게 네트워크 드라이버 초기화를 시도함

---
## 2. 적용된 솔루션 (Solutions)
* 문제를 해결하기 위해 로직(Logic), 의존성(Dependency), 설정(Configuration) 세 가지 측면에서 수정을 진행

### ✅ 솔루션 1: 맵 전환 로직 수정 (Level Transition)
* **조치**: Title 레벨이나 Lobby 진입 시 OpenLevel 노드(혹은 코드)에서 ?listen 옵션을 제거

* **이유**: 메인 메뉴(Title)는 네트워크 연결이 필요 없는 단독(Standalone) 클라이언트 환경이어야 함. ?listen을 제거함으로써 불필요한 GameNetDriver 초기화 시도를 막아 1차적인 크래시를 방지하고 로직을 명확히 함 (실제 호스팅은 '방 만들기' 버튼을 누를 때 수행)

### ✅ 솔루션 2: 플러그인 및 모듈 활성화 (System Dependency)
* **OnlineSubsystemSteam**이 정상 작동하지 않던 원인을 해결하기 위해 필수 구성 요소를 추가
* **플러그인 활성화**: 에디터 메뉴 -> Plugins -> Steam Sockets 플러그인 Enabled 설정

* 빌드 스크립트 수정 (ProjectName.Build.cs): **SteamSockets** 모듈을 의존성 목록에 명시적으로 추가하여, 패키징 시 코드가 포함되도록 조치
```C#
PublicDependencyModuleNames.AddRange(new string[] {
"...",
"OnlineSubsystem",
"OnlineSubsystemSteam",
"SteamSockets" // <--- 필수 추가
});
```

### ✅ 솔루션 3: DefaultEngine.ini 최종 구성
* **SteamSockets**를 사용하기 위해 **DefaultEngine.ini** 파일을 다음과 같이 구성

```
[/Script/Engine.GameEngine]
!NetDriverDefinitions=ClearArray
; SteamSockets를 메인 드라이버로 설정하고, 실패 시 IpNetDriver로 대체(Fallback)하도록 설정하여 안정성 확보
+NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="/Script/SteamSockets.SteamSocketsNetDriver",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")

[OnlineSubsystem]
DefaultPlatformService=Steam
bHasVoiceEnabled=true
VoiceNotificationDelta=0.2
MaxLocalTalkers=1
MaxRemoteTalkers=16
PollingIntervalInMs=20
bUseBuildIdOverride=false
BuildIdOverride=0
!AdditionalModulesToLoad=Clear
+AdditionalModulesToLoad=HTTP
+AdditionalModulesToLoad=XMPP

[OnlineSubsystemSteam]
bEnabled=true
SteamDevAppId=480
bInitServerOnClient=true
bUsesPresence=true
bUseLobbiesIfAvailable=true
bUseSteamNetworking=true
bAllowP2PPacketRelay=true

; 기존 SteamNetDriver 설정 (호환성 유지용)
[/Script/OnlineSubsystemSteam.SteamNetDriver]
NetConnectionClassName="OnlineSubsystemSteam.SteamNetConnection"

; [핵심] SteamSockets 전용 설정 섹션
[/Script/SteamSockets.SteamSocketsNetDriver]
NetConnectionClassName="/Script/SteamSockets.SteamSocketsNetConnection"
ConnectionTimeout=80.0
InitialConnectTimeout=120.0
```
