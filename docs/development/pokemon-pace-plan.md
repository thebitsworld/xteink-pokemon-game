# Pokémon pace: how long the game takes

Decided on 2026-10-19, not implemented yet. How many reading hours it takes to beat every gym and to fill
the Pokédex, and the changes agreed to bring the Pokédex down to 0.5-1 year at an hour of reading a day.
Numbers come from `scripts/dev/simulate_pokemon_pace.py` (Monte Carlo over the rules in
`lib/Pokemon/PokemonGame.cpp`; run it from the repo root after a test build).

## Assumptions of the simulation

- Books of 8 hours, progress growing evenly; 1 XP per reading minute to the party leader.
- Wild encounters as today: a roll every 15 minutes at 2/5, guaranteed after 3 misses (~1.84 an hour).
  Every new species met is caught (optimistic for Mewtwo, which needs a Master Ball in practice);
  species met again are beaten for XP (level x 4).
- Road trainers (the planned feature from the crosslingua fork) on the same schedule as wild
  encounters, 10 XP for each win.
- One main Pokémon takes the gyms solo, challenging each when it is 5 levels above the gym's strongest
  member; gym XP stays level x 6.
- After the gyms, the player trains evolutions: the leader is the caught Pokémon closest to a level
  evolution that is still missing from the Pokédex. Stones and the Link Cable are used as soon as they
  drop (the drop already prefers what an owned Pokémon needs).

## Results (reading hours)

| | Gyms, E4, Champion | Pokédex p10 | median | p90 |
|---|---|---|---|---|
| Today's rules | ~10 | 344 | 565 | 1121 |
| Agreed changes (below) | ~10 | 162 | 192 | 280 |
| Agreed changes, never switching the leader | ~10 | 479 | 744 | 1102 |

What made the Pokédex slow: ~30-44 species of weight 1 (1 chance in 190-285 per encounter, even when
not caught yet), legendaries at 1/20 per encounter with Mewtwo only in the last 5% of a book, and 20
stone/Link Cable evolutions at one item per ~13 hours. Training evolutions instead of waiting for wild
middle/final stages cuts the time about four times.

## Battle XP and road trainers (agreed 2026-10-19: option D)

Today's battle XP (`battleVictoryXp`: opponent level x 6 for gym/E4/Champion, x 4 for wild) lets one
Pokémon solo every gym in ~6 hours of reading: from Koga on, the gyms' own XP lifts it past the next
gym, and it reaches level 100 at Bruno. Road trainers must NOT scale their XP with the party's level the
way the crosslingua fork does (level x 6 per member): that would finish the gyms in ~2 hours.

Agreed on 2026-10-19 (option D):

- Gym/E4/Champion: opponent level x 1, scaled by level difference.
- Wild: opponent level x 1, scaled by level difference.
- Scaling (Gen 5 style): XP x `((2*Lo + 10) / (Lo + Lp + 10))^2.5`, with `Lo` the opponent's level and
  `Lp` the player's. 1 at the same level; e.g. a level 24 Pokémon beating a level 14 one gets x0.56,
  a level 40 one beating a level 50 one x1.27.
- Road trainers: 10 XP for each win (fixed, not scaled), at half the wild encounter rate (agreed
  2026-10-19: a roll every 30 minutes at 2/5, guaranteed after 3 misses, ~0.92 a reading hour), teams
  of 1-3 at the party's average level -4..+2, and a win gives a random item with today's item-kind
  odds.

Simulated (median of 100 runs, one Pokémon soloing from level 5, challenging each gym 5 levels above its
strongest member; reading hours and the solo Pokémon's level after the fight):

| Gym | Strongest | Today (x6 / x4, trainers 10 XP at the wild rate) | Option above (trainers at half rate) |
|---|---|---|---|
| Brock | 14 | 3.2 h, lv 23 | 5.0 h, lv 19 |
| Erika | 29 | 4.2 h, lv 44 | 13.0 h, lv 35 |
| Koga | 43 | 6.0 h, lv 58 | 25.0 h, lv 49 |
| Giovanni | 50 | 6.0 h, lv 86 | 28.5 h, lv 56 |
| Bruno | 58 | 6.0 h, lv 100 | 34.0 h, lv 65 |
| Champion | 65 | 6.0 h, lv 100 | 34.3 h, lv 72 |

All four formulas compared with road trainers at half the wild rate (median of 100 runs; "battle
share" is how much of the solo Pokémon's XP up to level 100 came from battles):

| Formula | Erika | Giovanni | Champion | Level 100 after | Battle share |
|---|---|---|---|---|---|
| A, today: gym x6, wild x4 | 4 h, lv 44 | 6 h, lv 87 | 6 h, lv 100 | 6 h | 98% |
| B: gym x2 scaled, wild x4 | 7 h, lv 36 | 13 h, lv 60 | 14 h, lv 81 | 31 h | 78% |
| C: gym x2 scaled, wild x1 scaled | 12 h, lv 36 | 23 h, lv 59 | 25 h, lv 81 | 64 h | 54% |
| **D (agreed): gym x1 scaled, wild x1 scaled** | 13 h, lv 35 | 28 h, lv 56 | 34 h, lv 72 | 88 h | 36% |

Reading alone takes 139 h to level 100. Battle XP goes to the Pokémon that lands the KO and reading XP
to the party leader only, so a whole team of 3-4 around level 60-65 takes longer than the solo numbers:
roughly 50-70 hours of reading with D. The jump from Erika (29) to Koga (43) and the slow climb after it
come from the gym team data: whatever the formula, Koga to the Champion tends to fall in one sitting.

## HP between fights (agreed 2026-10-19)

Measured with the real battle engine (`lib/Pokemon`, 3000 fights per level; a party of 4 at one level,
the next member in when one faints, always the most damaging move, no switching for type, no items):
a road trainer fight (1-3 Pokémon at -4..+2) costs 1.6-1.8 full HP bars of one Pokémon, faints 1.3-1.5
of the party and is won 86-89% of the time; a wild fight costs 0.75 bars at level 20 but under 0.1
from level 45 (wild levels stop at 30). PP is never short: 5-7 PP per trainer fight against ~96 PP an
hour recovered by a party of 4 (1 PP per move every 10 minutes).

HP today is 1 HP a minute for each party member (`PokemonService::healPartyOnRead`), flat, so it falls
behind as max HP grows: with trainers at the wild rate a party of 4 would lack ~20% at level 45, ~40%
at 60 and ~50% at 75 (medicine included, ~76 HP an hour on average).

Agreed:
- Road trainers at half the wild rate (~0.92 a reading hour, see above).
- HP recovers `max(1 HP, 1.5% of max HP)` a minute: the same as today up to level ~35-45, and a
  fainted Pokémon is full again after ~67 minutes of reading, about the gap between two trainers.
  A party of 4 then recovers 2.3-2.4 times what it loses at levels 45-75 (1% would be 1.6-1.7 times,
  but a Pokémon fainted in one trainer fight would be only ~65% back by the next one).

## Agreed Pokédex changes (all in `lib/Pokemon/PokemonGame.cpp`)

1. A species not caught yet weighs x3 when a wild Pokémon is picked. Early on nearly every species is
   new, so little changes; late in the game the missing ones show up far more often.
2. Legendary chance per encounter 1/20 -> 1/10 (the 20 h / 50 h lifetime-reading conditions stay).
3. Mewtwo from 90% of a book instead of 95%.
4. Stones and the Link Cable: hourly chance 1/20 -> 1/10, guaranteed after 9 misses instead of 19.

No save change. The scripted random sequences in the Pokémon game tests need updating.

## Pending event queue: items straight to the bag (written down 2026-10-19, not decided)

The queue holds 16 events (`PENDING_EVENT_CAPACITY`; 16 is also the width of `inspectSnapshot()`'s
bitmask, so more needs a save format change). Today it gets ~5.6 events per reading hour: wild
encounters, medicine and TM/HM 1.84 each (15-minute rolls at 2/5, guaranteed after 3 misses), stones
and the Link Cable ~0.08 (0.15 with the Pokédex plan above), plus evolutions and moves to learn. It is
full after ~2.9 hours of reading without opening the game; road trainers on the wild schedule would
bring that down to ~2.1 h (~2.5 h with at most 2 trainers waiting). While it is full, the encounter,
medicine, TM/HM and stone rolls are skipped (their pity is set so the next roll after a slot frees
hits), so encounters are lost.

What the user wrote down: medicine, TM/HM and stones/Link Cable go straight into the bag, like balls
already do, and the game shows a single summary screen listing in detail everything received. The
queue then only holds encounters, road trainers, evolutions and moves to learn: ~1.84 events an hour
without trainers (full after ~8.7 h); with trainers at half the wild rate ~7.6 h with at most 2
trainers waiting, ~5.8 h with no trainer cap.

Queue split agreed on 2026-10-19 (16 slots, no save format change):

| Kind | Limit |
|---|---|
| Evolutions + moves to learn | 2 slots kept free for them (still one evolution per Pokémon) |
| Road trainers | at most 2 waiting (as in the crosslingua fork); while 2 wait, the trainer roll keeps its pity so the next one comes as soon as there is room |
| Wild encounters | no limit of their own: the remaining slots, at least 12 (full after ~7.6 h of reading without opening the game) |
| Medicine, TM/HM, stones, Link Cable | 0 if items go straight to the bag (still undecided, see above) |

Why not leave trainers unlimited too: HP only recovers while reading, and 4-5 queued trainer fights
cost 7-9 HP bars against the 4 of a party of 4; and the queue is resolved front first, so a trainer the
party cannot beat yet would block every encounter and evolution behind it. The reserved slots exist
because today a full queue silently drops an evolution prompt until the next level-up.
The item summary screen would keep "received since last viewed" in its own side file, so
`PokemonState` stays as it is.
