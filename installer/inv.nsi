; ============================================================================
; inv.nsi - Infinity inv (command line version control) installer
;
;   makensis /V2 inv.nsi
;   makensis /V2 /DAPP_VERSION=0.1.0 /DSRC_DIR=C:\...\dist /DOUT_DIR=C:\...\dist inv.nsi
;   makensis /V2 /DDRYRUN /DOUT_DIR=%TEMP%\inv-dryrun inv.nsi   (syntax check only)
;
; Payload layout expected under SRC_DIR (default: <repo>\dist):
;   inv.exe
;   vanadium.dll
;   java\vanadium-report.jar
;   java\runtime\**            (jlink runtime, bin\java.exe required)
;   docs\**
;
; The installer is a plain CLI product: no GUI pages of its own and no
; MessageBox calls anywhere in this script. Silent install/uninstall:
; setup.exe /S and uninstall.exe /S. User data (%APPDATA%\inv, archives) is
; never touched.
; ============================================================================

Unicode true
ManifestDPIAware true
RequestExecutionLevel admin
SetCompressor /SOLID zlib

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "WinMessages.nsh"
!include "x64.nsh"

; ---- version ---------------------------------------------------------------
; Precedence: -DAPP_VERSION=...  >  <repo>\VERSION (single ASCII line)  >  0.1.0
!ifndef APP_VERSION
  !if /FILEEXISTS "${__FILEDIR__}\..\VERSION"
    !searchparse /file "${__FILEDIR__}\..\VERSION" "" APP_VERSION
  !endif
!endif
!ifndef APP_VERSION
  !define APP_VERSION "0.1.0"
!endif

; APP_VERSION must be MAJOR.MINOR.PATCH. A trailing dot lets the compiler
; split all three fields (the last field would otherwise swallow the tail);
; /noerrors keeps a malformed version from aborting with a cryptic searchparse
; message, so the explicit checks below can name the offending value.
!searchparse /noerrors "${APP_VERSION}." "" VER_MAJOR "." VER_MINOR "." VER_PATCH "." VER_REST
!if "${VER_MAJOR}" == ""
  !error "inv installer: APP_VERSION must be MAJOR.MINOR.PATCH (from -DAPP_VERSION or VERSION), got '${APP_VERSION}'"
!endif
!if "${VER_MINOR}" == ""
  !error "inv installer: APP_VERSION must be MAJOR.MINOR.PATCH (from -DAPP_VERSION or VERSION), got '${APP_VERSION}'"
!endif
!if "${VER_PATCH}" == ""
  !error "inv installer: APP_VERSION must be MAJOR.MINOR.PATCH (from -DAPP_VERSION or VERSION), got '${APP_VERSION}'"
!endif

; ---- directories -----------------------------------------------------------
!ifndef SRC_DIR
  !define SRC_DIR "${__FILEDIR__}\..\dist"
!endif
!ifndef OUT_DIR
  !define OUT_DIR "${__FILEDIR__}\..\dist"
!endif

!define APP_NAME "Infinity inv"
!define UNINST_KEY "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\inv"
!define APP_PATH_KEY "SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\inv.exe"
!define ENV_KEY "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"

; ---- payload macros --------------------------------------------------------
; DRYRUN compiles every page/section/registry/PATH instruction but embeds no
; payload, so the script can be syntax checked before dist/ exists.
!macro BundleFile SRC DEST
  !ifndef DRYRUN
    !if /FILEEXISTS "${SRC}"
      File "/oname=${DEST}" "${SRC}"
    !else
      !error "inv installer: missing payload file ${SRC}"
    !endif
  !endif
!macroend

!macro BundleTree SRC
  !ifndef DRYRUN
    File /r "${SRC}\*.*"
  !endif
!macroend

; ---- installer metadata ----------------------------------------------------
Name "${APP_NAME} ${APP_VERSION}"
OutFile "${OUT_DIR}\inv-setup-${APP_VERSION}.exe"
InstallDir "$PROGRAMFILES64\Infinity\inv"
InstallDirRegKey HKLM "${UNINST_KEY}" "InstallLocation"
BrandingText "${APP_NAME} ${APP_VERSION}"
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}.0"
VIAddVersionKey "ProductName" "${APP_NAME}"
VIAddVersionKey "CompanyName" "Infinity"
VIAddVersionKey "FileDescription" "${APP_NAME} installer"
VIAddVersionKey "FileVersion" "${APP_VERSION}"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
VIAddVersionKey "LegalCopyright" "Copyright (c) Infinity"
VIAddVersionKey "OriginalFilename" "inv-setup-${APP_VERSION}.exe"

; ---- pages (MUI2) ----------------------------------------------------------
!define MUI_WELCOMEPAGE_TITLE_3LINES
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${__FILEDIR__}\license.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

; ---- languages: English default, zh-cn, tr-cn; am-en uses English ----------
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "TradChinese"

LangString INV_SEC_CORE     ${LANG_ENGLISH}     "inv (required)"
LangString INV_SEC_CORE     ${LANG_SIMPCHINESE} "inv（必需）"
LangString INV_SEC_CORE     ${LANG_TRADCHINESE} "inv（必要）"

LangString INV_SEC_PATH     ${LANG_ENGLISH}     "Add inv to the system PATH"
LangString INV_SEC_PATH     ${LANG_SIMPCHINESE} "将 inv 加入系统 PATH"
LangString INV_SEC_PATH     ${LANG_TRADCHINESE} "將 inv 加入系統 PATH"

LangString INV_SEC_SHORTCUT ${LANG_ENGLISH}     "Command Prompt shortcut"
LangString INV_SEC_SHORTCUT ${LANG_SIMPCHINESE} "命令提示符快捷方式"
LangString INV_SEC_SHORTCUT ${LANG_TRADCHINESE} "命令提示字元捷徑"

LangString INV_DESC_CORE     ${LANG_ENGLISH}     "inv.exe, vanadium.dll, the Java report sidecar with its runtime, and documentation."
LangString INV_DESC_CORE     ${LANG_SIMPCHINESE} "inv.exe、vanadium.dll、Java 报告边车及其运行时，以及文档。"
LangString INV_DESC_CORE     ${LANG_TRADCHINESE} "inv.exe、vanadium.dll、Java 報告邊車及其執行環境，以及文件。"

LangString INV_DESC_PATH     ${LANG_ENGLISH}     "Append the install directory to the machine PATH so 'inv' works in any shell."
LangString INV_DESC_PATH     ${LANG_SIMPCHINESE} "将安装目录追加到系统 PATH，使任意终端都可直接运行 inv。"
LangString INV_DESC_PATH     ${LANG_TRADCHINESE} "將安裝目錄附加到系統 PATH，讓任何終端都能直接執行 inv。"

LangString INV_DESC_SHORTCUT ${LANG_ENGLISH}     "Create one Start Menu shortcut that opens a Command Prompt in the install directory."
LangString INV_DESC_SHORTCUT ${LANG_SIMPCHINESE} "在开始菜单创建一个在安装目录打开命令提示符的快捷方式。"
LangString INV_DESC_SHORTCUT ${LANG_TRADCHINESE} "在開始功能表建立一個於安裝目錄開啟命令提示字元的捷徑。"

LangString INV_SHORTCUT_NAME ${LANG_ENGLISH}     "inv Command Prompt"
LangString INV_SHORTCUT_NAME ${LANG_SIMPCHINESE} "inv 命令提示符"
LangString INV_SHORTCUT_NAME ${LANG_TRADCHINESE} "inv 命令提示字元"

; ---- sections --------------------------------------------------------------
Section "$(INV_SEC_CORE)" SEC_CORE
  SectionIn RO
  SetOutPath "$INSTDIR"
  !insertmacro BundleFile "${SRC_DIR}\inv.exe" "inv.exe"
  !insertmacro BundleFile "${SRC_DIR}\vanadium.dll" "vanadium.dll"

  SetOutPath "$INSTDIR\java"
  !insertmacro BundleFile "${SRC_DIR}\java\vanadium-report.jar" "vanadium-report.jar"
  SetOutPath "$INSTDIR\java\runtime"
  !insertmacro BundleTree "${SRC_DIR}\java\runtime"

  SetOutPath "$INSTDIR\docs"
  !insertmacro BundleTree "${SRC_DIR}\docs"

  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; App Paths (Run dialog / CreateProcess lookup)
  WriteRegStr HKLM "${APP_PATH_KEY}" "" "$INSTDIR\inv.exe"
  WriteRegStr HKLM "${APP_PATH_KEY}" "Path" "$INSTDIR"

  ; Add/Remove Programs entry
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "Infinity"
  WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\inv.exe"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" $0
SectionEnd

Section "$(INV_SEC_PATH)" SEC_PATH
  Call AddToSystemPath
SectionEnd

Section "$(INV_SEC_SHORTCUT)" SEC_SHORTCUT
  CreateDirectory "$SMPROGRAMS\Infinity"
  CreateShortCut "$SMPROGRAMS\Infinity\$(INV_SHORTCUT_NAME).lnk" "$SYSDIR\cmd.exe" '/k cd /d "$INSTDIR"' "$SYSDIR\cmd.exe" 0
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_CORE} "$(INV_DESC_CORE)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_PATH} "$(INV_DESC_PATH)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_SHORTCUT} "$(INV_DESC_SHORTCUT)"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ---- PATH helpers ----------------------------------------------------------
; Append $INSTDIR to the machine PATH unless an equivalent entry already exists.
; Existing entries, order, and REG_EXPAND_SZ type are preserved.
Function AddToSystemPath
  ReadRegStr $0 HKLM "${ENV_KEY}" "Path"
  StrCpy $1 0            ; 1 = already present
  StrCpy $2 $0           ; remaining text
  ${Do}
    ${If} $2 == ""
      ${Break}
    ${EndIf}
    StrCpy $3 ""         ; current entry
    ${Do}
      StrCpy $4 $2 1
      ${If} $4 == ""
      ${OrIf} $4 == ";"
        ${Break}
      ${EndIf}
      StrCpy $3 "$3$4"
      StrCpy $2 $2 "" 1
    ${Loop}
    ${If} $4 == ";"
      StrCpy $2 $2 "" 1
    ${EndIf}
    StrCpy $5 $3 1 -1
    ${If} $5 == "\"
      StrCpy $3 $3 -1
    ${EndIf}
    ${If} $3 == "$INSTDIR"
      StrCpy $1 1
      ${Break}
    ${EndIf}
  ${Loop}
  ${If} $1 == 0
    ${If} $0 == ""
      StrCpy $0 "$INSTDIR"
    ${Else}
      StrCpy $5 $0 1 -1
      ${If} $5 == ";"
        StrCpy $0 "$0$INSTDIR"
      ${Else}
        StrCpy $0 "$0;$INSTDIR"
      ${EndIf}
    ${EndIf}
    WriteRegExpandStr HKLM "${ENV_KEY}" "Path" "$0"
    System::Call 'user32::SendMessageTimeoutW(i ${HWND_BROADCAST}, i ${WM_SETTINGCHANGE}, i 0, w "Environment", i 0, i 5000, *i .r0)'
  ${EndIf}
FunctionEnd

; Rebuild PATH without $INSTDIR (case-insensitive, trailing backslash tolerated).
Function un.RemoveFromSystemPath
  ReadRegStr $0 HKLM "${ENV_KEY}" "Path"
  StrCpy $1 ""     ; rebuilt PATH, entries kept verbatim
  StrCpy $2 $0     ; remaining text
  StrCpy $6 0      ; 1 = the install directory was actually removed
  ${Do}
    ${If} $2 == ""
      ${Break}
    ${EndIf}
    StrCpy $3 ""
    ${Do}
      StrCpy $4 $2 1
      ${If} $4 == ""
      ${OrIf} $4 == ";"
        ${Break}
      ${EndIf}
      StrCpy $3 "$3$4"
      StrCpy $2 $2 "" 1
    ${Loop}
    ${If} $4 == ";"
      StrCpy $2 $2 "" 1
    ${EndIf}
    StrCpy $5 $3 1 -1
    ${If} $5 == "\"
      StrCpy $5 $3 -1     ; comparison copy without trailing backslash
    ${Else}
      StrCpy $5 $3
    ${EndIf}
    ${If} $5 == "$INSTDIR"
      StrCpy $6 1
    ${Else}
      ${If} $3 != ""
        ${If} $1 == ""
          StrCpy $1 "$3"
        ${Else}
          StrCpy $1 "$1;$3"
        ${EndIf}
      ${EndIf}
    ${EndIf}
  ${Loop}
  ${If} $6 == 1
    WriteRegExpandStr HKLM "${ENV_KEY}" "Path" "$1"
    System::Call 'user32::SendMessageTimeoutW(i ${HWND_BROADCAST}, i ${WM_SETTINGCHANGE}, i 0, w "Environment", i 0, i 5000, *i .r0)'
  ${EndIf}
FunctionEnd

; ---- uninstall -------------------------------------------------------------
Section "Uninstall"
  Delete "$INSTDIR\inv.exe"
  Delete "$INSTDIR\vanadium.dll"
  Delete "$INSTDIR\java\vanadium-report.jar"
  RMDir /r "$INSTDIR\java\runtime"
  RMDir /r "$INSTDIR\docs"
  RMDir "$INSTDIR\java"
  Delete "$INSTDIR\uninstall.exe"

  ; language independent: remove whichever shortcut name was created
  Delete "$SMPROGRAMS\Infinity\*.lnk"
  RMDir "$SMPROGRAMS\Infinity"

  DeleteRegKey HKLM "${APP_PATH_KEY}"
  DeleteRegKey HKLM "${UNINST_KEY}"

  Call un.RemoveFromSystemPath

  RMDir "$INSTDIR"
  RMDir "$PROGRAMFILES64\Infinity"
SectionEnd

; ---- init ------------------------------------------------------------------
Function .onInit
  SetRegView 64
  SetShellVarContext all
  ${IfNot} ${RunningX64}
    SetErrorLevel 2
    Quit
  ${EndIf}
  ${IfNot} ${Silent}
    !insertmacro MUI_LANGDLL_DISPLAY
  ${EndIf}
FunctionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext all
FunctionEnd