#pragma once

#include <vector>
#include <mutex>

namespace util::draw
{
	struct d3d9_tex_wrapper
	{
		~d3d9_tex_wrapper()
		{
			if (_texture)
				_texture->Release();
		}

		// 4 channels
		bool set_tex_data(IDirect3DDevice9 *device, const uint8_t *data, uint32_t size_x, uint32_t size_y);

		void clear_data()
		{
			if (_texture)
				_texture->Release();
			_texture = nullptr;
			_texture_data.clear();
		}

		void invalidate()
		{
			if (_texture)
				_texture->Release();
			_texture = nullptr;
		}

		void create(IDirect3DDevice9 *device);

		IDirect3DTexture9* texture() const
		{
			return _texture;
		}

		bool free = false;
	protected:
		bool copy_texture_data(IDirect3DDevice9 *device);

		IDirect3DTexture9 *_texture        = nullptr;
		std::vector<uint8_t> _texture_data = {};

		uint32_t _size_x = 0u, _size_y = 0u;
	};

	struct d3d9_tex_dict
	{
		~d3d9_tex_dict()
		{
			clear_textures();
		}

		uint32_t create_texture(uint32_t size_x, uint32_t size_y);
		void destroy_texture(size_t id);
		void clear_textures();

		IDirect3DTexture9* texture(const size_t id)
		{
			std::scoped_lock g(_mutex);
			if (id >= _textures.size())
				return nullptr;
			return _textures[id].texture();
		}

		bool set_tex_data(IDirect3DDevice9 *device,
		                  const size_t id,
		                  const uint8_t *data,
		                  const uint32_t size_x,
		                  const uint32_t size_y)
		{
			std::scoped_lock g(_mutex);
			if (id >= _textures.size())
				return false;

			return _textures[id].set_tex_data(device, data, size_x, size_y);
		}

		void pre_reset()
		{
			std::scoped_lock g(_mutex);
			for (auto &tex : _textures)
				tex.invalidate();
		}

		void post_reset(IDirect3DDevice9 *device)
		{
			std::scoped_lock g(_mutex);
			for (auto &tex : _textures)
				if (!tex.free)
					tex.create(device);
		}

	protected:
		std::mutex _mutex;
		std::vector<d3d9_tex_wrapper> _textures = {};
	};
}
