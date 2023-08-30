#include "../include/types.hlsli"

float4 main(VS_OUTPUT IN) : SV_TARGET
{
    float4 col;

	float2 center  = float2(scissor.x, scissor.y);
	float2 pos     = float2(IN.screenPos.x, IN.screenPos.y);
	float2 distVec = center - pos;
	float distSqr  = dot(distVec, distVec);

	if (distSqr > scissor.z)
	{
        col = float4(0, 0, 0, 0);
        return col;
    }

	float distToEdge = sqrt(scissor.z) - sqrt(distSqr);
	float alpha      = saturate(distToEdge);

    col = IN.color0 * texture0.Sample(curtex, IN.texcoord0);

    if (col.r == key_color.r && col.g == key_color.g && col.b == key_color.b)
	{
        col = float4(0, 0, 0, 0);
    } else
	{
        col.a *= alpha;
    }

	return col;
}