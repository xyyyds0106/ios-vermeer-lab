param([string]$Compiler = 'gcc', [switch]$Analyze)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskOutput = Join-Path ([IO.Path]::GetTempPath()) ('k70-cellular-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskOutput | Out-Null
foreach ($taskTest in @('cellular_service', 'cellular_qrtr')) {
    $taskSource = Join-Path $PSScriptRoot ($taskTest + '_test.c')
    $taskExecutable = Join-Path $taskOutput ($taskTest + '.exe')
    $taskArgs = @('-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2', '-g')
    if ($Analyze) { $taskArgs += '-fanalyzer' }
    & $Compiler @taskArgs $taskSource '-o' $taskExecutable
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $taskTest ($LASTEXITCODE)" }
    & $taskExecutable
    if ($LASTEXITCODE -ne 0) { throw "Test failed: $taskTest ($LASTEXITCODE)" }
}
foreach ($taskHeader in @('vermeer_cellular.h', 'vermeer_cellular_qrtr.h')) {
    $taskHeaderPath = Join-Path $taskRoot ('xnu/vermeer/cellular/' + $taskHeader)
    & $Compiler '-std=c11' '-Wall' '-Wextra' '-Werror' '-pedantic' '-ffreestanding' '-fsyntax-only' '-x' 'c' $taskHeaderPath
    if ($LASTEXITCODE -ne 0) { throw "Freestanding syntax failed: $taskHeader ($LASTEXITCODE)" }
}
Write-Output "Host compiler: $Compiler; native freestanding syntax checked; artifacts: $taskOutput"
Write-Output 'No device I/O or hardware capability validation occurred.'
