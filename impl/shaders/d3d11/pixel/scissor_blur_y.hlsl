#include "../include/scissor_blur.hlsli"

float4 main(VS_OUTPUT IN) : SV_TARGET {
	return scissor_blur(IN, false);
}