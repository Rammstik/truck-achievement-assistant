**[Русский](README.md)** | English

<div align="center">

# ETS2 Achievement Assistant

### Plan achievements. Find useful jobs. See your targets on the map.

An achievement assistant built into Euro Truck Simulator 2.

**[Download the Windows installer](https://github.com/Rammstik/truck-achievement-assistant/releases/download/ets2-v0.6.17/ETS2-Achievement-Assistant-Setup-0.6.17.exe)** · **[All downloads](https://github.com/Rammstik/truck-achievement-assistant/releases)** · **[Report a problem](https://github.com/Rammstik/truck-achievement-assistant/issues)**

**0.6.17** · ETS2 **1.62.2.1** · Windows x64 · DirectX 11

</div>

---

Add an achievement to your plan to see available progress details, highlight supported map targets and filter job offers. Press **Ctrl+F8** to open the assistant.

> This version supports ETS2 **1.62.2.1**. The installer checks compatibility before copying files. A game update may require a new assistant version.

## Features

| Feature | What it does |
| --- | --- |
| **Achievement plan** | Keeps selected goals together and saves your plan between sessions. |
| **Detailed progress** | Shows requirements, cities, companies and cargos using Steam, save files and delivery history. |
| **Map highlighting** | Highlights supported cities, companies, viewpoints, road locations and selected services in red. |
| **Job filtering** | Keeps offers relevant to planned delivery achievements. Multiple achievements combine their matching offers. |
| **Completed-item exclusion** | Excludes confirmed items while keeping unknown progress as a candidate. |
| **Route choice** | Offers a separate, optional job filter for unvisited cities. It is off by default. |
| **Russian and English** | Follows the game language by default. You can choose a different language in Settings. |

### How the plan works

1. Add an achievement with supported delivery conditions to show matching offers.
2. Add another to combine offers useful for either achievement.
3. Remove an achievement to exclude offers that only matched that goal.
4. If no offers match, the list stays empty and the assistant explains why.
5. Remove all delivery goals to restore the normal job list.

Map highlighting works independently of the unvisited-city job filter. You can take a longer job and visit a required city along the way.

## Installation

1. **Close ETS2.**
2. **[Download and run the installer](https://github.com/Rammstik/truck-achievement-assistant/releases/download/ets2-v0.6.17/ETS2-Achievement-Assistant-Setup-0.6.17.exe).** It searches Steam libraries. If necessary, select your `Euro Truck Simulator 2` folder manually.
3. **Start the game and press Ctrl+F8.** Select your profile and add achievements to your plan.

You do not need Python or development tools for normal installation. The installer updates assistant files without changing game saves. If Windows prevents writing to the game folder, run the installer as administrator.

While the panel is open, the mouse and keyboard control the assistant. Press **Ctrl+F8** again to close it.

### Assistant language

Open **Settings → Language** and choose **Game language (automatic)**, **Русский** or **English**. Automatic mode uses Russian when the game is in Russian and English for every other game language. A manual choice is saved between sessions and takes effect without restarting the game.

<details>
<summary><strong>Manual installation with Portable</strong></summary>

Download [Portable 0.6.17](https://github.com/Rammstik/truck-achievement-assistant/releases/download/ets2-v0.6.17/ETS2-Achievement-Assistant-Portable-0.6.17.zip), close the game and merge the archive's `bin` folder into your ETS2 directory.

The `bin/win_x64/plugins` folder should contain:

```text
AchievementAssistant.dll
AchievementAssistant/
  AADataWorker.exe
  _internal/
map-probe.json
```

Keep an existing `map-probe.json` if you have changed its settings. Third-party licences are in the archive's `licenses` folder.

To remove Portable, close the game and delete only `AchievementAssistant.dll` and the `AchievementAssistant` folder.

</details>

## Compatibility and limitations

| Component | Status |
| --- | --- |
| ETS2 | **1.62.2.1**, Windows x64, DirectX 11. |
| Other ETS2 versions | Require a matching assistant build. Native bindings are checked against the exact game executable. |
| ATS | Source is included in the shared project. This installer is for ETS2 only. ATS 0.1.0 targets game version 1.61.3.18. |
| Detailed rules | **110 of 114** Steam achievements, including all four Iceland achievements. |

A detailed rule does not imply that every condition can be filtered or highlighted. Some progress updates after the game saves. Missing delivery records do not prove an item is incomplete; unknown items remain candidates. Some offer conditions, including the exact Special Transport route, cannot be fully checked automatically.

## Updating and uninstalling

**Update:** close the game and run the new installer, selecting the same ETS2 folder.

**Uninstall:** select **ETS2 Achievement Assistant** in Windows installed apps. Your plan and preferences remain in `%LOCALAPPDATA%\ETS2AchievementAssistant`. Other plugins, game saves and the shared `map-probe.json` are preserved.

## Source and building

ETS2 and ATS source code is available in this repository. They share an engine, with a separate DLL build for each game.

**[Download the current source tree (ZIP)](https://github.com/Rammstik/truck-achievement-assistant/archive/refs/heads/main.zip)**

| Folder | Contents |
| --- | --- |
| [work/native-assistant](work/native-assistant) | DLL, interface, game bindings and native tests. |
| [work/achievement-core](work/achievement-core) | Save reading, Steam data and achievement rules. |
| [work/achievement-lab](work/achievement-lab) | Rule and filter checks. |
| [installer](installer) | ETS2 installer and install/uninstall checks. |
| [publication](publication) | Documentation and release publishing. |

Building requires **Windows x64**, **PowerShell 7**, **Visual Studio Build Tools** with C++ x64 and Windows SDK, **Python 3.11**, and **Inno Setup 7** for the installer.

```powershell
python -m pip install -r requirements-build.txt

# DLL and shared data worker
./work/native-assistant/build.ps1 -Game ets2
./work/native-assistant/build_worker.ps1

# Distribution package and installer
python ./installer/prepare_payload.py
./installer/build.ps1 -Compiler 'C:/Program Files/Inno Setup 7/ISCC.exe'
```

Distribution files are written to `outputs/public`. If the DLL hash changes, validate the build and update `installer/release.json`; packaging intentionally rejects an unreviewed binary.

Use `build.ps1 -Game ats` to build the ATS DLL. Saves, user preferences, research dumps and build outputs are excluded from Git. Third-party licences are stored with their source and included in distributions. No separate open-source licence has been assigned to the project's own code yet.

## Feedback

[Open an issue](https://github.com/Rammstik/truck-achievement-assistant/issues) with your game version, assistant version, expected behaviour and what happened. Include a screenshot without personal information if useful.

---

An independent project, not an official SCS Software product. The installer uses [Inno Setup](https://jrsoftware.org/).
