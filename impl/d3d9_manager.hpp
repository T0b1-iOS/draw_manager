#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d9.h>
#include <d3dx9.h>

#include "tex_dict.h"

namespace util::draw
{
	struct d3d9_manager : public draw_manager
	{
	public:
		explicit d3d9_manager(IDirect3DDevice9 *device)
			: _device_ptr(device)
		{
			init();
		};

		virtual void draw() override;

		bool create_device_objects();
		bool invalidate_device_objects();

		auto device_ptr() const
		{
			return _device_ptr;
		}

		// TODO: Create some sort of dictionary to hold the rgba data for textures to let this handle resets
		tex_id create_texture(uint32_t width, uint32_t height) override;
		bool set_texture_rgba(tex_id id, const uint8_t *rgba, uint32_t width, uint32_t height) override;
		bool set_texture_rabg(tex_id id, const uint8_t *rabg, uint32_t width, uint32_t height) override;
		bool delete_texture(tex_id id) override;

	protected:
		bool create_font_texture();
		bool setup_draw_state();
		void destroy_draw_state();
		void setup_shader();
		void invalidate_shader();

		void save_csgo_state();
		void apply_csgo_state();

	private:
		IDirect3DDevice9 *_device_ptr = nullptr;
		d3d9_tex_dict _tex_dict{};

		struct
		{
			IDirect3DVertexBuffer9 *vtx_buffer = nullptr;
			IDirect3DIndexBuffer9 *idx_buffer  = nullptr;
			IDirect3DTexture9 *font_texture    = nullptr;
			IDirect3DStateBlock9 *state_block  = nullptr;

			IDirect3DPixelShader9 *pixel_shader = nullptr;
			LPD3DXCONSTANTTABLE constant_table  = nullptr;

			IDirect3DPixelShader9 *key_shader      = nullptr;
			LPD3DXCONSTANTTABLE key_constant_table = nullptr;

			IDirect3DVertexShader9 *vertex_shader           = nullptr;
			LPDIRECT3DVERTEXDECLARATION9 vertex_declaration = nullptr;
			LPD3DXCONSTANTTABLE vtx_constant_table          = nullptr;

			IDirect3DTexture9 *buffer_copy = nullptr;
			IDirect3DTexture9 *blur_copy   = nullptr;
		} _r; //Needs to be reset

		struct
		{
			D3DMATRIX world, view, projection;
			IDirect3DVertexDeclaration9 *vert_declaration = nullptr;
			IDirect3DVertexShader9 *vert_shader;
			IDirect3DPixelShader9 *pixel_shader;
			IDirect3DBaseTexture9 *texture;
			unsigned long fvf;
		} _bak;

		size_t _vtx_buf_size = 0, _idx_buf_size = 0;

		unsigned long _color_write_enable = 0ul;
		void *_vertex_declaration         = nullptr, *_vertex_shader = nullptr;
		unsigned long _sampler_u, _sampler_v, _sampler_w, _sampler_srgb;
	};
}
