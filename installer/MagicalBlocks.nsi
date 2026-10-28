; Magical Blocks - Windows setup (NSIS 3, Unicode). Built by tools/package-release.sh:
;   makensis -DVERSION=x.y.z -DSTAGE=<staged program folder> -DOUTFILE=<setup.exe> MagicalBlocks.nsi
; The setup does exactly two things: install the program + shaders folder, and install the user's ROM
; (pick file -> MagicalBlocks.exe --install-rom <file> --result-file <tmp>; exit 0/2/3/4). The ROM step
; also prepares the game: the game code is generated from that ROM on this PC (exit 4 = ROM saved, preparing failed).
; No ROM is ever bundled. Per-user install, no administrator rights needed.
Unicode true
Target amd64-unicode ; 64-bit setup: the game itself is x64-only, and wine64 runs it headless
ManifestDPIAware true
RequestExecutionLevel user
SetCompressor /SOLID lzma

!ifndef VERSION
  !error "pass -DVERSION=..."
!endif
!ifndef STAGE
  !error "pass -DSTAGE=<staged program folder>"
!endif
!ifndef OUTFILE
  !define OUTFILE "MagicalBlocks-${VERSION}-setup.exe"
!endif

!define APPNAME "Magical Blocks"
!define EXE "MagicalBlocks.exe"
!define SHORTCUT "Magical Blocks"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\MagicalBlocks"

Name "${APPNAME}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\MagicalBlocks"
InstallDirRegKey HKCU "Software\MagicalBlocks" "InstallDir"
BrandingText "${APPNAME} ${VERSION}"
!ifndef VERSION_NUM
  !define VERSION_NUM "${VERSION}"
!endif
VIProductVersion "${VERSION_NUM}.0"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "FileDescription" "${APPNAME} setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "GPL-3.0. No game data included."

!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

!ifdef ICON
  !define MUI_ICON "${ICON}"
  !define MUI_UNICON "${ICON}"
!endif
!define MUI_ABORTWARNING

Var RomDialog
Var RomPath
Var RomField
Var RomSkip
Var RomStatus
Var FinishText
Var RomBusy

!define MUI_WELCOMEPAGE_TITLE "Welcome to ${APPNAME}"
!define MUI_WELCOMEPAGE_TEXT "This will install the game and its shaders, and then ask for your ROM file.$\r$\n$\r$\nYou need your own ROM of Magical Tetris Challenge (USA, Nintendo 64). We do not provide it.$\r$\n$\r$\nClick Next to continue."
!insertmacro MUI_PAGE_WELCOME
!define MUI_PAGE_HEADER_TEXT "Before you install"
!define MUI_PAGE_HEADER_SUBTEXT "Please read this short notice."
!define MUI_LICENSEPAGE_TEXT_TOP "Please read this short notice."
!define MUI_LICENSEPAGE_BUTTON "&Next >"
!define MUI_LICENSEPAGE_TEXT_BOTTOM "Click Next to continue."
!insertmacro MUI_PAGE_LICENSE "notice.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
Page custom RomPageCreate RomPageLeave
!define MUI_FINISHPAGE_TITLE "All done!"
!define MUI_FINISHPAGE_TEXT "$FinishText"
!define MUI_FINISHPAGE_TEXT_LARGE
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Play now"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"


; ---------------------------------------------------------------- 1. program + shaders
Section "Game and shaders" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  ; A previous version's files are replaced; settings, saves and the stored ROM live in
  ; %LOCALAPPDATA%\MagicalBlocks and are not touched.
  RMDir /r "$INSTDIR\assets"
  RMDir /r "$INSTDIR\shaders"
  DetailPrint "Installing the game..."
  File "${STAGE}\${EXE}"
  File "${STAGE}\SDL2.dll"
  File "${STAGE}\dxcompiler.dll"
  File "${STAGE}\dxil.dll"
  File "${STAGE}\recompcontrollerdb.txt"
  File "${STAGE}\LICENSE.txt"
  File "${STAGE}\THIRD-PARTY-NOTICES.txt"
  File "${STAGE}\README.txt"
  File /r "${STAGE}\licenses"
  File /r "${STAGE}\assets"
  ; game-code generator kit (no game code inside): generator, compiler subset, symbol/config data
  RMDir /r "$INSTDIR\tools"
  RMDir /r "$INSTDIR\toolchain"
  RMDir /r "$INSTDIR\gamecode"
  File /r "${STAGE}\tools"
  File /r "${STAGE}\toolchain"
  File /r "${STAGE}\gamecode"
  DetailPrint "Installing the shaders..."
  SetOutPath "$INSTDIR\shaders"
  File /r "${STAGE}\shaders\*.*"
  SetOutPath "$INSTDIR"
  ; Extra shaders: .slangp/.slang files or a "shaders" folder placed next to this setup file are copied too.
  ${If} ${FileExists} "$EXEDIR\*.slangp"
    DetailPrint "Copying extra shader presets from $EXEDIR..."
    CopyFiles /SILENT "$EXEDIR\*.slangp" "$INSTDIR\shaders"
  ${EndIf}
  ${If} ${FileExists} "$EXEDIR\*.slang"
    CopyFiles /SILENT "$EXEDIR\*.slang" "$INSTDIR\shaders"
  ${EndIf}
  ${If} ${FileExists} "$EXEDIR\shaders\*.*"
  ${AndIf} "$EXEDIR" != "$INSTDIR"
    DetailPrint "Copying extra shaders from $EXEDIR\shaders..."
    CopyFiles /SILENT "$EXEDIR\shaders\*.*" "$INSTDIR\shaders"
  ${EndIf}

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateShortCut "$SMPROGRAMS\${SHORTCUT}.lnk" "$INSTDIR\${EXE}"
  CreateShortCut "$DESKTOP\${SHORTCUT}.lnk" "$INSTDIR\${EXE}"
  WriteRegStr HKCU "Software\MagicalBlocks" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "Unofficial fan project"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${EXE}"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" "$0"
SectionEnd

; ---------------------------------------------------------------- 2. the user's ROM
Function RomPageCreate
  !insertmacro MUI_HEADER_TEXT "Your ROM" "Pick your own ROM file of Magical Tetris Challenge (USA)."
  nsDialogs::Create 1018
  Pop $RomDialog
  ${If} $RomDialog == error
    Abort
  ${EndIf}
  ${NSD_CreateLabel} 0 0 100% 36u "Click Browse and choose your ROM file (it usually ends in .z64, .v64 or .n64).$\r$\nSetup checks that it is the right game and saves a copy for the game. The file you pick is not changed."
  Pop $0
  ${NSD_CreateText} 0 42u 76% 13u "$RomPath"
  Pop $RomField
  ${NSD_CreateBrowseButton} 78% 41u 22% 15u "&Browse..."
  Pop $0
  ${NSD_OnClick} $0 RomBrowse
  ${NSD_CreateCheckbox} 0 66u 100% 12u "&Skip for now - I will pick my ROM in the game later"
  Pop $RomSkip
  ${If} ${FileExists} "$LOCALAPPDATA\MagicalBlocks\mtc.n64.us.z64"
    ${NSD_CreateLabel} 0 84u 100% 24u "A ROM is already installed from an earlier setup. You can keep it: tick Skip and click Next."
    Pop $0
  ${EndIf}
  ${NSD_CreateLabel} 0 112u 100% 24u "You must supply your own legally dumped ROM. We do not provide it."
  Pop $0
  ${NSD_CreateLabel} 0 132u 100% 12u ""
  Pop $RomBusy
  GetDlgItem $0 $HWNDPARENT 1
  SendMessage $0 ${WM_SETTEXT} 0 "STR:&Next >"
  nsDialogs::Show
FunctionEnd

Function RomBrowse
  ${NSD_GetText} $RomField $0
  nsDialogs::SelectFileDialog open "$0" "Nintendo 64 ROM (*.z64;*.v64;*.n64)|*.z64;*.v64;*.n64|All files (*.*)|*.*"
  Pop $0
  ${If} $0 != ""
  ${AndIf} $0 != "error"
    ${NSD_SetText} $RomField $0
  ${EndIf}
FunctionEnd

Function RomPageLeave
  ${NSD_GetState} $RomSkip $0
  ${If} $0 == ${BST_CHECKED}
    StrCpy $RomStatus "skipped"
    StrCpy $FinishText "The game is installed.$\r$\n$\r$\nWhen the game starts, click $\"Select ROM$\" and choose your ROM file. You only do this once."
    Return
  ${EndIf}
  ${NSD_GetText} $RomField $RomPath
  ${If} $RomPath == ""
    MessageBox MB_ICONINFORMATION|MB_OK "Please click Browse and choose your ROM file.$\r$\n$\r$\n(Or tick $\"Skip for now$\" to choose it later in the game.)"
    Abort
  ${EndIf}
  ${IfNot} ${FileExists} "$RomPath"
    MessageBox MB_ICONEXCLAMATION|MB_OK "That file could not be found:$\r$\n$RomPath$\r$\n$\r$\nPlease click Browse and choose it again."
    Abort
  ${EndIf}

  GetDlgItem $0 $HWNDPARENT 1
  EnableWindow $0 0
  ${NSD_SetText} $RomBusy "Preparing the game, this takes a few seconds..."
  ; nsExec blocks the message loop: paint the label now so the page doesn't look frozen.
  System::Call "user32::RedrawWindow(p $RomBusy, p 0, p 0, i 0x185)"
  System::Call "user32::RedrawWindow(p $HWNDPARENT, p 0, p 0, i 0x185)"
  GetTempFileName $1
  ; ExecToStack keeps the console hidden; the friendly one-line result comes from --result-file.
  nsExec::ExecToStack '"$INSTDIR\${EXE}" --install-rom "$RomPath" --result-file "$1"'
  Pop $2 ; exit code (or "error"/"timeout")
  Pop $3 ; console output (not shown)
  ; The exe writes the result as UTF-16LE with a BOM on Windows.
  StrCpy $4 ""
  ClearErrors
  FileOpen $5 "$1" r
  ${IfNot} ${Errors}
    FileReadWord $5 $6 ; BOM
    FileReadUTF16LE $5 $4 4096
    FileClose $5
  ${EndIf}
  Delete "$1"
  ${NSD_SetText} $RomBusy ""
  GetDlgItem $0 $HWNDPARENT 1
  EnableWindow $0 1

  ${If} $2 == 0
    StrCpy $RomStatus "ok"
    StrCpy $FinishText "The game and your ROM are installed. Click Finish to play.$\r$\n$\r$\nNext time, start it from the Start menu.$\r$\n$\r$\nTip: pick a shader in the game (press Esc, then Graphics)."
    MessageBox MB_ICONINFORMATION|MB_OK "Your ROM is OK, and the game has been prepared from it.$\r$\n$\r$\nYou are ready to play!"
    Return
  ${ElseIf} $2 == 4
    StrCpy $RomStatus "ok"
    StrCpy $FinishText "The game and your ROM are installed. The game will finish preparing itself when it starts."
    MessageBox MB_ICONEXCLAMATION|MB_OK "Your ROM is OK and has been saved, but preparing the game failed:$\r$\n$\r$\n$4$\r$\nThe game will try again when you start it. If it keeps failing, check that your antivirus is not blocking the files in$\r$\n$INSTDIR\toolchain"
    Return
  ${ElseIf} $2 == 2
    MessageBox MB_ICONEXCLAMATION|MB_OK "This file can't be used:$\r$\n$\r$\n$4$\r$\nThe game needs the USA version of Magical Tetris Challenge (Nintendo 64). Please choose another file."
    Abort
  ${ElseIf} $2 == 3
    MessageBox MB_ICONEXCLAMATION|MB_OK "Your ROM is correct, but it could not be saved:$\r$\n$\r$\n$4$\r$\nCheck that your disk is not full, then click Next to try again.$\r$\n(Saved to: $LOCALAPPDATA\MagicalBlocks)"
    Abort
  ${Else}
    MessageBox MB_ICONSTOP|MB_OK "The game program could not check the ROM (code: $2).$\r$\n$\r$\nYou can tick $\"Skip for now$\" and choose your ROM in the game instead."
    Abort
  ${EndIf}
FunctionEnd

; Finish page text depends on the ROM step.
Function .onInit
  StrCpy $FinishText "The game is installed. Start it from the Start menu: ${SHORTCUT}."
FunctionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\${SHORTCUT}.lnk"
  Delete "$DESKTOP\${SHORTCUT}.lnk"
  Delete "$INSTDIR\${EXE}"
  Delete "$INSTDIR\SDL2.dll"
  Delete "$INSTDIR\dxcompiler.dll"
  Delete "$INSTDIR\dxil.dll"
  Delete "$INSTDIR\recompcontrollerdb.txt"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\THIRD-PARTY-NOTICES.txt"
  Delete "$INSTDIR\README.txt"
  RMDir /r "$INSTDIR\assets"
  RMDir /r "$INSTDIR\shaders"
  RMDir /r "$INSTDIR\licenses"
  RMDir /r "$INSTDIR\shadercache"
  RMDir /r "$INSTDIR\tools"
  RMDir /r "$INSTDIR\toolchain"
  RMDir /r "$INSTDIR\gamecode"
  ; the game code generated from the ROM can always be regenerated: remove it
  RMDir /r "$LOCALAPPDATA\MagicalBlocks\gamecode-cache"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "${UNINST_KEY}"
  DeleteRegKey HKCU "Software\MagicalBlocks"
  MessageBox MB_ICONINFORMATION|MB_OK "The game was removed.$\r$\n$\r$\nYour settings, saves and your stored ROM copy were kept in:$\r$\n$LOCALAPPDATA\MagicalBlocks$\r$\nDelete that folder yourself if you no longer need them." /SD IDOK
SectionEnd
