#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d9.h>
#include <d3dx9.h>

#include "tex_dict.h"

namespace util::draw
{
	struct d3d_shared_reset_data
	{
		IDirect3DTexture9* font_texture = nullptr;
		IDirect3DStateBlock9* state_block = nullptr;

		IDirect3DVertexShader9* vertex_shader = nullptr;

		IDirect3DPixelShader9* pixel_shader = nullptr;
		IDirect3DPixelShader9* key_shader = nullptr;

		IDirect3DPixelShader9* scissor_pixel_shader = nullptr;
		IDirect3DPixelShader9* scissor_blur_shader = nullptr;
		IDirect3DPixelShader9* scissor_key_shader = nullptr;

		IDirect3DTexture9* buffer_copy = nullptr;
	};

	struct d3d9_manager : public draw_manager
	{
		explicit d3d9_manager(IDirect3DDevice9* device);

		void draw() override;

		void pre_reset() const {};

		void post_reset() const {};

		bool create_device_objects();
		bool invalidate_device_objects();

		auto device_ptr() const { return _device_ptr; }

		tex_id create_texture(uint32_t width, uint32_t height) override;
		bool set_texture_rgba(tex_id id, const uint8_t* rgba, uint32_t width,
			uint32_t height) override;
		bool set_texture_rabg(tex_id id, const uint8_t* rabg, uint32_t width,
			uint32_t height) override;
		bool texture_size(tex_id id, uint32_t& width, uint32_t& height) override;
		bool delete_texture(tex_id id) override;

		void update_screen_size(const position& screen_size) override;

	protected:
		bool create_font_texture();
		bool setup_draw_state();
		void destroy_draw_state();
		void setup_shader();
		void invalidate_shader();

	protected:
		IDirect3DDevice9* _device_ptr = nullptr;
		static d3d9_tex_dict _tex_dict;
		static d3d_shared_reset_data _r;
		IDirect3DVertexBuffer9* _vtx_buffer = nullptr;
		IDirect3DIndexBuffer9* _idx_buffer = nullptr;

		D3DXMATRIX _identity;
		D3DXMATRIX _projection;
		D3DXMATRIX _wvp;

		struct
		{
			D3DMATRIX world{}, view{}, projection{};
			IDirect3DVertexDeclaration9* vert_declaration = nullptr;
			IDirect3DVertexShader9* vert_shader{};
			IDirect3DPixelShader9* pixel_shader{};
			IDirect3DBaseTexture9* texture{};
			unsigned long fvf{};
			D3DVIEWPORT9 vp{};
		} _bak;

		size_t _vtx_buf_size = 0, _idx_buf_size = 0;

		unsigned long _color_write_enable = 0ul;
		unsigned long _sampler_u{}, _sampler_v{}, _sampler_w{}, _sampler_srgb{};
	};
}  // namespace util::draw
