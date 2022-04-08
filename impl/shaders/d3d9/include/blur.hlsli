float iteration_count : register(c13);
float4 blur_weights_packed[24] : register(c14);
float4 blur_offsets_packed[24] : register(c38);

static float blur_weights[96] = (float[96]) blur_weights_packed;
static float blur_offsets[96] = (float[96]) blur_offsets_packed;

// we could make this even faster by using the weighted offsets from https://www.rastergrid.com/blog/2010/09/efficient-gaussian-blur-with-linear-sampling/
float3 blur(float2 pos, bool x_dir) {
	float2 step = float2(1 / dimension.x, 1 / dimension.y);

	float3 col = tex2D(backbuffer, pos).rgb * blur_weights[0];
	float2 test = float2(ddx(pos).x, ddy(pos).y);

	int it = int(iteration_count);

	if (x_dir) {
		[loop] for (int i = 1; i < it; ++i) {
			col += tex2Dgrad(backbuffer, pos + float2(step.x * int(blur_offsets[i]), 0), test.x, test.y).rgb * blur_weights[i];
			col += tex2Dgrad(backbuffer, pos - float2(step.x * int(blur_offsets[i]), 0), test.x, test.y).rgb * blur_weights[i];
		}
	}
	else {
		[loop] for (int i = 1; i < it; ++i) {
			col += tex2Dgrad(backbuffer, pos + float2(0, step.y * int(blur_offsets[i])), test.x, test.y).rgb * blur_weights[i];
			col += tex2Dgrad(backbuffer, pos - float2(0, step.y * int(blur_offsets[i])), test.x, test.y).rgb * blur_weights[i];
		}
	}

	return col;
}