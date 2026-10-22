param(
    [string]$CubeIdeRoot = 'C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE'
)

$plugins = Join-Path $CubeIdeRoot 'plugins'

$toolPatterns = @(
    'com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*\tools\bin\arm-none-eabi-gcc.exe'
    'com.st.stm32cube.ide.mcu.externaltools.make.win32_*\tools\bin\make.exe'
    'com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_*\tools\bin\STM32_Programmer_CLI.exe'
)

$toolDirectories = @()

foreach ($pattern in $toolPatterns) {
    $tool = Get-Item -Path (Join-Path $plugins $pattern) `
        -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1

    if ($null -eq $tool) {
        throw "Tool not found: $pattern. Check CubeIdeRoot."
    }

    $toolDirectories += $tool.DirectoryName
    Write-Host "Found: $($tool.Name)"
}

$env:Path = ($toolDirectories -join ';') + ';' + $env:Path

Write-Host 'STM32 tools are ready in this PowerShell session.'