// -*-c++-*-

/*!
  \file ball_trajectory.h
  \brief server-compatible grounded/airborne ball trajectory
*/

#ifndef RCSC_PLAYER_BALL_TRAJECTORY_H
#define RCSC_PLAYER_BALL_TRAJECTORY_H

#include <rcsc/geom/vector_2d.h>
#include <rcsc/geom/vector_3d.h>

#include <vector>

namespace rcsc {

class BallObject;

/*!\brief immutable trajectory samples used during one decision cycle.

  Player reach remains planar.  The vertical component is an additional
  control-height predicate, matching the server's kick and catch rules.
  This is a deterministic local trajectory: stochastic wind/noise and
  post/crossbar geometry collisions are intentionally not replayed.
*/
class BallTrajectory3D {
public:
    enum class Mode {
        PLANAR,
        KNOWN_3D,
        UNKNOWN_VERTICAL
    };

    struct State {
        Vector3D pos;
        Vector3D vel;
        Mode mode;
        bool grounded;

        State()
            : pos(),
              vel(),
              mode( Mode::PLANAR ),
              grounded( true )
          { }
    };

private:
    static constexpr int MAX_CYCLE = 100;
    static constexpr double EPS = 1.0e-6;

    Vector3D M_initial_pos;
    Vector3D M_initial_vel;
    Mode M_mode;
    std::vector< State > M_states;

    void buildStates();

public:
    BallTrajectory3D();
    explicit BallTrajectory3D( const BallObject & ball );
    BallTrajectory3D( const Vector3D & pos,
                      const Vector3D & vel,
                      bool pos_z_valid,
                      bool vel_z_valid );

    //! Construct a deterministic hypothetical trajectory (for pass planning).
    static BallTrajectory3D hypothetical( const Vector3D & pos,
                                          const Vector3D & vel )
      {
          return BallTrajectory3D( pos, vel, true, true );
      }

    void update( const BallObject & ball );

    Mode mode() const { return M_mode; }
    bool isPlanar() const { return M_mode == Mode::PLANAR; }
    bool isKnown3D() const { return M_mode == Mode::KNOWN_3D; }
    bool isVerticalUnknown() const
      {
          return M_mode == Mode::UNKNOWN_VERTICAL;
      }

    bool stateAt( int cycle, State & result ) const;
    bool position2D( int cycle, Vector2D & result ) const;
    double heightAt( int cycle ) const;
    bool canControlAt( int cycle, double player_height ) const;

    static int maxCycle() { return MAX_CYCLE; }
};

}

#endif
