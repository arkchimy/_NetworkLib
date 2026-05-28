# IOCP 네트워크 라이브러리 포트폴리오

C++ IOCP 기반 네트워크 라이브러리를 직접 구현하고,  
이를 기반으로 EchoServer · ChattingServer · LoginServer를 단계별로 제작한 포트폴리오입니다.

---

## 핵심 성과

- 동접 **1,000명** 기준 Content TPS **32만 ~ 45만** 달성
- Interlocked CAS를 활용한 **Lock-free Send 동기화** 구현
- Redis 토큰 기반 **Stateless 인증 구조** 설계  
  → ChatServer가 LoginServer 없이 독립적으로 인증 처리

---

## 프로젝트 구성

| 컴포넌트 | 설명 |
|---|---|
| **NetworkLib** | IOCP 네트워크 라이브러리 (Interlocked CAS Send 동기화) |
| **EchoServer** | NetworkLib 기반 에코 서버 |
| **Echo_DummyClient** | 에코 서버 부하 테스트 클라이언트 |
| **ChattingServer** | 로그인 / 인증 / 이동 / 채팅 전 흐름 처리 |
| **ChatDummyClient** | SSL 인증 포함 전 흐름 자동화 클라이언트 |
| **SSL_LoginServer** | MySQL 계정 조회 + Redis 토큰 발급 로그인 서버 |

---

## 아키텍처

```
[ChatDummyClient]
       │  SSL
       ▼
[SSL_LoginServer] ──── MySQL (계정 인증)
       │                Redis (토큰 발급)
       │ JWT Token
       ▼
[ChattingServer] ──── Redis (토큰 검증)
  ├─ login / auth
  ├─ move
  └─ chat broadcast
```

- LoginServer에서 발급한 토큰을 Redis에 저장  
- ChatServer는 토큰만 검증해 LoginServer 의존성 없이 독립 운용 가능

---

## NetworkLib 구조

```
[IOCP Worker Thread]
   onRecv()  →  Content Queue  →  [Content Thread]
   onSend()  (Interlocked CAS 동기화)
   onRelease()
```

**Send 동기화 핵심 — Interlocked CAS**

```cpp
// SendPost 진입 시 이미 Send 중인지 CAS로 확인
if (InterlockedCompareExchange(&_sendFlag, 1, 0) == 0)
    WSASend(...);  // 직접 전송
else
    _sendQ.push(packet);  // 큐에 적재, onSend에서 재시도
```

Worker Thread가 직접 WSASend를 호출하되,  
동시 전송이 발생하지 않도록 CAS로 단일 진입을 보장합니다.

---

## 성능 측정

| 항목 | 수치 |
|---|---|
| 동시 접속자 | 1,000명 |
| Content TPS | 320,000 ~ 450,000 |
| 측정 도구 | DummyClient 1,000 인스턴스 동시 구동 |

---

## 기술 스택

- **언어**: C++17
- **네트워크**: WinSock2, IOCP
- **보안**: OpenSSL (TLS)
- **DB**: MySQL, Redis
- **동기화**: Interlocked CAS, CRITICAL_SECTION

---

## 빌드 환경

- Windows 10
- Visual Studio 2022
- OpenSSL 3.x
- MySQL 8.x / Redis 7.x
