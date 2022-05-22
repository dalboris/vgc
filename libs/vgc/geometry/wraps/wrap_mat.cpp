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

#include <vgc/core/wraps/common.h>
#include <pybind11/operators.h>
#include <vgc/geometry/mat3d.h>
#include <vgc/geometry/mat3f.h>
#include <vgc/geometry/mat4d.h>
#include <vgc/geometry/mat4f.h>

namespace {

// Provides matrix alias template
template<int dim, typename T> struct Mat_ {};
template<> struct Mat_<3, float>  { using type = vgc::geometry::Mat3f; };
template<> struct Mat_<3, double> { using type = vgc::geometry::Mat3d; };
template<> struct Mat_<4, float>  { using type = vgc::geometry::Mat4f; };
template<> struct Mat_<4, double> { using type = vgc::geometry::Mat4d; };
template<int dim, typename T>
using Mat = typename Mat_<dim, T>::type;

// Provides vector alias template
template<int dim, typename T> struct Vec_ {};
template<> struct Vec_<2, float>  { using type = vgc::geometry::Vec2f; };
template<> struct Vec_<2, double> { using type = vgc::geometry::Vec2d; };
template<int dim, typename T>
using Vec = typename Vec_<dim, T>::type;

// Allows `mat[i][j]` Python syntax
template<int dim, typename T>
class MatRowView {
    using TMat = Mat<dim, T>;

public:
    MatRowView(TMat& mat, vgc::Int i) : mat_(&mat), i_(i) {}
    T& operator[](vgc::Int j) { return (*mat_)(i_, j); }
    const T& operator[](vgc::Int j) const { return (*mat_)(i_, j); }

private:
    TMat* mat_;
    vgc::Int i_;
};

template<int dim, typename T>
void wrap_mat(py::module& m, const std::string& name)
{
    using TMat = Mat<dim, T>;
    using TRow = MatRowView<dim, T>;

    // Wrap RowView class
    py::class_<TRow>(m, (name + "RowView").c_str())
        .def("__getitem__", [](const TRow& row, vgc::Int j) {
            if (j < 0 || j >= dim) throw py::index_error();
            return row[j]; })
        .def("__setitem__", [](TRow& row, vgc::Int j, T x) {
            if (j < 0 || j >= dim) throw py::index_error();
            row[j] = x; });

    py::class_<TMat> cmat(m, name.c_str());

    // Default constructor, copy constructor, and diagonal matrix constructor.
    // Note that in Python, unlike in C++, the default constructor does
    // zero-initialization.
    cmat.def(py::init([]() { return TMat(0); } ))
        .def(py::init<TMat>())
        .def(py::init<T>());

    // Constructor with explicit initialization of all elements, e.g.:
    //
    // m = Mat3d(1, 2, 3,
    //           4, 5, 6,
    //           7, 8, 9)
    //
    // Note that in numpy the typical syntax is:
    //
    // A = np.array([[1, 2, 3],
    //               [4, 5, 6],
    //               [7, 8, 9]])
    //
    // The "list of list" is required so that numpy can infer the numRows and
    // numColumns, since numpy doesn't have per-size specific matrix types. We
    // don't have this constraint, so there is no good reason to do the same.
    //
    if constexpr (dim == 3) {
        cmat.def(py::init<T, T, T,
                          T, T, T,
                          T, T, T>());
    }
    else if constexpr (dim == 4) {
        cmat.def(py::init<T, T, T, T,
                          T, T, T, T,
                          T, T, T, T,
                          T, T, T, T>());
    }

    // TODO: constructor from list so we can do:
    //
    //     m = Mat4d([i+j for i, j in Mat4d.indices])
    //
    // or perhaps also a constructor taking a lambda, e.g.:
    //
    //     m = Mat4d(lambda i, j: i+j)
    //

    // Get/Set individual element via `m[i][j] = 42`
    cmat.def("__getitem__",
        [](TMat& mat, vgc::Int i) {
            if (i < 0 || i >= dim) throw py::index_error();
            return TRow(mat, i);
        },
        py::keep_alive<0, 1>()); // we want the Row to keep the Mat alive
            // Note: py::return_value_policy::reference_internal wouldn't work
            // here because it expects a T* or T&, and here we return a T.

    // Convenient way to iterate over all valid indices in the matrix type
    cmat.def_property_readonly_static("indices",
        [](py::object) {
            py::list res(dim * dim);
            for (int i = 0; i < dim; ++i) {
                for (int j = 0; j < dim; ++j) {
                    py::tuple t(2); t[0] = i; t[1] = j;
                    res[j + i*dim] = t;
                }
            }
            return res;
         });

    // Overload of arithmetic operators
    cmat.def(py::self += py::self)
        .def(py::self + py::self)
        .def(+ py::self)
        .def(py::self -= py::self)
        .def(py::self - py::self)
        .def(- py::self)
        .def(py::self *= py::self)
        .def(py::self * py::self)
        .def(py::self *= T())
        .def(T() * py::self)
        .def(py::self * T())
        .def(py::self /= T())
        .def(py::self / T())
        .def(py::self == py::self)
        .def(py::self != py::self)
        .def(py::self * Vec<2, T>());

    // Identity
    cmat.def_property_readonly_static("identity", [](py::object) -> TMat {
        return TMat::identity; });

    // Other methods
    cmat.def("setElements", &TMat::setElements)
        .def("setToDiagonal", &TMat::setToDiagonal)
        .def("setToZero", &TMat::setToZero)
        .def("setToIdentity", &TMat::setToIdentity)
        .def("inverted", [](const TMat& m) {
            bool isInvertible;
            TMat res = m.inverted(&isInvertible);
            if (!isInvertible) {
                throw py::value_error("The matrix is not invertible.");
            }
            return res; })
        .def("rotate", &TMat::rotate, "t"_a, "orthosnap"_a = true)
        .def("scale", py::overload_cast<T>(&TMat::scale));

    if constexpr (dim == 3) {
        cmat.def("translate", &TMat::translate,
                 "vx"_a, "vy"_a = 0)
             .def("scale", py::overload_cast<T, T>(&TMat::scale));
    }
    else if constexpr (dim == 4) {
        cmat.def("translate", &TMat::translate,
                 "vx"_a, "vy"_a = 0, "vz"_a = 0)
             .def("scale", py::overload_cast<T, T, T>(&TMat::scale),
                  "sx"_a, "sy"_a, "sz"_a = 0);
    }

    // Conversion to string
    cmat.def("__repr__", [](const TMat& m) { return vgc::core::toString(m); });

    // TODO: parse from string
}

} // namespace

void wrap_mat(py::module& m)
{
    wrap_mat<3, float>(m, "Mat3f");
    wrap_mat<3, double>(m, "Mat3d");
    wrap_mat<4, float>(m, "Mat4f");
    wrap_mat<4, double>(m, "Mat4d");
}