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
#include <Eigen/IterativeLinearSolvers>
#include <unsupported/Eigen/IterativeSolvers>

#define USE_EXPLICIT
//#define USE_IMPLICIT

// values are for rubber;
template<class T, int dim>
double TetraMesh<T,dim>::k = 500000.f;
template<class T, int dim>
double TetraMesh<T,dim>::nu = 0.3f;

#ifdef USE_EXPLICIT
const double cTimeStep = 1e-5;
const int stepsPerFrame = 600;
#endif
#ifdef USE_IMPLICIT
const double cTimeStep = 0.01;
const int stepsPerFrame = 10;
#endif

const double gravity = 9.8f;
const double epsilon = 1e-9;

inline double epsilonCheck(double n) {
    if (std::abs(n) < epsilon) {
        return 0;
    }
    return n;
}

inline double epsilonCheckSquareMatrix(Eigen::Matrix<T,dim,dim> &matrix) {
    // epsilon check
    for (int i = 0; i < dim; ++i) {
        for (int j = 0; j < dim; ++j) {
            matrix(i, j) = epsilonCheck(matrix(i, j));
        }
    }
}

template<class T, int dim>
class FEMSolver {
private:

    TetraMesh<T,dim> mTetraMesh;
    int mSteps;
    double mu;
    double lambda;
    ForwardEuler<T, dim> mExplicitIntegrator;
    BackwardEuler<T, dim> mImplicitIntegrator;

    void calculateMaterialConstants();    // calculates mu and lambda values for material
    void precomputeTetraConstants();      // precompute tetrahedron constant values
    void computeDs(Eigen::Matrix<T,dim,dim>& Ds,
                    const Tetrahedron<T,dim>& t);       // assembles Ds matrix
    void computeDm();                                   // precomputes Dm matrices
    void computeF(Eigen::Matrix<T,dim,dim>& F,
                    const Eigen::Matrix<T,dim,dim>& Ds,
                    const Tetrahedron<T,dim>& t);       // computes F matrix
    void computeRS(Eigen::Matrix<T,dim,dim>& R,
                    Eigen::Matrix<T,dim,dim>& S,
                    const Eigen::Matrix<T,dim,dim>& F); // computes R and S matrices from F using SVD
    void computeJFinvT(Eigen::Matrix<T,dim,dim>& JFinvT,
                    const Eigen::Matrix<T,dim,dim>& F); // computes det(F) * (F^-1)^T
    void computeK(Eigen::MatrixXf& KMatrix,
                    const Eigen::Matrix<T,dim,dim>& F,
                    const Eigen::Matrix<T,dim,dim>& JFinvT,
                    const Eigen::Matrix<T,dim,dim>& R,
                    const Eigen::Matrix<T,dim,dim>& S);
    void distributeMass();          // distributes tetrahedron mass to its constituent particles

    // helper functions for computeK
    double DsqPsiDsqF(int j, int k, int m, int n,
                    const Eigen::Matrix<T,dim,dim>& F,
                    const Eigen::Matrix<T,dim,dim>& JFinvT,
                    const Eigen::Matrix<T,dim,dim>& R,
                    const Eigen::Matrix<T,dim,dim>& S);
    double DFDx(int m, int n, int q, int r, const Tetrahedron<T,dim>& t);
    // helper functions for DsPsiDsqF
    double DFDF(int j, int k, int m, int n);
    double DRDF(int j, int k, int m, int n,
                const Eigen::Matrix<T,dim,dim>& R,
                const Eigen::Matrix<T,dim,dim>& S);
    double DHDF(int j, int k, int m, int n,
                const Eigen::Matrix<T,dim,dim>& F);
    void computeAinv(Eigen::Matrix<T,dim,dim>& A,
                const Eigen::Matrix<T,dim,dim>& S);
    double leviCevita(int i, int j, int k);


public:
    FEMSolver(int steps);
    ~FEMSolver();

    void initializeMesh();
    void cookMyJello();
};

template<class T, int dim>
FEMSolver<T,dim>::FEMSolver(int steps) : mTetraMesh(TetraMesh<T,dim>("objects/cube.1")), mSteps(steps), mu(0.0f), lambda(0.0f), mExplicitIntegrator("explicit"), mImplicitIntegrator("implicit") {
}

template<class T, int dim>
FEMSolver<T,dim>::~FEMSolver(){
}

template<class T, int dim>
void FEMSolver<T,dim>::initializeMesh() {
    mTetraMesh.generateTetras();
}

template<class T, int dim>
void FEMSolver<T,dim>::cookMyJello() {

    // Create a basic ground plane
    DefaultScene<T, dim> scene = DefaultScene<T, dim>();

    // Create plinko scene
    //PlinkoScene<T, dim> scene = PlinkoScene<T, dim>();

    // Create a sphere collision scene
    //BulldozeScene<T, dim> scene = BulldozeScene<T, dim>();

    // calculate deformation constants
    calculateMaterialConstants();
    std::cout << mu << std::endl;
    std::cout << lambda << std::endl;
    // precompute Dm matrices
    computeDm();
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
    // det(F) * (F^-1)^T term
    Eigen::Matrix<T,dim,dim> JFinvT = Eigen::Matrix<T,dim,dim>::Zero(dim,dim);

    int size = mTetraMesh.mParticles.positions.size();
    //std::vector<Eigen::Matrix<T, dim, 1>> past_pos(mTetraMesh->mParticles.positions);
    Eigen::Matrix<T, dim, 1> temp_pos = Eigen::Matrix<T,dim,1>::Zero(dim);

    //<<<<< FOR SCALING TEST
    // for(int i = 0; i < size; ++i){
    //     mTetraMesh.mParticles.positions[i] += Eigen::Matrix<T,dim,1>(1.0f,0.0,0.0);
    // }

    // <<<<< Time Loop BEGIN
    for(int z = 1; z <= mSteps; ++z){
        for(int i = 0; i < stepsPerFrame; ++i)
        {
            mTetraMesh.mParticles.zeroForces();
            // <<<<< force update BEGIN
            for(Tetrahedron<T,dim> &t : mTetraMesh.mTetras){
                computeDs(Ds, t);
                computeF(F, Ds, t);
                computeRS(R, S, F);
                computeJFinvT(JFinvT, F);
                double J = F.determinant();
                P = 2.f * mu * (F - R) + lambda * (J - 1.f) * JFinvT;
                //P = mu * (F - (1.f/J) * JFinvT) + lambda * std::log(J) * (1.f/J) * JFinvT;
                G = -1 * P * t.mVolDmInvT;
                epsilonCheckSquareMatrix(G);

                for(int j = 0; j < dim; ++j){
                    mTetraMesh.mParticles.forces[t.mPIndices[j]] += G.col(j);
                }
                mTetraMesh.mParticles.forces[t.mPIndices[3]] += -1.f * (G.col(0) + G.col(1) + G.col(2));
            }

    // <<<<< force update END
    // <<<<< Integration BEGIN

    #ifdef USE_EXPLICIT

            for(int j = 0; j < size; ++j) {

                temp_pos = Eigen::Matrix<T,dim,1>::Zero(dim);

                State<T, dim> currState;
                State<T, dim> newState;

                currState.mComponents[POS] = mTetraMesh.mParticles.positions[j];
                currState.mComponents[VEL] = mTetraMesh.mParticles.velocities[j];
                currState.mMass = mTetraMesh.mParticles.masses[j];
                currState.mComponents[FOR] = mTetraMesh.mParticles.forces[j];
                currState.mComponents[FOR][1] += -gravity * mTetraMesh.mParticles.masses[j];

                mExplicitIntegrator.integrate(cTimeStep, 0, currState, newState);

                scene.updatePosition(cTimeStep);

                //<<<<<< FOR SCENE COLLISIONS TYPE 1
                // if(scene.checkCollisions(newState.mComponents[POS], temp_pos)){
                //     newState.mComponents[POS] = temp_pos;
                //     newState.mComponents[VEL] = (temp_pos - currState.mComponents[POS]) / cTimeStep;
                // }
                //<<<<<< FOR SCENE COLLISIONS TYPE 2
                if(scene.checkCollisions(newState.mComponents[POS], temp_pos)){
                    newState.mComponents[POS] = currState.mComponents[POS];
                    newState.mComponents[VEL] = Eigen::Matrix<T,dim,1>(0,0,0);
                }

                // <<<<<< FOR HANGING TESTS
                // if(currState.mComponents[POS][0] <= 0.001){
                //     newState.mComponents[POS] = currState.mComponents[POS];
                //     newState.mComponents[VEL] = currState.mComponents[VEL];
                // }

                mTetraMesh.mParticles.positions[j] = newState.mComponents[POS];
                mTetraMesh.mParticles.velocities[j] = newState.mComponents[VEL];

            }

    #endif

    #ifdef USE_IMPLICIT

            const int n = size;
            const int dimen = dim * n;

            // 1. Calculate A Matrix Here
            Eigen::MatrixXf AMatrix(dimen, dimen);
            AMatrix.setZero();

            for(int d = 0; d < n; ++d){
                for(int e = 0; e < dim; ++e){
                    AMatrix(dim * d + e, dim * d + e) = mTetraMesh.mParticles.masses[d] * (1 / (cTimeStep * cTimeStep));
                }
            }

            // 2. Calculate K Matrix here
            Eigen::MatrixXf KMatrix(dimen, dimen);
            KMatrix.setZero();
            computeK(KMatrix, F, JFinvT, R, S);

            // 3. Do A = A - K
            AMatrix -= KMatrix;

            // 4. Calculate B Matrix
            Eigen::MatrixXf B1Mat(dimen, 1);
            B1Mat.setZero();

            for(int d = 0; d < n; ++d) {
                // Doing Calculations as:
                // B = Vn * mass/(dt) + f + mg

                for(int e = 0; e < dim; ++e) {
                    B1Mat(dim * d + e, 0) = mTetraMesh.mParticles.masses[d] * mTetraMesh.mParticles.velocities[d](e) * (1 / cTimeStep) + mTetraMesh.mParticles.forces[d](e);
                    if(e == 1){
                        B1Mat(dim * d + e, 0) += mTetraMesh.mParticles.masses[d] * -1 * gravity;
                    }
                }
            }

            // 5. Solve Ax = B
            Eigen::MatrixXf dxMat(dimen, 1);
            dxMat.setZero();

    	    Eigen::MINRES<Eigen::MatrixXf, Eigen::Lower|Eigen::Upper, Eigen::IdentityPreconditioner> minres;
        	minres.compute(AMatrix);
        	dxMat = minres.solve(B1Mat);

            // 6. Update velocities and position with dx
            Eigen::Matrix<T, dim, 1> newPos;
            newPos.setZero();
            Eigen::Matrix<T, dim, 1> newVel;
            newPos.setZero();
            Eigen::Matrix<T, dim, 1> deltaX;
            deltaX.setZero();

            for(int d = 0; d < size; ++d) {
                for(int e = 0; e < dim; ++e) {
                    deltaX(e, 0) = dxMat(d * dim + e, 0);
                }
                // v(n + 1) = dx/dt;
                newVel = deltaX / cTimeStep;

                // x(n + 1) = x(n) + dx;
                newPos = mTetraMesh.mParticles.positions[d] + deltaX;

                // collision tests here
                if(scene.checkCollisions(newPos, temp_pos)){
                    newPos = mTetraMesh.mParticles.positions[d];
                    newVel.setZero();
                }

                mTetraMesh.mParticles.positions[d] = newPos;
                mTetraMesh.mParticles.velocities[d] = newVel;
            }
    #endif
            // <<<<< Integration END
        }
        mTetraMesh.outputFrame(z);
        scene.outputFrame(z);
    }
    // <<<<< Time Loop END
}

template<class T, int dim>
void FEMSolver<T,dim>::calculateMaterialConstants(){
    mu = TetraMesh<T,dim>::k / (2.f * (1.f + TetraMesh<T,dim>::nu));
    lambda = (TetraMesh<T,dim>::k * TetraMesh<T,dim>::nu) / ((1.f + TetraMesh<T,dim>::nu)*(1.f - 2.f*TetraMesh<T,dim>::nu));
}

template<class T, int dim>
void FEMSolver<T,dim>::computeDm(){
    for(Tetrahedron<T,dim> &t : mTetraMesh.mTetras){
        Eigen::Matrix<T,dim,dim> Dm = Eigen::Matrix<T,dim,dim>::Zero(dim,dim);
        for(int i = 0; i < dim; ++i){
            for(int j = 0; j < dim; ++j){
                Dm(j,i) = mTetraMesh.mParticles.positions[t.mPIndices[i]][j] - mTetraMesh.mParticles.positions[t.mPIndices[3]][j];
            }
        }
        t.mDm = Dm;
    }
}

template<class T, int dim>
void FEMSolver<T,dim>::precomputeTetraConstants(){
    for(Tetrahedron<T,dim> &t : mTetraMesh.mTetras){
        t.precompute();
    }
}

template<class T, int dim>
void FEMSolver<T,dim>::computeDs(Eigen::Matrix<T,dim,dim>& Ds, const Tetrahedron<T,dim>& t){
    for(int i = 0; i < dim; ++i){
        for(int j = 0; j < dim; ++j){
            Ds(j,i) = mTetraMesh.mParticles.positions[t.mPIndices[i]][j] - mTetraMesh.mParticles.positions[t.mPIndices[3]][j];
        }
    }
}

template<class T, int dim>
void FEMSolver<T,dim>::computeF(Eigen::Matrix<T,dim,dim>& F,
                    const Eigen::Matrix<T,dim,dim>& Ds,
                    const Tetrahedron<T,dim>& t){
    F = Ds * t.mDmInv;
    epsilonCheckSquareMatrix(F);
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
    if(U.determinant() < 0.f){
        U.col(dim - 1) = -1 * U.col(dim - 1);
        sigma(dim - 1, dim - 1) = -1 * sigma(dim - 1, dim - 1);
    }
    if(V.determinant() < 0.f){
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
            JFinvT(1,0) = -F(0,1);
            JFinvT(0,1) = -F(1,0);
            JFinvT(1,1) = F(0,0);
            break;
        case 3:
            JFinvT(0,0) = F(1,1)*F(2,2) - F(1,2)*F(2,1);
            JFinvT(1,0) = F(0,2)*F(2,1) - F(0,1)*F(2,2);
            JFinvT(2,0) = F(0,1)*F(1,2) - F(0,2)*F(1,1);
            JFinvT(0,1) = F(1,2)*F(2,0) - F(1,0)*F(2,2);
            JFinvT(1,1) = F(0,0)*F(2,2) - F(0,2)*F(2,0);
            JFinvT(2,1) = F(0,2)*F(1,0) - F(0,0)*F(1,2);
            JFinvT(0,2) = F(1,0)*F(2,1) - F(1,1)*F(2,0);
            JFinvT(1,2) = F(0,1)*F(2,0) - F(0,0)*F(2,1);
            JFinvT(2,2) = F(0,0)*F(1,1) - F(0,1)*F(1,0);
            break;
        default: std::cout << "error: dimension must be 2 or 3" << std::endl;
    }
    // transpose is hard-coded!!
}

template<class T, int dim>
void FEMSolver<T,dim>::distributeMass(){
    for(Tetrahedron<T,dim> &t : mTetraMesh.mTetras){
        for(int i = 0; i < dim + 1; ++i){
            // distribute 1/4 of mass to each tetrahedron point
            mTetraMesh.mParticles.masses[t.mPIndices[i]] += 0.25f * t.mass;
            mTetraMesh.mParticles.tets[t.mPIndices[i]] += 1;
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
    for(Tetrahedron<T,dim> t : mTetraMesh.mTetras){
        Eigen::MatrixXf K(4*dim, 4*dim);
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
        for(int i = 0; i < dim + 1; ++i){
            for(int j = 0; j < dim + 1; ++j){
                for(int m = 0; m < dim; ++m){
                    for(int n = 0; n < dim; ++n){
                        KMatrix(3 * t.mPIndices[i] + m, 3 * t.mPIndices[j] + n) += K(3*i + m, 3*j + n);
                    }
                }
            }
        }
    }
}

template<class T, int dim>
double FEMSolver<T,dim>::DsqPsiDsqF(int j, int k, int m, int n,
                    const Eigen::Matrix<T,dim,dim>& F,
                    const Eigen::Matrix<T,dim,dim>& JFinvT,
                    const Eigen::Matrix<T,dim,dim>& R,
                    const Eigen::Matrix<T,dim,dim>& S)
{
    return 2 * mu * (DFDF(j, k, m, n) - DRDF(j, k, m, n, R, S)) + lambda * (JFinvT(m, n) * JFinvT(j, k) + (F.determinant() - 1) * DHDF(j, k, m, n, F));
}

template<class T, int dim>
double FEMSolver<T,dim>::DFDx(int m, int n, int q, int r, const Tetrahedron<T,dim>& t){
    double val = 0.0;
    if(q == 3){
        for(int l = 0; l < dim; ++l){
            if(r == m){
                val += -1 * t.mDmInv(l, n);
            }
        }
    }
    else if(q == 0 || q == 1 || q == 2){
        for(int l = 0; l < dim; ++l){
            if(r == m && q == l){
                val += t.mDmInv(l, n);
            }
        }
    }
    return val;
}

template<class T, int dim>
double FEMSolver<T,dim>::DFDF(int j, int k, int m, int n)
{
    if(j == m && k == n){
        return 1.0;
    }
    else{
        return 0.0;
    }
}

template<class T, int dim>
double FEMSolver<T,dim>::DRDF(int j, int k, int m, int n,
            const Eigen::Matrix<T,dim,dim>& R,
            const Eigen::Matrix<T,dim,dim>& S)
{
    Eigen::Matrix<T,dim,dim> Ainv = Eigen::Matrix<T,dim,dim>::Zero(dim,dim);
    computeAinv(Ainv, S);
    double val = 0.0;
    for(int a = 0; a < dim; ++a){
        for(int b = 0; b < dim; ++b){
            for(int c = 0; c < dim; ++c){
                for(int d = 0; d < dim; ++d){
                    val += R(j,a) * leviCevita(a,b,k) * Ainv(b,c) * leviCevita(d,c,n) * R(m, d);
                }
            }
        }
    }
    return val;
}

template<class T, int dim>
void FEMSolver<T,dim>::computeAinv(Eigen::Matrix<T,dim,dim>& A,
            const Eigen::Matrix<T,dim,dim>& S)
{
    double val = 0.0;
    for(int i = 0; i < dim; ++i){
        for(int j = 0; j < dim; ++j){
            val = 0.0;
            for(int a = 0; a < dim; ++a){
                for(int b = 0; b < dim; ++b){
                    for(int c = 0; c < dim; ++c){
                        val += leviCevita(a,i,b) * S(c,b) * leviCevita(a,j,c);
                    }
                }
            }
            A(i,j) = val;
        }
    }
    A = A.inverse().eval();
}

template<class T, int dim>
double FEMSolver<T,dim>::DHDF(int j, int k, int m, int n,
            const Eigen::Matrix<T,dim,dim>& F)
{
    Eigen::Matrix<T,dim,dim> dHdF = Eigen::Matrix<T,dim,dim>::Zero(dim, dim);
    if(m == 0 & n == 0){
        dHdF << 0, 0, 0,
                0, F(2,2), -F(2,1),
                0, -F(1,2), F(1,1);
    }
    else if(m == 0 && n == 1){
        dHdF << 0, 0, 0,
                -F(2,2), 0, F(2,0),
                F(1,2), 0, -F(1,0);
    }
    else if(m == 0 && n == 2){
        dHdF << 0, 0, 0,
                F(2,1), -F(2,0), 0,
                -F(1,1), F(1,0), 0;
    }
    else if(m == 1 && n == 0){
        dHdF << 0, -F(2,2), F(2,1),
                0, 0, 0,
                0, F(0,2), -F(0,1);
    }
    else if(m == 1 && n == 1){
        dHdF << F(2,2), 0, -F(2,0),
                0, 0, 0,
                -F(0,2), 0, F(0,0);
    }
    else if(m == 1 && n == 2){
        dHdF << -F(2,1), F(2,0), 0,
                0, 0, 0,
                F(0,1), -F(0,0), 0;
    }
    else if(m == 2 && n == 0){
        dHdF << 0, 0, 0,
                0, F(1,2), F(2,0),
                0, -F(0,2), F(0,1);
    }
    else if(m == 2 && n == 1){
        dHdF << -F(1,2), 0, F(1,0),
                F(0,2), 0, -F(0,0),
                0, 0, 0;
    }
    else if(m == 2 && n == 2){
        dHdF << F(1,1), -F(1,0), 0,
                -F(0,1), F(0,0), 0,
                0, 0, 0;
    }
    else{
        std::cout << "error in dHdF" << std::endl;
    }
    return dHdF(j, k);
}
template<class T, int dim>
double FEMSolver<T,dim>::leviCevita(int i, int j, int k){
    // even cases
    if(i == 0 && j == 1 && k == 2){
        return 1.0;
    }
    else if(i == 2 && j == 0 && k == 1){
        return 1.0;
    }
    else if(i == 1 && j == 2 && k == 0){
        return 1.0;
    }
    // odd cases
    else if(i == 0 && j == 2 && k == 1){
        return -1.0;
    }
    else if(i == 2 && j == 1 && k == 0){
        return -1.0;
    }
    else if(i == 1 && j == 0 && k == 2){
        return -1.0;
    }
    else{
        return 0.0;
    }
}

