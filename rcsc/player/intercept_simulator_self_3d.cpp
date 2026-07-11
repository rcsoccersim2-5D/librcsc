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
    const ServerParam & SP = ServerParam::i();
    const BallObject & ball = wm.ball();

    // In 2D mode the ball height is always 0, so it is trivially reachable
    // right now -- callers do not need to special-case 2D-mode servers.
    if ( SP.is2dMode() )
    {
        result = Intercept3D( 0, ball.pos3D() );
        return true;
    }

    const double z0 = ball.posZ();
    const double vz0 = ball.velZ();
    const double g = SP.gravity();
    const double player_height = SP.playerHeight();

    // already reachable this cycle
    if ( z0 <= player_height )
    {
        result = Intercept3D( 0, ball.pos3D() );
        return true;
    }

    // Closed-form evaluation of rcssserver's discrete semi-implicit-Euler
    // recurrence used by Ball::incZ() (vel_z -= gravity; pos_z += vel_z,
    // applied once per cycle, ignoring bounce/goal/catch special cases,
    // which do not apply while the ball is still above player_height and
    // in flight):
    //
    //   vz(t) = vz0 - t*g
    //   z(t)  = z0 + sum_{k=1}^{t} vz(k)
    //         = z0 + t*vz0 - g * t*(t+1)/2
    //
    // verified by hand to match the discrete recurrence step-by-step for
    // t=1,2 (see Step 5 verification notes).
    // Horizontal motion: as of the 2026-07-10 physics rework the ball has
    // ZERO horizontal friction while airborne (ground-only ball_decay
    // friction, applied only once pos_z<=0 -- see rcssserver's Ball::incZ()/
    // applyBounceEnergyLoss()). Every t evaluated in this loop is still in
    // the airborne phase (the loop returns as soon as z_t first drops to/
    // below player_height), so the correct ground-plane projection here is
    // constant-velocity (ball.pos() + ball.vel()*t), NOT the decaying
    // ball.inertiaPoint( t ) helper (that helper assumes ball_decay ground
    // friction and would under-estimate how far an airborne ball travels).
    for ( int t = 1; t <= max_step; ++t )
    {
        const double z_t = z0 + t*vz0 - 0.5*g*t*(t+1);

        if ( z_t <= player_height )
        {
            const Vector2D pos_t = ball.pos() + ball.vel() * t;
            result = Intercept3D( t, Vector3D( pos_t, z_t ) );
            return true;
        }
    }

    return false;
}

}
