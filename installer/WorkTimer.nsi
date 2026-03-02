Unicode True

!define APP_NAME      "WorkTimer"
!define APP_VERSION   "0.1.0"
!define APP_PUBLISHER "WorkTimer"
!define APP_EXE       "WorkTimer.exe"
!define INSTALL_DIR   "$PROGRAMFILES64\${APP_NAME}"
!define REG_KEY       "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "WorkTimer_Setup_v${APP_VERSION}.exe"
InstallDir "${INSTALL_DIR}"
InstallDirRegKey HKLM "${REG_KEY}" "InstallLocation"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "LogicLib.nsh"

!define MUI_ABORTWARNING
!define MUI_ICON   "..\resources\app.ico"
!define MUI_UNICON "..\resources\app.ico"

; Installer pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\resources\LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

; Finish page - offer to launch app
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Launch WorkTimer"
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; --- Main install section ---
Section "WorkTimer" SEC01
    SectionIn RO
    SetOutPath "$INSTDIR"
    SetOverwrite on

    File "..\build\bin\Release\${APP_EXE}"
    File "..\resources\app.ico"

    ; Start menu shortcut
    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" \
        "$INSTDIR\${APP_EXE}" "" "$INSTDIR\app.ico" 0
    CreateShortcut "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk" \
        "$INSTDIR\Uninstall.exe"

    ; Desktop shortcut
    CreateShortcut "$DESKTOP\${APP_NAME}.lnk" \
        "$INSTDIR\${APP_EXE}" "" "$INSTDIR\app.ico" 0

    ; Registry - Add/Remove Programs
    WriteRegStr   HKLM "${REG_KEY}" "DisplayName"      "${APP_NAME}"
    WriteRegStr   HKLM "${REG_KEY}" "DisplayVersion"   "${APP_VERSION}"
    WriteRegStr   HKLM "${REG_KEY}" "Publisher"        "${APP_PUBLISHER}"
    WriteRegStr   HKLM "${REG_KEY}" "DisplayIcon"      "$INSTDIR\app.ico"
    WriteRegStr   HKLM "${REG_KEY}" "InstallLocation"  "$INSTDIR"
    WriteRegStr   HKLM "${REG_KEY}" "UninstallString"  "$INSTDIR\Uninstall.exe"
    WriteRegDWORD HKLM "${REG_KEY}" "NoModify"         1
    WriteRegDWORD HKLM "${REG_KEY}" "NoRepair"         1

    WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

; --- Optional: run on Windows startup ---
Section /o "Run on Windows startup" SEC_STARTUP
    WriteRegStr HKCU \
        "Software\Microsoft\Windows\CurrentVersion\Run" \
        "${APP_NAME}" "$INSTDIR\${APP_EXE}"
SectionEnd

; --- Uninstall section ---
Section "Uninstall"
    ; Stop running instance
    ExecWait 'taskkill /F /IM "${APP_EXE}"'

    Delete "$INSTDIR\${APP_EXE}"
    Delete "$INSTDIR\app.ico"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir  "$INSTDIR"

    Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk"
    RMDir  "$SMPROGRAMS\${APP_NAME}"
    Delete "$DESKTOP\${APP_NAME}.lnk"

    DeleteRegKey HKLM "${REG_KEY}"
    DeleteRegValue HKCU \
        "Software\Microsoft\Windows\CurrentVersion\Run" "${APP_NAME}"

    ; User data is kept. To remove, uncomment:
    ; RMDir /r "$APPDATA\WorkTimer"
SectionEnd
