@echo off
sun "32-bitstrapper"
if exist "32-bitstrapper.dll" (
	mv "32-bitstrapper.dll" "32-bitstrapper.asi"
	del "32-bitstrapper.lib"
) else (
	pause > nul
)
