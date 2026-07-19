// -*-c++-*-

/*!
  \file vector_3d.h
  \brief 3d vector class Header File.
*/

/*
 *Copyright:

 Copyright (C) Hidehisa AKIYAMA, Hiroki Shimora

 This code is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 3 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 *EndCopyright:
 */

/////////////////////////////////////////////////////////////////////

#ifndef RCSC_GEOM_VECTOR3D_H
#define RCSC_GEOM_VECTOR3D_H

#include <rcsc/geom/vector_2d.h>

#include <cmath>

namespace rcsc {

/*!
  \class Vector3D
  \brief 3D point vector class

  Minimal, additive companion to Vector2D used to represent the ball's
  height (z) alongside its existing ground-plane (x,y) position. This
  class intentionally does NOT replicate Vector2D's polar accessors
  (r()/th()) or its full operator surface -- it stays small and is
  only used where 3D-aware code (ball belief, kickable/catchable height
  gating, the new 3D interception predictor) needs it. Any code that
  only needs the ground-plane position should keep using xy().
*/
class Vector3D {
public:

    //! constant threshold value for calculation error
    static const double EPSILON;

    //! constant error value for XYZ (= std::numeric_limits< double >::max()).
    static const double ERROR_VALUE;

    //! invalidated value vector
    static const Vector3D INVALIDATED;

    double x; //!< X coordinate
    double y; //!< Y coordinate
    double z; //!< Z coordinate (height)

    /*!
      \brief default constructor.
    */
    Vector3D()
        : x( 0.0 ),
          y( 0.0 ),
          z( 0.0 )
      { }

    /*!
      \brief create Vector with XYZ value directly.
      \param xx assigned x value
      \param yy assigned y value
      \param zz assigned z value
    */
    Vector3D( const double xx,
              const double yy,
              const double zz )
        : x( xx ),
          y( yy ),
          z( zz )
      { }

    /*!
      \brief create Vector from an existing 2D vector plus a height.
      This is the dominant call-site shape: an existing 2D belief
      (ground-plane position/velocity) plus one new height scalar.
      \param xy existing 2D vector
      \param zz assigned z value
    */
    Vector3D( const Vector2D & xy,
              const double zz )
        : x( xy.x ),
          y( xy.y ),
          z( zz )
      { }

    /*!
      \brief check if this vector is valid or not.
      \return true if component values are validate.
    */
    bool isValid() const
      {
          return ( ( x != ERROR_VALUE )
                    && ( y != ERROR_VALUE )
                    && ( z != ERROR_VALUE ) );
      }

    /*!
      \brief assign XYZ value directly.
      \param xx assigned x value
      \param yy assigned y value
      \param zz assigned z value
      \return reference to itself
    */
    Vector3D & assign( const double xx,
                       const double yy,
                       const double zz )
      {
          x = xx;
          y = yy;
          z = zz;
          return *this;
      }

    /*!
      \brief invalidate this object
      \return this
    */
    const Vector3D & invalidate()
      {
          x = ERROR_VALUE;
          y = ERROR_VALUE;
          z = ERROR_VALUE;
          return *this;
      }

    /*!
      \brief get the ground-plane (x,y) projection of this vector.
      Most existing 2D algorithms should keep using this projection;
      only new 3D-aware code should touch z directly.
      \return new Vector2D holding (x,y)
    */
    Vector2D xy() const
      {
          return Vector2D( x, y );
      }

    /*!
      \brief return this vector
      \return const reference of this vector
    */
    const Vector3D & operator+() const
      {
          return *this;
      }

    /*!
      \brief create reversed vector
      \return new vector that XYZ values are reversed.
    */
    Vector3D operator-() const
      {
          return Vector3D( -x, -y, -z );
      }

    /*!
      \brief add vector to itself
      \param v added vector
      \return const reference to itself
    */
    const Vector3D & operator+=( const Vector3D & v )
      {
          x += v.x;
          y += v.y;
          z += v.z;
          return *this;
      }

    /*!
      \brief subtract vector to itself
      \param v subtract argument
      \return const reference to itself
    */
    const Vector3D & operator-=( const Vector3D & v )
      {
          x -= v.x;
          y -= v.y;
          z -= v.z;
          return *this;
      }

    /*!
      \brief multiplied by 'scalar'
      \param scalar multiplication argument
      \return const reference to itself
    */
    const Vector3D & operator*=( const double scalar )
      {
          x *= scalar;
          y *= scalar;
          z *= scalar;
          return *this;
      }

    /*!
      \brief divided by 'scalar'.
      \param scalar division argument
      \return const reference to itself
    */
    const Vector3D & operator/=( const double scalar )
      {
          if ( std::fabs( scalar ) > EPSILON )
          {
              x /= scalar;
              y /= scalar;
              z /= scalar;
          }
          return *this;
      }

    /*!
      \brief get the squared distance from this to 'p'.
      \param p target point
      \return squared distance to 'p'
    */
    double dist2( const Vector3D & p ) const
      {
          return ( std::pow( this->x - p.x, 2 )
                    + std::pow( this->y - p.y, 2 )
                    + std::pow( this->z - p.z, 2 ) );
      }

    /*!
      \brief get the distance from this to 'p'.
      \param p target point
      \return distance to 'p'
    */
    double dist( const Vector3D & p ) const
      {
          return std::sqrt( dist2( p ) );
      }

};

}

/////////////////////////////////////////////////////////////////////
// arithmetic operators

/*!
  \brief operator add(Vector3D, Vector3D)
  \param lhs left hand side variable
  \param rhs right hand side variable
  \return new vector
 */
inline
rcsc::Vector3D
operator+( const rcsc::Vector3D & lhs,
           const rcsc::Vector3D & rhs )
{
    return rcsc::Vector3D( lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z );
}

/*!
  \brief operator sub(Vector3D, Vector3D)
  \param lhs left hand side variable
  \param rhs right hand side variable
  \return new vector
 */
inline
rcsc::Vector3D
operator-( const rcsc::Vector3D & lhs,
           const rcsc::Vector3D & rhs )
{
    return rcsc::Vector3D( lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z );
}

/*!
  \brief operator mult(Vector3D, double)
  \param lhs left hand side variable
  \param rhs right hand side variable
  \return new vector
 */
inline
rcsc::Vector3D
operator*( const rcsc::Vector3D & lhs,
           const double & rhs )
{
    return rcsc::Vector3D( lhs.x * rhs, lhs.y * rhs, lhs.z * rhs );
}

/*!
  \brief operator div(Vector3D, double)
  \param lhs left hand side variable
  \param rhs right hand side variable
  \return new vector
 */
inline
rcsc::Vector3D
operator/( const rcsc::Vector3D & lhs,
           const double & rhs )
{
    rcsc::Vector3D result = lhs;
    result /= rhs;
    return result;
}

#endif
