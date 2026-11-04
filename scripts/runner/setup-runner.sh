#!/usr/bin/env bash
# Prepares an Ubuntu 24.04 VM as the self-hosted GitHub Actions runner that
# packages Live Map. Run as the VM's sudo user (the runner runs as that user).
#
#   RUNNER_TOKEN=<registration token> bash setup-runner.sh
#
# Get a token from repo Settings > Actions > Runners > New self-hosted runner, or:
#   gh api -X POST repos/TheTrollGod/Satisfactory-LiveMap/actions/runners/registration-token --jq .token
#
# Build state (engine, SML, Wine, MSVC, intermediates) lives in /srv/livemap and
# survives between runs; that's what makes releases fast after the first one.
set -euo pipefail

REPO_URL="https://github.com/TheTrollGod/Satisfactory-LiveMap"
RUNNER_VERSION="${RUNNER_VERSION:?set RUNNER_VERSION, e.g. 2.329.0}"
RUNNER_NAME="${RUNNER_NAME:-$(hostname)}"
LABELS="livemap"
: "${RUNNER_TOKEN:?set RUNNER_TOKEN}"

echo "==> Packages"
sudo apt-get update -q
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q \
  qemu-guest-agent git curl wget jq zstd unzip zip dos2unix msitools perl python3 \
  build-essential nodejs gh \
  gcc-multilib gcc-mingw-w64 libasound2-dev bluez libpulse-dev libdbus-1-dev libfontconfig-dev \
  libfreetype-dev libgnutls28-dev libgl-dev libunwind-dev libx11-dev libxcomposite-dev \
  libxcursor-dev libxfixes-dev libxi-dev libxrandr-dev libxrender-dev libxext-dev libwayland-bin \
  libwayland-dev libegl-dev libxkbcommon-dev libxkbregistry-dev libgstreamer1.0-dev \
  libgstreamer-plugins-base1.0-dev libosmesa6-dev libsdl2-dev libudev-dev libvulkan-dev flex bison
sudo systemctl enable --now qemu-guest-agent

echo "==> Limits"
# Zen (used while paking) raises its open-file limit; give it room.
echo "$USER soft nofile 1048576
$USER hard nofile 1048576" | sudo tee /etc/security/limits.d/90-livemap-runner.conf >/dev/null
sudo mkdir -p /etc/systemd/system.conf.d
printf '[Manager]\nDefaultLimitNOFILE=1048576\n' | sudo tee /etc/systemd/system.conf.d/90-nofile.conf >/dev/null
sudo systemctl daemon-reexec

echo "==> Build state directory"
sudo mkdir -p /srv/livemap
sudo chown "$USER:$USER" /srv/livemap

echo "==> Runner $RUNNER_VERSION"
mkdir -p "$HOME/actions-runner"
cd "$HOME/actions-runner"
if [ ! -f config.sh ]; then
  curl -fsSL -o runner.tgz "https://github.com/actions/runner/releases/download/v${RUNNER_VERSION}/actions-runner-linux-x64-${RUNNER_VERSION}.tar.gz"
  tar xzf runner.tgz && rm runner.tgz
fi
./config.sh --unattended --replace --url "$REPO_URL" --token "$RUNNER_TOKEN" \
  --name "$RUNNER_NAME" --labels "$LABELS" --work "$HOME/actions-runner/_work"
sudo ./svc.sh install "$USER"
sudo ./svc.sh start
echo "==> Done. Runner '$RUNNER_NAME' registered with label '$LABELS'."
