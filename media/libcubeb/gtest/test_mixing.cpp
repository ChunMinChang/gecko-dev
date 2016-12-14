/*
 * Copyright © 2016 Mozilla Foundation
 *
 * This program is made available under an ISC-style license.  See the
 * accompanying file LICENSE for details.
 */
#include "gtest/gtest.h"
#include "cubeb/cubeb.h"
#include "cubeb_mixing.h"

#define NELEMS(x) ((int) (sizeof(x) / sizeof(x[0])))

#define STREAM_FREQUENCY 48000
#define STREAM_FORMAT CUBEB_SAMPLE_FLOAT32LE

#define M 1.0f    // Mono
#define L 2.0f    // Left
#define R 3.0f    // Right
#define C 4.0f    // Center
#define LS 5.0f   // Left Surround
#define RS 6.0f   // Right Surround
#define RLS 7.0f  // Rear Left Surround
#define RC 8.0f   // Rear Center
#define RRS 9.0f  // Rear Right Surround
#define LFE 10.0f // Low Frequency Effects

#define D3F2_RESULT_IN_INDEX(x) (x - CUBEB_LAYOUT_3F2)
#define D3F2_RESULT_OUT_INDEX(x) (x - CUBEB_LAYOUT_MONO)
#define SQRT_1_2 0.707106f // 1/sqrt(2)
static const float downmix_3f2_results[2][12][5] = {
  // 3F2
  {
    { SQRT_1_2*(L+R) + C + 0.5*(LS+RS) },                       // Mono
    { SQRT_1_2*(L+R) + C + 0.5*(LS+RS), 0 },                    // Mono-LFE
    { L + SQRT_1_2*(C+LS), R + SQRT_1_2*(C+RS) },               // Stereo
    { L + SQRT_1_2*(C+LS), R + SQRT_1_2*(C+RS), 0 },            // Stereo-LFE
    { L + SQRT_1_2*LS, R + SQRT_1_2*RS, C },                    // 3F
    { L + SQRT_1_2*LS, R + SQRT_1_2*RS, C, 0 },                 // 3F-LFE
    { L + C*SQRT_1_2, R + C*SQRT_1_2, SQRT_1_2*(LS+RS) },       // 2F1
    { L + C*SQRT_1_2, R + C*SQRT_1_2, 0, SQRT_1_2*(LS+RS) },    // 2F1-LFE
    { L, R, C, SQRT_1_2*(LS+RS) },                              // 3F1
    { L, R, C, 0, SQRT_1_2*(LS+RS) },                           // 3F1-LFE
    { L + SQRT_1_2*C, R + SQRT_1_2*C, LS, RS },                 // 2F2
    { L + SQRT_1_2*C, R + SQRT_1_2*C, 0, LS, RS }               // 2F2-LFE
  },
  // 3F2-LFE
  {
    { SQRT_1_2*(L+R) + C + 0.5*(LS+RS) },                       // Mono
    { SQRT_1_2*(L+R) + C + 0.5*(LS+RS), LFE },                  // Mono-LFE
    { L + SQRT_1_2*(C+LS), R + SQRT_1_2*(C+RS) },               // Stereo
    { L + SQRT_1_2*(C+LS), R + SQRT_1_2*(C+RS), LFE },          // Stereo-LFE
    { L + SQRT_1_2*LS, R + SQRT_1_2*RS, C },                    // 3F
    { L + SQRT_1_2*LS, R + SQRT_1_2*RS, C, LFE },               // 3F-LFE
    { L + C*SQRT_1_2, R + C*SQRT_1_2, SQRT_1_2*(LS+RS) },       // 2F1
    { L + C*SQRT_1_2, R + C*SQRT_1_2, LFE, SQRT_1_2*(LS+RS) },  // 2F1-LFE
    { L, R, C, SQRT_1_2*(LS+RS) },                              // 3F1
    { L, R, C, LFE, SQRT_1_2*(LS+RS) },                         // 3F1-LFE
    { L + SQRT_1_2*C, R + SQRT_1_2*C, LS, RS },                 // 2F2
    { L + SQRT_1_2*C, R + SQRT_1_2*C, LFE, LS, RS }             // 2F2-LFE
  }
};

typedef struct {
  cubeb_channel_layout layout;
  float data[10];
} audio_input;

audio_input audio_inputs[CUBEB_LAYOUT_MAX] = {
  { CUBEB_LAYOUT_UNSUPPORTED,   { } },
  { CUBEB_LAYOUT_DUAL_MONO,     { L, R } },
  { CUBEB_LAYOUT_DUAL_MONO_LFE, { L, R, LFE } },
  { CUBEB_LAYOUT_MONO,          { M } },
  { CUBEB_LAYOUT_MONO_LFE,      { M, LFE } },
  { CUBEB_LAYOUT_STEREO,        { L, R } },
  { CUBEB_LAYOUT_STEREO_LFE,    { L, R, LFE } },
  { CUBEB_LAYOUT_3F,            { L, R, C } },
  { CUBEB_LAYOUT_3F_LFE,        { L, R, C, LFE } },
  { CUBEB_LAYOUT_2F1,           { L, R, RC } },
  { CUBEB_LAYOUT_2F1_LFE,       { L, R, LFE, RC } },
  { CUBEB_LAYOUT_3F1,           { L, R, C, RC } },
  { CUBEB_LAYOUT_3F1_LFE,       { L, R, C, LFE, RC } },
  { CUBEB_LAYOUT_2F2,           { L, R, LS, RS } },
  { CUBEB_LAYOUT_2F2_LFE,       { L, R, LFE, LS, RS } },
  { CUBEB_LAYOUT_3F2,           { L, R, C, LS, RS } },
  { CUBEB_LAYOUT_3F2_LFE,       { L, R, C, LFE, LS, RS } },
  { CUBEB_LAYOUT_3F3R_LFE,      { L, R, C, LFE, RC, LS, RS } },
  { CUBEB_LAYOUT_3F4_LFE,       { L, R, C, LFE, RLS, RRS, LS, RS } }
};

void downmix_test(const float* data, const cubeb_channel_layout in_layout, const cubeb_channel_layout out_layout) {
  if (in_layout == CUBEB_LAYOUT_UNSUPPORTED) {
    return; // Only output layout could be unsupported.
  }

  cubeb_stream_params in_params = {
    STREAM_FORMAT,
    STREAM_FREQUENCY,
    CUBEB_CHANNEL_LAYOUT_MAPS[in_layout].channels,
    in_layout
  };

  cubeb_stream_params out_params = {
    STREAM_FORMAT, STREAM_FREQUENCY,
    // To downmix audio data with unsupported layout, its channel number must be
    // smaller than or equal to the input channels.
    (out_layout == CUBEB_LAYOUT_UNSUPPORTED) ?
      CUBEB_CHANNEL_LAYOUT_MAPS[in_layout].channels : CUBEB_CHANNEL_LAYOUT_MAPS[out_layout].channels,
    out_layout
   };

  if (!cubeb_should_downmix(&in_params, &out_params)) {
    return;
  }

  fprintf(stderr, "Downmix from %s to %s\n", CUBEB_CHANNEL_LAYOUT_MAPS[in_layout].name, CUBEB_CHANNEL_LAYOUT_MAPS[out_layout].name);

  const unsigned int inframes = 10;
  float* in = (float*) malloc(sizeof(float) * in_params.channels * inframes);
  float* out = (float*) malloc(sizeof(float) * out_params.channels * inframes);

  for (unsigned int offset = 0 ; offset < inframes * in_params.channels ; offset += in_params.channels) {
    for (unsigned int i = 0 ; i < in_params.channels ; ++i) {
      in[offset + i] = data[i];
    }
  }

  cubeb_downmix_float(reinterpret_cast<const float*>(in), inframes, out, in_params.channels, out_params.channels, in_params.layout, out_params.layout);

  uint32_t in_layout_mask = 0;
  for (unsigned int i = 0 ; i < in_params.channels; ++i) {
    in_layout_mask |= 1 << CHANNEL_INDEX_TO_ORDER[in_layout][i];
  }

  uint32_t out_layout_mask = 0;
  for (unsigned int i = 0 ; out_layout != CUBEB_LAYOUT_UNSUPPORTED && i < out_params.channels; ++i) {
    out_layout_mask |= 1 << CHANNEL_INDEX_TO_ORDER[out_layout][i];
  }

  for (unsigned int i = 0 ; i < inframes * out_params.channels ; ++i) {
    unsigned int index = i % out_params.channels;

    // downmix_3f2
    if ((in_layout == CUBEB_LAYOUT_3F2 || in_layout == CUBEB_LAYOUT_3F2_LFE) &&
        out_layout >= CUBEB_LAYOUT_MONO && out_layout <= CUBEB_LAYOUT_2F2_LFE) {
      fprintf(stderr, "[3f2] Expect: %lf, Get: %lf\n", downmix_3f2_results[D3F2_RESULT_IN_INDEX(in_layout)][D3F2_RESULT_OUT_INDEX(out_layout)][index], out[index]);
      ASSERT_EQ(out[index], downmix_3f2_results[D3F2_RESULT_IN_INDEX(in_layout)][D3F2_RESULT_OUT_INDEX(out_layout)][index]);
      continue;
    }

    // mix_map_channels
    if (out_layout_mask & in_layout_mask) {
      uint32_t mask = 1 << CHANNEL_INDEX_TO_ORDER[out_layout][index];
      fprintf(stderr, "[bypass] Expect: %lf, Get: %lf\n", (mask & in_layout_mask) ? audio_inputs[out_layout].data[index] : 0, out[index]);
      ASSERT_EQ(out[index], (mask & in_layout_mask) ? audio_inputs[out_layout].data[index] : 0);
      continue;
    }

    // downmix_fallback
    fprintf(stderr, "[fallback] Expect: %lf, Get: %lf\n", audio_inputs[in_layout].data[index], out[index]);
    ASSERT_EQ(out[index], audio_inputs[in_layout].data[index]);
  }

  free(out);
  free(in);
}

TEST(cubeb, run_mixing_test)
{
  for (int i = 0 ; i < NELEMS(audio_inputs) ; ++i) {
    for (int j = 0 ; j < NELEMS(CUBEB_CHANNEL_LAYOUT_MAPS) ; ++j) {
      downmix_test(audio_inputs[i].data, audio_inputs[i].layout, CUBEB_CHANNEL_LAYOUT_MAPS[j].layout);
    }
  }
}
