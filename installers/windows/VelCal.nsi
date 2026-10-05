Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
Name "VelCal ${VERSION}"
OutFile "${OUTPUT}"
InstallDir "$PROGRAMFILES64\VelCal"
InstallDirRegKey HKLM "Software\VelCal" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${STAGE}\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "VelCal requires 64-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
FunctionEnd

Section "Standalone application" Standalone
  SetOutPath "$INSTDIR"
  File "${STAGE}\VelCal.exe"
  CreateDirectory "$SMPROGRAMS\VelCal"
  CreateShortcut "$SMPROGRAMS\VelCal\VelCal.lnk" "$INSTDIR\VelCal.exe"
SectionEnd

Section "VST3 instrument" VST3
  SetOutPath "$COMMONFILES64\VST3\VelCal.vst3"
  File /r "${STAGE}\VelCal.vst3\*"
SectionEnd

Section "Licences and uninstaller"
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "${STAGE}\LICENSE"
  SetOutPath "$INSTDIR\LICENSES"
  File "${STAGE}\LICENSES\*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\VelCal" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelCal" "DisplayName" "VelCal"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelCal" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelCal" "Publisher" "VelCal"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelCal" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelCal" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelCal" "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetRegView 64
  SetShellVarContext all
  Delete "$INSTDIR\VelCal.exe"
  Delete "$INSTDIR\LICENSE"
  RMDir /r "$INSTDIR\LICENSES"
  RMDir /r "$COMMONFILES64\VST3\VelCal.vst3"
  Delete "$SMPROGRAMS\VelCal\VelCal.lnk"
  RMDir "$SMPROGRAMS\VelCal"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VelCal"
  DeleteRegKey HKLM "Software\VelCal"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd
