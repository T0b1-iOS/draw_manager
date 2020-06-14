#include "../include/types.hlsli"
#include "../include/blur.hlsli"

PS_OUTPUT main(VS_OUTPUT IN)
{
	PS_OUTPUT OUT;
	
	float2 tex_coord = float2((IN.hposition.x + 1) / 2, (IN.hposition.y - 1) / -2);
	//if (overlay)
	//{
		tex_coord.x += (1 / dimension.x);
		tex_coord.y += (1 / dimension.y);
	//}

	matrix_holder holder = get2DMatrix(5, 0.5);

	float3 result = blur2D(holder, float4(tex_coord * dimension, tex_coord));
	OUT.color     = float4(result, 1);
	
	return OUT;
}