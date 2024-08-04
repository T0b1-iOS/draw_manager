#include "d3d11_manager.hpp"

#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftglyph.h>
#include <bit>

#include "shaders_dx11.hpp"

using namespace util::draw;

tex_dict_dx11 d3d11_manager::_tex_dict{};

struct d3d11_vertex {
	float pos[3];
	union {
		uint8_t col[4];
		uint32_t col_u32;
	};
	float uv[2];
};

struct pix_size_buf {
	float size[4];
};

struct pix_scissor_buf {
	float circle_def[4];
	float key_color[4];
};

struct pix_blur_buf {
	float sample_vec[4];
	float weights[96];
	float offsets[96];
};

d3d11_manager::d3d11_manager(ID3D11Device* dev, ID3D11DeviceContext* ctx) : _device_ptr(dev), _ctx(ctx) {
	init();
}

static float gauss(float sigma, float x) {
	float tmp = 1.f / (std::sqrt(2 * 3.141592653589793) * sigma);
	float tmp2 = std::exp(-0.5 * ((x * x) / (sigma * sigma)));
	return tmp * tmp2;
}

void d3d11_manager::draw() {
	_tex_dict.process_update_queue(_ctx);
	std::scoped_lock g(_list_mutex, fonts->tex_mutex);
	fonts->locked = true;
	auto idx_count = 0u;
	auto vtx_count = 0u;

	for (const auto& node : _buffer_list)
	{
		const auto vtx_idx_count = node.active_buffer->vtx_idx_count();
		vtx_count += vtx_idx_count.first;
		idx_count += vtx_idx_count.second;
	}

	if (!vtx_count || !idx_count)
	{
		fonts->locked = false;
		return;
	}

	if (!_dat.font_tex || fonts->has_updated)
	{
		create_font_texture();
	}

	if (!_dat.vtx_buf || _dat._vtx_buf_size < vtx_count) {
		_dat._vtx_buf_size = vtx_count + 500;
		auto desc = D3D11_BUFFER_DESC{};
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.ByteWidth = _dat._vtx_buf_size * sizeof(d3d11_vertex);
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (_device_ptr->CreateBuffer(&desc, nullptr, _dat.vtx_buf.ReleaseAndGetAddressOf()) != S_OK) {
			fonts->locked = false;
			return;
		}
	}

	if (!_dat.idx_buf || _dat._idx_buf_size < idx_count) {
		_dat._idx_buf_size = idx_count + 1000;
		auto desc = D3D11_BUFFER_DESC{};
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.ByteWidth = _dat._idx_buf_size * sizeof(draw_buffer::draw_index);
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (_device_ptr->CreateBuffer(&desc, nullptr, _dat.idx_buf.ReleaseAndGetAddressOf()) != S_OK) {
			fonts->locked = false;
			return;
		}
	}

	D3D11_MAPPED_SUBRESOURCE vtx_res, idx_res;
	if (_ctx->Map(_dat.vtx_buf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &vtx_res) != S_OK) {
		fonts->locked = false;
		return;
	}
	if (_ctx->Map(_dat.idx_buf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &idx_res) != S_OK) {
		fonts->locked = false;
		return;
	}
	auto* vtx_dst = reinterpret_cast<d3d11_vertex*>(vtx_res.pData);
	auto* idx_dst = reinterpret_cast<draw_buffer::draw_index*>(idx_res.pData);

	const auto copy_draw_data = [&](draw_buffer* buf_ptr) {
		auto* vtx_src = buf_ptr->vertices.data();
		for (auto i = 0u; i < buf_ptr->vertices.size(); i++)
		{
			vtx_dst->pos[0] = vtx_src->pos.x;
			vtx_dst->pos[1] = vtx_src->pos.y;
			vtx_dst->pos[2] = 1.f;
			vtx_dst->col_u32 = vtx_src->col.as_abgr();
			vtx_dst->uv[0] = vtx_src->uv.x;
			vtx_dst->uv[1] = vtx_src->uv.y;
			vtx_dst++;
			vtx_src++;
		}
		std::memcpy(idx_dst, buf_ptr->indices.data(),
			buf_ptr->indices.size() * sizeof(draw_buffer::draw_index));
		idx_dst += buf_ptr->indices.size();
	};

	const auto copy_child_data = [=](const buffer_node::child_array& childs,
		const auto& self_ref) -> void {
			for (const auto& child : childs)
			{
				const auto& element = _buffer_list[child.second];
				copy_draw_data(element.active_buffer.get());
				if (!element.child_buffers.empty())
					self_ref(element.child_buffers, self_ref);
			}
	};

	if (vtx_count && idx_count)
	{
		for (const auto& prio_idx : _priorities)
		{
			const auto& node = _buffer_list[prio_idx.second];

			copy_draw_data(node.active_buffer.get());
			copy_child_data(node.child_buffers, copy_child_data);
		}
	}

	_ctx->Unmap(_dat.vtx_buf.Get(), 0);
	_ctx->Unmap(_dat.idx_buf.Get(), 0);

	if (!setup_render_data() || !setup_draw_state())
	{
		fonts->locked = false;
		return;
	}

	// TODO
	uint32_t vtx_off = 0;
	uint32_t idx_off = 0;

	{
		D3D11_MAPPED_SUBRESOURCE res;
		if (_ctx->Map(_dat.pix_size_buf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &res) != S_OK) {
			return;
		}

		auto* size_buf = reinterpret_cast<pix_size_buf*>(res.pData);
		*size_buf = pix_size_buf{ { _screen_size.x, _screen_size.y, 0.f, 0.f } };
		_ctx->Unmap(_dat.pix_size_buf.Get(), 0);
	}

	{
		D3D11_MAPPED_SUBRESOURCE res;
		if (_ctx->Map(_dat.vtx_const_buf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &res) != S_OK) {
			return;
		}

		std::memcpy(res.pData, _wvp, sizeof(_wvp));
		_ctx->Unmap(_dat.vtx_const_buf.Get(), 0);
	}

	// make a copy of the render target
	ComPtr<ID3D11RenderTargetView> rt_view;
	_ctx->OMGetRenderTargets(1, rt_view.GetAddressOf(), nullptr);

	ComPtr<ID3D11Resource> rt_res;
	rt_view->GetResource(rt_res.GetAddressOf());

	if (!_dat.buffer_copy || _screen_size.x != _dat.old_width || _screen_size.y != _dat.old_height) {
		// TODO: size check
		ComPtr<ID3D11Texture2D> rt_tex;
		rt_res.As(&rt_tex);

		D3D11_TEXTURE2D_DESC rt_desc;
		rt_tex->GetDesc(&rt_desc);

		_dat.old_width = _screen_size.x;
		_dat.old_height = _screen_size.y;

		auto desc = D3D11_TEXTURE2D_DESC{};
		desc.Width = rt_desc.Width;
		desc.Height = rt_desc.Height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = rt_desc.Format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		
		ComPtr<ID3D11Texture2D> tex;
		if (_device_ptr->CreateTexture2D(&desc, nullptr, tex.GetAddressOf()) != S_OK) {
			return;
		}

		auto srv_desc = D3D11_SHADER_RESOURCE_VIEW_DESC{};
		srv_desc.Format = desc.Format;
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MipLevels = 1;
		if (_device_ptr->CreateShaderResourceView(tex.Get(), &srv_desc, _dat.buffer_copy.ReleaseAndGetAddressOf()) != S_OK) {
			return;
		}

		auto sampler_desc = D3D11_SAMPLER_DESC{};
		sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_MIRROR;
		sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;
		sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
		if (_device_ptr->CreateSamplerState(&sampler_desc, _dat.buffer_copy_sampler.ReleaseAndGetAddressOf()) != S_OK) {
			return;
		}
	}

	ComPtr<ID3D11Resource> copy_res;
	{
		_dat.buffer_copy->GetResource(copy_res.GetAddressOf());
		_ctx->CopyResource(copy_res.Get(), rt_res.Get());
	}

	_ctx->PSSetSamplers(1, 1, _dat.buffer_copy_sampler.GetAddressOf());
	_ctx->PSSetShaderResources(1, 1, _dat.buffer_copy.GetAddressOf());

	const auto font_tex = fonts->tex_id;
	const auto draw_cmds = [&](draw_buffer* buf_ptr) {
		for (const auto& cmd : buf_ptr->cmds)
		{
			if (cmd.callback)
			{
				cmd.callback(&cmd);
			}
			else if (cmd.elem_count > 0)
			{
				RECT clip = { cmd.clip_rect.x, cmd.clip_rect.y, cmd.clip_rect.z,
													 cmd.clip_rect.w };
				pix_scissor_buf scissor_buf{};
				if (cmd.circle_scissor)
				{
					// x,y = center; z = radius*radius; screenSpace
					D3D11_MAPPED_SUBRESOURCE res;
					if (_ctx->Map(_dat.pix_scissor_buf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &res) != S_OK) {
						return;
					}

					auto* buf = reinterpret_cast<pix_scissor_buf*>(res.pData);
					scissor_buf.circle_def[0] =
						static_cast<float>(cmd.clip_rect.x + cmd.clip_rect.z) * 0.5f;
					scissor_buf.circle_def[1] =
						static_cast<float>(cmd.clip_rect.y + cmd.clip_rect.w) * 0.5f;
					scissor_buf.circle_def[2] =
						static_cast<float>(cmd.clip_rect.z - cmd.clip_rect.x) * 0.5f;
					scissor_buf.circle_def[2] *= scissor_buf.circle_def[2];
					std::memcpy(buf, &scissor_buf, sizeof(scissor_buf));
					_ctx->Unmap(_dat.pix_scissor_buf.Get(), 0);

					_ctx->PSSetShader(_dat.scissor_pixel_shader.Get(), nullptr, 0);

					clip = { cmd.circle_outer_clip.x, cmd.circle_outer_clip.y, cmd.circle_outer_clip.z,
													 cmd.circle_outer_clip.w };
				}

				auto tex_id = cmd.tex_id;
				if (cmd.font_texture)
					tex_id = font_tex;
				else if (tex_id && !cmd.native_texture)
					tex_id = _tex_dict.texture(reinterpret_cast<tex_wrapper_dx11*>(tex_id));

				if (!tex_id) {
					tex_id = _tex_dict.texture(_white_tex);
				}

				_ctx->RSSetScissorRects(1, &clip);
				_ctx->PSSetShaderResources(0, 1, reinterpret_cast<ID3D11ShaderResourceView**>(&tex_id));
				
				// TODO: i dont use it so it's unsupported
				/*_device_ptr->SetTransform(
					D3DTS_WORLD,
					reinterpret_cast<const D3DMATRIX*>(cmd.matrix.matrix.data()));*/
				if (cmd.blur_strength)
				{
					// TODO: maybe precompute common values?
					const auto sample_count = std::min(cmd.blur_strength, uint8_t(95 * 2));
					const auto sigma = static_cast<float>(sample_count) / 3.f;
					std::array<float, 191> weights;
					std::array<float, 191> offsets;
					weights[0] = gauss(sigma, 0);
					float sum = weights[0];
					for (int i = 1; i <= sample_count / 2; ++i) {
						// combined computation courtesy of https://www.rastergrid.com/blog/2010/09/efficient-gaussian-blur-with-linear-sampling/
						const auto weight1 = gauss(sigma, i * 2);
						const auto weight2 = gauss(sigma, i * 2 + 1);
						const auto off1 = (float)i * 2;
						const auto off2 = off1 + 1;
						const auto combined_weight = weight1 + weight2;
						weights[i] = combined_weight;
						offsets[i] = ((off1 * weight1) + (off2 * weight2)) / combined_weight;

						sum += combined_weight * 2.f;
					}
					if (sample_count & 1) {
						weights[sample_count] = gauss(sigma, sample_count);
						offsets[sample_count] = sample_count;
						sum += weights[sample_count] * 2;
					}

					float sum_inv = 1.f / sum;
					for (int i = 0; i <= sample_count; ++i) {
						weights[i] *= sum_inv;
					}

					// we effectively half the original sample count
					const auto end_sample_count = (sample_count >> 1) + (sample_count & 1);
					float sample_vec[4] = { end_sample_count, 0, 0, 0 };
					const auto f_count = (end_sample_count + 3) / 4; // round up

					{
						D3D11_MAPPED_SUBRESOURCE sub_res;
						if (_ctx->Map(_dat.pix_blur_buf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &sub_res) != S_OK) {
							return;
						}

						auto* data = reinterpret_cast<pix_blur_buf*>(sub_res.pData);
						std::memcpy(data->sample_vec, sample_vec, sizeof(sample_vec));
						std::memcpy(data->weights, weights.data(), sizeof(float) * end_sample_count);
						std::memcpy(data->offsets, offsets.data(), sizeof(float) * end_sample_count);
					
						_ctx->Unmap(_dat.pix_blur_buf.Get(), 0);
					}

					for (auto i = 0; i < cmd.blur_pass_count; ++i)
					{
						// TODO: maybe cache min/max coords in the cmd so we can say somewhat accurately where we draw?
						_ctx->CopyResource(copy_res.Get(), rt_res.Get());
						_ctx->PSSetShader(cmd.circle_scissor ? _dat.scissor_blur_x_shader.Get() : _dat.blur_x_pixel_shader.Get(), nullptr, 0);
						_ctx->DrawIndexed(cmd.elem_count, idx_off, vtx_off);
						
						_ctx->CopyResource(copy_res.Get(), rt_res.Get());
						_ctx->PSSetShader(cmd.circle_scissor ? _dat.scissor_blur_y_shader.Get() : _dat.blur_y_pixel_shader.Get(), nullptr, 0);
						_ctx->DrawIndexed(cmd.elem_count, idx_off, vtx_off);
					}

					_ctx->PSSetShader(_dat.pix_shader.Get(), nullptr, 0);
				}
				else
				{
					if (cmd.key_color.a() != 0)
					{
						D3D11_MAPPED_SUBRESOURCE res;
						if (_ctx->Map(_dat.pix_scissor_buf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &res) != S_OK) {
							return;
						}

						scissor_buf.key_color[0] = cmd.key_color.r() / 255.f;
						scissor_buf.key_color[1] = cmd.key_color.g() / 255.f;
						scissor_buf.key_color[2] = cmd.key_color.b() / 255.f;
						scissor_buf.key_color[3] = 0.f;

						std::memcpy(res.pData, &scissor_buf, sizeof(scissor_buf));
						_ctx->Unmap(_dat.pix_scissor_buf.Get(), 0);

						_ctx->PSSetShader(cmd.circle_scissor ? _dat.scissor_key_shader.Get() : _dat.key_shader.Get(), nullptr, 0);
					}

					_ctx->DrawIndexed(cmd.elem_count, idx_off, vtx_off);

					if (cmd.key_color.a() != 0)
					{
						_ctx->PSSetShader(_dat.pix_shader.Get(), nullptr, 0);
					}
				}

				if (cmd.circle_scissor)
				{
					_ctx->PSSetShader(_dat.pix_shader.Get(), nullptr, 0);
				}
				idx_off += cmd.elem_count;
			}
		}
		vtx_off += buf_ptr->vertices.size();
	};

	const auto draw_child_cmds = [=](const buffer_node::child_array& childs,
		const auto& self_ref) -> void {
			for (const auto child : childs)
			{
				const auto& element = _buffer_list[child.second];
				draw_cmds(element.active_buffer.get());
				if (!element.child_buffers.empty())
					self_ref(element.child_buffers, self_ref);
			}
	};

	for (const auto prio_idx : _priorities)
	{
		const auto& node = _buffer_list[prio_idx.second];

		draw_cmds(node.active_buffer.get());
		draw_child_cmds(node.child_buffers, draw_child_cmds);
	}

	destroy_draw_state();
	fonts->locked = false;
}

bool d3d11_manager::create_font_texture() {
	uint8_t* pixels;
	uint32_t width, height, bytes_per_pixel;
	fonts->tex_data_as_rgba_32(&pixels, &width, &height, &bytes_per_pixel);

	if (pixels == nullptr)
		return true;

	_dat.font_tex = nullptr;

	{
		auto desc = D3D11_TEXTURE2D_DESC{};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		auto sub_res = D3D11_SUBRESOURCE_DATA{};
		sub_res.pSysMem = pixels;
		sub_res.SysMemPitch = width * 4;
		ComPtr<ID3D11Texture2D> tex;
		if (_device_ptr->CreateTexture2D(&desc, &sub_res, tex.GetAddressOf()) != S_OK) {
			return false;
		}

		auto shader_desc = D3D11_SHADER_RESOURCE_VIEW_DESC{};
		shader_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		shader_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		shader_desc.Texture2D.MipLevels = 1;
		if (_device_ptr->CreateShaderResourceView(tex.Get(), &shader_desc, _dat.font_tex.ReleaseAndGetAddressOf()) != S_OK) {
			return false;
		}
	}

	// sampler
	{
		auto desc = D3D11_SAMPLER_DESC{};
		desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
		if (_device_ptr->CreateSamplerState(&desc, _dat.font_sampler.ReleaseAndGetAddressOf()) != S_OK) {
			return false;
		}
	}

	fonts->tex_id = _dat.font_tex.Get();
	fonts->has_updated = false;
	return true;
}

bool d3d11_manager::setup_render_data() {
	if (_dat.valid) {
		return true;
	}

	// blend state
	{
		auto desc = D3D11_BLEND_DESC{};
		desc.AlphaToCoverageEnable = false;
		desc.RenderTarget[0].BlendEnable = true;
		desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (_device_ptr->CreateBlendState(&desc, _dat.bs.ReleaseAndGetAddressOf()) != S_OK) {
			return false;
		}
	}

	// rasterizer state
	{
		auto desc = D3D11_RASTERIZER_DESC{};
		desc.FillMode = D3D11_FILL_SOLID;
		desc.CullMode = D3D11_CULL_NONE;
		desc.ScissorEnable = true;
		desc.DepthClipEnable = true;
		if (_device_ptr->CreateRasterizerState(&desc, _dat.rs.ReleaseAndGetAddressOf()) != S_OK) {
			return false;
		}
	}

	// depth-stencil state
	{
		auto desc = D3D11_DEPTH_STENCIL_DESC{};
		desc.DepthEnable = false;
		desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		desc.StencilEnable = false;
		desc.FrontFace.StencilFailOp = desc.FrontFace.StencilDepthFailOp = desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		desc.BackFace = desc.FrontFace;
		if (_device_ptr->CreateDepthStencilState(&desc, _dat.dss.ReleaseAndGetAddressOf()) != S_OK) {
			return false;
		}
	}

	// vertex shader
	{
		if (_device_ptr->CreateVertexShader(shaders::vertex::generic, sizeof(shaders::vertex::generic), nullptr, _dat.vtx_shader.ReleaseAndGetAddressOf()) != S_OK) {
			return false;
		}

		D3D11_INPUT_ELEMENT_DESC layout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, (UINT)offsetof(d3d11_vertex, pos), D3D11_INPUT_PER_VERTEX_DATA, 0},
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)offsetof(d3d11_vertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0},
			{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, (UINT)offsetof(d3d11_vertex, col), D3D11_INPUT_PER_VERTEX_DATA, 0},
		};
		if (_device_ptr->CreateInputLayout(layout, 3, shaders::vertex::generic, sizeof(shaders::vertex::generic), _dat.input_layout.ReleaseAndGetAddressOf()) != S_OK) {
			return false;
		}

		// vertex const buf
		auto desc = D3D11_BUFFER_DESC{};
		desc.ByteWidth = sizeof(_wvp);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (_device_ptr->CreateBuffer(&desc, nullptr, _dat.vtx_const_buf.ReleaseAndGetAddressOf()) != S_OK) {
			return false;
		}
	}

	// pixel shader
	{
		if (_device_ptr->CreatePixelShader(shaders::pixel::generic, sizeof(shaders::pixel::generic), nullptr, _dat.pix_shader.ReleaseAndGetAddressOf()) != S_OK) {
			return false;
		}

		if (_device_ptr->CreatePixelShader(shaders::pixel::scissor, sizeof(shaders::pixel::scissor), nullptr, _dat.scissor_pixel_shader.ReleaseAndGetAddressOf()) != S_OK) {
			return false;
		}

		if (_device_ptr->CreatePixelShader(shaders::pixel::blur_x, sizeof(shaders::pixel::blur_x), nullptr, _dat.blur_x_pixel_shader.ReleaseAndGetAddressOf()) != S_OK) {
			return false;
		}

		if (_device_ptr->CreatePixelShader(shaders::pixel::blur_y, sizeof(shaders::pixel::blur_y), nullptr, _dat.blur_y_pixel_shader.ReleaseAndGetAddressOf()) != S_OK) {
			return false;
		}

		if (_device_ptr->CreatePixelShader(shaders::pixel::scissor_blur_x, sizeof(shaders::pixel::scissor_blur_x), nullptr, _dat.scissor_blur_x_shader.ReleaseAndGetAddressOf()) != S_OK) {
			return false;
		}

		if (_device_ptr->CreatePixelShader(shaders::pixel::scissor_blur_y, sizeof(shaders::pixel::scissor_blur_y), nullptr, _dat.scissor_blur_y_shader.ReleaseAndGetAddressOf()) != S_OK) {
			return false;
		}

		if (_device_ptr->CreatePixelShader(shaders::pixel::key, sizeof(shaders::pixel::key), nullptr, _dat.key_shader.ReleaseAndGetAddressOf()) != S_OK) {
			return false;
		}

		if (_device_ptr->CreatePixelShader(shaders::pixel::scissor_key, sizeof(shaders::pixel::scissor_key), nullptr, _dat.scissor_key_shader.ReleaseAndGetAddressOf()) != S_OK) {
			return false;
		}

		// pix consts bufs
		auto desc = D3D11_BUFFER_DESC{};
		desc.ByteWidth = sizeof(pix_size_buf);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (_device_ptr->CreateBuffer(&desc, nullptr, _dat.pix_size_buf.ReleaseAndGetAddressOf()) != S_OK) {
			return false;
		}

		desc.ByteWidth = sizeof(pix_scissor_buf);
		if (_device_ptr->CreateBuffer(&desc, nullptr, _dat.pix_scissor_buf.ReleaseAndGetAddressOf()) != S_OK) {
			return false;
		}

		desc.ByteWidth = sizeof(pix_blur_buf);
		if (_device_ptr->CreateBuffer(&desc, nullptr, _dat.pix_blur_buf.ReleaseAndGetAddressOf()) != S_OK) {
			return false;
		}
	}

	if (!_white_tex) {
		_white_tex = _tex_dict.create_texture(2, 2);
		uint8_t data[] = { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 };
		if (!_white_tex->set_tex_data(&_tex_dict, _device_ptr, data, 2, 2)) {
			return false;
		}
	}

	_dat.valid = true;
	return true;
}

bool d3d11_manager::setup_draw_state() {
	_bak.vp_count = _bak.scissor_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	_ctx->RSGetScissorRects(&_bak.scissor_count, _bak.scissors);
	_ctx->RSGetViewports(&_bak.vp_count, _bak.viewports);
	_ctx->RSGetState(&_bak.rs);
	_ctx->OMGetBlendState(&_bak.bs, _bak.blend_factor, &_bak.sample_mask);
	_ctx->OMGetDepthStencilState(&_bak.stencil_state, &_bak.stencil_ref);
	_ctx->PSGetShaderResources(0, 2, _bak.shader_res);
	_ctx->PSGetSamplers(0, 2, _bak.ps_ss);
	_ctx->PSGetConstantBuffers(0, 3, _bak.ps_consts_bufs);
	_bak.ps_inst_count = _bak.vs_inst_count = _bak.gs_inst_count = 256;
	_ctx->PSGetShader(&_bak.ps, _bak.ps_insts, &_bak.ps_inst_count);
	_ctx->VSGetShader(&_bak.vs, _bak.vs_insts, &_bak.vs_inst_count);
	_ctx->VSGetConstantBuffers(0, 1, &_bak.vs_const_buf);
	_ctx->GSGetShader(&_bak.gs, _bak.gs_insts, &_bak.gs_inst_count);

	_ctx->IAGetPrimitiveTopology(&_bak.prim_top);
	_ctx->IAGetIndexBuffer(&_bak.idx_buf, &_bak.idx_buf_fmt, &_bak.idx_buf_off);
	_ctx->IAGetVertexBuffers(0, 1, &_bak.vtx_buf, &_bak.vtx_buf_stride, &_bak.vtx_buf_off);
	_ctx->IAGetInputLayout(&_bak.input_layout);

	auto vp = D3D11_VIEWPORT{};
	vp.Width = _screen_size.x;
	vp.Height = _screen_size.y;
	vp.MinDepth = 0.f;
	vp.MaxDepth = 1.f;
	_ctx->RSSetViewports(1, &vp);

	uint32_t stride = sizeof(d3d11_vertex);
	uint32_t off = 0;
	_ctx->IASetInputLayout(_dat.input_layout.Get());
	_ctx->IASetVertexBuffers(0, 1, _dat.vtx_buf.GetAddressOf(), &stride, &off);
	_ctx->IASetIndexBuffer(_dat.idx_buf.Get(), sizeof(draw_buffer::draw_index) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
	_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_ctx->VSSetShader(_dat.vtx_shader.Get(), nullptr, 0);
	_ctx->VSSetConstantBuffers(0, 1, _dat.vtx_const_buf.GetAddressOf());
	_ctx->PSSetConstantBuffers(0, 1, _dat.pix_size_buf.GetAddressOf());
	_ctx->PSSetConstantBuffers(1, 1, _dat.pix_scissor_buf.GetAddressOf());
	_ctx->PSSetConstantBuffers(2, 1, _dat.pix_blur_buf.GetAddressOf());
	_ctx->PSSetShader(_dat.pix_shader.Get(), nullptr, 0);
	_ctx->PSSetSamplers(0, 1, _dat.font_sampler.GetAddressOf());
	_ctx->GSSetShader(nullptr, nullptr, 0);
	_ctx->HSSetShader(nullptr, nullptr, 0);
	_ctx->DSSetShader(nullptr, nullptr, 0);
	_ctx->CSSetShader(nullptr, nullptr, 0);

	const float blend_factor[4] = { 0.f, 0.f, 0.f, 0.f };
	_ctx->OMSetBlendState(_dat.bs.Get(), blend_factor, 0xFFFFFFFF);
	_ctx->OMSetDepthStencilState(_dat.dss.Get(), 0);
	_ctx->RSSetState(_dat.rs.Get());
	return true;
}

template<typename type>
static void safe_release(type*& obj)
{
	if (obj)
	{
		obj->Release();
		obj = nullptr;
	}
}

void d3d11_manager::destroy_draw_state() {
	_ctx->RSSetScissorRects(_bak.scissor_count, _bak.scissors);
	_ctx->RSSetViewports(_bak.vp_count, _bak.viewports);
	_ctx->RSSetState(_bak.rs);
	_ctx->OMSetBlendState(_bak.bs, _bak.blend_factor, _bak.sample_mask);
	_ctx->OMSetDepthStencilState(_bak.stencil_state, _bak.stencil_ref);
	_ctx->PSSetShaderResources(0, 2, _bak.shader_res);
	_ctx->PSSetSamplers(0, 2, _bak.ps_ss);
	_ctx->PSSetShader(_bak.ps, _bak.ps_insts, _bak.ps_inst_count);
	_ctx->PSSetConstantBuffers(0, 3, _bak.ps_consts_bufs);
	_ctx->VSSetConstantBuffers(0, 1, &_bak.vs_const_buf);
	_ctx->VSSetShader(_bak.vs, _bak.vs_insts, _bak.vs_inst_count);
	_ctx->GSSetShader(_bak.gs, _bak.gs_insts, _bak.gs_inst_count);
	_ctx->IASetPrimitiveTopology(_bak.prim_top);
	_ctx->IASetIndexBuffer(_bak.idx_buf, _bak.idx_buf_fmt, _bak.idx_buf_off);
	_ctx->IASetVertexBuffers(0, 1, &_bak.vtx_buf, &_bak.vtx_buf_stride, &_bak.vtx_buf_off);
	_ctx->IASetInputLayout(_bak.input_layout);

	safe_release(_bak.rs);
	safe_release(_bak.bs);
	safe_release(_bak.stencil_state);
	safe_release(_bak.shader_res[0]);
	safe_release(_bak.shader_res[1]);
	safe_release(_bak.ps_ss[0]);
	safe_release(_bak.ps_ss[1]);
	safe_release(_bak.ps);
	safe_release(_bak.ps_consts_bufs[0]);
	safe_release(_bak.ps_consts_bufs[1]);
	safe_release(_bak.ps_consts_bufs[2]);
	safe_release(_bak.vs_const_buf);
	for (auto i = 0; i < _bak.ps_inst_count; ++i)
		safe_release(_bak.ps_insts[i]);
	safe_release(_bak.vs);
	for (auto i = 0; i < _bak.vs_inst_count; ++i)
		safe_release(_bak.vs_insts[i]);
	safe_release(_bak.gs);
	for (auto i = 0; i < _bak.gs_inst_count; ++i)
		safe_release(_bak.gs_insts[i]);
	safe_release(_bak.idx_buf);
	safe_release(_bak.vtx_buf);
	safe_release(_bak.input_layout);
}

void d3d11_manager::update_screen_size(const position& screen_size)
{
	_screen_size = screen_size;
	std::memset(&_wvp, 0, sizeof(_wvp));
	_wvp[0] = 2.f / _screen_size.x;
	_wvp[5] = 2.f / -_screen_size.y;
	_wvp[10] = 0.5f;
	_wvp[12] = (_screen_size.x) / -_screen_size.x;
	_wvp[13] = (_screen_size.y) / _screen_size.y;
	_wvp[14] = 0.5f;
	_wvp[15] = 1.f;
}

tex_id d3d11_manager::create_texture(const uint32_t width, const uint32_t height)
{
	return reinterpret_cast<tex_id>(_tex_dict.create_texture(width, height));
}

// I'm doing something wrong so we ghetto-fix it
static void copy_convert(const uint8_t* rgba, uint8_t* out, const size_t size)
{
	auto buf = reinterpret_cast<uint32_t*>(out);
	for (auto i = 0u; i < (size / 4); ++i)
	{
		uint32_t v = rgba[i*4];
		v |= rgba[i*4 + 1] << 8;
		v |= rgba[i*4 + 2] << 16;
		v |= rgba[i*4 + 3] << 24;
		*buf++ = v;
	}
}

bool d3d11_manager::set_texture_rgba(const tex_id id, const uint8_t* rgba,
	const uint32_t width, const uint32_t height)
{
	assert(id != reinterpret_cast<tex_id>(0));

	if (!id)
		return false;

	auto tmp_data = std::vector<uint8_t>{};
	tmp_data.resize(width * height * 4u);

	/*copy_convert(rgba, tmp_data.data(),
		width * height * sizeof(std::remove_pointer_t<decltype(rgba)>)
		* 4u);*/

	return _tex_dict.set_tex_data(_device_ptr,
		reinterpret_cast<tex_wrapper_dx11*>(id),
		rgba, width, height);
}

// this is broken
bool d3d11_manager::set_texture_rabg(const tex_id id, const uint8_t* rabg,
	const uint32_t width, const uint32_t height)
{
	assert(id != reinterpret_cast<tex_id>(0));

	if (!id)
		return false;

	return _tex_dict.set_tex_data(
		_device_ptr, reinterpret_cast<tex_wrapper_dx11*>(id), rabg, width, height);
}

bool d3d11_manager::texture_size(const tex_id id, uint32_t& width,
	uint32_t& height)
{
	assert(id != reinterpret_cast<tex_id>(0));
	if (!id)
		return false;

	return _tex_dict.texture_size(reinterpret_cast<tex_wrapper_dx11*>(id), width,
		height);
}

bool d3d11_manager::delete_texture(const tex_id id)
{
	assert(id != reinterpret_cast<tex_id>(0));
	if (!id)
		return false;

	_tex_dict.destroy_texture(reinterpret_cast<tex_wrapper_dx11*>(id));
	return true;
}
