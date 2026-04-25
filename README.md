# 32-Bitstrapper

Supported versions:
- 2014.04.23.18.00

## Usage

1. [Download 32-bitstrapper.7z from the latest release](https://github.com/Sainan/32-bitstrapper/releases)
2. Drop 32-bitstrapper.asi & version.dll next to Warframe.exe
3. Now you can double-click Warframe.exe to start the game

## Command Line Arguments

32-Bitstrapper adds the following command line arguments, which can be used for configuration:
- `owfServerHost` (default: 127.0.0.1)
- `owfHttpPort` (default: 80)

## Traffic Deviations

- login.php & worldState.php requests have two query parameters added: `buildLabel` to indicate the client's buildLabel, and `clientMod` to indicate the 32-Bitstrapper's name and version.
