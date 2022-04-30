// Copyright 2022 The VGC Developers
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

#include <vgc/core/mat4f.h>

#include <limits>

namespace vgc::core {

Mat4f Mat4f::inversed(bool* isInvertible, float epsilon) const
{
    Mat4f res;

    const auto& d = data_;
    auto& inv = res.data_;

    inv[0][0] =   d[1][1]*d[2][2]*d[3][3] - d[1][1]*d[2][3]*d[3][2] - d[2][1]*d[1][2]*d[3][3]
                + d[2][1]*d[1][3]*d[3][2] + d[3][1]*d[1][2]*d[2][3] - d[3][1]*d[1][3]*d[2][2];
    inv[1][0] = - d[1][0]*d[2][2]*d[3][3] + d[1][0]*d[2][3]*d[3][2] + d[2][0]*d[1][2]*d[3][3]
                - d[2][0]*d[1][3]*d[3][2] - d[3][0]*d[1][2]*d[2][3] + d[3][0]*d[1][3]*d[2][2];
    inv[2][0] =   d[1][0]*d[2][1]*d[3][3] - d[1][0]*d[2][3]*d[3][1] - d[2][0]*d[1][1]*d[3][3]
                + d[2][0]*d[1][3]*d[3][1] + d[3][0]*d[1][1]*d[2][3] - d[3][0]*d[1][3]*d[2][1];
    inv[3][0] = - d[1][0]*d[2][1]*d[3][2] + d[1][0]*d[2][2]*d[3][1] + d[2][0]*d[1][1]*d[3][2]
                - d[2][0]*d[1][2]*d[3][1] - d[3][0]*d[1][1]*d[2][2] + d[3][0]*d[1][2]*d[2][1];

    float det = d[0][0]*inv[0][0] + d[0][1]*inv[1][0] + d[0][2]*inv[2][0] + d[0][3]*inv[3][0];

    if (std::abs(det) <= epsilon) {
        if(isInvertible) {
            *isInvertible = false;
        }
        constexpr float inf = std::numeric_limits<float>::infinity();
        res.setElements(inf, inf, inf, inf,
                        inf, inf, inf, inf,
                        inf, inf, inf, inf,
                        inf, inf, inf, inf);
    }
    else {
        if(isInvertible) {
            *isInvertible = true;
        }

        inv[0][1] = - d[0][1]*d[2][2]*d[3][3] + d[0][1]*d[2][3]*d[3][2] + d[2][1]*d[0][2]*d[3][3]
                    - d[2][1]*d[0][3]*d[3][2] - d[3][1]*d[0][2]*d[2][3] + d[3][1]*d[0][3]*d[2][2];
        inv[1][1] =   d[0][0]*d[2][2]*d[3][3] - d[0][0]*d[2][3]*d[3][2] - d[2][0]*d[0][2]*d[3][3]
                    + d[2][0]*d[0][3]*d[3][2] + d[3][0]*d[0][2]*d[2][3] - d[3][0]*d[0][3]*d[2][2];
        inv[2][1] = - d[0][0]*d[2][1]*d[3][3] + d[0][0]*d[2][3]*d[3][1] + d[2][0]*d[0][1]*d[3][3]
                    - d[2][0]*d[0][3]*d[3][1] - d[3][0]*d[0][1]*d[2][3] + d[3][0]*d[0][3]*d[2][1];
        inv[3][1] =   d[0][0]*d[2][1]*d[3][2] - d[0][0]*d[2][2]*d[3][1] - d[2][0]*d[0][1]*d[3][2]
                    + d[2][0]*d[0][2]*d[3][1] + d[3][0]*d[0][1]*d[2][2] - d[3][0]*d[0][2]*d[2][1];
        inv[0][2] =   d[0][1]*d[1][2]*d[3][3] - d[0][1]*d[1][3]*d[3][2] - d[1][1]*d[0][2]*d[3][3]
                    + d[1][1]*d[0][3]*d[3][2] + d[3][1]*d[0][2]*d[1][3] - d[3][1]*d[0][3]*d[1][2];
        inv[1][2] = - d[0][0]*d[1][2]*d[3][3] + d[0][0]*d[1][3]*d[3][2] + d[1][0]*d[0][2]*d[3][3]
                    - d[1][0]*d[0][3]*d[3][2] - d[3][0]*d[0][2]*d[1][3] + d[3][0]*d[0][3]*d[1][2];
        inv[2][2] =   d[0][0]*d[1][1]*d[3][3] - d[0][0]*d[1][3]*d[3][1] - d[1][0]*d[0][1]*d[3][3]
                    + d[1][0]*d[0][3]*d[3][1] + d[3][0]*d[0][1]*d[1][3] - d[3][0]*d[0][3]*d[1][1];
        inv[3][2] = - d[0][0]*d[1][1]*d[3][2] + d[0][0]*d[1][2]*d[3][1] + d[1][0]*d[0][1]*d[3][2]
                    - d[1][0]*d[0][2]*d[3][1] - d[3][0]*d[0][1]*d[1][2] + d[3][0]*d[0][2]*d[1][1];
        inv[0][3] = - d[0][1]*d[1][2]*d[2][3] + d[0][1]*d[1][3]*d[2][2] + d[1][1]*d[0][2]*d[2][3]
                    - d[1][1]*d[0][3]*d[2][2] - d[2][1]*d[0][2]*d[1][3] + d[2][1]*d[0][3]*d[1][2];
        inv[1][3] =   d[0][0]*d[1][2]*d[2][3] - d[0][0]*d[1][3]*d[2][2] - d[1][0]*d[0][2]*d[2][3]
                    + d[1][0]*d[0][3]*d[2][2] + d[2][0]*d[0][2]*d[1][3] - d[2][0]*d[0][3]*d[1][2];
        inv[2][3] = - d[0][0]*d[1][1]*d[2][3] + d[0][0]*d[1][3]*d[2][1] + d[1][0]*d[0][1]*d[2][3]
                    - d[1][0]*d[0][3]*d[2][1] - d[2][0]*d[0][1]*d[1][3] + d[2][0]*d[0][3]*d[1][1];
        inv[3][3] =   d[0][0]*d[1][1]*d[2][2] - d[0][0]*d[1][2]*d[2][1] - d[1][0]*d[0][1]*d[2][2]
                    + d[1][0]*d[0][2]*d[2][1] + d[2][0]*d[0][1]*d[1][2] - d[2][0]*d[0][2]*d[1][1];

        res *= static_cast<float>(1) / det;
    }
    return res;
}

} // namespace vgc::core
