#include "draw_manager.hpp"
#include <memory>

const auto font_path = R"(C:\Windows\Fonts\verdana.ttf)";

extern int window_size_x;
extern int window_size_y;
extern std::atomic<int> fps;

extern std::unique_ptr<util::draw::draw_manager> draw_manager;
extern bool stop_user_thread;

std::atomic<util::draw::tex_id> tex;
std::atomic<bool> reset_tex = false;

static util::draw::tex_id create_texture()
{
	struct pixel
	{
		uint8_t r, g, b, a;
	};
	std::vector<pixel> data;
	data.resize(200 * 200);

	const auto fill_rect = [&data](const auto min_x,
		const auto min_y,
		const auto max_x,
		const auto max_y,
		const math::color_rgba col)
	{
		for (int x = min_x; x < max_x; ++x)
		{
			for (auto y = min_y; y < max_y; ++y)
			{
				data[y * 200 + x] = { col.r(), col.g(), col.b(), col.a() };
			}
		}
	};
	fill_rect(0, 0, 100, 100, math::color_rgba::red());
	fill_rect(100, 0, 200, 100, math::color_rgba::blue());
	fill_rect(0, 100, 100, 200, math::color_rgba::green());
	fill_rect(100, 100, 200, 200, math::color_rgba{ 226, 238, 37 });

	const auto tex = draw_manager->create_texture(200, 200);
	draw_manager->set_texture_rgba(tex, reinterpret_cast<const uint8_t*>(data.data()), 200, 200);

	return tex;
}

static auto buffer_idx = -1;

void user_thread_impl()
{
	using namespace std::chrono_literals;

	util::draw::font* font = nullptr;
	auto x = 0;
	char buffer[128];
	tex = create_texture();
	while (!stop_user_thread)
	{
		if (buffer_idx == -1)
		{
			buffer_idx = draw_manager->register_buffer();
			font = draw_manager->add_font(font_path, 20.f, false, true);
		}

		const auto buf = draw_manager->get_buffer(buffer_idx);

		std::snprintf(buffer, 128, "Current FPS: %d", fps.load());
		//buf->rectangle_filled({ 0, 0 }, { 100, 100 }, math::color_rgba::white());

		const auto size = buf->text_size(font, buffer);
		buf->text(font, buffer, { 10, 10 }, math::color_rgba::white());

		buf->rectangle({ 10, 10 }, { 10 + size.x, 10 + size.y }, 2.f, math::color_rgba::white());

		buf->rectangle_filled({ 0, 120 }, { 100, 220 }, math::color_rgba::white());

		buf->triangle_filled({ 0, 350 },
			{ 100, 230 },
			{ 200, 350 },
			math::color_rgba::red(),
			math::color_rgba::blue(),
			math::color_rgba::green(), true);

		static auto degrees = 360.f;
		buf->circle_filled({ 400, 200 }, 25, math::color_rgba{ 0, 0, 0, 0 }, math::color_rgba::blue(), 64, degrees, 90.f);
		/*degrees -= 1.f;
		if (degrees < 0.f)
			degrees = 360.f;*/

		buf->push_tex_id(tex);
		buf->prim_reserve(6, 4);
		buf->prim_rect_uv({ 400, 400 }, { 600, 600 }, { 0, 0 }, { 1, 1 }, math::color_rgba::white());

		buf->prim_reserve(6, 4);
		buf->prim_rect_uv({ 610, 400 }, { 810, 600 }, { 0, 0 }, { 1, 1 }, math::color_rgba::white());
		buf->pop_tex_id();

		buf->set_blur(180);
		buf->rectangle_filled({ 610, 400 }, { 810, 600 }, math::color_rgba::white());
		buf->set_blur(0);

		buf->set_key_color(math::color_rgba::green());
		buf->push_tex_id(tex);
		buf->prim_reserve(6, 4);
		buf->prim_rect_uv({ 820, 400 }, { 1020, 600 }, { 0, 0 }, { 1, 1 }, math::color_rgba::white());
		buf->pop_tex_id();
		buf->set_key_color(math::color_rgba{ 0, 0, 0, 0 });

		buf->push_clip_rect({ 1040, 410 }, { 1220, 590 });
		buf->push_clip_rect({ 1030, 400 }, { 1230, 600 }, true);
		buf->push_tex_id(tex);

		buf->set_key_color(math::color_rgba::green());
		buf->prim_reserve(6, 4);
		buf->prim_rect_uv({ 1030, 400 }, { 1230, 600 }, { 0, 0 }, { 1, 1 }, math::color_rgba::white());
		buf->set_key_color(math::color_rgba{ 0, 0, 0, 0 });

		buf->pop_tex_id();

		buf->text(font, buffer, { 1030, 410 }, math::color_rgba::white());
		buf->pop_clip_rect();
		buf->pop_clip_rect();

		buf->push_clip_rect({ window_size_x - 100.f, window_size_y - 100.f }, { window_size_x + 100.f, window_size_y + 100.f }, true);
		buf->push_tex_id(tex);

		buf->set_key_color(math::color_rgba::green());
		buf->prim_reserve(6, 4);
		buf->prim_rect_uv({ window_size_x - 100.f, window_size_y - 100.f }, { window_size_x + 100.f, window_size_y + 100.f }, { 0, 0 }, { 1, 1 }, math::color_rgba::white());
		buf->set_key_color(math::color_rgba{ 0, 0, 0, 0 });

		buf->pop_tex_id();
		buf->pop_clip_rect();

		util::draw::position positions[4] = {
			{600, 50},
			{700, 150},
			{800, 70},
			{900, 200}
		};
		buf->poly_line(positions, 4, math::color_rgba::white(), 1.f, true);

		buf->line({ 600, 150 }, { 900, 250 }, math::color_rgba::white(), 2.f, true);

		buf->circle({ 100, 550 }, 100, math::color_rgba::white(), 3.f, 64);

		draw_manager->swap_buffers(buffer_idx);

		x++;
		std::this_thread::sleep_for(16ms);
	}
}