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

#ifndef VGC_CORE_DETAIL_PARSEBITS_H
#define VGC_CORE_DETAIL_PARSEBITS_H

// This header helps avoiding a cyclic dependency between stringid.h and parse.h

#include <string>

namespace vgc::core::detail {

template<typename IStream>
std::string readStringUntilEof(std::string& s, IStream& in) {
    char c;
    while (in.get(c)) {
        s.push_back(c);
    }
    return s;
}

} // namespace vgc::core::detail

#endif // VGC_CORE_DETAIL_PARSEBITS_H
