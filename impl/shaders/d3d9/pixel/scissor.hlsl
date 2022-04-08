#include "../include/types.hlsli"

PS_OUTPUT main(VS_OUTPUT IN)
{
	PS_OUTPUT OUT;

	float2 center  = float2(scissor.x, scissor.y);
	float2 pos     = float2(IN.screenPos.x, IN.screenPos.y);
	float2 distVec = center - pos;
	float distSqr  = dot(distVec, distVec);

	if (distSqr > scissor.z)
	{
		OUT.color = float4(0, 0, 0, 0);
		return OUT;
	}

	float distToEdge = sqrt(scissor.z) - sqrt(distSqr);
	float alpha      = saturate(distToEdge);
	if (samplerAvailable)
	{
		OUT.color   = tex2D(curtex, IN.texcoord0);
	} else
	{
		OUT.color = IN.color0;
	}
	OUT.color.a *= alpha;
	return OUT;
}