#include "../include/scissor_blur.hlsli"

PS_OUTPUT main(VS_OUTPUT IN) {
	return scissor_blur(IN, true);
}