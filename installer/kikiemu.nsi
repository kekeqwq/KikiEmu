; SPDX-License-Identifier: GPL-2.0-or-later
Unicode true
; The requested public filename is setup.exe. NSIS warns (9000) that Windows
; may apply filename-based installer compatibility shims. This one explicitly
; documented filename warning is accepted; all OTHER warnings remain errors.
; No elevation, host shim settings, security bypass or filename disguise.
!pragma warning disable 9000
!include MUI2.nsh
!include LogicLib.nsh
!include x64.nsh
!include WinVer.nsh
!ifndef PAYLOAD
!error "Supply a validated PAYLOAD directory with build_kikiemu_setup.ps1"
!endif
!ifndef OUTPUT
!error "Supply an OUTPUT setup.exe path"
!endif
Name "KikiEmu 0.3 Alpha"
OutFile "${OUTPUT}"
RequestExecutionLevel user
SetCompressor /SOLID lzma
CRCCheck force
ManifestDPIAware true
InstallDir "$LOCALAPPDATA\Programs\KikiEmu"
Icon "${PAYLOAD}\kikiemu.ico"
UninstallIcon "${PAYLOAD}\kikiemu.ico"
VIProductVersion "0.3.0.0"
VIAddVersionKey /LANG=1033 "ProductName" "KikiEmu"
VIAddVersionKey /LANG=1033 "ProductVersion" "0.3.0-alpha"
VIAddVersionKey /LANG=1033 "FileDescription" "KikiEmu setup (ARM64 applications)"
VIAddVersionKey /LANG=1033 "FileVersion" "0.3.0.0"
VIAddVersionKey /LANG=1033 "LegalCopyright" "KikiEmu contributors"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "Install the native Windows ARM64 KikiEmu CLI and desktop entry for this user.$\r$\n$\r$\nSystem packages and your own compatible QEMU runtime are separate downloads/builds. No Android disk or QEMU is installed by setup.$\r$\n$\r$\nClose KikiEmu instances before updating. Uninstall keeps all system instances and data."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${PAYLOAD}\licenses\project\GPL-3.0.txt"
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TEXT "KikiEmu has been installed.$\r$\n$\r$\nOpen a NEW PowerShell window and follow QUICK_START.md to create an instance with its own system ZIP, storage, total size and QEMU bin directory.$\r$\n$\r$\nThe desktop entry starts only the instance you explicitly set as default."
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
Var InstallLease

Function .onInit
  SetShellVarContext current
  SetRegView 64
  ${IfNot} ${IsNativeARM64}
    MessageBox MB_OK|MB_ICONSTOP "KikiEmu requires native ARM64 Windows 11. x86/x64 PCs are not supported."
    Abort
  ${EndIf}
  ${IfNot} ${AtLeastWin11}
    MessageBox MB_OK|MB_ICONSTOP "KikiEmu requires Windows 11 on ARM64."
    Abort
  ${EndIf}
  ; Fixed owned application location, even with an unsupported /D override.
  StrCpy $INSTDIR "$LOCALAPPDATA\Programs\KikiEmu"
  StrCpy $InstallLease 0
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  File /oname=kikiemu-setup-helper.exe "${PAYLOAD}\kikiemu-setup-helper.exe"
  ExecWait '"$PLUGINSDIR\kikiemu-setup-helper.exe" preflight' $0
  ${If} $0 != 0
    Abort
  ${EndIf}
FunctionEnd

Function un.onInit
  SetShellVarContext current
  SetRegView 64
  ; Never follow a relocated uninstaller into a caller-selected directory.
  StrCmp $INSTDIR "$LOCALAPPDATA\Programs\KikiEmu" +3
    MessageBox MB_OK|MB_ICONSTOP "The registered installation path does not match KikiEmu. Removal refused."
    Abort
  StrCpy $InstallLease 0
  ExecWait '"$INSTDIR\kikiemu-setup-helper.exe" preflight' $0
  ${If} $0 != 0
    Abort
  ${EndIf}
FunctionEnd

!macro AcquireLease
  System::Call 'kernel32::CreateFileW(w "$INSTDIR\install.lock", i 0xC0000000, i 0, p 0, i 4, i 0x200080, p 0) p.r0'
  StrCpy $InstallLease $0
  ${If} $InstallLease == -1
    StrCpy $InstallLease 0
    MessageBox MB_OK|MB_ICONSTOP "KikiEmu is running, another setup is active, or the installation cannot be locked. Close its instances and retry. No process will be killed automatically."
    Abort
  ${EndIf}
!macroend
!macro ReleaseLease
  ${If} $InstallLease != 0
    System::Call 'kernel32::CloseHandle(p $InstallLease)'
    StrCpy $InstallLease 0
  ${EndIf}
!macroend

Section "Install"
  CreateDirectory "$INSTDIR"
  !insertmacro AcquireLease
  WriteRegStr HKCU "Software\KikiEmu\Installer" "InstallRoot" "$INSTDIR"
  SetOutPath "$INSTDIR"
  File "${PAYLOAD}\kikiemu.exe"
  File "${PAYLOAD}\kikiemu-desktop.exe"
  File "${PAYLOAD}\kikiemu-setup-helper.exe"
  File "${PAYLOAD}\surface-camera-bridge.exe"
  File "${PAYLOAD}\zlib1.dll"
  File "${PAYLOAD}\kikiemu.ico"
  File "${PAYLOAD}\LICENSE"
  File "${PAYLOAD}\LICENSE_NOTICE.md"
  File "${PAYLOAD}\QUICK_START.md"
  SetOutPath "$INSTDIR\licenses"
  File /r "${PAYLOAD}\licenses\*"
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  ExecWait '"$INSTDIR\kikiemu-setup-helper.exe" path-add' $0
  ${If} $0 != 0
    MessageBox MB_OK|MB_ICONSTOP "PATH setup did not complete. The app files remain installed; retry setup or run uninstall.exe. No instance data was changed."
    Abort
  ${EndIf}
  CreateDirectory "$SMPROGRAMS\KikiEmu"
  CreateShortcut "$SMPROGRAMS\KikiEmu\KikiEmu.lnk" "$INSTDIR\kikiemu-desktop.exe" "" "$INSTDIR\kikiemu.ico"
  CreateShortcut "$DESKTOP\KikiEmu.lnk" "$INSTDIR\kikiemu-desktop.exe" "" "$INSTDIR\kikiemu.ico"
  CreateShortcut "$SMPROGRAMS\KikiEmu\Uninstall KikiEmu.lnk" "$INSTDIR\uninstall.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KikiEmu" "DisplayName" "KikiEmu 0.3 Alpha"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KikiEmu" "DisplayVersion" "0.3.0-alpha"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KikiEmu" "Publisher" "KikiEmu contributors"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KikiEmu" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KikiEmu" "UninstallString" '$\"$INSTDIR\uninstall.exe$\"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KikiEmu" "DisplayIcon" "$INSTDIR\kikiemu.ico"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KikiEmu" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KikiEmu" "NoRepair" 1
  !insertmacro ReleaseLease
SectionEnd

Section "Uninstall"
  !insertmacro AcquireLease
  ExecWait '"$INSTDIR\kikiemu-setup-helper.exe" path-remove' $0
  ${If} $0 != 0
    MessageBox MB_OK|MB_ICONSTOP "PATH cleanup did not complete. Removal stopped so you can retry safely. Your systems and data remain unchanged."
    Abort
  ${EndIf}
  Delete "$DESKTOP\KikiEmu.lnk"
  Delete "$SMPROGRAMS\KikiEmu\KikiEmu.lnk"
  Delete "$SMPROGRAMS\KikiEmu\Uninstall KikiEmu.lnk"
  RMDir "$SMPROGRAMS\KikiEmu"
  Delete "$INSTDIR\kikiemu.exe"
  Delete "$INSTDIR\kikiemu-desktop.exe"
  Delete "$INSTDIR\kikiemu-setup-helper.exe"
  Delete "$INSTDIR\surface-camera-bridge.exe"
  Delete "$INSTDIR\zlib1.dll"
  Delete "$INSTDIR\kikiemu.ico"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\LICENSE_NOTICE.md"
  Delete "$INSTDIR\QUICK_START.md"
  ; Only compiler-enumerated installer-owned license paths; never RMDir /r.
  !include "${PAYLOAD}\uninstall-licenses.nsh"
  RMDir "$INSTDIR\licenses"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KikiEmu"
  DeleteRegValue HKCU "Software\KikiEmu\Installer" "InstallRoot"
  DeleteRegKey /ifempty HKCU "Software\KikiEmu\Installer"
  !insertmacro ReleaseLease
  Delete "$INSTDIR\install.lock"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  ; NO release registry, instance storage, QEMU or downloaded ZIP is removed.
SectionEnd

Function .onGUIEnd
  !insertmacro ReleaseLease
FunctionEnd
Function un.onGUIEnd
  !insertmacro ReleaseLease
FunctionEnd
