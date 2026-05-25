# ChattingServer 포트폴리오 PPT 작성 지시서

> **이 지시서는 코드를 읽지 않은 Claude가 이 문서만 보고 PowerPoint를 제작할 수 있도록 작성됨.**
> **목적: 컴투스 게임서버 공채 포트폴리오 발표용**

---

## 제작 기본 규칙

- **슬라이드 수**: 총 11장
- **디자인**: 짙은 남색 또는 검정 배경 / 흰색·청록색 텍스트
- 코드·다이어그램 블록은 어두운 박스 배경 처리
- 슬라이드에 실제 코드 스니펫 넣지 말 것 → 도식·다이어그램으로만 표현
- 기조: "단순한 채팅 서버가 아닌 실서버 수준 설계"를 강조

---

## 슬라이드 1 — 표지

**제목**: `C++ IOCP ChattingServer`
**부제목**: `AcceptEx 기반 고성능 채팅 서버 포트폴리오`

하단에 기술 태그 배지 형태로 나열:
`C++17` `IOCP / AcceptEx` `Redis` `멀티스레딩` `패킷 암호화` `섹터 브로드캐스트`

이름 / 날짜 기입란

---

## 슬라이드 2 — 프로젝트 개요

**제목**: 프로젝트 개요

**왼쪽 텍스트 블록**:
```
목표
  IOCP AcceptEx 기반의 실서버 수준 채팅 서버 구현

핵심 구현 목표
  - 고성능 비동기 네트워크 처리 (Worker 1개로 Recv 16만 TPS 달성)
  - 단일 Contents Thread로 Lock-Free 게임 로직 구현
  - Redis 연동을 통한 세션 인증
  - 패킷 암호화 + SeqNumber 기반 보안
  - 50x50 섹터 기반 범위 채팅 브로드캐스트
```

**오른쪽 강조 박스 (밝은 색으로 눈에 띄게)**:
```
실측 성능 (단일 서버 기준)
┌─────────────────────────┐
│  Recv TPS  │  ~160,000  │
│  Send TPS  │  ~500,000  │
│  CPU 사용  │    46%     │
│  세션 최대 │    7,000   │
└─────────────────────────┘
```

---

## 슬라이드 3 — 전체 시스템 아키텍처

**제목**: 전체 시스템 아키텍처

중앙에 아래 흐름을 화살표 도식으로 그릴 것:

```
[클라이언트 다수] ──TCP──▶ [AcceptEx_IOCP_NetworkLib]
                                      │
                              [Worker Thread 1개]
                              (IOCP 완료 처리)
                              onAccept / onRecv / onRelease 콜백
                                      │
                                      ▼
                              [ContentsQ : std::queue]
                              (최대 14만 메시지 버퍼 가능)
                                      │
                                      ▼
                              [Contents Thread 1개]
                              Player / Sector 로직 단일 처리
                                      │
                              [Monitor Thread 1개]
                              1초마다 TPS 콘솔 출력
                                      │
                                      ▼
                              [Redis Server]
                              session:{accountNo}
                              - sessionToken
                              - fixedKey (암호화 키)
```

다이어그램 아래 설명:
> NetworkLib(IOCP)와 게임 로직(ContentsThread)을 완전히 분리한 구조.
> 모든 패킷은 ContentsQ를 경유해 단일 스레드에서 처리됨.

---

## 슬라이드 4 — NetworkLib 핵심 설계

**제목**: AcceptEx_IOCP_NetworkLib — 핵심 설계

**왼쪽 — Session 구조 박스**:
```
Session 구조
┌─────────────────────────────┐
│  SOCKET mSock               │
│  SeqAndIdx mSessionID       │  ← 64비트 세션 ID
│  RingBuffer* mRecvBuffer    │  ← 수신 링버퍼 (2001 bytes)
│  queue<Message*> mSendQ     │  ← 송신 큐
│  short mIOcnt               │  ← IO 참조 카운트
│  char mLive                 │  ← 세션 활성 상태
│  char mSendFlag             │  ← 중복 Send 방지
│  AcceptOv / RecvOv / SendOv │  ← Overlapped 구조체
└─────────────────────────────┘
```

**오른쪽 — SeqAndIdx 비트 구조 박스 (필수 도식화)**:
```
SeqAndIdx — 64비트 세션 ID
┌─────────────────────────────┐
│  63bit ────────── 17bit  0  │
│  [   Seq (47bit)  ][Idx(17)]│
│                             │
│  Idx : Session 배열 인덱스  │
│         (최대 7,000개)      │
│  Seq : 재접속 시 고유성 보장│
│         (ABA 문제 방지)     │
└─────────────────────────────┘
```

**하단 — 비동기 흐름 박스**:
```
AcceptEx 등록 → IOCP 완료 → completeAcceptEx → onAccept 콜백
RecvEx 등록   → IOCP 완료 → completeRecv    → onRecv   콜백
SendEx 등록   → IOCP 완료 → completeSend    → onSend   콜백
세션 종료     → IOcnt==0  → completeRelease → onRelease 콜백
```

하단 비고:
> CONFIG_ZERO_COPY = 0 적용: 수신 버퍼를 WSARecv에 직접 넘기지 않고
> RingBuffer로 복사 후 처리 (안정성 우선)

---

## 슬라이드 5 — Contents Thread 설계 (★ 핵심 슬라이드)

**제목**: 단일 Contents Thread — Lock-Free 게임 로직

**※ 이 슬라이드가 포트폴리오의 핵심. 강조 표현 사용.**

**왼쪽 — 일반적인 설계 (❌ 표시)**:
```
[ 일반적인 멀티스레드 설계 ]

Worker Thread 1 ──┐
Worker Thread 2 ──┼──▶ PlayerMap (Lock 필요)
Worker Thread 3 ──┘         │
                             ▼
                      Sector (Lock 필요)

→ 락 경쟁 → 성능 저하
→ 데드락 위험
```

**오른쪽 — 이 프로젝트 설계 (✅ 표시, 강조)**:
```
[ 이 프로젝트의 설계 ]

Worker Thread ──▶ ContentsQ ──▶ Contents Thread (1개)
  (IOCP 완료)    (락 1개)            │
                              ├── PlayerMap (락 없음)
                              ├── SectorMap (락 없음)
                              └── Redis 조회

→ 게임 로직에 락 Zero
→ ContentsQ 락 하나만 존재
→ 처리 순서 보장
```

**하단 — 내부 메시지 통합 방식 설명**:
```
onAccept  → CHAT_PLAYER_ALLOC  패킷 생성 → ContentsQ push
onRecv    → 수신 패킷 그대로              → ContentsQ push
onRelease → CHAT_PLAYER_DELETE 패킷 생성 → ContentsQ push

Contents Thread:
  WaitForSingleObject(hEchoEvent) → popContentsQ → packetProc
```

> **설계 의도**: 모든 게임 로직을 단일 스레드로 직렬화해 락 경쟁 제거.
> Worker Thread는 순수 네트워크 I/O만 담당.
> Player 생명주기(생성/삭제)까지 ContentsThread가 단독 관리.

---

## 슬라이드 6 — 패킷 흐름

**제목**: 패킷 처리 흐름 — CS_LOGIN → CS_AUTH → CS_CHAT

세로 타임라인 방식으로 도식화:

```
클라이언트                    ChattingServer                    Redis
    │                               │                              │
    │── CS_LOGIN(AccountNo) ───────▶│                              │
    │                               │── HGET session:{accountNo}  │
    │                               │   sessionToken / fixedKey ─▶│
    │                               │◀─ fixedKey, sessionToken ───│
    │                               │   SeqNumber = rand()        │
    │◀─ SC_LOGIN(Result, SeqNum) ───│                              │
    │                               │                              │
    │── CS_AUTH(SeqNum,             │                              │
    │           Nickname,           │                              │
    │           TokenKey) ─────────▶│                              │
    │                               │  TokenKey 비교 검증          │
    │                               │  SectorX/Y = rand() % 50    │
    │                               │  Sector에 Player 등록        │
    │◀─ SC_AUTH(SectorX, SectorY) ──│                              │
    │                               │                              │
    │── CS_CHAT(SeqNum, Len, Msg) ─▶│                              │
    │                               │  주변 9섹터 플레이어 탐색    │
    │                               │  각 플레이어에게 sendPost    │
    │◀─ SC_CHAT(Nickname, Len, Msg)─│ (브로드캐스트)               │
```

**오른쪽 — 패킷 구조 요약 박스**:
```
CS_LOGIN  { __int16 Type, __int32 SeqNumber(0xfdfdfdfd), __int64 AccountNo }
SC_LOGIN  { __int16 Type, __int8 Result, __int32 SeqNumber }
CS_AUTH   { __int16 Type, __int32 SeqNumber,
            wchar_t Nickname[20], char TokenKey[20] }
SC_AUTH   { __int16 Type, __int8 SectorX, __int8 SectorY }
CS_CHAT   { __int16 Type, __int32 SeqNumber,
            __int16 Len, wchar_t Message[Len] }
SC_CHAT   { __int16 Type, wchar_t Nickname[20],
            __int16 Len, wchar_t Message[Len] }
```

---

## 슬라이드 7 — 보안 구조

**제목**: 다층 보안 설계

3개의 보안 레이어를 박스로 나란히 배치:

```
┌──────────────────────┐  ┌──────────────────────┐  ┌──────────────────────┐
│   Layer 1            │  │   Layer 2             │  │   Layer 3            │
│   패킷 암호화        │  │   SeqNumber 검증      │  │   TokenKey 인증      │
│                      │  │                       │  │                      │
│ FixedKey (FK)        │  │ 초기값: 0xfdfdfdfd   │  │ LoginServer가 발급   │
│ Redis에서 취득       │  │ 매 패킷마다 +1 증가  │  │ Redis에 저장         │
│                      │  │                       │  │ CS_AUTH 시 비교      │
│ RandKey (RK)         │  │ 불일치 시             │  │                      │
│ 패킷마다 새 랜덤값   │  │ → 즉시 강제 종료     │  │ 불일치 시            │
│                      │  │                       │  │ → 즉시 강제 종료    │
│ 암호화: XOR(FK, RK)  │  │ 재전송 공격 방어      │  │                      │
│ Header에 RK 포함     │  │ 패킷 순서 보장        │  │ 세션 하이재킹 방어   │
└──────────────────────┘  └──────────────────────┘  └──────────────────────┘
```

하단 추가 방어 로직:
```
범위 검증   : SectorX/Y 범위(0~49) 초과 → 강제 종료
중복 이동   : 같은 섹터로 이동 시도 → 공격으로 판단, 강제 종료
메시지 길이 : CONFIG_MSG_MAX_LEN(800자) 초과 또는 0 이하 → 강제 종료
미인증 채팅 : bAuth == false 상태에서 CS_CHAT → 강제 종료
```

---

## 슬라이드 8 — 섹터 시스템

**제목**: 50×50 섹터 기반 범위 브로드캐스트

**왼쪽 — 섹터 그리드 시각화**:
```
50×50 섹터 그리드 (총 2,500개)
┌─────────────────────────────┐
│  ···  ···  ···  ···  ···   │
│  ···  [■] [■] [■] ···    │  ← 주변 8개 섹터
│  ···  [■] [★] [■] ···    │  ← ★ = 발화자 위치
│  ···  [■] [■] [■] ···    │
│  ···  ···  ···  ···  ···   │
└─────────────────────────────┘
Sector = { unordered_map<sessionID, Player*> }
```

**오른쪽 — 브로드캐스트 로직 설명**:
```
CS_CHAT 수신 시:
  1. 발화자 섹터(x, y) 확인
  2. dx[] = {-1,-1,-1, 0,0,0, +1,+1,+1}
     dy[] = {-1, 0,+1,-1,0,1, -1, 0,+1}
  3. 9개 인접 섹터 순회 (경계 클램핑 포함)
  4. 각 섹터의 모든 Player에게 SC_CHAT 전송

Send TPS가 Recv TPS의 약 3배인 이유:
  → 1명의 채팅 = 주변 N명에게 SC_CHAT 전송
  → Recv 16만 → Send 50만 (평균 팬아웃 ≈ 3.1배)
```

하단 비고:
> 섹터 이동 시 데드락 방지: 섹터 선형 순서(before < after)로 락 순서 고정
> (현재 단일 Contents Thread이므로 실제 락은 비활성화 상태,
>  향후 멀티 Contents Thread 확장 시를 대비한 설계)

---

## 슬라이드 9 — 실측 성능 지표

**제목**: 실측 성능 — 단일 Worker Thread 기준

4개 대형 수치 강조 박스로 배치:

```
┌────────────────┐  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐
│   Recv TPS     │  │   Send TPS     │  │   CPU 사용률   │  │  ContentsQ     │
│                │  │                │  │                │  │                │
│  160,000 /s   │  │  500,000 /s   │  │     46%        │  │  최대 14만     │
│                │  │                │  │                │  │  메시지 버퍼   │
│  Worker 1개로  │  │  팬아웃 ≈3.1x │  │  Worker 1개    │  │  Contents가    │
│  처리          │  │  섹터 브로드캐 │  │  + Contents 1  │  │  병목 지점     │
└────────────────┘  └────────────────┘  └────────────────┘  └────────────────┘
```

하단 분석 텍스트:
```
병목 분석:
  ContentsQ에 14만 메시지가 쌓이는 것은 그만큼 수신 처리량이 크다는 증거
  → Worker Thread(IOCP)가 게임 로직보다 훨씬 빠르게 패킷을 수신함을 증명
  → 개선 방향: Contents Thread 멀티화 (섹터 기반 샤딩) 또는 Lock-Free Queue 도입

모니터링 항목 (1초 주기 콘솔 실시간 출력):
  Accept TPS / Recv TPS / Send TPS / Contents TPS / 누적 Accept 수
  패킷별 TPS: CS_LOGIN, SC_LOGIN, CS_AUTH, SC_AUTH, CS_MOVE, CS_CHAT, SC_CHAT
  세션 수 / 유저 수 / ContentsQ 크기 / 강제 종료 수
```

---

## 슬라이드 10 — 핵심 기술 도전

**제목**: 구현 과정에서의 핵심 기술 도전

4개 도전 카드 형식으로 배치:

```
┌──────────────────────────────┐  ┌──────────────────────────────┐
│  🔑 세션 고유성 문제          │  │  🔒 Lock 최소화              │
│                              │  │                              │
│  재접속 시 같은 배열 인덱스   │  │  IOCP Worker ↔ ContentsQ    │
│  사용 → ABA 문제 발생        │  │  사이에 락 단 1개만 존재     │
│                              │  │                              │
│  해결: SeqAndIdx             │  │  Player / Sector 조작은      │
│  Idx(17bit) + Seq(47bit)    │  │  Contents Thread 독점으로    │
│  64비트 원자적 비교로 해결   │  │  락 없이 동작               │
└──────────────────────────────┘  └──────────────────────────────┘

┌──────────────────────────────┐  ┌──────────────────────────────┐
│  📦 내부 메시지 통합         │  │  🌐 Redis 비동기 처리        │
│                              │  │                              │
│  Accept/Release 이벤트를     │  │  cpp_redis::client를        │
│  외부 패킷과 동일한 Message  │  │  thread_local로 선언         │
│  포맷으로 래핑               │  │  (Contents Thread 전용)      │
│                              │  │                              │
│  CHAT_PLAYER_ALLOC (100)    │  │  Promise/Future 패턴으로     │
│  CHAT_PLAYER_DELETE (101)   │  │  동기식 Redis 조회 구현      │
│  → ContentsQ 단일 진입점   │  │  sync_commit() 활용          │
└──────────────────────────────┘  └──────────────────────────────┘
```

---

## 슬라이드 11 — 정리 및 향후 계획

**제목**: 정리 및 향후 개선 방향

**왼쪽 — 구현 완료 목록**:
```
✅ 구현 완료
  - AcceptEx 기반 비동기 IOCP 서버
  - SeqAndIdx 64비트 세션 관리 (ABA 방지)
  - Single Contents Thread 게임 로직 (Lock-Free)
  - Redis 세션 인증 (sessionToken + fixedKey)
  - 패킷 암호화 (FixedKey XOR RandKey)
  - SeqNumber 기반 패킷 무결성 검증
  - 50x50 섹터 범위 브로드캐스트
  - 실시간 TPS 모니터링 (패킷 종류별)
  - 지연 없는 즉시 세션 해제 (DeferredQ 제거, Lock-Free 완성)
  - CrushDump (크래시 덤프 자동 수집)
  - ZeroCopy 옵션 설계
```

**오른쪽 — 향후 개선 방향**:
```
🔧 개선 계획
  - Contents Thread 멀티화
    → 섹터 그룹별 샤딩으로 병목 해소
    → ContentsQ 14만 버퍼 문제 해결

  - Lock-Free Queue 도입
    → mContentsQ를 CLockFreeQueue로 교체
    → Worker ↔ Contents 간 락 완전 제거

  - 종료 처리 구현
    → 현재 무한루프, 정상 종료 미구현

  - 타임아웃 처리
    → lastTime 필드 활용한 유휴 세션 정리
```

---

## 부록 — 작성 시 주의사항

1. **코드 스니펫을 슬라이드에 직접 넣지 말 것** — 도식/다이어그램으로만 표현
2. **ContentsQ 14만 = 병목이지만 부정적으로 표현 금지**
   → "처리량이 그만큼 크다는 증거"로 긍정적 표현
3. **Worker Thread가 1개인 것 강조**
   → "단일 워커로 16만 Recv TPS"가 핵심 포인트
4. **SeqAndIdx 비트 레이아웃은 슬라이드 4에서 반드시 도식화**
5. **섹터 시스템에서 9방향 탐색을 격자 그림으로 시각화**
6. **전체 설계 철학 = "분리와 단순화"**
   → IOCP(I/O 전담) / ContentsThread(로직 전담) 역할 분리를 일관되게 강조
