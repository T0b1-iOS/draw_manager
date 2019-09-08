#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d9.h>
#include <d3dx9.h>

#include <assert.h>

#include "tex_dict.h"

using namespace util::draw;

void d3d9_tex_wrapper::create(IDirect3DDevice9 *device)
{
	const auto res = device->CreateTexture(_size_x,
	                                       _size_y,
	                                       1,
	                                       0,
	                                       D3DFMT_A8R8G8B8,
	                                       D3DPOOL_DEFAULT,
	                                       &_texture,
	                                       nullptr);
	assert(res == D3D_OK);

	copy_texture_data(device);
}

bool d3d9_tex_wrapper::set_tex_data(IDirect3DDevice9 *device,
                                    const uint8_t *data,
                                    const uint32_t size_x,
                                    const uint32_t size_y)
{
	_texture_data.resize(size_x * size_y * 4);
	std::copy(data, data + _texture_data.size(), _texture_data.begin());

	_size_x = size_x;
	_size_y = size_y;
	if (!_texture)
	{
		const auto res = device->CreateTexture(_size_x,
		                                       _size_y,
		                                       1,
		                                       0,
		                                       D3DFMT_A8R8G8B8,
		                                       D3DPOOL_DEFAULT,
		                                       &_texture,
		                                       nullptr);
		assert(res == D3D_OK);
	}

	return copy_texture_data(device);
}

bool d3d9_tex_wrapper::copy_texture_data(IDirect3DDevice9 *device)
{
	IDirect3DTexture9 *tmp_tex = nullptr;
	auto res                   = device->CreateTexture(_size_x,
	                                                   _size_y,
	                                                   1,
	                                                   D3DUSAGE_DYNAMIC,
	                                                   D3DFMT_A8R8G8B8,
	                                                   D3DPOOL_SYSTEMMEM,
	                                                   &tmp_tex,
	                                                   nullptr);
	if (res != D3D_OK)
	{
		assert(0);
		return false;
	}

	D3DLOCKED_RECT rect;
	res = tmp_tex->LockRect(0, &rect, nullptr, D3DLOCK_DISCARD);
	if (res != D3D_OK)
	{
		tmp_tex->Release();
		assert(0);
		return false;
	}

	auto src = _texture_data.data();
	auto dst = reinterpret_cast<uint8_t*>(rect.pBits);
	for (auto y = 0u; y < _size_y; ++y)
	{
		std::copy(src, src + (_size_x * 4), dst);

		src += _size_x * 4;
		dst += rect.Pitch;
	}

	res = tmp_tex->UnlockRect(0);
	if (res != D3D_OK)
	{
		tmp_tex->Release();
		assert(0);
		return false;
	}

	res = device->UpdateTexture(tmp_tex, _texture);

	tmp_tex->Release();
	assert(res == D3D_OK);
	return (res == D3D_OK);
}

void d3d9_tex_dict::clear_textures()
{
	std::scoped_lock g(_mutex);
	for (auto &tex : _textures)
		if (!tex.free)
			tex.clear_data();
	_textures.clear();
}

uint32_t d3d9_tex_dict::create_texture(uint32_t size_x, uint32_t size_y)
{
	std::scoped_lock g(_mutex);
	for (auto i = 0u; i < _textures.size(); ++i)
	{
		if (_textures[i].free)
		{
			_textures[i].free = false;
			return i;
		}
	}

	const auto idx = _textures.size();
	_textures.emplace_back(d3d9_tex_wrapper{});
	return idx;
}

void d3d9_tex_dict::destroy_texture(const size_t id)
{
	std::scoped_lock g(_mutex);
	_textures[id].clear_data();
	_textures[id].free = true;
}
