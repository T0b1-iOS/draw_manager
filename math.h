#pragma once

#include <array>
#include <memory>
#include <cinttypes>

// just some classes to make the renderer runnable
namespace math
{
	template<typename T>
	static constexpr T PI = T(3.14159265358979323846L);

	template<typename T>
	constexpr T rad2deg(T rad)
	{
		return rad * T(180) / PI<T>;
	}

	template<typename T>
	constexpr T deg2rad(T deg)
	{
		return deg * PI<T> / T(180);
	}

	struct vec2f
	{
		float x, y;

		vec2f()
			: x(0.f),
			  y(0.f) {}

		vec2f(const float x, const float y)
			: x(x),
			  y(y) {}

		float dot(const vec2f &o) const
		{
			return (x * o.x) + (y * o.y);
		}

		float dot() const
		{
			return dot(*this);
		}

		float length_sqr() const
		{
			return dot();
		}

		float length() const
		{
			return sqrtf(length_sqr());
		}

		float reciprocal_length() const
		{
			return 1.f / length();
		}

		vec2f& normalize()
		{
			return (*this *= reciprocal_length());
		}

		vec2f normalized() const
		{
			auto r{*this};
			return r.normalize();
		}

		bool operator==(const vec2f &o) const
		{
			return (x == o.x && y == o.y);
		}

		vec2f& operator+=(const float v)
		{
			x += v;
			y += v;
			return *this;
		}

		vec2f& operator-=(const float v)
		{
			x -= v;
			y -= v;
			return *this;
		}

		vec2f& operator/=(const float v)
		{
			x /= v;
			y /= v;
			return *this;
		}

		vec2f& operator*=(const float v)
		{
			x *= v;
			y *= v;
			return *this;
		}

		vec2f& operator+=(const vec2f &o)
		{
			x += o.x;
			y += o.y;
			return *this;
		}

		vec2f& operator-=(const vec2f &o)
		{
			x -= o.x;
			y -= o.y;
			return *this;
		}

		vec2f& operator/=(const vec2f &o)
		{
			x /= o.x;
			y /= o.y;
			return *this;
		}

		vec2f& operator*=(const vec2f &o)
		{
			x *= o.x;
			y *= o.y;
			return *this;
		}

		vec2f operator+(const float v) const
		{
			auto r{*this};
			r.x += v;
			r.y += v;
			return r;
		}

		vec2f operator-(const float v) const
		{
			auto r{*this};
			r.x -= v;
			r.y -= v;
			return r;
		}

		vec2f operator*(const float v) const
		{
			auto r{*this};
			r.x *= v;
			r.y *= v;
			return r;
		}

		vec2f operator/(const float v) const
		{
			auto r{*this};
			r.x /= v;
			r.y /= v;
			return r;
		}

		vec2f operator+(const vec2f &o) const
		{
			auto r{*this};
			r.x += o.x;
			r.y += o.y;
			return r;
		}

		vec2f operator-(const vec2f &o) const
		{
			auto r{*this};
			r.x -= o.x;
			r.y -= o.y;
			return r;
		}

		vec2f operator*(const vec2f &o) const
		{
			auto r{*this};
			r.x *= o.x;
			r.y *= o.y;
			return r;
		}

		vec2f operator/(const vec2f &o) const
		{
			auto r{*this};
			r.x /= o.x;
			r.y /= o.y;
			return r;
		}

		const float& operator[](const size_t idx) const
		{
			return reinterpret_cast<const float*>(this)[idx];
		}

		float& operator[](const size_t idx)
		{
			return reinterpret_cast<float*>(this)[idx];
		}
	};

	inline vec2f operator*(const float v, const vec2f &o)
	{
		auto r{o};
		r.x *= v;
		r.y *= v;
		return r;
	}

	struct vec4f
	{
		union
		{
			struct
			{
				float x, y, z, w;
			};

			struct
			{
				vec2f xy;
				vec2f zw;
			};
		};

		vec4f()
			: x(0.f),
			  y(0.f),
			  z(0.f),
			  w(0.f) {}

		vec4f(const float x, const float y, const float z, const float w)
			: x(x),
			  y(y),
			  z(z),
			  w(w) {}

		vec4f(const vec2f &xy, const vec2f &zw)
			: x(xy.x),
			  y(xy.y),
			  z(zw.x),
			  w(zw.y) {}

		float operator[](const size_t idx) const
		{
			return (&x)[idx];
		}

		float& operator[](const size_t idx)
		{
			return (&x)[idx];
		}

		bool operator==(const vec4f &o) const
		{
			return (xy == o.xy && zw == o.zw);
		}
	};

	struct color_rgba
	{
		uint8_t _r, _g, _b, _a;

		//color_rgba(const uint8_t r, const uint8_t g, const uint8_t b, const uint8_t a = 255u) : _r(r), _g(g), _b(b), _a(a) {}
		color_rgba(const int r, const int g, const int b, const int a = 255u)
			: _r(r),
			  _g(g),
			  _b(b),
			  _a(a) {}

		color_rgba(const uint32_t val)
		{
			_r = (val >> 24);
			_g = (val >> 16) & 0xFF;
			_b = (val >> 8) & 0xFF;
			_a = val & 0xFF;
		}

		uint32_t as_argb() const
		{
			return (_a << 24) | (_r << 16) | (_g << 8) | _b;
		}

		operator uint32_t() const
		{
			return (_r << 24) | (_g << 16) | (_b << 8) | _a;
		}

		uint8_t r() const
		{
			return _r;
		}

		uint8_t g() const
		{
			return _g;
		}

		uint8_t b() const
		{
			return _b;
		}

		uint8_t a() const
		{
			return _a;
		}

		static color_rgba white()
		{
			return color_rgba{255, 255, 255};
		}

		static color_rgba black()
		{
			return color_rgba{0, 0, 0};
		}

		static color_rgba red()
		{
			return color_rgba{235, 64, 52};
		}

		static color_rgba green()
		{
			return color_rgba{52, 217, 77};
		}

		static color_rgba blue()
		{
			return color_rgba{34, 108, 199};
		}
	};

	struct matrix4x4f
	{
		std::array<vec4f, 4> matrix = {};

		matrix4x4f() = default;

		matrix4x4f(const vec4f &row1, const vec4f &row2, const vec4f &row3, const vec4f &row4)
			: matrix{row1, row2, row3, row4} {}

		const vec4f& operator[](const size_t idx) const
		{
			return matrix[idx];
		}

		vec4f& operator[](const size_t idx)
		{
			return matrix[idx];
		}
	};
}
