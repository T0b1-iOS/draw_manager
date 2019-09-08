# draw_manager

#### basic multithreaded drawing

Setting up
------
To get drawing to work you need to implement a class which extends draw_manager and implements the virtual functions.
How to actually draw is pretty specific to your environment but to get an idea you can look at the d3d9 implementation provided or how ImGui does drawing, it's pretty similar.
Please keep in mind that the d3d9 implementation does not implement everything(circle scissors) as I am pretty lazy and only implemented features in the implementations in which I needed them.

You also need to provide freetype headers & binaries(https://www.freetype.org/download.html) as well as stb_rectpack.h(https://github.com/nothings/stb/blob/master/stb_rect_pack.h)

Usage for Draing
------

```cpp
// Register buffer once somewhere
static const auto buffer_idx = draw_manager->register_buffer();

// Each time you do a draw pass get a pointer to the buffer
const auto buffer = draw_manager->get_buffer(buffer_idx);
// do the drawing stuff
// then swap
draw_manager->swap_buffers(buffer_idx);
```

Feature List
------

* drawing of primitives & text(freetype) into a vertex/index buffer consisting of triangles
* multithreading as it operates like a swapchain
* multiple independent buffers which can be sorted by priority & parent-child hierarchy
* independent of actual drawing implementation(confirmed to work for d3d9 & csgo's surface)
* supports bluring/color-keying (& circle scissors if they are implemented)

Kinda looks like this(after a resize and d3d9)
------
![preview](https://i.imgur.com/GNCDkpe.png)
