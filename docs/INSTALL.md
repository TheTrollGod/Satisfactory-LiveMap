# Live Map: Installation and Operations Guide

Audience: administrators deploying Live Map on a Satisfactory dedicated server or a
player-hosted (listen) server.

## 1. Overview

Live Map is a single server-side mod. It reads world state on the game thread and
serves both the data (HTTP and WebSocket) and the map page itself. After installing
it and starting the world, browse to `http://<host>:8086/`. Nothing else needs to be
installed or run.

The mod is server-side only (`RequiredOnRemote: false`). Connecting players do not
need it installed. The map page is also published separately (`LiveMap-Web`) for
setups that host it elsewhere (section 6), but that is optional.

### 1.1 Network interface

The mod opens one TCP listener, **port 8086**, on all IPv4 interfaces. HTTP and WebSocket
share the port. There is no TLS.

| Path | Protocol | Purpose |
|---|---|---|
| `/`, `/assets/*` | HTTP GET | The map page (compiled into the mod) |
| `/health` | HTTP GET | Liveness check, returns `200` with a JSON body |
| `/ws` | WebSocket | Snapshot on connect, then dynamic deltas (players, vehicles, trains, drones) at 5 Hz |
| `/static/*` | HTTP GET | Versioned static layers (buildings, rails, belts, pipes, power lines) and the terrain image (`/static/map`) |

The page connects back to the host and port it was loaded from. Data endpoints carry
`Access-Control-Allow-Origin: *`, so the page can also be hosted on a different
origin (section 6).

### 1.2 Release artifacts

Each [release](https://github.com/TheTrollGod/Satisfactory-LiveMap/releases) is tagged
`v<version>-build.<n>` and contains:

| File | Target |
|---|---|
| `LiveMap-LinuxServer-<v>.zip` | Linux dedicated server |
| `LiveMap-WindowsServer-<v>.zip` | Windows dedicated server |
| `LiveMap-Windows-<v>.zip` | Game client on Windows (listen server). Steam and Epic |
| `LiveMap-Web-<v>.zip` | Map page as static files. Optional; the mod already serves it |

Each mod zip holds the plugin root: `LiveMap.uplugin`, `Binaries/<platform>/`,
`Content/Paks/<platform>/`. It extracts to `FactoryGame/Mods/LiveMap/`.

## 2. Requirements

- Satisfactory 1.2 or later (build 513885+).
- **Satisfactory Mod Loader (SML) 3.12 or later**, installed on the same host.
  Live Map does not load without it. Install it with
  [Satisfactory Mod Manager](https://smm.ficsit.app/) (supports local and SFTP
  server targets) or [ficsit-cli](https://github.com/satisfactorymodding/ficsit-cli).
- Linux install script: `bash`, `wget` or `curl`, and `unzip` (falls back to
  `python3` or `bsdtar`).
- Inbound TCP 8086 reachable from wherever the map will be viewed.

## 3. Installation

Stop the server before installing or updating. The mod is loaded at startup only.

### 3.1 Linux dedicated server (script)

Run as the account that owns the server files (commonly `steam`), not root, from
the server's install directory (the one containing `FactoryGame/`):

```bash
cd ~/SatisfactoryServer
wget -qO- https://raw.githubusercontent.com/TheTrollGod/Satisfactory-LiveMap/main/scripts/install.sh | bash
```

To review the script before executing it:

```bash
cd ~/SatisfactoryServer
wget https://raw.githubusercontent.com/TheTrollGod/Satisfactory-LiveMap/main/scripts/install.sh
less install.sh
bash install.sh
```

What it does:

1. Resolves the server directory: the argument if given, otherwise the current
   directory, otherwise common locations (`~/SatisfactoryDedicatedServer`, Steam
   library paths, `/home/steam/...`, `/opt/satisfactory`, `/config/gamefiles` for
   the common Docker image). Fails if no `FactoryGame/` is found.
2. Queries the GitHub Releases API and downloads `LiveMap-LinuxServer-*.zip`.
3. Replaces `FactoryGame/Mods/LiveMap/` with the zip contents.
4. Warns if `FactoryGame/Mods/SML/` is absent.

Script reference (with the piped form, pass options after `bash -s --`, e.g.
`... | bash -s -- --uninstall`):

| Option | Effect |
|---|---|
| `[SERVER_DIR]` | Install into this directory instead of auto-detecting |
| `--version TAG` | Install a specific release tag instead of the latest |
| `--zip FILE` | Install from a local zip; no download (offline hosts) |
| `--web DIR` | Also download and extract the frontend into `DIR` |
| `--uninstall` | Remove `FactoryGame/Mods/LiveMap/` |
| `-h`, `--help` | Usage |

Exit status is non-zero on any failure; the existing install is left in place if
the download or extraction fails.

### 3.2 Linux dedicated server (manual)

```bash
cd ~/SatisfactoryServer/FactoryGame/Mods
rm -rf LiveMap && mkdir LiveMap
unzip /path/to/LiveMap-LinuxServer-<v>.zip -d LiveMap
test -f LiveMap/LiveMap.uplugin && echo ok
```

### 3.3 Windows dedicated server

```powershell
$mods = "C:\SatisfactoryServer\FactoryGame\Mods"
Remove-Item "$mods\LiveMap" -Recurse -Force -ErrorAction SilentlyContinue
Expand-Archive LiveMap-WindowsServer-<v>.zip -DestinationPath "$mods\LiveMap"
Test-Path "$mods\LiveMap\LiveMap.uplugin"
```

### 3.4 Listen server (host player's game, Windows)

Extract `LiveMap-Windows-<v>.zip` into `<game install>\FactoryGame\Mods\LiveMap\`
(Steam: `<Steam library>\steamapps\common\Satisfactory`; Epic: the folder chosen at
install). The zip
contains both the Steam (`FactoryGameSteam-*`) and Epic (`FactoryGameEGS-*`) binaries;
the game loads the matching one. The mod starts when the host loads a save.

## 4. Firewall

Open inbound TCP 8086 on the host and on any upstream firewall or cloud security
group.

```bash
sudo ufw allow 8086/tcp                                      # ufw
sudo firewall-cmd --permanent --add-port=8086/tcp && sudo firewall-cmd --reload   # firewalld
```

```powershell
New-NetFirewallRule -DisplayName "Satisfactory Live Map" -Direction Inbound -Protocol TCP -LocalPort 8086 -Action Allow
```

## 5. Security considerations

- **No authentication and no TLS.** Anyone who can reach port 8086 can see player
  positions and the full base layout. Do not expose it to the internet as-is.
- Restrict the source range, e.g. `sudo ufw allow from 192.168.1.0/24 to any port 8086 proto tcp`,
  or keep 8086 internal and put an authenticating TLS reverse proxy in front (5.1).
- The mod only reads world state; it accepts no commands that change the game.

### 5.1 Reverse proxy (nginx example)

Puts the mod behind HTTPS with basic auth. The page uses `wss://` automatically when
loaded over HTTPS. The mod must be at the root of the proxied host (the page requests
`/ws`, `/static/` and `/assets/` from the root).

```nginx
server {
    listen 443 ssl;
    server_name map.example.com;
    ssl_certificate     /etc/letsencrypt/live/map.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/map.example.com/privkey.pem;

    auth_basic           "Live Map";
    auth_basic_user_file /etc/nginx/livemap.htpasswd;

    location / {
        proxy_pass http://127.0.0.1:8086;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_read_timeout 1h;
    }
}
```

Viewers open `https://map.example.com/`. With this setup, block 8086 externally.

## 6. Map page

The mod serves the page at `http://<host>:8086/`; no setup is needed.

To host the page elsewhere instead (an existing web server, a different domain),
use the `LiveMap-Web` zip. It is static and must be served over HTTP(S); `file://`
does not work (ES modules).

```bash
bash install.sh --web /var/www/livemap      # or unzip LiveMap-Web-<v>.zip yourself
```

A separately hosted page connects to its own host by default, so pass the mod's
address:

| Query parameter | Example | Effect |
|---|---|---|
| `ws` | `?ws=ws://10.0.0.5:8086/ws` | WebSocket endpoint of the mod |
| `mock` | `?mock` | Synthetic data, no server needed (frontend testing) |

## 7. Verification

```bash
curl -i http://<host>:8086/health          # expect HTTP/1.1 200 OK
grep LiveMap <server>/FactoryGame/Saved/Logs/FactoryGame.log
```

Then open `http://<host>:8086/` in a browser; the status line at the bottom left
should read `connected`.

A healthy start logs `LiveMap: listening on port 8086 at 5 Hz` and
`LiveMap: map page at http://<this host>:8086/`. On a dedicated
server it also logs `LiveMap: no terrain on a server that can't render` (expected,
see 8).

Log locations:

| Host | Log |
|---|---|
| Linux dedicated server | `<server>/FactoryGame/Saved/Logs/FactoryGame.log` |
| Windows dedicated server | `<server>\FactoryGame\Saved\Logs\FactoryGame.log` |
| Windows game client | `%LOCALAPPDATA%\FactoryGame\Saved\Logs\FactoryGame.log` |

## 8. Configuration and limitations

Settings are compiled-in defaults; there is no config file yet.

| Setting | Value |
|---|---|
| Listen port | 8086 |
| Dynamic update rate | 5 Hz |
| Static layer rescan | every 10 s |
| Terrain image size | 8192 px |

Known limitations:

- No terrain background on dedicated servers (no GPU to render it). All other layers work.
- Dedicated server builds are produced by CI but have not yet been verified on a live server.

## 9. Update, rollback, removal

| Task | Procedure |
|---|---|
| Update | Stop server, rerun the script (or repeat the manual install), start server |
| Rollback | Stop server, `bash install.sh --version <older tag>` |
| Remove | Stop server, `bash install.sh --uninstall` or delete `FactoryGame/Mods/LiveMap/` |

The mod's subsystem is not part of the save game (it does not implement the save
interface), so saves load normally after removal.

## 10. Troubleshooting

| Symptom | Cause / action |
|---|---|
| Mod not loaded, no `LiveMap` log lines | SML missing or older than 3.12, or wrong zip for the platform. Check `FactoryGame/Mods/SML/` and that `LiveMap/Binaries/` contains your platform |
| `could not listen on port 8086` in log | Port in use (`ss -ltnp 'sport = :8086'` / `Get-NetTCPConnection -LocalPort 8086`). Free it and restart |
| `/health` works locally, not remotely | Firewall or security group (section 4) |
| `http://<host>:8086/` returns 404, `/health` works | Mod built without the page (log says `built without the map page`). Use a release build |
| Page shows "disconnected" | No world loaded yet (the mod starts with the world), port blocked, or a wrong `ws` parameter on a separately hosted page |
| Frontend loads but no buildings | Static layers rebuild every 10 s after load; wait, then check the browser console for failed `/static/` requests |
| Mixed-content error in browser | Page served over HTTPS but `ws=ws://...`. Use the reverse proxy and `wss://` (5.1) |
| Script: `couldn't find the server` | Run from the directory containing `FactoryGame/`, or pass it as an argument |
