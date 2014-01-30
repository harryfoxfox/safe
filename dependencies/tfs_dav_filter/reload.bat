rem get back working directory after ShellExecute "runas"
setlocal enableextensions
cd /d "%~dp0"

fltmc.exe unload "WebDAV RAM Cache"
RUNDLL32.EXE SETUPAPI.DLL,InstallHinfSection DefaultUninstall 132 %cd%\tfs_dav_filter.inf

install_non_pnp_device safe_ramdisk.inf root\saferamdisk

RUNDLL32.EXE SETUPAPI.DLL,InstallHinfSection DefaultInstall 132 %cd%\tfs_dav_filter.inf
fltmc.exe load "WebDAV RAM Cache"
