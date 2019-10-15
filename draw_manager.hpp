#pragma once
#include "font.hpp"

#include <functional>
#include <assert.h>

enum ROUND_RECT_FLAG : uint8_t
{
	ROUND_RECT_TL = (1 << 0),
	ROUND_RECT_BL = (1 << 1),
	ROUND_RECT_BR = (1 << 2),
	ROUND_RECT_TR = (1 << 3),

	ROUND_RECT_TOP = ROUND_RECT_TL | ROUND_RECT_TR,
	ROUND_RECT_BOT = ROUND_RECT_BR | ROUND_RECT_BL,
	ROUND_RECT_LEFT = ROUND_RECT_TL | ROUND_RECT_BL,
	ROUND_RECT_RIGHT = ROUND_RECT_TR | ROUND_RECT_BR,
	ROUND_RECT_ALL = ROUND_RECT_TOP | ROUND_RECT_BOT
};

namespace util::draw
{
	struct callback_data
	{
		virtual ~callback_data() = default;
	};

	struct vecl
	{
		long x, y;
	};

	struct clip_rect
	{
		union
		{
			struct
			{
				long x, y, z, w;
			};

			struct
			{
				vecl xy, zw;
			};
		};

		rect float_rect() const
		{
			return rect{
				static_cast<float>(x),
				static_cast<float>(y),
				static_cast<float>(z),
				static_cast<float>(w)
			};
		}

		clip_rect() = default;

		clip_rect(const rect &r)
		{
			x = std::lround(r.x);
			y = std::lround(r.y);
			z = std::lround(r.z);
			w = std::lround(r.w);
		}

		bool operator!=(const clip_rect &o) const
		{
			return (o.x != x || o.y != y || o.z != z || o.w != w);
		}
	};

	struct draw_buffer
	{
		using draw_index = std::uint32_t;

		struct draw_cmd
		{
			std::uint32_t elem_count;
			clip_rect clip_rect;
			tex_id tex_id;
			bool font_texture                              = false;
			bool circle_scissor                            = false;
			uint8_t blur_strength                          = 0;
			std::uint32_t vtx_count                        = 0;
			std::function<void(const draw_cmd *)> callback =
					nullptr;
			//Callback that will be called if not null instead of drawing
			std::shared_ptr<callback_data> callback_data = nullptr; //Data for callback
			// If color matches it will be made transparent, alpha indicates enabling of the feature
			color key_color = {0, 0, 0, 0};
			// This will be used as the model matrix, e.g. "model space to world space", basically a tool to transform all vertexes
			matrix matrix = {
				vec4f{1.f, 0.f, 0.f, 0.f},
				{0.f, 1.f, 0.f, 0.f},
				{0.f, 0.f, 1.f, 0.f},
				{0.f, 0.f, 0.f, 1.f}
			};
		};

		struct draw_vertex
		{
			draw_vertex() = default;

			draw_vertex(const position &p, const position &u, const std::uint32_t c)
				: pos(p),
				  uv(u),
				  col(c) { }

			draw_vertex(const position &p, const position &u, const pack_color c)
				: pos(p),
				  uv(u),
				  col(c) { }

			position pos   = {};
			position uv    = {};
			pack_color col = 0u; //R8G8B8A8
		};

		std::vector<draw_cmd> cmds        = {};
		std::vector<draw_vertex> vertices = {};
		std::vector<draw_index> indices   = {};

		bool is_child_buffer    = false;
		pos_type scaling_factor = 1.f;

	public: //Changed for now
		std::vector<std::pair<rect, bool>> clip_rect_stack = {};
		std::vector<tex_id> tex_id_stack                   = {};
		std::vector<font*> font_stack                      = {};
		std::vector<position> path                         = {};
		draw_vertex *vtx_write_ptr                         = nullptr;
		draw_index *idx_write_ptr                          = nullptr;
		draw_index cur_idx                                 = 0;
		font *cur_font                                     = nullptr;
		draw_manager *manager                              = nullptr;

	public:

		draw_buffer(draw_manager *manager)
			: manager(manager)
		{
			update_clip_rect();
		}

		std::pair<std::size_t, std::size_t> vtx_idx_count() const
		{
			return {vertices.size(), indices.size()};
		}

		void clear_buffers()
		{
			cmds            = {};
			vertices        = {};
			indices         = {};
			clip_rect_stack = {};
			tex_id_stack    = {};
			font_stack      = {};
			path            = {};
			vtx_write_ptr   = nullptr;
			idx_write_ptr   = nullptr;
			cur_idx         = 0;
			cur_font        = nullptr;
			update_clip_rect();
		}

		rect cur_clip_rect();
		rect clip_rect_to_cur_rect(const rect &);

		void push_clip_rect(const position &min, const position &max, const bool circle = false) //rvalue reference?
		{
			clip_rect_stack.emplace_back(std::make_pair(clip_rect_to_cur_rect(rect{min, max}), circle));
			update_clip_rect();
		}

		void push_clip_rect(const pos_type min_x,
		                    const pos_type min_y,
		                    const pos_type max_x,
		                    const pos_type max_y,
		                    const bool circle = false)
		{
			clip_rect_stack.emplace_back(std::make_pair(clip_rect_to_cur_rect(rect{min_x, min_y, max_x, max_y}),
			                                            circle));
			update_clip_rect();
		}

		void push_clip_rect(const rect &clip_rect, const bool circle = false)
		{
			clip_rect_stack.emplace_back(std::make_pair(clip_rect_to_cur_rect(clip_rect), circle));
			update_clip_rect();
		}

		void pop_clip_rect()
		{
			assert(!clip_rect_stack.empty( ));
			clip_rect_stack.pop_back();
			update_clip_rect();
		}

		void update_clip_rect();

		tex_id cur_tex_id()
		{
			return (tex_id_stack.empty() ? nullptr : tex_id_stack.back());
		}

		void push_font(font *font);

		void push_tex_id(tex_id id, bool force_font = false)
		{
			tex_id_stack.emplace_back(id);
			update_tex_id(force_font);
		}

		void pop_tex_id()
		{
			assert(!tex_id_stack.empty( ));
			tex_id_stack.pop_back();
			update_tex_id();
		}

		void pop_font();
		void update_tex_id(bool force_font = false);

		// This will force a new command to be started and return the index of it
		size_t force_new_cmd();

		// This is special: This function allows to move all vertexes in a cmd(or all vertexes) by the x&y-values specified in xy_translate
		// This can be useful if you later want to move an already-swapped buffer without redoing all the computing
		// Warning: This will add to the current translation
		void update_matrix_translate(const position &xy_translate, const size_t cmd_idx = -1);

		void set_blur(uint8_t strength = 2);
		void set_key_color(color col);

		//Drawing Funcs
		//Triangles
		void triangle_filled(const position &p1,
		                     const position &p2,
		                     const position &p3,
		                     const pack_color col_p1,
		                     const pack_color col_p2,
		                     const pack_color col_p3);

		void triangle_filled(const position &p1,
		                     const position &p2,
		                     const position &p3,
		                     const pack_color col_p1,
		                     const pack_color col_p2)
		{
			triangle_filled(p1, p2, p3, col_p1, col_p2, col_p2);
		}

		void triangle_filled(const position &p1, const position &p2, const position &p3, const pack_color col)
		{
			triangle_filled(p1, p2, p3, col, col, col);
		}

		void rectangle_filled(const position &top_left,
		                      const position &bot_right,
		                      const pack_color col_top_left,
		                      const pack_color col_top_right,
		                      const pack_color col_bot_left,
		                      const pack_color col_bot_right);

		void rectangle_filled(const position &top_left,
		                      const position &bot_right,
		                      const pack_color col)
		{
			rectangle_filled(top_left, bot_right, col, col, col, col);
		}

		void rectangle_filled(const position &top_left,
		                      const pos_type width,
		                      const pos_type height,
		                      const pack_color col_top_left,
		                      const pack_color col_top_right,
		                      const pack_color col_bot_left,
		                      const pack_color col_bot_right)
		{
			rectangle_filled(top_left,
			                 {top_left.x + width, top_left.y + height},
			                 col_top_left,
			                 col_top_right,
			                 col_bot_left,
			                 col_bot_right);
		}

		void rectangle_filled(const position &top_left,
		                      const pos_type width,
		                      const pos_type height,
		                      const pack_color col)
		{
			rectangle_filled(top_left, {top_left.x + width, top_left.y + height}, col, col, col, col);
		}

		void rectangle(const position &top_left,
		               const position &bot_right,
		               const pos_type thickness,
		               const pack_color col_top_left,
		               const pack_color col_top_right,
		               const pack_color col_bot_left,
		               const pack_color col_bot_right,
		               const bool clipped = false);

		void rectangle(const position &top_left,
		               const position &bot_right,
		               const pos_type thickness,
		               const pack_color col,
		               const bool clipped = false)
		{
			rectangle(top_left, bot_right, thickness, col, col, col, col, clipped);
		}

		void rectangle(const position &top_left,
		               const pos_type width,
		               const pos_type height,
		               const pos_type thickness,
		               const pack_color col_top_left,
		               const pack_color col_top_right,
		               const pack_color col_bot_left,
		               const pack_color col_bot_right,
		               const bool clipped = false)
		{
			rectangle(top_left,
			          {top_left.x + width, top_left.y + height},
			          thickness,
			          col_top_left,
			          col_top_right,
			          col_bot_left,
			          col_bot_right,
			          clipped);
		}

		void rectangle(const position &top_left,
		               const pos_type width,
		               const pos_type height,
		               const pos_type thickness,
		               const pack_color col,
		               const bool clipped = false)
		{
			rectangle(top_left, {top_left.x + width, top_left.y + height}, thickness, col, col, col, col, clipped);
		}

		//degrees = counter-clockwise; start_degrees = clockwise; blame the performance
		void circle_filled(const position &center,
		                   const pos_type radius,
		                   const pack_color inner_col,
		                   const pack_color *outer_col,
		                   const uint32_t parts   = 12,
		                   const pos_type degrees = 360.f,
		                   pos_type start_degree  = 0.f);

		//degrees = counter-clockwise; start_degrees = clockwise
		void circle_filled(const position &center,
		                   const pos_type radius,
		                   const pack_color inner_col,
		                   const pack_color outer_col,
		                   const uint32_t parts   = 12,
		                   const pos_type degrees = 360.f,
		                   pos_type start_degree  = 0.f,
		                   bool anti_aliasing     = true);
		//degrees = counter-clockwise; start_degrees = clockwise
		void circle_filled(const position &center,
		                   const pos_type radius,
		                   const pack_color col,
		                   const uint32_t parts = 12,
		                   bool anti_aliasing   = true)
		{
			circle_filled(center, radius, col, col, parts, 360.f, 0, anti_aliasing);
		}

		void circle(const position &center,
		            const pos_type radius,
		            const pack_color col,
		            const pos_type thickness,
		            const uint32_t parts        = 12,
		            const pos_type degrees      = 360.f,
		            const pos_type start_degree = 0.f);

	private:

		void circle_impl(const position &center,
		                 const pos_type radius,
		                 const pack_color inner_col,
		                 const pack_color outer_col,
		                 const uint32_t parts,
		                 const pos_type degrees,
		                 pos_type start_degree,
		                 position *stack_buf,
		                 bool anti_aliasing,
		                 bool fill               = true,
		                 pos_type line_thickness = 1.f);

		void fill_circle_impl(const position &center,
		                      position *vtx,
		                      uint32_t vtx_count,
		                      pack_color col_inner,
		                      pack_color col_outer);
	public:

		void line(const position &p1,
		          const position &p2,
		          const pack_color col1,
		          const pack_color col2,
		          const pos_type thickness,
		          const bool aa = false)
		{
			reserve_path(2);
			push_path(p1);
			push_path(p2);
			pack_color colors[] = {col1, col2};
			poly_line(path_data(), 2, colors, thickness, aa);
			clear_path();
		}

		void line(const position &p1,
		          const position &p2,
		          const pack_color col,
		          const pos_type thickness,
		          const bool aa = false)
		{
			line(p1, p2, col, col, thickness, aa);
		}

		//Polystuff
		void poly_line(position *points,
		               uint32_t count,
		               pack_color col,
		               pos_type thickness,
		               bool anti_aliased = false/*, bool closed = false, bool anti_aliased = true*/);
		void poly_line(position *points,
		               uint32_t count,
		               const pack_color *col,
		               pos_type thickness,
		               bool anti_aliased = false/*, bool closed = false, bool anti_aliased = true*/);

		void poly_fill(position *points, uint32_t count, const pack_color *col); //TODO: Anti-Aliasing
		void poly_fill(position *points, uint32_t count, pack_color col);

		//Primitives(Call prim_reserve before calling the draw funcs)
		void prim_reserve(uint32_t idx_count, uint32_t vtx_count);
		void prim_rect(const position &p1, const position &p2, const pack_color col);
		void prim_rect_uv(const position &p1,
		                  const position &p2,
		                  const position &uv1,
		                  const position &uv2,
		                  const pack_color col);
		void prim_quad_uv(const position &p1,
		                  const position &p2,
		                  const position &p3,
		                  const position &p4,
		                  const position &uv1,
		                  const position &uv2,
		                  const position &uv3,
		                  const position &uv4,
		                  const pack_color col);

		//Not formatted and auto-clipped rn
		void text(font *font,
		          const char *text,
		          const position &top_left,
		          const pack_color col,
		          bool outline              = false,
		          const position &bot_right = position
				          {-1.f, -1.f});

		void text(const char *text,
		          const position &top_left,
		          const pack_color col,
		          const bool outline        = false,
		          const position &bot_right = position{
			          -1.f,
			          -1.f
		          })
		{
			this->text(nullptr, text, top_left, col, outline, bot_right);
		}

		position text_size(font *font, const char *text, const char *text_end = nullptr) const;

		position text_size(const char *text, const char *text_end = nullptr) const
		{
			return text_size(nullptr, text, text_end);
		}

		rect text_bounds(font *font, const char *text, const char *text_end = nullptr) const;

		rect text_bounds(const char *text, const char *text_end = nullptr) const
		{
			return text_bounds(nullptr, text, text_end);
		}

		void text(font *font,
		          float target_size,
		          const char *text,
		          const position &top_left,
		          const pack_color col,
		          bool outline              = false,
		          const position &bot_right = position{-1.f, -1.f});

		void text(float target_size,
		          const char *text,
		          const position &top_left,
		          const pack_color col,
		          const bool outline        = false,
		          const position &bot_right = position{-1.f, -1.f})
		{
			this->text(nullptr, target_size, text, top_left, col, outline, bot_right);
		}

		position text_size(font *font, float target_size, const char *text, const char *text_end = nullptr) const;

		position text_size(float target_size, const char *text, const char *text_end = nullptr) const
		{
			return text_size(nullptr, target_size, text, text_end);
		}

		rect text_bounds(font *font, float target_size, const char *text, const char *text_end = nullptr) const;

		rect text_bounds(float target_size, const char *text, const char *text_end = nullptr) const
		{
			return text_bounds(nullptr, target_size, text, text_end);
		}

	private:
		void reserve_primitives(std::uint32_t idx_count, std::uint32_t vtx_count);

		void write_vtx(const position &p, const position &uv, const std::uint32_t col)
		{
			vtx_write_ptr->pos = p;
			vtx_write_ptr->uv  = uv;
			vtx_write_ptr->col = col;
			vtx_write_ptr++;
		}

		void write_idx(const draw_index idx)
		{
			*idx_write_ptr = idx;
			idx_write_ptr++;
		}

		void reserve_path(const size_t size)
		{
			path.reserve(size);
		}

		void push_path(const position &pos)
		{
			path.emplace_back(pos);
		}

		position* path_data()
		{
			return path.data();
		}

		void clear_path()
		{
			path.clear();
		}
	};

	struct draw_manager
	{
	protected:
		struct buffer_node
		{
			using child_array = std::vector<std::pair<size_t, size_t>>;
			std::unique_ptr<draw_buffer> active_buffer  = nullptr;
			std::unique_ptr<draw_buffer> working_buffer = nullptr;
			child_array child_buffers                   = {}; // sorted low to high by size_t
			bool is_free                                = false;
			size_t parent                               = std::numeric_limits<size_t>::max();
		};

	public:
		std::unique_ptr<font_atlas> fonts = nullptr;

		size_t register_buffer(size_t init_priority = 0);
		size_t register_child_buffer(size_t parent, size_t priority);
		void update_child_priority(size_t child_idx, size_t new_priority);
		void update_buffer_priority(size_t buffer, size_t new_priority);
		void remove_buffer(size_t idx);
		void update_buffer_ptrs();
		draw_buffer* get_buffer(const size_t);
		void swap_buffers(const size_t);

		void update_screen_size(const position &screen_size)
		{
			this->_screen_size = screen_size;
		}

		const position& get_screen_size() const
		{
			return _screen_size;
		}

		//TODO: filename without backslashes translated to default windows font dir
		font* add_font(const char *file,
		               float size,
		               bool italic          = false,
		               bool bold            = false,
		               GLYPH_RANGES range   = GLYPH_RANGE_LATIN,
		               int rasterizer_flags = 0)
		const;
		font* add_font(const char *file,
		               float size,
		               const font_wchar *glyph_ranges,
		               bool italic          = false,
		               bool bold            = false,
		               int rasterizer_flags = 0) const;

		// this does not copy the font data so keep the buffer alive until you remove the font :)
		font* add_font_mem(uint8_t *data,
		                   size_t data_size,
		                   float font_size,
		                   bool italic          = false,
		                   bool bold            = false,
		                   GLYPH_RANGES range   = GLYPH_RANGE_LATIN,
		                   int rasterizer_flags = 0)
		const;
		void remove_font(const font *) const;

		// This will call update_matrix_translate on the currently active(!) buffer in the "swapchain", 
		// use with care, may impact performance
		void update_matrix_translate(size_t buffer, const position &xy_translate, size_t cmd_idx = -1);


		// Texture Creation
		virtual tex_id create_texture(uint32_t width, uint32_t height) = 0;
		virtual bool set_texture_rgba(tex_id id, const uint8_t *rgba, uint32_t width, uint32_t height) = 0;
		// this function exists to fix bugs im too lazy to find out why they even happen
		// also it may not even do what the name suggests
		// the d3d9_manager feeds it directly to directx while the csgo impl converts the data to bgra
		virtual bool set_texture_rabg(tex_id id, const uint8_t *rabg, uint32_t width, uint32_t height) = 0;
		virtual bool delete_texture(tex_id id) = 0;

		virtual void draw() = 0;
	protected:
		std::vector<buffer_node> _buffer_list              = {};
		std::vector<std::pair<size_t, size_t>> _priorities = {}; //(priority,idx) //TODO: Check performance
		std::vector<size_t> _free_buffers                  = {};
		std::mutex _list_mutex;
		position _screen_size = position{};

		void sort_priorities()
		{
			std::sort(_priorities.begin(),
			          _priorities.end(),
			          [ ](auto &first, auto &sec) -> bool
			          {
				          return first.first < sec.first;
			          });
		}

		draw_manager() = default;
		void init();
	};


	//Draw Helper Funcs
	void rectangle_filled_rounded(draw_buffer *buf,
	                              const position &top_left,
	                              const position &bot_right,
	                              const pos_type radius,
	                              const pack_color col_top_left,
	                              const pack_color col_top_right,
	                              const pack_color col_bot_left,
	                              const pack_color col_bot_right,
	                              const uint8_t flags = ROUND_RECT_ALL);

	inline void rectangle_filled_rounded(draw_buffer *buf,
	                                     const position &top_left,
	                                     const position &bot_right,
	                                     const pos_type radius,
	                                     const pack_color col,
	                                     const uint8_t flags = ROUND_RECT_ALL)
	{
		rectangle_filled_rounded(buf, top_left, bot_right, radius, col, col, col, col, flags);
	}

	inline void rectangle_filled_rounded(draw_buffer *buf,
	                                     const position &top_left,
	                                     const pos_type width,
	                                     const pos_type height,
	                                     const pos_type radius,
	                                     const pack_color col_top_left,
	                                     const pack_color col_top_right,
	                                     const pack_color col_bot_left,
	                                     const pack_color col_bot_right,
	                                     const uint8_t flags = ROUND_RECT_ALL)
	{
		rectangle_filled_rounded(buf,
		                         top_left,
		                         {top_left.x + width, top_left.y + height},
		                         radius,
		                         col_top_left,
		                         col_top_right,
		                         col_bot_left,
		                         col_bot_right,
		                         flags);
	}

	inline void rectangle_filled_rounded(draw_buffer *buf,
	                                     const position &top_left,
	                                     const pos_type width,
	                                     const pos_type height,
	                                     const pos_type radius,
	                                     const pack_color col,
	                                     const uint8_t flags = ROUND_RECT_ALL)
	{
		rectangle_filled_rounded(buf,
		                         top_left,
		                         {top_left.x + width, top_left.y + height},
		                         radius,
		                         col,
		                         col,
		                         col,
		                         col,
		                         flags);
	}

	extern void check_mark(draw_buffer *buf, position top_left, pos_type width, const pack_color col);
}
