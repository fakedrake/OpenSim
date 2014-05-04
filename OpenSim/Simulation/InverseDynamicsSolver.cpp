/* -------------------------------------------------------------------------- *
 *                    OpenSim:  InverseDynamicsSolver.cpp                     *
 * -------------------------------------------------------------------------- *
 * The OpenSim API is a toolkit for musculoskeletal modeling and simulation.  *
 * See http://opensim.stanford.edu and the NOTICE file for more information.  *
 * OpenSim is developed at Stanford University and supported by the US        *
 * National Institutes of Health (U54 GM072970, R24 HD065690) and by DARPA    *
 * through the Warrior Web program.                                           *
 *                                                                            *
 * Copyright (c) 2005-2012 Stanford University and the Authors                *
 * Author(s): Ajay Seth                                                       *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may    *
 * not use this file except in compliance with the License. You may obtain a  *
 * copy of the License at http://www.apache.org/licenses/LICENSE-2.0.         *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 * -------------------------------------------------------------------------- */

#include "InverseDynamicsSolver.h"
#include "Model/Model.h"
#include <OpenSim/Common/FunctionSet.h>

using namespace std;
using namespace SimTK;

namespace OpenSim {

//______________________________________________________________________________
/**
 * An implementation of the InverseDynamicsSolver 
 *
 * @param model to assemble
 */
InverseDynamicsSolver::InverseDynamicsSolver(const Model &model) : Solver(model)
{
	setAuthors("Ajay Seth");
}

/** Solve the inverse dynamics system of equations for generalized coordinate forces, Tau. 
    Applied loads are computed by the model from the state.	 */
Vector InverseDynamicsSolver::solve(const SimTK::State &s, const SimTK::Vector &udot)
{
	// Default is a statics inverse dynamics analysis, udot = 0;
	Vector knownUdots(getModel().getNumSpeeds(), 0.0);
	
	// If valid accelerations provided then use for inverse dynamics
	if(udot.size() == getModel().getNumSpeeds()){
		knownUdots = udot;
	}
	else if (udot.size() != 0){
		throw Exception("InverseDynamicsSolver::solve with input 'udot' of invalid size.");
	}

	// Realize to dynamics stage so that all model forces are computed
	getModel().getMultibodySystem().realize(s,SimTK::Stage::Dynamics);

	// Get applied mobility (generalized) forces generated by components of the model, like actuators
	const Vector &appliedMobilityForces = getModel().getMultibodySystem().getMobilityForces(s, Stage::Dynamics);
		
	// Get all applied body forces like those from conact
	const Vector_<SpatialVec>& appliedBodyForces = getModel().getMultibodySystem().getRigidBodyForces(s, Stage::Dynamics);
	
	// Perform general inverse dynamics
	return solve(s,	knownUdots, appliedMobilityForces, appliedBodyForces);
}


/** Solve the inverse dynamics system of equations for generalized coordinate forces, Tau. 
    Applied loads are explicity provided as generalized coordinate forces (MobilityForces)
	and/or a Vector of Spatial-body forces */
Vector InverseDynamicsSolver::solve(const SimTK::State &s, const SimTK::Vector &udot, 
	const SimTK::Vector &appliedMobilityForces, const SimTK::Vector_<SimTK::SpatialVec>& appliedBodyForces)
{
	//Results of the inverse dynamics for the generalized forces to satisfy accelerations
	Vector residualMobilityForces;

	if(s.getSystemStage() < SimTK::Stage::Dynamics)
		getModel().getMultibodySystem().realize(s,SimTK::Stage::Dynamics);
	
	// Perform inverse dynamics
	getModel().getMultibodySystem().getMatterSubsystem().calcResidualForceIgnoringConstraints(s,
			appliedMobilityForces, appliedBodyForces, udot, residualMobilityForces);

	return residualMobilityForces;
}

/** Solve the inverse dynamics system of equations for generalized coordinate forces, Tau. 
    Now the state is not given, but is constructed from known coordinates, q as functions of time.
	Coordinate functions must be twice differentiable. 
	NOTE: state dependent forces and other applied loads are NOT computed since these may depend on
	state variables (like muscle fiber lengths) that are not known */
Vector InverseDynamicsSolver::solve(SimTK::State &s, const FunctionSet &Qs, double time)
{
	int nq = getModel().getNumCoordinates();

	if(Qs.getSize() != nq){
		throw Exception("InverseDynamicsSolver::solve invalid number of q functions.");
	}

	if( nq != getModel().getNumSpeeds()){
		throw Exception("InverseDynamicsSolver::solve using FunctionSet, nq != nu not supported.");
	}

	// update the State so we get the correct gravity and coriolis effects
	// direct references into the state so no allocation required
	s.updTime() = time;
	Vector &q = s.updQ();
	Vector &u = s.updU();
	Vector &udot = s.updUDot();

	for(int i=0; i<nq; i++){
		q[i] = Qs.evaluate(i, 0, time);
		u[i] = Qs.evaluate(i, 1, time);
		udot[i] = Qs.evaluate(i, 2, time);
	}

	// Perform general inverse dynamics
	return solve(s,	udot);
}


/** Same as above but for a given time series */
void InverseDynamicsSolver::solve(SimTK::State &s, const FunctionSet &Qs, const Array_<double> &times, Array_<Vector> &genForceTrajectory)
{
	int nq = getModel().getNumCoordinates();
	int nt = times.size();

	//Preallocate if not done already
	genForceTrajectory.resize(nt, Vector(nq));
	
	AnalysisSet& analysisSet = const_cast<AnalysisSet&>(getModel().getAnalysisSet());
	//fill in results for each time
	for(int i=0; i<nt; i++){ 
		genForceTrajectory[i] = solve(s, Qs, times[i]);
		analysisSet.step(s, i);
	}
}

} // end of namespace OpenSim