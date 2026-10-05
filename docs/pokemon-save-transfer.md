---
title: Pokémon Save Transfer
nav_order: 1.3
---

# Pokémon Save Transfer

Pokémon Save Transfer sends your whole Pokémon game - party, PC Box, Bag,
Pokédex, badges, movesets, IVs/EVs and Hall of Fame - directly from one reader
to another nearby. Like [Nearby File Transfer](nearby-file-transfer.md) it uses
ESP-NOW, so no Wi-Fi network, internet connection or computer is needed. It
works between any two supported devices (X3, X4, X4 Pro).

To sync your reading stats and book position at the same time, use
[Sync with Nearby Reader](nearby-sync.md) from the File Transfer menu instead -
it sends the Pokémon save too.

## Requirements

- Both readers run Pokémon firmware with this feature, and are close together.
- Both readers need Xteink Pokemon firmware v1.8.0 or newer (the first version
  with this feature).
- The receiving reader's firmware must be the same version as the sender's or
  newer. A save from newer firmware is refused (with a message) instead of
  being loaded by firmware that does not understand it.

## Receive a Save

1. On the receiving reader, open **Pokémon > Settings > Receive Save from
   Nearby Device**. On a reader that has no game yet, the same option is the
   wide button under the four starter Pokémon.
2. When the offer appears, check the sender's name and the save's summary
   (lead Pokémon, level, badges, Pokémon caught), then select **Accept**. Use
   **Cancel** or **Back** to decline.
3. Wait for **Save received**, then press **OK**. The reader restarts and loads
   the new save.

If this reader already has a save, the offer warns that it will be replaced.
The replaced save is not deleted: it is moved to `/.crosspoint/pokemon-backup/`
on the SD card (see [Backups](#backups)).

## Send a Save

1. Start receiving on the other reader first.
2. On the sending reader, open **Pokémon > Settings > Send Save to Nearby
   Device**.
3. Choose how to send it:
   - **Copy (keep it here too)** - both readers end up with the same save.
   - **Move (remove it from here)** - once the receiver confirms the save is
     safely stored, this reader's save is moved to its backup folder and the
     reader starts a new game next time.
4. Select the receiving reader, wait for it to accept, and keep both readers on
   their transfer screens until the sender shows **Save sent**.

Press **Back** on either reader to cancel. A cancelled or failed transfer never
changes either reader's save.

## How It Stays Safe

- Every save file is sent with its own checksum, and the whole transfer is
  checked again end to end. A save that arrives damaged is thrown away.
- The receiver stores the new save in a temporary file first. Only when it has
  fully arrived and been verified does the receiver commit it - and only then
  does a sender using **Move** remove its own copy.
- Installing the committed save is crash-safe: if the reader loses power
  partway through, it finishes the install the next time the Pokémon game
  loads, so you never end up with half of one save and half of another.
- If the sender never hears the receiver's confirmation (for example the
  readers moved apart at the very last moment), **Move** reports a failure and
  keeps its save. The receiver may still have received it - check before
  trying again. Nothing is lost either way; at worst both readers have it.

## Backups

The save a reader gives up - replaced by a received one, or sent away with
**Move** - is kept in `/.crosspoint/pokemon-backup/`. Each transfer replaces
the previous backup. To restore it by hand, copy the files from that folder
back into `/.crosspoint/` (replacing any `pokemon-*.bin` files there) with the
reader connected to a computer.

## Troubleshooting

**The receiving reader does not appear**

Make sure the receiver is on **Receive Save from Nearby Device** (not the
book **Receive File** screen - the two do not see each other), both readers
are close together, and try again.

**"This save comes from newer firmware"**

Update the receiving reader to the same firmware version as the sender (or
newer), then retry.

**"The save finishes installing after a restart"**

The save was received and committed, but writing it into place did not finish.
It completes automatically the next time the Pokémon game is opened.
