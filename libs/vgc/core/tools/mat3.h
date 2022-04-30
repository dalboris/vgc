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

// This file is a "template" (not in the C++ sense) used to generate all the
// Mat3 variants. Please refer to mat3.py for more info.

#ifndef VGC_CORE_MAT3X_H
#define VGC_CORE_MAT3X_H

#include <cmath>
#include <vgc/core/api.h>
#include "vec2.h"

namespace vgc::core {

/// \class vgc::core::Mat3x
/// \brief 3x3 matrix using %SCALAR_DESCRIPTION%.
///
/// A Mat3x represents a 3x3 matrix in column-major format.
///
/// The memory size of a Mat3x is exactly 9 * sizeof(float). This will never
/// change in any future version, as this allows to conveniently use this class
/// for data transfer to the GPU (via OpenGL, Metal, etc.).
///
/// Unlike in the Eigen library, VGC has chosen not to distinguish between 3x3
/// matrices and 2D affine transformations in homogeneous coordinates. In other
/// words, if you wish to represent a 2D affine transformation, simply use a
/// Mat3x.
///
// VGC_CORE_API <- Omitted on purpose, otherwise we couldn't define `identity`.
//                 Instead, we manually export functions defined in the .cpp.
class Mat3x
{
public:
    /// Creates an uninitialized Mat3x.
    ///
    Mat3x() {}

    /// Creates a Mat3x initialized with the given arguments.
    ///
    constexpr Mat3x(float m11, float m12, float m13,
                    float m21, float m22, float m23,
                    float m31, float m32, float m33) :

        data_{{m11, m21, m31},
              {m12, m22, m32},
              {m13, m23, m33}}
    {

    }

    /// Creates a diagonal matrix with diagonal elements equal to the given
    /// value. As specific cases, the null matrix is Mat3x(0), and the identity
    /// matrix is Mat3x(1).
    ///
    explicit constexpr Mat3x(float d) :

        data_{{d, 0, 0},
              {0, d, 0},
              {0, 0, d}}
    {

    }

    /// Defines explicitely all the elements of the matrix
    ///
    Mat3x& setElements(float m11, float m12, float m13,
                       float m21, float m22, float m23,
                       float m31, float m32, float m33)
    {
        data_[0][0] = m11; data_[0][1] = m21; data_[0][2] = m31;
        data_[1][0] = m12; data_[1][1] = m22; data_[1][2] = m32;
        data_[2][0] = m13; data_[2][1] = m23; data_[2][2] = m33;
        return *this;
    }

    /// Sets this Mat3x to a diagonal matrix with all diagonal elements equal to
    /// the given value.
    ///
    Mat3x& setToDiagonal(float d)
    {
        return setElements(d, 0, 0,
                           0, d, 0,
                           0, 0, d);
    }

    /// Sets this Mat3x to the zero matrix.
    ///
    Mat3x& setToZero() { return setToDiagonal(0); }

    /// Sets this Mat3x to the identity matrix.
    ///
    Mat3x& setToIdentity() { return setToDiagonal(1); }

    /// The identity matrix Mat3x(1).
    ///
    static const Mat3x identity;

    /// Accesses the component of the Mat3x the the i-th row and j-th column.
    ///
    const float& operator()(Int i, Int j) const { return data_[j][i]; }

    /// Mutates the component of the Mat3x the the i-th row and j-th column.
    ///
    float& operator()(Int i, Int j) { return data_[j][i]; }

    /// Adds in-place the \p other Mat3x to this Mat3x.
    ///
    Mat3x& operator+=(const Mat3x& other) {
        data_[0][0] += other.data_[0][0];
        data_[0][1] += other.data_[0][1];
        data_[0][2] += other.data_[0][2];
        data_[1][0] += other.data_[1][0];
        data_[1][1] += other.data_[1][1];
        data_[1][2] += other.data_[1][2];
        data_[2][0] += other.data_[2][0];
        data_[2][1] += other.data_[2][1];
        data_[2][2] += other.data_[2][2];
        return *this;
    }

    /// Returns the addition of the Mat3x \p m1 and the Mat3x \p m2.
    ///
    friend Mat3x operator+(const Mat3x& m1, const Mat3x& m2) {
        return Mat3x(m1) += m2;
    }

    /// Substracts in-place the \p other Mat3x to this Mat3x.
    ///
    Mat3x& operator-=(const Mat3x& other) {
        data_[0][0] -= other.data_[0][0];
        data_[0][1] -= other.data_[0][1];
        data_[0][2] -= other.data_[0][2];
        data_[1][0] -= other.data_[1][0];
        data_[1][1] -= other.data_[1][1];
        data_[1][2] -= other.data_[1][2];
        data_[2][0] -= other.data_[2][0];
        data_[2][1] -= other.data_[2][1];
        data_[2][2] -= other.data_[2][2];
        return *this;
    }

    /// Returns the substraction of the Mat3x \p m1 and the Mat3x \p m2.
    ///
    friend Mat3x operator-(const Mat3x& m1, const Mat3x& m2) {
        return Mat3x(m1) -= m2;
    }

    /// Multiplies in-place the \p other Mat3x to this Mat3x.
    ///
    Mat3x& operator*=(const Mat3x& other) {
        *this = (*this) * other;
        return *this;
    }

    /// Returns the multiplication of the Mat3x \p m1 and the Mat3x \p m2.
    ///
    friend Mat3x operator*(const Mat3x& m1, const Mat3x& m2) {
        Mat3x res;
        res(0,0) = m1(0,0)*m2(0,0) + m1(0,1)*m2(1,0) + m1(0,2)*m2(2,0);
        res(0,1) = m1(0,0)*m2(0,1) + m1(0,1)*m2(1,1) + m1(0,2)*m2(2,1);
        res(0,2) = m1(0,0)*m2(0,2) + m1(0,1)*m2(1,2) + m1(0,2)*m2(2,2);
        res(1,0) = m1(1,0)*m2(0,0) + m1(1,1)*m2(1,0) + m1(1,2)*m2(2,0);
        res(1,1) = m1(1,0)*m2(0,1) + m1(1,1)*m2(1,1) + m1(1,2)*m2(2,1);
        res(1,2) = m1(1,0)*m2(0,2) + m1(1,1)*m2(1,2) + m1(1,2)*m2(2,2);
        res(2,0) = m1(2,0)*m2(0,0) + m1(2,1)*m2(1,0) + m1(2,2)*m2(2,0);
        res(2,1) = m1(2,0)*m2(0,1) + m1(2,1)*m2(1,1) + m1(2,2)*m2(2,1);
        res(2,2) = m1(2,0)*m2(0,2) + m1(2,1)*m2(1,2) + m1(2,2)*m2(2,2);
        return res;
    }

    /// Multiplies in-place this Mat3x by the given scalar \p s.
    ///
    Mat3x& operator*=(float s) {
        data_[0][0] *= s;
        data_[0][1] *= s;
        data_[0][2] *= s;
        data_[1][0] *= s;
        data_[1][1] *= s;
        data_[1][2] *= s;
        data_[2][0] *= s;
        data_[2][1] *= s;
        data_[2][2] *= s;
        return *this;
    }

    /// Returns the multiplication of this Mat3x by the given scalar \p s.
    ///
    Mat3x operator*(float s) const {
        return Mat3x(*this) *= s;
    }

    /// Returns the multiplication of the scalar \p s with the Mat3x \p m.
    ///
    friend Mat3x operator*(float s, const Mat3x& m) {
        return m * s;
    }

    /// Divides in-place this Mat3x by the given scalar \p s.
    ///
    Mat3x& operator/=(float s) {
        data_[0][0] /= s;
        data_[0][1] /= s;
        data_[0][2] /= s;
        data_[1][0] /= s;
        data_[1][1] /= s;
        data_[1][2] /= s;
        data_[2][0] /= s;
        data_[2][1] /= s;
        data_[2][2] /= s;
        return *this;
    }

    /// Returns the division of this Mat3x by the given scalar \p s.
    ///
    Mat3x operator/(float s) const {
        return Mat3x(*this) /= s;
    }

    /// Returns the multiplication of this Mat3x by the given Vec2x \p v.
    /// This assumes that the Vec2x represents the Vec3x(x, y, 1) in
    /// homogeneous coordinates, and then only returns the x and y coordinates
    /// of the result.
    ///
    Vec2x operator*(const Vec2x& v) const {
        return Vec2x(
            data_[0][0]*v[0] + data_[1][0]*v[1] +  data_[2][0],
            data_[0][1]*v[0] + data_[1][1]*v[1] +  data_[2][1]);
    }

    /// Returns the inverse of this Mat3x.
    ///
    /// If provided, isInvertible is set to either true or false depending on
    /// whether the matrix was considered invertible or not.
    ///
    /// The matrix is considered non-invertible whenever its determinant is
    /// less or equal than the provided epsilon. An appropriate epsilon is
    /// context-dependent, and therefore zero is used as default, which means
    /// that the matrix is considered non-invertible if and only if its
    /// determinant is exactly zero (example: the null matrix).
    ///
    /// If the matrix is considered non-invertible, then the returned matrix
    /// has all its elements set to std::numeric_limits<float>::infinity().
    ///
    VGC_CORE_API
    Mat3x inversed(bool* isInvertible = nullptr, float epsilon = 0) const;

    /// Right-multiplies this matrix by the translation matrix given
    /// by vx and vy, that is:
    ///
    /// \verbatim
    /// | 1 0 vx |
    /// | 0 1 vy |
    /// | 0 0 1  |
    /// \endverbatim
    ///
    /// Returns a reference to this Mat3x.
    ///
    Mat3x& translate(float vx, float vy = 0) {
        Mat3x m(1, 0, vx,
                0, 1, vy,
                0, 0, 1);
        return (*this) *= m;
    }

    /// Right-multiplies this matrix by the rotation matrix around
    /// the z-axis by \p t radians, that is:
    ///
    /// \verbatim
    /// | cos(t) -sin(t)  0 |
    /// | sin(t)  cos(t)  0 |
    /// | 0       0       1 |
    /// \endverbatim
    ///
    /// Returns a reference to this Mat3x.
    ///
    Mat3x& rotate(float t) {
        float s = std::sin(t);
        float c = std::cos(t);
        Mat3x m(c,-s, 0,
                s, c, 0,
                0, 0, 1);
        return (*this) *= m;
    }

    /// Right-multiplies this matrix by the uniform scaling matrix
    /// given by s, that is:
    ///
    /// \verbatim
    /// | s 0 0 |
    /// | 0 s 0 |
    /// | 0 0 1 |
    /// \endverbatim
    ///
    /// Returns a reference to this Mat3x.
    ///
    /// Note: if your 3x3 matrix is not meant to represent a 2D affine
    /// transformation, simply use the following code instead (multiplication
    /// by scalar), which also multiplies the last row and column:
    ///
    /// \code
    /// m *= s;
    /// \endcode
    ///
    Mat3x& scale(float s) {
        Mat3x m(s, 0, 0,
                0, s, 0,
                0, 0, 1);
        return (*this) *= m;
    }

    /// Right-multiplies this matrix by the non-uniform scaling matrix
    /// given by sx and sy, that is:
    ///
    /// \verbatim
    /// | sx 0  0 |
    /// | 0  sy 0 |
    /// | 0  0  1 |
    /// \endverbatim
    ///
    /// Returns a reference to this Mat3x.
    ///
    Mat3x& scale(float sx, float sy, float sz = 1) {
        Mat3x m(sx, 0,  0,
                0,  sy, 0,
                0,  0,  1);
        return (*this) *= m;
    }

private:
    float data_[3][3];
};

inline constexpr Mat3x Mat3x::identity = Mat3x(1);

} // namespace vgc::core

#endif // VGC_CORE_MAT3X_H
