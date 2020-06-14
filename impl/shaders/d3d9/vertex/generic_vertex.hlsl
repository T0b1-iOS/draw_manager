#include "../include/types.hlsli"

VS_OUTPUT main(VS_INPUT IN)
{
	VS_OUTPUT OUT;

	float4 v      = float4(IN.position.x, IN.position.y, IN.position.z, 1.0f);
	float4 tmp    = mul(v, worldViewProj);
	OUT.position  = tmp;
	OUT.hposition = tmp;
	OUT.color0    = IN.color0;
	OUT.texcoord0 = IN.texcoord0;
	OUT.screenPos = float2(IN.position.x, IN.position.y);

	return OUT;
}