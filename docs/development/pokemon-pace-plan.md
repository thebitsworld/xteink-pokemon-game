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

## Battle XP and road trainers: option under consideration (not decided)

Today's battle XP (`battleVictoryXp`: opponent level x 6 for gym/E4/Champion, x 4 for wild) lets one
Pokémon solo every gym in ~6 hours of reading: from Koga on, the gyms' own XP lifts it past the next
gym, and it reaches level 100 at Bruno. Road trainers must NOT scale their XP with the party's level the
way the crosslingua fork does (level x 6 per member): that would finish the gyms in ~2 hours.

The option the user wrote down on 2026-10-19 to think about:

- Gym/E4/Champion: opponent level x 1, scaled by level difference.
- Wild: opponent level x 1, scaled by level difference.
- Scaling (Gen 5 style): XP x `((2*Lo + 10) / (Lo + Lp + 10))^2.5`, with `Lo` the opponent's level and
  `Lp` the player's. 1 at the same level; e.g. a level 24 Pokémon beating a level 14 one gets x0.56,
  a level 40 one beating a level 50 one x1.27.
- Road trainers: 10 XP for each win (fixed, not scaled), on the wild encounter schedule (a roll every 15
  minutes at 2/5, guaranteed after 3 misses), teams of 1-3 at the party's average level -4..+2, and a
  win gives a random item with today's item-kind odds.

Simulated (median of 100 runs, one Pokémon soloing from level 5, challenging each gym 5 levels above its
strongest member; reading hours and the solo Pokémon's level after the fight):

| Gym | Strongest | Today (x6 / x4, trainers 10 XP) | Option above |
|---|---|---|---|
| Brock | 14 | 3.2 h, lv 23 | 4.5 h, lv 19 |
| Erika | 29 | 4.2 h, lv 44 | 11.5 h, lv 35 |
| Koga | 43 | 6.0 h, lv 58 | 22.5 h, lv 49 |
| Giovanni | 50 | 6.0 h, lv 86 | 25.3 h, lv 57 |
| Bruno | 58 | 6.0 h, lv 100 | 30.3 h, lv 65 |
| Champion | 65 | 6.0 h, lv 100 | 30.5 h, lv 72 |

Other variants simulated: gym x2 scaled with wild x1 scaled ends at ~22 h, level 81; gym x2 scaled with
wild x4 unscaled at ~13 h, level 81. The jump from Erika (29) to Koga (43) and the slow climb after it
come from the gym team data: whatever the formula, Koga to the Champion tends to fall in one sitting.

## Agreed changes (all in `lib/Pokemon/PokemonGame.cpp`)

1. A species not caught yet weighs x3 when a wild Pokémon is picked. Early on nearly every species is
   new, so little changes; late in the game the missing ones show up far more often.
2. Legendary chance per encounter 1/20 -> 1/10 (the 20 h / 50 h lifetime-reading conditions stay).
3. Mewtwo from 90% of a book instead of 95%.
4. Stones and the Link Cable: hourly chance 1/20 -> 1/10, guaranteed after 9 misses instead of 19.

No save change. The scripted random sequences in the Pokémon game tests need updating.
