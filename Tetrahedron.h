#pragma once

#include <iostream>
#include <vector>
#include <string>

#include <Eigen/Core>
#include <Eigen/Dense>
#include <math.h>

template<class T, int dim>
class Tetrahedron{
public:
	std::vector<int> mPIndices;        // tetrahedron vertices
    Eigen::Matrix<T,dim,dim> mDmInv;   // rest configuration Dm inverse
    T volume;                          // volume of tetrahedron in rest configuration

	Tetrahedron(const std::vector<int>& indices);
	~Tetrahedron();

    // precompute populates Dm inverse matrix and tetrahedron volume in rest configuration
    void precompute(const std::vector<Eigen::Matrix<T,dim,1>>& x);
	void print_info() const;           // for debugging
};

template<class T, int dim>
Tetrahedron<T,dim>::Tetrahedron(const std::vector<int>& indices) :
                                mPIndices(indices),
                                mDmInv(Eigen::Matrix<T,dim,dim>::Zero(dim, dim)),
                                volume(0.0) {}

template<class T, int dim>
Tetrahedron<T,dim>::~Tetrahedron(){}

template<class T, int dim>
void Tetrahedron<T,dim>::precompute(const std::vector<Eigen::Matrix<T,dim,1>>& x){
    Eigen::Matrix<T,dim,dim> Dm;
    for(int i = 0; i < dim; ++i){
        Dm.col(i) = x[i] - x.back();
    }
    switch(dim){
        case 2: volume = Dm.determinant() / 2;
        case 3: volume = Dm.determinant() / 6;
        default: std::cout << "error: dimension must be 2 or 3" << std::endl;
    }
    if(volume != 0){    // check if Dm is nonsingular
        mDmInv = Dm.inverse();
    }
}

template<class T, int dim>
void Tetrahedron<T,dim>::print_info() const{
    std::cout << mDmInv << std::endl;
    std::cout << volume << std::endl;
}
