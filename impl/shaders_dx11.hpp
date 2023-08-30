#pragma once

namespace shaders
{
	namespace vertex
	{
#include "shaders/cpp/d3d11/vertex/generic.h"
	}

	namespace pixel
	{
#include "shaders/cpp/d3d11/pixel/generic.h"
#include "shaders/cpp/d3d11/pixel/scissor.h"
#include "shaders/cpp/d3d11/pixel/blur_x.h"
#include "shaders/cpp/d3d11/pixel/blur_y.h"
#include "shaders/cpp/d3d11/pixel/scissor_blur_x.h"
#include "shaders/cpp/d3d11/pixel/scissor_blur_y.h"
#include "shaders/cpp/d3d11/pixel/key.h"
#include "shaders/cpp/d3d11/pixel/scissor_key.h"
	}  // namespace pixel

}  // namespace shaders
