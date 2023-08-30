#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <memory>
#include <chrono>
#include <thread>

#include "draw_manager.hpp"
#include "impl/d3d11_manager.hpp"

#include "math.h"

LRESULT CALLBACK window_proc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam);

bool create_window();
bool create_draw_context();
void destroy_draw_context();

void draw();
void user_thread_impl();

int window_size_x = 1280;
int window_size_y = 720;

WNDCLASS window_class = {};
HWND window_handle = nullptr;

using Microsoft::WRL::ComPtr;
ComPtr<ID3D11Device> d3d11_device = nullptr;
ComPtr<ID3D11DeviceContext> d3d11_device_ctx = nullptr;
ComPtr<ID3D11RenderTargetView> d3d11_rtview = nullptr;
ComPtr<IDXGISwapChain> d3d11_swapchain = nullptr;

std::atomic<int> fps = 0;
auto fps_counter = 0;
auto fps_counter_start_time = 0ull;
bool reset_rt = false;

std::unique_ptr<util::draw::draw_manager> draw_manager = nullptr;
auto stop_user_thread = false;
std::thread user_thread;

int main()
{
	if (!create_window())
		return 1;

	ShowWindow(window_handle, SW_SHOW);

	if (!create_draw_context())
		return 1;

	draw_manager = std::make_unique<util::draw::d3d11_manager>(d3d11_device.Get(), d3d11_device_ctx.Get());
	draw_manager->update_screen_size(util::draw::position{
		static_cast<float>(window_size_x),
			static_cast<float>(window_size_y)
	});
	user_thread = std::thread{ user_thread_impl };

	MSG msg;
	std::memset(&msg, 0, sizeof(MSG));
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, nullptr, 0u, 0u, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}

		draw();
	}

	stop_user_thread = true;
	user_thread.join();

	destroy_draw_context();
	DestroyWindow(window_handle);
	UnregisterClass(window_class.lpszClassName, window_class.hInstance);

	return 0;
}

void draw()
{
	using namespace std::chrono;

	const auto ms = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	if (fps_counter_start_time == 0ull)
	{
		fps_counter_start_time = ms;
	}
	else if (ms - fps_counter_start_time >= 1000ull)
	{
		fps = fps_counter;
		fps_counter = 0;
		fps_counter_start_time = ms;
		char buffer[128];
		std::snprintf(buffer, 128, "D3D11 Test Window - FPS: %d", fps.load());
		SetWindowTextA(window_handle, buffer);
	}

	if (reset_rt) {
		d3d11_rtview = nullptr;
		auto res = d3d11_swapchain->ResizeBuffers(2, window_size_x, window_size_y, DXGI_FORMAT_UNKNOWN, 0);
		if (res != S_OK) {
			std::fprintf(stderr, "ResizeBuffers failed: %d\n", res);
			exit(1);
		}

		ComPtr<ID3D11Texture2D> back_buffer;
		d3d11_swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
		d3d11_device->CreateRenderTargetView(back_buffer.Get(), nullptr,
			d3d11_rtview.GetAddressOf());
	}

	const FLOAT clear_col[4] = { 0.f, 0.f, 0.f, 1.f };
	d3d11_device_ctx->ClearRenderTargetView(d3d11_rtview.Get(), clear_col);
	d3d11_device_ctx->OMSetRenderTargets(1, d3d11_rtview.GetAddressOf(), nullptr);
	draw_manager->draw();

	const auto res = d3d11_swapchain->Present(0, 0);

	if (res != S_OK)
	{
		std::fprintf(stderr, "Present failed: %d\n", res);
		exit(1);
	}

	fps_counter++;
}

bool create_window()
{
	const auto class_name = "Draw Manager";

	window_class.lpfnWndProc = &window_proc;
	window_class.hInstance = GetModuleHandleA(nullptr);
	window_class.lpszClassName = class_name;

	if (!RegisterClassA(&window_class))
		return false;

	window_handle = CreateWindowExA(0,
		class_name,
		"D3D11 Test Window - FPS: 0",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		window_size_x,
		window_size_y,
		nullptr,
		nullptr,
		GetModuleHandleA(nullptr),
		nullptr);

	return (window_handle != nullptr);
}

LRESULT __stdcall window_proc(const HWND wnd, const UINT msg, const WPARAM wparam, const LPARAM lparam)
{
	switch (msg)
	{
	case WM_SIZE:
	{
		if (d3d11_device && wparam != SIZE_MINIMIZED)
		{
			window_size_x = LOWORD(lparam);
			window_size_y = HIWORD(lparam);
			reset_rt = true;
		}
		if (draw_manager && wparam != SIZE_MINIMIZED)
		{
			draw_manager->update_screen_size(util::draw::position{
				static_cast<float>(LOWORD(lparam)),
					static_cast<float>(HIWORD(lparam))
			});
		}
		return 0;
	}
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProcA(wnd, msg, wparam, lparam);
	}
}

bool create_draw_context()
{
	DXGI_SWAP_CHAIN_DESC desc;
	std::memset(&desc, 0, sizeof(desc));
	desc.BufferCount = 2;
	desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.BufferDesc.RefreshRate.Numerator = 60;
	desc.BufferDesc.RefreshRate.Denominator = 1;
	desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.OutputWindow = window_handle;
	desc.SampleDesc.Count = 1;
	desc.Windowed = TRUE;
	desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	D3D_FEATURE_LEVEL feature_level;
	const D3D_FEATURE_LEVEL feature_levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
	if (const auto res = D3D11CreateDeviceAndSwapChain(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
		feature_levels, 2, D3D11_SDK_VERSION, &desc,
		d3d11_swapchain.GetAddressOf(), d3d11_device.GetAddressOf(),
		&feature_level, d3d11_device_ctx.GetAddressOf());
		res != S_OK)
		return false;

	ComPtr<ID3D11Texture2D> back_buffer;
	d3d11_swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
	d3d11_device->CreateRenderTargetView(back_buffer.Get(), nullptr,
		d3d11_rtview.GetAddressOf());
	return true;
}

void destroy_draw_context()
{
	d3d11_rtview = nullptr;
	d3d11_swapchain = nullptr;
	d3d11_device_ctx = nullptr;
	d3d11_device = nullptr;
}
