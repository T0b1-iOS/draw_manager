#include <fstream>

#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftglyph.h>
#include <freetype/ftsynth.h>
#include <mutex>

#include "freetype/ftcache.h"
#include "freetype/ftbitmap.h"
#include "freetype/ftsizes.h"

#include "draw_manager.hpp"

#define ARRAY_SIZE(x) ((int)(sizeof(x)/sizeof(*x)))

#define STBRP_ASSERT(x)    assert(x)
#define STBRP_STATIC
#define STB_RECT_PACK_IMPLEMENTATION
#include <stb/stb_rectpack.h>

const unsigned int FONT_ATLAS_DEFAULT_TEX_DATA_ID = 0x80000000;

using namespace util::draw;

#pragma region font_utils

void* load_file_to_mem(const char *filename, size_t *data_size)
{
	std::ifstream file(filename, std::ios::in | std::ios::binary | std::ios::ate);

	if (!file.is_open())
		return nullptr;

	const auto file_size = file.tellg();
	const auto file_data = reinterpret_cast<char*>(malloc(static_cast<std::size_t>(file_size)));
	file.seekg(0, std::ios::beg);
	file.read(file_data, file_size);
	file.close();

	*data_size = static_cast<std::size_t>(file_size);
	return file_data;
}

template<typename ...args>
size_t format_string(char *buf, const size_t buf_size, const char *fmt, args ... arg_list)
{
	auto req_size = snprintf(buf, buf_size, fmt, arg_list...);
	if (!buf)
		return req_size;
	if (req_size == -1 || req_size >= static_cast<int32_t>(buf_size))
		req_size = static_cast<int32_t>(buf_size) - 1;
	buf[req_size] = 0;
	return req_size;
}

int text_char_from_utf8(unsigned int *out_char, const char *in_text, const char *in_text_end)
{
	auto c          = -1;
	const auto *str = reinterpret_cast<const unsigned char*>(in_text);
	if (!(*str & 0x80))
	{
		c         = static_cast<uint32_t>(*str++);
		*out_char = c;
		return 1;
	}
	if ((*str & 0xe0) == 0xc0)
	{
		*out_char = 0xFFFD; // will be invalid but not end of string
		if (in_text_end && in_text_end - reinterpret_cast<const char*>(str) < 2)
			return 1;
		if (*str < 0xc2)
			return 2;
		c = static_cast<uint32_t>((*str++ & 0x1f) << 6);
		if ((*str & 0xc0) != 0x80)
			return 2;
		c += (*str++ & 0x3f);
		*out_char = c;
		return 2;
	}
	if ((*str & 0xf0) == 0xe0)
	{
		*out_char = 0xFFFD; // will be invalid but not end of string
		if (in_text_end && in_text_end - reinterpret_cast<const char*>(str) < 3)
			return 1;
		if (*str == 0xe0 && (str[1] < 0xa0 || str[1] > 0xbf))
			return 3;
		if (*str == 0xed && str[1] > 0x9f)
			return 3; // str[1] < 0x80 is checked below
		c = static_cast<uint32_t>((*str++ & 0x0f) << 12);
		if ((*str & 0xc0) != 0x80)
			return 3;
		c += static_cast<uint32_t>((*str++ & 0x3f) << 6);
		if ((*str & 0xc0) != 0x80)
			return 3;
		c += (*str++ & 0x3f);
		*out_char = c;
		return 3;
	}
	if ((*str & 0xf8) == 0xf0)
	{
		*out_char = 0xFFFD; // will be invalid but not end of string
		if (in_text_end && in_text_end - reinterpret_cast<const char*>(str) < 4)
			return 1;
		if (*str > 0xf4)
			return 4;
		if (*str == 0xf0 && (str[1] < 0x90 || str[1] > 0xbf))
			return 4;
		if (*str == 0xf4 && str[1] > 0x8f)
			return 4; // str[1] < 0x80 is checked below
		c = static_cast<uint32_t>((*str++ & 0x07) << 18);
		if ((*str & 0xc0) != 0x80)
			return 4;
		c += static_cast<uint32_t>((*str++ & 0x3f) << 12);
		if ((*str & 0xc0) != 0x80)
			return 4;
		c += static_cast<uint32_t>((*str++ & 0x3f) << 6);
		if ((*str & 0xc0) != 0x80)
			return 4;
		c += (*str++ & 0x3f);
		// utf-8 encodings of values used in surrogate pairs are invalid
		if ((c & 0xFFFFF800) == 0xD800)
			return 4;
		*out_char = c;
		return 4;
	}
	*out_char = 0;
	return 0;
}

static inline int upper_power_of_two(int v)
{
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

#pragma endregion

#pragma region font_config

font_config::font_config()
{
	font_data           = nullptr;
	font_data_size      = 0;
	owned_by_atlas      = true;
	font_idx            = 0;
	size_pixels         = 0.f;
	oversample_h        = 3;
	oversample_v        = 1;
	pixel_snap_h        = false;
	glyph_extra_spacing = position{0.f, 0.f};
	glyph_offset        = position{0.f, 0.f};
	glyph_ranges        = nullptr;
	glyph_min_advance   = 0.f;
	glyph_max_advance   = FLT_MAX;
	merge_mode          = false;
	rasterizer_flags    = 0;
	rasterizer_multiply = 1.f;
	dst_font            = nullptr;
}


#pragma endregion


#pragma region font

// From SDL_ttf : Handy routines for converting from fixed point
template<typename t>
t FT_CEIL(const t x)
{
	static_assert(std::is_fundamental_v<t>, "FT_CEIL not used with fundamental type.");
	return ((x + 63) & -64) / 64;
}

font::font()
{
	scale          = 1.f;
	fallback_char  = static_cast<font_wchar>('?');
	display_offset = position{0.f, 0.f};
	clear_output_data();
}

font::~font()
{
	clear_output_data();
}


bool font::init(const font_config &cfg, const uint32_t user_flags)
{
	auto result = FT_Init_FreeType(&freetype_library);
	if (result)
		return false;

	result = FT_New_Memory_Face(freetype_library,
	                            reinterpret_cast<uint8_t*>(cfg.font_data),
	                            cfg.font_data_size,
	                            cfg.font_idx,
	                            &freetype_face);
	if (result)
		return false;

	result = FT_Select_Charmap(freetype_face, FT_ENCODING_UNICODE);
	if (result)
		return false;

	info = {};
	set_pixel_height(static_cast<std::size_t>(cfg.size_pixels));

	this->user_flags    = cfg.rasterizer_flags | user_flags;
	freetype_load_flags = FT_LOAD_NO_BITMAP;
	if (this->user_flags & NO_HINTING)
		freetype_load_flags |= FT_LOAD_NO_HINTING;
	if (this->user_flags & NO_AUTO_HINT)
		freetype_load_flags |= FT_LOAD_NO_AUTOHINT;
	if (this->user_flags & FORCE_AUTO_HINT)
		freetype_load_flags |= FT_LOAD_FORCE_AUTOHINT;
	if (this->user_flags & LIGHT_HINTING)
		freetype_load_flags |= FT_LOAD_TARGET_LIGHT;
	else if (this->user_flags & MONO_HINTING)
		freetype_load_flags |= FT_LOAD_TARGET_MONO;
	else
		freetype_load_flags |= FT_LOAD_TARGET_NORMAL;

	return true;
}

void font::shutdown()
{
	if (!freetype_face)
		return;

	FT_Done_Face(freetype_face);
	freetype_face = nullptr;
	FT_Done_FreeType(freetype_library);
	freetype_library = nullptr;
}

void font::set_pixel_height(uint32_t pixel_height)
{
	FT_Size_RequestRec req;
	req.type           = FT_SIZE_REQUEST_TYPE_REAL_DIM;
	req.width          = 0;
	req.height         = pixel_height * 64;
	req.horiResolution = 0;
	req.vertResolution = 0;
	FT_Request_Size(freetype_face, &req); //TODO: err

	const auto &metrics    = freetype_face->size->metrics;
	info.pixel_height      = pixel_height;
	info.ascender          = static_cast<float>(FT_CEIL(metrics.ascender));
	info.descender         = static_cast<float>(FT_CEIL(metrics.descender));
	info.line_spacing      = static_cast<float>(FT_CEIL(metrics.height));
	info.line_gap          = static_cast<float>(FT_CEIL(metrics.height - metrics.ascender + metrics.descender));
	info.max_advance_width = static_cast<float>(FT_CEIL(metrics.max_advance));
}

bool font::calc_glyph_info(const uint32_t codepoint,
                           glyph_info &glyph_info,
                           FT_Glyph &ft_glyph,
                           FT_BitmapGlyph &ft_bitmap) const
{
	const auto glyph_idx = FT_Get_Char_Index(freetype_face, codepoint);
	if (!glyph_idx)
		return false;
	auto result = FT_Load_Glyph(freetype_face, glyph_idx, freetype_load_flags);
	if (result)
		return false;

	//Need an outline for this to work
	const auto slot = freetype_face->glyph;
	assert(slot->format == FT_GLYPH_FORMAT_OUTLINE);

	if (user_flags & BOLD)
		FT_GlyphSlot_Embolden(slot);
	if (user_flags & OBLIQUE)
		FT_GlyphSlot_Oblique(slot);

	//Retrieve Glyph
	result = FT_Get_Glyph(slot, &ft_glyph);
	if (result)
		return false;

	//Rasterize
	result = FT_Glyph_To_Bitmap(&ft_glyph, FT_RENDER_MODE_NORMAL, nullptr, true);
	if (result)
		return false;

	ft_bitmap            = reinterpret_cast<FT_BitmapGlyph>(ft_glyph);
	glyph_info.advance_x = static_cast<float>(FT_CEIL(slot->advance.x));
	glyph_info.offset_x  = static_cast<float>(ft_bitmap->left);
	glyph_info.offset_y  = static_cast<float>(-ft_bitmap->top);
	glyph_info.width     = static_cast<float>(ft_bitmap->bitmap.width);
	glyph_info.height    = static_cast<float>(ft_bitmap->bitmap.rows);

	return true;
}

void font::blit_glyph(const FT_BitmapGlyph ft_bitmap,
                      uint8_t *dst,
                      const uint32_t dst_pitch,
                      const unsigned char *multiply_table) const
{
	assert(ft_bitmap != nullptr);

	const auto w         = ft_bitmap->bitmap.width;
	const auto h         = ft_bitmap->bitmap.rows;
	auto src             = ft_bitmap->bitmap.buffer;
	const auto src_pitch = ft_bitmap->bitmap.pitch;

	for (auto y = 0u; y < h; y++, src += src_pitch, dst += dst_pitch)
	{
		if (multiply_table)
		{
			for (auto x = 0u; x < w; x++)
				dst[x]  = multiply_table[src[x]];
		}
		else
		{
			memcpy(dst, src, w);
		}
	}
}


void font::clear_output_data()
{
	font_size = 0.f;
	glyphs.clear();
	index_advance_x.clear();
	index_lookup.clear();
	index_glyph_y.clear();
	fallback_glyph        = nullptr;
	fallback_advance_x    = 0.f;
	config_data_count     = 0;
	config_data           = nullptr;
	container_atlas       = nullptr;
	ascent                = descent = 0.f;
	dirty_lookup_tables   = true;
	metrics_total_surface = 0;
}

void font::build_lookup_table()
{
	auto max_codepoint = 0;
	for (const auto &glyph : glyphs)
		max_codepoint = std::max(max_codepoint, static_cast<int32_t>(glyph.codepoint));

	assert(glyphs.size() < 0xFFFF);
	index_advance_x.clear();
	index_lookup.clear();
	dirty_lookup_tables = false;
	grow_index(max_codepoint + 1);
	for (auto i = 0u; i < glyphs.size(); i++)
	{
		const auto codepoint       = static_cast<int32_t>(glyphs[i].codepoint);
		index_advance_x[codepoint] = glyphs[i].advance_x;
		index_glyph_y[codepoint]   = glyphs[i].y1;
		index_lookup[codepoint]    = i;
	}

	if (find_glyph(static_cast<font_wchar>(' ')))
	{
		if (glyphs.back().codepoint != '\t')
			glyphs.resize(glyphs.size() + 1);
		auto &tab_glyph     = glyphs.back();
		tab_glyph           = *find_glyph(static_cast<font_wchar>(' '));
		tab_glyph.codepoint = '\t';
		tab_glyph.advance_x *= 4;
		index_advance_x[tab_glyph.codepoint] = tab_glyph.advance_x;
		index_lookup[tab_glyph.codepoint]    = static_cast<font_wchar>(glyphs.size() - 1);
		index_glyph_y[tab_glyph.codepoint]   = font_size;
	}

	fallback_glyph     = find_glyph_no_fallback(fallback_char);
	fallback_advance_x = fallback_glyph ? fallback_glyph->advance_x : 0.f;
	for (auto i = 0; i < max_codepoint + 1; i++)
		if (index_advance_x[i] < 0.f)
			index_advance_x[i] = fallback_advance_x;
}

void font::set_fallback_char(font_wchar c)
{
	fallback_char = c;
	build_lookup_table();
}

void font::grow_index(const size_t new_size)
{
	assert(index_advance_x.size() == index_lookup.size());
	if (new_size <= index_lookup.size())
		return;
	index_advance_x.resize(new_size, -1.f);
	index_lookup.resize(new_size, -1);
	index_glyph_y.resize(new_size, -1);
}

void font::add_glyph(const font_wchar c,
                     const float x0,
                     const float y0,
                     const float x1,
                     const float y1,
                     const float u0,
                     const float v0,
                     const float u1,
                     const float v1,
                     const float advance_x)
{
	glyphs.resize(glyphs.size() + 1);
	auto &glyph     = glyphs.back();
	glyph.codepoint = c;
	glyph.x0        = x0;
	glyph.y0        = y0;
	glyph.x1        = x1;
	glyph.y1        = y1;
	glyph.u0        = u0;
	glyph.v0        = v0;
	glyph.u1        = u1;
	glyph.v1        = v1;
	glyph.advance_x = advance_x + config_data->glyph_extra_spacing.x;

	if (config_data->pixel_snap_h)
		glyph.advance_x = std::roundf(glyph.advance_x);

	dirty_lookup_tables   = true;
	metrics_total_surface = static_cast<int32_t>((glyph.u1 - glyph.u0) * container_atlas->tex_width + 1.99f) *
			static_cast<int32_t>((glyph.v1 - glyph.v0) * container_atlas->tex_height + 1.99f);
}

void font::add_remap_char(const font_wchar dst, const font_wchar src, const bool overwrite_dst)
{
	const auto index_size = index_lookup.size();
	assert(index_size > 0);

	if (dst < index_size && index_lookup[dst] == 0xFFFF && !overwrite_dst) //dst exists
		return;
	if (src >= index_size && dst >= index_size) //src & dst dont exist
		return;

	grow_index(dst + 1);
	index_lookup[dst]    = (src < index_size) ? index_lookup[src] : -1;
	index_advance_x[dst] = (src < index_size) ? index_advance_x[src] : 1.f;
}

const font_glyph* font::find_glyph(const font_wchar c) const
{
	if (c >= index_lookup.size())
		return fallback_glyph;
	const auto i = index_lookup[c];
	if (i == 0xFFFF)
		return fallback_glyph;
	return &glyphs[i];
}

const font_glyph* font::find_glyph_no_fallback(font_wchar c) const
{
	if (c >= index_lookup.size())
		return nullptr;
	const auto i = index_lookup[c];
	if (i == 0xFFFF)
		return nullptr;
	return &glyphs[i];
}

const char* font::calc_word_wrap_pos(float scale, const char *text, const char *text_end, float wrap_width) const
{
	//TODO: Blatant c&p
	// Simple word-wrapping for English, not full-featured. Please submit failing cases!
	// FIXME: Much possible improvements (don't cut things like "word !", "word!!!" but cut within "word,,,,", more sensible support for punctuations, support for Unicode punctuations, etc.)

	// For references, possible wrap point marked with ^
	//  "aaa bbb, ccc,ddd. eee   fff. ggg!"
	//      ^    ^    ^   ^   ^__    ^    ^

	// List of hardcoded separators: .,;!?'"

	// Skip extra blanks after a line returns (that includes not counting them in width computation)
	// e.g. "Hello    world" --> "Hello" "World"

	// Cut words that cannot possibly fit within one line.
	// e.g.: "The tropical fish" with ~5 characters worth of width --> "The tr" "opical" "fish"

	auto line_width  = 0.0f;
	auto word_width  = 0.0f;
	auto blank_width = 0.0f;
	wrap_width /= scale; // We work with unscaled widths to avoid scaling every characters

	auto word_end             = text;
	const char *prev_word_end = nullptr;
	auto inside_word          = true;

	auto s = text;
	while (s < text_end)
	{
		auto c = static_cast<uint32_t>(*s);
		const char *next_s;
		if (c < 0x80)
			next_s = s + 1;
		else
			next_s = s + text_char_from_utf8(&c, s, text_end);
		if (c == 0)
			break;

		if (c < 32)
		{
			if (c == '\n')
			{
				line_width  = word_width = blank_width = 0.0f;
				inside_word = true;
				s           = next_s;
				continue;
			}
			if (c == '\r')
			{
				s = next_s;
				continue;
			}
		}

		const float char_width = (c < index_advance_x.size() ? index_advance_x[c] : fallback_advance_x);
		if (c == ' ' || c == '\t' || c == 0x3000)
		{
			if (inside_word)
			{
				line_width += blank_width;
				blank_width = 0.0f;
				word_end    = s;
			}
			blank_width += char_width;
			inside_word = false;
		}
		else
		{
			word_width += char_width;
			if (inside_word)
			{
				word_end = next_s;
			}
			else
			{
				prev_word_end = word_end;
				line_width += word_width + blank_width;
				word_width = blank_width = 0.0f;
			}

			// Allow wrapping after punctuation.
			inside_word = !(c == '.' || c == ',' || c == ';' || c == '!' || c == '?' || c == '\"');
		}

		// We ignore blank width at the end of the line (they can be skipped)
		if (line_width + word_width >= wrap_width)
		{
			// Words that cannot possibly fit within an entire line will be cut anywhere.
			if (word_width < wrap_width)
				s = prev_word_end ? prev_word_end : word_end;
			break;
		}

		s = next_s;
	}

	return s;
}

position font::calc_text_size(const float size,
                              const float max_width,
                              const float wrap_width,
                              const char *text_begin,
                              const char *text_end,
                              const char **remaining) const
{
	if (!text_end)
		text_end = text_begin + strlen(text_begin); // FIXME-OPT: Need to avoid this.

	const auto line_height = size;
	const auto scale       = size / font_size;

	auto text_size  = position{0.f, 0.f};
	auto line_width = 0.0f;

	const auto word_wrap_enabled = (wrap_width > 0.0f);
	const char *word_wrap_eol    = nullptr;

	auto s = text_begin;
	while (s < text_end)
	{
		if (word_wrap_enabled)
		{
			// Calculate how far we can render. Requires two passes on the string data but keeps the code simple and not intrusive for what's essentially an uncommon feature.
			if (!word_wrap_eol)
			{
				word_wrap_eol = calc_word_wrap_pos(scale, s, text_end, wrap_width - line_width);
				if (word_wrap_eol == s)
					// Wrap_width is too small to fit anything. Force displaying 1 character to minimize the height discontinuity.
					word_wrap_eol++;
				// +1 may not be a character start point in UTF-8 but it's ok because we use s >= word_wrap_eol below
			}

			if (s >= word_wrap_eol)
			{
				if (text_size.x < line_width)
					text_size.x = line_width;
				text_size.y += line_height;
				line_width    = 0.0f;
				word_wrap_eol = nullptr;

				// Wrapping skips upcoming blanks
				while (s < text_end)
				{
					const char c = *s;
					if (c == ' ' || c == '\t' || c == 0x3000)
					{
						s++;
					}
					else if (c == '\n')
					{
						s++;
						break;
					}
					else
					{
						break;
					}
				}
				continue;
			}
		}

		// Decode and advance source
		const auto prev_s = s;
		auto c            = static_cast<uint32_t>(*s);
		if (c < 0x80)
		{
			s += 1;
		}
		else
		{
			s += text_char_from_utf8(&c, s, text_end);
			if (c == 0) // Malformed UTF-8?
				break;
		}

		if (c < 32)
		{
			if (c == '\n')
			{
				text_size.x = std::max(text_size.x, line_width);
				text_size.y += line_height;
				line_width = 0.0f;
				continue;
			}
			if (c == '\r')
				continue;
		}

		const auto char_width = (c < index_advance_x.size() ? index_advance_x[c] : fallback_advance_x) * scale;
		if (line_width + char_width >= max_width)
		{
			s = prev_s;
			break;
		}

		line_width += char_width;
	}

	if (text_size.x < line_width)
		text_size.x = line_width;

	if (line_width > 0 || text_size.y == 0.0f)
		text_size.y += line_height;

	if (remaining)
		*remaining = s;

	return text_size;
}

rect font::calc_text_bounds(const float size,
                            const float max_width,
                            const float wrap_width,
                            const char *text_begin,
                            const char *text_end,
                            const char **remaining) const
{
	if (!text_end)
		text_end = text_begin + strlen(text_begin); // FIXME-OPT: Need to avoid this.

	auto line_height = 0.f;
	auto offset_y    = FLT_MAX;
	const auto scale = size / font_size;

	auto text_size          = position{0.f, 0.f};
	auto line_width         = 0.0f;
	auto first_char_of_line = true;

	const auto word_wrap_enabled = (wrap_width > 0.0f);
	const char *word_wrap_eol    = nullptr;

	auto s = text_begin;
	while (s < text_end)
	{
		if (word_wrap_enabled)
		{
			// Calculate how far we can render. Requires two passes on the string data but keeps the code simple and not intrusive for what's essentially an uncommon feature.
			if (!word_wrap_eol)
			{
				word_wrap_eol = calc_word_wrap_pos(scale, s, text_end, wrap_width - line_width);
				if (word_wrap_eol == s)
					// Wrap_width is too small to fit anything. Force displaying 1 character to minimize the height discontinuity.
					word_wrap_eol++;
				// +1 may not be a character start point in UTF-8 but it's ok because we use s >= word_wrap_eol below
			}

			if (s >= word_wrap_eol)
			{
				if (text_size.x < line_width)
					text_size.x = line_width;
				text_size.y += line_height;
				line_width    = 0.0f;
				word_wrap_eol = nullptr;

				// Wrapping skips upcoming blanks
				while (s < text_end)
				{
					const char c = *s;
					if (c == ' ' || c == '\t' || c == 0x3000)
					{
						s++;
					}
					else if (c == '\n')
					{
						s++;
						break;
					}
					else
					{
						break;
					}
				}
				continue;
			}
		}

		// Decode and advance source
		const auto prev_s = s;
		auto c            = static_cast<uint32_t>(*s);
		if (c < 0x80)
		{
			s += 1;
		}
		else
		{
			s += text_char_from_utf8(&c, s, text_end);
			if (c == 0) // Malformed UTF-8?
				break;
		}

		if (c < 32)
		{
			if (c == '\n')
			{
				text_size.x = std::max(text_size.x, line_width);
				text_size.y += line_height;
				line_width         = 0.0f;
				line_height        = 0.f;
				first_char_of_line = true;
				continue;
			}
			if (c == '\r')
				continue;
		}

		const auto glyph_info = find_glyph(c);
		const auto char_width = glyph_info->advance_x * scale;
		if (line_width + char_width >= max_width)
		{
			s = prev_s;
			break;
		}

		if (first_char_of_line)
		{
			first_char_of_line = false;
			line_width += glyph_info->x0 * scale;
		}
		line_width += char_width;
		line_height = std::max(line_height, glyph_info->y1 * scale);
		offset_y    = std::min(offset_y, glyph_info->y0 * scale);
	}

	if (text_size.x < line_width)
		text_size.x = line_width;

	if (line_width > 0 || text_size.y == 0.0f)
		text_size.y += line_height;

	if (remaining)
		*remaining = s;

	return rect{0.f, offset_y, line_width, line_height};
}

void font::render_char(draw_buffer *draw_buffer,
                       const float size,
                       position pos,
                       const pack_color col,
                       const font_wchar c) const
{
	if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
		return;
	if (const auto glyph = find_glyph(c))
	{
		const auto scale = (size >= 0.f) ? (size / font_size) : 1.f;
		pos.x            = static_cast<float>(static_cast<int>(pos.x + display_offset.x));
		pos.y            = static_cast<float>(static_cast<int>(pos.y + display_offset.y));
		draw_buffer->prim_reserve(6, 4);
		draw_buffer->prim_rect_uv(position{pos.x + glyph->x0 * scale, pos.y + glyph->y0 * scale},
		                          position{pos.x + glyph->x1 * scale, pos.y + glyph->y1 * scale},
		                          position{glyph->u0, glyph->v0},
		                          position{glyph->u1, glyph->v1},
		                          col);
	}
}

void font::render_text(draw_buffer *draw_buffer,
                       const float size,
                       position pos,
                       const pack_color col,
                       const rect &clip_rect,
                       const char *text_begin,
                       const char *text_end,
                       const float wrap_width,
                       const bool cpu_fine_clip) const
{
	//TODO: more c&p
	if (!text_end)
		text_end = text_begin + strlen(text_begin);
	// ImGui functions generally already provides a valid text_end, so this is merely to handle direct calls.

	// Align to be pixel perfect
	pos.x  = static_cast<float>(static_cast<int>(pos.x + display_offset.x));
	pos.y  = static_cast<float>(static_cast<int>(pos.y + display_offset.y));
	auto x = pos.x;
	auto y = pos.y;
	if (y > clip_rect.w)
		return;

	const auto scale             = size / font_size;
	const auto line_height       = font_size * scale;
	const auto word_wrap_enabled = (wrap_width > 0.0f);
	const char *word_wrap_eol    = nullptr;

	// Fast-forward to first visible line
	auto s = text_begin;
	if (y + line_height < clip_rect.y && !word_wrap_enabled)
		while (y + line_height < clip_rect.y && s < text_end)
		{
			s = reinterpret_cast<const char*>(memchr(s, '\n', text_end - s));
			s = s ? s + 1 : text_end;
			y += line_height;
		}

	// For large text, scan for the last visible line in order to avoid over-reserving in the call to PrimReserve()
	// Note that very large horizontal line will still be affected by the issue (e.g. a one megabyte string buffer without a newline will likely crash atm)
	if (text_end - s > 10000 && !word_wrap_enabled)
	{
		const char *s_end = s;
		float y_end       = y;
		while (y_end < clip_rect.w && s_end < text_end)
		{
			s_end = reinterpret_cast<const char*>(memchr(s_end, '\n', text_end - s_end));
			s     = s ? s + 1 : text_end;
			y_end += line_height;
		}
		text_end = s_end;
	}
	if (s == text_end)
		return;

	// Reserve vertices for remaining worse case (over-reserving is useful and easily amortized)
	const auto vtx_count_max     = static_cast<int32_t>(text_end - s) * 4;
	const auto idx_count_max     = static_cast<int32_t>(text_end - s) * 6;
	const auto idx_expected_size = draw_buffer->indices.size() + idx_count_max;
	const auto vtx_expected_size = draw_buffer->vertices.size() + vtx_count_max;
	draw_buffer->prim_reserve(idx_count_max, vtx_count_max);

	auto vtx_write       = draw_buffer->vtx_write_ptr;
	auto idx_write       = draw_buffer->idx_write_ptr;
	auto vtx_current_idx = draw_buffer->cur_idx;

	while (s < text_end)
	{
		if (word_wrap_enabled)
		{
			// Calculate how far we can render. Requires two passes on the string data but keeps the code simple and not intrusive for what's essentially an uncommon feature.
			if (!word_wrap_eol)
			{
				word_wrap_eol = calc_word_wrap_pos(scale, s, text_end, wrap_width - (x - pos.x));
				if (word_wrap_eol == s)
					// Wrap_width is too small to fit anything. Force displaying 1 character to minimize the height discontinuity.
					word_wrap_eol++;
				// +1 may not be a character start point in UTF-8 but it's ok because we use s >= word_wrap_eol below
			}

			if (s >= word_wrap_eol)
			{
				x = pos.x;
				y += line_height;
				word_wrap_eol = nullptr;

				// Wrapping skips upcoming blanks
				while (s < text_end)
				{
					const char c = *s;
					if (c == ' ' || c == '\t' || c == 0x3000)
					{
						s++;
					}
					else if (c == '\n')
					{
						s++;
						break;
					}
					else
					{
						break;
					}
				}
				continue;
			}
		}

		// Decode and advance source
		auto c = static_cast<uint32_t>(*s);
		if (c < 0x80)
		{
			s += 1;
		}
		else
		{
			s += text_char_from_utf8(&c, s, text_end);
			if (c == 0) // Malformed UTF-8?
				break;
		}

		if (c < 32)
		{
			if (c == '\n')
			{
				x = pos.x;
				y += line_height;
				if (y > clip_rect.w)
					break; // break out of main loop
				continue;
			}
			if (c == '\r')
				continue;
		}

		auto char_width = 0.0f;
		if (const auto glyph = find_glyph(static_cast<font_wchar>(c)))
		{
			char_width = glyph->advance_x * scale;

			// Arbitrarily assume that both space and tabs are empty glyphs as an optimization
			if (c != ' ' && c != '\t')
			{
				// We don't do a second finer clipping test on the Y axis as we've already skipped anything before clip_rect.y and exit once we pass clip_rect.w
				float x1 = x + glyph->x0 * scale;
				float x2 = x + glyph->x1 * scale;
				float y1 = y + glyph->y0 * scale;
				float y2 = y + glyph->y1 * scale;
				if (x1 <= clip_rect.z && x2 >= clip_rect.x)
				{
					// Render a character
					float u1 = glyph->u0;
					float v1 = glyph->v0;
					float u2 = glyph->u1;
					float v2 = glyph->v1;

					// CPU side clipping used to fit text in their frame when the frame is too small. Only does clipping for axis aligned quads.
					if (cpu_fine_clip)
					{
						if (x1 < clip_rect.x)
						{
							u1 = u1 + (1.0f - (x2 - clip_rect.x) / (x2 - x1)) * (u2 - u1);
							x1 = clip_rect.x;
						}
						if (y1 < clip_rect.y)
						{
							v1 = v1 + (1.0f - (y2 - clip_rect.y) / (y2 - y1)) * (v2 - v1);
							y1 = clip_rect.y;
						}
						if (x2 > clip_rect.z)
						{
							u2 = u1 + ((clip_rect.z - x1) / (x2 - x1)) * (u2 - u1);
							x2 = clip_rect.z;
						}
						if (y2 > clip_rect.w)
						{
							v2 = v1 + ((clip_rect.w - y1) / (y2 - y1)) * (v2 - v1);
							y2 = clip_rect.w;
						}
						if (y1 >= y2)
						{
							x += char_width;
							continue;
						}
					}

					//TODO: Reformat to proper stuff
					// We are NOT calling PrimRectUV() here because non-inlined causes too much overhead in a debug builds. Inlined here:
					{
						idx_write[0]       = (draw_buffer::draw_index)(vtx_current_idx);
						idx_write[1]       = (draw_buffer::draw_index)(vtx_current_idx + 1);
						idx_write[2]       = (draw_buffer::draw_index)(vtx_current_idx + 2);
						idx_write[3]       = (draw_buffer::draw_index)(vtx_current_idx);
						idx_write[4]       = (draw_buffer::draw_index)(vtx_current_idx + 2);
						idx_write[5]       = (draw_buffer::draw_index)(vtx_current_idx + 3);
						vtx_write[0].pos.x = x1;
						vtx_write[0].pos.y = y1;
						vtx_write[0].col   = col;
						vtx_write[0].uv.x  = u1;
						vtx_write[0].uv.y  = v1;
						vtx_write[1].pos.x = x2;
						vtx_write[1].pos.y = y1;
						vtx_write[1].col   = col;
						vtx_write[1].uv.x  = u2;
						vtx_write[1].uv.y  = v1;
						vtx_write[2].pos.x = x2;
						vtx_write[2].pos.y = y2;
						vtx_write[2].col   = col;
						vtx_write[2].uv.x  = u2;
						vtx_write[2].uv.y  = v2;
						vtx_write[3].pos.x = x1;
						vtx_write[3].pos.y = y2;
						vtx_write[3].col   = col;
						vtx_write[3].uv.x  = u1;
						vtx_write[3].uv.y  = v2;
						vtx_write += 4;
						vtx_current_idx += 4;
						idx_write += 6;
					}
				}
			}
		}

		x += char_width;
	}

	// Give back unused vertices
	draw_buffer->vertices.resize(static_cast<int32_t>(vtx_write - draw_buffer->vertices.data()));
	draw_buffer->indices.resize(static_cast<int32_t>(idx_write - draw_buffer->indices.data()));
	draw_buffer->cmds[draw_buffer->cmds.size() - 1].elem_count -= (idx_expected_size - draw_buffer->indices.size());
	draw_buffer->cmds[draw_buffer->cmds.size() - 1].vtx_count -= (vtx_expected_size - draw_buffer->vertices.size());
	draw_buffer->vtx_write_ptr = vtx_write;
	draw_buffer->idx_write_ptr = idx_write;
	draw_buffer->cur_idx       = static_cast<uint32_t>(draw_buffer->vertices.size());
}


#pragma endregion


#pragma region font_atlas

#pragma region custom_rect

font_atlas::custom_rect::custom_rect()
{
	id              = 0xFFFFFFFF;
	width           = height = 0;
	x               = y      = 0xFFFF;
	glyph_advance_x = 0.f;
	glyph_offset    = position{0.f, 0.f};
	font            = nullptr;
}

#pragma endregion


font_atlas::font_atlas()
{
	locked            = false;
	flags             = FONT_ATLAS_FLAGS_NONE;
	tex_id            = nullptr;
	tex_desired_width = 0;
	tex_glyph_padding = 1;

	tex_pixels_alpha_8 = nullptr;
	tex_pixels_rgba_32 = nullptr;
	tex_width          = tex_height = 0;
	tex_uv_scale       = position{0.f, 0.f};
	tex_uv_white_pixel = position{0.f, 0.f};
	for (auto &i : custom_rect_idx)
		i = -1;
}

font_atlas::~font_atlas()
{
	assert(!locked);
	clear();
}

void font_atlas::clear_input_data()
{
	assert(!locked);

	for (auto &data : config_data)
	{
		if (data.font_data && data.owned_by_atlas)
		{
			free(data.font_data);
			data.font_data = nullptr;
		}
	}

	//TODO: ConfigData clear?

	config_data.clear();
	custom_rects.clear();
	for (auto i            = 0u; i == custom_rect_idx.size(); i++)
		custom_rect_idx[i] = -1;
}

void font_atlas::clear_tex_data(const bool lock_tex)
{
	if (lock_tex)
	{
		std::lock_guard g(tex_mutex);
		//assert( !locked );

		if (tex_pixels_alpha_8)
			free(tex_pixels_alpha_8);
		if (tex_pixels_rgba_32)
			free(tex_pixels_rgba_32);
		tex_pixels_alpha_8 = nullptr;
		tex_pixels_rgba_32 = nullptr;
	}
	else
	{
		//assert( !locked );

		if (tex_pixels_alpha_8)
			free(tex_pixels_alpha_8);
		if (tex_pixels_rgba_32)
			free(tex_pixels_rgba_32);
		tex_pixels_alpha_8 = nullptr;
		tex_pixels_rgba_32 = nullptr;
	}
}

void font_atlas::clear_fonts()
{
	fonts.clear();
}

void font_atlas::clear()
{
	clear_input_data();
	clear_tex_data();
	clear_fonts();
}


void font_atlas::tex_data_as_alpha_8(uint8_t **out_pixels,
                                     uint32_t *out_width,
                                     uint32_t *out_height,
                                     uint32_t *out_bytes_per_pixel)
{
	//Build if it isnt already
	if (!tex_pixels_alpha_8)
	{
		if (config_data.empty())
		{
			*out_pixels = nullptr;
			return;
		}
		build();
	}

	*out_pixels = tex_pixels_alpha_8;
	if (out_width)
		*out_width = tex_width;
	if (out_height)
		*out_height = tex_height;
	if (out_bytes_per_pixel)
		*out_bytes_per_pixel = 1;
}

void font_atlas::tex_data_as_rgba_32(uint8_t **out_pixels,
                                     uint32_t *out_width,
                                     uint32_t *out_height,
                                     uint32_t *out_bytes_per_pixel)
{
	if (!tex_pixels_rgba_32)
	{
		uint8_t *pixels;
		tex_data_as_alpha_8(&pixels, nullptr, nullptr);
		if (pixels)
		{
			tex_pixels_rgba_32 = reinterpret_cast<uint32_t *>(malloc(tex_width * tex_height * 4));
			const auto *src    = pixels;
			auto dst           = tex_pixels_rgba_32;
			for (int i = tex_width * tex_height; i > 0; i--)
				*dst++ = pack_color{color{255, 255, 255, (*src++)}}.as_argb();
		}
	}

	*out_pixels = reinterpret_cast<uint8_t*>(tex_pixels_rgba_32);
	if (out_width)
		*out_width = tex_width;
	if (out_height)
		*out_height = tex_height;
	if (out_bytes_per_pixel)
		*out_bytes_per_pixel = 4;
}

font* font_atlas::add_font(const font_config *font_cfg)
{
	//assert(!locked);
	assert(font_cfg->font_data != nullptr && font_cfg->font_data_size > 0);
	assert(font_cfg->size_pixels > 0.f);

	if (!font_cfg->merge_mode)
		fonts.emplace_back(std::make_shared<font>());
	else
		assert(!fonts.empty());

	config_data.emplace_back(*font_cfg);
	auto &new_font_cfg = config_data.back();

	if (!new_font_cfg.dst_font)
		new_font_cfg.dst_font = fonts.back();
	if (!new_font_cfg.owned_by_atlas)
	{
		new_font_cfg.font_data      = malloc(new_font_cfg.font_data_size);
		new_font_cfg.owned_by_atlas = true;
		memcpy(new_font_cfg.font_data, font_cfg->font_data, new_font_cfg.font_data_size);
	}

	std::lock_guard g(tex_mutex);
	clear_tex_data(false);
	build();
	return new_font_cfg.dst_font.get();
}

//TODO: AddFontDefault

font* font_atlas::add_font_from_ttf(const char *file_name,
                                    float size_pixels,
                                    const font_config *font_cfg_template,
                                    const font_wchar *glyph_ranges)
{
	//assert(!locked);

	size_t data_size = 0u;
	auto data        = load_file_to_mem(file_name, &data_size);
	if (!data)
	{
		assert(0);
		return nullptr;
	}

	auto font_cfg = font_cfg_template ? *font_cfg_template : font_config();
	if (font_cfg.name[0] == '\0')
	{
		const char *p;
		for (p = file_name + strlen(file_name); p > file_name && p[-1] != '/' && p[-1] != '\\'; p--) { }
		format_string(font_cfg.name.data(), font_cfg.name.size(), "%s, %.0fpx", p, size_pixels);
	}
	return add_font_from_ttf_mem(data, data_size, size_pixels, &font_cfg, glyph_ranges);
}

font* font_atlas::add_font_from_ttf_mem(void *font_data,
                                        const size_t data_size,
                                        const float size_pixels,
                                        const font_config *font_cfg_template,
                                        const font_wchar *glyph_ranges)
{
	//assert(!locked);

	auto font_cfg = font_cfg_template ? *font_cfg_template : font_config();
	assert(font_cfg.font_data == nullptr);
	font_cfg.font_data      = font_data;
	font_cfg.font_data_size = data_size;
	font_cfg.size_pixels    = size_pixels;

	if (glyph_ranges)
		font_cfg.glyph_ranges = glyph_ranges;
	return add_font(&font_cfg);
}

void font_atlas::remove_font(const font *font_ptr)
{
	//assert( !locked );

	config_data.erase(
		std::remove_if(config_data.begin(),
		               config_data.end(),
		               [&](const font_config &cfg) -> bool
		               {
			               return (cfg.dst_font.get() == font_ptr);
		               }),
		config_data.end());

	fonts.erase(
		std::remove_if(fonts.begin(),
		               fonts.end(),
		               [&](const std::shared_ptr<font> &ptr) -> bool
		               {
			               return (ptr.get() == font_ptr);
		               }),
		fonts.end());

	std::lock_guard g(tex_mutex);
	build();
}


uint32_t font_atlas::add_custom_rect_regular(uint32_t id, uint16_t width, uint16_t height)
{
	assert(id >= 0x10000);
	assert(width > 0 && width <= 0xFFFF);
	assert(height > 0 && height <= 0xFFFF);
	custom_rect rect;
	rect.id     = id;
	rect.width  = width;
	rect.height = height;
	custom_rects.emplace_back(rect);
	return custom_rects.size() - 1;
}

uint32_t font_atlas::add_custom_rect_glyph(font *font,
                                           font_wchar id,
                                           uint16_t width,
                                           uint16_t height,
                                           float advance_x,
                                           const position &offset)
{
	assert(font);
	assert(width > 0 && width <= 0xFFFF);
	assert(height > 0 && height <= 0xFFFF);

	custom_rect rect;
	rect.id              = id;
	rect.width           = width;
	rect.height          = height;
	rect.glyph_advance_x = advance_x;
	rect.glyph_offset    = offset;
	rect.font            = font;
	custom_rects.emplace_back(rect);
	return custom_rects.size() - 1;
}

void font_atlas::calc_custom_rect_uv(const custom_rect *rect, position *out_uv_min, position *out_uv_max)
{
	assert(tex_width > 0 && tex_height > 0);
	assert(rect->is_packed( ));
	*out_uv_min = position{rect->x * tex_uv_scale.x, rect->y * tex_uv_scale.y};
	*out_uv_max = position{(rect->x + rect->width) * tex_uv_scale.x, (rect->y + rect->height) * tex_uv_scale.y};
}

void font_atlas_build_register_default_custom_rects(font_atlas *atlas);
void font_atlas_build_pack_custom_rects(font_atlas *atlas, stbrp_context *pack_context);
void font_atlas_build_setup_font(font_atlas *atlas, font *font, font_config *font_config, float ascent, float descent);
void font_atlas_build_multiply_calc_lookup_table(unsigned char out_table[ 256 ], float in_brighten_factor);
void font_atlas_build_finish(font_atlas *atlas);

bool font_atlas::build(const uint32_t extra_flags)
{
	assert(!config_data.empty());
	assert(tex_glyph_padding == 1);

	font_atlas_build_register_default_custom_rects(this);

	tex_id             = nullptr;
	tex_width          = tex_height = 0;
	tex_uv_scale       = position{0.f, 0.f};
	tex_uv_white_pixel = position{0.f, 0.f};
	clear_tex_data(false);

	//std::vector<font> fonts;
	//fonts.resize( config_data.size( ) );

	auto max_glyph_size = position{1.f, 1.f};

	auto total_glyphs_count = 0;
	auto total_ranges_count = 0;
	for (auto input_i = 0u; input_i < config_data.size(); input_i++)
	{
		auto &cfg       = config_data[input_i];
		auto &font_face = fonts[input_i];
		assert(cfg.dst_font && (!cfg.dst_font->loaded( ) || cfg.dst_font->container_atlas == this));

		if (!font_face->init(cfg, extra_flags))
			return false;

		max_glyph_size.x = std::max(max_glyph_size.x, font_face->info.max_advance_width);
		max_glyph_size.y = std::max(max_glyph_size.y, font_face->info.ascender - font_face->info.descender);

		if (!cfg.glyph_ranges)
			cfg.glyph_ranges = glyph_ranges_default();
		for (auto in_range = cfg.glyph_ranges; in_range[0] && in_range[1]; in_range += 2, total_ranges_count++)
		{
			total_glyphs_count += (in_range[1] - in_range[0]) + 1;
		}
	}

	tex_width = (tex_desired_width > 0)
		            ? tex_desired_width
		            : (total_glyphs_count > 4000)
		            ? 4096
		            : (total_glyphs_count > 2000)
		            ? 2048
		            : (total_glyphs_count > 1000)
		            ? 1024
		            : 512;

	const auto total_rects    = total_glyphs_count + custom_rects.size();
	auto min_rects_per_row    = std::ceilf((tex_width / (max_glyph_size.x + 1.f)));
	auto min_rects_per_column = std::ceilf(total_rects / min_rects_per_row);
	tex_height                = static_cast<uint32_t>(min_rects_per_column * (max_glyph_size.y + 1.f));

	tex_height = (flags & FONT_ATLAS_FLAGS_NO_POWER_OF_TWO_HEIGHT)
		             ? (tex_height + 1)
		             : upper_power_of_two(tex_height);
	tex_uv_scale       = position{1.f / tex_width, 1.f / tex_height};
	tex_pixels_alpha_8 = reinterpret_cast<unsigned char *>(malloc(tex_width * tex_height));
	memset(tex_pixels_alpha_8, 0, tex_width * tex_height);

	std::vector<stbrp_node> pack_nodes;
	pack_nodes.resize(total_rects);
	stbrp_context context;
	stbrp_init_target(&context, tex_width, tex_height, pack_nodes.data(), total_rects);

	font_atlas_build_pack_custom_rects(this, &context);

	for (auto input_i = 0u; input_i < config_data.size(); input_i++)
	{
		auto &cfg       = config_data[input_i];
		auto &font_face = fonts[input_i];
		auto dst_font   = cfg.dst_font;
		if (cfg.merge_mode)
			dst_font->build_lookup_table();

		const auto ascent  = font_face->info.ascender;
		const auto descent = font_face->info.descender;
		font_atlas_build_setup_font(this, dst_font.get(), &cfg, ascent, descent);
		const auto font_off_x = cfg.glyph_offset.x;
		const auto font_off_y = cfg.glyph_offset.y + static_cast<float>(static_cast<int>(dst_font->ascent + 0.5f));

		auto multiply_enabled = (cfg.rasterizer_multiply != 1.0f);
		unsigned char multiply_table[ 256 ];
		if (multiply_enabled)
			font_atlas_build_multiply_calc_lookup_table(multiply_table, cfg.rasterizer_multiply);

		for (auto in_range = cfg.glyph_ranges; in_range[0] && in_range[1]; in_range += 2)
		{
			for (uint32_t codepoint = in_range[0]; codepoint <= in_range[1]; ++codepoint)
			{
				if (cfg.merge_mode && dst_font->find_glyph_no_fallback(codepoint))
					continue;

				FT_Glyph ft_glyph              = nullptr;
				FT_BitmapGlyph ft_glyph_bitmap = nullptr; // NB: will point to bitmap within FT_Glyph
				glyph_info glyph_info;
				if (!font_face->calc_glyph_info(codepoint, glyph_info, ft_glyph, ft_glyph_bitmap))
					continue;

				// Pack rectangle
				stbrp_rect rect;
				rect.w = static_cast<stbrp_coord>(glyph_info.width + 1.f); // Account for texture filtering
				rect.h = static_cast<stbrp_coord>(glyph_info.height + 1.f);
				stbrp_pack_rects(&context, &rect, 1);

				// Copy rasterized pixels to main texture
				uint8_t *blit_dst = tex_pixels_alpha_8 + rect.y * tex_width + rect.x;
				font_face->blit_glyph(ft_glyph_bitmap,
				                      blit_dst,
				                      tex_width,
				                      multiply_enabled ? multiply_table : nullptr);
				FT_Done_Glyph(ft_glyph);

				auto char_advance_x_org = glyph_info.advance_x;
				auto char_advance_x_mod = std::clamp(char_advance_x_org, cfg.glyph_min_advance, cfg.glyph_max_advance);
				auto char_off_x         = font_off_x;
				if (char_advance_x_org != char_advance_x_mod)
					char_off_x += cfg.pixel_snap_h
						              ? (float)(int)((char_advance_x_mod - char_advance_x_org) * 0.5f)
						              : (char_advance_x_mod - char_advance_x_org) * 0.5f;

				// Register glyph
				dst_font->add_glyph(codepoint,
				                    glyph_info.offset_x + char_off_x,
				                    glyph_info.offset_y + font_off_y,
				                    glyph_info.offset_x + char_off_x + glyph_info.width,
				                    glyph_info.offset_y + font_off_y + glyph_info.height,
				                    rect.x / (float)tex_width,
				                    rect.y / (float)tex_height,
				                    (rect.x + glyph_info.width) / (float)tex_width,
				                    (rect.y + glyph_info.height) / (float)tex_height,
				                    char_advance_x_mod);
			}
		}
	}

	for (auto &font : fonts)
		font->shutdown();

	font_atlas_build_finish(this);

	has_updated = true;

	return true;
}


void font_atlas_build_register_default_custom_rects(font_atlas *atlas)
{
	if (atlas->custom_rect_idx[0] >= 0)
		return;

	atlas->custom_rect_idx[0] = atlas->add_custom_rect_regular(FONT_ATLAS_DEFAULT_TEX_DATA_ID, 2, 2);
}

void font_atlas_build_pack_custom_rects(font_atlas *atlas, stbrp_context *pack_context)
{
	auto &user_rects = atlas->custom_rects;
	assert(user_rects.size() >= 1);

	std::vector<stbrp_rect> pack_rects;
	pack_rects.resize(user_rects.size());
	memset(pack_rects.data(), 0, sizeof(stbrp_rect) * user_rects.size());

	for (auto i = 0u; i < user_rects.size(); i++)
	{
		pack_rects[i].w = user_rects[i].width;
		pack_rects[i].h = user_rects[i].height;
	}
	stbrp_pack_rects(pack_context, pack_rects.data(), pack_rects.size());
	for (auto i = 0u; i < pack_rects.size(); i++)
	{
		if (pack_rects[i].was_packed)
		{
			user_rects[i].x = pack_rects[i].x;
			user_rects[i].y = pack_rects[i].y;
			assert(pack_rects[i].w == user_rects[i].width && pack_rects[i].h == user_rects[i].height);
			atlas->tex_height = std::max(atlas->tex_height, static_cast<uint32_t>(pack_rects[i].y + pack_rects[i].h));
		}
	}
}

void font_atlas_build_setup_font(font_atlas *atlas, font *font, font_config *font_config, float ascent, float descent)
{
	if (!font_config->merge_mode)
	{
		font->clear_output_data();
		font->font_size       = font_config->size_pixels;
		font->config_data     = font_config;
		font->container_atlas = atlas;
		font->ascent          = ascent;
		font->descent         = descent;
	}
	font->config_data_count++;
}

void font_atlas_build_multiply_calc_lookup_table(unsigned char out_table[256], float in_brighten_factor)
{
	for (auto i = 0u; i < 256; i++)
	{
		const auto val = static_cast<uint32_t>(i * in_brighten_factor);
		out_table[i]   = val > 255 ? 255 : (val & 0xFF);
	}
}

void font_atlas_build_render_default_tex_data(font_atlas *atlas)
{
	assert(atlas->custom_rect_idx[0] >= 0);
	assert(atlas->tex_pixels_alpha_8 != nullptr);
	auto &r = atlas->custom_rects[atlas->custom_rect_idx[0]];
	assert(r.id == FONT_ATLAS_DEFAULT_TEX_DATA_ID);
	assert(r.is_packed( ));

	const auto w = atlas->tex_width;
	if (!(atlas->flags & FONT_ATLAS_FLAGS_NO_MOUSE_CURSORS))
	{
		//TODO
	}
	else
	{
		assert(r.width == 2 && r.height == 2);
		const int offset                  = (r.x) + (r.y) * w;
		atlas->tex_pixels_alpha_8[offset] = atlas->tex_pixels_alpha_8[offset + 1] = atlas->tex_pixels_alpha_8[offset
			+ w
		] = atlas->tex_pixels_alpha_8[offset + w + 1] = 0xFF;
	}
	atlas->tex_uv_white_pixel = position{(r.x + 0.5f) * atlas->tex_uv_scale.x, (r.y + 0.5f) * atlas->tex_uv_scale.y};
}

void font_atlas_build_finish(font_atlas *atlas)
{
	font_atlas_build_render_default_tex_data(atlas);

	for (auto i = 0u; i < atlas->custom_rects.size(); i++)
	{
		const auto &r = atlas->custom_rects[i];
		if (r.font == nullptr || r.id > 0x10000)
			continue;

		assert(r.font->container_atlas == atlas);
		position uv0, uv1;
		atlas->calc_custom_rect_uv(&r, &uv0, &uv1);
		r.font->add_glyph(r.id,
		                  r.glyph_offset.x,
		                  r.glyph_offset.y,
		                  r.glyph_offset.x + r.width,
		                  r.glyph_offset.y + r.height,
		                  uv0.x,
		                  uv0.y,
		                  uv1.x,
		                  uv1.y,
		                  r.glyph_advance_x);
	}

	for (auto &font : atlas->fonts)
		if (font->dirty_lookup_tables)
			font->build_lookup_table();
}

const font_wchar* font_atlas::glyph_ranges_default()
{
	static const font_wchar ranges[ ] =
	{
		0x0020,
		0x00FF,
		// Basic Latin + Latin Supplement
		0,
	};
	return &ranges[0];
}

static void UnpackAccumulativeOffsetsIntoRanges(int base_codepoint,
                                                const short *accumulative_offsets,
                                                int accumulative_offsets_count,
                                                font_wchar *out_ranges)
{
	for (auto n = 0; n < accumulative_offsets_count; n++, out_ranges += 2)
	{
		out_ranges[0] = out_ranges[1] = (font_wchar)(base_codepoint + accumulative_offsets[n]);
		base_codepoint += accumulative_offsets[n];
	}
	out_ranges[0] = 0;
}

const font_wchar* font_atlas::glyph_ranges_japanese()
{
	// 1946 common ideograms code points for Japanese
	// Sourced from http://theinstructionlimit.com/common-kanji-character-ranges-for-xna-spritefont-rendering
	// FIXME: Source a list of the revised 2136 Joyo Kanji list from 2010 and rebuild this.
	// You can use ImFontAtlas::GlyphRangesBuilder to create your own ranges derived from this, by merging existing ranges or adding new characters.
	// (Stored as accumulative offsets from the initial unicode codepoint 0x4E00. This encoding is designed to helps us compact the source code size.)
	static const short accumulative_offsets_from_0x4E00[ ] =
	{
		0,
		1,
		2,
		4,
		1,
		1,
		1,
		1,
		2,
		1,
		6,
		2,
		2,
		1,
		8,
		5,
		7,
		11,
		1,
		2,
		10,
		10,
		8,
		2,
		4,
		20,
		2,
		11,
		8,
		2,
		1,
		2,
		1,
		6,
		2,
		1,
		7,
		5,
		3,
		7,
		1,
		1,
		13,
		7,
		9,
		1,
		4,
		6,
		1,
		2,
		1,
		10,
		1,
		1,
		9,
		2,
		2,
		4,
		5,
		6,
		14,
		1,
		1,
		9,
		3,
		18,
		5,
		4,
		2,
		2,
		10,
		7,
		1,
		1,
		1,
		3,
		2,
		4,
		3,
		23,
		2,
		10,
		12,
		2,
		14,
		2,
		4,
		13,
		1,
		6,
		10,
		3,
		1,
		7,
		13,
		6,
		4,
		13,
		5,
		2,
		3,
		17,
		2,
		2,
		5,
		7,
		6,
		4,
		1,
		7,
		14,
		16,
		6,
		13,
		9,
		15,
		1,
		1,
		7,
		16,
		4,
		7,
		1,
		19,
		9,
		2,
		7,
		15,
		2,
		6,
		5,
		13,
		25,
		4,
		14,
		13,
		11,
		25,
		1,
		1,
		1,
		2,
		1,
		2,
		2,
		3,
		10,
		11,
		3,
		3,
		1,
		1,
		4,
		4,
		2,
		1,
		4,
		9,
		1,
		4,
		3,
		5,
		5,
		2,
		7,
		12,
		11,
		15,
		7,
		16,
		4,
		5,
		16,
		2,
		1,
		1,
		6,
		3,
		3,
		1,
		1,
		2,
		7,
		6,
		6,
		7,
		1,
		4,
		7,
		6,
		1,
		1,
		2,
		1,
		12,
		3,
		3,
		9,
		5,
		8,
		1,
		11,
		1,
		2,
		3,
		18,
		20,
		4,
		1,
		3,
		6,
		1,
		7,
		3,
		5,
		5,
		7,
		2,
		2,
		12,
		3,
		1,
		4,
		2,
		3,
		2,
		3,
		11,
		8,
		7,
		4,
		17,
		1,
		9,
		25,
		1,
		1,
		4,
		2,
		2,
		4,
		1,
		2,
		7,
		1,
		1,
		1,
		3,
		1,
		2,
		6,
		16,
		1,
		2,
		1,
		1,
		3,
		12,
		20,
		2,
		5,
		20,
		8,
		7,
		6,
		2,
		1,
		1,
		1,
		1,
		6,
		2,
		1,
		2,
		10,
		1,
		1,
		6,
		1,
		3,
		1,
		2,
		1,
		4,
		1,
		12,
		4,
		1,
		3,
		1,
		1,
		1,
		1,
		1,
		10,
		4,
		7,
		5,
		13,
		1,
		15,
		1,
		1,
		30,
		11,
		9,
		1,
		15,
		38,
		14,
		1,
		32,
		17,
		20,
		1,
		9,
		31,
		2,
		21,
		9,
		4,
		49,
		22,
		2,
		1,
		13,
		1,
		11,
		45,
		35,
		43,
		55,
		12,
		19,
		83,
		1,
		3,
		2,
		3,
		13,
		2,
		1,
		7,
		3,
		18,
		3,
		13,
		8,
		1,
		8,
		18,
		5,
		3,
		7,
		25,
		24,
		9,
		24,
		40,
		3,
		17,
		24,
		2,
		1,
		6,
		2,
		3,
		16,
		15,
		6,
		7,
		3,
		12,
		1,
		9,
		7,
		3,
		3,
		3,
		15,
		21,
		5,
		16,
		4,
		5,
		12,
		11,
		11,
		3,
		6,
		3,
		2,
		31,
		3,
		2,
		1,
		1,
		23,
		6,
		6,
		1,
		4,
		2,
		6,
		5,
		2,
		1,
		1,
		3,
		3,
		22,
		2,
		6,
		2,
		3,
		17,
		3,
		2,
		4,
		5,
		1,
		9,
		5,
		1,
		1,
		6,
		15,
		12,
		3,
		17,
		2,
		14,
		2,
		8,
		1,
		23,
		16,
		4,
		2,
		23,
		8,
		15,
		23,
		20,
		12,
		25,
		19,
		47,
		11,
		21,
		65,
		46,
		4,
		3,
		1,
		5,
		6,
		1,
		2,
		5,
		26,
		2,
		1,
		1,
		3,
		11,
		1,
		1,
		1,
		2,
		1,
		2,
		3,
		1,
		1,
		10,
		2,
		3,
		1,
		1,
		1,
		3,
		6,
		3,
		2,
		2,
		6,
		6,
		9,
		2,
		2,
		2,
		6,
		2,
		5,
		10,
		2,
		4,
		1,
		2,
		1,
		2,
		2,
		3,
		1,
		1,
		3,
		1,
		2,
		9,
		23,
		9,
		2,
		1,
		1,
		1,
		1,
		5,
		3,
		2,
		1,
		10,
		9,
		6,
		1,
		10,
		2,
		31,
		25,
		3,
		7,
		5,
		40,
		1,
		15,
		6,
		17,
		7,
		27,
		180,
		1,
		3,
		2,
		2,
		1,
		1,
		1,
		6,
		3,
		10,
		7,
		1,
		3,
		6,
		17,
		8,
		6,
		2,
		2,
		1,
		3,
		5,
		5,
		8,
		16,
		14,
		15,
		1,
		1,
		4,
		1,
		2,
		1,
		1,
		1,
		3,
		2,
		7,
		5,
		6,
		2,
		5,
		10,
		1,
		4,
		2,
		9,
		1,
		1,
		11,
		6,
		1,
		44,
		1,
		3,
		7,
		9,
		5,
		1,
		3,
		1,
		1,
		10,
		7,
		1,
		10,
		4,
		2,
		7,
		21,
		15,
		7,
		2,
		5,
		1,
		8,
		3,
		4,
		1,
		3,
		1,
		6,
		1,
		4,
		2,
		1,
		4,
		10,
		8,
		1,
		4,
		5,
		1,
		5,
		10,
		2,
		7,
		1,
		10,
		1,
		1,
		3,
		4,
		11,
		10,
		29,
		4,
		7,
		3,
		5,
		2,
		3,
		33,
		5,
		2,
		19,
		3,
		1,
		4,
		2,
		6,
		31,
		11,
		1,
		3,
		3,
		3,
		1,
		8,
		10,
		9,
		12,
		11,
		12,
		8,
		3,
		14,
		8,
		6,
		11,
		1,
		4,
		41,
		3,
		1,
		2,
		7,
		13,
		1,
		5,
		6,
		2,
		6,
		12,
		12,
		22,
		5,
		9,
		4,
		8,
		9,
		9,
		34,
		6,
		24,
		1,
		1,
		20,
		9,
		9,
		3,
		4,
		1,
		7,
		2,
		2,
		2,
		6,
		2,
		28,
		5,
		3,
		6,
		1,
		4,
		6,
		7,
		4,
		2,
		1,
		4,
		2,
		13,
		6,
		4,
		4,
		3,
		1,
		8,
		8,
		3,
		2,
		1,
		5,
		1,
		2,
		2,
		3,
		1,
		11,
		11,
		7,
		3,
		6,
		10,
		8,
		6,
		16,
		16,
		22,
		7,
		12,
		6,
		21,
		5,
		4,
		6,
		6,
		3,
		6,
		1,
		3,
		2,
		1,
		2,
		8,
		29,
		1,
		10,
		1,
		6,
		13,
		6,
		6,
		19,
		31,
		1,
		13,
		4,
		4,
		22,
		17,
		26,
		33,
		10,
		4,
		15,
		12,
		25,
		6,
		67,
		10,
		2,
		3,
		1,
		6,
		10,
		2,
		6,
		2,
		9,
		1,
		9,
		4,
		4,
		1,
		2,
		16,
		2,
		5,
		9,
		2,
		3,
		8,
		1,
		8,
		3,
		9,
		4,
		8,
		6,
		4,
		8,
		11,
		3,
		2,
		1,
		1,
		3,
		26,
		1,
		7,
		5,
		1,
		11,
		1,
		5,
		3,
		5,
		2,
		13,
		6,
		39,
		5,
		1,
		5,
		2,
		11,
		6,
		10,
		5,
		1,
		15,
		5,
		3,
		6,
		19,
		21,
		22,
		2,
		4,
		1,
		6,
		1,
		8,
		1,
		4,
		8,
		2,
		4,
		2,
		2,
		9,
		2,
		1,
		1,
		1,
		4,
		3,
		6,
		3,
		12,
		7,
		1,
		14,
		2,
		4,
		10,
		2,
		13,
		1,
		17,
		7,
		3,
		2,
		1,
		3,
		2,
		13,
		7,
		14,
		12,
		3,
		1,
		29,
		2,
		8,
		9,
		15,
		14,
		9,
		14,
		1,
		3,
		1,
		6,
		5,
		9,
		11,
		3,
		38,
		43,
		20,
		7,
		7,
		8,
		5,
		15,
		12,
		19,
		15,
		81,
		8,
		7,
		1,
		5,
		73,
		13,
		37,
		28,
		8,
		8,
		1,
		15,
		18,
		20,
		165,
		28,
		1,
		6,
		11,
		8,
		4,
		14,
		7,
		15,
		1,
		3,
		3,
		6,
		4,
		1,
		7,
		14,
		1,
		1,
		11,
		30,
		1,
		5,
		1,
		4,
		14,
		1,
		4,
		2,
		7,
		52,
		2,
		6,
		29,
		3,
		1,
		9,
		1,
		21,
		3,
		5,
		1,
		26,
		3,
		11,
		14,
		11,
		1,
		17,
		5,
		1,
		2,
		1,
		3,
		2,
		8,
		1,
		2,
		9,
		12,
		1,
		1,
		2,
		3,
		8,
		3,
		24,
		12,
		7,
		7,
		5,
		17,
		3,
		3,
		3,
		1,
		23,
		10,
		4,
		4,
		6,
		3,
		1,
		16,
		17,
		22,
		3,
		10,
		21,
		16,
		16,
		6,
		4,
		10,
		2,
		1,
		1,
		2,
		8,
		8,
		6,
		5,
		3,
		3,
		3,
		39,
		25,
		15,
		1,
		1,
		16,
		6,
		7,
		25,
		15,
		6,
		6,
		12,
		1,
		22,
		13,
		1,
		4,
		9,
		5,
		12,
		2,
		9,
		1,
		12,
		28,
		8,
		3,
		5,
		10,
		22,
		60,
		1,
		2,
		40,
		4,
		61,
		63,
		4,
		1,
		13,
		12,
		1,
		4,
		31,
		12,
		1,
		14,
		89,
		5,
		16,
		6,
		29,
		14,
		2,
		5,
		49,
		18,
		18,
		5,
		29,
		33,
		47,
		1,
		17,
		1,
		19,
		12,
		2,
		9,
		7,
		39,
		12,
		3,
		7,
		12,
		39,
		3,
		1,
		46,
		4,
		12,
		3,
		8,
		9,
		5,
		31,
		15,
		18,
		3,
		2,
		2,
		66,
		19,
		13,
		17,
		5,
		3,
		46,
		124,
		13,
		57,
		34,
		2,
		5,
		4,
		5,
		8,
		1,
		1,
		1,
		4,
		3,
		1,
		17,
		5,
		3,
		5,
		3,
		1,
		8,
		5,
		6,
		3,
		27,
		3,
		26,
		7,
		12,
		7,
		2,
		17,
		3,
		7,
		18,
		78,
		16,
		4,
		36,
		1,
		2,
		1,
		6,
		2,
		1,
		39,
		17,
		7,
		4,
		13,
		4,
		4,
		4,
		1,
		10,
		4,
		2,
		4,
		6,
		3,
		10,
		1,
		19,
		1,
		26,
		2,
		4,
		33,
		2,
		73,
		47,
		7,
		3,
		8,
		2,
		4,
		15,
		18,
		1,
		29,
		2,
		41,
		14,
		1,
		21,
		16,
		41,
		7,
		39,
		25,
		13,
		44,
		2,
		2,
		10,
		1,
		13,
		7,
		1,
		7,
		3,
		5,
		20,
		4,
		8,
		2,
		49,
		1,
		10,
		6,
		1,
		6,
		7,
		10,
		7,
		11,
		16,
		3,
		12,
		20,
		4,
		10,
		3,
		1,
		2,
		11,
		2,
		28,
		9,
		2,
		4,
		7,
		2,
		15,
		1,
		27,
		1,
		28,
		17,
		4,
		5,
		10,
		7,
		3,
		24,
		10,
		11,
		6,
		26,
		3,
		2,
		7,
		2,
		2,
		49,
		16,
		10,
		16,
		15,
		4,
		5,
		27,
		61,
		30,
		14,
		38,
		22,
		2,
		7,
		5,
		1,
		3,
		12,
		23,
		24,
		17,
		17,
		3,
		3,
		2,
		4,
		1,
		6,
		2,
		7,
		5,
		1,
		1,
		5,
		1,
		1,
		9,
		4,
		1,
		3,
		6,
		1,
		8,
		2,
		8,
		4,
		14,
		3,
		5,
		11,
		4,
		1,
		3,
		32,
		1,
		19,
		4,
		1,
		13,
		11,
		5,
		2,
		1,
		8,
		6,
		8,
		1,
		6,
		5,
		13,
		3,
		23,
		11,
		5,
		3,
		16,
		3,
		9,
		10,
		1,
		24,
		3,
		198,
		52,
		4,
		2,
		2,
		5,
		14,
		5,
		4,
		22,
		5,
		20,
		4,
		11,
		6,
		41,
		1,
		5,
		2,
		2,
		11,
		5,
		2,
		28,
		35,
		8,
		22,
		3,
		18,
		3,
		10,
		7,
		5,
		3,
		4,
		1,
		5,
		3,
		8,
		9,
		3,
		6,
		2,
		16,
		22,
		4,
		5,
		5,
		3,
		3,
		18,
		23,
		2,
		6,
		23,
		5,
		27,
		8,
		1,
		33,
		2,
		12,
		43,
		16,
		5,
		2,
		3,
		6,
		1,
		20,
		4,
		2,
		9,
		7,
		1,
		11,
		2,
		10,
		3,
		14,
		31,
		9,
		3,
		25,
		18,
		20,
		2,
		5,
		5,
		26,
		14,
		1,
		11,
		17,
		12,
		40,
		19,
		9,
		6,
		31,
		83,
		2,
		7,
		9,
		19,
		78,
		12,
		14,
		21,
		76,
		12,
		113,
		79,
		34,
		4,
		1,
		1,
		61,
		18,
		85,
		10,
		2,
		2,
		13,
		31,
		11,
		50,
		6,
		33,
		159,
		179,
		6,
		6,
		7,
		4,
		4,
		2,
		4,
		2,
		5,
		8,
		7,
		20,
		32,
		22,
		1,
		3,
		10,
		6,
		7,
		28,
		5,
		10,
		9,
		2,
		77,
		19,
		13,
		2,
		5,
		1,
		4,
		4,
		7,
		4,
		13,
		3,
		9,
		31,
		17,
		3,
		26,
		2,
		6,
		6,
		5,
		4,
		1,
		7,
		11,
		3,
		4,
		2,
		1,
		6,
		2,
		20,
		4,
		1,
		9,
		2,
		6,
		3,
		7,
		1,
		1,
		1,
		20,
		2,
		3,
		1,
		6,
		2,
		3,
		6,
		2,
		4,
		8,
		1,
		5,
		13,
		8,
		4,
		11,
		23,
		1,
		10,
		6,
		2,
		1,
		3,
		21,
		2,
		2,
		4,
		24,
		31,
		4,
		10,
		10,
		2,
		5,
		192,
		15,
		4,
		16,
		7,
		9,
		51,
		1,
		2,
		1,
		1,
		5,
		1,
		1,
		2,
		1,
		3,
		5,
		3,
		1,
		3,
		4,
		1,
		3,
		1,
		3,
		3,
		9,
		8,
		1,
		2,
		2,
		2,
		4,
		4,
		18,
		12,
		92,
		2,
		10,
		4,
		3,
		14,
		5,
		25,
		16,
		42,
		4,
		14,
		4,
		2,
		21,
		5,
		126,
		30,
		31,
		2,
		1,
		5,
		13,
		3,
		22,
		5,
		6,
		6,
		20,
		12,
		1,
		14,
		12,
		87,
		3,
		19,
		1,
		8,
		2,
		9,
		9,
		3,
		3,
		23,
		2,
		3,
		7,
		6,
		3,
		1,
		2,
		3,
		9,
		1,
		3,
		1,
		6,
		3,
		2,
		1,
		3,
		11,
		3,
		1,
		6,
		10,
		3,
		2,
		3,
		1,
		2,
		1,
		5,
		1,
		1,
		11,
		3,
		6,
		4,
		1,
		7,
		2,
		1,
		2,
		5,
		5,
		34,
		4,
		14,
		18,
		4,
		19,
		7,
		5,
		8,
		2,
		6,
		79,
		1,
		5,
		2,
		14,
		8,
		2,
		9,
		2,
		1,
		36,
		28,
		16,
		4,
		1,
		1,
		1,
		2,
		12,
		6,
		42,
		39,
		16,
		23,
		7,
		15,
		15,
		3,
		2,
		12,
		7,
		21,
		64,
		6,
		9,
		28,
		8,
		12,
		3,
		3,
		41,
		59,
		24,
		51,
		55,
		57,
		294,
		9,
		9,
		2,
		6,
		2,
		15,
		1,
		2,
		13,
		38,
		90,
		9,
		9,
		9,
		3,
		11,
		7,
		1,
		1,
		1,
		5,
		6,
		3,
		2,
		1,
		2,
		2,
		3,
		8,
		1,
		4,
		4,
		1,
		5,
		7,
		1,
		4,
		3,
		20,
		4,
		9,
		1,
		1,
		1,
		5,
		5,
		17,
		1,
		5,
		2,
		6,
		2,
		4,
		1,
		4,
		5,
		7,
		3,
		18,
		11,
		11,
		32,
		7,
		5,
		4,
		7,
		11,
		127,
		8,
		4,
		3,
		3,
		1,
		10,
		1,
		1,
		6,
		21,
		14,
		1,
		16,
		1,
		7,
		1,
		3,
		6,
		9,
		65,
		51,
		4,
		3,
		13,
		3,
		10,
		1,
		1,
		12,
		9,
		21,
		110,
		3,
		19,
		24,
		1,
		1,
		10,
		62,
		4,
		1,
		29,
		42,
		78,
		28,
		20,
		18,
		82,
		6,
		3,
		15,
		6,
		84,
		58,
		253,
		15,
		155,
		264,
		15,
		21,
		9,
		14,
		7,
		58,
		40,
		39,
	};
	static font_wchar base_ranges[ ] = // not zero-terminated
	{
		0x0020,
		0x00FF,
		// Basic Latin + Latin Supplement
		0x3000,
		0x30FF,
		// Punctuations, Hiragana, Katakana
		0x31F0,
		0x31FF,
		// Katakana Phonetic Extensions
		0xFF00,
		0xFFEF,
		// Half-width characters
	};
	static font_wchar full_ranges[ ARRAY_SIZE(base_ranges) + ARRAY_SIZE(accumulative_offsets_from_0x4E00) * 2 + 1 ] = {
		0
	};
	if (!full_ranges[0])
	{
		memcpy(full_ranges, base_ranges, sizeof(base_ranges));
		UnpackAccumulativeOffsetsIntoRanges(0x4E00,
		                                    accumulative_offsets_from_0x4E00,
		                                    ARRAY_SIZE(accumulative_offsets_from_0x4E00),
		                                    full_ranges + ARRAY_SIZE(base_ranges));
	}
	return &full_ranges[0];
}


#pragma endregion
