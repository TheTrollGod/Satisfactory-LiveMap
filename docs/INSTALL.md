# Installing Live Map

Live Map has two parts:

- **The mod** (`LiveMap-Windows-<version>.zip`) runs inside the game and streams
  the world over WebSocket on port 8086.
- **The map page** (`LiveMap-Web-<version>.zip`) is a static web page you open in
  a browser. It connects to the mod and draws the map.

Both are attached to every [release](https://github.com/TheTrollGod/Satisfactory-LiveMap/releases).

## Requirements

- Satisfactory 1.2 or newer, **Steam** version on Windows. (The Epic build needs a
  different binary that the release doesn't include yet.)
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

Replace `192.168.1.50` with the IP of the PC running the game, and make sure the
firewall allows port 8086.

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
