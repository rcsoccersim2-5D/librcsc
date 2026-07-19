// -*-c++-*-

/*!
  \file intercept_simulator_self_3d.cpp
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "intercept_simulator_self_3d.h"

#include "ball_object.h"
#include "world_model.h"

#include <rcsc/common/server_param.h>

namespace rcsc {

/*-------------------------------------------------------------------*/
bool
InterceptSimulatorSelf3D::simulate( const WorldModel & wm,
                                     const int max_step,
                                     Intercept3D & result ) const
{
    const BallObject & ball = wm.ball();
    const BallTrajectory3D trajectory( ball );

    if ( trajectory.canControlAt( 0, ServerParam::i().playerHeight() ) )
    {
        BallTrajectory3D::State state;
        if ( trajectory.stateAt( 0, state ) )
        {
            result = Intercept3D( 0, state.pos );
            return true;
        }
    }

    for ( int t = 1; t <= max_step; ++t )
    {
        BallTrajectory3D::State state;
        if ( ! trajectory.stateAt( t, state ) ) return false;
        if ( trajectory.canControlAt( t, ServerParam::i().playerHeight() ) )
        {
            result = Intercept3D( t, state.pos );
            return true;
        }
    }

    return false;
}

}
