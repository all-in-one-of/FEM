#include "FEMSolver.h"
#include "globalincludes.h"


int main()
{
    // Cook My Jello!

    FEMSolver<double,3> solver(999);
    solver.initializeMesh();
    solver.cookMyJello();

}
