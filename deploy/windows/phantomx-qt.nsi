; PhantomX Wallet - Windows installer (NSIS)
; Build with:
;   makensis -DSTAGE_DIR=dist\qt-windows-stage -DOUT_FILE=dist\PhantomX-Wallet-Setup.exe deploy\windows\phantomx-qt.nsi

!include "MUI2.nsh"

!ifndef STAGE_DIR
  !error "STAGE_DIR is required (directory containing phantomx-qt.exe and deployed Qt runtime)"
!endif
!ifndef OUT_FILE
  !define OUT_FILE "PhantomX-Wallet-Setup.exe"
!endif

Name "PhantomX Wallet"
OutFile "${OUT_FILE}"
SetCompressor /SOLID lzma
InstallDir "$PROGRAMFILES64\PhantomX Wallet"
InstallDirRegKey HKLM "Software\PhantomX\Wallet" "InstallDir"
RequestExecutionLevel admin
Unicode true

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\phantomx-qt.exe"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "PhantomX Wallet" SecMain
  SetOutPath "$INSTDIR"
  File /r "${STAGE_DIR}\*.*"

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateShortcut "$SMPROGRAMS\PhantomX Wallet.lnk" "$INSTDIR\phantomx-qt.exe"
  CreateShortcut "$DESKTOP\PhantomX Wallet.lnk" "$INSTDIR\phantomx-qt.exe"

  WriteRegStr HKLM "Software\PhantomX\Wallet" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PhantomXWallet" "DisplayName" "PhantomX Wallet"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PhantomXWallet" "DisplayVersion" "0.1.0"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PhantomXWallet" "Publisher" "PhantomX"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PhantomXWallet" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PhantomXWallet" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PhantomXWallet" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\PhantomX Wallet.lnk"
  Delete "$DESKTOP\PhantomX Wallet.lnk"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PhantomXWallet"
  DeleteRegKey HKLM "Software\PhantomX\Wallet"
SectionEnd
