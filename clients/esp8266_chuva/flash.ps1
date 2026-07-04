param(
    [string]$Port
)

if (-not (Get-Command pio -ErrorAction SilentlyContinue)) {
    Write-Error "PlatformIO (pio) not found"
    exit 1
}

$portArg = ""
if ($Port) {
    $portArg = "--upload-port $Port"
}

pio run --target upload $portArg
