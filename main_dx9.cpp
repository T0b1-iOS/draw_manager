#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>

#include <atomic>
#include <memory>
#include <chrono>
#include <thread>

#include "draw_manager.hpp"
#include "impl/d3d9_manager.hpp"
#include "math.h"

LRESULT CALLBACK window_proc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam);

bool create_window();
bool create_draw_context();
void destroy_draw_context();

void reset_d3d9_device();

void draw();
extern void user_thread_impl();

int window_size_x = 1280;
int window_size_y = 720;

WNDCLASS window_class = {};
HWND window_handle    = nullptr;

LPDIRECT3D9 d3d9_instance                 = nullptr;
LPDIRECT3DDEVICE9 d3d9_device             = nullptr;
D3DPRESENT_PARAMETERS d3d9_present_params = {};
auto do_reset                             = false;

std::atomic<int> fps        = 0;
auto fps_counter            = 0;
auto fps_counter_start_time = 0ull;

std::unique_ptr<util::draw::draw_manager> draw_manager = nullptr;
auto stop_user_thread                                  = false;
extern std::atomic<util::draw::tex_id> tex;
extern std::atomic<bool> reset_tex;
std::thread user_thread;

int main()
{
	if (!create_window())
		return 1;

	ShowWindow(window_handle, SW_SHOW);

	if (!create_draw_context())
		return 1;

	draw_manager = std::make_unique<util::draw::d3d9_manager>(d3d9_device);
	draw_manager->update_screen_size(util::draw::position{
		static_cast<float>(window_size_x),
		static_cast<float>(window_size_y)
	});
	user_thread = std::thread{user_thread_impl};

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
		fps                    = fps_counter;
		fps_counter            = 0;
		fps_counter_start_time = ms;
		char buffer[128];
		std::snprintf(buffer, 128, "D3D9 Test Window - FPS: %d", fps.load());
		SetWindowTextA(window_handle, buffer);
	}

	d3d9_device->SetRenderState(D3DRS_ZENABLE, false);
	d3d9_device->SetRenderState(D3DRS_ALPHABLENDENABLE, false);
	d3d9_device->SetRenderState(D3DRS_SCISSORTESTENABLE, false);
	const auto clear_col = D3DCOLOR_RGBA(0, 0, 0, 255);
	d3d9_device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col, 1.f, 0);
	if (d3d9_device->BeginScene() >= 0)
	{
		draw_manager->draw();
		d3d9_device->EndScene();
	}

	const auto res = d3d9_device->Present(nullptr, nullptr, nullptr, nullptr);

	if (do_reset || (res == D3DERR_DEVICELOST && d3d9_device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET))
	{
		reset_d3d9_device();
		do_reset = false;
	}

	fps_counter++;
}

bool create_window()
{
	const auto class_name = "Draw Manager";

	window_class.lpfnWndProc   = &window_proc;
	window_class.hInstance     = GetModuleHandleA(nullptr);
	window_class.lpszClassName = class_name;

	if (!RegisterClassA(&window_class))
		return false;

	window_handle = CreateWindowExA(0,
	                                class_name,
	                                "D3D9 Test Window - FPS: 0",
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
				if (d3d9_device && wparam != SIZE_MINIMIZED)
				{
					d3d9_present_params.BackBufferWidth  = LOWORD(lparam);
					d3d9_present_params.BackBufferHeight = HIWORD(lparam);
					do_reset                             = true;
					//reset_d3d9_device();
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
	d3d9_instance = Direct3DCreate9(D3D_SDK_VERSION);
	if (!d3d9_instance)
		return false;

	std::memset(&d3d9_present_params, 0, sizeof(d3d9_present_params));
	d3d9_present_params.Windowed               = true;
	d3d9_present_params.SwapEffect             = D3DSWAPEFFECT_DISCARD;
	d3d9_present_params.BackBufferFormat       = D3DFMT_A8R8G8B8;
	d3d9_present_params.EnableAutoDepthStencil = true;
	d3d9_present_params.AutoDepthStencilFormat = D3DFMT_D16;
	d3d9_present_params.PresentationInterval   = D3DPRESENT_INTERVAL_IMMEDIATE;
	d3d9_present_params.BackBufferWidth        = window_size_x;
	d3d9_present_params.BackBufferHeight       = window_size_y;

	if (d3d9_instance->CreateDevice(D3DADAPTER_DEFAULT,
	                                D3DDEVTYPE_HAL,
	                                window_handle,
	                                D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
	                                &d3d9_present_params,
	                                &d3d9_device) != D3D_OK)
		return false;

	return true;
}

void destroy_draw_context()
{
	if (d3d9_device)
		d3d9_device->Release();
	if (d3d9_instance)
		d3d9_instance->Release();
}

void reset_d3d9_device()
{
	reinterpret_cast<util::draw::d3d9_manager*>(draw_manager.get())->invalidate_device_objects();
	const auto res = d3d9_device->Reset(&d3d9_present_params);
	assert(res == D3D_OK);
	reinterpret_cast<util::draw::d3d9_manager*>(draw_manager.get())->create_device_objects();
}
