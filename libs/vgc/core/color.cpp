// Copyright 2021 The VGC Developers
// See the COPYRIGHT file at the top-level directory of this distribution
// and at https://github.com/vgc/vgc/blob/master/COPYRIGHT
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <vgc/core/color.h>

namespace vgc {
namespace core {

Color Color::hsl(double h, double s,  double l)
{
    // Wrap h to [0, 360] range, and clamp s, l to [0, 1]
    h = std::fmod(h, 360.0);
    if (h < 0) {
        h += 360;
    }
    s = core::clamp(s, 0.0, 1.0);
    l = core::clamp(l, 0.0, 1.0);

    // HSL to RGB
    double c = (1 - std::abs(2*l - 1)) * s;
    double hp = h / 60;
    double x = c * (1 - std::abs(std::fmod(hp, 2.0) - 1));
    int hi = core::ifloor<int>(hp+1); // in theory, we should use iceil instead
    double r1, g1, b1;
    if      (hi == 1) { r1 = c; g1 = x; b1 = 0; }
    else if (hi == 2) { r1 = x; g1 = c; b1 = 0; }
    else if (hi == 3) { r1 = 0; g1 = c; b1 = x; }
    else if (hi == 4) { r1 = 0; g1 = x; b1 = c; }
    else if (hi == 5) { r1 = x; g1 = 0; b1 = c; }
    else if (hi == 6) { r1 = c; g1 = 0; b1 = x; }
    else              { r1 = 0; g1 = 0; b1 = 0; }
    double m = l - 0.5*c;
    return Color(r1 + m, g1 + m, b1 + m);
}

} // namespace core
} // namespace vgc

// Note 1: In the future, we'd like to have the class "Color" be more flexible.
// Notably, there should be control on:
//
//   1. How is the color stored in memory (C++ representation)
//   2. How is it converted to a string (XML representation)
//
// Examples of orthogonal choices for #1 include rgb/hls, int/float, 8/16/32/64
// bits per channel, premultiplied or not, etc.
//
// Examples of orthogonal choices for #2 include all the above + whether to
// omit the alpha channel if fully-opaque, whether to use #rrggbbaa notation or
// rgb(r, g, b), or rgb8(r, g, b), etc.
//
// In addition to the per-Color formatting style, there could be a
// per-file/per-session/per-user preferred format (the one used by default),
// but allowing a per-color formatting style is important, since within the
// same file the user might prefer to use hex notation for some colors, and
// functional notation for others. It's especially useful to preserve existing
// format when opening a file initially written by hand. New colors should have
// color.stringFormat() == Color::FileDefault, where the per-file default
// format for colors can be inferred when reading the file. For new files, or
// files where the FileDefault can't be inferred (or only partially inferred),
// then "FileDefault" should be determined via user preference.

// Note 2: Currently, for simplicity and genericity, the Color class is
// actually quite heavy: 4x64bits (and later: + metadata). This is okay for
// single colors, but would make a large vector<Color> very inefficient. This
// is why the type ColorArray shouldn't just be a vector<Color>, but instead a
// vector<char> + metadata, so that the metadata is shared, and only the
// required number of bytes is used. In fact, even the Color class might be a
// vector<char>, that is, use dynamic allocation to save memory. It isn't clear
// at the moment whether it's better to have a heavy Color class, or a Color
// class doing dynamic allocation to save memory.

// Note 3: ideally, we think it makes more sense if the alpha channel was
// formatted as a 8bit integer by default (like the other color channels are),
// instead of a floating point between [0, 1]. However, we decided to stick to
// the existing SVG and CSS standards:
//
//   https://developer.mozilla.org/en-US/docs/Web/CSS/CSS_Types
//
// For now, we always print as rgb(int8, int8, int8, float), but in the future
// we'd like to have the following the bring back sanity and flexibility in
// a backward-compatible manner:
//
//     - rgb(int8, int8, int8 [, double])
//     - rgb8(int8, int8, int8 [, int8])
//     - rgb16(int16, int16, int16 [, int16])
//     - rgb32(int32, int32, int32 [, int32])
//     - rgb64(int64, int64, int64 [, int64])
//     - rgb32f(int32, int32, int32 [, int32])
//     - rgb64f(int64, int64, int64 [, int64])
//
// Color arrays may be stored as:
//
//    - rbg[(r, g, b), ...]
//    - rbg8[(r, g, b), ...]
//    - ...
//
// Sometimes, we may want to have per-item alpha values, and sometimes a shared
// alpha values, we haven't decided yet the best way to allow both, although
// a separate "opacity" attribute is probably enough in most use cases.

// Note 4: when the color is read as rgb(int8, int8, int8, double), the alpha
// channel should actually simply be stored as a 8-bit integer, especially in
// a large ColorArray. This is because if each color channel is 8-bit anyway,
// there's probably no need for more precision than 8-bit for the alpha channel
// either, since the result of alpha-compositing would clamp to 8-bit anyway.
