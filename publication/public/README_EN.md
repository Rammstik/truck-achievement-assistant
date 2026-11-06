English | [Русский](README_RU.md)

# ETS2 Achievement Assistant 0.6.16

An in-game achievement plan, red map highlighting and matching job offers for Euro Truck Simulator 2.

## Requirements

ETS2 **1.62.1.0**, Windows x64 and DirectX 11. This package is for ETS2 only. Other game versions require a compatible assistant build.

## Installation

Close the game and run the installer. It searches Steam libraries; select the `Euro Truck Simulator 2` root folder if necessary. If Windows prevents writing to that folder, run the installer as administrator.

For Portable, merge the archive's `bin` folder into the game directory. Keep an existing `map-probe.json` if you changed its settings.

Start ETS2 and press **Ctrl+F8**. Select your profile and add achievements to your plan. Python is not required. While the panel is open, the mouse and keyboard control the assistant; press Ctrl+F8 again to close it.

## Language

Open **Settings → Language**. Choose **Game language (automatic)**, **Русский** or **English**. Automatic mode uses Russian for a Russian game language and English for every other game language. Your manual choice is saved and takes effect without restarting the game.

## Features

- Achievement requirements and progress from Steam, saves and delivery history.
- Red highlighting for supported cities, companies, viewpoints and other locations.
- Combined job filtering for planned delivery achievements.
- Confirmed items are excluded; unknown items remain candidates.
- An optional unvisited-city job filter, off by default. Map highlighting works independently.

Detailed rules cover 106 of 114 Steam achievements. Eight additional bus and Iceland goals do not have detailed support yet. Not every condition can be filtered or highlighted. Some progress updates after saving; missing records do not prove an item is incomplete.

## Updating and uninstalling

Close the game and run a new installer against the same folder. To uninstall, choose **ETS2 Achievement Assistant** in Windows installed apps. Plans and preferences in `%LOCALAPPDATA%\ETS2AchievementAssistant` are preserved, along with game saves, other plugins and `map-probe.json`.

To remove Portable, delete only `AchievementAssistant.dll` and the `AchievementAssistant` folder from `bin/win_x64/plugins` while the game is closed.

## Source and support

[Project and source code](https://github.com/Rammstik/truck-achievement-assistant) · [English documentation](https://github.com/Rammstik/truck-achievement-assistant/blob/main/README.en.md) · [Report a problem](https://github.com/Rammstik/truck-achievement-assistant/issues)

An independent project, not an official SCS Software product. Third-party licences are included. The installer uses [Inno Setup](https://jrsoftware.org/).
