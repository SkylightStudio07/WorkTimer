# WorkTimer - 작업 시간 타이머

현재 실행 중인 앱을 감지하여 자동으로 작업 시간을 기록하는 데스크탑 타이머입니다.

---

## 📋 빌드 환경

| 항목 | 버전 |
|------|------|
| C++ 표준 | C++17 |
| GUI 프레임워크 | wxWidgets 3.2.x |
| 컴파일러 | MSVC (Visual Studio 2019/2022) |
| 빌드 시스템 | CMake 3.16+ |
| 인스톨러 | NSIS 3.x |

---

## 🚀 빌드 방법

### 1. 사전 준비

**wxWidgets 설치**
```
https://www.wxwidgets.org/downloads/
```
- Windows Installer 또는 소스 빌드
- 권장 설치 경로: `C:\wxWidgets-3.2.4`
- 정적 링크 권장: CMake 빌드 시 `BUILD_SHARED_LIBS=OFF`

**NSIS 설치** (인스톨러 생성용)
```
https://nsis.sourceforge.io
```

**CMake 설치**
```
https://cmake.org/download/
```

### 2. 빌드 실행

```bat
build.bat
```

또는 수동으로:
```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DwxWidgets_ROOT_DIR="C:\wxWidgets-3.2.4"
cmake --build build --config Release
```

### 3. 인스톨러 생성

```bat
cd installer
"C:\Program Files (x86)\NSIS\makensis.exe" WorkTimer.nsi
```

---

## 📁 프로젝트 구조

```
WorkTimer/
├── src/
│   └── main.cpp          ← 전체 소스 코드
├── resources/
│   ├── app.rc            ← 아이콘 & 버전 정보
│   ├── app.ico           ← 앱 아이콘 (직접 제작/추가 필요)
│   └── LICENSE.txt       ← 라이선스 (NSIS용)
├── installer/
│   └── WorkTimer.nsi     ← NSIS 인스톨러 스크립트
├── CMakeLists.txt
├── build.bat             ← 원클릭 빌드 스크립트
└── README.md
```

---

## ⚙️ 주요 기능

- **자동 앱 감지**: 등록된 앱 키워드가 포그라운드 창에 포함되면 자동 타이머 시작/정지
- **수동 제어**: 시작/정지/리셋 버튼
- **오늘 총 시간**: 앱 재시작 후에도 누적 유지
- **세션 기록**: 앱별 작업 시간 저장 (설정 창에서 확인)
- **색상 알림**: 설정한 간격마다 색상 변경 + 벨 알림
- **항상 위**: 화면 우측 하단에 항상 표시
- **설정 저장**: `%APPDATA%\WorkTimer\work_timer.ini`

---

## 🔧 wxWidgets 정적 빌드 (권장)

DLL 없이 단일 .exe로 배포하려면:

```bat
:: wxWidgets 소스 폴더에서
mkdir build_static && cd build_static
cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DBUILD_SHARED_LIBS=OFF ^
    -DwxBUILD_SHARED=OFF
cmake --build . --config Release
```
