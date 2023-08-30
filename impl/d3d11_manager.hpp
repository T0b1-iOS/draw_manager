#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <wrl/client.h>

#include "tex_dict_dx11.hpp"
#include "../draw_manager.hpp"

namespace util::draw {
	using Microsoft::WRL::ComPtr;

	struct d3d11_manager final : draw_manager {
		explicit d3d11_manager(ID3D11Device*, ID3D11DeviceContext*);

		void draw() override;

		void pre_reset() const {};

		void post_reset() const {};

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
		bool setup_render_data();
		void destroy_render_data();

	protected:
		ID3D11Device* _device_ptr = nullptr;
		ID3D11DeviceContext* _ctx = nullptr;
		static tex_dict_dx11 _tex_dict;
		tex_wrapper_dx11* _white_tex = nullptr;

		float _wvp[16];

		struct {
			ComPtr<ID3D11Buffer> vtx_buf;
			ComPtr<ID3D11Buffer> idx_buf;
			ComPtr<IDXGIFactory> factory;
			ComPtr<ID3D11ShaderResourceView> font_tex;
			ComPtr<ID3D11SamplerState> font_sampler;
			ComPtr<ID3D11InputLayout> input_layout;
			ComPtr<ID3D11VertexShader> vtx_shader;
			ComPtr<ID3D11Buffer> vtx_const_buf;
			ComPtr<ID3D11PixelShader> pix_shader;
			ComPtr<ID3D11Buffer> pix_size_buf, pix_scissor_buf, pix_blur_buf;
			ComPtr<ID3D11PixelShader> key_shader;
			ComPtr<ID3D11PixelShader> scissor_pixel_shader;
			ComPtr<ID3D11PixelShader> scissor_blur_x_shader;
			ComPtr<ID3D11PixelShader> scissor_blur_y_shader;
			ComPtr<ID3D11PixelShader> scissor_key_shader;
			ComPtr<ID3D11PixelShader> blur_x_pixel_shader;
			ComPtr<ID3D11PixelShader> blur_y_pixel_shader;
			ComPtr<ID3D11ShaderResourceView> buffer_copy;
			float old_width = 0, old_height = 0;
			ComPtr<ID3D11SamplerState> buffer_copy_sampler;
			ComPtr<ID3D11RasterizerState> rs;
			ComPtr<ID3D11BlendState> bs;
			ComPtr<ID3D11DepthStencilState> dss;
			size_t _vtx_buf_size = 0, _idx_buf_size = 0;
			bool valid = false;
		} _dat{};

		struct {
			uint32_t vp_count, scissor_count;
			D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
			D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
			ID3D11RasterizerState* rs;
			ID3D11BlendState* bs;
			float blend_factor[4];
			uint32_t sample_mask;
			uint32_t stencil_ref;
			ID3D11DepthStencilState* stencil_state;
			ID3D11ShaderResourceView* shader_res[2];
			ID3D11SamplerState* ps_ss[2];
			ID3D11PixelShader* ps;
			ID3D11VertexShader* vs;
			ID3D11GeometryShader* gs;
			uint32_t ps_inst_count, vs_inst_count, gs_inst_count;
			ID3D11ClassInstance* ps_insts[256], * vs_insts[256], * gs_insts[256];
			D3D11_PRIMITIVE_TOPOLOGY prim_top;
			ID3D11Buffer* idx_buf, * vtx_buf, * vs_const_buf;
			ID3D11Buffer* ps_consts_bufs[3];
			uint32_t idx_buf_off, vtx_buf_stride, vtx_buf_off;
			DXGI_FORMAT idx_buf_fmt;
			ID3D11InputLayout* input_layout;
		} _bak{};
	};
}