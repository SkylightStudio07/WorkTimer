; ─────────────────────────────────────────────────────────────
; WorkTimer NSIS 인스톨러 스크립트
; NSIS 3.x 필요: https://nsis.sourceforge.io
; ─────────────────────────────────────────────────────────────

Unicode True

; ── 기본 정보 ─────────────────────────────────────────────────
!define APP_NAME      "WorkTimer"
!define APP_VERSION   "1.0.0"
!define APP_PUBLISHER "WorkTimer"
!define APP_EXE       "WorkTimer.exe"
!define INSTALL_DIR   "$PROGRAMFILES64\${APP_NAME}"
!define REG_KEY       "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"

; ── 출력 설정 ─────────────────────────────────────────────────
Name "${APP_NAME} ${APP_VERSION}"
OutFile "WorkTimer_Setup_v${APP_VERSION}.exe"
InstallDir "${INSTALL_DIR}"
InstallDirRegKey HKLM "${REG_KEY}" "InstallLocation"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

; ── 최신 UI ───────────────────────────────────────────────────
!include "MUI2.nsh"
!include "LogicLib.nsh"

!define MUI_ABORTWARNING
!define MUI_ICON   "..\resources\app.ico"
!define MUI_UNICON "..\resources\app.ico"

; 설치 페이지
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\resources\LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; 제거 페이지
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Korean"

; ── 설치 섹션 ─────────────────────────────────────────────────
Section "MainSection" SEC01
    SetOutPath "$INSTDIR"
    SetOverwrite on

    ; 실행 파일 복사
    ; 빌드 후 경로에 맞게 수정하세요
    File "..\build\bin\Release\${APP_EXE}"

    ; 시작 메뉴 바로가기
    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" \
        "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0

    ; 바탕화면 바로가기
    CreateShortcut "$DESKTOP\${APP_NAME}.lnk" \
        "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0

    ; 레지스트리 등록 (프로그램 추가/제거)
    WriteRegStr   HKLM "${REG_KEY}" "DisplayName"      "${APP_NAME}"
    WriteRegStr   HKLM "${REG_KEY}" "DisplayVersion"   "${APP_VERSION}"
    WriteRegStr   HKLM "${REG_KEY}" "Publisher"        "${APP_PUBLISHER}"
    WriteRegStr   HKLM "${REG_KEY}" "InstallLocation"  "$INSTDIR"
    WriteRegStr   HKLM "${REG_KEY}" "UninstallString"  "$INSTDIR\Uninstall.exe"
    WriteRegDWORD HKLM "${REG_KEY}" "NoModify"         1
    WriteRegDWORD HKLM "${REG_KEY}" "NoRepair"         1

    ; 제거 프로그램 생성
    WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

; 시작프로그램 자동 실행 (선택)
Section /o "Windows 시작 시 자동 실행" SEC_STARTUP
    WriteRegStr HKCU \
        "Software\Microsoft\Windows\CurrentVersion\Run" \
        "${APP_NAME}" "$INSTDIR\${APP_EXE}"
SectionEnd

; ── 제거 섹션 ─────────────────────────────────────────────────
Section "Uninstall"
    Delete "$INSTDIR\${APP_EXE}"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir  "$INSTDIR"

    Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
    RMDir  "$SMPROGRAMS\${APP_NAME}"
    Delete "$DESKTOP\${APP_NAME}.lnk"

    DeleteRegKey HKLM "${REG_KEY}"
    DeleteRegValue HKCU \
        "Software\Microsoft\Windows\CurrentVersion\Run" "${APP_NAME}"

    ; 사용자 데이터는 보존 (삭제하려면 아래 주석 해제)
    ; RMDir /r "$APPDATA\${APP_NAME}"
SectionEnd
