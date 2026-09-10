---
title: Controls
nav_order: 6
---

# Controls

The Controls menu lets you customize front buttons, side buttons, and reader shortcuts.

## Settings Menu Layout

### Power Button

- Short-press action
- Long-press action
- Power + Up action

### Front Buttons

- Remap front buttons
- Remap front buttons while reading
- Orientation aware
- Long-press behavior (in-reader only)
- Long-press back action (in-reader only)
- Long-press menu action (in-reader only)

Note: Even though some actions assigned to the front buttons could be used globally, they are restricted to apply within the reader only due to the dynamic nature of the front buttons (they can mean different things based on the screen you're on).

### Side Buttons

- Layout
- Orientation aware
- Long-press action

### Taps & Gestures

- Page Turn Gesture
- Pinch to Resize Font (on supported multi-touch devices)
- Two-finger Swipe (on supported multi-touch devices)

### Page Turn Gesture

On touchscreen devices, **Page Turn Gesture** is in
**Settings > Controls > Taps & Gestures**. It changes page-turn gestures while
reading.

- **Tap & Swipe** (default): Tap the left third to go back and the rest of the screen to go forward, or swipe right and left.
- **Tap Only**: Use the normal tap zones; horizontal swipes do not turn pages or go Back/Home.
- **Swipe Only**: Swipe right or left; taps do not turn pages.
- **Inverted Tap**: Tap the left two-thirds to go forward and the right third to go back.
- **Disabled**: Do not turn pages with taps or horizontal swipes.

## Two-finger Swipe Actions

On touchscreen devices with multi-touch support, open **Settings > Controls >
Taps & Gestures > Two-finger Swipe** to assign actions to the four swipe
directions. During reading, place two fingers on the page and move them
together in the configured direction. Use **Not Set** to leave a direction
unassigned.

Each action can be assigned to only one direction. If you assign an action to a
new direction, CrossInk clears its previous direction automatically.

Available actions depend on the device and reader:

| Action | Availability |
| --- | --- |
| Increase Brightness / Decrease Brightness | Devices with a frontlight |
| Increase Warmth / Decrease Warmth | Devices with an adjustable warm/cool frontlight |
| Next Chapter / Previous Chapter | EPUB readers |
| Increase Font Size / Decrease Font Size | EPUB and TXT readers |

Use a clear, mostly straight motion so both contacts are recognized as one
gesture. Configured two-finger swipes are handled separately from the ordinary
one-finger page-turn mapping. On image-based XTC books, chapter and font-size
actions are consumed but cannot change the pre-rendered pages.

## Side Button Long-press Action

When set to `Change Font Size`, hold a side button for about 2 seconds:

- Up increases font size
- Down decreases font size

When set to `Orientation Change`, hold a side button for about 2 seconds:

- Up cycles through the orientations in the following order: `Landscape CCW` -> `Inverted` -> `Landscape CW` -> `Portrait`
- Down cycles through the orientations in the following order: `Landscape CW` -> `Inverted` -> `Landscape CCW` -> `Portrait`

## Power, Back, and Menu Button Actions

Defaults:

- Short-press Power Button Action: Ignore
- Long-press Power Button Action: Sleep
- Long-press Back Button Action: Browse Files
- Long Press Menu Button Action: Ignore

Available actions include:

- Ignore
- Sleep
- Page Turn
- Refresh Screen
- Change Font
- Guide Dots
- Bionic Reading
- Toggle Bookmark
- Sync Progress
- Mark as Finished
- Reading Stats
- Take Screenshot
- Auto Page Turn Interval
- File Transfer
- Calibre Wireless
- Join a Network
- Create Hotspot
- Tilt Page Turn (X3 only)
- Footnotes
- Dark Mode
- Browse Files
- Create Clipping
- Look Up Word
- Quick Lock
- Quick Actions
- Toggle Frontlight (on supported devices)
- Toggle Touchscreen (on supported devices)

## Power + Up Shortcut

The **Power + Up** shortcut runs the action selected in **Settings > Controls >
Power + Up**. It is disabled by default. Press the **Power** and **Volume Up**
buttons together to trigger it, then release both buttons before using another
shortcut.

The shortcut can run many of the same actions available for the Power button,
including **Quick Lock**. The existing **Power + Volume Down** screenshot
shortcut is unchanged.

## Quick Actions Triggers

In **Settings > Quick Actions**, assign the menu to one shortcut. Available
triggers include short- or long-press Power, **Power + Up**, and on X4 Pro,
**Tap Home**, **Long-Press Home**, or **Double Tap Home**. Selecting a trigger
there replaces any previous Quick Actions trigger; it does not change the five
actions in the menu.

## Quick Lock

**Quick Lock** temporarily disables normal button and touchscreen input while
leaving the current screen visible. A lock badge appears on the display while
the device is locked. It is useful when carrying the reader or setting it down
while reading.

Quick Lock can be assigned to **Power + Up**, short- or long-press **Power**,
or long-press **Back** or **Menu**. Trigger the assigned action to toggle the
lock. While locked, repeat that same shortcut to unlock; all other buttons,
including **Power + Volume Down** screenshots, and touchscreen input stay
locked.

Quick Lock still follows the regular **Time to Sleep** setting. If the timeout
expires, the device sleeps and restores the lock when it wakes. Reading timers
pause while Quick Lock is active.

## Footnote Shortcut

When a shortcut is mapped to Footnotes, the shortcut opens the footnotes submenu while reading. If the current page has only one footnote, CrossInk opens that referenced page directly.

The **Quick-return from Footnotes** setting controls whether the Power button acts like Back after opening a footnote page, making it faster to return to the original reading position.
