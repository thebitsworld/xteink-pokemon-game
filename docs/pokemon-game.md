---
title: Pokémon game
nav_order: 1.2
---

# The Pokémon game

A companion game built into this firmware that turns real reading time into Pokémon
progress. There's no grinding, no timers to watch, and nothing to tap through — just keep
reading the way you normally would, and your team grows along with you.

This page explains what the game does and how it feels to play, without the underlying
technical details. If you're looking for save-file formats, save internals, or version
history, see [Save-file formats](file-formats.md) and [CHANGELOG.md](../CHANGELOG.md)
instead.

- [What you can do](#what-you-can-do)
- [How progress works](#how-progress-works)
- [Getting started](#getting-started)
- [Catching Pokémon](#catching-pokémon)
- [Battling](#battling)
- [Gym Leaders, the Elite Four, and the Champion](#gym-leaders-the-elite-four-and-the-champion)
- [Your Party and the PC Box](#your-party-and-the-pc-box)
- [The Bag](#the-bag)
- [Evolution](#evolution)
- [The Pokédex](#the-pokédex)
- [Trainer Card and Hall of Fame](#trainer-card-and-hall-of-fame)
- [Shiny Pokémon](#shiny-pokémon)
- [Language support](#language-support)
- [Your save is separate from your books](#your-save-is-separate-from-your-books)
- [How this compares to the original games](#how-this-compares-to-the-original-games)
- [Frequently asked questions](#frequently-asked-questions)

## What you can do

- Pick one of four classic first partners — Bulbasaur, Charmander, Squirtle, or Pikachu —
  choose its gender, and give it a nickname.
- Level up, learn moves, and evolve your Pokémon just by reading.
- Meet and catch wild Pokémon while you read, from the full original 151.
- Fight full turn-based battles: moves, types, critical hits, status conditions, stat
  changes, and items, all working the way they did in the original Game Boy games.
- Build a team of six, with a PC Box for everyone else you've caught.
- Take on all eight Gym Leaders, the Elite Four, and finally the Champion, in order.
- Fill out a complete Kanto Pokédex, with a full-page card for every species you've seen.
- Track your reading time, badges, and Pokédex progress on a Trainer Card, and revisit your
  championship-winning team any time from a Hall of Fame.

## How progress works

Whichever Pokémon is at the **top of your Party** is the one training. As you turn real
pages, it earns experience, levels up, and learns new moves — exactly like reading a book
in the original games would slowly fill an experience bar.

A few things worth knowing:

- **Only active reading counts.** Turning pages yourself counts; leaving a book open, or
  using an automatic page-turn feature, does not.
- **Progress arrives in the background** while you read — there's nothing to start or stop.
  Every few minutes, the app quietly checks in and saves your progress, so you never lose
  more than a couple of minutes of credit even if you close the book without warning.
- **Wild encounters and item finds happen the same way**, popping up every so often as you
  read. A small `!` appears on your device's dashboard whenever something is waiting for
  you in the Pokémon menu — a Pokémon to catch, an item you found, a move to learn, or a
  Pokémon ready to evolve.
- **How far you are into a book matters.** The wild Pokémon and items you find while
  reading a longer or further-along book tend to be a bit tougher and rarer than what you'd
  find right at the start of a short one.
- **Battling also grants experience**, in addition to reading — so any Pokémon you send out
  to fight (not just your lead) has a way to catch up.

## Getting started

The very first time you open the Pokémon menu, you'll be asked to choose your starter,
its gender, and an optional nickname. After that, the Pokémon menu becomes your home base:
Party, Pokédex, PC Box, Bag, Gym Battle, and your Trainer Card.

## Catching Pokémon

When a wild Pokémon shows up, you're taken into a short battle instead of catching it on
the spot. Weaken it, then throw a Poké Ball (or a Great Ball, or an Ultra Ball, if you have
one — better balls catch more reliably) to try to catch it. You can also just run.

If your Party and PC Box are both completely full, a new catch is blocked until you free
up space — the game will tell you rather than losing the encounter.

## Battling

Battles are full turn-based fights: pick a move, use an item, switch Pokémon, or run.
Type match-ups, critical hits, status conditions (like Poison, Paralysis, or Sleep), stat
boosts and drops, multi-hit moves, and two-turn moves like Fly or Solar Beam all work.
A Pokémon that faints needs a Revive (or a trip back to full health) before it can fight
again.

## Gym Leaders, the Elite Four, and the Champion

Once your team feels ready, take on the eight Gym Leaders from the Pokémon menu's Gym
Battle screen, in order — each one unlocks the next. Beat all eight and the Elite Four
unlocks, followed by the Champion as the final challenge. Every Gym Leader, Elite Four
member, and the Champion uses their real classic team. Winning a gym badge also gives your
own Pokémon a small permanent stat boost, matching the original games.

Gyms can only be challenged once each — there's no re-fighting a Gym Leader you've already
beaten to farm experience.

## Your Party and the PC Box

Your Party holds up to six Pokémon; anything beyond that goes to the PC Box, which holds
hundreds more. Move Pokémon in and out freely, reorder your Party, and sort the Box by
catch order, Pokédex number, or name. A Pokémon's moves, PP, and current condition are
kept exactly as they were through a trip to the Box — nothing resets just from storing it.

## The Bag

Everything you find while reading lands in your Bag, sorted into a few categories:

- **Poké Balls** — regular, Great, and Ultra Balls for catching.
- **Medicine** — Potions, Full Restores, status cures, PP restores, Rare Candy, vitamins,
  and PP Up.
- **TMs & HMs** — teach a move to a Pokémon that can learn it.
- **Battle items** — X Attack and similar stat boosters, used mid-fight.
- **Evolution items** — stones and the Link Cable, used from the Bag on a Pokémon that's
  ready for them.

## Evolution

Pokémon evolve the same three ways they always have: by reaching a certain level, by using
an evolution stone or Link Cable, or in a couple of cases, either one. When a Pokémon
reaches its evolution level, you'll be asked whether to go ahead — or you can turn that
prompt off per-Pokémon from its Summary screen if you'd rather decide for yourself later.
Once a Pokémon is past the level where it would evolve, an "Evolve now" option appears on
its Summary screen so you're never stuck waiting for the prompt to come back around.

## The Pokédex

Every species you've seen or caught is tracked in a 151-entry Pokédex, each with a
full-page illustrated card. Defeating a Gym Leader's, Elite Four member's, or the
Champion's Pokémon counts as seeing it too, even if you've never met one in the wild.

## Trainer Card and Hall of Fame

Your Trainer Card (in the Pokémon menu) shows your total reading time, your Pokédex
progress, and a badge case with all eight Gym badges plus the Elite Four and Champion.
The first time you beat the Champion, the game captures a snapshot of your winning
team — sprites, names, levels, genders, and shiny stars — along with how long it took you
to get there, and adds it to a Hall of Fame you can revisit any time from the Trainer
Card. Releasing or evolving those Pokémon afterward never changes what's recorded there.

## Shiny Pokémon

Every wild encounter has a small (1-in-64) chance of being shiny, marked with a ★ next to
its name everywhere it appears. Shiny Pokémon also tend to have noticeably better hidden
stats than an ordinary catch of the same species.

## Language support

The Pokémon game's menus, messages, and prompts are available in every language this
firmware supports. Pokémon, move, and item names themselves stay in their original
English form in every language, matching how the mainline games have always kept those
names consistent worldwide.

## Your save is separate from your books

Your Pokémon progress, your books, and your reading statistics are all stored separately
and don't affect each other. Updating the firmware never touches your books or your
Pokémon save. See [Installation](installation.md) for how saves are protected during an
update, and where the save files live if you ever want to back them up yourself.

## How this compares to the original games

This is a faithful companion, not a byte-for-byte remake, and a handful of things are
deliberately simplified so the game fits comfortably on an e-reader and stays built around
reading instead of grinding:

- There's no trading, no multiplayer, and no regional Pokédex beyond the original 151.
- Hidden training stats (the values that make two Pokémon of the same species slightly
  different) use a simplified version of the original formula — the day-to-day effect on
  your Pokémon's stats is the same, just calculated a bit differently under the hood.
- Wild Pokémon and items you find are tuned around reading pace rather than the original
  games' exact encounter tables, so the specific odds and locations won't match a Game Boy
  cartridge exactly.

Everything else — types, moves, status conditions, stat stages, critical hits, gym teams,
and the evolution/leveling formulas — follows the original Generation I games as closely as
this platform allows.

## Frequently asked questions

**Do I need to do anything to start earning experience?**
No. Once you have a starter, just read normally with the trained Pokémon at the top of
your Party. Progress happens in the background.

**Why isn't my Pokémon gaining experience?**
Make sure it's the Pokémon at the very top of your Party, and that you're actually turning
pages yourself (not leaving the book open or using auto page-turn).

**Does reading a TXT or a pre-rendered book count too, or only EPUB?**
All three reading formats this firmware supports count the same way.

**What happens to my Pokémon if I update the firmware?**
Nothing — your save carries forward automatically. See
[Installation](installation.md#your-saves-are-safe) for details.

**Can I reset just my Pokémon progress without touching my books?**
Yes — the Pokémon menu has its own reset, separate from any other device reset, and it
only clears your Pokémon save.

**Is my data affected by choosing a different reading-companion language?**
No, changing the device's language only changes what's displayed; your save and its
Pokémon are unaffected.
