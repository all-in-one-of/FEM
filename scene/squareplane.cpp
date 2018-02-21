#include "squareplane.h"

template<class T, int dim>
bool SquarePlane<T, dim>::checkCollisions(Eigen::Matrix<T, dim, 1> &pos, Eigen::Matrix<T, dim, 1> &out_pos) const
{
    //for (unsigned int i = 0; i < particles.positions.size(); ++i) {

        // If particle is under plane
        if (pos[1] < center[1]) {

            // Check if x, z is within plane
            if (Eigen::abs(pos[0] - center[0]) < length_half
                || (dim == 3 && Eigen::abs(pos[2] - center[2]) < length_half)) {
                //TODO is there a better way to see if this is 2D or 3D?

                // Collision
                // Move particle out of plane
                //particles.positions[i][1] = center[1];
                // Update velocity?

                // fill out_pos with new position
                // Some function outside this will calculate velocity
                out_pos[0] = pos[0];
                out_pos[1] = center[1];
                if (dim == 3) {
                    out_pos[2] = pos[2];
                }
                return true;

            }

        }
    //}
    return false;
}

