# LPC 2026

Collect resources, build bases, and construct workers. Your program sends one action per worker each turn, then receives the new state of the world. The team with the most gold in its bank after 480 turns wins.

## How to play

### Your team

You start with one base, one worker nearby, and 15 kg of iron in your bank. All your workers and bases share that bank.

The default map is 50 by 50 tiles. Choose an action for each worker every turn.

A base, a worker, and a nearby gold deposit.

Team bank: 0 kg gold, 15 kg iron

[View the game board example](https://lpc.sh/help?rulesVersion=lpc2026#help-start)

### Move

Move one tile in any of eight directions, including diagonals.

Entering a mountain or crater puts the worker on cooldown for 2 additional turns. Actions will be accepted but not executed during this travel time.

A mountain move includes entry and two additional turns of travel.

**Before**

The worker is next to a mountain.

**After**

The worker has entered the mountain.

The worker waits two more turns before it can act.

[View the game board example](https://lpc.sh/help?rulesVersion=lpc2026#help-movement)

### Mine

Stand on a gold or iron deposit to mine it. One mine action moves 1 kg into the worker's inventory. A worker can carry 8 kg in total.

One mining action takes 1 kg from the deposit.

**Before**

The worker stands on a gold deposit.

Deposit: 6 kg gold

Worker: 0 kg gold

**After**

The worker mines once.

Deposit: 5 kg gold

Worker: 1 kg gold

[View the game board example](https://lpc.sh/help?rulesVersion=lpc2026#help-mine)

### Deposit

Move onto one of your bases, then deposit. This moves all the worker's gold and iron into your shared bank.

Only banked gold counts toward your score.

Every friendly base connects to the same bank.

**Before**

The worker has returned to its base.

Worker: 4 kg gold, 2 kg iron

Bank: 0 kg gold, 15 kg iron

**After**

The worker deposits everything it carries.

Worker: 0 kg gold, 0 kg iron

Bank: 4 kg gold, 17 kg iron

[View the game board example](https://lpc.sh/help?rulesVersion=lpc2026#help-deposit)

### Trade

A worker can trade 3 kg of carried iron for 1 kg of gold. It can also trade 3 kg of carried gold for 1 kg of iron.

Trade wherever the worker stands. It uses the worker's inventory, not your bank.

3 kg of carried iron trades for 1 kg of gold.

[View the game board example](https://lpc.sh/help?rulesVersion=lpc2026#help-trade)

### Build a worker

Build a worker on the acting worker's tile. You do not need a base.

The first new worker costs 5 kg of banked iron. The next costs 6 kg, then 11, 17, 28, and so on. Each price is the sum of the previous two.

The new worker starts on the same tile.

**Before**

One worker is ready to build.

Bank: 15 kg iron

**After**

Both workers occupy the build tile.

Bank: 10 kg iron

[View the game board example](https://lpc.sh/help?rulesVersion=lpc2026#help-recruit)

### Build a base

A worker can build a base for 6 kg of banked iron. Stand alone on a plain tile with no base or resource deposit, outside enemy territory.

The new base gives your workers another place to deposit resources and claims nearby ore.

The builder stays on the new base.

**Before**

The builder stands alone on a plain tile.

Bank: 15 kg iron

**After**

A base now occupies the builder's tile.

Bank: 9 kg iron

[View the game board example](https://lpc.sh/help?rulesVersion=lpc2026#help-build-base)

### Pass

Do nothing for this turn.

The worker stays in place and keeps its inventory.

[View the game board example](https://lpc.sh/help?rulesVersion=lpc2026#help-pass)

## More about the rules

### Claim nearby ore

A base claims ore within Manhattan distance 2. Your workers can mine ore in your territory or in unclaimed territory.

All teams that claim a deposit can mine it. Claims do not restrict movement.

Territories can overlap.

**One base**

The gold is claimed. The iron is unclaimed.

**Four bases**

All four teams can mine this gold deposit.

[View the game board example](https://lpc.sh/help?rulesVersion=lpc2026#help-territory)

### Explore the map

Your workers and bases reveal tiles within Manhattan distance 3. Mountains and craters do not block vision.

Manhattan distance counts horizontal and vertical steps. A diagonal tile is two steps away. Other teams' inventories and bank balances stay private.

Tiles within Manhattan distance 3 are visible.

[View the game board example](https://lpc.sh/help?rulesVersion=lpc2026#help-vision)

### Find gold and iron

Gold deposits outside the center start with 6 to 9 kg. Center deposits hold 12 to 18 kg. Iron deposits hold 2 to 4 kg. The number on a deposit is the amount left in kilograms.

When a deposit runs out, a new deposit of the same resource appears on an empty tile next turn if there is room.

The number on each deposit is its remaining ore in kilograms.

[View the game board example](https://lpc.sh/help?rulesVersion=lpc2026#help-ore)

### The rich center

The default map has a 16 by 16 center with richer gold deposits. It starts with 35% of the map's gold deposits.

Each new gold deposit has a 35% chance to appear in the center. It can appear there even if the old deposit was outside it. If the chosen area is full, the new deposit goes in the other area. If no empty tile is available, it waits. Match settings can change the amounts and map size shown here.

A small example map with a 3 by 3 rich center. The default map is 50 by 50 with a 16 by 16 center.

[View the game board example](https://lpc.sh/help?rulesVersion=lpc2026#help-center)

## Conflicts and resolution

### When workers want the same ore

Several workers can mine the same deposit in one turn. If there is not enough ore for everyone, the worker with the longest current run of eligible mine actions on that tile mines first. Next comes the worker that has stood on the tile longest. A stable random order breaks any remaining tie.

A worker keeps its mining run if it tries to mine but another worker takes the last ore. Passing, choosing another action, or trying to mine when the worker is not eligible ends the run. Leaving the tile resets both the mining run and time spent standing there.

### What happens during a turn

Each worker performs only its submitted action. The game handles movement before building, mining, trading, and depositing. A new base can claim ore before workers mine that turn. Iron deposited this turn can pay for construction on a later turn.

The match seed chooses an initial team order for construction. After each turn, the first team moves to the back. Teams keep their place even when they do not build. When opposing base sites are within Manhattan distance 2, the earlier team builds first.

Within your team, worker and base construction spend bank resources in the order the server accepted the actions. Worker ID breaks equal acceptance times. Failed construction spends nothing.

1. Replace depleted deposits that are due to return.

2. Move workers.

3. Build workers and bases.

4. Mine ore.

5. Trade carried resources.

6. Deposit carried resources.

### When teams finish with the same gold

The team with more banked gold places higher. If banked gold is equal, compare banked iron, carried gold, carried iron, base count, then worker count. Use each value only if the previous values are equal.

Teams still tied share a place. Two teams sharing first place means the next team is third.

## Set up your bot

### Create a practice match

Log in on the match page with your team key. Choose a seed and opponents, then select Start game. Zero opponents lets you test alone. Download all the files for your language below and run the command for your shell.

The starter connects to your current match. It does not create one. For ranked play, the organizers assign your lobby. Run one client per team and restart it for each new match.

1. Edit the strategy in your chosen language. The starter passes for each of your workers each turn. Write your strategy in the strategy function.

2. Watch your workers in the browser. After the match finishes, select it under Replay or current match.

### JavaScript

Install Node.js 22.14 or newer. Check with node --version.

- [bot_template.js](https://lpc.sh/clients/js/bot_template.js): Strategy starter
- [bot_client.js](https://lpc.sh/clients/js/bot_client.js): Connects your strategy to the server
- [package.json](https://lpc.sh/clients/js/package.json): Node package configuration

1. Save bot_template.js, bot_client.js, and package.json in the same folder.
2. Write your strategy in calculateActions in bot_template.js. Use identity.teamId to find your workers.
3. Game helpers are below the helper banner. Give state helpers the LPC view and give pathTo its map. The starter includes editor types for state, actions, and helpers.

#### CMD

```
set "API_KEY=your-team-key"
set "LUCID_API_BASE_URL=https://lpc.sh"
npm start
```

#### PowerShell

```
$env:API_KEY = 'your-team-key'
$env:LUCID_API_BASE_URL = 'https://lpc.sh'
npm start
```

#### Bash

```
API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' npm start
```

#### Zsh

```
API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' npm start
```

#### Fish

```
env API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' npm start
```

### TypeScript

Install Node.js 22.14 or newer. Check with node --version. Run npm install in the client folder to install the type checker.

- [bot_template.ts](https://lpc.sh/clients/ts/bot_template.ts): Typed strategy starter
- [bot_client.ts](https://lpc.sh/clients/ts/bot_client.ts): Connects your strategy to the server
- [package.json](https://lpc.sh/clients/ts/package.json): Node package configuration
- [tsconfig.json](https://lpc.sh/clients/ts/tsconfig.json): TypeScript configuration

1. Save bot_template.ts, bot_client.ts, package.json, and tsconfig.json in the same folder.
2. Write your strategy in calculateActions in bot_template.ts. Use import type for types and keep .ts extensions in imports. Use TypeScript syntax that Node can strip. Async strategies are supported.
3. Game helpers are below the helper banner. They accept gameState.view and its map, or your own objects with the fields each helper needs.

#### CMD

```
set "API_KEY=your-team-key"
set "LUCID_API_BASE_URL=https://lpc.sh"
npm start
```

#### PowerShell

```
$env:API_KEY = 'your-team-key'
$env:LUCID_API_BASE_URL = 'https://lpc.sh'
npm start
```

#### Bash

```
API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' npm start
```

#### Zsh

```
API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' npm start
```

#### Fish

```
env API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' npm start
```

#### Check types

```
npm run check
```

### Python

Install Python 3.9 or newer. You do not need extra packages.

- [bot_template.py](https://lpc.sh/clients/python/bot_template.py): Strategy starter
- [bot_client.py](https://lpc.sh/clients/python/bot_client.py): Connects your strategy to the server

1. Save bot_template.py and bot_client.py in the same folder.
2. Write your strategy in calculate_actions in bot_template.py. Use identity['teamId'] to find your workers.
3. Game helpers are below the helper banner. Use mars_view to get the typed LPC view, then give state helpers that view and give path_to its map.

#### CMD

```
set "API_KEY=your-team-key"
set "LUCID_API_BASE_URL=https://lpc.sh"
py -3 bot_template.py
```

#### PowerShell

```
$env:API_KEY = 'your-team-key'
$env:LUCID_API_BASE_URL = 'https://lpc.sh'
py -3 bot_template.py
```

#### Bash

```
API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' python3 bot_template.py
```

#### Zsh

```
API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' python3 bot_template.py
```

#### Fish

```
env API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' python3 bot_template.py
```

### Java

Install JDK 21 or newer and Maven 3.9 or newer. Run java -version and mvn -version. Both must show Java 21 or newer. Maven downloads Gson on the first run.

- [Main.java](https://lpc.sh/clients/java/Main.java): Strategy starter
- [BotClient.java](https://lpc.sh/clients/java/BotClient.java): Connects your strategy to the server
- [pom.xml](https://lpc.sh/clients/java/pom.xml): Maven build configuration

1. Save all three files in one folder.
2. Write your strategy in calculateActions in Main.java. Use the team ID in identity to find your workers.
3. BotClient parses state into records. Use state.view().units() to read workers. Game helpers are below the helper banner; give grid helpers state.view().map() and territory helpers state.view().bases(). You can write your own CoordinateGrid.

#### CMD

```
set "API_KEY=your-team-key"
set "LUCID_API_BASE_URL=https://lpc.sh"
mvn -q compile exec:java
```

#### PowerShell

```
$env:API_KEY = 'your-team-key'
$env:LUCID_API_BASE_URL = 'https://lpc.sh'
mvn -q compile exec:java
```

#### Bash

```
API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' mvn -q compile exec:java
```

#### Zsh

```
API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' mvn -q compile exec:java
```

#### Fish

```
env API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' mvn -q compile exec:java
```

### Rust

Use the current stable release of Rust and Cargo. Cargo downloads packages the first time you run the starter.

- [Cargo.toml](https://lpc.sh/clients/rust/Cargo.toml): Cargo build configuration
- [Cargo.lock](https://lpc.sh/clients/rust/Cargo.lock): Package versions; keep beside Cargo.toml
- [main.rs](https://lpc.sh/clients/rust/src/main.rs): Strategy starter; save as src/main.rs
- [bot_client.rs](https://lpc.sh/clients/rust/src/bot_client.rs): Client helper; save as src/bot_client.rs

1. Save Cargo.toml and Cargo.lock in your client folder. Save the two .rs files in a src subfolder.
2. Write your strategy in calculate_actions in src/main.rs. Use the team ID in identity to find your workers.
3. BotClient parses state into structs. Match the game variant, then read the typed view. Game helpers are below the helper banner; give them the view map and bases. Your own grid can implement CoordinateGrid.

#### CMD

```
set "API_KEY=your-team-key"
set "LUCID_API_BASE_URL=https://lpc.sh"
cargo run --release
```

#### PowerShell

```
$env:API_KEY = 'your-team-key'
$env:LUCID_API_BASE_URL = 'https://lpc.sh'
cargo run --release
```

#### Bash

```
API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' cargo run --release
```

#### Zsh

```
API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' cargo run --release
```

#### Fish

```
env API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' cargo run --release
```

### C++

Install a C++20 compiler, CMake 3.20+, and libcurl development files with HTTPS support. On Ubuntu: sudo apt install g++ cmake libcurl4-openssl-dev. On macOS: install Xcode command-line tools and brew install cmake curl. On Windows: use Visual Studio C++ tools and vcpkg install curl, then pass its CMake toolchain file when configuring. CMake downloads nlohmann/json if it is not installed.

- [bot_template.cpp](https://lpc.sh/clients/cpp/bot_template.cpp): Strategy starter
- [bot_client.hpp](https://lpc.sh/clients/cpp/bot_client.hpp): Connects your strategy to the server
- [game_helpers.hpp](https://lpc.sh/clients/cpp/game_helpers.hpp): Optional game helpers
- [CMakeLists.txt](https://lpc.sh/clients/cpp/CMakeLists.txt): CMake build configuration

1. Save all four files in one folder. Configure with cmake -S . -B build -DCMAKE_BUILD_TYPE=Release, then build with cmake --build build --config Release. Rebuild after editing your strategy.
2. Write calculate_actions in bot_template.cpp. State and actions use nlohmann::json and the field names in the rules. Use identity.at("teamId") to identify your workers. Optional helpers in game_helpers.hpp accept the view map and bases.

#### CMD

```
set "API_KEY=your-team-key"
set "LUCID_API_BASE_URL=https://lpc.sh"
.\build\bin\lpc-bot.exe
```

#### PowerShell

```
$env:API_KEY = 'your-team-key'
$env:LUCID_API_BASE_URL = 'https://lpc.sh'
.\build\bin\lpc-bot.exe
```

#### Bash

```
API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' ./build/bin/lpc-bot
```

#### Zsh

```
API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' ./build/bin/lpc-bot
```

#### Fish

```
env API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' ./build/bin/lpc-bot
```

### Go

Install Go 1.22 or newer. No external packages are needed.

- [bot_template.go](https://lpc.sh/clients/go/bot_template.go): Strategy starter
- [bot_client.go](https://lpc.sh/clients/go/bot_client.go): Connects your strategy to the server
- [game_helpers.go](https://lpc.sh/clients/go/game_helpers.go): Optional game helpers
- [go.mod](https://lpc.sh/clients/go/go.mod): Go module configuration

1. Save all four files in one folder.
2. Write calculateActions in bot_template.go. Use identity.TeamID to identify your workers. State has typed fields; nil worker Inventory means it is hidden. Optional helpers in game_helpers.go accept the view map and bases.

#### CMD

```
set "API_KEY=your-team-key"
set "LUCID_API_BASE_URL=https://lpc.sh"
go run bot_template.go bot_client.go game_helpers.go
```

#### PowerShell

```
$env:API_KEY = 'your-team-key'
$env:LUCID_API_BASE_URL = 'https://lpc.sh'
go run bot_template.go bot_client.go game_helpers.go
```

#### Bash

```
API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' go run bot_template.go bot_client.go game_helpers.go
```

#### Zsh

```
API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' go run bot_template.go bot_client.go game_helpers.go
```

#### Fish

```
env API_KEY='your-team-key' LUCID_API_BASE_URL='https://lpc.sh' go run bot_template.go bot_client.go game_helpers.go
```

## API and reference

### Practice opponents

Practice supports zero to nineteen opponents. Novice collects nearby resources and returns full loads. Beginner remembers explored terrain and searches for gold. Intermediate remembers terrain, compares travel and return costs, builds up to three workers, and can build a second base. It stops investing halfway through the match and returns to bank before time runs out.

A seed repeats the initial map only with the same settings and roster. Actions and generated bot IDs can affect the result.

### Inspect and follow workers

A count badge marks workers that share a visible tile. Select the tile to inspect its workers. Select Follow unit to follow a worker with the camera. The inspector keeps the selected tile.

### Game-state helpers

The downloaded clients include helpers for finding a path, checking territory, and identifying bases and ore. They use the player view and its map, not the enclosing response. Use gameState.view for state helpers and gameState.view.map for pathTo.

isClaimed returns all visible claiming teams because territories can overlap. A hidden base can still claim a tile. isBase returns a visible base owner or null. isOre returns gold or iron for a visible deposit with stock, otherwise null. canMoveOnto rejects unknown terrain and enemy bases; pass your team ID to allow friendly bases. Workers do not block movement.

pathTo finds a route across eight directions using visible movement costs. It excludes the start and includes the destination. An empty path means the start is the destination. Null means no known route. Supply bases and your team ID to account for enemy bases. The default obstacle cost is three. Use the match setting if it differs.

A route includes the time until a worker can act again after entering an obstacle. Wait until view.turn reaches worker.readyOnTurn, then check the route against the new view. Actions sent during cooldown do not queue.

Each starter provides state and action types. Java and Rust parse state into records or structs; JavaScript provides editor types, and Python uses typed dictionaries. TypeScript accepts objects with only the fields each helper needs. Java and Rust accept your own CoordinateGrid implementation and take known bases separately for territory checks. Helpers stay below the strategy function in your bot file. Python and Rust use snake_case names and None.

### Submit actions

Use your authenticated team ID to select your workers. Do not rely on roster order. Return worker IDs mapped to individual action objects. Submit fresh actions each turn. Use pass to tell a worker to do nothing.

LPC practice advances when you submit. Timed ranked matches normally advance every second. The full rules explain action payloads, accepted turns, and submission receipts.

### Connection and recovery

Keep your team key private. The client stays with the match it registers for and exits when that match finishes or is cancelled.

Run one bot session per team key and match. Browser viewing stays available. Registration returns an x-bot-session header; custom clients must keep it and send it with later requests. Another session gets HTTP 409. After a disconnect, the session has 60 seconds to reconnect before another bot can take over.

The helper retries eligible reads and match-bound submissions up to three attempts. A retry preserves the body, submission ID, and revisions. The starter selects a match before registering, then retries registration with the same match ID and random session token. Requests time out after five seconds. JavaScript, TypeScript, and Python use turn streams that reconnect after ten seconds without data. Java and Rust poll at turn deadlines in timed matches and every 100 milliseconds otherwise.

After a reconnect, the client fetches current state and skips missed strategy calls. Strategy errors and rejected actions are logged, then the bot submits no actions for that turn and continues. Late actions trigger a warning and a refresh to the newest state. Authentication and session ownership errors stop the bot.

If there is no current match, create practice or ask the organizers for a ranked lobby. Fix your payload for invalid_action. Check your revision counters for stale_action_revision. Do not reuse a submission ID with different content for submission_conflict.

Each batch accepts at most 100 worker actions and a JSON body up to 1 MiB. Larger bodies return HTTP 413. Invalid batch sizes return HTTP 400. The full rules contain the custom transport contract.

## Full rules

# Lucid Programming Competition - Rules 2026

Teams collect resources, build bases, and recruit workers. Your program submits one action per worker for each turn. After each turn, you receive an updated view of the world visible to your team.

Resource quantities are measured in kilograms (kg). Inventory, bank, cost, and deposit amounts in the API use the same numeric values: an amount of 5 means 5 kg.

## Objective and starting state

Collect gold and deposit it at your bases. Final score is the gold remaining in your team's shared bank. Carried gold does not contribute to that score; spending banked gold reduces the amount left to score. When banked gold is equal, compare these values in order, with higher values winning:

1. Banked iron.
2. Total gold carried by all of the team's workers.
3. Total iron carried by all of the team's workers.
4. Number of bases.
5. Number of workers.

Only move to the next criterion when the previous values are equal. If every value matches, teams share placement, skipping the following placements (1, 1, 3). There is no lottery or team-ID tie-breaker. The reported score remains banked gold in kg; the other values determine placement only. These rules rank teams within a match, not across the competition's overall leaderboard.

The default match has 16 teams on a 50 by 50 map and ends after 480 turns. Each team starts with one base, one worker nearby, and a shared bank with unlimited storage containing 0 kg of gold and 15 kg of iron. All friendly bases use this bank. Workers carry up to 8 kg total. Gold deposits start with 6–9 kg outside the rich center and 12–18 kg inside it; iron deposits hold 2–4 kg; the initial layout allocates three gold and two iron deposits per team.

Coordinates use `x` increasing east and `y` increasing south. The `cells` array stores the map one row at a time, from left to right and then top to bottom. Coordinates and array indices start at zero. To find cell `(x, y)`, use `cells[y * width + x]`. For example, on a 50-wide map, `(2, 1)` is `cells[52]`: skip the first 50 cells, then move two cells into the second row. Workers and bases are separate lists; multiple workers can occupy one cell.

## Worker actions

Send actions under the worker's `unitId`, not a base ID or team ID. Each submitted action is consumed for its accepted turn, including actions whose requirements are not met. Submit fresh actions each turn.

- **Move:** `{"type":"move","direction":"north"}`. Directions are `north`, `north-east`, `east`, `south-east`, `south`, `south-west`, `west`, and `north-west`.
  - Ordinary movement takes one turn, including diagonals.
  - Entering a mountain or crater puts the worker on cooldown for 2 additional turns. Actions will be accepted but not executed during this travel time. The worker enters the destination immediately. Ignored actions are consumed, not queued. After those two turns, submit a new action. The configurable `obstacleTraversalCost` includes the entry turn; a cost of 1 adds no cooldown. Each visible worker has `readyOnTurn`: it can act when the current view's `turn` is at least this value.
  - Workers cannot leave the map or enter an enemy base.
  - Friendly and opposing workers may overlap. Worker occupancy does not block movement, and simultaneous movement to the same tile is allowed.
- **Mine:** `{"type":"mine"}` takes 1 kg from the gold or iron deposit at the worker's position, if stock and inventory space remain. Deposits within two Manhattan steps of a base are claimed: only claiming teams may mine them. If friendly and enemy claims overlap, either claiming team may mine. Unclaimed deposits are available to everyone.
  - Multiple workers can mine the same deposit in a turn. If stock is insufficient for all eligible miners, workers with the longest current streak of eligible mine actions on that tile mine first. An eligible attempt counts even if another worker takes the remaining ore. Waiting, taking another action, or an ineligible mine attempt ends the streak. For equal mining streaks, workers that have stayed on the tile longest mine first. Leaving and returning resets both mining and occupancy priority. Workers tied on both use a seeded random tie-breaker. Submission time does not determine mining priority.
- **Deposit:** `{"type":"deposit"}` transfers all carried gold and iron to the shared bank when standing on a friendly base.
- **Trade:** `{"type":"trade","from":"iron"}` exchanges 3 kg of carried iron for 1 kg of gold. `from: "gold"` exchanges 3 kg of carried gold for 1 kg of iron. Trading uses the worker's inventory, not the bank, and does not require a base.
- **Spawn:** `{"type":"spawn"}` creates a worker on the acting worker's tile, spending from the bank. It does not require standing on a base. The first two recruited workers cost 5 kg and 6 kg of iron; each subsequent price is the sum of the previous two (5, 6, 11, 17, … kg). Starting workers do not count.
- **Build base:** `{"type":"build-base"}` costs 6 kg of banked iron. The builder must stand on empty terrain with no base and no other worker, including friendly workers. Mountains, craters, and resource tiles are not valid sites. The site must be more than two Manhattan steps from every enemy base; friendly bases do not restrict construction. New bases affect mining eligibility in the same turn.
- **Pass:** `{"type":"pass"}` does no work for the accepted turn. A worker with no accepted action also idles. Request-level passing is described below.

## Turn resolution

Each turn resolves in this order:

1. Due depleted deposits respawn.
2. Workers move.
3. Workers spawn new workers or build bases.
4. Workers mine.
5. Workers trade.
6. Workers deposit.
7. Built-in bot memory updates, and the turn counter advances.

Construction happens before deposits: iron deposited this turn cannot pay for construction until a later turn. The match seed determines an initial team order for construction. After each turn, the first team moves to the back of that order. All teams keep their place in the rotation, even when they do not submit a construction action. If new opposing bases would claim each other's sites, the team earlier in that turn's order builds first.

Within a team, construction consumes its shared bank in action acceptance-time order, with equal times resolved by worker ID. This applies to both building workers and building bases. An unsuccessful construction action spends no resources and does not stop other workers from building. Mining scarcity uses the seeded selection described above. A worker performs only its submitted action.

## Resource respawning

When a deposit is depleted, a replacement of the same resource is scheduled for the start of the following turn. It receives a newly seeded amount (6–9 kg of outside gold, 12–18 kg of center gold, or 2–4 kg of iron). Its location is selected reproducibly from empty terrain without a worker or base. Each gold replacement independently has a 35% chance (configurable) of spawning in the center and otherwise spawns outside it, regardless of where it was mined. If the chosen zone has no eligible cell, it uses the other zone. The emptied original position can be reused if no other eligible cell exists in the chosen zone.

Placement prefers sites at least two movement steps from the old site and other resource sites (diagonal distance counts as one step). If none meet both spacing preferences, it relaxes spacing from other resources, then spacing from the old site. If no valid site exists, respawning waits until a later turn.

## Vision and private information

Friendly workers and bases reveal tiles within Manhattan distance 3 by default. Mountains and craters do not block vision. A worker or base on either obstacle also reveals all eight neighboring cells.

A live player view contains:

- `map`: visible terrain and resource amounts, with unseen playable cells marked `hidden`.
- `units`: workers on visible cells. Your workers include their inventory; opponents' inventories are omitted.
- `bases`: bases on visible cells.
- `scores`: banked gold totals for all teams, including teams outside your vision.
- `teamResources`: banked gold and iron for your team only.
- `baseResourceDeposits`: deposits at your bases only.
- `turn`, `warnings`, `info`, and `effects`.

The enclosing state response includes `match` and `serverTimeMs`, and may include `turnEndsAtMs`. Pending matches may omit `view`. Do not infer hidden terrain or inventories from missing data. Completed replays available to participants contain the full map and resources, plus `placements` calculated using the tie-break criteria for the displayed turn; admin live views are also authoritative/full-state. Live player access is different from both.

## Actions and timing

The downloaded helpers take worker IDs mapped to individual action objects:

```json
{
  "your-worker-id": { "type": "mine" }
}
```

This only mines successfully when the worker stands on an eligible deposit and has inventory space. Submit a new action each turn; a sequence such as moving, mining, returning, and depositing belongs in your strategy logic.

Helpers submit to the match selected at registration. Custom clients can use `PUT /api/team/matches/current/actions` or `PUT /api/team/matches/:matchId/action` to stay bound to a specific match:

```json
{
  "submissionId": "a-new-unique-id-for-this-request",
  "submissions": [
    {
      "entityId": "your-worker-id",
      "revision": 1,
      "action": { "type": "mine" }
    }
  ]
}
```

- A submission replaces the worker's pending action for its accepted turn. It does not append future work.
- Omitting a worker leaves any action already accepted for that turn intact. Returning `{}` does not cancel all pending actions. An empty array is not a valid worker action; submit `{"type":"pass"}` to replace a pending action with an idle action.
- At the raw batch endpoint, `{"advanceOnly":true}` passes without replacing actions. It advances client-action practice, not arbitrary ranked/timed turns.
- Malformed LPC actions reject the batch with HTTP 400 and code `invalid_action`. Correct the payload before resubmitting. No actions are replaced and no client-action turn advances. This does not predict whether a valid action will succeed on the map.
- Revisions are non-negative safe integers (at most 9,007,199,254,740,991), scoped to one match, team, and entity. Every replacement, including a pass action, must use a revision strictly greater than that entity's highest accepted revision. Consuming actions or advancing turns does not reset the counter; a new match does.
- For a new submission, if any action has a stale or equal revision, the entire batch is rejected with HTTP 409 and code `stale_action_revision`. No actions change and no client-action turn advances. Successful acknowledgments report the accepted revision and `acceptedForTurn` for each entity.
- Registration and team-state responses include `actionRevisions`, a map of entity IDs to their highest accepted revisions. Helpers initialize their counters from registration, so restarting in the same match continues counting. Custom clients must do the same; do not reset to 1 mid-match.
- Use a unique `submissionId` for every new request, including empty batches and `{"advanceOnly":true}`. It must contain 1–128 letters, digits, hyphens, or underscores; UUIDs work. IDs are scoped to your authenticated team across matches. The starter clients generate them.
- To recover a lost acknowledgment, resend the same ID and exact revisions/actions to the same match-specific endpoint. The server returns the original acknowledgment (including `submissionId`) without replacing actions or triggering another turn, even after completion or a server restart. Object key order does not matter; array order does. Reusing an accepted ID for a different payload or explicit match returns HTTP 409, `submission_conflict`.
- Receipts are retained until the associated match is deleted. The acknowledgment confirms acceptance, not completion of a turn. If the server fails after accepting a request but before advancing a client-action match, the actions remain pending and a retry only returns the receipt. Check state; use a new intentional submission/pass to advance. Timed matches continue on their server schedule.
- To retry registration safely, first read the current match, choose a random UUID for `x-bot-session`, and send the selected match ID in `x-bot-match-id` on registration. Reuse both headers for every retry. Registration stays bound to that match even if a new match becomes current.
- Register one bot session per match and API key. Keep the `x-bot-session` response header and send it with state, action, and event-stream requests. All action routes require it, including requests authenticated by a browser cookie. A competing session receives HTTP 409 with code `bot_session_conflict`. Browser viewing and the Join button remain available. An open bot stream holds the session; after a disconnect, the same token has 60 seconds to reconnect before another bot can claim it. Completion releases the session. Sessions reset when the server restarts.
- Keep one helper process per team. Calling a helper again generates a new submission ID; it is not a retry. Helpers retry eligible transport failures at most three times with the original serialized body, ID, and revisions; they do not recalculate actions for a retry. Registration retries use the same random session token and selected match ID. Other writes through a mutable current-match endpoint are not automatically retried. See `/help` for timeout and reconnect limits. Requests without an ID (including raw `pass`) have no deduplication guarantee. Repeating a revision under a new ID is still a stale-revision error; do not automatically resubmit rejected work with a higher revision.
- In scheduled modes, acceptance before the current deadline targets `match.turn + 1`; acceptance at or after that deadline targets `match.turn + 2`. Inspect `acceptedForTurn` in the acknowledgement. The server decides acceptance under its match lock; sending before a local-clock deadline does not guarantee acceptance before the server deadline.

Turn modes:

- `timed`: the server advances on its timer, normally every 1000ms, even if a client is idle.
- `client-action`: contestant LPC practice advances when the human client submits actions or a pass request. Without submissions, the turn does not advance.
- `multi-client-action`: the server can advance once every controllable entity has an accepted action, subject to a minimum turn duration (200ms); a 1000ms fallback timer prevents waiting indefinitely. Readiness is per entity, not one request per team. This mode is available through the ranked-lobby API; ordinary admin round creation uses timed matches.

The organizers' default template determines contestant practice mechanics. LPC practice uses client-action turns; Solar Orbitor practice uses timed turns. The starter clients register for the team's existing current match; they do not create it. See `/help` for setup and connection behavior.

The default mountain terrain weight is 20%; crater terrain weight is 10%.

## High gold area

A centered 16 by 16 rectangle contains richer gold deposits, each holding 12–18 kg, twice the normal amount range. Outside this rectangle, gold deposits hold 6–9 kg. When exact centering falls between cells, the rectangle is shifted toward the top/left.

Initially, 35% of generated gold deposits are assigned to the center, rounded to the nearest whole deposit. Each depleted gold deposit independently chooses its respawn zone using the same configured center percentage and receives an amount from its new zone's range. If one zone has too few eligible initial cells, placement uses remaining cells in the other zone.

The 35% center share concentrates gold in the rich rectangle, which covers 10.24% of the default map area. Visible deposits expose their remaining quantity in kg in `amount`, which decreases by 1 with each successful mining action.
