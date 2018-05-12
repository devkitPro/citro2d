# citro2d

**( ͡° ͜ʖ ͡°)**

*Library for drawing 2D graphics using the Nintendo 3DS's PICA200 GPU.*

This library contains optimized routines that allow 3DS homebrew developers to
develop applications that take full advantage of the GPU to draw 2D graphics.
The routines in this library have been carefully designed and optimized for
the purpose of removing bottlenecks and allowing higher GPU throughput.

citro2d uses [citro3d](https://github.com/fincs/citro3d) under the hood to
talk to the GPU. It is possible to use citro2d on its own, or use it alongside
citro3d to draw mixed 2D and 3D content.

Features:
- Lightweight and straightforward API
- Full doxygen documentation
- Drawing on any surface (C3D_RenderTarget)
- Drawing images and sprites (the latter contain state whereas the former don't)
- Drawing text using the system font
- Spritesheet/texture atlas support using [tex3ds](https://github.com/mtheall/tex3ds)
- Scaling, flipping, rotation
- Drawing untextured triangles and rectangles
- Per-vertex tinting with configurable blend factor (additive color blending with user specified colors)
- Flexible and configurable gradients
- Full-screen fade-out/fade-in transitions (to any color)
- Concurrent usage of citro2d and citro3d
