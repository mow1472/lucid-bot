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
2. **Distance fields.** Dijkstra over 8 directions. A normal tile costs 1. A mountain or crater costs the obstacle cost, which the bot learns from `readyOnTurn` after its own obstacle moves (default 3). Enemy bases are impassable, and unseen tiles get a planning cost of 1.5. One reverse field from all friendly bases gives the trip home from any tile.
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

**Real server:** one practice match, seed `ah-1`, 16 teams, against 15 Intermediate bots, using the earlier parameters. We placed **1st with 406 gold**. The Intermediate bots scored 109–290.

### What is validated and what is not

- **Validated:**
  - The bot plays a full real match with no errors, about 10 ms per turn.
  - It won that one real match against Intermediate bots.
  - In the simulator, each variant beats the one before it, except that center vs early2 is within noise.
- **Simulator only:** the parameter tuning, the size of each improvement, and the gold-for-iron trade, which hasn't played a real match yet. Small simulator differences (|t| < 2) are noise.
- **Heuristic:**
  - the recruit value curve (`saturation`), base valuation, the exploration prior and the deposit staleness model;
  - the rule of 3 gold deposits per team, used to estimate how much gold the map holds;
  - the opponent behavior in the simulator.
- **Not yet measured:** the bot's performance against strong human-written bots, and its variance over many real matches.
