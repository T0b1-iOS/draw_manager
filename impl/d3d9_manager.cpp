#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftglyph.h>

#include "../draw_manager.hpp"
#include "d3d9_manager.hpp"

#include <d3dx9shader.h>

#include "shaders.hpp"

using namespace util::draw;

constexpr auto D3DFVF_CUSTOM = (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);

struct d3d9_vertex
{
	float pos[ 3 ];
	D3DCOLOR col;
	float uv[ 2 ];
};

// This backups shader constant that are overwritten with the cheats
// to prevent csgo's vertex shaders from breaking (displacements)
template<typename _type, bool pixel_shader = false>
struct shader_constant
{
	shader_constant(IDirect3DDevice9 *device_ptr, LPD3DXCONSTANTTABLE const_table, const char *var_name)
	{
		D3DXCONSTANT_DESC const_desc;
		UINT count;
		const auto const_handle = const_table->GetConstantByName(nullptr, var_name);
		const_table->GetConstantDesc(const_handle, &const_desc, &count);
		_idx    = const_desc.RegisterIndex;
		_device = device_ptr;

		if constexpr (pixel_shader)
			_device->GetPixelShaderConstantF(_idx, _bak.data(), _bak.size() / 4);
		else
			_device->GetVertexShaderConstantF(_idx, _bak.data(), _bak.size() / 4);
	}

	~shader_constant()
	{
		if constexpr (pixel_shader)
			_device->SetPixelShaderConstantF(_idx, _bak.data(), _bak.size() / 4);
		else
			_device->SetVertexShaderConstantF(_idx, _bak.data(), _bak.size() / 4);
	}

	void set(_type *val)
	{
		if constexpr (pixel_shader)
			_device->SetPixelShaderConstantF(_idx, reinterpret_cast<float*>(val), _bak.size() / 4);
		else
			_device->SetVertexShaderConstantF(_idx, reinterpret_cast<float*>(val), _bak.size() / 4);
	}

protected:
	std::array<float, sizeof(_type) / sizeof(float)> _bak;
	uint32_t _idx;
	IDirect3DDevice9 *_device;
};

void d3d9_manager::draw()
{
	std::scoped_lock g(_list_mutex, fonts->tex_mutex);
	fonts->locked  = true;
	auto idx_count = 0u;
	auto vtx_count = 0u;

	save_csgo_state();

	for (const auto &node : _buffer_list)
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

	if (!_r.font_texture || fonts->has_updated)
		create_font_texture();

	if (!_r.vtx_buffer || _vtx_buf_size < vtx_count)
	{
		if (_r.vtx_buffer)
			_r.vtx_buffer->Release();
		_vtx_buf_size = vtx_count + 500;
		if (_device_ptr->CreateVertexBuffer(_vtx_buf_size * sizeof(d3d9_vertex),
		                                    D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
		                                    D3DFVF_CUSTOM,
		                                    D3DPOOL_DEFAULT,
		                                    &_r.vtx_buffer,
		                                    nullptr) != D3D_OK)
			return;
	}
	if (!_r.idx_buffer || _idx_buf_size < idx_count)
	{
		if (_r.idx_buffer)
			_r.idx_buffer->Release();
		_idx_buf_size = idx_count + 1000;
		if (_device_ptr->CreateIndexBuffer(_idx_buf_size * sizeof(draw_buffer::draw_index),
		                                   D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
		                                   D3DFMT_INDEX32,
		                                   D3DPOOL_DEFAULT,
		                                   &_r.idx_buffer,
		                                   nullptr) != D3D_OK)
			return;
	}

	//TODO: This is taken over from ImGui but it can be designed more performant
	d3d9_vertex *vtx_dest;
	draw_buffer::draw_index *idx_dest;
	if (_r.vtx_buffer->Lock(0,
	                        vtx_count * sizeof(d3d9_vertex),
	                        reinterpret_cast<void**>(&vtx_dest),
	                        D3DLOCK_DISCARD) != D3D_OK)
		return;
	if (_r.idx_buffer->Lock(0,
	                        idx_count * sizeof(draw_buffer::draw_index),
	                        reinterpret_cast<void**>(&idx_dest),
	                        D3DLOCK_DISCARD) != D3D_OK)
	{
		_r.vtx_buffer->Unlock();
		return;
	}

	const auto copy_draw_data = [ & ](draw_buffer *buf_ptr)
	{
		auto *vtx_src = buf_ptr->vertices.data();
		for (auto i = 0u; i < buf_ptr->vertices.size(); i++)
		{
			vtx_dest->pos[0] = vtx_src->pos.x;
			vtx_dest->pos[1] = vtx_src->pos.y;
			vtx_dest->pos[2] = 1.f;
			vtx_dest->col    = vtx_src->col.as_argb();
			vtx_dest->uv[0]  = vtx_src->uv.x;
			vtx_dest->uv[1]  = vtx_src->uv.y;
			vtx_dest++;
			vtx_src++;
		}
		std::memcpy(idx_dest, buf_ptr->indices.data(), buf_ptr->indices.size() * sizeof(draw_buffer::draw_index));
		idx_dest += buf_ptr->indices.size();
	};

	const auto copy_child_data = [ = ](const buffer_node::child_array &childs, const auto &self_ref) -> void
	{
		for (const auto child : childs)
		{
			const auto &element = _buffer_list[child.second];
			copy_draw_data(element.active_buffer.get());
			if (!element.child_buffers.empty())
				self_ref(element.child_buffers, self_ref);
		}
	};

	for (const auto prio_idx : _priorities)
	{
		const auto &node = _buffer_list[prio_idx.second];

		copy_draw_data(node.active_buffer.get());
		copy_child_data(node.child_buffers, copy_child_data);
	}

	_r.idx_buffer->Unlock();
	_r.vtx_buffer->Unlock();

	if (!setup_draw_state())
	{
		fonts->locked = false;
		return;
	}

	setup_shader();

	//Render
	std::uint32_t vtx_offset = 0;
	std::uint32_t idx_offset = 0;

	if (_r.pixel_shader)
	{
		_device_ptr->SetPixelShader(_r.pixel_shader);
	}
	if (_r.vertex_shader)
	{
		_device_ptr->SetVertexDeclaration(_r.vertex_declaration);
		_device_ptr->SetVertexShader(_r.vertex_shader);
	}

	D3DXMATRIX worldViewProjection;
	D3DXMATRIX tmpMatrix;
	_device_ptr->GetTransform(D3DTS_WORLD, &worldViewProjection);
	_device_ptr->GetTransform(D3DTS_VIEW, &tmpMatrix);
	worldViewProjection *= tmpMatrix;
	_device_ptr->GetTransform(D3DTS_PROJECTION, &tmpMatrix);
	worldViewProjection *= tmpMatrix;

	shader_constant<D3DXMATRIX> const_vtx_matrix{_device_ptr, _r.vtx_constant_table, "worldViewProj"};
	const_vtx_matrix.set(&worldViewProjection);

	const auto backbuffer_handle = _r.constant_table->GetConstantByName(nullptr, "backbuffer");

	D3DXVECTOR4 size_vec = {_screen_size.x, _screen_size.y, 0.f, 0.f};
	shader_constant<D3DXVECTOR4, true> const_size_vec{_device_ptr, _r.constant_table, "dimension"};
	const_size_vec.set(&size_vec);

	IDirect3DSurface9 *back_buffer;
	_device_ptr->GetRenderTarget(0, &back_buffer);

	IDirect3DSurface9 *target_surface;
	IDirect3DSurface9 *blur_surface;
	if (!_r.buffer_copy)
	{
		D3DSURFACE_DESC buffer_desc;
		back_buffer->GetDesc(&buffer_desc);
		_device_ptr->CreateTexture(buffer_desc.Width,
		                           buffer_desc.Height,
		                           0,
		                           D3DUSAGE_RENDERTARGET,
		                           D3DFMT_A8R8G8B8,
		                           D3DPOOL_DEFAULT,
		                           &_r.buffer_copy,
		                           nullptr);
	}

	if (!_r.blur_copy)
	{
		D3DSURFACE_DESC buffer_desc;
		back_buffer->GetDesc(&buffer_desc);
		_device_ptr->CreateTexture(buffer_desc.Width,
		                           buffer_desc.Height,
		                           0,
		                           D3DUSAGE_RENDERTARGET,
		                           D3DFMT_A8R8G8B8,
		                           D3DPOOL_DEFAULT,
		                           &_r.blur_copy,
		                           nullptr);
	}

	_r.buffer_copy->GetSurfaceLevel(0, &target_surface);
	_r.blur_copy->GetSurfaceLevel(0, &blur_surface);
	_device_ptr->StretchRect(back_buffer, nullptr, target_surface, nullptr, D3DTEXF_NONE);

	_device_ptr->SetRenderTarget(0, target_surface);

	uint32_t texture_stage = 0;

	D3DXCONSTANT_DESC const_desc;
	UINT count;
	_r.constant_table->GetConstantDesc(backbuffer_handle, &const_desc, &count);

	if (const_desc.RegisterSet == D3DXRS_SAMPLER)
		_device_ptr->SetTexture(const_desc.RegisterIndex, _r.buffer_copy);

	shader_constant<D3DXVECTOR4, true> constant_key_color{_device_ptr, _r.key_constant_table, "key"};

	_device_ptr->SetPixelShader(nullptr);
	_device_ptr->SetVertexShader(nullptr);

	const auto font_tex  = fonts->tex_id;
	const auto draw_cmds = [ & ](draw_buffer *buf_ptr)
	{
		for (const auto &cmd : buf_ptr->cmds)
		{
			if (cmd.callback)
			{
				cmd.callback(&cmd);
			}
			else if (cmd.elem_count > 0)
			{
				const RECT clip = {cmd.clip_rect.x, cmd.clip_rect.y, cmd.clip_rect.z, cmd.clip_rect.w};
				auto tex_id     = cmd.tex_id;
				if (cmd.font_texture)
					tex_id = font_tex;
				_device_ptr->SetScissorRect(&clip);
				_device_ptr->SetTexture(texture_stage, reinterpret_cast<IDirect3DTexture9*>(tex_id));
				_device_ptr->SetTransform(
					D3DTS_WORLD,
					reinterpret_cast<const D3DMATRIX*>(cmd.matrix.matrix.data()));
				if (cmd.blur_strength)
				{
					_device_ptr->SetVertexShader(_r.vertex_shader);
					_device_ptr->SetPixelShader(_r.pixel_shader);
					_device_ptr->SetRenderTarget(0, blur_surface);
					_device_ptr->StretchRect(target_surface, nullptr, blur_surface, nullptr, D3DTEXF_NONE);
					for (auto i = 0; i < cmd.blur_strength; ++i)
					{
						_device_ptr->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,
						                                  vtx_offset,
						                                  0,
						                                  buf_ptr->vertices.size(),
						                                  idx_offset,
						                                  cmd.elem_count / 3);
						_device_ptr->StretchRect(blur_surface, nullptr, target_surface, nullptr, D3DTEXF_NONE);
					}
					_device_ptr->SetRenderTarget(0, target_surface);
					_device_ptr->SetPixelShader(nullptr);
					_device_ptr->SetVertexShader(nullptr);
				}
				else
				{
					if (cmd.key_color._a != 0)
					{
						_device_ptr->SetVertexShader(_r.vertex_shader);
						D3DXVECTOR4 vec = {
							cmd.key_color._r / 255.f,
							cmd.key_color._g / 255.f,
							cmd.key_color._b / 255.f,
							0.f
						};
						constant_key_color.set(&vec);
						_device_ptr->SetPixelShader(_r.key_shader);
					}

					_device_ptr->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,
					                                  vtx_offset,
					                                  0,
					                                  buf_ptr->vertices.size(),
					                                  idx_offset,
					                                  cmd.elem_count / 3);

					if (cmd.key_color._a != 0)
					{
						_device_ptr->SetVertexShader(nullptr);
						_device_ptr->SetPixelShader(nullptr);
					}
				}
				idx_offset += cmd.elem_count;
			}
		}
		vtx_offset += buf_ptr->vertices.size();
	};

	const auto draw_child_cmds = [ = ](const buffer_node::child_array &childs, const auto &self_ref) -> void
	{
		for (const auto child : childs)
		{
			const auto &element = _buffer_list[child.second];
			draw_cmds(element.active_buffer.get());
			if (!element.child_buffers.empty())
				self_ref(element.child_buffers, self_ref);
		}
	};

	for (const auto prio_idx : _priorities)
	{
		const auto &node = _buffer_list[prio_idx.second];

		draw_cmds(node.active_buffer.get());
		draw_child_cmds(node.child_buffers, draw_child_cmds);
	}

	_device_ptr->SetRenderTarget(0, back_buffer);

	_device_ptr->StretchRect(target_surface, nullptr, back_buffer, nullptr, D3DTEXF_NONE);
	blur_surface->Release();
	target_surface->Release();
	back_buffer->Release();

	destroy_draw_state();
	fonts->locked = false;
	apply_csgo_state();
}

void d3d9_manager::setup_shader()
{
	if (!_r.pixel_shader)
	{
		LPD3DXBUFFER code;
		LPD3DXBUFFER buffer_errors = nullptr;
		auto res                   = D3DXCompileShader(shaders::pixel_shader,
		                                               strlen(shaders::pixel_shader),
		                                               nullptr,
		                                               nullptr,
		                                               "main",
		                                               "ps_3_0",
		                                               0,
		                                               &code,
		                                               &buffer_errors,
		                                               &_r.constant_table);
		if (res == D3D_OK)
		{
			res = _device_ptr->CreatePixelShader((DWORD*)code->GetBufferPointer(), &_r.pixel_shader);
			code->Release();
		}
		else
		{
			LPVOID compile_errors = buffer_errors->GetBufferPointer();
			MessageBoxA(NULL,
			            (const char*)compile_errors,
			            "Pixel Shader Compile Error",
			            MB_OK | MB_ICONEXCLAMATION);
		}
	}

	if (!_r.vertex_shader)
	{
		LPD3DXBUFFER code;
		LPD3DXBUFFER buffer_errors = nullptr;

		D3DVERTEXELEMENT9 declaration[ ] =
		{
			{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
			{0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
			{0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
			D3DDECL_END()
		};
		_device_ptr->CreateVertexDeclaration(declaration, &_r.vertex_declaration);

		auto res = D3DXCompileShader(shaders::vertex_shader,
		                             strlen(shaders::vertex_shader),
		                             nullptr,
		                             nullptr,
		                             "main",
		                             "vs_2_0",
		                             0,
		                             &code,
		                             &buffer_errors,
		                             &_r.vtx_constant_table);
		if (res == D3D_OK)
		{
			res = _device_ptr->CreateVertexShader((DWORD*)code->GetBufferPointer(), &_r.vertex_shader);
			code->Release();
		}
		else
		{
			LPVOID compile_errors = buffer_errors->GetBufferPointer();
			MessageBoxA(NULL,
			            (const char*)compile_errors,
			            "Vertex Shader Compile Error",
			            MB_OK | MB_ICONEXCLAMATION);
		}
	}

	if (!_r.key_shader)
	{
		LPD3DXBUFFER code;
		LPD3DXBUFFER buffer_errors = nullptr;
		auto res                   = D3DXCompileShader(shaders::key_shader,
		                                               strlen(shaders::key_shader),
		                                               nullptr,
		                                               nullptr,
		                                               "main",
		                                               "ps_3_0",
		                                               0,
		                                               &code,
		                                               &buffer_errors,
		                                               &_r.key_constant_table);
		if (res == D3D_OK)
		{
			res = _device_ptr->CreatePixelShader((DWORD*)code->GetBufferPointer(), &_r.key_shader);
			code->Release();
		}
		else
		{
			LPVOID compile_errors = buffer_errors->GetBufferPointer();
			MessageBoxA(NULL,
			            (const char*)compile_errors,
			            "Pixel Shader Compile Error",
			            MB_OK | MB_ICONEXCLAMATION);
		}
	}
}

bool d3d9_manager::setup_draw_state()
{
	_device_ptr->SetRenderState(D3DRS_COLORWRITEENABLE, 0xFFFFFFFF);
	_device_ptr->SetRenderState(D3DRS_SRGBWRITEENABLE, false);
	_device_ptr->SetSamplerState(0ul, D3DSAMP_ADDRESSU, D3DTADDRESS_MIRROR);
	_device_ptr->SetSamplerState(0ul, D3DSAMP_ADDRESSV, D3DTADDRESS_MIRROR);
	_device_ptr->SetSamplerState(0ul, D3DSAMP_ADDRESSW, D3DTADDRESS_MIRROR);
	_device_ptr->SetSamplerState(0ul, D3DSAMP_SRGBTEXTURE, 0ul);

	_device_ptr->GetVertexDeclaration(&_bak.vert_declaration);

	if (_device_ptr->CreateStateBlock(D3DSBT_PIXELSTATE, &_r.state_block) != D3D_OK)
		return false;

	_device_ptr->GetTexture(0, &_bak.texture);
	_device_ptr->GetVertexShader(&_bak.vert_shader);
	_device_ptr->GetPixelShader(&_bak.pixel_shader);
	_device_ptr->SetTexture(0, nullptr);
	_device_ptr->GetFVF(&_bak.fvf);
	_device_ptr->SetFVF(D3DFVF_CUSTOM);
	_device_ptr->SetStreamSource(0, _r.vtx_buffer, 0, sizeof(d3d9_vertex));
	_device_ptr->SetIndices(_r.idx_buffer);


	_device_ptr->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
	_device_ptr->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	_device_ptr->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
	_device_ptr->SetRenderState(D3DRS_LIGHTING, false);
	_device_ptr->SetRenderState(D3DRS_ZENABLE, false);
	_device_ptr->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	_device_ptr->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	_device_ptr->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	_device_ptr->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	_device_ptr->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	_device_ptr->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	_device_ptr->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	_device_ptr->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);

	_device_ptr->SetRenderState(D3DRS_SCISSORTESTENABLE, true);
	_device_ptr->SetRenderState(D3DRS_ALPHABLENDENABLE, true);
	_device_ptr->SetRenderState(D3DRS_ALPHATESTENABLE, false);
	_device_ptr->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	_device_ptr->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	_device_ptr->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

	//Backup Transformation matrices
	_device_ptr->GetTransform(D3DTS_WORLD, &_bak.world);
	_device_ptr->GetTransform(D3DTS_VIEW, &_bak.view);
	_device_ptr->GetTransform(D3DTS_PROJECTION, &_bak.projection);

	{
		float L                = 0.5f;
		float R                = _screen_size.x + 0.5f;
		float T                = 0.5f;
		float B                = _screen_size.y + 0.5f;
		D3DMATRIX mat_identity = {
			{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}
		};
		D3DMATRIX mat_projection =
		{
			2.0f / (R - L),
			0.0f,
			0.0f,
			0.0f,
			0.0f,
			2.0f / (T - B),
			0.0f,
			0.0f,
			0.0f,
			0.0f,
			0.5f,
			0.0f,
			(L + R) / (L - R),
			(T + B) / (B - T),
			0.5f,
			1.0f,
		};
		_device_ptr->SetTransform(D3DTS_WORLD, &mat_identity);
		_device_ptr->SetTransform(D3DTS_VIEW, &mat_identity);
		_device_ptr->SetTransform(D3DTS_PROJECTION, &mat_projection);
	}

	return true;
}

void d3d9_manager::destroy_draw_state()
{
	_device_ptr->SetTransform(D3DTS_WORLD, &_bak.world);
	_device_ptr->SetTransform(D3DTS_VIEW, &_bak.view);
	_device_ptr->SetTransform(D3DTS_PROJECTION, &_bak.projection);

	if (_r.state_block)
	{
		_r.state_block->Apply();
		_r.state_block->Release();
		_r.state_block = nullptr;
	}

	_device_ptr->SetVertexDeclaration(_bak.vert_declaration);
	_device_ptr->SetPixelShader(_bak.pixel_shader);
	_device_ptr->SetVertexShader(_bak.vert_shader);
	_device_ptr->SetTexture(0, _bak.texture);
	_device_ptr->SetFVF(_bak.fvf);
}

void d3d9_manager::save_csgo_state()
{
	const auto device = _device_ptr;
	device->GetRenderState(D3DRS_COLORWRITEENABLE, &_color_write_enable);
	device->GetVertexDeclaration(reinterpret_cast<IDirect3DVertexDeclaration9 **>(&_vertex_declaration));
	device->GetVertexShader(reinterpret_cast<IDirect3DVertexShader9 **>(&_vertex_shader));

	device->GetSamplerState(0ul, D3DSAMP_ADDRESSU, &_sampler_u);
	device->GetSamplerState(0ul, D3DSAMP_ADDRESSV, &_sampler_v);
	device->GetSamplerState(0ul, D3DSAMP_ADDRESSW, &_sampler_w);
	device->GetSamplerState(0ul, D3DSAMP_SRGBTEXTURE, &_sampler_srgb);
}

void d3d9_manager::apply_csgo_state()
{
	const auto device = _device_ptr;
	device->SetSamplerState(0ul, D3DSAMP_SRGBTEXTURE, _sampler_srgb);
	device->SetSamplerState(0ul, D3DSAMP_ADDRESSW, _sampler_w);
	device->SetSamplerState(0ul, D3DSAMP_ADDRESSV, _sampler_v);
	device->SetSamplerState(0ul, D3DSAMP_ADDRESSU, _sampler_u);

	device->SetRenderState(D3DRS_COLORWRITEENABLE, _color_write_enable);
	device->SetRenderState(D3DRS_SRGBWRITEENABLE, true);
	device->SetVertexDeclaration(reinterpret_cast<IDirect3DVertexDeclaration9 *>(_vertex_declaration));
	device->SetVertexShader(reinterpret_cast<IDirect3DVertexShader9 *>(_vertex_shader));
}


bool d3d9_manager::create_font_texture()
{
	uint8_t *pixels;
	uint32_t width, height, bytes_per_pixel;
	fonts->tex_data_as_rgba_32(&pixels, &width, &height, &bytes_per_pixel);

	if (pixels == nullptr)
		return true;

	if (_r.font_texture)
		_r.font_texture->Release();

	_r.font_texture = nullptr;
	if (_device_ptr->CreateTexture(width,
	                               height,
	                               1,
	                               D3DUSAGE_DYNAMIC,
	                               D3DFMT_A8R8G8B8,
	                               D3DPOOL_DEFAULT,
	                               &_r.font_texture,
	                               nullptr) != D3D_OK)
		return false;

	D3DLOCKED_RECT locked_rect;
	if (_r.font_texture->LockRect(0, &locked_rect, nullptr, 0) != D3D_OK)
		return false;
	for (auto i = 0u; i < height; i++)
		std::memcpy(reinterpret_cast<unsigned char*>(locked_rect.pBits) + locked_rect.Pitch * i,
		            pixels + (width * bytes_per_pixel) * i,
		            (width * bytes_per_pixel));

	_r.font_texture->UnlockRect(0);

	fonts->tex_id      = _r.font_texture;
	fonts->has_updated = false;

	return true;
}

bool d3d9_manager::create_device_objects()
{
	if (!_device_ptr)
		return false;
	if (!create_font_texture())
		return false;

	return true;
}

void d3d9_manager::invalidate_shader()
{
	if (_r.pixel_shader)
	{
		_r.pixel_shader->Release();
		_r.pixel_shader = nullptr;
	}
	if (_r.constant_table)
	{
		_r.constant_table->Release();
		_r.constant_table = nullptr;
	}
	if (_r.vertex_shader)
	{
		_r.vertex_shader->Release();
		_r.vertex_shader = nullptr;
	}
	if (_r.vertex_declaration)
	{
		_r.vertex_declaration->Release();
		_r.vertex_declaration = nullptr;
	}
	if (_r.vtx_constant_table)
	{
		_r.vtx_constant_table->Release();
		_r.vtx_constant_table = nullptr;
	}
	if (_r.key_shader)
	{
		_r.key_shader->Release();
		_r.key_shader = nullptr;
	}
	if (_r.key_constant_table)
	{
		_r.key_constant_table->Release();
		_r.key_constant_table = nullptr;
	}
}


bool d3d9_manager::invalidate_device_objects()
{
	if (!_device_ptr)
		return false;
	if (_r.vtx_buffer)
	{
		_r.vtx_buffer->Release();
		_r.vtx_buffer = nullptr;
	}
	if (_r.idx_buffer)
	{
		_r.idx_buffer->Release();
		_r.idx_buffer = nullptr;
	}
	if (_r.font_texture)
		_r.font_texture->Release();
	if (_r.state_block)
	{
		_r.state_block->Release();
		_r.state_block = nullptr;
	}
	//invalidate_shader( );

	if (_r.buffer_copy)
	{
		_r.buffer_copy->Release();
		_r.buffer_copy = nullptr;
	}
	if (_r.blur_copy)
	{
		_r.blur_copy->Release();
		_r.blur_copy = nullptr;
	}

	_r.font_texture = nullptr;
	fonts->tex_id   = nullptr;

	return true;
}

tex_id d3d9_manager::create_texture(const uint32_t width, const uint32_t height)
{
	IDirect3DTexture9 *texture = nullptr;
	const auto res             = _device_ptr->CreateTexture(width,
	                                                        height,
	                                                        1,
	                                                        D3DUSAGE_RENDERTARGET,
	                                                        D3DFMT_A8R8G8B8,
	                                                        D3DPOOL_DEFAULT,
	                                                        &texture,
	                                                        nullptr);
	if (res != D3D_OK)
		return nullptr;

	return reinterpret_cast<tex_id>(texture);
}

// directx seems to be using RABG internally, maybe not, this is weird
void copy_convert(const uint8_t *rgba, uint8_t *out, const size_t size)
{
	auto in  = reinterpret_cast<const uint32_t*>(rgba);
	auto buf = reinterpret_cast<uint32_t*>(out);
	for (auto i = 0u; i < (size / 4); ++i)
	{
		*buf++ = (*in & 0xFF00FF00) | ((*in & 0xFF0000) >> 16) | ((*in++ & 0xFF) << 16);
	}
}

bool d3d9_manager::set_texture_rgba(const tex_id id, const uint8_t *rgba, const uint32_t width, const uint32_t height)
{
	assert(id != reinterpret_cast<tex_id>(0));

	IDirect3DTexture9 *tmp_tex = nullptr;
	auto res                   = _device_ptr->CreateTexture(width,
	                                                        height,
	                                                        1,
	                                                        D3DUSAGE_DYNAMIC,
	                                                        D3DFMT_A8R8G8B8,
	                                                        D3DPOOL_SYSTEMMEM,
	                                                        &tmp_tex,
	                                                        nullptr);
	if (res != D3D_OK)
		return false;

	D3DLOCKED_RECT rect;
	res = tmp_tex->LockRect(0, &rect, nullptr, D3DLOCK_DISCARD);
	if (res != D3D_OK)
	{
		tmp_tex->Release();
		return false;
	}

	copy_convert(rgba,
	             reinterpret_cast<uint8_t*>(rect.pBits),
	             width * height * sizeof(std::remove_pointer_t<decltype(rgba)>) * 4u);

	res = tmp_tex->UnlockRect(0);
	if (res != D3D_OK)
	{
		tmp_tex->Release();
		return false;
	}

	res = _device_ptr->UpdateTexture(tmp_tex, reinterpret_cast<IDirect3DTexture9*>(id));

	tmp_tex->Release();
	return (res == D3D_OK);
}

bool d3d9_manager::set_texture_rabg(const tex_id id, const uint8_t *rabg, const uint32_t width, const uint32_t height)
{
	IDirect3DTexture9 *tmp_tex = nullptr;
	auto res                   = _device_ptr->CreateTexture(width,
	                                                        height,
	                                                        1,
	                                                        D3DUSAGE_DYNAMIC,
	                                                        D3DFMT_A8R8G8B8,
	                                                        D3DPOOL_SYSTEMMEM,
	                                                        &tmp_tex,
	                                                        nullptr);
	if (res != D3D_OK)
		return false;

	D3DLOCKED_RECT rect;
	res = tmp_tex->LockRect(0, &rect, nullptr, D3DLOCK_DISCARD);
	if (res != D3D_OK)
	{
		tmp_tex->Release();
		return false;
	}

	std::copy(rabg,
	          rabg + (width * height * sizeof(std::remove_pointer_t<decltype(rabg)>) * 4u),
	          reinterpret_cast<uint8_t*>(rect.pBits));

	res = tmp_tex->UnlockRect(0);
	if (res != D3D_OK)
	{
		tmp_tex->Release();
		return false;
	}

	res = _device_ptr->UpdateTexture(tmp_tex, reinterpret_cast<IDirect3DTexture9*>(id));

	tmp_tex->Release();
	return (res == D3D_OK);
}


bool d3d9_manager::delete_texture(const tex_id id)
{
	assert(id != reinterpret_cast<tex_id>(0));
	reinterpret_cast<IDirect3DTexture9*>(id)->Release();
	return true;
}
