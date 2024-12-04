import easyocr
from PIL import ImageGrab
import numpy as np
import pygetwindow as gw
import os
import threading
from flask import Flask, render_template_string
import time
import csv
import win32gui
import tkinter as tk
from tkinter import ttk

reader = easyocr.Reader(['en'])
DATA_FILE = "deck_stats.csv"
data = {}  # Shared data dictionary
app = Flask(__name__)

TEMPLATE = """
<!doctype html>
<html>
    <head>
        <title>Deck Stats</title>
        <style>
            body { font-size: 26px; font-family: Roboto; }
            th { text-align: left; }
            th, td { padding-right: 1em; }
        </style>
        <script>
            setTimeout(function() { location.reload(); }, 5000);
        </script>
    </head>
    <body>
        <table>
            <thead>
                <tr><th>Deck</th><th>Wins</th><th>Losses</th><th>WR</th></tr>
            </thead>
            <tbody>
                {% for deck, stats in data.items() %}
                    {% if stats[0] > 0 or stats[1] > 0 %}
                    <tr>
                        <td>{{ deck }}</td>
                        <td>{{ stats[0] }}</td>
                        <td>{{ stats[1] }}</td>
                        <td>{{ (stats[0] * 100 // (stats[0] + stats[1])) }}%</td>
                    </tr>
                    {% endif %}
                {% endfor %}
            </tbody>
        </table>
    </body>
</html>
"""

@app.route("/")
def show_data():
    return render_template_string(TEMPLATE, data=data)

def load_data():
    if os.path.exists(DATA_FILE):
        with open(DATA_FILE, mode="r") as file:
            reader = csv.reader(file)
            for row in reader:
                if len(row) == 3:
                    deck, wins, losses = row[0], int(row[1]), int(row[2])
                    data[deck] = [wins, losses]

def write_data():
    with open(DATA_FILE, mode="w", newline="") as file:
        writer = csv.writer(file)
        for deck, stats in data.items():
            if stats[0] > 0 or stats[1] > 0:
                writer.writerow([deck, stats[0], stats[1]])

def run_server():
    app.run(port=5000, debug=False, use_reloader=False)

server_thread = threading.Thread(target=run_server, daemon=True)
server_thread.start()

def is_window_active(window_title):
    return win32gui.GetForegroundWindow() == win32gui.FindWindow(None, window_title)

def grab_text():
    title = "MuMu Player 12"
    window = [w for w in gw.getWindowsWithTitle(title) if w.visible]
    if not window or not is_window_active(title):
        return None
    
    window = window[0]
    screenshot = ImageGrab.grab(bbox=(window.left, window.top, window.right, window.bottom))
    screenshot_np = np.array(screenshot)
    results = reader.readtext(screenshot_np)
    
    if results:
        results = sorted(results, key=lambda x: x[0][0][1])
    return [result[1] for result in results]

def find_deck(lines):
    for i, line in enumerate(lines):
        if "Battle" in line and "Battle stance" not in line:
            return lines[i - 1] if i > 0 else None
    return None

def find(lines, text):
    return any(text in line for line in lines)

def start_scraping(root):
    root.destroy()
    current_deck = None

    while True:
        try:
            lines = grab_text()
            if lines:
                if find(lines, "Random Match"):
                    updated_deck = find_deck(lines)
                    if updated_deck:
                        current_deck = updated_deck
                        if current_deck not in data:
                            data[current_deck] = [0, 0]
                        print(f"Deck updated to: {current_deck}")

                if current_deck:
                    if find(lines, "Victory"):
                        data[current_deck][0] += 1
                        print(f"Victory for deck: {current_deck}")
                        current_deck = None
                        write_data()
                        continue

                    if find(lines, "Defeat"):
                        data[current_deck][1] += 1
                        print(f"Defeat for deck: {current_deck}")
                        current_deck = None
                        write_data()
                        continue

            time.sleep(0.5)
        except KeyboardInterrupt:
            write_data()
            break

def setup_gui():
    root = tk.Tk()
    root.title("Window Scraper")
    tk.Label(root, text="Select a window to scrape from:").pack(pady=10)
    windows = [w.title for w in gw.getWindowsWithTitle("")]
    selected_window = tk.StringVar(value=windows[0] if windows else "")
    window_menu = ttk.Combobox(root, textvariable=selected_window, values=windows)
    window_menu.pack(pady=10)
    start_button = tk.Button(root, text="Start Scraping", command=lambda: start_scraping(root))
    start_button.pack(pady=10)
    root.mainloop()

load_data()
setup_gui()
