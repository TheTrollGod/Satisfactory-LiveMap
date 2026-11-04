# Installing Live Map

Live Map has two parts:

- **The mod** runs wherever the world is hosted and streams it over WebSocket on
  port 8086. There's one zip per place it can run:
  - `LiveMap-Windows-<version>.zip`: you host from the game on Windows.
  - `LiveMap-WindowsServer-<version>.zip`: Windows dedicated server.
  - `LiveMap-LinuxServer-<version>.zip`: Linux dedicated server.
- **The map page** (`LiveMap-Web-<version>.zip`) is a static web page you open in
  a browser. It connects to the mod and draws the map.

Both are attached to every [release](https://github.com/TheTrollGod/Satisfactory-LiveMap/releases).

## Requirements

- Satisfactory 1.2 or newer. For hosting from the game: the **Steam** version on
  Windows (the Epic build needs a different binary that the release doesn't include
  yet). Dedicated servers work on Windows and Linux.
- [Satisfactory Mod Loader (SML)](https://ficsit.app/mod/SML) 3.12 or newer. The easiest
  way to get it is to install any mod with
  [Satisfactory Mod Manager](https://smm.ficsit.app/), which pulls SML in for you.
- Live Map runs on whoever hosts the world. Players joining your game don't need it.

## 1. Install the mod

1. Download `LiveMap-Windows-<version>.zip` from the latest release.
2. Open your game folder. In Steam: right-click Satisfactory > Manage > Browse local files.
3. Go to `FactoryGame\Mods`. Create it if it doesn't exist (SML creates it).
4. Make a folder named `LiveMap` there and extract the zip into it. You should end up with:

   ```
   FactoryGame\Mods\LiveMap\LiveMap.uplugin
   FactoryGame\Mods\LiveMap\Binaries\Win64\...
   FactoryGame\Mods\LiveMap\Content\Paks\Windows\...
   ```

   If you see `Mods\LiveMap\LiveMap\LiveMap.uplugin`, you extracted one level too
   deep: move the inner contents up.

5. Start the game and load a save. The first time, Windows may ask whether to let
   Satisfactory through the firewall. Allow it on private networks if you want to
   view the map from another device.

To check it's running, open <http://localhost:8086/health> in a browser while the
save is loaded.

### On a dedicated server

The server needs SML too. [Satisfactory Mod Manager](https://smm.ficsit.app/) can
install mods on a server (local folder or SFTP), or use
[ficsit-cli](https://github.com/satisfactorymodding/ficsit-cli) on the server itself.

Then install Live Map by hand, the same way as above, using the server zip:

```bash
# Linux example; adjust the path to your server install
cd ~/SatisfactoryDedicatedServer/FactoryGame/Mods
mkdir -p LiveMap && cd LiveMap
unzip ~/LiveMap-LinuxServer-<version>.zip
ls LiveMap.uplugin   # should exist at this level
```

On Windows servers, extract `LiveMap-WindowsServer-<version>.zip` into
`<server>\FactoryGame\Mods\LiveMap\` instead.

Restart the server, then open TCP port 8086 in the server's firewall (for example
`sudo ufw allow 8086/tcp`) and your hosting provider's firewall if it has one.
Check it with `curl http://<server-ip>:8086/health`.

On a dedicated server the map has no terrain background yet (the server has no GPU
to render it). Players, vehicles, trains, rails and buildings all show.

## 2. Open the map

The map page has to be served over HTTP; opening `index.html` straight from disk
won't work.

1. Download `LiveMap-Web-<version>.zip` and extract it anywhere.
2. In that folder, start a local web server. Either of these works:

   ```bash
   npx serve -l 5173 .
   ```

   ```bash
   python -m http.server 5173
   ```

3. Open <http://localhost:5173>. It connects to `ws://localhost:8086/ws` by default.

### Viewing from another machine

Point the page at the host's address with the `ws` parameter:

```
http://localhost:5173/?ws=ws://192.168.1.50:8086/ws
```

Replace `192.168.1.50` with the IP of the PC or server running the world, and make
sure the firewall allows port 8086. For a dedicated server, you can also put the map
page on the server itself and serve it from there.

There is no password on the map yet. Anyone who can reach port 8086 can see player
positions and your base layout, so don't forward it to the internet.

## Updating

Close the game, delete `FactoryGame\Mods\LiveMap`, and repeat step 1 with the new zip.

## Uninstalling

Delete `FactoryGame\Mods\LiveMap`. The mod only reads the world, so your saves
keep working without it.

## Troubleshooting

- **The game says LiveMap needs SML, or the mod doesn't load.** Install or update
  SML (see Requirements).
- **The page says "disconnected".** Make sure a save is loaded (the mod only runs
  in a loaded world), and check <http://localhost:8086/health>.
- **"could not listen on port 8086" in `FactoryGame.log`.** Something else is using
  the port. Close it and reload the save.
- Logs are in `%LOCALAPPDATA%\FactoryGame\Saved\Logs\FactoryGame.log`; search for `LiveMap`.
