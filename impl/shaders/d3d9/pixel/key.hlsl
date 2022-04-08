#include "../include/types.hlsli"

PS_OUTPUT main(VS_OUTPUT IN)
{
	PS_OUTPUT OUT;
	
	if (samplerAvailable)
	{
		OUT.color = tex2D(curtex, IN.texcoord0);
		OUT.color.a = 1;  // really ghetto fix
		OUT.color *= IN.color0;
	}
	else
	{
		OUT.color = IN.color0;
	}

	if (OUT.color.r == key.r && OUT.color.g == key.g && OUT.color.b == key.b)
	{
		OUT.color = float4(0, 0, 0, 0);
	}

	return OUT;
}