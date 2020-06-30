# draw_manager

#### basic multithreaded drawing

Setting up
------
To get drawing to work you need to implement a class which extends draw_manager and implements the virtual functions.
How to actually draw is pretty specific to your environment but to get an idea you can look at the d3d9 implementation provided or how ImGui does drawing, it's pretty similar.

You also need to provide freetype headers & binaries(https://www.freetype.org/download.html) as well as stb_rectpack.h(https://github.com/nothings/stb/blob/master/stb_rect_pack.h)

Usage for Drawing
------

```cpp
// Register buffer once somewhere
static const auto buffer_idx = draw_manager->register_buffer();

// Each time you do a draw pass get a pointer to the buffer
const auto buffer = draw_manager->get_buffer(buffer_idx);
// do the drawing stuff
// so for example
buffer->rectangle_filled({0.f, 0.f}, draw_manager->get_screen_size(), math::color_rgba::white());
// then swap
draw_manager->swap_buffers(buffer_idx);
```

Feature List
------

* drawing of primitives & text(freetype) into a vertex/index buffer consisting of triangles
* multithreading as it operates like a swapchain
* multiple independent buffers which can be sorted by priority & parent-child hierarchy
* independent of actual drawing implementation(confirmed to work for d3d9 & csgo's surface)
* supports bluring, color-keying & circle scissors if they are implemented

Looks like this (after a resize and with the d3d9 implementation)
------
![preview](https://i.imgur.com/OKl12dH.png)
