#include "../include/types.hlsli"
#include "../include/blur.hlsli"

PS_OUTPUT scissor_blur(VS_OUTPUT IN, bool x_dir)
{
	PS_OUTPUT OUT;

	float2 center = float2(scissor.x, scissor.y);
	float2 pos = float2(IN.screenPos.x, IN.screenPos.y);
	float2 distVec = center - pos;
	float distSqr = dot(distVec, distVec);

	if (distSqr > scissor.z)
	{
		OUT.color = float4(0, 0, 0, 0);
		return OUT;
	}

	float distToEdge = sqrt(scissor.z) - sqrt(distSqr);
	float alpha = saturate(distToEdge);

	float2 tex_coord =
		float2((IN.hposition.x + 1) / 2, (IN.hposition.y - 1) / -2);
	//if (overlay)
	//{
	tex_coord.x += (1 / dimension.x);
	tex_coord.y += (1 / dimension.y);
	//}

	float3 result = blur(tex_coord, x_dir);
	OUT.color = float4(result, alpha);

	return OUT;
}