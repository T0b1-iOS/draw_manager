#include "../include/types.hlsli"
#include "../include/blur.hlsli"

PS_OUTPUT main(VS_OUTPUT IN)
{
	PS_OUTPUT OUT;

	float2 tex_coord = float2((IN.hposition.x + 1) / 2, (IN.hposition.y - 1) / -2);
	OUT.color = float4(blur(tex_coord, true), 1);

	return OUT;
}