#pragma once

namespace shaders
{
	namespace vertex
	{
#include "shaders/cpp/vertex/generic.h"
	}

	namespace pixel
	{
#include "shaders/cpp/pixel/blur.h"
#include "shaders/cpp/pixel/key.h"
#include "shaders/cpp/pixel/scissor.h"
#include "shaders/cpp/pixel/scissor_blur.h"
#include "shaders/cpp/pixel/scissor_key.h"
	}  // namespace pixel

}  // namespace shaders
