#include "../include/types.hlsli"

float4 main(VS_OUTPUT IN) : SV_TARGET
{
	float4 col;
	
    col = IN.color0 * texture0.Sample(curtex, IN.texcoord0);

    if (col.r == key_color.r && col.g == key_color.g && col.b == key_color.b)
	{
        col = float4(0, 0, 0, 0);
    }

    return col;
}