rem get back working directory after ShellExecute "runas"
setlocal enableextensions
cd /d "%~dp0"

install_non_pnp_device safe_ramdisk.inf root\saferamdisk
exit /b %ERRORLEVEL%
