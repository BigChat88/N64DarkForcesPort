# intro/

Source images for the boot intro's "powered by libdragon" dragon logo animation
(see [src/n64/intro_n64.cpp](../../src/n64/intro_n64.cpp)). `dragon1.png`..`dragon4.png`
are the four line-art layers (head, body, tail, wordmark) that get rotated,
scaled and faded in to animate the dragon "jumping" onto the screen.

They come from [lambertjamesd/n64brew2025](https://github.com/lambertjamesd/n64brew2025)
(`assets/images/intro/`), which in turn sourced them from the N64brew-GameJam2024
repository (Copyright (c) 2024 N64brew, MIT licensed; see the license header in
`src/n64/intro_n64.cpp`). The same assets are used by the N64 Doom port.

The Makefile converts them to `gamedata/intro/*.sprite` with
`mksprite -f I8`: the grayscale value is used as both intensity and alpha, so
black areas are transparent and the line art is tinted with the RDP prim color.
