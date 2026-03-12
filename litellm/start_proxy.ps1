$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

$litellm = Get-Command litellm -ErrorAction SilentlyContinue
if (-not $litellm) {
    Write-Host "Installing litellm..."
    pip install litellm
}

Write-Host "Starting LiteLLM proxy on port 4000..."
litellm --config "$ScriptDir\litellm_config.yaml" --port 4000
