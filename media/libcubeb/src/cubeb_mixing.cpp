/*
 * Copyright © 2016 Mozilla Foundation
 *
 * This program is made available under an ISC-style license.  See the
 * accompanying file LICENSE for details.
 */

#include <cassert>
#include "cubeb_mixing.h"

static const int CHANNEL_ORDER_TO_INDEX[CUBEB_LAYOUT_MAX][CHANNEL_MAX] = {
  // M | LF | RF | CEN | LS | RS | RLS | RC | RRS | LFE
  { -1 , -1 , -1 ,  -1,  -1 , -1,   -1,  -1 ,  -1 ,  -1 }, // UNSUPPORTED
  { -1 ,  0 ,  1 ,  -1 , -1 , -1 ,  -1 , -1 ,  -1 ,  -1 }, // DUAL_MONO
  { -1 ,  0 ,  1 ,  -1 , -1 , -1 ,  -1 , -1 ,  -1 ,   2 }, // DUAL_MONO_LFE
  {  0 , -1 , -1 ,  -1 , -1 , -1 ,  -1 , -1 ,  -1 ,  -1 }, // MONO
  {  0 , -1 , -1 ,  -1 , -1 , -1 ,  -1 , -1 ,  -1 ,   1 }, // MONO_LFE
  { -1 ,  0 ,  1 ,  -1 , -1 , -1 ,  -1 , -1 ,  -1 ,  -1 }, // STEREO
  { -1 ,  0 ,  1 ,  -1 , -1 , -1 ,  -1 , -1 ,  -1 ,   2 }, // STEREO_LFE
  { -1 ,  0 ,  1 ,   2 , -1 , -1 ,  -1 , -1 ,  -1 ,  -1 }, // 3F
  { -1 ,  0 ,  1 ,   2 , -1 , -1 ,  -1 , -1 ,  -1 ,   3 }, // 3F_LFE
  { -1 ,  0 ,  1 ,  -1 , -1 , -1 ,  -1 ,  2 ,  -1 ,  -1 }, // 2F1
  { -1 ,  0 ,  1 ,  -1 , -1 , -1 ,  -1 ,  3 ,  -1 ,   2 }, // 2F1_LFE
  { -1 ,  0 ,  1 ,   2 , -1 , -1 ,  -1 ,  3 ,  -1 ,  -1 }, // 3F1
  { -1 ,  0 ,  1 ,   2 , -1 , -1 ,  -1 ,  4 ,  -1 ,   3 }, // 3F1_LFE
  { -1 ,  0 ,  1 ,  -1 ,  2 ,  3 ,  -1 , -1 ,  -1 ,  -1 }, // 2F2
  { -1 ,  0 ,  1 ,  -1 ,  3 ,  4 ,  -1 , -1 ,  -1 ,   2 }, // 2F2_LFE
  { -1 ,  0 ,  1 ,   2 ,  3 ,  4 ,  -1 , -1 ,  -1 ,  -1 }, // 3F2
  { -1 ,  0 ,  1 ,   2 ,  4 ,  5 ,  -1 , -1 ,  -1 ,   3 }, // 3F2_LFE
  { -1 ,  0 ,  1 ,   2 ,  5 ,  6 ,  -1 ,  4 ,  -1 ,   3 }, // 3F3R_LFE
  { -1 ,  0 ,  1 ,   2 ,  6 ,  7 ,   4 , -1 ,   5 ,   3 }, // 3F4_LFE
};

// The downmix matrix from TABLE 2 in the ITU-R BS.775-3[1] defines a way to
// convert 3F2 input data to 1F, 2F, 3F, 2F1, 3F1, 2F2 output data. We extend it
// to convert 3F2-LFE input data to 1F, 2F, 3F, 2F1, 3F1, 2F2 and their LFEs
// output data.
// [1] https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.775-3-201208-I!!PDF-E.pdf

// Number of converted layouts: 1F, 2F, 3F, 2F1, 3F1, 2F2 and their LFEs.
#define SUPPORTED_LAYOUT_NUM 12
// Number of input channel for downmix conversion.
#define INPUT_CHANNEL_NUM 6 // 3F2-LFE
// Max number of possible output channels.
#define MAX_OUTPUT_CHANNEL_NUM 5 // 2F2-LFE or 3F1-LFE
#define SQRT_1_2 0.707106f // 1/sqrt(2)
// Each array contains coefficients that will be multiplied with
// { L, R, C, LFE, LS, RS } channels respectively.
static const float DOWNMIX_MATRIX_3F2_LFE[SUPPORTED_LAYOUT_NUM][MAX_OUTPUT_CHANNEL_NUM][INPUT_CHANNEL_NUM] =
{
// 1F Mono
  {
    { SQRT_1_2, SQRT_1_2, 1, 0, 0.5, 0.5 },  // M
  },
// 1F Mono-LFE
  {
    { SQRT_1_2, SQRT_1_2, 1, 0, 0.5, 0.5 }, // M
    { 0, 0, 0, 1, 0, 0 }                    // LFE
  },
// 2F Stereo
  {
    { 1, 0, SQRT_1_2, 0, SQRT_1_2, 0 },     // L
    { 0, 1, SQRT_1_2, 0, 0, SQRT_1_2 }      // R
  },
// 2F Stereo-LFE
  {
    { 1, 0, SQRT_1_2, 0, SQRT_1_2, 0 },     // L
    { 0, 1, SQRT_1_2, 0, 0, SQRT_1_2 },     // R
    { 0, 0, 0, 1, 0, 0 }                    // LFE
  },
// 3F
  {
    { 1, 0, 0, 0, SQRT_1_2, 0 },            // L
    { 0, 1, 0, 0, 0, SQRT_1_2 },            // R
    { 0, 0, 1, 0, 0, 0 }                    // C
  },
// 3F-LFE
  {
    { 1, 0, 0, 0, SQRT_1_2, 0 },            // L
    { 0, 1, 0, 0, 0, SQRT_1_2 },            // R
    { 0, 0, 1, 0, 0, 0 },                   // C
    { 0, 0, 0, 1, 0, 0 }                    // LFE
  },
// 2F1
  {
    { 1, 0, SQRT_1_2, 0, 0, 0 },            // L
    { 0, 1, SQRT_1_2, 0, 0, 0 },            // R
    { 0, 0, 0, 0, SQRT_1_2, SQRT_1_2 }      // S
  },
// 2F1-LFE
  {
    { 1, 0, SQRT_1_2, 0, 0, 0 },            // L
    { 0, 1, SQRT_1_2, 0, 0, 0 },            // R
    { 0, 0, 0, 1, 0, 0 },                   // LFE
    { 0, 0, 0, 0, SQRT_1_2, SQRT_1_2 }      // S
  },
// 3F1
  {
    { 1, 0, 0, 0, 0, 0 },                   // L
    { 0, 1, 0, 0, 0, 0 },                   // R
    { 0, 0, 1, 0, 0, 0 },                   // C
    { 0, 0, 0, 0, SQRT_1_2, SQRT_1_2 }      // S
  },
// 3F1-LFE
  {
    { 1, 0, 0, 0, 0, 0 },                   // L
    { 0, 1, 0, 0, 0, 0 },                   // R
    { 0, 0, 1, 0, 0, 0 },                   // C
    { 0, 0, 0, 1, 0, 0 },                   // LFE
    { 0, 0, 0, 0, SQRT_1_2, SQRT_1_2 }      // S
  },
// 2F2
  {
    { 1, 0, SQRT_1_2, 0, 0, 0 },            // L
    { 0, 1, SQRT_1_2, 0, 0, 0 },            // R
    { 0, 0, 0, 0, 1, 0 },                   // LS
    { 0, 0, 0, 0, 0, 1 }                    // RS
  },
// 2F2-LFE
  {
    { 1, 0, SQRT_1_2, 0, 0, 0 },            // L
    { 0, 1, SQRT_1_2, 0, 0, 0 },            // R
    { 0, 0, 0, 1, 0, 0 },                   // LFE
    { 0, 0, 0, 0, 1, 0 },                   // LS
    { 0, 0, 0, 0, 0, 1 }                    // RS
  }
};

/* Convert audio data from 3F2(-LFE) to 1F, 2F, 3F, 2F1, 3F1, 2F2 and their LFEs. */
template<typename T>
bool
downmix_3f2(const T * in, const long inframes, T * out, const cubeb_channel_layout in_layout, const cubeb_channel_layout out_layout)
{
  if ((in_layout != CUBEB_LAYOUT_3F2 && in_layout != CUBEB_LAYOUT_3F2_LFE) ||
      out_layout < CUBEB_LAYOUT_MONO || out_layout > CUBEB_LAYOUT_2F2_LFE) {
    return false;
  }

  unsigned int in_channels = CUBEB_CHANNEL_LAYOUT_MAPS[in_layout].channels;
  unsigned int out_channels = CUBEB_CHANNEL_LAYOUT_MAPS[out_layout].channels;

  // Conversion from 3F2 to 2F2-LFE or 3F1-LFE is allowed, so we use '<=' instead of '<'.
  assert(out_channels <= in_channels);

  long out_index = 0;
  unsigned int layout_index = out_layout - CUBEB_LAYOUT_MONO; // The matrix is started from mono.
  for (long i = 0; i < inframes * in_channels; i += in_channels) {
    for (unsigned int j = 0; j < out_channels; ++j) {
      out[out_index + j] = 0; // Clear its value.
      for (unsigned int k = 0 ; k < INPUT_CHANNEL_NUM ; ++k) {
        // 3F2-LFE has 6 channels: L, R, C, LFE, LS, RS, while 3F2 has only 5
        // channels: L, R, C, LS, RS. Thus, we need to append 0 to LFE(index 3)
        // to simulate a 3F2-LFE data when input layout is 3F2.
        T data = (in_layout == CUBEB_LAYOUT_3F2_LFE) ? in[i + k] : (k == 3) ? 0 : in[i + ((k < 3) ? k : k - 1)];
        out[out_index + j] += DOWNMIX_MATRIX_3F2_LFE[layout_index][j][k] * data;
      }
    }
    out_index += out_channels;
  }

  return true;
}

/* Map the audio channel data by layouts*/
template<class T>
bool
mix_map_channels(const T* in, const long inframes, T* out, const cubeb_channel_layout in_layout, const cubeb_channel_layout out_layout) {
  assert(in_layout != out_layout);
  unsigned int in_channels = CUBEB_CHANNEL_LAYOUT_MAPS[in_layout].channels;
  unsigned int out_channels = CUBEB_CHANNEL_LAYOUT_MAPS[out_layout].channels;

  uint32_t in_layout_mask = 0;
  for (unsigned int i = 0 ; i < in_channels ; ++i) {
    in_layout_mask |= 1 << CHANNEL_INDEX_TO_ORDER[in_layout][i];
  }

  uint32_t out_layout_mask = 0;
  for (unsigned int i = 0 ; i < out_channels ; ++i) {
    out_layout_mask |= 1 << CHANNEL_INDEX_TO_ORDER[out_layout][i];
  }

  // If there is no matched channel, then do nothing.
  if (!(out_layout_mask & in_layout_mask)) {
    return false;
  }

  long out_index = 0;
  for (long i = 0; i < inframes * in_channels; i += in_channels) {
    for (unsigned int j = 0; j < out_channels; ++j) {
      cubeb_channel channel = CHANNEL_INDEX_TO_ORDER[out_layout][j];
      uint32_t channel_mask = 1 << channel;
      int channel_index = CHANNEL_ORDER_TO_INDEX[in_layout][channel];
      if (in_layout_mask & channel_mask) {
        assert(channel_index != -1);
        out[out_index + j] = in[i + channel_index];
      } else {
        assert(channel_index == -1);
        out[out_index + j] = 0;
      }
    }
    out_index += out_channels;
  }

  return true;
}

/* Drop the extra channels beyond the provided output channels. */
template<typename T>
void
downmix_fallback(const T * in, const long inframes, T * out, const unsigned int in_channels, const unsigned int out_channels)
{
  assert(in_channels >= out_channels);
  long out_index = 0;
  for (long i = 0; i < inframes * in_channels; i += in_channels) {
    for (unsigned int j = 0; j < out_channels; ++j) {
      out[out_index + j] = in[i + j];
    }
    out_index += out_channels;
  }
}


template<typename T>
void
cubeb_downmix(const T * in, const long inframes, T * out,
              const unsigned int in_channels, const unsigned int out_channels,
              const cubeb_channel_layout in_layout, const cubeb_channel_layout out_layout)
{
  assert(in_channels >= out_channels && in_layout != CUBEB_LAYOUT_UNSUPPORTED);

  // If the channel number is different from the layout's setting or it's not a
  // valid audio 5.1 downmix, then we use fallback downmix mechanism.
  if (out_channels == CUBEB_CHANNEL_LAYOUT_MAPS[out_layout].channels &&
      in_channels == CUBEB_CHANNEL_LAYOUT_MAPS[in_layout].channels) {
    if (downmix_3f2(in, inframes, out, in_layout, out_layout)) {
      return;
    }

    if (mix_map_channels(in, inframes, out, in_layout, out_layout)) {
      return;
    }
  }

  downmix_fallback(in, inframes, out, in_channels, out_channels);
}

/* Upmix function, copies a mono channel into L and R */
template<typename T>
void
mono_to_stereo(const T * in, const long insamples, T * out, const unsigned int out_channels)
{
  for (long i = 0, j = 0; i < insamples; ++i, j += out_channels) {
    out[j] = out[j + 1] = in[i];
  }
}

template<typename T>
void
cubeb_upmix(const T * in, const long inframes, T * out,
            const unsigned int in_channels, const unsigned int out_channels)
{
  assert(out_channels >= in_channels && in_channels > 0);

  /* Either way, if we have 2 or more channels, the first two are L and R. */
  /* If we are playing a mono stream over stereo speakers, copy the data over. */
  if (in_channels == 1 && out_channels >= 2) {
    mono_to_stereo(in, inframes, out, out_channels);
  } else {
    /* Copy through. */
    for (unsigned int i = 0, o = 0; i < inframes * in_channels;
        i += in_channels, o += out_channels) {
      for (unsigned int j = 0; j < in_channels; ++j) {
        out[o + j] = in[i + j];
      }
    }
  }

  /* Check if more channels. */
  if (out_channels <= 2) {
    return;
  }

  /* Put silence in remaining channels. */
  for (long i = 0, o = 0; i < inframes; ++i, o += out_channels) {
    for (unsigned int j = 2; j < out_channels; ++j) {
      out[o + j] = 0.0;
    }
  }
}

bool cubeb_should_upmix(cubeb_stream_params * stream, cubeb_stream_params * mixer)
{
  return mixer->channels > stream->channels;
}

bool cubeb_should_downmix(cubeb_stream_params * stream, cubeb_stream_params * mixer)
{
  if (mixer->channels > stream->channels || mixer->layout == stream->layout) {
    return false;
  }

  return mixer->channels < stream->channels ||
         // When mixer.channels == stream.channels
         mixer->layout == CUBEB_LAYOUT_UNSUPPORTED ||  // fallback downmix
         (stream->layout == CUBEB_LAYOUT_3F2 &&        // 3f2 downmix
          (mixer->layout == CUBEB_LAYOUT_2F2_LFE ||
           mixer->layout == CUBEB_LAYOUT_3F1_LFE));
}

void
cubeb_downmix_float(const float * in, const long inframes, float * out,
                    const unsigned int in_channels, const unsigned int out_channels,
                    const cubeb_channel_layout in_layout, const cubeb_channel_layout out_layout)
{
  cubeb_downmix(in, inframes, out, in_channels, out_channels, in_layout, out_layout);
}

void
cubeb_upmix_float(const float * in, const long inframes, float * out,
                  const unsigned int in_channels, const unsigned int out_channels)
{
  cubeb_upmix(in, inframes, out, in_channels, out_channels);
}
