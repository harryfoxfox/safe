echo off

inf2cat /v /driver:%1 /os:XP_X86,Vista_X86,Vista_X64,7_X86,7_X64,8_X86,8_X64

rem /c is where the signing certificate (Rian Hunter) is stored
rem /ac is our cross signing cert

signtool sign /v /s MY /n "Rian Hunter" /ac %~DP0\digicert.crt /fd sha1 /t "http://timestamp.digicert.com" %1\safe_ramdisk.cat
