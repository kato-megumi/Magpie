// CuNNy 2x4C BILINEAR RGB NVL DN - https://github.com/funnyplanter/CuNNy

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// 
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

//!MAGPIE EFFECT
//!VERSION 4
//!SORT_NAME CuNNy-DN-D04N02
//!USE MulAdd
//!CAPABILITY FP16

#include "../StubDefs.hlsli"

//!TEXTURE
Texture2D INPUT;

//!TEXTURE
//!WIDTH INPUT_WIDTH * 2
//!HEIGHT INPUT_HEIGHT * 2
Texture2D OUTPUT;

//!SAMPLER
//!FILTER POINT
SamplerState SP;

//!SAMPLER
//!FILTER LINEAR
SamplerState SL;

//!COMMON
#define O(t, p) t.SampleLevel(SP, pos + p * pt, 0)
#define V4 MF4
#define M4 MF4x4

//!TEXTURE
//!WIDTH INPUT_WIDTH
//!HEIGHT INPUT_HEIGHT
//!FORMAT R8G8B8A8_SNORM
Texture2D t0;

//!TEXTURE
//!WIDTH INPUT_WIDTH
//!HEIGHT INPUT_HEIGHT
//!FORMAT R8G8B8A8_SNORM
Texture2D t1;

//!PASS 1
//!DESC in
//!BLOCK_SIZE 8
//!NUM_THREADS 64
//!IN INPUT
//!OUT t0

#define l0(x, y) (dot(MF3(-3.725e-01, -7.046e-01, -1.734e-01), O(INPUT, float2(x, y)).rgb) + MF(1.169e-01))

V4 f0(MF s0_0, MF s0_1, MF s0_2, MF s0_3, MF s0_4, MF s0_5, MF s0_6, MF s0_7, MF s0_8) {
	V4 r = { 1.492e-02, -1.961e-02, -7.539e-03, -3.574e-03 };
	r = mad(s0_0, V4(-2.745e-03, -2.925e-03, 1.135e-01, 3.162e-02), r);
	r = mad(s0_1, V4(4.049e-03, -3.428e-01, -7.641e-02, 2.484e-02), r);
	r = mad(s0_2, V4(-8.372e-03, 3.398e-01, 1.072e-01, -5.449e-02), r);
	r = mad(s0_3, V4(1.592e-02, 1.884e-02, -3.160e-02, -7.727e-02), r);
	r = mad(s0_4, V4(4.429e-01, -3.936e-01, -4.134e-01, -4.287e-01), r);
	r = mad(s0_5, V4(4.556e-02, 3.754e-01, -2.300e-02, 4.971e-01), r);
	r = mad(s0_6, V4(-2.031e-02, -6.662e-03, 8.906e-02, 4.602e-02), r);
	r = mad(s0_7, V4(-4.365e-01, 2.183e-03, 8.609e-02, 9.402e-03), r);
	r = mad(s0_8, V4(-3.845e-02, 5.695e-03, 9.645e-02, -5.310e-02), r);
	return r;
}

void Pass1(uint2 blockStart, uint3 tid) {
	float2 pt = float2(GetInputPt());
	uint2 gxy = Rmp8x8(tid.x) + blockStart;
	uint2 size = GetInputSize();
	if (gxy.x >= size.x || gxy.y >= size.y) {
		return;
	}
	float2 pos = (gxy + 0.5) * pt;

	MF s0_0 = l0(-1.0, -1.0);
	MF s0_1 = l0(0.0, -1.0);
	MF s0_2 = l0(1.0, -1.0);
	MF s0_3 = l0(-1.0, 0.0);
	MF s0_4 = l0(0.0, 0.0);
	MF s0_5 = l0(1.0, 0.0);
	MF s0_6 = l0(-1.0, 1.0);
	MF s0_7 = l0(0.0, 1.0);
	MF s0_8 = l0(1.0, 1.0);

	t0[gxy] = f0(s0_0, s0_1, s0_2, s0_3, s0_4, s0_5, s0_6, s0_7, s0_8);
}

//!PASS 2
//!DESC conv1
//!BLOCK_SIZE 8
//!NUM_THREADS 64
//!IN t0
//!OUT t1

#define l0(x, y) V4(O(t0, float2(x, y)))

V4 f0(V4 s0_0, V4 s0_1, V4 s0_2, V4 s0_3, V4 s0_4, V4 s0_5, V4 s0_6, V4 s0_7, V4 s0_8, V4 s1_0, V4 s1_1, V4 s1_2, V4 s1_3, V4 s1_4, V4 s1_5, V4 s1_6, V4 s1_7, V4 s1_8) {
	V4 r = { 4.789e-02, 4.713e-03, -2.854e-02, 9.967e-03 };
	r = MulAdd(s0_0, M4(1.218e-02, -1.208e-01, -1.955e-01, -1.217e-01, 3.123e-02, -2.317e-02, 1.961e-01, -9.984e-02, 3.038e-03, 2.863e-02, -1.042e-01, -5.529e-02, 1.266e-01, -3.877e-01, 2.315e-01, -1.334e-01), r);
	r = MulAdd(s0_1, M4(-1.774e-02, 1.636e-01, 1.379e-01, 7.499e-03, -7.890e-02, -3.970e-02, -6.053e-02, -1.431e-02, 4.167e-02, 9.728e-02, 3.825e-02, -2.704e-02, -2.303e-01, -3.348e-01, 2.940e-01, 4.825e-02), r);
	r = MulAdd(s0_2, M4(1.239e-02, 1.613e-02, -2.280e-01, 8.985e-02, 2.106e-03, 3.847e-02, -2.539e-02, -3.326e-02, -6.327e-02, -1.427e-01, 4.218e-02, 8.995e-02, -6.045e-02, -1.073e-01, -1.329e-01, -2.085e-02), r);
	r = MulAdd(s0_3, M4(-1.601e-01, -2.448e-01, -3.950e-01, 9.169e-03, -3.694e-02, 2.018e-01, -2.524e-01, 1.719e+00, 3.009e-02, 4.927e-02, 1.564e-01, 3.509e-02, -2.630e-02, -3.986e-01, 1.326e-01, -1.037e-02), r);
	r = MulAdd(s0_4, M4(-1.074e+00, -1.654e-01, 4.163e-01, 3.816e-02, 4.580e-01, 4.350e-01, -3.490e-01, -1.257e-02, 1.159e-02, -2.083e-01, -2.744e-01, -2.667e-02, 2.826e-03, 1.986e-01, -2.723e-01, 9.612e-02), r);
	r = MulAdd(s0_5, M4(-3.195e-01, -1.450e-01, -1.523e-01, -2.999e-03, 1.166e-01, 1.304e-01, 1.475e-01, 7.286e-02, -4.077e-02, -3.477e-02, 1.496e-01, -1.199e-02, 7.881e-02, 8.911e-02, -1.082e-01, -6.762e-02), r);
	r = MulAdd(s0_6, M4(2.020e-02, 1.556e-01, -9.837e-03, 1.537e-02, -1.047e-01, 2.095e-01, 2.025e-01, -3.522e-02, -3.407e-02, -8.949e-02, -7.721e-02, -8.910e-03, 9.305e-02, 2.231e-01, 2.178e-01, 1.502e-02), r);
	r = MulAdd(s0_7, M4(-7.936e-02, 3.096e-01, 1.869e-01, -1.950e-03, -2.452e-01, -5.098e-01, 5.304e-01, -4.921e-02, -1.073e-01, 1.062e-01, 2.527e-01, 5.909e-04, 3.797e-02, 3.291e-01, -2.395e-01, 2.768e-02), r);
	r = MulAdd(s0_8, M4(-5.559e-02, 1.090e-01, -1.757e-01, 1.261e-02, -1.632e-01, -2.476e-01, -5.674e-02, -4.843e-03, 1.064e-02, 1.023e-01, 2.540e-02, -1.336e-02, 1.362e-01, 1.833e-01, 3.772e-03, 5.118e-04), r);
	r = MulAdd(s1_0, M4(1.383e-01, 3.469e-01, 3.568e-02, -1.958e-01, -3.170e-02, -1.076e-02, -2.012e-02, -2.104e-04, 2.046e-02, -1.268e-02, -1.618e-01, -6.370e-02, 2.615e-02, 1.494e-01, -1.523e-01, 3.702e-02), r);
	r = MulAdd(s1_1, M4(-1.140e-02, 6.811e-01, 5.722e-02, 1.514e-01, -6.311e-02, -3.541e-02, -1.150e-01, 3.625e-02, 1.146e-01, -1.395e-03, 5.059e-01, -7.835e-02, -3.907e-01, 6.172e-02, -9.656e-02, -2.727e-02), r);
	r = MulAdd(s1_2, M4(1.239e-01, 1.206e-01, 7.519e-01, 2.106e-02, 8.647e-03, 1.082e-02, 5.931e-02, -4.215e-02, -2.216e-02, -4.829e-02, -1.927e-01, 1.159e-01, -1.789e-01, -9.596e-02, 1.395e-01, -6.395e-02), r);
	r = MulAdd(s1_3, M4(1.194e-01, -5.786e-01, -1.761e-03, -1.126e-02, -5.311e-02, -2.325e-01, 1.733e-01, 2.842e-01, -1.080e-01, -1.012e-01, 1.851e-01, 4.253e-02, 1.212e-01, 2.435e-02, -3.061e-01, -9.579e-02), r);
	r = MulAdd(s1_4, M4(-4.651e-02, -1.299e+00, -5.020e-01, 5.830e-02, 5.098e-01, 7.344e-02, -1.358e-01, 1.725e-02, -2.980e-01, -6.077e-01, 6.308e-01, -4.014e-02, 3.497e-01, 3.700e-01, -6.035e-01, 8.026e-02), r);
	r = MulAdd(s1_5, M4(-1.851e-02, -2.057e-01, 5.081e-01, -5.262e-02, 1.715e-01, 1.387e-01, -1.123e-01, 9.022e-02, -1.532e-01, -3.749e-02, -1.930e-01, 6.423e-02, 2.763e-02, 5.993e-02, 4.141e-01, -8.825e-02), r);
	r = MulAdd(s1_6, M4(-6.324e-03, -9.461e-02, 3.044e-02, -4.139e-03, -2.925e-02, 3.975e-01, 1.161e-01, 9.726e-03, 1.353e-01, 2.762e-01, 3.297e-03, 1.076e-02, -8.503e-02, -7.010e-01, -1.967e-01, -1.360e-03), r);
	r = MulAdd(s1_7, M4(1.873e-02, 1.099e-01, 1.229e-01, -1.232e-02, -5.723e-01, -4.599e-02, -1.236e-01, -2.003e-02, -4.268e-01, 5.929e-01, 2.942e-01, 3.485e-02, 4.326e-01, -9.250e-02, 3.736e-01, -2.393e-02), r);
	r = MulAdd(s1_8, M4(-5.991e-02, 1.199e-03, -1.349e-02, -1.321e-03, -2.036e-01, -1.937e-01, -7.888e-02, -9.144e-03, 1.557e-01, 7.018e-02, -2.646e-01, -3.360e-06, 1.742e-01, 1.814e-01, 1.385e-01, -1.030e-02), r);
	return r;
}

void Pass2(uint2 blockStart, uint3 tid) {
	float2 pt = float2(GetInputPt());
	uint2 gxy = Rmp8x8(tid.x) + blockStart;
	uint2 size = GetInputSize();
	if (gxy.x >= size.x || gxy.y >= size.y) {
		return;
	}
	float2 pos = (gxy + 0.5) * pt;

	V4 s0_0 = l0(-1.0, -1.0);
	V4 s0_1 = l0(0.0, -1.0);
	V4 s0_2 = l0(1.0, -1.0);
	V4 s0_3 = l0(-1.0, 0.0);
	V4 s0_4 = l0(0.0, 0.0);
	V4 s0_5 = l0(1.0, 0.0);
	V4 s0_6 = l0(-1.0, 1.0);
	V4 s0_7 = l0(0.0, 1.0);
	V4 s0_8 = l0(1.0, 1.0);
	V4 s1_0 = -max(-s0_0, 0.0);
	V4 s1_1 = -max(-s0_1, 0.0);
	V4 s1_2 = -max(-s0_2, 0.0);
	V4 s1_3 = -max(-s0_3, 0.0);
	V4 s1_4 = -max(-s0_4, 0.0);
	V4 s1_5 = -max(-s0_5, 0.0);
	V4 s1_6 = -max(-s0_6, 0.0);
	V4 s1_7 = -max(-s0_7, 0.0);
	V4 s1_8 = -max(-s0_8, 0.0);
	s0_0 = max(s0_0, 0.0);
	s0_1 = max(s0_1, 0.0);
	s0_2 = max(s0_2, 0.0);
	s0_3 = max(s0_3, 0.0);
	s0_4 = max(s0_4, 0.0);
	s0_5 = max(s0_5, 0.0);
	s0_6 = max(s0_6, 0.0);
	s0_7 = max(s0_7, 0.0);
	s0_8 = max(s0_8, 0.0);

	t1[gxy] = f0(s0_0, s0_1, s0_2, s0_3, s0_4, s0_5, s0_6, s0_7, s0_8, s1_0, s1_1, s1_2, s1_3, s1_4, s1_5, s1_6, s1_7, s1_8);
}

//!PASS 3
//!DESC conv2
//!BLOCK_SIZE 8
//!NUM_THREADS 64
//!IN t1
//!OUT t0

#define l0(x, y) V4(O(t1, float2(x, y)))

V4 f0(V4 s0_0, V4 s0_1, V4 s0_2, V4 s0_3, V4 s0_4, V4 s0_5, V4 s0_6, V4 s0_7, V4 s0_8, V4 s1_0, V4 s1_1, V4 s1_2, V4 s1_3, V4 s1_4, V4 s1_5, V4 s1_6, V4 s1_7, V4 s1_8) {
	V4 r = { 7.359e-03, -1.132e-02, 1.248e-02, 7.243e-04 };
	r = MulAdd(s0_0, M4(-1.565e-01, 1.307e-02, -5.269e-02, 5.465e-02, 2.936e-01, 1.626e-01, 4.589e-02, 2.478e-02, 3.520e-01, -5.445e-02, -2.480e-01, 2.838e-02, 1.841e-04, 1.264e-02, -1.370e-02, 2.588e-02), r);
	r = MulAdd(s0_1, M4(2.350e-01, 2.116e-01, 2.167e-02, -1.559e-01, 2.502e-01, 4.320e-01, -7.152e-01, 2.270e-01, -2.668e-01, -2.117e-01, 5.598e-01, 2.261e-01, 4.101e-02, -4.860e-02, 3.530e-02, 8.932e-02), r);
	r = MulAdd(s0_2, M4(-4.398e-02, -4.486e-02, -5.040e-02, 9.803e-02, 7.515e-02, 1.203e-01, -5.357e-02, -2.803e-01, -1.435e-01, 7.150e-03, -3.118e-02, -2.636e-01, -2.969e-02, -2.011e-02, 2.658e-02, -2.572e-02), r);
	r = MulAdd(s0_3, M4(9.140e-02, -1.875e-01, 9.757e-02, 2.976e-02, -8.325e-02, 6.109e-02, -4.304e-02, 7.057e-02, 7.324e-01, -1.528e-01, 2.930e-01, 7.503e-02, -3.901e-02, 1.109e-03, -2.693e-02, -3.330e-02), r);
	r = MulAdd(s0_4, M4(-9.944e-02, 1.858e-01, -2.436e-01, 3.822e-02, 6.685e-02, -1.758e-01, 1.382e-01, -1.715e-01, 3.252e-01, 5.176e-01, -2.939e-01, 4.311e-01, -6.125e-02, 1.905e-01, 8.140e-02, 2.095e-01), r);
	r = MulAdd(s0_5, M4(3.193e-02, 6.029e-02, 1.869e-03, 8.627e-04, -1.402e-02, 4.288e-02, -5.756e-02, 8.813e-02, -2.758e-02, -5.267e-02, 1.702e-03, -6.676e-01, 6.373e-02, 5.766e-02, -6.325e-02, -2.744e-01), r);
	r = MulAdd(s0_6, M4(4.918e-02, 5.420e-04, 3.692e-02, 7.796e-03, -1.163e-02, -4.074e-02, 2.057e-02, -2.837e-02, 1.083e-01, 1.958e-01, -5.078e-02, 2.750e-02, 5.323e-02, 5.953e-03, 4.766e-02, -2.265e-03), r);
	r = MulAdd(s0_7, M4(-3.968e-02, -1.535e-01, 6.564e-02, -2.620e-02, 3.742e-02, 8.659e-02, -4.440e-02, 6.007e-03, -9.585e-02, -9.425e-02, -1.517e-01, 3.701e-01, -1.332e-01, -1.860e-01, -5.436e-02, 3.781e-01), r);
	r = MulAdd(s0_8, M4(-1.145e-02, 6.045e-02, -4.676e-02, -5.604e-02, -1.576e-02, -3.528e-02, 2.252e-02, 1.997e-02, -2.546e-02, -6.894e-02, 7.238e-02, -3.495e-01, -6.323e-02, -1.042e-01, 1.091e-01, -4.170e-01), r);
	r = MulAdd(s1_0, M4(-5.215e-01, 6.255e-01, 5.587e-02, -5.362e-02, 9.895e-02, -8.743e-03, 1.058e-01, -3.585e-02, -1.594e-02, -1.034e-01, 3.848e-02, -5.432e-02, -1.796e-02, 5.838e-02, 1.304e-01, -2.122e-02), r);
	r = MulAdd(s1_1, M4(-6.987e-02, 8.696e-01, -1.130e+00, 5.558e-03, -1.080e-01, 4.195e-02, -1.323e-01, 2.270e-01, 3.451e-02, -1.616e-02, 4.251e-03, 1.470e-01, 2.442e-01, -5.904e-02, -3.467e-01, -2.056e-02), r);
	r = MulAdd(s1_2, M4(4.884e-02, -1.034e-01, 5.823e-02, 1.131e-01, -4.126e-02, 6.519e-02, -1.532e-02, -2.420e-01, 1.092e-02, 1.869e-02, 1.913e-03, -1.787e-02, 1.122e-01, -1.481e-01, 1.843e-01, 3.454e-01), r);
	r = MulAdd(s1_3, M4(-2.906e-01, -9.847e-01, 4.092e-01, 1.655e-01, 4.092e-02, 2.913e-01, 1.306e-01, -4.682e-02, 2.568e-01, -4.528e-02, 3.207e-02, 9.888e-02, -3.928e-01, -3.546e-01, -2.367e-01, -3.239e-01), r);
	r = MulAdd(s1_4, M4(4.463e-01, -1.594e-01, 8.418e-01, -3.525e-01, 5.957e-01, 1.082e+00, -9.245e-01, 2.726e-01, 1.210e-01, 2.024e-01, -8.063e-03, -2.433e-01, -1.512e+00, 9.316e-01, 2.305e-01, -5.109e-01), r);
	r = MulAdd(s1_5, M4(-2.393e-02, 1.286e-02, -9.453e-02, 3.071e-01, -1.402e-01, -2.436e-01, 1.202e-01, -1.409e-01, -1.857e-02, 2.421e-02, -2.642e-02, -7.415e-02, 8.786e-01, 5.260e-04, -9.212e-02, 1.849e-01), r);
	r = MulAdd(s1_6, M4(8.958e-02, 9.057e-02, 1.712e-02, -2.838e-02, -1.405e-01, -6.455e-02, -2.695e-02, -1.110e-02, 8.731e-03, 6.531e-02, -3.752e-02, 1.194e-01, 4.585e-01, 6.270e-01, -1.367e-01, -2.529e-01), r);
	r = MulAdd(s1_7, M4(-4.381e-02, -1.595e-02, -4.601e-02, 7.257e-02, -8.036e-02, -1.360e-01, 1.154e-01, -7.942e-02, -4.653e-02, -7.121e-02, 2.720e-02, 8.346e-02, -1.871e+00, -8.300e-01, -6.760e-01, 7.402e-01), r);
	r = MulAdd(s1_8, M4(1.359e-02, -2.489e-02, 3.529e-02, -1.121e-01, -6.190e-02, -2.628e-02, -2.090e-03, 2.359e-01, -2.412e-02, -2.463e-02, 8.317e-03, -5.330e-02, 2.105e+00, 1.550e-01, 1.457e+00, -1.129e+00), r);
	return r;
}

void Pass3(uint2 blockStart, uint3 tid) {
	float2 pt = float2(GetInputPt());
	uint2 gxy = Rmp8x8(tid.x) + blockStart;
	uint2 size = GetInputSize();
	if (gxy.x >= size.x || gxy.y >= size.y) {
		return;
	}
	float2 pos = (gxy + 0.5) * pt;

	V4 s0_0 = l0(-1.0, -1.0);
	V4 s0_1 = l0(0.0, -1.0);
	V4 s0_2 = l0(1.0, -1.0);
	V4 s0_3 = l0(-1.0, 0.0);
	V4 s0_4 = l0(0.0, 0.0);
	V4 s0_5 = l0(1.0, 0.0);
	V4 s0_6 = l0(-1.0, 1.0);
	V4 s0_7 = l0(0.0, 1.0);
	V4 s0_8 = l0(1.0, 1.0);
	V4 s1_0 = -max(-s0_0, 0.0);
	V4 s1_1 = -max(-s0_1, 0.0);
	V4 s1_2 = -max(-s0_2, 0.0);
	V4 s1_3 = -max(-s0_3, 0.0);
	V4 s1_4 = -max(-s0_4, 0.0);
	V4 s1_5 = -max(-s0_5, 0.0);
	V4 s1_6 = -max(-s0_6, 0.0);
	V4 s1_7 = -max(-s0_7, 0.0);
	V4 s1_8 = -max(-s0_8, 0.0);
	s0_0 = max(s0_0, 0.0);
	s0_1 = max(s0_1, 0.0);
	s0_2 = max(s0_2, 0.0);
	s0_3 = max(s0_3, 0.0);
	s0_4 = max(s0_4, 0.0);
	s0_5 = max(s0_5, 0.0);
	s0_6 = max(s0_6, 0.0);
	s0_7 = max(s0_7, 0.0);
	s0_8 = max(s0_8, 0.0);

	t0[gxy] = f0(s0_0, s0_1, s0_2, s0_3, s0_4, s0_5, s0_6, s0_7, s0_8, s1_0, s1_1, s1_2, s1_3, s1_4, s1_5, s1_6, s1_7, s1_8);
}

//!PASS 4
//!DESC out-shuffle
//!BLOCK_SIZE 16
//!NUM_THREADS 64
//!IN INPUT, t0
//!OUT OUTPUT

#define l0(x, y) V4(O(t0, float2(x, y)))

V4 f0(V4 s0_0, V4 s0_1, V4 s0_2, V4 s0_3, V4 s0_4, V4 s0_5, V4 s0_6, V4 s0_7, V4 s0_8, V4 s1_0, V4 s1_1, V4 s1_2, V4 s1_3, V4 s1_4, V4 s1_5, V4 s1_6, V4 s1_7, V4 s1_8) {
	V4 r = { -7.528e-04, -8.388e-04, -1.247e-03, -1.205e-03 };
	r = MulAdd(s0_0, M4(8.642e-03, -1.295e-02, 1.998e-02, -1.289e-03, -4.147e-02, -4.021e-03, 1.491e-04, -7.275e-03, 1.574e-02, -4.122e-03, 1.126e-02, 8.962e-03, 5.174e-02, 3.405e-02, 4.993e-02, 4.529e-02), r);
	r = MulAdd(s0_1, M4(-1.028e-01, -2.764e-02, -2.777e-02, -7.170e-03, -8.365e-02, 3.550e-02, 1.288e-01, 2.475e-02, 5.017e-02, 5.917e-02, 3.473e-02, 8.510e-03, 2.332e-02, 8.047e-02, 9.838e-02, 4.234e-02), r);
	r = MulAdd(s0_2, M4(-2.319e-02, -4.432e-02, -1.679e-02, 8.855e-03, 3.259e-02, -1.974e-01, 5.938e-02, 1.616e-01, -5.605e-04, 3.183e-02, -3.356e-03, 3.138e-02, 9.572e-03, -3.887e-02, -2.632e-02, -1.161e-02), r);
	r = MulAdd(s0_3, M4(-2.947e-02, -4.358e-02, 1.208e-03, -2.705e-02, -1.037e-02, -6.812e-02, -5.436e-02, -3.840e-02, 3.684e-02, 2.560e-02, 1.715e-02, -3.670e-02, -5.930e-02, -2.310e-02, -6.163e-02, -3.562e-02), r);
	r = MulAdd(s0_4, M4(5.520e-01, 1.213e-01, 1.753e-01, 5.436e-02, 5.879e-01, 2.281e-01, -2.703e-01, 1.519e-01, 5.739e-01, 2.959e-01, 9.449e-02, 2.473e-02, -5.998e-01, -9.548e-02, -6.035e-01, -9.663e-02), r);
	r = MulAdd(s0_5, M4(-9.740e-02, 2.744e-01, -1.522e-01, -7.204e-02, 1.178e-01, 6.112e-01, -4.801e-02, -5.176e-01, 1.480e-02, 8.323e-02, -6.764e-02, 4.138e-02, 1.121e-01, -8.141e-02, 1.211e-01, -8.737e-02), r);
	r = MulAdd(s0_6, M4(6.315e-02, 6.323e-02, 1.146e-02, 3.378e-02, -9.598e-02, -1.089e-01, 2.780e-02, -6.091e-02, -1.194e-01, -1.038e-01, -2.147e-02, -4.236e-02, -2.300e-02, -3.184e-02, -1.560e-02, -2.206e-02), r);
	r = MulAdd(s0_7, M4(-1.772e-01, -1.304e-01, 1.265e-01, -7.871e-02, 1.978e-01, 1.074e-01, 1.240e-02, 4.600e-02, 1.558e-02, -3.196e-02, 2.018e-01, 1.496e-01, 1.421e-01, 8.472e-02, 7.432e-02, 9.935e-02), r);
	r = MulAdd(s0_8, M4(1.132e-02, -2.296e-03, 1.274e-01, 3.428e-01, -5.796e-02, -6.156e-02, -2.549e-01, -2.231e-01, -8.762e-02, -9.318e-02, -2.378e-01, -3.018e-01, 5.601e-03, -2.670e-02, 2.896e-02, -3.910e-02), r);
	r = MulAdd(s1_0, M4(4.603e-02, -2.582e-02, -9.045e-03, 1.446e-02, -1.835e-02, -2.533e-02, 3.681e-03, -9.420e-03, -5.802e-02, 2.310e-02, 3.059e-02, 1.313e-03, 9.639e-02, 8.284e-02, 1.071e-01, -3.287e-02), r);
	r = MulAdd(s1_1, M4(-2.480e-02, 2.321e-03, -3.594e-02, -1.101e-01, 2.850e-02, 2.912e-02, 2.597e-02, 2.777e-02, 5.701e-02, 9.536e-04, 2.533e-02, 1.102e-02, -3.714e-03, 7.838e-02, -1.716e-02, 1.723e-01), r);
	r = MulAdd(s1_2, M4(-4.473e-03, 1.521e-02, -1.887e-02, 6.731e-03, 2.199e-03, 2.965e-02, -3.709e-03, 1.671e-02, 1.376e-02, -4.819e-02, -8.832e-04, 3.531e-02, -8.453e-03, -1.276e-02, -1.461e-02, 4.460e-03), r);
	r = MulAdd(s1_3, M4(6.139e-02, -1.511e-01, 1.102e-01, -1.428e-01, -5.114e-02, -6.594e-02, -1.693e-02, -4.651e-02, 2.440e-01, 2.010e-02, -1.900e-01, -1.243e-03, -2.397e-01, 2.002e-01, -3.506e-01, 2.171e-01), r);
	r = MulAdd(s1_4, M4(-6.189e-02, 5.137e-01, -8.132e-02, 4.526e-01, 3.263e-01, 2.134e-01, 1.027e-01, 2.067e-02, 2.407e-01, 2.591e-01, 4.489e-01, 2.042e-01, 1.932e-02, -4.463e-01, -1.479e-01, -6.843e-01), r);
	r = MulAdd(s1_5, M4(-7.571e-03, -7.787e-02, 9.918e-03, -8.469e-02, 4.056e-02, -1.926e-02, -4.968e-02, 2.416e-02, 2.699e-02, 2.783e-01, -7.854e-02, -6.549e-02, 6.835e-03, 2.288e-02, 1.048e-02, -3.273e-02), r);
	r = MulAdd(s1_6, M4(7.034e-02, 4.236e-02, 7.905e-02, -2.283e-03, -8.423e-02, -7.784e-02, -7.540e-03, -3.373e-02, -1.019e-01, -1.421e-01, 6.713e-02, -8.716e-02, -6.980e-02, -4.731e-02, -3.086e-02, -6.210e-03), r);
	r = MulAdd(s1_7, M4(-1.597e-01, -2.036e-01, 5.194e-02, 8.457e-02, 1.387e-01, 7.910e-02, 2.030e-02, 5.848e-02, 2.154e-01, 1.382e-01, -8.617e-02, 7.552e-02, 3.127e-02, 5.899e-02, 1.733e-01, 1.657e-01), r);
	r = MulAdd(s1_8, M4(3.595e-02, 3.243e-02, 1.450e-01, 2.046e-01, -2.939e-02, -1.306e-02, -1.587e-01, -2.607e-01, -8.980e-02, -5.350e-02, -2.627e-01, -2.861e-01, -1.585e-02, -2.032e-02, -1.662e-02, 1.560e-02), r);
	return tanh(r);
}

void Pass4(uint2 blockStart, uint3 tid) {
	float2 pt = float2(GetInputPt());
	uint2 gxy = (Rmp8x8(tid.x) << 1) + blockStart;
	uint2 size = GetOutputSize();
	if (gxy.x >= size.x || gxy.y >= size.y) {
		return;
	}
	float2 pos = ((gxy >> 1) + 0.5) * pt;

	V4 s0_0 = l0(-1.0, -1.0);
	V4 s0_1 = l0(0.0, -1.0);
	V4 s0_2 = l0(1.0, -1.0);
	V4 s0_3 = l0(-1.0, 0.0);
	V4 s0_4 = l0(0.0, 0.0);
	V4 s0_5 = l0(1.0, 0.0);
	V4 s0_6 = l0(-1.0, 1.0);
	V4 s0_7 = l0(0.0, 1.0);
	V4 s0_8 = l0(1.0, 1.0);
	V4 s1_0 = -max(-s0_0, 0.0);
	V4 s1_1 = -max(-s0_1, 0.0);
	V4 s1_2 = -max(-s0_2, 0.0);
	V4 s1_3 = -max(-s0_3, 0.0);
	V4 s1_4 = -max(-s0_4, 0.0);
	V4 s1_5 = -max(-s0_5, 0.0);
	V4 s1_6 = -max(-s0_6, 0.0);
	V4 s1_7 = -max(-s0_7, 0.0);
	V4 s1_8 = -max(-s0_8, 0.0);
	s0_0 = max(s0_0, 0.0);
	s0_1 = max(s0_1, 0.0);
	s0_2 = max(s0_2, 0.0);
	s0_3 = max(s0_3, 0.0);
	s0_4 = max(s0_4, 0.0);
	s0_5 = max(s0_5, 0.0);
	s0_6 = max(s0_6, 0.0);
	s0_7 = max(s0_7, 0.0);
	s0_8 = max(s0_8, 0.0);

	V4 r = f0(s0_0, s0_1, s0_2, s0_3, s0_4, s0_5, s0_6, s0_7, s0_8, s1_0, s1_1, s1_2, s1_3, s1_4, s1_5, s1_6, s1_7, s1_8);

	static const MF3x3 rgb2yuv = { 0.299, 0.587, 0.114, -0.169, -0.331, 0.5, 0.5, -0.419, -0.081 };
	static const MF3x3 yuv2rgb = { 1, -0.00093, 1.401687, 1, -0.3437, -0.71417, 1, 1.77216, 0.00099 };
	float2 opt = float2(GetOutputPt());

	pos -= 0.5f * opt;
	MF3 yuv = mul(rgb2yuv, INPUT.SampleLevel(SL, pos, 0).rgb);
	OUTPUT[gxy] = MF4(mul(yuv2rgb, MF3(saturate(yuv.r + r.x), yuv.yz)), 1);

	++gxy.x;
	pos.x += opt.x;
	yuv = mul(rgb2yuv, INPUT.SampleLevel(SL, pos, 0).rgb);
	OUTPUT[gxy] = MF4(mul(yuv2rgb, MF3(saturate(yuv.r + r.y), yuv.yz)), 1);

	++gxy.y;
	pos.y += opt.y;
	yuv = mul(rgb2yuv, INPUT.SampleLevel(SL, pos, 0).rgb);
	OUTPUT[gxy] = MF4(mul(yuv2rgb, MF3(saturate(yuv.r + r.w), yuv.yz)), 1);

	--gxy.x;
	pos.x -= opt.x;
	yuv = mul(rgb2yuv, INPUT.SampleLevel(SL, pos, 0).rgb);
	OUTPUT[gxy] = MF4(mul(yuv2rgb, MF3(saturate(yuv.r + r.z), yuv.yz)), 1);
}
