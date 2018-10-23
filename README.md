# 1010 for Arduboy

Gameplay and scoring modeled after the
[1010!](https://itunes.apple.com/us/app/1010/id911793120?mt=8)
game for iOS.

Highlight one of the three randomly selected pieces with the left and
right directional buttons, then choose a piece with the A button.
Move it around on the board and place it anywhere that isn't overlapping
with the A button, or press B to go back and choose a different piece.

Position ten blocks in a row or column and it is freed up, play continues
until there are no free spaces to place any of the three pieces.

One point is scored for each block in the piece placed, and ten points
are scored for clearing a row or column.

## Command Mode

Hold down both A+B buttons and:

- Up to make the screen backlight brighter,
- Down to make it dimmer, or
- Left or Right to reset the game.

## Compiling

Requires
[Arduino-Makefile](https://github.com/sudar/Arduino-Makefile)
installed.
Adjust the path to `Arduino.mk` in `Makefile`.

Compile with `make` (requires GNU Make, which is `gmake` on OpenBSD).

Connect your Arduboy and power it on, then flash it with `make upload`.
