// -*-c++-*-

/*!
  \file ball_trajectory.cpp
  \brief server-compatible grounded/airborne ball trajectory
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "ball_trajectory.h"
#include "ball_object.h"

#include <rcsc/common/server_param.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace rcsc {

namespace {

BallTrajectory3D::State
stepState( const BallTrajectory3D::State & current )
{
    const ServerParam & SP = ServerParam::i();
    const double decay = SP.ballDecay();
    const double gravity = SP.gravity();
    const double restitution = SP.ballBounceRestitution();

    BallTrajectory3D::State next = current;
    next.pos.x = current.pos.x + current.vel.x;
    next.pos.y = current.pos.y + current.vel.y;
    next.vel.x = current.vel.x * decay;
    next.vel.y = current.vel.y * decay;

    const bool resting = ( current.pos.z <= 0.0
                           && current.vel.z == 0.0 );
    if ( resting )
    {
        next.pos.z = 0.0;
        next.vel.z = 0.0;
    }
    else
    {
        next.vel.z = current.vel.z - gravity;
    }

    const double z0 = current.pos.z;
    const double new_z = resting ? 0.0 : z0 + next.vel.z;
    bool bounced = false;

    const auto settleThreshold = [&]() {
        if ( restitution >= 1.0 || gravity <= 0.0 )
        {
            return SP.bounceStopSpeed();
        }
        const double vz_star = ( restitution * gravity )
            / ( 1.0 + restitution );
        return std::max( SP.bounceStopSpeed(), vz_star * 1.0001 );
    };

    const auto applyBounceEnergyLoss = [&]() {
        next.vel.x *= restitution;
        next.vel.y *= restitution;
    };

    if ( ! resting )
    {
        if ( SP.preciseBounceTiming()
             && z0 > 0.0
             && new_z <= 0.0 )
        {
            const double frac = z0 / ( z0 - new_z );
            const double candidate = -next.vel.z * restitution;

            if ( std::fabs( candidate ) < settleThreshold() )
            {
                next.vel.z = 0.0;
                next.pos.z = 0.0;
                applyBounceEnergyLoss();
            }
            else
            {
                const double remaining = 1.0 - frac;
                next.vel.z = candidate;
                applyBounceEnergyLoss();
                next.pos.z = std::max( 0.0,
                                       candidate * remaining
                                       - 0.5 * gravity * remaining * remaining );
            }
            bounced = true;
        }
        else
        {
            next.pos.z = new_z;
        }
    }

    if ( ! resting && ! bounced && next.pos.z <= 0.0 )
    {
        next.pos.z = 0.0;
        const double candidate = -next.vel.z * restitution;
        if ( std::fabs( candidate ) < settleThreshold() )
        {
            next.vel.z = 0.0;
            applyBounceEnergyLoss();
        }
        else
        {
            next.vel.z = candidate;
            applyBounceEnergyLoss();
        }
    }

    if ( next.pos.z > 1.0e-6 && decay > 1.0e-9 )
    {
        next.vel.x /= decay;
        next.vel.y /= decay;
    }

    if ( next.pos.z <= 0.0 )
    {
        next.pos.z = 0.0;
        next.grounded = ( next.vel.z == 0.0 );
        if ( next.grounded )
        {
            const double speed_xy = std::sqrt( next.vel.x * next.vel.x
                                               + next.vel.y * next.vel.y );
            if ( speed_xy > 0.0 && speed_xy < SP.rollStopSpeed() )
            {
                next.vel.x = 0.0;
                next.vel.y = 0.0;
            }
        }
    }
    else
    {
        next.grounded = false;
    }

    return next;
}

}

BallTrajectory3D::BallTrajectory3D()
    : M_initial_pos(),
      M_initial_vel(),
      M_mode( Mode::PLANAR ),
      M_states()
{
    buildStates();
}

BallTrajectory3D::BallTrajectory3D( const BallObject & ball )
    : M_initial_pos( ball.pos3D() ),
      M_initial_vel( ball.vel3D() ),
      M_mode( Mode::PLANAR ),
      M_states()
{
    if ( ServerParam::i().is2dMode() || ! ball.posZValid() )
    {
        M_mode = Mode::PLANAR;
    }
    else if ( ! ball.velZValid() )
    {
        M_mode = Mode::UNKNOWN_VERTICAL;
    }
    else
    {
        M_mode = Mode::KNOWN_3D;
    }
    buildStates();
}

BallTrajectory3D::BallTrajectory3D( const Vector3D & pos,
                                    const Vector3D & vel,
                                    const bool pos_z_valid,
                                    const bool vel_z_valid )
    : M_initial_pos( pos ),
      M_initial_vel( vel ),
      M_mode( Mode::PLANAR ),
      M_states()
{
    if ( ServerParam::i().is2dMode() || ! pos_z_valid )
    {
        M_mode = Mode::PLANAR;
    }
    else if ( ! vel_z_valid )
    {
        M_mode = Mode::UNKNOWN_VERTICAL;
    }
    else
    {
        M_mode = Mode::KNOWN_3D;
    }
    buildStates();
}

void
BallTrajectory3D::update( const BallObject & ball )
{
    M_initial_pos = ball.pos3D();
    M_initial_vel = ball.vel3D();

    if ( ServerParam::i().is2dMode() || ! ball.posZValid() )
    {
        M_mode = Mode::PLANAR;
    }
    else if ( ! ball.velZValid() )
    {
        M_mode = Mode::UNKNOWN_VERTICAL;
    }
    else
    {
        M_mode = Mode::KNOWN_3D;
    }
    buildStates();
}

void
BallTrajectory3D::buildStates()
{
    M_states.clear();
    M_states.reserve( MAX_CYCLE + 1 );

    State initial;
    initial.pos = M_initial_pos;
    initial.vel = M_initial_vel;
    initial.mode = M_mode;
    if ( M_mode == Mode::PLANAR )
    {
        initial.pos.z = 0.0;
        initial.vel.z = 0.0;
    }
    if ( initial.pos.z < 0.0 ) initial.pos.z = 0.0;
    initial.grounded = ( initial.pos.z <= 0.0
                         && initial.vel.z == 0.0 );
    M_states.push_back( initial );

    if ( M_mode == Mode::UNKNOWN_VERTICAL ) return;

    if ( M_mode == Mode::PLANAR )
    {
        for ( int cycle = 1; cycle <= MAX_CYCLE; ++cycle )
        {
            State next = M_states.back();
            next.pos.x += next.vel.x;
            next.pos.y += next.vel.y;
            next.vel.x *= ServerParam::i().ballDecay();
            next.vel.y *= ServerParam::i().ballDecay();
            next.pos.z = 0.0;
            next.vel.z = 0.0;
            next.mode = Mode::PLANAR;
            next.grounded = true;
            M_states.push_back( next );
        }
        return;
    }

    for ( int cycle = 1; cycle <= MAX_CYCLE; ++cycle )
    {
        State next = stepState( M_states.back() );
        next.mode = M_mode;
        M_states.push_back( next );
    }
}

bool
BallTrajectory3D::stateAt( const int cycle,
                            State & result ) const
{
    if ( cycle < 0 || cycle >= static_cast< int >( M_states.size() ) )
    {
        return false;
    }
    if ( M_mode == Mode::UNKNOWN_VERTICAL && cycle > 0 ) return false;
    result = M_states[cycle];
    return true;
}

bool
BallTrajectory3D::position2D( const int cycle,
                              Vector2D & result ) const
{
    State state;
    if ( ! stateAt( cycle, state ) ) return false;
    result = state.pos.xy();
    return true;
}

double
BallTrajectory3D::heightAt( const int cycle ) const
{
    State state;
    if ( ! stateAt( cycle, state ) )
    {
        return std::numeric_limits< double >::quiet_NaN();
    }
    return state.pos.z;
}

bool
BallTrajectory3D::canControlAt( const int cycle,
                                const double player_height ) const
{
    if ( cycle < 0 ) return false;
    if ( M_mode == Mode::UNKNOWN_VERTICAL )
    {
        return cycle == 0 && M_initial_pos.z <= player_height;
    }
    const double height = heightAt( cycle );
    return std::isfinite( height ) && height <= player_height;
}

}
