"""
Quick sanity check — fetches 2023 Monza race and prints key stats.
Run this before the full pipeline to confirm FastF1 + network are working.
Usage: python scripts/verify_fastf1.py
"""
import warnings
warnings.filterwarnings("ignore")

from pathlib import Path
import fastf1
import numpy as np

CACHE = Path(__file__).parent.parent / "data" / ".fastf1_cache"
CACHE.mkdir(parents=True, exist_ok=True)
fastf1.Cache.enable_cache(str(CACHE))

print("FastF1 version:", fastf1.__version__)
print("Fetching 2024 Italian GP (Monza) Race...")

session = fastf1.get_session(2024, "Italian", "R")
session.load(telemetry=False, weather=False, messages=False)

laps = session.laps
accurate = laps[laps["IsAccurate"] == True]

base_lt = accurate["LapTime"].dt.total_seconds().median()
compounds = laps["Compound"].dropna().unique().tolist()

pit_laps = laps[laps["PitInTime"].notna() & laps["PitOutTime"].notna()]
pit_durations = (pit_laps["PitOutTime"] - pit_laps["PitInTime"]).dt.total_seconds()
pit_durations = pit_durations[(pit_durations > 15) & (pit_durations < 50)]

print(f"\n  Total laps      : {int(laps['LapNumber'].max())}")
print(f"  Base lap time   : {base_lt:.3f}s")
print(f"  Compounds used  : {compounds}")
print(f"  Pit loss median : {pit_durations.median():.2f}s ({len(pit_durations)} stops)")
print(f"  Drivers         : {laps['Driver'].nunique()}")

if session.results is not None:
    winner = session.results.sort_values("Position").iloc[0]
    print(f"  Winner          : {winner['FullName']} ({winner['TeamName']})")

print("\nFastF1 is working correctly.")
