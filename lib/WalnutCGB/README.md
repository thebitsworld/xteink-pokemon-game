# Walnut-CGB

[Walnut-CGB](https://github.com/Mr-PauI/Walnut-CGB) v1.2.4 by Mr. Paul (MIT),
a rewrite of [Peanut-GB](https://github.com/deltabeard/Peanut-GB) (MIT) with
Game Boy Color support. Used for Game Boy Color games on the X4 Pro; it is
compiled once, with its symbols renamed, in `src/walnut_cgb_impl.c`, behind
the opaque API in `src/walnut_cgb_port.h`.

Local change, marked `xteink-pokemon:`: a `struct gb_s;` forward declaration
before the early prototypes in `src/walnut_cgb.h`, so the header compiles as C.
Line endings were converted to LF.
