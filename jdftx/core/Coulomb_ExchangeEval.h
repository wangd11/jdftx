/*-------------------------------------------------------------------
Copyright 2012 Ravishankar Sundararaman

This file is part of JDFTx.

JDFTx is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

JDFTx is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with JDFTx.  If not, see <http://www.gnu.org/licenses/>.
-------------------------------------------------------------------*/

#ifndef JDFTX_CORE_COULOMB_EXCHANGEEVAL_H
#define JDFTX_CORE_COULOMB_EXCHANGEEVAL_H

#include <core/Coulomb.h>
#include <core/Coulomb_internal.h>

//! @addtogroup LongRange
//! @{

//! Helper class for evaluating regularized Coulomb kernel for exchange
struct ExchangeEval
{
	ExchangeEval(const GridInfo& gInfo, const CoulombParams& params, const Coulomb& coulomb, double omega);
	~ExchangeEval();
	complexScalarFieldTilde operator()(complexScalarFieldTilde&& in, vector3<> kDiff) const;

	//! Return the lattice gradient of exchange integral dot(X, O(coulomb(X)) for given k-point difference
	matrix3<> latticeGradient(const complexScalarFieldTilde& X, vector3<> kDiff) const;

private:
	const GridInfo& gInfo;
	double omega;
	
	//Shorthand for combinations of regularization method and geometry
	enum KernelMode
	{	PeriodicKernel, //regularization = None/AuxiliaryFunction/ProbeChargeEwald, with geometry = Periodic
		SphericalKernel, //regularization = SphericalTruncated, or regularization = None and geometry = Spherical
		SlabKernel, //regularization = None/AuxiliaryFunction/ProbeChargeEwald with geometry = Slab
		WignerSeitzGammaKernel, //regularization = None and geometry = Isolated
		NumericalKernel //regularization = WignerSeitzTruncated, or regularization = None/AuxiliaryFunction/ProbeChargeEwald with geometry = Wire/Cylinder
	} kernelMode;
	
	//For all analytic modes (None, AuxiliaryFunction, SphericalTruncated):
	double Vzero; //G=0 term (0, the auxiliary correction, and the G=0 limit respectively)
	matrix3<> Vzero_RRT; //lattice derivative of Vzero
	//For spherical mode
	double Rc;
	ExchangeSphericalScreened_calc sphericalScreenedCalc; //for screened exchange
	ManagedArray<double> sphericalScreenedCoeff;
	//For slab mode:
	ExchangeSlab_calc slabCalc;
	ManagedArray<double> slabCoeff;
	//For Wigner-Seitz Gamma-only truncated mode:
	RealKernel* VcGamma; //Gamma-point-only kernel (used for Isolated geometry (with no need for regularization))
	ManagedArray<symmetricMatrix3<>>* VcGamma_RRT; //corresponding lattice derivative
	//For precomputed numerical kernel mode:
	std::vector< vector3<> > dkArr; //list of allowed k-point differences (modulo integer offsets)
	ManagedArray<double> kernelData; //data for all the kernels
	ManagedArray<symmetricMatrix3<>> kernelData_RRT; //lattice derivative data for all the kernels
};

//! @}
#endif // JDFTX_CORE_COULOMB_EXCHANGEEVAL_H
