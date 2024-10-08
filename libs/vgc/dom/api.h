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

#ifndef VGC_DOM_API_H
#define VGC_DOM_API_H

/// \file vgc/dom/api.h
/// \brief Defines symbol visibility macros for defining shared library API.
///
/// Please read vgc/core/dll.h for details.
///

#include <vgc/core/dll.h>

// clang-format off

#if defined(VGC_DOM_STATIC)
#  define VGC_DOM_API                                VGC_DLL_STATIC
#  define VGC_DOM_API_HIDDEN                         VGC_DLL_STATIC_HIDDEN
#  define VGC_DOM_API_DECLARE_TEMPLATE(k, ...)       VGC_DLL_STATIC_DECLARE_TEMPLATE(k, __VA_ARGS__)
#  define VGC_DOM_API_DEFINE_TEMPLATE(k, ...)        VGC_DLL_STATIC_DEFINE_TEMPLATE(k, __VA_ARGS__)
#  define VGC_DOM_API_DECLARE_TEMPLATE_FUNCTION(...) VGC_DLL_STATIC_DECLARE_TEMPLATE_FUNCTION(__VA_ARGS__)
#  define VGC_DOM_API_DEFINE_TEMPLATE_FUNCTION(...)  VGC_DLL_STATIC_DEFINE_TEMPLATE_FUNCTION(__VA_ARGS__)
#  define VGC_DOM_API_EXCEPTION                      VGC_DLL_STATIC_EXCEPTION
#elif defined(VGC_DOM_EXPORTS)
#  define VGC_DOM_API                                VGC_DLL_EXPORT
#  define VGC_DOM_API_HIDDEN                         VGC_DLL_EXPORT_HIDDEN
#  define VGC_DOM_API_DECLARE_TEMPLATE(k, ...)       VGC_DLL_EXPORT_DECLARE_TEMPLATE(k, __VA_ARGS__)
#  define VGC_DOM_API_DEFINE_TEMPLATE(k, ...)        VGC_DLL_EXPORT_DEFINE_TEMPLATE(k, __VA_ARGS__)
#  define VGC_DOM_API_DECLARE_TEMPLATE_FUNCTION(...) VGC_DLL_EXPORT_DECLARE_TEMPLATE_FUNCTION(__VA_ARGS__)
#  define VGC_DOM_API_DEFINE_TEMPLATE_FUNCTION(...)  VGC_DLL_EXPORT_DEFINE_TEMPLATE_FUNCTION(__VA_ARGS__)
#  define VGC_DOM_API_EXCEPTION                      VGC_DLL_EXPORT_EXCEPTION
#else
#  define VGC_DOM_API                                VGC_DLL_IMPORT
#  define VGC_DOM_API_HIDDEN                         VGC_DLL_IMPORT_HIDDEN
#  define VGC_DOM_API_DECLARE_TEMPLATE(k, ...)       VGC_DLL_IMPORT_DECLARE_TEMPLATE(k, __VA_ARGS__)
#  define VGC_DOM_API_DEFINE_TEMPLATE(k, ...)        VGC_DLL_IMPORT_DEFINE_TEMPLATE(k, __VA_ARGS__)
#  define VGC_DOM_API_DECLARE_TEMPLATE_FUNCTION(...) VGC_DLL_IMPORT_DECLARE_TEMPLATE_FUNCTION(__VA_ARGS__)
#  define VGC_DOM_API_DEFINE_TEMPLATE_FUNCTION(...)  VGC_DLL_IMPORT_DEFINE_TEMPLATE_FUNCTION(__VA_ARGS__)
#  define VGC_DOM_API_EXCEPTION                      VGC_DLL_IMPORT_EXCEPTION
#endif
#define VGC_DOM_API_DECLARE_TEMPLATE_CLASS(...)  VGC_DOM_API_DECLARE_TEMPLATE(class, __VA_ARGS__)
#define VGC_DOM_API_DECLARE_TEMPLATE_STRUCT(...) VGC_DOM_API_DECLARE_TEMPLATE(struct, __VA_ARGS__)
#define VGC_DOM_API_DECLARE_TEMPLATE_UNION(...)  VGC_DOM_API_DECLARE_TEMPLATE(union, __VA_ARGS__)
#define VGC_DOM_API_DEFINE_TEMPLATE_CLASS(...)   VGC_DOM_API_DEFINE_TEMPLATE(class, __VA_ARGS__)
#define VGC_DOM_API_DEFINE_TEMPLATE_STRUCT(...)  VGC_DOM_API_DEFINE_TEMPLATE(struct, __VA_ARGS__)
#define VGC_DOM_API_DEFINE_TEMPLATE_UNION(...)   VGC_DOM_API_DEFINE_TEMPLATE(union, __VA_ARGS__)

#endif // VGC_DOM_API_H
