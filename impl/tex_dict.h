#pragma once

#include <atomic>
#include <forward_list>
#include <vector>
#include <mutex>

namespace util::draw
{
	struct d3d9_tex_wrapper
	{
		d3d9_tex_wrapper() noexcept = default;
		~d3d9_tex_wrapper() noexcept
		{
			if (_texture)
				_texture->Release();
		}

		d3d9_tex_wrapper(const d3d9_tex_wrapper&) = delete;
		d3d9_tex_wrapper& operator=(const d3d9_tex_wrapper&) = delete;

		d3d9_tex_wrapper(d3d9_tex_wrapper&&) noexcept = default;
		d3d9_tex_wrapper& operator=(d3d9_tex_wrapper&&) noexcept = default;

		// 4 channels
		bool set_tex_data(IDirect3DDevice9* device, const uint8_t* data, uint32_t size_x, uint32_t size_y);

		bool texture_size(uint32_t& width, uint32_t& height) const
		{
			width = _size_x;
			height = _size_y;
			return true;
		}

		void clear_data()
		{
			_texture_data.clear();
			_size_x = 0;
			_size_y = 0;
			if (_texture)
				_texture->Release();
			_texture = nullptr;
		}

		void invalidate()
		{
			if (_texture)
				_texture->Release();
			_texture = nullptr;
		}

		void create(IDirect3DDevice9* device); // <- for reset
		IDirect3DTexture9* texture() const { return _texture; }

		bool free = false;

	protected:
		bool copy_texture_data(IDirect3DDevice9* device);

		IDirect3DTexture9* _texture = nullptr;
		std::vector<uint8_t> _texture_data = {};

		uint32_t _size_x = 0u, _size_y = 0u;
	};

	struct d3d9_tex_dict
	{
		d3d9_tex_dict() = default;
		~d3d9_tex_dict() { clear_textures(); }

		d3d9_tex_wrapper* create_texture(uint32_t size_x, uint32_t size_y);
		void destroy_texture(d3d9_tex_wrapper* tex);

		// call from directx thread
		void clear_textures();

		IDirect3DTexture9* texture(const d3d9_tex_wrapper* tex)
		{
			std::scoped_lock g(_mutex);
			if (!is_valid_tex(tex))
				return nullptr;

			return tex->texture();
		}

		bool set_tex_data(IDirect3DDevice9* device, d3d9_tex_wrapper* tex,
			const uint8_t* data, const uint32_t size_x,
			const uint32_t size_y)
		{
			std::scoped_lock g(_mutex);
			if (!is_valid_tex(tex))
				return false;

			return tex->set_tex_data(device, data, size_x, size_y);
		}

		bool texture_size(const d3d9_tex_wrapper* tex, uint32_t& width, uint32_t& height)
		{
			std::scoped_lock g(_mutex);
			if (!is_valid_tex(tex))
				return false;

			return tex->texture_size(width, height);
		}

		void pre_reset()
		{
			std::scoped_lock g(_mutex);
			for (auto& tex : _textures)
				tex.invalidate();
		}

		void post_reset(IDirect3DDevice9* device)
		{
			std::scoped_lock g(_mutex);
			for (auto& tex : _textures)
				if (!tex.free)
					tex.create(device);
		}

		bool is_valid_tex(const d3d9_tex_wrapper* tex)
		{
			return std::find(_valid_elements.begin(), _valid_elements.end(), tex) != _valid_elements.end();
		}

	protected:
		std::mutex _mutex;
		std::forward_list<d3d9_tex_wrapper> _textures = {};
		std::vector<d3d9_tex_wrapper*> _valid_elements = {};
	};
}  // namespace util::draw
