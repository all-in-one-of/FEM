#pragma once

#include "globalincludes.h"
#include "mesh/TetraMesh.h"
#include "mesh/Tetrahedron.h"
#include "integrator/ForwardEuler.h"
//#include "scene/squareplane.h"
//#include "scene/sphere.h"
#include "scene/scene.h"
#include "scene/defaultScene.h"
#include "scene/plinkoScene.h"
#include "scene/constrainedTop.h"
#include "scene/bulldozeScene.h"
#include "integrator/BackwardEuler.h"


#define USE_EXPLICIT
#define USE_IMPLICIT
//#define WORK_IN_PROGRESS
#ifdef WORK_IN_PROGRESS
#include "utility/MINRES.h"
#endif


// values are for rubber;
template<class T, int dim>
double TetraMesh<T,dim>::k = 10000;
template<class T, int dim>
double TetraMesh<T,dim>::nu = 0.2;
const int divisor = 600;
constexpr float cTimeStep = 1/(24.f*divisor); //0.001f;
const float gravity = 1.0f;

// 24 frames per second
// mesh resolution
// try outputting poly file to see tetrahedra deform

template<class T, int dim>
class FEMSolver {
private:

    TetraMesh<T,dim> *mTetraMesh;
    int mSteps;
    double mu;
    double lambda;
    ForwardEuler mExplicitIntegrator;
    BackwardEuler mImplicitIntegrator;

    void calculateMaterialConstants();    // calculates mu and lambda values for material
    void precomputeTetraConstants();      // precompute tetrahedron constant values
    void computeDs(Eigen::Matrix<T,dim,dim>& Ds,
                    const Tetrahedron<T,dim>& t);       // assembles Ds matrix
    void computeF(Eigen::Matrix<T,dim,dim>& F,
                    const Eigen::Matrix<T,dim,dim>& Ds,
                    const Tetrahedron<T,dim>& t);       // computes F matrix
    void computeRS(Eigen::Matrix<T,dim,dim>& R,
                    Eigen::Matrix<T,dim,dim>& S,
                    const Eigen::Matrix<T,dim,dim>& F); // computes R  and S matrices from F using SVD
    void computeJFinvT(Eigen::Matrix<T,dim,dim>& JFinvT,
                    const Eigen::Matrix<T,dim,dim>& F); // computes det(F) * (F^-1)^T
    void computeK(Eigen::MatrixXf& KMatrix,
                    const Eigen::Matrix<T,dim,dim>& F,
                    const Eigen::Matrix<T,dim,dim>& JFinvT,
                    const Eigen::Matrix<T,dim,dim>& R,
                    const Eigen::Matrix<T,dim,dim>& S);
    void distributeMass();          // distributes tetrahedron mass to its constituent particles

    // helper functions for computeK
    float DsqPsiDsqF(int j, int k, int m, int n,
                    const Eigen::Matrix<T,dim,dim>& F,
                    const Eigen::Matrix<T,dim,dim>& JFinvT,
                    const Eigen::Matrix<T,dim,dim>& R,
                    const Eigen::Matrix<T,dim,dim>& S);
    float DFDx(int m, int n, int q, int r, const Tetrahedron<T,dim>& t);

public:
    FEMSolver(int steps);
    ~FEMSolver();

    void initializeMesh();
    void cookMyJello();
};

template<class T, int dim>
FEMSolver<T,dim>::FEMSolver(int steps) : mTetraMesh(nullptr), mSteps(steps), mu(0.0), lambda(0.0), mExplicitIntegrator("explicit"), mImplicitIntegrator("implicit") {}

template<class T, int dim>
FEMSolver<T,dim>::~FEMSolver(){
    //delete mTetraMesh;
}

template<class T, int dim>
void FEMSolver<T,dim>::initializeMesh() {

    // Initialize mTetraMesh here
    mTetraMesh = new TetraMesh<T,dim>("objects/cube.1");
    mTetraMesh->generateTetras();
    //mTetraMesh->generateSimpleTetrahedron();
}

template<class T, int dim>
void FEMSolver<T,dim>::cookMyJello() {

    // Create a basic ground plane
    //DefaultScene<T, dim> scene = DefaultScene<T, dim>();

    // Create plinko scene
    //PlinkoScene<T, dim> scene = PlinkoScene<T, dim>();
    
    // Create a sphere collision scene
    BulldozeScene<T, dim> scene = BulldozeScene<T, dim>();

    // calculate deformation constants
    calculateMaterialConstants();
    std::cout << mu << std::endl;
    std::cout << lambda << std::endl;
    // precompute tetrahedron constant values
    precomputeTetraConstants();
    // distribute mass to tetrahedra particles
    distributeMass();

    // deformation gradient matrix
    Eigen::Matrix<T,dim,dim> F = Eigen::Matrix<T,dim,dim>::Zero(dim,dim);
    // deformed tetrahedron matrix
    Eigen::Matrix<T,dim,dim> Ds = Eigen::Matrix<T,dim,dim>::Zero(dim,dim);
    // SVD rotation matrix
    Eigen::Matrix<T,dim,dim> R = Eigen::Matrix<T,dim,dim>::Zero(dim,dim);
    // SVD scale matrix
    Eigen::Matrix<T,dim,dim> S = Eigen::Matrix<T,dim,dim>::Zero(dim,dim);
    // Piola stress tensor
    Eigen::Matrix<T,dim,dim> P = Eigen::Matrix<T,dim,dim>::Zero(dim,dim);
    // Force matrix
    Eigen::Matrix<T,dim,dim> G = Eigen::Matrix<T,dim,dim>::Zero(dim,dim);
    // momentum conservation
    Eigen::Matrix<T,dim,1> force = Eigen::Matrix<T,dim,1>::Zero(dim);
    // det(F) * (F^-1)^T term
    Eigen::Matrix<T,dim,dim> JFinvT = Eigen::Matrix<T,dim,dim>::Zero(dim,dim);

    int size = mTetraMesh->mParticles.positions.size();
    //std::vector<Eigen::Matrix<T, dim, 1>> past_pos(mTetraMesh->mParticles.positions);
    Eigen::Matrix<T, dim, 1> temp_pos = Eigen::Matrix<T,dim,1>::Zero(dim);

    // <<<<< FOR SCALING TEST
    // for(int i = 0; i < size; ++i){
    //     mTetraMesh->mParticles.positions[i] *= 0.75;
    // }

    int numSteps = (mSteps / 24) / (cTimeStep);

    // <<<<< Time Loop BEGIN
    int currFrame = 0;

    for(int i = 0; i < numSteps; ++i)
    {
        mTetraMesh->mParticles.zeroForces();
        // <<<<< force update BEGIN
        for(Tetrahedron<T,dim> &t : mTetraMesh->mTetras){
            force = Eigen::Matrix<T,dim,1>::Zero(dim);
            computeDs(Ds, t);
            computeF(F, Ds, t);
            computeRS(R, S, F);
            computeJFinvT(JFinvT, F);
            P = 2 * mu * (F - R) + lambda * (F.determinant() - 1) * JFinvT;
            G = P * t.mVolDmInvT;

            for(int j = 1; j < dim + 1; ++j){
                if(G.col(j - 1).norm() > 1E-10){
                    mTetraMesh->mParticles.forces[t.mPIndices[j]] += G.col(j - 1);
                    force += G.col(j - 1);
                }
            }
            mTetraMesh->mParticles.forces[t.mPIndices[0]] += -force;

        }
        // <<<<< force update END
        // <<<<< Integration BEGIN



#ifdef USE_EXPLICIT

        for(int j = 0; j < size; ++j) {

            temp_pos = Eigen::Matrix<T,dim,1>::Zero(dim);

            State<T, dim> currState;
            State<T, dim> newState;

            currState.mComponents[POS] = mTetraMesh->mParticles.positions[j];
            currState.mComponents[VEL] = mTetraMesh->mParticles.velocities[j];
            currState.mMass = mTetraMesh->mParticles.masses[j];
            currState.mComponents[FOR] = mTetraMesh->mParticles.forces[j] / currState.mMass + Eigen::Matrix<T,dim,1>(0, -gravity, 0);
            //currState.mComponents[FOR] = mTetraMesh->mParticles.forces[j] / currState.mMass;

            mExplicitIntegrator.integrate(cTimeStep, 0, currState, newState);

            scene.updatePosition(cTimeStep);

            // <<<<<< FOR SCENE COLLISIONS
            if(scene.checkCollisions(newState.mComponents[POS], temp_pos)){
                newState.mComponents[POS] = temp_pos;
                newState.mComponents[VEL] = (temp_pos - currState.mComponents[POS]) / cTimeStep;
            }

            // <<<<<< FOR HANGING TESTS
            // if(currState.mComponents[POS][0] <= 0.001){
            //     newState.mComponents[POS] = currState.mComponents[POS];
            //     newState.mComponents[VEL] = currState.mComponents[VEL];
            // }

            mTetraMesh->mParticles.positions[j] = newState.mComponents[POS];
            mTetraMesh->mParticles.velocities[j] = newState.mComponents[VEL];

            // <<<<< FOR PULLING A CORNER
            // if(i == 1800 && j == 6){
            //     mTetraMesh->mParticles.positions[j] += Eigen::Matrix<T,dim,1>(0.2, 0.2, 0.2);
            // }
            // if(i == 1800 && j == 26){
            //     mTetraMesh->mParticles.positions[j] += Eigen::Matrix<T,dim,1>(0.1, 0.1, 0.1);
            // }
            // if(i == 1800 && j == 31){
            //     mTetraMesh->mParticles.positions[j] += Eigen::Matrix<T,dim,1>(0.1, 0.1, 0.1);
            // }
            // if(i == 1800 && j == 38){
            //     mTetraMesh->mParticles.positions[j] += Eigen::Matrix<T,dim,1>(0.1, 0.1, 0.1);
            // }
        }

#endif

#ifdef USE_IMPLICIT

        // TODO : Implicit Integrator Here

        const int n = size;
        const int dimen = dim * n;

        // 1. Calculate A Matrix Here
        float a1 = mTetraMesh->mParticles.masses[0] / (cTimeStep * cTimeStep);

        Eigen::MatrixXf A1Mat(dimen, 1);
        A1Mat.setConstant(a1);

        Eigen::MatrixXf AMatrix = A1Mat.asDiagonal();

        // 2. Calculate K Matrix here

        Eigen::MatrixXf KMatrix(dimen, dimen);

        KMatrix.setZero();



        // <<< Zach to Update Here >>>

        std::cout << "Break 1" << std::endl;

        // 3. Do A = A + K

        AMatrix += KMatrix;

        std::cout << "Break 2" << std::endl;

        // 4. Calculate B Matrix

        float b1 = mTetraMesh->mParticles.masses[0] / (cTimeStep);
        Eigen::MatrixXf B1Mat(dimen, 1);

        for(int i = 0; i < size; ++i) {

            // Doing Calculations as:
            // B = Vn * mass/(dt) + f + mg

            for(int k = 0; k < dim; ++k) {
                B1Mat(dim * i + k, 0) = mTetraMesh->mParticles.velocities[i](k) * b1 + mTetraMesh->mParticles.forces[i](k) + k == 1? mTetraMesh->mParticles.masses[i] * -gravity : 0;
            }
        }

        std::cout << "Break 3" << std::endl;

        // 5. Solve Ax = B

        Eigen::MatrixXf dxMat(dimen, 1);
        dxMat.setZero();

#ifdef WORK_IN_PROGRESS

        Eigen::MINRES<Eigen::MatrixXf> mr;

        mr.compute(AMatrix);
        dxMat = mr.solve(B1Mat);

#endif

        std::cout << "Break 4" << std::endl;



        // 6. Update velocities and position with dx

        Eigen::Matrix<T, dim, 1> newPos;
        Eigen::Matrix<T, dim, 1> newVel;

        // Adding collision tests here
        for(int i = 0; i < size; ++i) {

            for(int k = 0; k < dim; ++k) {
                newPos(k, 0) = dxMat(i * dim + k, 0);
            }

            // v(n + 1) = v(n) + dx/dt;
            newVel = mTetraMesh->mParticles.velocities[i] + newPos /  cTimeStep;

            // x(n + 1) = x(n) + dx;
            newPos = mTetraMesh->mParticles.positions[i] + newPos;

            if(scene.checkCollisions(newPos, temp_pos)){
                newPos = temp_pos;
                newVel = (temp_pos - mTetraMesh->mParticles.positions[i]) / cTimeStep;
            }

            mTetraMesh->mParticles.positions[i] = newPos;
            mTetraMesh->mParticles.velocities[i] = newVel;

        }

        std::cout << "Break 5" << std::endl;

#endif
        // <<<<< Integration END

       if(i % divisor == 0  || i == 0)
       {
         mTetraMesh->outputFrame(currFrame);
         scene.outputFrame(currFrame);
         currFrame++;
       }
    }
    // <<<<< Time Loop END
}

template<class T, int dim>
void FEMSolver<T,dim>::calculateMaterialConstants(){
    mu = TetraMesh<T,dim>::k / (2 * (1. + TetraMesh<T,dim>::nu));
    lambda = (TetraMesh<T,dim>::k * TetraMesh<T,dim>::nu) / ((1. + TetraMesh<T,dim>::nu)*(1. - 2*TetraMesh<T,dim>::nu));
}

template<class T, int dim>
void FEMSolver<T,dim>::precomputeTetraConstants(){
    std::vector<Eigen::Matrix<T,dim,1>> positions;
    for(Tetrahedron<T,dim> &t : mTetraMesh->mTetras){
        positions.clear();
        for(int i = 0; i < dim + 1; ++i){
            positions.push_back(mTetraMesh->mParticles.positions[t.mPIndices[i]]);
        }
        t.precompute(positions);
    }
}

template<class T, int dim>
void FEMSolver<T,dim>::computeDs(Eigen::Matrix<T,dim,dim>& Ds, const Tetrahedron<T,dim>& t){
    std::vector<Eigen::Matrix<T,dim,1>> x;
    for(int i = 0; i < dim + 1; ++i){
        x.push_back(mTetraMesh->mParticles.positions[t.mPIndices[i]]);
    }
    for(int i = 1; i < dim + 1; ++i){
        Ds.col(i - 1) = x[i] - x[0];
    }
}

template<class T, int dim>
void FEMSolver<T,dim>::computeF(Eigen::Matrix<T,dim,dim>& F,
                    const Eigen::Matrix<T,dim,dim>& Ds,
                    const Tetrahedron<T,dim>& t){
    F = Ds * t.mDmInv;
}

template<class T, int dim>
void FEMSolver<T,dim>::computeRS(Eigen::Matrix<T,dim,dim>& R,
                    Eigen::Matrix<T,dim,dim>& S,
                    const Eigen::Matrix<T,dim,dim>& F){
    Eigen::JacobiSVD<Eigen::Matrix<T,dim,dim>> svd(F, Eigen::ComputeFullU | Eigen::ComputeFullV);
    Eigen::Matrix<T,dim,dim> U = svd.matrixU();
    Eigen::Matrix<T,dim,dim> V = svd.matrixV();
    Eigen::Matrix<T,dim,dim> sigma = Eigen::Matrix<T,dim,dim>::Zero(dim, dim);

    for(int i = 0; i < dim; ++i){
        sigma(i, i) = svd.singularValues()[i];
    }

    if(U.determinant() < -1E-5){
        U.col(dim - 1) = -1 * U.col(dim - 1);
        sigma(dim - 1, dim - 1) = -1 * sigma(dim - 1, dim - 1);
    }
    if(V.determinant() < -1E-5){
        V.col(dim - 1) = -1 * V.col(dim - 1);
        sigma(dim - 1, dim - 1) = -1 * sigma(dim - 1, dim - 1);
    }

    R = U * V.transpose();
    S = V * sigma * V.transpose();
}

template<class T, int dim>
void FEMSolver<T,dim>::computeJFinvT(Eigen::Matrix<T,dim,dim>& JFinvT, const Eigen::Matrix<T,dim,dim>& F){
    switch(dim){
        case 2:
            JFinvT(0,0) = F(1,1);
            JFinvT(0,1) = -F(0,1);
            JFinvT(1,0) = -F(1,0);
            JFinvT(1,1) = F(0,0);
            break;
        case 3:
            JFinvT(0,0) = F(1,1)*F(2,2) - F(1,2)*F(2,1);
            JFinvT(0,1) = F(0,2)*F(2,1) - F(0,1)*F(2,2);
            JFinvT(0,2) = F(0,1)*F(1,2) - F(0,2)*F(1,1);
            JFinvT(1,0) = F(1,2)*F(2,0) - F(1,0)*F(2,2);
            JFinvT(1,1) = F(0,0)*F(2,2) - F(0,2)*F(2,0);
            JFinvT(1,2) = F(0,2)*F(1,0) - F(0,0)*F(1,2);
            JFinvT(2,0) = F(1,0)*F(2,1) - F(1,1)*F(2,0);
            JFinvT(2,1) = F(0,1)*F(2,0) - F(0,0)*F(2,1);
            JFinvT(2,2) = F(0,0)*F(1,1) - F(0,1)*F(1,0);
            break;
        default: std::cout << "error: dimension must be 2 or 3" << std::endl;
    }
    JFinvT.transposeInPlace();
}

template<class T, int dim>
void FEMSolver<T,dim>::distributeMass(){
    for(Tetrahedron<T,dim> &t : mTetraMesh->mTetras){
        for(int i = 0; i < dim + 1; ++i){
            // distribute 1/4 of mass to each tetrahedron point
            mTetraMesh->mParticles.masses[t.mPIndices[i]] += 0.25 * t.mass;
        }
    }
}

//////// K MATRIX COMPUTATION //////////

template<class T, int dim>
void FEMSolver<T,dim>::computeK(Eigen::MatrixXf& KMatrix,
                const Eigen::Matrix<T,dim,dim>& F,
                const Eigen::Matrix<T,dim,dim>& JFinvT,
                const Eigen::Matrix<T,dim,dim>& R,
                const Eigen::Matrix<T,dim,dim>& S)
{
    for(Tetrahedron<T,dim> t : mTetraMesh->mTetras){
        Eigen::Matrix<T,dim,dim> K = Eigen::Matrix<T,dim,dim>::Zero(dim, dim);
        for(int p = 0; p < dim + 1; ++p){
            for(int q = 0; q < dim + 1; ++q){
                for(int i = 0; i < dim; ++i){
                    for(int r = 0; r < dim; ++r){
                        for(int m = 0; m < dim; ++m){
                            for(int n = 0; n < dim; ++n){
                                for(int j = 0; j < dim; ++j){
                                    for(int k = 0; k < dim; ++k){
                                        K(3 * p + i, 3 * q + r) += -1 * t.volume * DsqPsiDsqF(j, k, m, n, F, JFinvT, R, S) * DFDx(m, n, q, r, t) * DFDx(j, k, p, i, t);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        // add to global K here
    }
}

template<class T, int dim>
float FEMSolver<T,dim>::DsqPsiDsqF(int j, int k, int m, int n,
                    const Eigen::Matrix<T,dim,dim>& F,
                    const Eigen::Matrix<T,dim,dim>& JFinvT,
                    const Eigen::Matrix<T,dim,dim>& R,
                    const Eigen::Matrix<T,dim,dim>& S)
{
    return 0.0;
}

template<class T, int dim>
float FEMSolver<T,dim>::DFDx(int m, int n, int q, int r, const Tetrahedron<T,dim>& t){
    return 0.0;
}