#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>

#include <atomic>
#include <forward_list>
#include <vector>
#include <mutex>

namespace util::draw {
	struct tex_dict_dx11;

	struct tex_wrapper_dx11
	{
		tex_wrapper_dx11() noexcept = default;
		~tex_wrapper_dx11() noexcept
		{
			if (_texture)
				_texture->Release();
			if (_res_view)
				_res_view->Release();
			_texture = nullptr;
			_res_view = nullptr;
		}

		tex_wrapper_dx11(const tex_wrapper_dx11&) = delete;
		tex_wrapper_dx11& operator=(const tex_wrapper_dx11&) = delete;

		tex_wrapper_dx11(tex_wrapper_dx11&&) noexcept = default;
		tex_wrapper_dx11& operator=(tex_wrapper_dx11&&) noexcept = default;

		// 4 channels
		bool set_tex_data(tex_dict_dx11* dict, ID3D11Device* device, const uint8_t* data, uint32_t size_x, uint32_t size_y);
		bool apply_tex_changes(ID3D11DeviceContext*);

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
			if (_res_view)
				_res_view->Release();
			_texture = nullptr;
			_res_view = nullptr;
		}

		void invalidate()
		{
			if (_texture)
				_texture->Release();
			if (_res_view)
				_res_view->Release();
			_texture = nullptr;
			_res_view = nullptr;
		}

		void create(ID3D11Device* device); // <- for reset
		ID3D11ShaderResourceView* texture() const { return _res_view; }

		bool free = false;

	protected:
		bool copy_texture_data(ID3D11DeviceContext* ctx);

		ID3D11Texture2D* _texture = nullptr;
		ID3D11ShaderResourceView* _res_view = nullptr;
		std::vector<uint8_t> _texture_data = {};

		uint32_t _size_x = 0u, _size_y = 0u;
	};

	struct tex_dict_dx11 {
		friend struct tex_wrapper_dx11;

		tex_dict_dx11() = default;
		~tex_dict_dx11() { clear_textures(); }

		ID3D11ShaderResourceView* texture(const tex_wrapper_dx11* tex)
		{
			std::scoped_lock g(_mutex);
			if (!is_valid_tex(tex))
				return nullptr;

			return tex->texture();
		}

		bool set_tex_data(ID3D11Device* device, tex_wrapper_dx11* tex,
			const uint8_t* data, const uint32_t size_x,
			const uint32_t size_y)
		{
			std::scoped_lock g(_mutex);
			if (!is_valid_tex(tex))
				return false;

			return tex->set_tex_data(this, device, data, size_x, size_y);
		}

		bool texture_size(const tex_wrapper_dx11* tex, uint32_t& width, uint32_t& height)
		{
			std::scoped_lock g(_mutex);
			if (!is_valid_tex(tex))
				return false;

			return tex->texture_size(width, height);
		}

		bool is_valid_tex(const tex_wrapper_dx11* tex)
		{
			return std::find(_valid_elements.begin(), _valid_elements.end(), tex) != _valid_elements.end();
		}

		tex_wrapper_dx11* create_texture(uint32_t size_x, uint32_t size_y);
		void destroy_texture(tex_wrapper_dx11* tex);

		// call from directx thread
		void clear_textures();

		void pre_reset()
		{
			std::scoped_lock g(_mutex);
			for (auto& tex : _textures)
				tex.invalidate();
		}

		void post_reset(ID3D11Device* device)
		{
			std::scoped_lock g(_mutex);
			for (auto& tex : _textures)
				if (!tex.free)
					tex.create(device);
		}

		void process_update_queue(ID3D11DeviceContext*);

	protected:
		std::mutex _mutex;
		std::forward_list<tex_wrapper_dx11> _textures = {};
		std::vector<tex_wrapper_dx11*> _valid_elements = {};
		std::mutex _update_queue_lock;
		std::vector<tex_wrapper_dx11*> _update_queue{};
	};
}
