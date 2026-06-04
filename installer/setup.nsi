; PS2Emu - Instalador NSIS
; Gera PS2Emu_Setup.exe profissional com menu iniciar, desinstalar, etc.

!define APP_NAME      "PS2Emu"
!define APP_VERSION   "1.0"
!define APP_PUBLISHER "PS2Emu Project"
!define APP_URL       "https://github.com/SEU_USUARIO/ps2emu"
!define APP_EXE       "ps2emu.exe"
!define INSTALL_DIR   "$PROGRAMFILES64\PS2Emu"

; Compressao maxima
SetCompressor /SOLID lzma
SetCompressorDictSize 32

Name "${APP_NAME} ${APP_VERSION}"
OutFile "PS2Emu_Setup.exe"
InstallDir "${INSTALL_DIR}"
InstallDirRegKey HKLM "Software\${APP_NAME}" "Install_Dir"
RequestExecutionLevel admin
ShowInstDetails show
ShowUnInstDetails show

; Paginas do instalador
Page license
Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

LicenseData "..\LICENSE"

Section "PS2Emu (obrigatorio)" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"

  ; Copiar arquivos principais
  File "..\release\ps2emu.exe"
  File "..\release\SDL2.dll"
  File "..\release\zlib1.dll"

  ; Criar pastas
  CreateDirectory "$INSTDIR\bios"
  CreateDirectory "$INSTDIR\saves"
  CreateDirectory "$INSTDIR\states"
  CreateDirectory "$INSTDIR\roms"

  ; Arquivos de texto
  File /oname=bios\LEIA_ME.txt "..\release\bios\LEIA_ME.txt"

  ; Criar config padrao
  FileOpen $0 "$INSTDIR\config.ini" w
  FileWrite $0 "[video]$\r$\n"
  FileWrite $0 "width=640$\r$\n"
  FileWrite $0 "height=448$\r$\n"
  FileWrite $0 "fps=60$\r$\n"
  FileWrite $0 "skip_frames=true$\r$\n"
  FileWrite $0 "$\r$\n[cpu]$\r$\n"
  FileWrite $0 "threads=1$\r$\n"
  FileWrite $0 "fast_boot=true$\r$\n"
  FileWrite $0 "$\r$\n[paths]$\r$\n"
  FileWrite $0 "bios=$INSTDIR\bios\SCPH-70012.bin$\r$\n"
  FileWrite $0 "saves=$INSTDIR\saves\$\r$\n"
  FileWrite $0 "states=$INSTDIR\states\$\r$\n"
  FileClose $0

  ; Registro para desinstalar
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
    "DisplayName" "${APP_NAME}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
    "UninstallString" "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
    "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
    "Publisher" "${APP_PUBLISHER}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
    "URLInfoAbout" "${APP_URL}"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
    "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" \
    "NoRepair" 1

  WriteUninstaller "$INSTDIR\uninstall.exe"

  ; Atalhos Menu Iniciar
  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" \
    "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\Desinstalar.lnk" \
    "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0

  ; Atalho na area de trabalho
  CreateShortcut "$DESKTOP\${APP_NAME}.lnk" \
    "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0

  ; Associar .iso e .bin com o emulador
  WriteRegStr HKCR ".iso\OpenWithProgids" "${APP_NAME}.iso" ""
  WriteRegStr HKCR "${APP_NAME}.iso" "" "Imagem de PS2"
  WriteRegStr HKCR "${APP_NAME}.iso\shell\open\command" "" \
    '"$INSTDIR\${APP_EXE}" "%1"'

SectionEnd

Section "Uninstall"
  Delete "$INSTDIR\ps2emu.exe"
  Delete "$INSTDIR\SDL2.dll"
  Delete "$INSTDIR\zlib1.dll"
  Delete "$INSTDIR\config.ini"
  Delete "$INSTDIR\uninstall.exe"
  RMDir /r "$INSTDIR\bios"
  RMDir /r "$INSTDIR\saves"
  RMDir /r "$INSTDIR\states"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\${APP_NAME}\*.*"
  RMDir "$SMPROGRAMS\${APP_NAME}"
  Delete "$DESKTOP\${APP_NAME}.lnk"

  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
  DeleteRegKey HKCR "${APP_NAME}.iso"
SectionEnd
