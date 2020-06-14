#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftglyph.h>

#include "../draw_manager.hpp"
#include "d3d9_manager.hpp"

#include <d3dx9shader.h>

#include "shaders.hpp"

using namespace util::draw;

constexpr auto D3DFVF_CUSTOM = (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);

d3d9_tex_dict d3d9_manager::_tex_dict{};
d3d_shared_reset_data d3d9_manager::_r{};

struct d3d9_vertex
{
	float pos[3];
	D3DCOLOR col;
	float uv[2];
};

d3d9_manager::d3d9_manager(IDirect3DDevice9* device) : _device_ptr(device)
{
	D3DXMatrixIdentity(&_identity);
	init();
};

void d3d9_manager::draw()
{
	//std::lock_guard<std::mutex> g(list_mutex);
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

	if (!_r.font_texture || fonts->has_updated)
	{
		create_font_texture();
	}

	if (!_vtx_buffer || _vtx_buf_size < vtx_count)
	{
		if (_vtx_buffer)
			_vtx_buffer->Release();
		_vtx_buf_size = vtx_count + 500;
		if (_device_ptr->CreateVertexBuffer(_vtx_buf_size * sizeof(d3d9_vertex),
			D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
			D3DFVF_CUSTOM, D3DPOOL_DEFAULT,
			&_vtx_buffer, nullptr)
			!= D3D_OK)
			return;
	}
	if (!_idx_buffer || _idx_buf_size < idx_count)
	{
		if (_idx_buffer)
			_idx_buffer->Release();
		_idx_buf_size = idx_count + 1000;
		if (_device_ptr->CreateIndexBuffer(
			_idx_buf_size * sizeof(draw_buffer::draw_index),
			D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFMT_INDEX32,
			D3DPOOL_DEFAULT, &_idx_buffer, nullptr)
			!= D3D_OK)
			return;
	}

	//TODO: This is taken over from ImGui but it can be designed more performant
	d3d9_vertex* vtx_dest;
	draw_buffer::draw_index* idx_dest;
	if ((vtx_count && idx_count)
		&& _vtx_buffer->Lock(0, vtx_count * sizeof(d3d9_vertex),
			reinterpret_cast<void**>(&vtx_dest),
			D3DLOCK_DISCARD)
		!= D3D_OK)
		return;
	if ((vtx_count && idx_count)
		&& _idx_buffer->Lock(0, idx_count * sizeof(draw_buffer::draw_index),
			reinterpret_cast<void**>(&idx_dest),
			D3DLOCK_DISCARD)
		!= D3D_OK)
	{
		_vtx_buffer->Unlock();
		return;
	}

	const auto copy_draw_data = [&](draw_buffer* buf_ptr) {
		auto* vtx_src = buf_ptr->vertices.data();
		for (auto i = 0u; i < buf_ptr->vertices.size(); i++)
		{
			vtx_dest->pos[0] = vtx_src->pos.x;
			vtx_dest->pos[1] = vtx_src->pos.y;
			vtx_dest->pos[2] = 1.f;
			vtx_dest->col = vtx_src->col.as_argb();
			vtx_dest->uv[0] = vtx_src->uv.x;
			vtx_dest->uv[1] = vtx_src->uv.y;
			vtx_dest++;
			vtx_src++;
		}
		std::memcpy(idx_dest, buf_ptr->indices.data(),
			buf_ptr->indices.size() * sizeof(draw_buffer::draw_index));
		idx_dest += buf_ptr->indices.size();
	};

	const auto copy_child_data = [=](const buffer_node::child_array& childs,
		const auto& self_ref) -> void {
			for (const auto child : childs)
			{
				const auto& element = _buffer_list[child.second];
				copy_draw_data(element.active_buffer.get());
				if (!element.child_buffers.empty())
					self_ref(element.child_buffers, self_ref);
			}
	};

	if (vtx_count && idx_count)
	{
		for (const auto prio_idx : _priorities)
		{
			const auto& node = _buffer_list[prio_idx.second];

			copy_draw_data(node.active_buffer.get());
			copy_child_data(node.child_buffers, copy_child_data);
		}

		_idx_buffer->Unlock();
		_vtx_buffer->Unlock();
	}

	if (!setup_draw_state())
	{
		fonts->locked = false;
		return;
	}

	setup_shader();

	//Render
	std::uint32_t vtx_offset = 0;
	std::uint32_t idx_offset = 0;

	_device_ptr->SetPixelShader(_r.pixel_shader);
	_device_ptr->SetVertexShader(_r.vertex_shader);

	D3DXVECTOR4 size_vec = { _screen_size.x, _screen_size.y, 0.f, 0.f };

	_device_ptr->SetVertexShaderConstantF(9, _wvp, 4);
	_device_ptr->SetPixelShaderConstantF(4, size_vec, 1);

	IDirect3DSurface9* back_buffer = nullptr;
	_device_ptr->GetRenderTarget(0, &back_buffer);

	IDirect3DSurface9* target_surface = nullptr;
	if (!_r.buffer_copy)
	{
		D3DSURFACE_DESC buffer_desc;
		back_buffer->GetDesc(&buffer_desc);
		_device_ptr->CreateTexture(buffer_desc.Width, buffer_desc.Height, 0,
			D3DUSAGE_RENDERTARGET, buffer_desc.Format,
			D3DPOOL_DEFAULT, &_r.buffer_copy, nullptr);
	}

	_r.buffer_copy->GetSurfaceLevel(0, &target_surface);
	const auto res = _device_ptr->StretchRect(back_buffer, nullptr, target_surface, nullptr,
		D3DTEXF_NONE);

	IDirect3DBaseTexture9* bak_tex = nullptr;
	_device_ptr->GetTexture(1, &bak_tex);
	_device_ptr->SetTexture(1, _r.buffer_copy);

	_device_ptr->SetPixelShader(nullptr);
	_device_ptr->SetVertexShader(nullptr);

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
				if (cmd.circle_scissor)
				{
					// x,y = center; z = radius*radius; screenSpace
					D3DXVECTOR4 circle_def;
					circle_def.x =
						static_cast<float>(cmd.clip_rect.x + cmd.clip_rect.z) * 0.5f;
					circle_def.y =
						static_cast<float>(cmd.clip_rect.y + cmd.clip_rect.w) * 0.5f;
					circle_def.z =
						static_cast<float>(cmd.clip_rect.z - cmd.clip_rect.x) * 0.5f;
					circle_def.z *= circle_def.z;
					_device_ptr->SetVertexShader(_r.vertex_shader);
					_device_ptr->SetPixelShader(_r.scissor_pixel_shader);
					_device_ptr->SetPixelShaderConstantF(5, circle_def, 1);
				}

				const RECT clip = { cmd.clip_rect.x, cmd.clip_rect.y, cmd.clip_rect.z,
													 cmd.clip_rect.w };
				auto tex_id = cmd.tex_id;
				if (cmd.font_texture)
					tex_id = font_tex;
				else if (tex_id && !cmd.native_texture)
					tex_id = _tex_dict.texture(reinterpret_cast<d3d9_tex_wrapper*>(tex_id));

				auto sampler_available = BOOL{ tex_id != nullptr };
				_device_ptr->SetPixelShaderConstantB(1, &sampler_available, 1);

				_device_ptr->SetScissorRect(&clip);
				_device_ptr->SetTexture(/*texture_stage*/ 0u,
					reinterpret_cast<IDirect3DTexture9*>(tex_id));
				_device_ptr->SetTransform(
					D3DTS_WORLD,
					reinterpret_cast<const D3DMATRIX*>(cmd.matrix.matrix.data()));
				if (cmd.blur_strength)
				{
					_device_ptr->SetVertexShader(_r.vertex_shader);
					_device_ptr->SetPixelShader(
						cmd.circle_scissor ? _r.scissor_blur_shader : _r.pixel_shader);
					for (auto i = 0; i < cmd.blur_strength; ++i)
					{
						_device_ptr->StretchRect(back_buffer, nullptr, target_surface,
							nullptr, D3DTEXF_NONE);
						_device_ptr->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, vtx_offset, 0,
							cmd.vtx_count, idx_offset,
							cmd.elem_count / 3);
					}
					_device_ptr->SetPixelShader(nullptr);
					_device_ptr->SetVertexShader(nullptr);
				}
				else
				{
					if (cmd.key_color.a() != 0)
					{
						_device_ptr->SetVertexShader(_r.vertex_shader);
						D3DXVECTOR4 vec = { cmd.key_color.r() / 255.f, cmd.key_color.g() / 255.f,
															 cmd.key_color.b() / 255.f, 0.f };
						_device_ptr->SetPixelShaderConstantF(8, vec, 1);
						_device_ptr->SetPixelShader(
							cmd.circle_scissor ? _r.scissor_key_shader : _r.key_shader);
					}

					_device_ptr->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, vtx_offset, 0,
						cmd.vtx_count, idx_offset,
						cmd.elem_count / 3);

					if (cmd.key_color.a() != 0)
					{
						_device_ptr->SetVertexShader(nullptr);
						_device_ptr->SetPixelShader(nullptr);
					}
				}

				if (cmd.circle_scissor)
				{
					_device_ptr->SetVertexShader(nullptr);
					_device_ptr->SetPixelShader(nullptr);
				}
				idx_offset += cmd.elem_count;
			}
		}
		vtx_offset += buf_ptr->vertices.size();
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

	_device_ptr->SetTexture(1, bak_tex);
	if (bak_tex)
		bak_tex->Release();

	target_surface->Release();
	back_buffer->Release();

	destroy_draw_state();
	fonts->locked = false;
}

void d3d9_manager::setup_shader()
{
	if (!_r.pixel_shader)
	{
		_device_ptr->CreatePixelShader(
			reinterpret_cast<const DWORD*>(shaders::pixel::blur), &_r.pixel_shader);
	}

	if (!_r.vertex_shader)
	{
		_device_ptr->CreateVertexShader(
			reinterpret_cast<const DWORD*>(shaders::vertex::generic),
			&_r.vertex_shader);
	}

	if (!_r.key_shader)
	{
		_device_ptr->CreatePixelShader(
			reinterpret_cast<const DWORD*>(shaders::pixel::key), &_r.key_shader);
	}

	if (!_r.scissor_pixel_shader)
	{
		_device_ptr->CreatePixelShader(
			reinterpret_cast<const DWORD*>(shaders::pixel::scissor),
			&_r.scissor_pixel_shader);
	}

	if (!_r.scissor_blur_shader)
	{
		_device_ptr->CreatePixelShader(
			reinterpret_cast<const DWORD*>(shaders::pixel::scissor_blur),
			&_r.scissor_blur_shader);
	}

	if (!_r.scissor_key_shader)
	{
		_device_ptr->CreatePixelShader(
			reinterpret_cast<const DWORD*>(shaders::pixel::scissor_key),
			&_r.scissor_key_shader);
	}
}

bool d3d9_manager::setup_draw_state()
{
	_device_ptr->GetRenderState(D3DRS_COLORWRITEENABLE, &_color_write_enable);
	_device_ptr->GetSamplerState(0ul, D3DSAMP_ADDRESSU, &_sampler_u);
	_device_ptr->GetSamplerState(0ul, D3DSAMP_ADDRESSV, &_sampler_v);
	_device_ptr->GetSamplerState(0ul, D3DSAMP_ADDRESSW, &_sampler_w);
	_device_ptr->GetSamplerState(0ul, D3DSAMP_SRGBTEXTURE, &_sampler_srgb);

	_device_ptr->GetViewport(&_bak.vp);
	_device_ptr->SetRenderState(D3DRS_COLORWRITEENABLE, 0xFFFFFFFF);
	_device_ptr->SetRenderState(D3DRS_SRGBWRITEENABLE, false);
	_device_ptr->SetSamplerState(0ul, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	_device_ptr->SetSamplerState(0ul, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	_device_ptr->SetSamplerState(0ul, D3DSAMP_ADDRESSW, D3DTADDRESS_WRAP);
	_device_ptr->SetSamplerState(0ul, D3DSAMP_SRGBTEXTURE, 0ul);

	_device_ptr->GetVertexDeclaration(&_bak.vert_declaration);

	if (_device_ptr->CreateStateBlock(D3DSBT_ALL, &_r.state_block) != D3D_OK)
		return false;
	_r.state_block->Capture();

	D3DVIEWPORT9 vp;
	vp.X = vp.Y = 0;
	vp.Width = static_cast<int>(_screen_size.x);
	vp.Height = static_cast<int>(_screen_size.y);
	vp.MinZ = 0.f;
	vp.MaxZ = 1.f;
	_device_ptr->SetViewport(&vp);

	_device_ptr->GetTexture(0, &_bak.texture);
	_device_ptr->GetVertexShader(&_bak.vert_shader);
	_device_ptr->GetPixelShader(&_bak.pixel_shader);
	_device_ptr->SetTexture(0, nullptr);
	_device_ptr->GetFVF(&_bak.fvf);
	_device_ptr->SetVertexShader(nullptr);
	_device_ptr->SetPixelShader(nullptr);
	_device_ptr->SetFVF(D3DFVF_CUSTOM);
	_device_ptr->SetStreamSource(0, _vtx_buffer, 0, sizeof(d3d9_vertex));
	_device_ptr->SetIndices(_idx_buffer);

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

	_device_ptr->SetTransform(D3DTS_WORLD, &_identity);
	_device_ptr->SetTransform(D3DTS_VIEW, &_identity);
	_device_ptr->SetTransform(D3DTS_PROJECTION, &_projection);

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
	_device_ptr->SetViewport(&_bak.vp);

	_device_ptr->SetSamplerState(0ul, D3DSAMP_SRGBTEXTURE, _sampler_srgb);
	_device_ptr->SetSamplerState(0ul, D3DSAMP_ADDRESSW, _sampler_w);
	_device_ptr->SetSamplerState(0ul, D3DSAMP_ADDRESSV, _sampler_v);
	_device_ptr->SetSamplerState(0ul, D3DSAMP_ADDRESSU, _sampler_u);

	_device_ptr->SetRenderState(D3DRS_COLORWRITEENABLE, _color_write_enable);
	_device_ptr->SetRenderState(D3DRS_SRGBWRITEENABLE, true);

	safe_release(_bak.vert_declaration);
	safe_release(_bak.pixel_shader);
	safe_release(_bak.vert_shader);
	safe_release(_bak.texture);
}

bool d3d9_manager::create_font_texture()
{
	uint8_t* pixels;
	uint32_t width, height, bytes_per_pixel;
	fonts->tex_data_as_rgba_32(&pixels, &width, &height, &bytes_per_pixel);

	if (pixels == nullptr)
		return true;

	if (_r.font_texture)
		_r.font_texture->Release();

	_r.font_texture = nullptr;
	if (_device_ptr->CreateTexture(width, height, 1, D3DUSAGE_DYNAMIC,
		D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
		&_r.font_texture, nullptr)
		!= D3D_OK)
		return false;

	D3DLOCKED_RECT locked_rect;
	if (_r.font_texture->LockRect(0, &locked_rect, nullptr, 0) != D3D_OK)
		return false;
	for (auto i = 0u; i < height; i++)
		std::memcpy(reinterpret_cast<unsigned char*>(locked_rect.pBits)
			+ locked_rect.Pitch * i,
			pixels + (width * bytes_per_pixel) * i,
			(width * bytes_per_pixel));

	_r.font_texture->UnlockRect(0);

	fonts->tex_id = _r.font_texture;
	fonts->has_updated = false;

	return true;
}

bool d3d9_manager::create_device_objects()
{
	if (!_device_ptr)
		return false;
	if (!create_font_texture())
		return false;

	_tex_dict.post_reset(_device_ptr);

	return true;
}

void d3d9_manager::invalidate_shader()
{
	if (_r.pixel_shader)
	{
		_r.pixel_shader->Release();
		_r.pixel_shader = nullptr;
	}
	if (_r.vertex_shader)
	{
		_r.vertex_shader->Release();
		_r.vertex_shader = nullptr;
	}
	if (_r.key_shader)
	{
		_r.key_shader->Release();
		_r.key_shader = nullptr;
	}
}

bool d3d9_manager::invalidate_device_objects()
{
	if (!_device_ptr)
		return false;
	if (_vtx_buffer)
	{
		_vtx_buffer->Release();
		_vtx_buffer = nullptr;
	}
	if (_idx_buffer)
	{
		_idx_buffer->Release();
		_idx_buffer = nullptr;
	}
	if (_r.font_texture)
		_r.font_texture->Release();
	if (_r.state_block)
	{
		_r.state_block->Release();
		_r.state_block = nullptr;
	}
	invalidate_shader();

	if (_r.buffer_copy)
	{
		_r.buffer_copy->Release();
		_r.buffer_copy = nullptr;
	}

	_r.font_texture = nullptr;
	fonts->tex_id = nullptr;
	_tex_dict.pre_reset();

	return true;
}

tex_id d3d9_manager::create_texture(const uint32_t width, const uint32_t height)
{
	return reinterpret_cast<tex_id>(_tex_dict.create_texture(width, height));
}

// Idk why directx seems to be using RABG internally, mabye not, IDK!!! this is weird, fuck directx
void copy_convert(const uint8_t* rgba, uint8_t* out, const size_t size)
{
	auto in = reinterpret_cast<const uint32_t*>(rgba);
	auto buf = reinterpret_cast<uint32_t*>(out);
	for (auto i = 0u; i < (size / 4); ++i)
	{
		*buf++ =
			(*in & 0xFF00FF00) | ((*in & 0xFF0000) >> 16) | ((*in++ & 0xFF) << 16);
	}
}

bool d3d9_manager::set_texture_rgba(const tex_id id, const uint8_t* rgba,
	const uint32_t width, const uint32_t height)
{
	assert(id != reinterpret_cast<tex_id>(0));

	if (!id)
		return false;

	auto tmp_data = std::vector<uint8_t>{};
	tmp_data.resize(width * height * 4u);

	copy_convert(rgba, tmp_data.data(),
		width * height * sizeof(std::remove_pointer_t<decltype(rgba)>)
		* 4u);

	return _tex_dict.set_tex_data(_device_ptr,
		reinterpret_cast<d3d9_tex_wrapper*>(id),
		tmp_data.data(), width, height);
}

bool d3d9_manager::set_texture_rabg(const tex_id id, const uint8_t* rabg,
	const uint32_t width, const uint32_t height)
{
	assert(id != reinterpret_cast<tex_id>(0));

	if (!id)
		return false;

	return _tex_dict.set_tex_data(
		_device_ptr, reinterpret_cast<d3d9_tex_wrapper*>(id), rabg, width, height);
}

bool d3d9_manager::texture_size(const tex_id id, uint32_t& width,
	uint32_t& height)
{
	assert(id != reinterpret_cast<tex_id>(0));
	if (!id)
		return false;

	return _tex_dict.texture_size(reinterpret_cast<d3d9_tex_wrapper*>(id), width,
		height);
}

bool d3d9_manager::delete_texture(const tex_id id)
{
	assert(id != reinterpret_cast<tex_id>(0));
	if (!id)
		return false;

	_tex_dict.destroy_texture(reinterpret_cast<d3d9_tex_wrapper*>(id));
	return true;
}

void d3d9_manager::update_screen_size(const position& screen_size)
{
	_screen_size = screen_size;
	std::memset(&_projection, 0, sizeof(_projection));
	_projection[0] = 2.f / _screen_size.x;
	_projection[5] = 2.f / -_screen_size.y;
	_projection[10] = 0.5f;
	_projection[12] = (_screen_size.x + 1.f) / -_screen_size.x;
	_projection[13] = (_screen_size.y + 1.f) / _screen_size.y;
	_projection[14] = 0.5f;
	_projection[15] = 1.f;

	D3DXMatrixIdentity(&_wvp);
	_wvp *= _identity;
	_wvp *= _projection;
}
