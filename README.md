# TCGP Stat Tracking

Automatically track WRs for decks in Pokemon Trading Card Game Pocket.

## Usage
1. Download `main.exe` from Releases or build it.
2. Run `main.exe`.
3. Add `http://localhost:5000/` as a browser source in OBS to display stats on stream.
4. A `deck_stats.csv` file will be saved and loaded when the program runs.

## How it works
The program asks you for a window to take screenshots from, which should be set to your phone window. Then the program takes screenshots of the window every 0.5 seconds and performs OCR on them. Based on the text, the program determines what deck you're using and whether you've won or lost. 

## Build `.exe`
```
pyinstaller --onefile main.py
```