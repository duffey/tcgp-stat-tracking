# TCGP Stat Tracking

Automatically track WRs for decks in Pokemon Trading Card Game Pocket.

## Usage
1. Download `main.exe` from Releases or build it.
2. Run `main.exe`.
3. Add `http://localhost:5000/` as a browser source in OBS to display stats on stream.
4. A `deck_stats.csv` file will be saved and loaded when the program runs.

## Build `.exe`
```
pyinstaller --onefile main.py
```