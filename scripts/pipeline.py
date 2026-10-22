"""
F1 PitWall — Data Pipeline
Fetches race session data via FastF1, fits tyre degradation curves,
extracts driver/team/circuit info, and writes everything to:
  - data/circuits/<circuit_id>_<year>.json
  - data/tyres/<circuit_id>_<year>_tyres.json
  - data/sessions/<circuit_id>_<year>_session.json
  - data/drivers.json
  - data/teams.json
  - data/pitwall.db  (SQLite — consumed by C++ backend)

Usage:
  python scripts/pipeline.py                        # default circuits, last 3 seasons
  python scripts/pipeline.py --year 2024            # single year
  python scripts/pipeline.py --circuit monza --year 2024
  python scripts/pipeline.py --list-circuits        # show available circuit names
  python scripts/pipeline.py --db-only              # skip fetch, rebuild DB from existing JSONs
"""

import argparse
import json
import os
import sqlite3
import sys
import warnings
from pathlib import Path
from datetime import datetime

import numpy as np

warnings.filterwarnings("ignore")

try:
    import fastf1
    import fastf1.plotting
    import pandas as pd
    from scipy.optimize import curve_fit
    from scipy.stats import median_abs_deviation
except ImportError as e:
    print(f"[ERROR] Missing dependency: {e}")
    print("Run: pip install fastf1 scipy")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SCRIPT_DIR   = Path(__file__).parent
REPO_ROOT    = SCRIPT_DIR.parent
DATA_DIR     = REPO_ROOT / "data"
CACHE_DIR    = DATA_DIR / ".fastf1_cache"
DB_PATH      = DATA_DIR / "pitwall.db"

for d in [DATA_DIR, CACHE_DIR,
          DATA_DIR / "circuits", DATA_DIR / "tyres",
          DATA_DIR / "sessions", DATA_DIR / "drivers"]:
    d.mkdir(parents=True, exist_ok=True)

fastf1.Cache.enable_cache(str(CACHE_DIR))

# ---------------------------------------------------------------------------
# Target circuits (FastF1 grand prix name → short id)
# ---------------------------------------------------------------------------
DEFAULT_CIRCUITS = {
    "Bahrain":      "bahrain",
    "Saudi Arabia": "jeddah",
    "Australia":    "melbourne",
    "Japan":        "suzuka",
    "China":        "shanghai",
    "Miami":        "miami",
    "Emilia Romagna": "imola",
    "Monaco":       "monaco",
    "Canada":       "montreal",
    "Spain":        "barcelona",
    "Austria":      "austria",
    "British":      "silverstone",
    "Hungary":      "budapest",
    "Belgian":      "spa",
    "Dutch":        "zandvoort",
    "Italian":      "monza",
    "Azerbaijan":   "baku",
    "Singapore":    "singapore",
    "United States":"cota",
    "Mexico City":  "mexico",
    "São Paulo":    "interlagos",
    "Las Vegas":    "lasvegas",
    "Qatar":        "qatar",
    "Abu Dhabi":    "abudhabi",
}

DEFAULT_YEARS   = [2022, 2023, 2024]
COMPOUNDS_ORDER = ["SOFT", "MEDIUM", "HARD", "INTERMEDIATE", "WET"]

# ---------------------------------------------------------------------------
# Tyre degradation curve fitting
# ---------------------------------------------------------------------------

def _deg_model(tyre_age, base, linear, quadratic):
    return base + linear * tyre_age + quadratic * tyre_age ** 2


def fit_degradation(laps_df: pd.DataFrame, compound: str,
                    base_lap_time: float) -> dict | None:
    """
    Fit a quadratic degradation curve for a given compound from race laps.
    Returns dict with curve params or None if insufficient data.
    """
    c = laps_df[laps_df["Compound"] == compound].copy()
    c = c[c["IsAccurate"] == True]
    c = c[c["LapTime"].notna()]
    c = c[c["TyreLife"].notna()]
    c = c[c["TyreLife"] > 0]
    # Remove outliers (> 3 MAD from median)
    lap_secs = c["LapTime"].dt.total_seconds()
    if len(lap_secs) < 8:
        return None
    mad = median_abs_deviation(lap_secs)
    median = lap_secs.median()
    c = c[np.abs(lap_secs - median) < 3.5 * mad]
    if len(c) < 8:
        return None

    ages      = c["TyreLife"].values.astype(float)
    lap_times = c["LapTime"].dt.total_seconds().values
    # Express as delta from base lap time
    deltas    = lap_times - base_lap_time

    try:
        popt, _ = curve_fit(
            _deg_model, ages, deltas,
            p0=[deltas[ages == ages.min()].mean(), 0.05, 0.001],
            maxfev=10000
        )
    except Exception:
        # Fall back to polyfit
        coeffs = np.polyfit(ages, deltas, 2)
        popt   = [coeffs[2], coeffs[1], coeffs[0]]

    base_delta, deg_linear, deg_quadratic = popt

    # Practical cliff: tyre age where lap time > median + 2s
    cliff = None
    for age in range(1, 80):
        if _deg_model(age, *popt) > 2.0:
            cliff = age
            break

    min_laps_map = {"SOFT": 5, "MEDIUM": 8, "HARD": 12, "INTERMEDIATE": 3, "WET": 2}
    max_laps_map = {"SOFT": 30, "MEDIUM": 42, "HARD": 60, "INTERMEDIATE": 30, "WET": 30}

    return {
        "compound":       compound,
        "base_lap_time_delta": round(float(base_delta), 4),
        "deg_linear":     round(float(deg_linear), 6),
        "deg_quadratic":  round(float(deg_quadratic), 8),
        "min_laps":       min_laps_map.get(compound, 5),
        "max_laps":       cliff or max_laps_map.get(compound, 40),
        "sample_count":   int(len(c)),
    }


# ---------------------------------------------------------------------------
# Single race session fetch
# ---------------------------------------------------------------------------

def fetch_race(gp_name: str, circuit_id: str, year: int) -> dict | None:
    print(f"  Loading {year} {gp_name} Race...", end=" ", flush=True)
    try:
        session = fastf1.get_session(year, gp_name, "R")
        session.load(telemetry=False, weather=False, messages=False)
        print("OK")
    except Exception as e:
        print(f"FAILED ({e})")
        return None

    laps   = session.laps
    results = session.results

    if laps is None or len(laps) == 0:
        print(f"  [WARN] No lap data for {year} {gp_name}")
        return None

    # -----------------------------------------------------------------------
    # Base lap time: median of all accurate laps from top 5 cars (representative pace)
    # -----------------------------------------------------------------------
    accurate = laps[laps["IsAccurate"] == True]
    top5_drivers = (
        results.sort_values("Position").head(5)["Abbreviation"].tolist()
        if results is not None and len(results) > 0 else []
    )
    if top5_drivers:
        top5_laps = accurate[accurate["Driver"].isin(top5_drivers)]
        base_lt = top5_laps["LapTime"].dt.total_seconds().median() if len(top5_laps) > 0 else accurate["LapTime"].dt.total_seconds().median()
    else:
        base_lt = accurate["LapTime"].dt.total_seconds().median()

    if np.isnan(base_lt):
        base_lt = accurate["LapTime"].dt.total_seconds().min()

    total_laps = int(laps["LapNumber"].max())

    # -----------------------------------------------------------------------
    # Pit loss: median of actual pit stop durations
    # FastF1 v3.x: use PitInTime/PitOutTime if available,
    # otherwise estimate from lap-time delta on pit laps vs clean laps.
    # -----------------------------------------------------------------------
    pit_loss = 22.0  # safe default
    try:
        pit_laps = laps[laps["PitInTime"].notna() & laps["PitOutTime"].notna()].copy()
        if len(pit_laps) > 0:
            pit_durations = (pit_laps["PitOutTime"] - pit_laps["PitInTime"]).dt.total_seconds()
            pit_durations = pit_durations[(pit_durations > 15) & (pit_durations < 50)]
            if len(pit_durations) > 0 and not np.isnan(pit_durations.median()):
                pit_loss = round(float(pit_durations.median()), 2)
    except Exception:
        pit_laps = pd.DataFrame()

    # Fallback: infer from stint transitions
    if pit_loss == 22.0:
        try:
            pit_laps_flag = laps[laps["PitInTime"].notna()].copy() if "PitInTime" in laps.columns else pd.DataFrame()
            if len(pit_laps_flag) == 0:
                # Estimate: find laps where next lap has tyre_age reset (stint change)
                laps_s = laps.sort_values(["Driver", "LapNumber"])
                laps_s["NextTyreLife"] = laps_s.groupby("Driver")["TyreLife"].shift(-1)
                pit_laps_flag = laps_s[
                    (laps_s["TyreLife"] > 1) &
                    (laps_s["NextTyreLife"] <= 2) &
                    laps_s["LapTime"].notna()
                ]
                if len(pit_laps_flag) > 3:
                    pit_lap_times = pit_laps_flag["LapTime"].dt.total_seconds()
                    pit_lap_times = pit_lap_times[(pit_lap_times > base_lt) & (pit_lap_times < base_lt + 60)]
                    if len(pit_lap_times) > 0:
                        pit_loss = round(float(pit_lap_times.median() - base_lt), 2)
                        pit_loss = max(15.0, min(pit_loss, 35.0))
        except Exception:
            pass

    # Fuel effect: estimate from lap time trend over a long stint (no pits)
    # Approx 0.03-0.06s/lap depending on circuit
    fuel_effect = 0.04  # conservative default

    # -----------------------------------------------------------------------
    # Tyre degradation curves
    # -----------------------------------------------------------------------
    tyre_specs = []
    used_compounds = laps["Compound"].dropna().unique()
    for compound in COMPOUNDS_ORDER:
        if compound not in used_compounds:
            continue
        spec = fit_degradation(laps, compound, base_lt)
        if spec:
            tyre_specs.append(spec)

    # -----------------------------------------------------------------------
    # Driver stints
    # -----------------------------------------------------------------------
    stints = []
    for driver in laps["Driver"].unique():
        drv_laps = laps[laps["Driver"] == driver].sort_values("LapNumber")
        for _, lap in drv_laps.iterrows():
            stints.append({
                "driver":     str(driver),
                "lap":        int(lap["LapNumber"]) if not pd.isna(lap["LapNumber"]) else None,
                "compound":   str(lap["Compound"]) if not pd.isna(lap["Compound"]) else None,
                "tyre_age":   int(lap["TyreLife"]) if not pd.isna(lap["TyreLife"]) else None,
                "stint":      int(lap["Stint"]) if not pd.isna(lap["Stint"]) else None,
                "position":   int(lap["Position"]) if not pd.isna(lap["Position"]) else None,
                "lap_time_s": round(lap["LapTime"].total_seconds(), 3) if not pd.isna(lap["LapTime"]) else None,
                "pit_in":     bool(not pd.isna(lap["PitInTime"])),
                "pit_out":    bool(not pd.isna(lap["PitOutTime"])),
                "is_accurate":bool(lap["IsAccurate"]) if not pd.isna(lap["IsAccurate"]) else False,
            })

    # -----------------------------------------------------------------------
    # Race results
    # -----------------------------------------------------------------------
    race_results = []
    if results is not None:
        for _, row in results.iterrows():
            race_results.append({
                "driver":      str(row.get("Abbreviation", "")),
                "full_name":   str(row.get("FullName", "")),
                "team":        str(row.get("TeamName", "")),
                "team_id":     str(row.get("TeamId", "")),
                "number":      int(row["DriverNumber"]) if not pd.isna(row.get("DriverNumber")) else None,
                "position":    int(row["Position"]) if not pd.isna(row.get("Position")) else None,
                "grid":        int(row["GridPosition"]) if not pd.isna(row.get("GridPosition")) else None,
                "status":      str(row.get("Status", "")),
                "points":      float(row["Points"]) if not pd.isna(row.get("Points")) else 0.0,
                "nationality": str(row.get("CountryCode", "")),
            })

    # -----------------------------------------------------------------------
    # Pit stops summary per driver
    # -----------------------------------------------------------------------
    pit_stops = []
    pit_laps_summary = laps[laps["PitInTime"].notna()].copy() if "PitInTime" in laps.columns else pd.DataFrame()
    if len(pit_laps_summary) > 0:
        for _, lap in pit_laps_summary.iterrows():
            duration = (lap["PitOutTime"] - lap["PitInTime"]).total_seconds()
            try:
                duration = (lap["PitOutTime"] - lap["PitInTime"]).total_seconds()
                if 15 < duration < 60:
                    pit_stops.append({
                        "driver":   str(lap["Driver"]),
                        "lap":      int(lap["LapNumber"]) if not pd.isna(lap["LapNumber"]) else None,
                        "duration": round(float(duration), 3),
                        "compound": str(lap["Compound"]) if not pd.isna(lap["Compound"]) else None,
                    })
            except Exception:
                pass

    session_data = {
        "circuit_id":   circuit_id,
        "gp_name":      gp_name,
        "year":         year,
        "date":         str(session.date.date()) if hasattr(session, "date") and session.date is not None else "",
        "total_laps":   total_laps,
        "base_lap_time":round(float(base_lt), 3),
        "pit_loss_seconds": pit_loss,
        "fuel_effect_per_lap": fuel_effect,
        "stints":       stints,
        "pit_stops":    pit_stops,
        "results":      race_results,
    }

    circuit_data = {
        "circuit_id":       circuit_id,
        "gp_name":          gp_name,
        "year":             year,
        "total_laps":       total_laps,
        "pit_loss_seconds": pit_loss,
        "base_lap_time":    round(float(base_lt), 3),
        "fuel_effect_per_lap": fuel_effect,
    }

    tyres_data = {
        "circuit_id": circuit_id,
        "year":       year,
        "compounds":  tyre_specs,
    }

    return {
        "circuit":  circuit_data,
        "tyres":    tyres_data,
        "session":  session_data,
        "results":  race_results,
    }


# ---------------------------------------------------------------------------
# Extract unique drivers and teams across all sessions
# ---------------------------------------------------------------------------

TEAM_COLORS = {
    "red bull racing":   "#3671C6",
    "ferrari":           "#E8002D",
    "mercedes":          "#27F4D2",
    "mclaren":           "#FF8000",
    "aston martin":      "#229971",
    "alpine":            "#FF87BC",
    "williams":          "#64C4FF",
    "rb":                "#6692FF",
    "haas":              "#B6BABD",
    "kick sauber":       "#52E252",
    "alphatauri":        "#5E8FAA",
    "alfa romeo":        "#C92D4B",
    "racing point":      "#F596C8",
    "renault":           "#FFF500",
    "toro rosso":        "#469BFF",
    "force india":       "#F595C0",
    "lotus f1":          "#FFB800",
}


def build_drivers_teams(all_results: list[dict]) -> tuple[list, list]:
    drivers_map = {}
    teams_map   = {}

    for r in all_results:
        for d in r:
            abbr = d.get("driver", "")
            if abbr and abbr not in drivers_map:
                drivers_map[abbr] = {
                    "abbreviation": abbr,
                    "full_name":    d.get("full_name", ""),
                    "number":       d.get("number"),
                    "team_id":      d.get("team_id", ""),
                    "nationality":  d.get("nationality", ""),
                }
            tid = d.get("team_id", "")
            if tid and tid not in teams_map:
                color = TEAM_COLORS.get(d.get("team", "").lower(), "#888888")
                teams_map[tid] = {
                    "team_id":   tid,
                    "team_name": d.get("team", ""),
                    "color":     color,
                }

    return list(drivers_map.values()), list(teams_map.values())


# ---------------------------------------------------------------------------
# SQLite database builder
# ---------------------------------------------------------------------------

SCHEMA_SQL = """
CREATE TABLE IF NOT EXISTS circuits (
    circuit_id      TEXT NOT NULL,
    year            INTEGER NOT NULL,
    gp_name         TEXT,
    total_laps      INTEGER,
    pit_loss_seconds REAL,
    base_lap_time    REAL,
    fuel_effect_per_lap REAL,
    PRIMARY KEY (circuit_id, year)
);

CREATE TABLE IF NOT EXISTS tyre_specs (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    circuit_id      TEXT NOT NULL,
    year            INTEGER NOT NULL,
    compound        TEXT NOT NULL,
    base_delta      REAL,
    deg_linear      REAL,
    deg_quadratic   REAL,
    min_laps        INTEGER,
    max_laps        INTEGER,
    sample_count    INTEGER,
    UNIQUE(circuit_id, year, compound)
);

CREATE TABLE IF NOT EXISTS drivers (
    abbreviation    TEXT PRIMARY KEY,
    full_name       TEXT,
    number          INTEGER,
    team_id         TEXT,
    nationality     TEXT
);

CREATE TABLE IF NOT EXISTS teams (
    team_id         TEXT PRIMARY KEY,
    team_name       TEXT,
    color           TEXT
);

CREATE TABLE IF NOT EXISTS race_results (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    circuit_id      TEXT NOT NULL,
    year            INTEGER NOT NULL,
    driver          TEXT NOT NULL,
    team_id         TEXT,
    position        INTEGER,
    grid            INTEGER,
    status          TEXT,
    points          REAL,
    UNIQUE(circuit_id, year, driver)
);

CREATE TABLE IF NOT EXISTS race_stints (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    circuit_id      TEXT NOT NULL,
    year            INTEGER NOT NULL,
    driver          TEXT NOT NULL,
    lap             INTEGER,
    compound        TEXT,
    tyre_age        INTEGER,
    stint           INTEGER,
    position        INTEGER,
    lap_time_s      REAL,
    pit_in          INTEGER,
    pit_out         INTEGER,
    is_accurate     INTEGER
);

CREATE TABLE IF NOT EXISTS pit_stops (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    circuit_id      TEXT NOT NULL,
    year            INTEGER NOT NULL,
    driver          TEXT NOT NULL,
    lap             INTEGER,
    duration_s      REAL,
    compound        TEXT
);

CREATE TABLE IF NOT EXISTS strategy_runs (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    created_at      TEXT NOT NULL,
    circuit_id      TEXT,
    year            INTEGER,
    algorithm       TEXT,
    data_structure  TEXT,
    total_race_time REAL,
    stops_count     INTEGER,
    wall_clock_ms   REAL,
    states_explored INTEGER,
    stops_json      TEXT
);

CREATE TABLE IF NOT EXISTS benchmark_runs (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    created_at      TEXT NOT NULL,
    circuit_id      TEXT,
    year            INTEGER,
    impl_tag        TEXT,
    num_simulations INTEGER,
    num_threads     INTEGER,
    layout          TEXT,
    opt_level       TEXT,
    mean_race_time  REAL,
    stddev_race_time REAL,
    wall_clock_ms   REAL
);

CREATE INDEX IF NOT EXISTS idx_stints_circuit_year   ON race_stints(circuit_id, year);
CREATE INDEX IF NOT EXISTS idx_stints_driver         ON race_stints(driver);
CREATE INDEX IF NOT EXISTS idx_tyre_specs_circuit    ON tyre_specs(circuit_id, year);
CREATE INDEX IF NOT EXISTS idx_strategy_runs_circuit ON strategy_runs(circuit_id, year);
CREATE INDEX IF NOT EXISTS idx_benchmark_runs_circuit ON benchmark_runs(circuit_id, year);
"""


def build_database(all_data: list[dict], drivers: list, teams: list):
    print(f"\n[DB] Building {DB_PATH} ...")
    con = sqlite3.connect(DB_PATH)
    cur = con.cursor()
    cur.executescript(SCHEMA_SQL)

    for d in drivers:
        cur.execute("""INSERT OR REPLACE INTO drivers VALUES (?,?,?,?,?)""",
                    (d["abbreviation"], d["full_name"], d["number"],
                     d["team_id"], d["nationality"]))

    for t in teams:
        cur.execute("""INSERT OR REPLACE INTO teams VALUES (?,?,?)""",
                    (t["team_id"], t["team_name"], t.get("color", "#888888")))

    for entry in all_data:
        c   = entry["circuit"]
        ty  = entry["tyres"]
        ses = entry["session"]
        cid = c["circuit_id"]
        yr  = c["year"]

        cur.execute("""INSERT OR REPLACE INTO circuits VALUES (?,?,?,?,?,?,?)""",
                    (cid, yr, c["gp_name"], c["total_laps"],
                     c["pit_loss_seconds"], c["base_lap_time"],
                     c["fuel_effect_per_lap"]))

        for spec in ty["compounds"]:
            cur.execute("""INSERT OR REPLACE INTO tyre_specs
                           (circuit_id,year,compound,base_delta,deg_linear,
                            deg_quadratic,min_laps,max_laps,sample_count)
                           VALUES (?,?,?,?,?,?,?,?,?)""",
                        (cid, yr, spec["compound"], spec["base_lap_time_delta"],
                         spec["deg_linear"], spec["deg_quadratic"],
                         spec["min_laps"], spec["max_laps"], spec["sample_count"]))

        for res in ses["results"]:
            cur.execute("""INSERT OR REPLACE INTO race_results
                           (circuit_id,year,driver,team_id,position,grid,status,points)
                           VALUES (?,?,?,?,?,?,?,?)""",
                        (cid, yr, res["driver"], res.get("team_id",""),
                         res.get("position"), res.get("grid"),
                         res.get("status",""), res.get("points",0.0)))

        cur.executemany("""INSERT INTO race_stints
                           (circuit_id,year,driver,lap,compound,tyre_age,stint,
                            position,lap_time_s,pit_in,pit_out,is_accurate)
                           VALUES (?,?,?,?,?,?,?,?,?,?,?,?)""",
                        [(cid, yr, s["driver"], s["lap"], s["compound"],
                          s["tyre_age"], s["stint"], s["position"],
                          s["lap_time_s"], int(s["pit_in"]), int(s["pit_out"]),
                          int(s["is_accurate"])) for s in ses["stints"]])

        cur.executemany("""INSERT INTO pit_stops
                           (circuit_id,year,driver,lap,duration_s,compound)
                           VALUES (?,?,?,?,?,?)""",
                        [(cid, yr, p["driver"], p["lap"], p["duration"],
                          p["compound"]) for p in ses["pit_stops"]])

    con.commit()
    con.close()
    print(f"[DB] Done — {DB_PATH}")


def build_db_from_json():
    """Rebuild SQLite from already-saved JSON files (no network fetch)."""
    all_data, all_results = [], []
    for f in (DATA_DIR / "circuits").glob("*.json"):
        cid_year  = f.stem
        tyre_file = DATA_DIR / "tyres"   / f"{cid_year}_tyres.json"
        ses_file  = DATA_DIR / "sessions"/ f"{cid_year}_session.json"
        if not tyre_file.exists() or not ses_file.exists():
            continue
        circuit = json.loads(f.read_text())
        tyres   = json.loads(tyre_file.read_text())
        session = json.loads(ses_file.read_text())
        all_data.append({"circuit": circuit, "tyres": tyres, "session": session})
        all_results.append(session["results"])

    d_file = DATA_DIR / "drivers" / "drivers.json"
    t_file = DATA_DIR / "drivers" / "teams.json"
    drivers = json.loads(d_file.read_text()) if d_file.exists() else []
    teams   = json.loads(t_file.read_text()) if t_file.exists() else []
    build_database(all_data, drivers, teams)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="F1 PitWall data pipeline")
    parser.add_argument("--year",     type=int,  nargs="+", default=DEFAULT_YEARS,
                        help="Season year(s) to fetch")
    parser.add_argument("--circuit",  type=str,  nargs="+", default=None,
                        help="Circuit short id(s) to fetch (e.g. monza spa)")
    parser.add_argument("--db-only",  action="store_true",
                        help="Skip fetch, rebuild DB from existing JSONs")
    parser.add_argument("--list-circuits", action="store_true",
                        help="Print available circuit names and exit")
    args = parser.parse_args()

    if args.list_circuits:
        print("\nAvailable circuits:")
        for gp, cid in DEFAULT_CIRCUITS.items():
            print(f"  {cid:<14} ({gp})")
        return

    if args.db_only:
        build_db_from_json()
        return

    # Filter circuits if --circuit was specified
    target_circuits = {}
    if args.circuit:
        for gp, cid in DEFAULT_CIRCUITS.items():
            if cid in args.circuit or gp.lower() in [c.lower() for c in args.circuit]:
                target_circuits[gp] = cid
        if not target_circuits:
            print(f"[ERROR] No circuits matched: {args.circuit}")
            print("Run with --list-circuits to see valid names.")
            sys.exit(1)
    else:
        target_circuits = DEFAULT_CIRCUITS

    all_data, all_results = [], []
    total = len(target_circuits) * len(args.year)
    done  = 0

    for year in args.year:
        for gp_name, circuit_id in target_circuits.items():
            done += 1
            print(f"\n[{done}/{total}] {year} {gp_name} ({circuit_id})")

            # Skip if already fetched
            ses_file = DATA_DIR / "sessions" / f"{circuit_id}_{year}_session.json"
            cir_file = DATA_DIR / "circuits" / f"{circuit_id}_{year}.json"
            if ses_file.exists() and cir_file.exists():
                print("  Already cached — skipping fetch")
                circuit = json.loads(cir_file.read_text())
                tyres   = json.loads((DATA_DIR / "tyres" / f"{circuit_id}_{year}_tyres.json").read_text())
                session = json.loads(ses_file.read_text())
                all_data.append({"circuit": circuit, "tyres": tyres, "session": session})
                all_results.append(session["results"])
                continue

            result = fetch_race(gp_name, circuit_id, year)
            if result is None:
                continue

            # Write JSON files
            cir_file.write_text(json.dumps(result["circuit"], indent=2))
            (DATA_DIR / "tyres"   / f"{circuit_id}_{year}_tyres.json").write_text(
                json.dumps(result["tyres"], indent=2))
            (DATA_DIR / "sessions"/ f"{circuit_id}_{year}_session.json").write_text(
                json.dumps(result["session"], indent=2))

            print(f"  Compounds fitted: {[s['compound'] for s in result['tyres']['compounds']]}")
            print(f"  Pit loss: {result['circuit']['pit_loss_seconds']}s  "
                  f"Base lap: {result['circuit']['base_lap_time']}s  "
                  f"Laps: {result['circuit']['total_laps']}")

            all_data.append(result)
            all_results.append(result["results"])

    if not all_data:
        print("\n[WARN] No data was fetched.")
        return

    # Build drivers/teams from all sessions
    print("\n[INFO] Building drivers & teams index...")
    drivers, teams = build_drivers_teams(all_results)
    (DATA_DIR / "drivers" / "drivers.json").write_text(json.dumps(drivers, indent=2))
    (DATA_DIR / "drivers" / "teams.json").write_text(json.dumps(teams, indent=2))
    print(f"  {len(drivers)} drivers, {len(teams)} teams")

    build_database(all_data, drivers, teams)

    print(f"\n=== Pipeline complete ===")
    print(f"  Sessions fetched : {len(all_data)}")
    print(f"  Drivers indexed  : {len(drivers)}")
    print(f"  Teams indexed    : {len(teams)}")
    print(f"  Database         : {DB_PATH}")
    print(f"  JSON data        : {DATA_DIR}/")


if __name__ == "__main__":
    main()
