# Code Breaker

## Goal
One player hides a secret code: a row of 4 or 5 symbols. The other breaks it by guessing. Every guess is scored with pegs that tell how close it came, and the code must be found within the allowed number of guesses (10 by default). You can break the device's code, or hide a code and watch the device break yours.

## The symbols
There are 6 symbols, or 8 in bigger games: filled circle, ring, filled square, open square, triangle, cross, star and diamond. In some games a symbol may appear more than once in the code ("repeats").

## Scoring
Each guess gets one peg for every symbol it has right:
- A black peg (a filled dot) for a right symbol in the right place.
- A white peg (a ring) for a right symbol in the wrong place.
- A small dot is an empty hole: nothing more.

The pegs do not say which symbols are meant. A symbol counts only as often as it is in the code: guessing four circles against a code with one circle scores one peg.

## Games
- Easy: 4 positions, 6 symbols, no repeats.
- Normal: 4 positions, 6 symbols, repeats allowed.
- Hard: 5 positions, 8 symbols, repeats allowed.
- Custom: the size chosen in Options.
- Device breaks your code: the device guesses a code of the size in Options.

Options also set the number of guesses (8, 10 or 12) and how the device's guesses are scored. With "by the app" you enter your code first and the app scores every guess for you, without mistakes. With "by you" you keep the code in your head and score each guess yourself. If your scores contradict each other, no code can fit them: the device says so and you score the last guess again. "Undo last score" in the menu takes back older ones.

The menu shows your games won, the average number of guesses and your best for each size. A game in progress is kept when you leave.

## Controls (X3/X4 buttons)
- Left and Right move along the positions to the Submit button.
- Up and Down change the symbol at the cursor.
- Confirm submits the guess.
- Scoring by hand: Left and Right choose Black, White or Score it; Up and Down change the number; Confirm scores.
- Back opens the menu. After a game, Confirm plays again.

## Controls (X4 Pro touch)
- Tap a position, then a symbol in the row of symbols below the board. The cursor moves on to the next empty position.
- Tap Submit guess. When scoring by hand, tap the numbers of black and white pegs, then Score it.
- Tap Menu at the top for the menu.

## Tips
- Start with a guess of two symbols, two of each (circle, circle, ring, ring): its score narrows things down quickly.
- Every new guess should fit all the scores so far, unless a guess with other symbols tells you more.
- A score of no pegs at all is good news: those symbols are gone from the whole code.
- On 4 x 6 with repeats the device plays Knuth's method and never needs more than five guesses.
