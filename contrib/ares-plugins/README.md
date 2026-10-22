# Ares plugins for Antares

The community scenarios from the [Ares Addons archive](http://www.cytheraguides.com/archives/ambrosia_addons/ares/),
converted to Antares plugins, with a launcher for Linux and Windows for picking one and playing it.

These plugins were written for the original Ares by its players, around 2000 to 2004. Each
still carries its author's name, and its archive description is in `description.txt` inside the
plugin. They're collected here so they can be played again; they belong to their authors.

## Contents

- `plugins/`: 116 converted plugins (`.antaresplugin`).
  - `Official/`: the 7 conversions published by arescentral.org, with their read-me files. These
    were converted by hand and are the better versions where a scenario appears twice; they
    share saved progress with the converted copy.
  - `notes/`: what the converter changed or couldn't convert, one file per plugin.
  - `conversion-log.tsv`: every file in the archive, and the plugin made from it or why none was.
  - `descriptions.json`: each plugin's archive entry and description.
- `launcher/antares-launcher`: the launcher (Linux and Windows).
- `tools/`: scripts used for the conversion.

## Playing

On any platform, copy the plugins you want (or the whole `plugins` folder) into the game's
scenarios folder, then choose one with the Plugins button on the main menu:

| Platform | Scenarios folder |
|---|---|
| Linux | `~/.local/share/games/antares/scenarios` |
| macOS | `~/Library/Application Support/Antares/Scenarios` |
| Windows | `Documents\Antares\Scenarios` |

The game can also list a folder in place: `antares -p contrib/ares-plugins/plugins` (on Windows,
`Antares.exe -p contrib\ares-plugins\plugins`).

## The launcher

`launcher/antares-launcher` lists the plugins in `plugins/` (or another folder, with "Plugins
folder…") with their authors, descriptions, solo chapters and how many you've unlocked, and
starts Antares with the one you pick. It can also save, import and reset a plugin's progress.
It needs Python 3 with Tk and an Antares build with the Plugins screen.

On Linux, install Tk with `sudo apt install python3-tk`. To run the launcher as
`antares-launcher`, link it from a directory on your `PATH` (the link still finds `plugins/`):

```
ln -s "$PWD/contrib/ares-plugins/launcher/antares-launcher" ~/.local/bin/antares-launcher
```

On Windows, the python.org installer includes Tk. Double-click
`launcher\antares-launcher.pyw`, or start the launcher with Python:

```
python contrib\ares-plugins\launcher\antares-launcher
```

It runs `Antares.exe` from your `PATH`, or else this checkout's build in `out\win\opt`, which it
points at the `data` folder here. Its settings and logs are kept in `%APPDATA%\Antares Launcher`
and `%LOCALAPPDATA%\Antares Launcher`, and progress in `Documents\Antares`, as the game keeps it.

Options at the bottom of the window (Linux only):

- **Software rendering** runs the game with `LIBGL_ALWAYS_SOFTWARE=1`, for virtual machines
  whose graphics drivers garble the display.
- **Let the game use the Windows and Alt keys** turns off the GNOME shortcuts that use them
  while the game runs, since the game uses them as fire keys, and restores the shortcuts when
  it quits.

On Windows, the game itself keeps the Windows keys from opening the Start menu while it has the
keyboard, and Alt doesn't open the window menu, so neither option is needed.

## Tools

- `archive_descriptions.py ARCHIVE_PAGE PLUGINS_DIR`: writes each plugin's description from a
  saved copy of the archive page.
- `set_identifier.py PLUGIN TAG`: gives a plugin its own saved progress when it shares a title
  with another.
- `batch.py`, `check-all.sh`, `refcheck.py`: batch conversion and checks, run with the
  ares2antares converter; `ares2antares-fixes.patch` is the set of converter fixes they were made
  with.
