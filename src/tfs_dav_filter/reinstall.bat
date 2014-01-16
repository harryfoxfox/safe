rem get back working directory after ShellExecute "runas"
setlocal enableextensions
cd /d "%~dp0"

rem now uninstall and reinstall!
RUNDLL32.EXE SETUPAPI.DLL,InstallHinfSection DefaultUninstall 132 %cd%\tfs_dav_filter.inf
RUNDLL32.EXE SETUPAPI.DLL,InstallHinfSection DefaultInstall 132 %cd%\tfs_dav_filter.inf
