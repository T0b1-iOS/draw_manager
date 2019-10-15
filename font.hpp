#pragma once

#include <vector>
#include <mutex>
#include "math.h"

typedef struct FT_LibraryRec_ *FT_Library;
typedef struct FT_FaceRec_ *FT_Face;
typedef int32_t FT_Int32;
typedef struct FT_BitmapGlyphRec_ *FT_BitmapGlyph;
typedef struct FT_GlyphRec_ *FT_Glyph;

namespace util::draw
{
	using pos_type = float;
	using position = math::vec2f;
	using rect = math::vec4f;
	using color = math::color_rgba;
	using pack_color = math::color_rgba;
	using font_t = size_t; //
	using tex_id = void*;
	using matrix = math::matrix4x4f;
	using vec4f = math::vec4f;


	enum GLYPH_RANGES
	{
		GLYPH_RANGE_LATIN,
		GLYPH_RANGE_JAPANESE
	};

	enum FONT_ATLAS_FLAGS : uint16_t
	{
		FONT_ATLAS_FLAGS_NONE = 0,
		FONT_ATLAS_FLAGS_NO_POWER_OF_TWO_HEIGHT = 1 << 0,
		FONT_ATLAS_FLAGS_NO_MOUSE_CURSORS = 1 << 1
	};

	enum RASTERIZER_FLAGS
	{
		// By default, hinting is enabled and the font's native hinter is preferred over the auto-hinter.
		NO_HINTING = 1 << 0,
		// Disable hinting. This generally generates 'blurrier' bitmap glyphs when the glyph are rendered in any of the anti-aliased modes.
		NO_AUTO_HINT = 1 << 1,
		// Disable auto-hinter.
		FORCE_AUTO_HINT = 1 << 2,
		// Indicates that the auto-hinter is preferred over the font's native hinter.
		LIGHT_HINTING = 1 << 3,
		// A lighter hinting algorithm for gray-level modes. Many generated glyphs are fuzzier but better resemble their original shape. This is achieved by snapping glyphs to the pixel grid only vertically (Y-axis), as is done by Microsoft's ClearType and Adobe's proprietary font renderer. This preserves inter-glyph spacing in horizontal text.
		MONO_HINTING = 1 << 4,
		// Strong hinting algorithm that should only be used for monochrome output.
		BOLD = 1 << 5,
		// Styling: Should we artificially embolden the font?
		OBLIQUE = 1 << 6,    
		// Styling: Should we slant the font, emulating italic style?
        NO_ANTIALIASING = 1 << 7 
		// Disable anti-aliasing. Combine this with MonoHinting for best results!
	};


	using font_wchar = uint16_t;
	using font_char = char;

	struct font_config;
	struct font_info;
	struct glyph_info;
	struct font_atlas;
	struct font;
	struct draw_manager;
	struct draw_buffer;

	struct font_config
	{
		void *font_data;
		uint32_t font_data_size;
		bool owned_by_atlas;
		font_t font_idx;

		float size_pixels;
		uint32_t oversample_h;
		uint32_t oversample_v;
		bool pixel_snap_h;

		position glyph_extra_spacing;
		position glyph_offset;
		const font_wchar *glyph_ranges;
		float glyph_min_advance;
		float glyph_max_advance;

		bool merge_mode; //Ignored for now
		uint32_t rasterizer_flags;
		float rasterizer_multiply;

		std::array<font_char, 40> name{};
		std::shared_ptr<font> dst_font;

		font_config();
	};

	struct font_info
	{
		uint32_t pixel_height;
		float ascender;
		float descender;
		float line_spacing;
		float line_gap;
		float max_advance_width;
	};

	struct font_glyph
	{
		font_wchar codepoint;
		float advance_x;
		float x0, y0, x1, y1;
		float u0, v0, u1, v1;
	};

	struct font
	{
		font_info info;
		uint32_t user_flags;
		FT_Library freetype_library{};
		FT_Face freetype_face{};
		FT_Int32 freetype_load_flags{};

		//Other stuff
		float font_size;
		float scale;
		position display_offset;
		std::vector<font_glyph> glyphs;
		std::vector<float> index_advance_x;
		std::vector<float> index_glyph_y;
		std::vector<font_wchar> index_lookup;
		const font_glyph *fallback_glyph;
		float fallback_advance_x;
		font_wchar fallback_char;

		short config_data_count;
		font_config *config_data;
		font_atlas *container_atlas;
		float ascent, descent;
		bool dirty_lookup_tables;
		int metrics_total_surface;

		font();
		~font();

		bool init(const font_config &cfg, uint32_t user_flags);
		void shutdown();
		void set_pixel_height(uint32_t pixel_height);
		bool calc_glyph_info(uint32_t codepoint,
		                     glyph_info &glyph_info,
		                     FT_Glyph &ft_glyph,
		                     FT_BitmapGlyph &ft_bitmap) const;
		void blit_glyph(FT_BitmapGlyph ft_bitmap,
		                uint8_t *dst,
		                uint32_t dst_pitch,
		                const unsigned char *multiply_table = nullptr) const;


		void clear_output_data();
		void build_lookup_table();
		void set_fallback_char(font_wchar c);
		const font_glyph* find_glyph(font_wchar c) const;
		const font_glyph* find_glyph_no_fallback(font_wchar c) const;

		float char_advance(font_wchar c) const
		{
			return (static_cast<uint32_t>(c) < index_advance_x.size())
				       ? index_advance_x[static_cast<uint32_t>(c)]
				       : fallback_advance_x;
		}

		bool loaded() const
		{
			return container_atlas != nullptr;
		}

		const char* debug_name() const
		{
			return config_data ? config_data->name.data() : "<unknown>";
		}

		//TODO: FIX THIS SHIT
		position calc_text_size(float size,
		                        float max_width,
		                        float wrap_width,
		                        const char *text_begin,
		                        const char *text_end   = nullptr,
		                        const char **remaining = nullptr) const;
		rect calc_text_bounds(float size,
		                      float max_width,
		                      float wrap_width,
		                      const char *text_begin,
		                      const char *text_end,
		                      const char **remaining = nullptr) const;
		const char* calc_word_wrap_pos(float scale, const char *text, const char *text_end, float wrap_width) const;
		void render_char(draw_buffer *draw_buffer, float size, position pos, pack_color col, font_wchar c) const;
		void render_text(draw_buffer *draw_buffer,
		                 float size,
		                 position pos,
		                 pack_color col,
		                 const rect &clip_rect,
		                 const char *text_begin,
		                 const char *text_end,
		                 float wrap_width   = 0.0f,
		                 bool cpu_fine_clip = false) const;


		void grow_index(size_t new_size);
		void add_glyph(font_wchar c,
		               float x0,
		               float y0,
		               float x1,
		               float y1,
		               float u0,
		               float v0,
		               float u1,
		               float v1,
		               float advance_x);
		void add_remap_char(font_wchar dst, font_wchar src, bool overwrite_dst = true);
	};

	struct glyph_info
	{
		float width;
		float height;
		float offset_x;
		float offset_y;
		float advance_x;
	};


	struct font_atlas
	{
		struct custom_rect
		{
			uint32_t id;
			uint16_t width, height;
			uint16_t x, y;
			float glyph_advance_x;
			position glyph_offset;
			font *font;

			custom_rect();

			bool is_packed() const
			{
				return (x != 0xFFFF);
			}
		};

		bool locked;
		bool has_updated;
		FONT_ATLAS_FLAGS flags;
		tex_id tex_id;
		uint32_t tex_desired_width;
		uint32_t tex_glyph_padding;


		uint8_t *tex_pixels_alpha_8;
		uint32_t *tex_pixels_rgba_32;
		uint32_t tex_width;
		uint32_t tex_height;
		position tex_uv_scale;
		position tex_uv_white_pixel;
		std::vector<std::shared_ptr<font>> fonts;
		std::vector<custom_rect> custom_rects;
		std::vector<font_config> config_data;
		std::array<int32_t, 1> custom_rect_idx;
		std::mutex tex_mutex;


		font_atlas();
		~font_atlas();

		void add_font_default() { } //TODO
		font* add_font(const font_config *font_cfg = nullptr);
		font* add_font_from_ttf(const char *file_name,
		                        float size_pixels,
		                        const font_config *font_cfg    = nullptr,
		                        const font_wchar *glyph_ranges = nullptr);
		font* add_font_from_ttf_mem(void *font_data,
		                            size_t data_size,
		                            float size_pixels,
		                            const font_config *font_cfg    = nullptr,
		                            const font_wchar *glyph_ranges = nullptr);
		//Skipped Compressed Fonts
		void remove_font(const font *);

		void clear_input_data();
		void clear_tex_data(bool lock_tex = true);
		void clear_fonts();
		void clear();


		bool build(uint32_t extra_flags = 0);

		bool is_built()
		{
			return !fonts.empty() && (tex_pixels_alpha_8 != nullptr || tex_pixels_rgba_32 != nullptr);
		}

		void tex_data_as_alpha_8(uint8_t **out_pixels,
		                         uint32_t *out_width,
		                         uint32_t *out_height,
		                         uint32_t *out_bytes_per_pixel = nullptr);
		void tex_data_as_rgba_32(uint8_t **out_pixels,
		                         uint32_t *out_width,
		                         uint32_t *out_height,
		                         uint32_t *out_bytes_per_pixel = nullptr);

		void set_tex_id(const draw::tex_id id)
		{
			tex_id = id;
		}

		const font_wchar* glyph_ranges_default();
		const font_wchar* glyph_ranges_japanese();

		uint32_t add_custom_rect_regular(uint32_t id, uint16_t width, uint16_t height);
		uint32_t add_custom_rect_glyph(font *font,
		                               font_wchar id,
		                               uint16_t width,
		                               uint16_t height,
		                               float advance_x,
		                               const position &offset = position{0.f, 0.f});

		void calc_custom_rect_uv(const custom_rect *rect, position *out_uv_min, position *out_uv_max);
	};
}
