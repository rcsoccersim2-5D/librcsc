// -*-c++-*-

/*!
  \file intercept_simulator_self_3d.h
  \brief 3D (ball height aware) self intercept simulator
*/

/*
 *Copyright:

 Copyright (C) Hidehisa AKIYAMA

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

#ifndef RCSC_PLAYER_INTERCEPT_SIMULATOR_SELF_3D_H
#define RCSC_PLAYER_INTERCEPT_SIMULATOR_SELF_3D_H

#include <rcsc/player/ball_trajectory.h>
#include <rcsc/geom/vector_3d.h>

namespace rcsc {

class WorldModel;

/*!
  \struct Intercept3D
  \brief lightweight result of the 3D self intercept simulation.

  Deprecated compatibility result for the old standalone API.  Normal
  interception uses rcsc::Intercept and InterceptSimulatorSelfV17.
*/
struct [[deprecated("use BallTrajectory3D and Intercept")]] Intercept3D {
    int cycle_; //!< cycle at which the ball height is estimated to become <= player_height (reachable)
    Vector3D pos_; //!< estimated ball 3D position at cycle_

    Intercept3D()
        : cycle_( -1 ),
          pos_( 0.0, 0.0, 0.0 )
      { }

    Intercept3D( const int cycle,
                 const Vector3D & pos )
        : cycle_( cycle ),
          pos_( pos )
      { }
};

/*!
  \class InterceptSimulatorSelf3D
  \brief deprecated adapter over the shared 3D ball trajectory.

  This class deliberately does NOT implement the InterceptSimulatorSelf
  interface.  It remains for source compatibility and delegates all state
  propagation to BallTrajectory3D, so it no longer owns a second vertical
  equation.
*/
class [[deprecated("use InterceptSimulatorSelfV17")]] InterceptSimulatorSelf3D {
public:

    InterceptSimulatorSelf3D() = default;
    ~InterceptSimulatorSelf3D() = default;

    /*!
      \brief simulate the ball's closed-form vertical (z-axis) motion and
      find the first cycle (within max_step) at which the ball height
      becomes reachable (<= player_height).
      \param wm world model
      \param max_step max prediction cycle considered
      \param result reference to the result container, only written when true is returned
      \return true if a reachable cycle was found within [0, max_step]
     */
    bool simulate( const WorldModel & wm,
                   const int max_step,
                   Intercept3D & result ) const;

};

}

#endif
