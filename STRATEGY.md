# Adaptive Hybrid bot

C++20 bot for LPC 2026. The strategy is in [adaptive_hybrid.hpp](adaptive_hybrid.hpp) and is called from [bot_template.cpp](bot_template.cpp). The official `bot_client.hpp`, `game_helpers.hpp` and root `CMakeLists.txt` are unchanged.

## Build and run

```powershell
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
$env:API_KEY = '<team key>'; $env:LUCID_API_BASE_URL = 'https://lpc.sh'
.\build\bin\lpc-bot.exe
```

Optional environment variables:

| Variable | Effect |
|---|---|
| `AH_VARIANT` | `full` (default), `bases`, `center`, `early2`, `baseline` |
| `AH_PARAMS` | Inline JSON or a JSON file path overriding any field of `ah::Params` |
| `AH_MEMORY_DIR` | Folder for map-memory snapshots, used when the bot restarts mid-match (default: current folder) |

## How it works

Each turn the `Brain` runs these steps:

1. **Memory.** Merges the visible view into a persistent map of terrain, deposits and enemy bases, and records the turn each tile was last seen. A deposit that is no longer visible is assumed less likely to still be there as time passes (`staleTau`). Every 10 turns the memory is saved to disk, so a restarted bot recovers its map.
2. **Distance fields.** Dijkstra over 8 directions. Route choice slightly favors tiles we haven't seen recently ("scenic routing"), so travel doubles as exploration. A normal tile costs 1. A mountain or crater costs the obstacle cost, which the bot learns from `readyOnTurn` after its own obstacle moves (default 3). Enemy bases are impassable, and unseen tiles get a planning cost of 1.5. One reverse field from all friendly bases gives the trip home from any tile.
3. **Investments.**
   - **Recruiting:** a recruit is worth `rate × e^(−workers/saturation) × turns left`.
   - **Bases:** a candidate site's value is the round trips it saves for known gold, plus gold expected to appear nearby (the rich-center prior), less the builder's travel time.
   - **Choice:** options are ranked by gold per kg of iron, and the best affordable one is taken. A builder heading to its site keeps 6 kg of iron reserved.
   - **Iron's value:** the best unaffordable option sets what iron is worth when mining it, so workers collect iron only while an investment needs it.
4. **Task assignment.**
   - **Candidate tasks:** each ready worker considers mining each known deposit, exploring any tile, or going home.
   - **Value:** a task's net value is gold gained minus turns used × `rate`. `rate` is the team's observed gold per worker-turn, blended with a prior.
   - **Deadline check:** a mining task is only feasible if the worker can travel, mine and deposit before the last turn.
   - **Assignment:** repeatedly assign the worker whose best task is worth most, then reserve that deposit's stock and that exploration area before picking the next worker. A bonus for keeping last turn's target prevents dithering between similar tasks.
5. **Endgame.** Any task whose gold can't be banked in time is infeasible, so workers come home on their own. Carried iron that no investment needs is traded 3:1 for gold at a base before depositing.

If anything throws, a fallback policy still submits valid actions: deposit if on a base, mine if on ore, otherwise pass.

## Benchmarking

`sim/` is an offline simulator that implements the published rules and feeds the strategy the same JSON the server sends. Build and run it like this:

```powershell
cmake -S sim -B sim/build; cmake --build sim/build --config Release
.\sim\build\bin\lpc-sim.exe --seeds 1:20 --variants baseline,early2,center,bases,full --opponents prospector:15 --csv out.csv
```

Bot types are `greed` (Novice), `wander` (Beginner) and `prospector` (Intermediate). For paired comparisons of parameter sets, run `python -I sim/sweep.py --configs configs.json --seeds 1:48`. The file maps each config name to its parameter overrides, and the first config is the reference. The simulator's map layout and practice bots are approximations, so use it to compare variants with each other, and confirm the winner with real practice matches.

### Results

**Simulator:** 20 seeds, us against 15 simulated Intermediate bots. This used the earlier parameters (`exploreWeight 0.5`, `workerRate 0.25`).

| Variant | Mean gold | SD | Min | Win % | Workers | Bases |
|---|---|---|---|---|---|---|
| baseline (mine and bank only) | 113 | 22 | 87 | 0 | 1 | 1 |
| early2 (+2 early recruits) | 277 | 44 | 219 | 5 | 3 | 1 |
| center (+center-weighted exploration) | 288 | 50 | 201 | 0 | 3 | 1 |
| bases (+dynamic bases) | 476 | 62 | 366 | 100 | 3 | 5.7 |
| full (+model-driven recruiting) | 551 | 80 | 418 | 90 | 4.2 | 5.5 |

**Tuning, round 1:** I swept 16 settings on seeds 1–24, then checked the best ones on held-out seeds 25–48. `exploreWeight 0.25` with `workerRate 0.35` scored mean 609, SD 51, min 493, against 571 for the old defaults. Setting `exploreWeight` to 1.0 was clearly harmful (mean 322).

**Calibration:** I matched the simulator to the real replay:
- starting bases as close as 3 tiles apart;
- craters in 2×2 and 3×3 blocks;
- about 26% obstacle tiles.

The simulator now gives us about 25% more gold than the real match did. Simulated Intermediate bots have a similar best score but a lower average (98 vs 185).

**Tuning, round 2:** I used `python -I sim/sweep.py`, which compares configs seed by seed. In this table, t is the difference divided by its standard error.

| Change | Gold vs reference | t | Seeds |
|---|---|---|---|
| Stronger center bias (`centerBoost` 2) | −54 | −4.5 | 1–32 |
| Shorter or longer deposit-memory decay (`staleTau` 30 or 120) | about −25 | about −2.2 | 1–32 |
| Gold-for-iron trading plus `depositLifetime` 150 (now the default) | **+68** | **4.6** | held-out 49–96 |
| `depositLifetime` 150 alone | +5 | 0.4 | 49–96 |

The gold-for-iron trade is the important part. When the next investment values iron at 3 gold per kg or more, a worker at a base trades carried gold 3:1 for iron. That funds roughly one extra worker: 4.9 vs 3.9.

The current defaults scored mean 611, min 410 and 100% wins on seeds 49–96, against mean 543 and min 368 for the round-1 defaults.

**Tuning, round 3 (robustness):** I tested each config against four opponent fields:
- 15 Intermediate bots;
- a Novice/Beginner/Intermediate mix;
- 4 copies of our own bot plus 11 Intermediate bots;
- a 10-team game.

The sweep tool takes the fields separated by `;`:

```powershell
python -I sim/sweep.py --configs configs.json --seeds 1:24 --opponents "prospector:15;greed:5,wander:5,prospector:5;ah:4,prospector:11;prospector:9"
```

| Change (pooled over 4 fields, 24 seeds each) | Gold vs reference | Worst field |
|---|---|---|
| `ironValueCap` 3 + `baseReach` 4 (now the default) | +1.7% ± 1.6 | −0.5% |
| `ironValueCap` 3 alone | +2.1% ± 1.5 | −0.7% |
| No gold-for-iron trading | **−6.6% ± 1.6** | −11% |

The current defaults came from the second scrimmage below rather than from simulator gains, which are within noise. Turning gold-for-iron trading off is clearly harmful.

**Tuning, round 4 (cutting walking time):** same four fields, 24 seeds each.

| Change | Pooled gold vs reference | Worst field |
|---|---|---|
| Scenic routing (`scenicBonus` 3) alone | +0.7% ± 1.4 | −3.2% |
| Stronger keep-target bonus (`stickiness` 2) alone | +3.1% ± 1.5 | +1.1% |
| **Both (now the default)** | **+5.2% ± 1.6** | **+1.6%** |

The combination gained +15, +9, +37 and +34 gold in the four fields. Picking the best of five configs overstates its gain, so I re-tested it on held-out seeds 25–48: +34, −15, +17 and −10 (pooled +1.2% ± 1.7). Over all 48 seeds it averages about +3%, with its clearest gains against Intermediate bots and against copies of our own bot. It's roughly even in the mixed and 10-team fields. Scenic routing chooses, among near-equal routes, the one that passes unseen or long-unseen tiles, so ordinary trips reveal gold. Distances used for deadline checks still use the true route cost. The stronger keep-target bonus stops scenic detours from making workers change targets.

**Where worker time goes** (simulator, 4 copies of our bot + 11 Intermediate, before round 4):

| Activity | Share of turns |
|---|---|
| Exploring | 26% |
| Walking to a known deposit | 26% |
| Mining | 26% |
| Walking home loaded | 13% |
| Depositing | 5% |

Abandoned trips (target lost or switched before arrival) are under 2% of turns. The strategy reports these counts through `Brain::turnUse()`, and the simulator prints them.

### Real matches

| Match | Field | Result |
|---|---|---|
| Practice `ah-1` (first version) | 15 Intermediate bots | **1st, 406 gold**. The bots scored 109–290. |
| Scrimmage 1 (gold-for-iron trading, `ironValueCap` 6) | 10 teams + 6 Intermediate bots | **3rd, 469 gold**. 1st had 583, 2nd had 539. |
| Scrimmage 2 (same version) | 9 teams + 7 Intermediate bots | **6th, 276 gold**. 1st had 374. |

What the replays showed (replays include every team's actions):
- **Scrimmage 1:** the leaders traded gold for iron 30–34 times and had their 3rd recruit by turn 22–40. We traded 13 times and recruited the 3rd at turn 91. Investing earlier and harder paid off.
- **Scrimmage 2:** we started near a corner. Iron worth 6 gold per kg sent workers after far-off iron, and exploration avoided the distant center, so our first gold was banked at turn 72. That led to the current defaults, `ironValueCap` 3 and `baseReach` 4.
- **Latency:** we missed 16 and 28 turns, against 9 for the fastest team; several of the misses were server-wide. Polling went from 100 ms to 30 ms, and a restart loop now survives network errors.

### What is validated and what is not

- **Validated:**
  - The bot plays full real matches with no errors, at 10–17 ms per turn.
  - It beats Intermediate bots: 1st in practice, and above all 13 Intermediate bots in both scrimmages.
  - Turning off gold-for-iron trading costs gold in every simulator field.
- **Simulator only:** most parameter tuning, and the current defaults, which haven't played a real match yet. Small simulator differences (|t| < 2) are noise.
- **Heuristic:**
  - the recruit value curve (`saturation`), base valuation, the exploration prior and the deposit staleness model;
  - the rule of 3 gold deposits per team, used to estimate how much gold the map holds;
  - the opponent behavior in the simulator.
- **Not yet measured:** how much results vary over many real matches, and how the current defaults do against human teams.
