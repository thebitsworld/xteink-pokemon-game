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

Battle XP was checked too and stays as it is: today's formula already gets the gyms done in 10-20 hours
of reading, and a Pokémon soloing every gym reaches level 100 only around the Elite Four. Road trainers
must NOT scale their XP with the party's level the way the crosslingua fork does (level x 6 per member):
that would finish the gyms in ~2 hours.

## Agreed changes (all in `lib/Pokemon/PokemonGame.cpp`)

1. A species not caught yet weighs x3 when a wild Pokémon is picked. Early on nearly every species is
   new, so little changes; late in the game the missing ones show up far more often.
2. Legendary chance per encounter 1/20 -> 1/10 (the 20 h / 50 h lifetime-reading conditions stay).
3. Mewtwo from 90% of a book instead of 95%.
4. Stones and the Link Cable: hourly chance 1/20 -> 1/10, guaranteed after 9 misses instead of 19.

No save change. The scripted random sequences in the Pokémon game tests need updating.
