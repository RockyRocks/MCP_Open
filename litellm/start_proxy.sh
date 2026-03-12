#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if ! command -v litellm &> /dev/null; then
    echo "Installing litellm..."
    pip install litellm
fi

echo "Starting LiteLLM proxy on port 4000..."
litellm --config "$SCRIPT_DIR/litellm_config.yaml" --port 4000
