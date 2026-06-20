# Kindle Settlers

A native KUAL hotseat island-settlement board game for jailbroken Kindle devices.

## Features

- Local hotseat multiplayer for 2-4 players
- Random 19-hex island board
- Touch-first E Ink UI
- Initial settlement and road placement flow
- Dice rolling and resource production
- Roads, settlements, cities, and basic victory detection
- Longest Route calculation
- Mandatory private handoff screen
- Autosave/resume using local JSON save data
- Offline native gameplay

## Current Milestone

This repository implements the first playable milestone: KUAL launch structure, main menu, new game setup, board rendering, initial placement, handoff privacy, dice rolling, resource production, basic building, end turn, autosave, and resume.

Second-milestone systems are intentionally stubbed in the UI until the core loop is stable: bank trading, player-to-player trades, full Bandit discard/steal flow, progress cards, and Largest Patrol.

## Installation

1. Download the GitHub Actions artifact zip.
2. Extract the `kindlesettlers` folder into `/mnt/us/extensions/`.
3. Open KUAL.
4. Launch **Kindle Settlers**.

## Save Data

Save data is stored at:

```text
/mnt/us/extensions/kindlesettlers/data/save.json
```

Settings are stored at:

```text
/mnt/us/extensions/kindlesettlers/data/settings.json
```

## Build

The included GitHub Actions workflow builds with the kindlehf toolchain, Kindle SDK, Meson, GTK2, and C++17, then packages one KUAL extension zip. No desktop-debug artifact is generated.
