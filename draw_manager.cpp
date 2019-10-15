#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftglyph.h>

#include "draw_manager.hpp"

using namespace util::draw;

template<typename T>
T get_deg(T pos)
{
	return math::deg2rad(45.f / 7.f * pos);
}

template<std::size_t steps>
std::array<position, steps> circle_pos()
{
	static_assert((steps - 1) % 8 == 0, "Steps must be a multiple of 8.");

	std::array<position, steps> result{};
	for (std::size_t i{}; i < steps - 1; ++i)
		result[i] = position{
			std::cos(get_deg(static_cast<float>(i))),
			std::sin(get_deg(static_cast<float>(i)))
		};
	result[steps - 1] = result[0];

	return result;
}


#pragma region util

static inline float inv_length(const position &lhs, const float fail_value)
{
	const auto d = lhs.x * lhs.x + lhs.y * lhs.y;
	if (d > 0.0f)
		return 1.0f / std::sqrt(d);
	return fail_value;
}

#pragma endregion

#pragma region draw_buffer

rect draw_buffer::cur_clip_rect()
{
	if (clip_rect_stack.empty())
	{
		return rect{position{0.f, 0.f}, manager->get_screen_size()};
	}
	return clip_rect_stack.back().first;
}

rect draw_buffer::clip_rect_to_cur_rect(const rect &clip)
{
	const auto cur_rect = cur_clip_rect();
	rect new_clip{
		std::max(cur_rect.xy.x, clip.xy.x),
		std::max(cur_rect.xy.y, clip.xy.y),
		std::min(cur_rect.z, clip.z),
		std::min(cur_rect.w, clip.w)
	};
	return new_clip;
}

void draw_buffer::update_clip_rect()
{
	if (!cmds.empty() && !clip_rect_stack.empty() && cur_clip_rect() == cmds.back().clip_rect.float_rect() &&
		clip_rect_stack.back().second == cmds.back().circle_scissor)
		return;

	if (!cmds.empty())
	{
		auto &cur_cmd = cmds.back();
		if (!cur_cmd.elem_count)
		{
			cur_cmd.clip_rect      = cur_clip_rect();
			cur_cmd.circle_scissor = clip_rect_stack.empty() ? false : clip_rect_stack.back().second;
			cur_cmd.tex_id         = cur_tex_id();
			cur_cmd.font_texture   = (cur_cmd.tex_id == manager->fonts->tex_id && manager->fonts->tex_id != nullptr);
			return;
		}
	}
	//Allocate new command
	draw_cmd new_cmd       = {};
	new_cmd.clip_rect      = cur_clip_rect();
	new_cmd.circle_scissor = clip_rect_stack.empty() ? false : clip_rect_stack.back().second;
	new_cmd.elem_count     = 0;
	new_cmd.tex_id         = cur_tex_id();
	new_cmd.font_texture   = (new_cmd.tex_id == manager->fonts->tex_id && manager->fonts->tex_id != nullptr);
	if (!cmds.empty())
		new_cmd.key_color = cmds.back().key_color;

	cmds.emplace_back(new_cmd);
}

void draw_buffer::push_font(font *font)
{
	cur_font = font;
	font_stack.emplace_back(font);
	push_tex_id(font->container_atlas->tex_id, true);
}

void draw_buffer::pop_font()
{
	assert(!font_stack.empty());
	pop_tex_id();
	font_stack.pop_back();
	if (!font_stack.empty())
	{
		cur_font = font_stack.back();
		push_tex_id(cur_font->container_atlas->tex_id);
		return;
	}
	cur_font = nullptr;
}

void draw_buffer::update_tex_id(const bool force_font)
{
	const auto cur_id = cur_tex_id();
	if (!cmds.empty() && !tex_id_stack.empty() && cur_id == cmds.back().tex_id && !force_font)
		return;

	if (!cmds.empty())
	{
		auto &cur_cmd = cmds.back();
		if (!cur_cmd.elem_count)
		{
			cur_cmd.clip_rect      = cur_clip_rect();
			cur_cmd.circle_scissor = clip_rect_stack.empty() ? false : clip_rect_stack.back().second;
			cur_cmd.tex_id         = cur_tex_id();
			cur_cmd.font_texture   = (cur_cmd.tex_id == manager->fonts->tex_id && manager->fonts->tex_id != nullptr)
					|| force_font;
			return;
		}
	}

	//Allocate new cmd
	draw_cmd new_cmd       = {};
	new_cmd.clip_rect      = cur_clip_rect();
	new_cmd.circle_scissor = clip_rect_stack.empty() ? false : clip_rect_stack.back().second;
	new_cmd.elem_count     = 0;
	new_cmd.tex_id         = cur_id;
	new_cmd.font_texture   = (cur_id == manager->fonts->tex_id && manager->fonts->tex_id != nullptr) || force_font;
	if (!cmds.empty())
		new_cmd.key_color = cmds.back().key_color;

	cmds.emplace_back(new_cmd);
}

size_t draw_buffer::force_new_cmd()
{
	if (!cmds.empty() && cmds.back().elem_count == 0u)
		return (cmds.size() - 1);

	// Allocate a new command
	draw_cmd new_cmd       = {};
	new_cmd.clip_rect      = cur_clip_rect();
	new_cmd.circle_scissor = clip_rect_stack.empty() ? false : clip_rect_stack.back().second;
	new_cmd.elem_count     = 0;
	new_cmd.tex_id         = cur_tex_id();
	new_cmd.font_texture   = (new_cmd.tex_id == manager->fonts->tex_id && manager->fonts->tex_id != nullptr);
	if (!cmds.empty())
		new_cmd.key_color = cmds.back().key_color;

	const auto new_idx = cmds.size();
	cmds.emplace_back(new_cmd);

	return new_idx;
}

void draw_buffer::update_matrix_translate(const position &xy_translate, const size_t cmd_idx)
{
	if (cmd_idx != -1)
	{
		if (cmds.size() <= cmd_idx)
			return;

		auto &cmd = cmds[cmd_idx];
		cmd.matrix[0][3] += xy_translate.x;
		cmd.matrix[1][3] += xy_translate.y;
		return;
	}

	if (cmds.empty())
		return;

	for (auto &cmd : cmds)
	{
		cmd.matrix[0][3] += xy_translate.x;
		cmd.matrix[1][3] += xy_translate.y;
	}
}

void draw_buffer::set_blur(const uint8_t strength)
{
	if (!cmds.empty() && cmds.back().blur_strength == strength)
		return;

	//Allocate new command
	draw_cmd new_cmd       = {};
	new_cmd.clip_rect      = cur_clip_rect();
	new_cmd.circle_scissor = clip_rect_stack.empty() ? false : clip_rect_stack.back().second;
	new_cmd.elem_count     = 0;
	new_cmd.tex_id         = cur_tex_id();
	new_cmd.font_texture   = (new_cmd.tex_id == manager->fonts->tex_id && manager->fonts->tex_id != nullptr);
	new_cmd.blur_strength  = strength;
	if (!cmds.empty())
		new_cmd.key_color = cmds.back().key_color;

	cmds.emplace_back(new_cmd);
}

void draw_buffer::set_key_color(const color col)
{
	//Allocate new command
	draw_cmd new_cmd       = {};
	new_cmd.clip_rect      = cur_clip_rect();
	new_cmd.circle_scissor = clip_rect_stack.empty() ? false : clip_rect_stack.back().second;
	new_cmd.elem_count     = 0;
	new_cmd.tex_id         = cur_tex_id();
	new_cmd.font_texture   = (new_cmd.tex_id == manager->fonts->tex_id && manager->fonts->tex_id != nullptr);
	new_cmd.key_color      = col;

	cmds.emplace_back(new_cmd);
}

void draw_buffer::triangle_filled(const position &p1,
                                  const position &p2,
                                  const position &p3,
                                  const pack_color col_p1,
                                  const pack_color col_p2,
                                  const pack_color col_p3)
{
	reserve_primitives(3, 3);
	const position uv = {1.f, 1.f};
	write_vtx(p1, uv, col_p1);
	write_vtx(p2, uv, col_p2);
	write_vtx(p3, uv, col_p3);
	write_idx(cur_idx);
	write_idx(cur_idx + 1);
	write_idx(cur_idx + 2);
	cur_idx += 3;
}

void draw_buffer::rectangle_filled(const position &top_left,
                                   const position &bot_right,
                                   const pack_color col_top_left,
                                   const pack_color col_top_right,
                                   const pack_color col_bot_left,
                                   const pack_color col_bot_right)
{
	triangle_filled({top_left.x, bot_right.y}, top_left, bot_right, col_bot_left, col_top_left, col_bot_right);
	triangle_filled(top_left, {bot_right.x, top_left.y}, bot_right, col_top_left, col_top_right, col_bot_right);
}

void draw_buffer::rectangle(const position &top_left_pre,
                            const position &bot_right_pre,
                            const pos_type thickness,
                            const pack_color col_top_left,
                            const pack_color col_top_right,
                            const pack_color col_bot_left,
                            const pack_color col_bot_right,
                            const bool clipped)
{
	const auto top_left = (clipped
		                       ? position{top_left_pre.x + thickness / 2, top_left_pre.y - thickness / 2}
		                       : position{top_left_pre.x, top_left_pre.y - thickness / 2});
	const auto bot_right = (clipped ? position{bot_right_pre.x - thickness / 2, bot_right_pre.y} : bot_right_pre);

	reserve_path(5);
	push_path(top_left);
	push_path({top_left.x, bot_right.y});
	push_path(bot_right);
	push_path({bot_right.x, top_left_pre.y});
	push_path({top_left.x + thickness / 2 - 1.f, top_left_pre.y});

	pack_color colors[] = {col_top_left, col_bot_left, col_bot_right, col_top_right, col_top_left};
	poly_line(path_data(), 5u, colors, thickness);
	clear_path();
}

void draw_buffer::circle_filled(const position &center,
                                const pos_type radius,
                                const pack_color inner_col,
                                const pack_color outer_col,
                                uint32_t parts,
                                const pos_type degrees,
                                pos_type start_degree,
                                bool anti_aliasing)
{
	start_degree += 90.f;
	parts                = std::clamp(parts, 8u, 64u);
	parts                = ((parts + 8 / 2) / 8) * 8 + 1;
	auto size_arr        = parts * sizeof(position) * (anti_aliasing ? 2 : 1);
	const auto stack_buf = reinterpret_cast<position*>(_alloca(size_arr));
	if (outer_col.a() != 255)
		anti_aliasing = false; //Not supported rn
	/*circle_filled_impl( center, radius+0.5f, sdk::color{ inner_col.r(), inner_col.g(), inner_col.b(), inner_col.a() / 2 }, 
	                    sdk::color{ outer_col.r( ), outer_col.g( ), outer_col.b( ), outer_col.a( ) / 2 }, parts, degrees, start_degree, stack_buf );*/
	circle_impl(center,
	            radius,
	            inner_col,
	            outer_col,
	            parts,
	            degrees,
	            start_degree,
	            stack_buf,
	            anti_aliasing); //TODO: Just do another circle and dont do it in circle_filled_impl
}

void draw_buffer::circle(const position &center,
                         const pos_type radius,
                         const pack_color col,
                         const pos_type thickness,
                         uint32_t parts,
                         pos_type degrees,
                         pos_type start_degree)
{
	start_degree += 90.f;
	parts                = std::clamp(parts, 8u, 64u);
	parts                = ((parts + 8 / 2) / 8) * 8 + 1;
	auto size_arr        = parts * sizeof(position);
	const auto stack_buf = reinterpret_cast<position*>(_alloca(size_arr));

	circle_impl(center, radius, col, col, parts, degrees, start_degree, stack_buf, false, false, thickness);
}

void draw_buffer::circle_impl(const position &center,
                              const pos_type radius,
                              const pack_color inner_col,
                              const pack_color outer_col,
                              const uint32_t parts,
                              const pos_type degrees,
                              pos_type start_degree,
                              position *stack_buf,
                              const bool anti_aliasing,
                              const bool fill,
                              pos_type line_thickness)
{
	const auto i = parts / 8;

	const auto k1 = 2 * i, k2 = 4 * i, k3 = 6 * i, k4 = 8 * i;
	for (auto j = 1u; j <= i; j++)
	{
		const auto pj     = circle_pos<9>()[(j != i) ? (9 / i * j) - 1 : 7] * radius;
		stack_buf[j]      = center + pj;
		stack_buf[k1 - j] = center + position{pj.y, pj.x};
		stack_buf[k1 + j] = center + position{-pj.y, pj.x};
		stack_buf[k2 - j] = center + position{-pj.x, pj.y};
		stack_buf[k2 + j] = center + position{-pj.x, -pj.y};
		stack_buf[k3 - j] = center + position{-pj.y, -pj.x};
		stack_buf[k3 + j] = center + position{pj.y, -pj.x};
		stack_buf[k4 - j] = center + position{pj.x, -pj.y};
		if (anti_aliasing && fill)
		{
			const auto apj            = circle_pos<9>()[(j != i) ? (9 / i * j) - 1 : 7] * (radius + 0.5f);
			stack_buf[parts + j]      = center + apj;
			stack_buf[parts + k1 - j] = center + position{apj.y, apj.x};
			stack_buf[parts + k1 + j] = center + position{-apj.y, apj.x};
			stack_buf[parts + k2 - j] = center + position{-apj.x, apj.y};
			stack_buf[parts + k2 + j] = center + position{-apj.x, -apj.y};
			stack_buf[parts + k3 - j] = center + position{-apj.y, -apj.x};
			stack_buf[parts + k3 + j] = center + position{apj.y, -apj.x};
			stack_buf[parts + k4 - j] = center + position{apj.x, -apj.y};
		}
	}
	stack_buf[0]  = center + position{radius, 0.f};
	stack_buf[k1] = center + position{0.f, radius};
	stack_buf[k2] = center + position{-radius, 0.f};
	stack_buf[k3] = center + position{0.f, -radius};
	stack_buf[k4] = center + position{radius, 0.f};
	if (anti_aliasing && fill)
	{
		stack_buf[parts + 0]  = center + position{(radius + 0.5f), 0.f};
		stack_buf[parts + k1] = center + position{0.f, (radius + 0.5f)};
		stack_buf[parts + k2] = center + position{-(radius + 0.5f), 0.f};
		stack_buf[parts + k3] = center + position{0.f, -(radius + 0.5f)};
		stack_buf[parts + k4] = center + position{(radius + 0.5f), 0.f};
	}

	if (degrees < 360.f)
	{
		const auto add_i = static_cast<uint32_t>(std::roundf(start_degree / (360.f / parts)));
		std::rotate(stack_buf, stack_buf + add_i, stack_buf + k4);
		if (anti_aliasing)
			std::rotate(stack_buf + parts, stack_buf + parts + add_i, stack_buf + parts + k4);
	}
	auto end_i = static_cast<uint32_t>(std::roundf(degrees / (360.f / parts)));
	end_i      = std::clamp(end_i, 0u, k4);
	//poly_fill( stack_buf, end_i, outer_col );
	//poly_line( stack_buf, end_i, outer_col, 2.f, true );
	//if( anti_aliasing )
	//    fill_circle_impl( center, stack_buf + parts, end_i + 1,
	//                     color{ outer_col.r( ), outer_col.g( ), outer_col.b( ), outer_col.a( ) / 2 } );
	if (anti_aliasing && fill)
		poly_line(stack_buf + parts,
		          end_i,
		          color{outer_col.r(), outer_col.g(), outer_col.b(), outer_col.a() / 2},
		          1.f,
		          true);

	if (fill)
		fill_circle_impl(center, stack_buf, end_i + 1, inner_col, outer_col);
	else
		poly_line(stack_buf, parts, outer_col, line_thickness, false);
}

void draw_buffer::fill_circle_impl(const position &center,
                                   position *vtx,
                                   const uint32_t vtx_count,
                                   const pack_color col_inner,
                                   const pack_color col_outer)
{
	if (vtx_count < 2)
		return;
	const auto uv = manager->fonts->tex_uv_white_pixel;

	prim_reserve((vtx_count - 1) * 3, vtx_count + 1);
	write_vtx(center, uv, col_inner);
	const auto center_idx = cur_idx;
	cur_idx++;

	write_vtx(*vtx, uv, col_outer);
	for (auto i = 1u; i < vtx_count; i++)
	{
		write_vtx(vtx[i], uv, col_outer);
		write_idx(center_idx);
		write_idx(cur_idx + 1);
		write_idx(cur_idx);
		cur_idx++;
	}
	cur_idx++;
}


//TODO: Instead of dupe code, stack alloc points_count colors and fill with col in a inlined func?
void draw_buffer::poly_line(position *points,
                            const uint32_t points_count,
                            const pack_color col,
                            const pos_type thickness,
                            const bool anti_aliased)
{
	// from dear imgui
	if (points_count < 2)
		return;

	constexpr auto aa_size = 1.f;
	const auto uv          = position{1.f, 1.f};
	const auto thick_line  = thickness > 1.f;
	const auto col_trans   = pack_color{col.r(), col.g(), col.b(), 0};
	const auto count       = points_count - 1;

	if (anti_aliased)
	{
		const auto idx_count = thick_line ? count * 18 : count * 12;
		const auto vtx_count = thick_line ? points_count * 4 : points_count * 3;
		reserve_primitives(idx_count, vtx_count);

		const auto tmp_normals = reinterpret_cast<position*>(alloca(
			points_count * (thick_line ? 5 : 3) * sizeof(position)));
		const auto tmp_points = tmp_normals + points_count;

		for (auto i = 0u; i < count; ++i)
		{
			const auto j     = (i + 1 == points_count) ? 0 : i + 1;
			const auto delta = (points[j] - points[i]).normalize();
			tmp_normals[i]   = {delta.y, -delta.x};
		}
		tmp_normals[points_count - 1] = tmp_normals[points_count - 2];

		if (!thick_line)
		{
			tmp_points[0]                          = points[0] + tmp_normals[0] * aa_size;
			tmp_points[1]                          = points[0] - tmp_normals[0] * aa_size;
			tmp_points[(points_count - 1) * 2 + 0] = points[points_count - 1] + tmp_normals[points_count - 1] * aa_size;
			tmp_points[(points_count - 1) * 2 + 1] = points[points_count - 1] - tmp_normals[points_count - 1] * aa_size;

			auto idx = cur_idx;
			for (auto i = 0u; i < count; ++i)
			{
				const auto j    = (i + 1 == points_count) ? 0 : i + 1;
				const auto idx2 = (i + 1 == points_count) ? cur_idx : idx + 3;

				auto dm = ((tmp_normals[i] + tmp_normals[j]) * 0.5);
				// IM_FIXNORMAL2F
				auto len_sqr = dm.length_sqr();
				if (len_sqr < 0.5f)
					len_sqr = 0.5f;
				dm *= (1.f / len_sqr);

				dm *= aa_size;

				const auto vtx_out = &tmp_points[j * 2];
				vtx_out[0]         = points[j] + dm;
				vtx_out[1]         = points[j] - dm;

				write_idx(idx2);
				write_idx(idx);
				write_idx(idx + 2);
				write_idx(idx + 2);
				write_idx(idx2 + 2);
				write_idx(idx2);
				write_idx(idx2 + 1);
				write_idx(idx + 1);
				write_idx(idx);
				write_idx(idx);
				write_idx(idx2);
				write_idx(idx2 + 1);

				idx = idx2;
			}

			for (auto i = 0u; i < points_count; ++i)
			{
				write_vtx(points[i], uv, col);
				write_vtx(tmp_points[i * 2], uv, col_trans);
				write_vtx(tmp_points[i * 2 + 1], uv, col_trans);
			}
		}
		else
		{
			const auto half_inner_thickness    = (thickness - aa_size) * 0.5f;
			tmp_points[0]                      = points[0] + tmp_normals[0] * (half_inner_thickness + aa_size);
			tmp_points[1]                      = points[0] + tmp_normals[0] * (half_inner_thickness);
			tmp_points[2]                      = points[0] - tmp_normals[0] * (half_inner_thickness);
			tmp_points[3]                      = points[0] - tmp_normals[0] * (half_inner_thickness + aa_size);
			tmp_points[(points_count - 1) * 4] = points[points_count - 1] + tmp_normals[points_count - 1] * (
				half_inner_thickness + aa_size);
			tmp_points[(points_count - 1) * 4 + 1] = points[points_count - 1] + tmp_normals[points_count - 1] * (
				half_inner_thickness);
			tmp_points[(points_count - 1) * 4 + 2] = points[points_count - 1] - tmp_normals[points_count - 1] * (
				half_inner_thickness);
			tmp_points[(points_count - 1) * 4 + 3] = points[points_count - 1] - tmp_normals[points_count - 1] * (
				half_inner_thickness + aa_size);

			auto idx = cur_idx;
			for (auto i = 0u; i < count; ++i)
			{
				const auto j    = (i + 1 == points_count) ? 0 : i + 1;
				const auto idx2 = (i + 1 == points_count) ? cur_idx : idx + 4;

				auto dm = (tmp_normals[i] + tmp_normals[j]) * 0.5f;
				// IM_FIXNORMAL2F
				auto len_sqr = dm.length_sqr();
				if (len_sqr < 0.5f)
					len_sqr = 0.5f;
				dm *= (1.f / len_sqr);

				const auto dm_out = dm * (half_inner_thickness + aa_size);
				const auto dm_in  = dm * half_inner_thickness;

				const auto vtx_out = &tmp_points[j * 4];
				vtx_out[0]         = points[j] + dm_out;
				vtx_out[1]         = points[j] + dm_in;
				vtx_out[2]         = points[j] - dm_in;
				vtx_out[3]         = points[j] - dm_out;

				write_idx(idx2 + 1);
				write_idx(idx + 1);
				write_idx(idx + 2);
				write_idx(idx + 2);
				write_idx(idx2 + 2);
				write_idx(idx2 + 1);
				write_idx(idx2 + 1);
				write_idx(idx + 1);
				write_idx(idx);
				write_idx(idx);
				write_idx(idx2);
				write_idx(idx2 + 1);
				write_idx(idx2 + 2);
				write_idx(idx + 2);
				write_idx(idx + 3);
				write_idx(idx + 3);
				write_idx(idx2 + 3);
				write_idx(idx2 + 2);

				idx = idx2;
			}

			for (auto i = 0u; i < points_count; ++i)
			{
				write_vtx(tmp_points[i * 4], uv, col_trans);
				write_vtx(tmp_points[i * 4 + 1], uv, col);
				write_vtx(tmp_points[i * 4 + 2], uv, col);
				write_vtx(tmp_points[i * 4 + 3], uv, col_trans);
			}
		}
		cur_idx += vtx_count;
	}
	else
	{
		const auto idx_count = count * 6;
		const auto vtx_count = count * 4;
		reserve_primitives(idx_count, vtx_count);

		for (auto i = 0u; i < count; ++i)
		{
			const auto j   = (i + 1 == points_count) ? 0 : i + 1;
			const auto &p1 = points[i];
			const auto &p2 = points[j];

			auto d = (p2 - p1).normalize();
			d *= (thickness * 0.5f);
			std::swap(d.x, d.y);
			d.x = -d.x;

			write_vtx(p1 + d, uv, col);
			write_vtx(p2 + d, uv, col);
			write_vtx(p2 - d, uv, col);
			write_vtx(p1 - d, uv, col);

			write_idx(cur_idx);
			write_idx(cur_idx + 1);
			write_idx(cur_idx + 2);
			write_idx(cur_idx);
			write_idx(cur_idx + 2);
			write_idx(cur_idx + 3);

			cur_idx += 4;
		}
	}
}

void draw_buffer::poly_line(position *points,
                            const uint32_t points_count,
                            const pack_color *cols,
                            const pos_type thickness,
                            const bool anti_aliased)
{
	// from dear imgui
	if (points_count < 2)
		return;

	constexpr auto aa_size = 1.f;
	const auto uv          = position{1.f, 1.f};
	const auto thick_line  = thickness > 1.f;
	//const auto col_trans = pack_color{ col.r(), col.g(), col.b(), 0 };
	const auto count = points_count - 1;

	if (anti_aliased)
	{
		const auto idx_count = thick_line ? count * 18 : count * 12;
		const auto vtx_count = thick_line ? points_count * 4 : points_count * 3;
		reserve_primitives(idx_count, vtx_count);

		const auto tmp_normals = reinterpret_cast<position*>(alloca(
			points_count * (thick_line ? 5 : 3) * sizeof(position)));
		const auto tmp_points = tmp_normals + points_count;

		for (auto i = 0u; i < count; ++i)
		{
			const auto j     = (i + 1 == points_count) ? 0 : i + 1;
			const auto delta = (points[j] - points[i]).normalize();
			tmp_normals[i]   = {delta.y, -delta.x};
		}
		tmp_normals[points_count - 1] = tmp_normals[points_count - 2];

		if (!thick_line)
		{
			tmp_points[0]                          = points[0] + tmp_normals[0] * aa_size;
			tmp_points[1]                          = points[0] - tmp_normals[0] * aa_size;
			tmp_points[(points_count - 1) * 2 + 0] = points[points_count - 1] + tmp_normals[points_count - 1] * aa_size;
			tmp_points[(points_count - 1) * 2 + 1] = points[points_count - 1] - tmp_normals[points_count - 1] * aa_size;

			auto idx = cur_idx;
			for (auto i = 0u; i < count; ++i)
			{
				const auto j    = (i + 1 == points_count) ? 0 : i + 1;
				const auto idx2 = (i + 1 == points_count) ? cur_idx : idx + 3;

				auto dm = ((tmp_normals[i] + tmp_normals[j]) * 0.5);
				// IM_FIXNORMAL2F
				auto len_sqr = dm.length_sqr();
				if (len_sqr < 0.5f)
					len_sqr = 0.5f;
				dm *= (1.f / len_sqr);

				dm *= aa_size;

				const auto vtx_out = &tmp_points[j * 2];
				vtx_out[0]         = points[j] + dm;
				vtx_out[1]         = points[j] - dm;

				write_idx(idx2);
				write_idx(idx);
				write_idx(idx + 2);
				write_idx(idx + 2);
				write_idx(idx2 + 2);
				write_idx(idx2);
				write_idx(idx2 + 1);
				write_idx(idx + 1);
				write_idx(idx);
				write_idx(idx);
				write_idx(idx2);
				write_idx(idx2 + 1);

				idx = idx2;
			}

			for (auto i = 0u; i < points_count; ++i)
			{
				const auto col       = cols[i];
				const auto col_trans = pack_color{col.r(), col.g(), col.b(), 0};
				write_vtx(points[i], uv, col);
				write_vtx(tmp_points[i * 2], uv, col_trans);
				write_vtx(tmp_points[i * 2 + 1], uv, col_trans);
			}
		}
		else
		{
			const auto half_inner_thickness    = (thickness - aa_size) * 0.5f;
			tmp_points[0]                      = points[0] + tmp_normals[0] * (half_inner_thickness + aa_size);
			tmp_points[1]                      = points[0] + tmp_normals[0] * (half_inner_thickness);
			tmp_points[2]                      = points[0] - tmp_normals[0] * (half_inner_thickness);
			tmp_points[3]                      = points[0] - tmp_normals[0] * (half_inner_thickness + aa_size);
			tmp_points[(points_count - 1) * 4] = points[points_count - 1] + tmp_normals[points_count - 1] * (
				half_inner_thickness + aa_size);
			tmp_points[(points_count - 1) * 4 + 1] = points[points_count - 1] + tmp_normals[points_count - 1] * (
				half_inner_thickness);
			tmp_points[(points_count - 1) * 4 + 2] = points[points_count - 1] - tmp_normals[points_count - 1] * (
				half_inner_thickness);
			tmp_points[(points_count - 1) * 4 + 3] = points[points_count - 1] - tmp_normals[points_count - 1] * (
				half_inner_thickness + aa_size);

			auto idx = cur_idx;
			for (auto i = 0u; i < count; ++i)
			{
				const auto j    = (i + 1 == points_count) ? 0 : i + 1;
				const auto idx2 = (i + 1 == points_count) ? cur_idx : idx + 4;

				auto dm = (tmp_normals[i] + tmp_normals[j]) * 0.5f;
				dm.normalize();
				const auto dm_out = dm * (half_inner_thickness + aa_size);
				const auto dm_in  = dm * half_inner_thickness;

				const auto vtx_out = &tmp_points[j * 4];
				vtx_out[0]         = points[j] + dm_out;
				vtx_out[1]         = points[j] + dm_in;
				vtx_out[2]         = points[j] - dm_in;
				vtx_out[3]         = points[j] - dm_out;

				write_idx(idx2 + 1);
				write_idx(idx + 1);
				write_idx(idx + 2);
				write_idx(idx + 2);
				write_idx(idx2 + 2);
				write_idx(idx2 + 1);
				write_idx(idx2 + 1);
				write_idx(idx + 1);
				write_idx(idx);
				write_idx(idx);
				write_idx(idx2);
				write_idx(idx2 + 1);
				write_idx(idx2 + 2);
				write_idx(idx + 2);
				write_idx(idx + 3);
				write_idx(idx + 3);
				write_idx(idx2 + 3);
				write_idx(idx2 + 2);

				idx = idx2;
			}

			for (auto i = 0u; i < points_count; ++i)
			{
				const auto col       = cols[i];
				const auto col_trans = pack_color{col.r(), col.g(), col.b(), 0};
				write_vtx(tmp_points[i * 4], uv, col_trans);
				write_vtx(tmp_points[i * 4 + 1], uv, col);
				write_vtx(tmp_points[i * 4 + 2], uv, col);
				write_vtx(tmp_points[i * 4 + 3], uv, col_trans);
			}
		}
		cur_idx += vtx_count;
	}
	else
	{
		const auto idx_count = count * 6;
		const auto vtx_count = count * 4;
		reserve_primitives(idx_count, vtx_count);

		for (auto i = 0u; i < count; ++i)
		{
			const auto j   = (i + 1 == points_count) ? 0 : i + 1;
			const auto &p1 = points[i];
			const auto &p2 = points[j];

			auto d = (p2 - p1).normalize();
			d *= (thickness * 0.5f);
			std::swap(d.x, d.y);
			d.x = -d.x;

			const auto col = cols[i];
			write_vtx(p1 + d, uv, col);
			write_vtx(p2 + d, uv, col);
			write_vtx(p2 - d, uv, col);
			write_vtx(p1 - d, uv, col);

			write_idx(cur_idx);
			write_idx(cur_idx + 1);
			write_idx(cur_idx + 2);
			write_idx(cur_idx);
			write_idx(cur_idx + 2);
			write_idx(cur_idx + 3);

			cur_idx += 4;
		}
	}
}

void draw_buffer::poly_fill(position *points, const uint32_t count, const pack_color *col)
{
	const auto uv        = manager->fonts->tex_uv_white_pixel;
	const auto idx_count = (count - 2) * 3;
	reserve_primitives(idx_count, count);
	for (auto i = 0u; i < count; i++)
		write_vtx(points[i], uv, col[i]);
	for (auto i = 2u; i < count; i++)
	{
		write_idx(cur_idx);
		write_idx(cur_idx + i - 1);
		write_idx(cur_idx + i);
	}
	cur_idx += count;
}

void draw_buffer::poly_fill(position *points, const uint32_t count, const pack_color col)
{
	const auto uv        = manager->fonts->tex_uv_white_pixel;
	const auto idx_count = (count - 2) * 3;
	reserve_primitives(idx_count, count);
	for (auto i = 0u; i < count; i++)
		write_vtx(points[i], uv, col);
	for (auto i = 2u; i < count; i++)
	{
		write_idx(cur_idx);
		write_idx(cur_idx + i - 1);
		write_idx(cur_idx + i);
	}
	cur_idx += count;
}

void draw_buffer::prim_reserve(uint32_t idx_count, uint32_t vtx_count)
{
	reserve_primitives(idx_count, vtx_count);
}

void draw_buffer::prim_rect(const position &a, const position &c, const pack_color col)
{
	position b{c.x, a.y}, d{a.x, c.y}, uv{1.f, 1.f};
	auto idx             = cur_idx;
	idx_write_ptr[0]     = idx;
	idx_write_ptr[1]     = (idx + 1);
	idx_write_ptr[2]     = (idx + 2);
	idx_write_ptr[3]     = idx;
	idx_write_ptr[4]     = (idx + 2);
	idx_write_ptr[5]     = (idx + 3);
	vtx_write_ptr[0].pos = a;
	vtx_write_ptr[0].uv  = uv;
	vtx_write_ptr[0].col = col;
	vtx_write_ptr[1].pos = b;
	vtx_write_ptr[1].uv  = uv;
	vtx_write_ptr[1].col = col;
	vtx_write_ptr[2].pos = c;
	vtx_write_ptr[2].uv  = uv;
	vtx_write_ptr[2].col = col;
	vtx_write_ptr[3].pos = d;
	vtx_write_ptr[3].uv  = uv;
	vtx_write_ptr[3].col = col;
	vtx_write_ptr += 4;
	cur_idx += 4;
	idx_write_ptr += 6;
}

void draw_buffer::prim_rect_uv(const position &a,
                               const position &c,
                               const position &uv_a,
                               const position &uv_c,
                               const pack_color col)
{
	const position b{c.x, a.y}, d{a.x, c.y}, uv_b{uv_c.x, uv_a.y}, uv_d{uv_a.x, uv_c.y};
	const auto idx       = cur_idx;
	idx_write_ptr[0]     = idx;
	idx_write_ptr[1]     = (idx + 1);
	idx_write_ptr[2]     = (idx + 2);
	idx_write_ptr[3]     = idx;
	idx_write_ptr[4]     = (idx + 2);
	idx_write_ptr[5]     = (idx + 3);
	vtx_write_ptr[0].pos = a;
	vtx_write_ptr[0].uv  = uv_a;
	vtx_write_ptr[0].col = col;
	vtx_write_ptr[1].pos = b;
	vtx_write_ptr[1].uv  = uv_b;
	vtx_write_ptr[1].col = col;
	vtx_write_ptr[2].pos = c;
	vtx_write_ptr[2].uv  = uv_c;
	vtx_write_ptr[2].col = col;
	vtx_write_ptr[3].pos = d;
	vtx_write_ptr[3].uv  = uv_d;
	vtx_write_ptr[3].col = col;
	vtx_write_ptr += 4;
	cur_idx += 4;
	idx_write_ptr += 6;
}

void draw_buffer::prim_quad_uv(const position &tl,
                               const position &tr,
                               const position &bl,
                               const position &br,
                               const position &uv1,
                               const position &uv2,
                               const position &uv3,
                               const position &uv4,
                               const pack_color col)
{
	const auto idx   = cur_idx;
	idx_write_ptr[0] = idx;
	idx_write_ptr[1] = (idx + 1);
	idx_write_ptr[2] = (idx + 2);
	idx_write_ptr[3] = (idx + 1);
	idx_write_ptr[4] = (idx + 3);
	idx_write_ptr[5] = (idx + 2);

	vtx_write_ptr[0].pos = tl;
	vtx_write_ptr[0].uv  = uv1;
	vtx_write_ptr[0].col = col;
	vtx_write_ptr[1].pos = tr;
	vtx_write_ptr[1].uv  = uv2;
	vtx_write_ptr[1].col = col;
	vtx_write_ptr[2].pos = bl;
	vtx_write_ptr[2].uv  = uv3;
	vtx_write_ptr[2].col = col;
	vtx_write_ptr[3].pos = br;
	vtx_write_ptr[3].uv  = uv4;
	vtx_write_ptr[3].col = col;

	vtx_write_ptr += 4;
	cur_idx += 4;
	idx_write_ptr += 6;
}

void draw_buffer::text(font *font,
                       const char *text,
                       const position &top_left,
                       const pack_color col,
                       const bool outline,
                       const position &bot_right)
{
	if (col.a() == 0)
		return;

	if (font != nullptr)
		push_font(font);

	assert(cur_font);

	auto clip_rect = cur_clip_rect();
	if (bot_right.x != -1.f && bot_right.y != -1.f)
	{
		clip_rect.z = bot_right.x;
		clip_rect.w = bot_right.y;
	}

	if (outline)
	{
		const auto col_out = color{0, 0, 0};
		auto copy_clip     = clip_rect;
		copy_clip.xy       = top_left;
		copy_clip.x -= 1.f;
		copy_clip.z -= 1.f;
		this->text(nullptr, text, copy_clip.xy, col_out, false, copy_clip.zw);
		copy_clip.x += 2.f;
		copy_clip.z += 2.f;
		if (copy_clip.z < 0)
			copy_clip.z = std::numeric_limits<float>::max();
		this->text(nullptr, text, copy_clip.xy, col_out, false, copy_clip.zw);
		copy_clip.x -= 1.f,
				copy_clip.z -= 1.f;
		copy_clip.y -= 1.f;
		copy_clip.w -= 1.f;
		this->text(nullptr, text, copy_clip.xy, col_out, false, copy_clip.zw);
		copy_clip.y += 2.f;
		copy_clip.w += 2.f;
		if (copy_clip.w < 0)
			copy_clip.w = std::numeric_limits<float>::max();
		this->text(nullptr, text, copy_clip.xy, col_out, false, copy_clip.zw);
	}

	cur_font->render_text(this, cur_font->font_size, top_left, col, clip_rect, text, text + strlen(text));

	if (font != nullptr)
		pop_font();
}

void draw_buffer::text(font *font,
                       float target_size,
                       const char *text,
                       const position &top_left,
                       const pack_color col,
                       bool outline,
                       const position &bot_right)
{
	if (col.a() == 0)
		return;

	if (font != nullptr)
		push_font(font);

	assert(cur_font);

	auto clip_rect = cur_clip_rect();
	if (bot_right.x != -1.f && bot_right.y != -1.f)
	{
		clip_rect.z = bot_right.x;
		clip_rect.w = bot_right.y;
	}

	if (outline)
	{
		const auto col_out = color{0, 0, 0};
		auto copy_clip     = clip_rect;
		copy_clip.xy       = top_left;
		copy_clip.x -= 1.f;
		copy_clip.z -= 1.f;
		this->text(nullptr, target_size, text, copy_clip.xy, col_out, false, copy_clip.zw);
		copy_clip.x += 2.f;
		copy_clip.z += 2.f;
		if (copy_clip.z < 0)
			copy_clip.z = std::numeric_limits<float>::max();
		this->text(nullptr, target_size, text, copy_clip.xy, col_out, false, copy_clip.zw);
		copy_clip.x -= 1.f,
				copy_clip.z -= 1.f;
		copy_clip.y -= 1.f;
		copy_clip.w -= 1.f;
		this->text(nullptr, target_size, text, copy_clip.xy, col_out, false, copy_clip.zw);
		copy_clip.y += 2.f;
		copy_clip.w += 2.f;
		if (copy_clip.w < 0)
			copy_clip.w = std::numeric_limits<float>::max();
		this->text(nullptr, target_size, text, copy_clip.xy, col_out, false, copy_clip.zw);
	}

	cur_font->render_text(this, target_size, top_left, col, clip_rect, text, text + strlen(text));

	if (font != nullptr)
		pop_font();
}


position draw_buffer::text_size(font *font, const char *text, const char *text_end) const
{
	if (!font)
		font = cur_font;
	assert(font);

	if (!strlen(text))
		return position{0.f, font->font_size};

	auto text_size = font->calc_text_size(font->font_size, FLT_MAX, -1.f, text, text_end);

	// Cancel out character spacing for the last character of a line (it is baked into glyph->AdvanceX field)
	if (text_size.x > 0.0f)
		text_size.x -= 1.0f;
	text_size.x = std::roundf(text_size.x + 0.95f);

	return text_size;
}

position draw_buffer::text_size(font *font, float target_size, const char *text, const char *text_end) const
{
	if (!font)
		font = cur_font;
	assert(font);

	if (!strlen(text))
		return position{0.f, font->font_size};

	auto text_size = font->calc_text_size(target_size, FLT_MAX, -1.f, text, text_end);

	// Cancel out character spacing for the last character of a line (it is baked into glyph->AdvanceX field)
	if (text_size.x > 0.0f)
		text_size.x -= 1.0f;
	text_size.x = std::roundf(text_size.x + 0.95f);

	return text_size;
}

rect draw_buffer::text_bounds(font *font, const char *text, const char *text_end) const
{
	if (!font)
		font = cur_font;
	assert(font);

	if (!strlen(text))
		return rect{0.f, 0.f, 0.f, font->font_size};

	auto text_bounds = font->calc_text_bounds(font->font_size, FLT_MAX, -1.f, text, text_end);

	// Cancel out character spacing for the last character of a line (it is baked into glyph->AdvanceX field)
	if (text_bounds.z > 0.0f)
		text_bounds.z -= 1.0f;
	text_bounds.z = std::roundf(text_bounds.z + 0.95f);

	return text_bounds;
}

rect draw_buffer::text_bounds(font *font, float target_size, const char *text, const char *text_end) const
{
	if (!font)
		font = cur_font;
	assert(font);

	if (!strlen(text))
		return rect{0.f, 0.f, 0.f, target_size};

	auto text_bounds = font->calc_text_bounds(target_size, FLT_MAX, -1.f, text, text_end);

	// Cancel out character spacing for the last character of a line (it is baked into glyph->AdvanceX field)
	if (text_bounds.z > 0.0f)
		text_bounds.z -= 1.0f * (target_size / font->font_size);
	//text_bounds.z = std::roundf(text_bounds.z + 0.95f);

	return text_bounds;
}


void draw_buffer::reserve_primitives(const std::uint32_t idx_count, const std::uint32_t vtx_count)
{
	const auto vtx_old_size = vertices.size();
	const auto idx_old_size = indices.size();
	vertices.resize(vtx_old_size + vtx_count);
	indices.resize(idx_old_size + idx_count);

	vtx_write_ptr = &vertices[vtx_old_size];
	idx_write_ptr = &indices[idx_old_size];

	cmds.back().elem_count += idx_count;
	cmds.back().vtx_count += vtx_count;
}

#pragma endregion

#pragma region draw_manager

size_t draw_manager::register_buffer(const size_t init_priority)
{
	std::lock_guard<std::mutex> g(_list_mutex);
	auto new_idx = _buffer_list.size();
	if (!_free_buffers.empty())
	{
		new_idx = _free_buffers.back();
		_free_buffers.erase(std::prev(_free_buffers.end()));
	}
	else
	{
		_buffer_list.emplace_back(buffer_node{});
	}

	auto &element          = _buffer_list[new_idx];
	element.active_buffer  = std::make_unique<draw_buffer>(this);
	element.working_buffer = std::make_unique<draw_buffer>(this);
	update_buffer_ptrs();

	_priorities.emplace_back(std::make_pair(init_priority, new_idx));
	sort_priorities();

	return new_idx;
}

size_t draw_manager::register_child_buffer(size_t parent, size_t priority)
{
	std::lock_guard<std::mutex> g(_list_mutex);
	auto new_idx = _buffer_list.size();
	if (!_free_buffers.empty())
	{
		new_idx = _free_buffers.back();
		_free_buffers.erase(std::prev(_free_buffers.end()));
	}
	else
	{
		_buffer_list.emplace_back(buffer_node{});
	}

	auto &element                           = _buffer_list[new_idx];
	element.active_buffer                   = std::make_unique<draw_buffer>(this);
	element.working_buffer                  = std::make_unique<draw_buffer>(this);
	element.active_buffer->is_child_buffer  = true;
	element.working_buffer->is_child_buffer = true;
	element.parent                          = parent;
	auto &vec                               = _buffer_list[parent].child_buffers;
	vec.emplace_back(std::make_pair(priority, new_idx));
	std::sort(vec.begin(),
	          vec.end(),
	          [](auto &first, auto &sec) -> bool
	          {
		          return first.first > sec.first;
	          });
	update_buffer_ptrs();

	return new_idx;
}

void draw_manager::update_child_priority(const size_t child_idx, const size_t new_priority)
{
	std::lock_guard<std::mutex> g(_list_mutex);
	assert(child_idx < _buffer_list.size( ));

	const auto &child = _buffer_list[child_idx];
	auto &parent      = _buffer_list[child.parent];
	const auto it     = std::find_if(parent.child_buffers.begin(),
	                                 parent.child_buffers.end(),
	                                 [ child_idx ](auto &pair)
	                                 {
		                                 return (pair.second == child_idx);
	                                 });
	if (it != parent.child_buffers.end())
	{
		it->first = new_priority;
		std::sort(parent.child_buffers.begin(),
		          parent.child_buffers.end(),
		          [ ](auto &first, auto &sec) -> bool
		          {
			          return first.first > sec.first;
		          });
	}
}

void draw_manager::update_buffer_priority(const size_t buffer_idx, const size_t new_priority)
{
	std::lock_guard<std::mutex> g(_list_mutex);
	assert(buffer_idx < _buffer_list.size( ));

	const auto &node = _buffer_list[buffer_idx];

	const auto it = std::find_if(_priorities.begin(),
	                             _priorities.end(),
	                             [ buffer_idx ](auto &pair)
	                             {
		                             return (pair.second == buffer_idx);
	                             });
	if (it == _priorities.end())
		return;

	it->first = new_priority;
	sort_priorities();
}


void draw_manager::remove_buffer(const size_t idx)
{
	std::lock_guard<std::mutex> g(_list_mutex);
	assert(idx < _buffer_list.size( ));

	//Clear/Free child buffers
	const auto free_buffer = [=](const size_t buf, auto &self_ref) -> void
	{
		auto &element = _buffer_list[buf];
		_free_buffers.emplace_back(buf);
		element.is_free = true;

		//Clear buffers
		element.active_buffer->clear_buffers();
		element.working_buffer->clear_buffers();
		element.active_buffer->is_child_buffer  = false;
		element.working_buffer->is_child_buffer = false;

		if (element.parent != -1)
		{
			auto &parent   = _buffer_list[element.parent];
			element.parent = -1;
			const auto it  = std::find_if(parent.child_buffers.begin(),
			                              parent.child_buffers.end(),
			                              [buf](auto &pair)
			                              {
				                              return (pair.second == buf);
			                              });
			if (it != parent.child_buffers.end())
				parent.child_buffers.erase(it);
		}

		for (auto &child : element.child_buffers)
		{
			self_ref(child.second, self_ref);
		}
	};

	free_buffer(idx, free_buffer);
}

void draw_manager::update_buffer_ptrs() { }

draw_buffer* draw_manager::get_buffer(const size_t idx)
{
	std::lock_guard<std::mutex> g(_list_mutex);
	assert(idx < _buffer_list.size());
	return _buffer_list[idx].working_buffer.get();
}

void draw_manager::swap_buffers(const size_t idx)
{
	std::lock_guard<std::mutex> g(_list_mutex);
	assert(idx < _buffer_list.size( ));

	const auto swap_buffer = [=](const size_t buf, const auto &self_ref) -> void
	{
		auto &element = _buffer_list[buf];
		element.active_buffer.swap(element.working_buffer);
		element.working_buffer->clear_buffers();
		for (auto &child : element.child_buffers)
			self_ref(child.second, self_ref);
	};

	swap_buffer(idx, swap_buffer);
}

font* draw_manager::add_font(const char *file,
                             const float size,
                             const bool italic,
                             const bool bold,
                             const GLYPH_RANGES range,
                             const int rasterizer_flags) const
{
	auto font_cfg = font_config();
	if (italic)
		font_cfg.rasterizer_flags |= OBLIQUE;
	if (bold)
		font_cfg.rasterizer_flags |= BOLD;

	font_cfg.rasterizer_flags |= rasterizer_flags;

	const font_wchar *ranges = nullptr;
	if (range & GLYPH_RANGE_LATIN)
		ranges = fonts->glyph_ranges_default();
	if (range & GLYPH_RANGE_JAPANESE)
		ranges = fonts->glyph_ranges_japanese();

	return fonts->add_font_from_ttf(file, size, &font_cfg, ranges);
}

font* draw_manager::add_font(const char *file,
                             float size,
                             const font_wchar *glyph_ranges,
                             const bool italic,
                             const bool bold,
                             const int rasterizer_flags) const
{
	auto font_cfg = font_config();
	if (italic)
		font_cfg.rasterizer_flags |= OBLIQUE;
	if (bold)
		font_cfg.rasterizer_flags |= BOLD;

	font_cfg.rasterizer_flags |= rasterizer_flags;

	return fonts->add_font_from_ttf(file, size, &font_cfg, glyph_ranges);
}

font* draw_manager::add_font_mem(uint8_t *data,
                                 size_t data_size,
                                 float font_size,
                                 bool italic,
                                 bool bold,
                                 GLYPH_RANGES range,
                                 const int rasterizer_flags) const
{
	auto font_cfg = font_config();
	if (italic)
		font_cfg.rasterizer_flags |= OBLIQUE;
	if (bold)
		font_cfg.rasterizer_flags |= BOLD;

	font_cfg.rasterizer_flags |= rasterizer_flags;

	const font_wchar *ranges = nullptr;
	if (range & GLYPH_RANGE_LATIN)
		ranges = fonts->glyph_ranges_default();
	if (range & GLYPH_RANGE_JAPANESE)
		ranges = fonts->glyph_ranges_japanese();

	return fonts->add_font_from_ttf_mem(data, data_size, font_size, &font_cfg, ranges);
}

void draw_manager::remove_font(const font *font_ptr) const
{
	fonts->remove_font(font_ptr);
}

void draw_manager::update_matrix_translate(const size_t buffer, const position &xy_translate, const size_t cmd_idx)
{
	std::lock_guard<std::mutex> g(_list_mutex);
	assert(buffer < _buffer_list.size());

	auto &buffer_pair = _buffer_list[buffer];
	buffer_pair.active_buffer->update_matrix_translate(xy_translate, cmd_idx);
}

void draw_manager::init()
{
	fonts = std::make_unique<font_atlas>();
	_buffer_list.reserve(1000);
}


#pragma endregion

#pragma region convenient_funcs

void util::draw::rectangle_filled_rounded(draw_buffer *buf,
                                          const position &top_left,
                                          const position &bot_right,
                                          const pos_type radius,
                                          const pack_color col_top_left,
                                          const pack_color col_top_right,
                                          const pack_color col_bot_left,
                                          const pack_color col_bot_right,
                                          const uint8_t flags)
{
	const auto corner_tl = position{top_left.x + radius, top_left.y + radius};
	const auto corner_tr = position{bot_right.x - radius, top_left.y + radius};
	const auto corner_bl = position{top_left.x + radius, bot_right.y - radius};
	const auto corner_br = position{bot_right.x - radius, bot_right.y - radius};
	buf->rectangle_filled(corner_tl, corner_br, col_top_left, col_top_right, col_bot_left, col_bot_right);

	buf->rectangle_filled({top_left.x, top_left.y + radius},
	                      {top_left.x + radius, bot_right.y - radius},
	                      col_top_left,
	                      col_top_left,
	                      col_bot_left,
	                      col_bot_left); //Left Side
	buf->rectangle_filled({corner_tl.x, bot_right.y - radius},
	                      {bot_right.x - radius, bot_right.y},
	                      col_bot_left,
	                      col_bot_right,
	                      col_bot_left,
	                      col_bot_right); //Bot Side
	buf->rectangle_filled(corner_tr,
	                      {bot_right.x, corner_br.y},
	                      col_top_right,
	                      col_top_right,
	                      col_bot_right,
	                      col_bot_right); //Right Side
	buf->rectangle_filled({corner_tl.x, top_left.y},
	                      corner_tr,
	                      col_top_left,
	                      col_top_right,
	                      col_top_left,
	                      col_top_right);

	if (flags & ROUND_RECT_TL)
		buf->circle_filled(corner_tl, radius, col_top_left, col_top_left, 64, 90, 90);
	else
		buf->rectangle_filled(top_left, corner_tl, col_top_left);

	if (flags & ROUND_RECT_TR)
		buf->circle_filled(corner_tr, radius, col_top_right, col_top_right, 64, 90, 180);
	else
		buf->rectangle_filled({corner_tr.x, top_left.y}, {bot_right.x, corner_tr.y}, col_top_right);

	if (flags & ROUND_RECT_BR)
		buf->circle_filled(corner_br, radius, col_bot_right, col_bot_right, 64, 90, 270);
	else
		buf->rectangle_filled(corner_br, bot_right, col_bot_right);

	if (flags & ROUND_RECT_BL)
		buf->circle_filled(corner_bl, radius, col_bot_left, col_bot_left, 64, 90, 0);
	else
		buf->rectangle_filled({top_left.x, corner_bl.y}, {corner_bl.x, bot_right.y}, col_bot_left);

	//buf->rectangle_filled( top_left, bot_right, col_top_left, col_top_right, col_bot_left, col_bot_right );
}

void util::draw::check_mark(draw_buffer *buf, position top_left, pos_type width, const pack_color col)
{
	//From dear ImGui by ocornut
	const auto thickness = std::max(width / 5.f, 1.f);
	width -= thickness * 0.5f;
	top_left += position{thickness * 0.25f, thickness * 0.25f};

	const auto third = width / 3.f;
	const auto bx    = top_left.x + third;
	const auto by    = top_left.y + width - third * 0.5f;
	position pos[3]  = {
		position{bx - third, by - third},
		position{bx, by},
		position{bx + third * 2, by - third * 2}
	};
	buf->poly_line(pos, 3, col, thickness);
}


#pragma endregion
