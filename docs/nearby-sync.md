---
title: Sync with Nearby Reader
nav_order: 7.2
---

# Sync with Nearby Reader

**Sync with Nearby Reader** moves everything that matters from one reader to
another in a single step, directly over ESP-NOW - no Wi-Fi network, internet
connection, account or computer needed. It works between X3, X4 and X4 Pro.

One sync carries:

| What | What happens on the receiving reader |
| --- | --- |
| **Pokémon save** (Pokémon firmware only) | Replaces the receiver's game, exactly like [Pokémon Save Transfer](pokemon-save-transfer.md). The previous game is kept in `/.crosspoint/pokemon-backup/`. |
| **Reading stats** | Exchanged **both ways**: each reader stores the other's all-time totals in `/.crosspoint/synced_stats/`, the same files [Reading Stats Sync](reading-stats-sync.md) uses, so both readers show the combined totals afterwards. |
| **Book position** | The position in the book that was last open on the sender is written to the same book on the receiver, which then becomes its most recent book. If the receiver does not have that book, the EPUB is copied over too. The book's own reading stats (time, pages, sessions, dates) come along and are combined with the receiver's. |

Anything the sender does not have (no Pokémon game, no book opened yet) is
simply left out.

## How to use it

1. On the receiving reader, open **File Transfer → Sync with Nearby Reader**
   and choose **Receive on this reader**.
2. On the sending reader, open the same entry and choose **Send from this
   reader**. It lists what it is about to send.
3. Choose how to sync:
   - **Sync (keep the Pokémon save here too)** - both readers end up with the
     same game.
   - **Sync and move the Pokémon save** - the sender hands its game over and
     starts a new one (its save goes to its backup folder).

   Without a Pokémon save there is a single **Sync** option.
4. Pick the receiving reader, then on the receiver check the summary (sender,
   lead Pokémon, badges, book and percentage) and press **Accept**.
5. When **Sync complete** appears, press **OK** on both readers - they restart
   to load the new data. On the receiver, **Open book** jumps straight into the
   synced book at the synced position.

## The book

The sender syncs the book it last had open (the one **Continue Reading** would
open), as an EPUB. The receiver looks for the same book:

1. at the same path on its SD card;
2. otherwise, a file with the same name among its recent books;
3. otherwise, in the folder where **Receive File** saves books, or the SD root.

If the receiver does not have the book, **the book itself is sent too**: the
offer screen says so, with its size, before you accept. It is saved under the
sender's file name in the folder **Receive File** uses (the SD root unless you
changed it there), then opened at the synced position. A book that is already
on the receiver is never sent again or overwritten.

Copying a book makes the sync noticeably longer - how much depends on the size
of the EPUB, since it travels over the same direct radio link as everything else. Keep both readers on their sync screens until
**Sync complete** appears. Books larger than 64 MB are not copied; the rest is
still synced and the screen says which file is missing.

The book's own reading stats travel with it, so its Reading Stats screen on the
receiver is filled in. They are combined with whatever the receiver already had
for that book by keeping the larger value of each counter - syncing twice never
counts the same reading twice. (The flip side: if both readers read different
parts of the book separately, the totals show the larger of the two, not the
sum.)

The position travels as the reader's own progress record, including the offset
of the text on screen, so it lands on the same text even when the two readers
lay the book out differently (different screen, font or margins). The book
itself is never modified.

## Safety

- The receiver must accept the sync; declining changes nothing.
- Everything arrives in one temporary file that is checked end to end (and
  each part separately) before anything is applied. A damaged or interrupted
  transfer is thrown away and changes nothing on either reader.
- The Pokémon save follows the same crash-safe install as
  [Pokémon Save Transfer](pokemon-save-transfer.md#how-it-stays-safe); with
  **move**, the sender removes its copy only after the receiver has committed
  it.
- A Pokémon save from newer firmware is refused instead of loaded.

## Compatibility

Both readers need firmware with this feature (Xteink Pokemon v1.8.0 or newer).
Book transfer (**Receive File**), **Sync Stats** and **Nearby
Position Sync** still work on their own as before.
