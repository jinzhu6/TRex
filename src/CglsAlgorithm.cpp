/*
-----------------------------------------------------------------------
Copyright: 2010-2015, iMinds-Vision Lab, University of Antwerp
           2014-2015, CWI, Amsterdam

Contact: astra@uantwerpen.be
Website: http://sf.net/projects/astra-toolbox

This file is part of the ASTRA Toolbox.


The ASTRA Toolbox is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

The ASTRA Toolbox is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the ASTRA Toolbox. If not, see <http://www.gnu.org/licenses/>.

-----------------------------------------------------------------------
$Id$
*/

#include "astra/CglsAlgorithm.h"

#include <boost/lexical_cast.hpp>

#include "astra/AstraObjectManager.h"

using namespace std;

namespace astra {

#include "astra/Projector2DImpl.inl"

// type of the algorithm, needed to register with CAlgorithmFactory
std::string CCglsAlgorithm::type = "CGLS";

//----------------------------------------------------------------------------------------
// Constructor
CCglsAlgorithm::CCglsAlgorithm() 
{
	_clear();
}

//---------------------------------------------------------------------------------------
// Initialize - C++
CCglsAlgorithm::CCglsAlgorithm(CProjector2D* _pProjector, 
							   CFloat32ProjectionData2D* _pSinogram, 
							   CFloat32VolumeData2D* _pReconstruction)
{
	_clear();
	initialize(_pProjector, _pSinogram, _pReconstruction);
}

//----------------------------------------------------------------------------------------
// Destructor
CCglsAlgorithm::~CCglsAlgorithm() 
{
	clear();
}

//---------------------------------------------------------------------------------------
// Clear - Constructors
void CCglsAlgorithm::_clear()
{
	CReconstructionAlgorithm2D::_clear();
	r = NULL;
	w = NULL;
	z = NULL;
	p = NULL;
	t = NULL;
	c = NULL;
	m_bUseJacobiPreconditioner;
	alpha = 0.0f;
	beta = 0.0f;
	gamma = 0.0f;
	m_iIteration = 0;
	m_bIsInitialized = false;
}

//---------------------------------------------------------------------------------------
// Clear - Public
void CCglsAlgorithm::clear()
{
	CReconstructionAlgorithm2D::_clear();
	ASTRA_DELETE(r);
	ASTRA_DELETE(w);
	ASTRA_DELETE(z);
	ASTRA_DELETE(p);
	ASTRA_DELETE(t);
	ASTRA_DELETE(c);
	alpha = 0.0f;
	beta = 0.0f;
	gamma = 0.0f;
	m_iIteration = 0;
	m_bIsInitialized = false;
}

//----------------------------------------------------------------------------------------
// Check
bool CCglsAlgorithm::_check()
{
	// check base class
	ASTRA_CONFIG_CHECK(CReconstructionAlgorithm2D::_check(), "CGLS", "Error in ReconstructionAlgorithm2D initialization");

	return true;
}

//---------------------------------------------------------------------------------------
// Initialize - Config
bool CCglsAlgorithm::initialize(const Config& _cfg)
{
	ASTRA_ASSERT(_cfg.self);
	ConfigStackCheck<CAlgorithm> CC("CglsAlgorithm", this, _cfg);

	// if already initialized, clear first
	if (m_bIsInitialized) {
		clear();
	}

	// initialization of parent class
	if (!CReconstructionAlgorithm2D::initialize(_cfg)) {
		return false;
	}

	// member variables
	r = new CFloat32ProjectionData2D(m_pSinogram->getGeometry());
	w = new CFloat32ProjectionData2D(m_pSinogram->getGeometry());
	z = new CFloat32VolumeData2D(m_pReconstruction->getGeometry());
	p = new CFloat32VolumeData2D(m_pReconstruction->getGeometry());	

	// Flag to use the Jacobi Preconditioner
	m_bUseJacobiPreconditioner = _cfg.self.getOptionBool(
		"UseJacobiPreconditioner", m_bUseJacobiPreconditioner);
	CC.markOptionParsed("UseJacobiPreconditioner");
	// Allocate if will be used.
	if (m_bUseJacobiPreconditioner) {
		c = new CFloat32VolumeData2D(m_pReconstruction->getGeometry());
		t = new CFloat32VolumeData2D(m_pReconstruction->getGeometry());
	}


	alpha = 0.0f;
	beta = 0.0f;
	gamma = 0.0f;

	// success
	m_bIsInitialized = _check();
	return m_bIsInitialized;
}

//---------------------------------------------------------------------------------------
// Initialize - C++
bool CCglsAlgorithm::initialize(CProjector2D* _pProjector, 
								CFloat32ProjectionData2D* _pSinogram, 
								CFloat32VolumeData2D* _pReconstruction)
{
	// if already initialized, clear first
	if (m_bIsInitialized) {
		clear();
	}

	// required classes
	m_pProjector = _pProjector;
	m_pSinogram = _pSinogram;
	m_pReconstruction = _pReconstruction;

	// member variables
	r = new CFloat32ProjectionData2D(m_pSinogram->getGeometry());
	w = new CFloat32ProjectionData2D(m_pSinogram->getGeometry());
	z = new CFloat32VolumeData2D(m_pReconstruction->getGeometry());
	p = new CFloat32VolumeData2D(m_pReconstruction->getGeometry());

	// success
	m_bIsInitialized = _check();
	return m_bIsInitialized;
}

//---------------------------------------------------------------------------------------
// Information - All
map<string,boost::any> CCglsAlgorithm::getInformation() 
{
	map<string, boost::any> res;
	return mergeMap<string,boost::any>(CReconstructionAlgorithm2D::getInformation(), res);
};

//---------------------------------------------------------------------------------------
// Information - Specific
boost::any CCglsAlgorithm::getInformation(std::string _sIdentifier) 
{
	return CAlgorithm::getInformation(_sIdentifier);
};

//----------------------------------------------------------------------------------------
// Iterate
void CCglsAlgorithm::run(int _iNrIterations)
{
	// check initialized
	ASTRA_ASSERT(m_bIsInitialized);

	// data projectors
	CDataProjectorInterface* pForwardProjector;
	CDataProjectorInterface* pFirstForwardProjector;
	CDataProjectorInterface* pBackProjector;

	// Clear reconstruction volume.
	if (m_bClearReconstruction) {
		m_pReconstruction->setData(0.0f);
	}

	// forward projection data projector
	if (m_bUseJacobiPreconditioner) {
		// w = A t
		pForwardProjector = dispatchDataProjector(
			m_pProjector, 
				SinogramMaskPolicy(m_pSinogramMask),					// sinogram mask
				ReconstructionMaskPolicy(m_pReconstructionMask),		// reconstruction mask
				DefaultFPPolicy(t, w),									// forward projection
				m_bUseSinogramMask, m_bUseReconstructionMask, true		// options on/off
			); 
	} else {
		// w = A p
		pForwardProjector = dispatchDataProjector(
			m_pProjector, 
				SinogramMaskPolicy(m_pSinogramMask),					// sinogram mask
				ReconstructionMaskPolicy(m_pReconstructionMask),		// reconstruction mask
				DefaultFPPolicy(p, w),									// forward projection
				m_bUseSinogramMask, m_bUseReconstructionMask, true		// options on/off
			); 
	}

	// backprojection data projector
	pBackProjector = dispatchDataProjector(
			m_pProjector, 
			SinogramMaskPolicy(m_pSinogramMask),														// sinogram mask
			ReconstructionMaskPolicy(m_pReconstructionMask),											// reconstruction mask
			DefaultBPPolicy(z, r),																		//  backprojection
			m_bUseSinogramMask, m_bUseReconstructionMask, true // options on/off
		); 

	// First forward projector to compute the Jacobi preconditioner, which
	// is just the norm squares of the columns of the projection matrix A
	// (it is diagonal of A' * A)
	pFirstForwardProjector = dispatchDataProjector(
		m_pProjector, 
			SinogramMaskPolicy(m_pSinogramMask),					// sinogram mask
			ReconstructionMaskPolicy(m_pReconstructionMask),		// reconstruction mask
			TotalPixelWeightPolicy(c, false, true),									// forward projection
			m_bUseSinogramMask, m_bUseReconstructionMask, true		// options on/off
		); 
	// Compute the Jacobi preconditioner.
	if (m_bUseJacobiPreconditioner) {
		c->setData(0.f);
		pFirstForwardProjector->project();
		// Compute sqrt
		c->sqrt();
	}

	int i;

	if (m_iIteration == 0) {
		// r = b;
		r->copyData(m_pSinogram->getData());
		// w = A*x0
		p->copyData(m_pReconstruction->getData());
		pForwardProjector->project();
		// r = b - A*x0
		*r -= *w;

		// z = A' * (b - A * x0) = A' * r
		z->setData(0.0f);
		pBackProjector->project();
		// CHECK
		//if (m_bUseMinConstraint)
		//	z->clampMin(m_fMinValue);
		//if (m_bUseMaxConstraint)
		//	z->clampMax(m_fMaxValue);

		// Precondition?
		if (m_bUseJacobiPreconditioner) {
			*z /= *c;
		}

		// p = z;
		p->copyData(z->getData());
		
		// gamma = dot(z,z);
		gamma = 0.0f;
		for (i = 0; i < z->getSize(); ++i) {
			gamma += z->getData()[i] * z->getData()[i];
		}

		m_iIteration++;
	}

	// start iterations
	//for (int iIteration = _iNrIterations-1; iIteration >= 0; --iIteration) {
	for (int iIteration = 0; iIteration < _iNrIterations; ++iIteration) {
		// start timer
		m_ulTimer = CPlatformDepSystemCode::getMSCount();

		if (m_bUseJacobiPreconditioner) {
			// t = C^-1 p
			t->copyData(p->getData());
			*t /= *c;
		}	

		// w = A*p (or A*t if precondioning)
		w->setData(0);
		pForwardProjector->project();
	
		// alpha = gamma/dot(w,w);
		float32 tmp = 0;
		for (i = 0; i < w->getSize(); ++i) {
			tmp += w->getData()[i] * w->getData()[i];
		}
		alpha = gamma / tmp;

		if (m_bUseJacobiPreconditioner) {
			// x = x + alpha*t;
			for (i = 0; i < m_pReconstruction->getSize(); ++i) {
				m_pReconstruction->getData()[i] += alpha * t->getData()[i];
			}
		} else {
			// x = x + alpha*p;
			for (i = 0; i < m_pReconstruction->getSize(); ++i) {
				m_pReconstruction->getData()[i] += alpha * p->getData()[i];
			}
		}

		// r = r - alpha*w;
		for (i = 0; i < r->getSize(); ++i) {
			r->getData()[i] -= alpha * w->getData()[i];
		}

		// z = A'*r;
		z->setData(0.0f);
		pBackProjector->project();

		// Precondition?
		if (m_bUseJacobiPreconditioner) {
			// z = C^-1 C
			*z /= *c;
		}

		// CHECKME: should these be here?
		// This was z. CHECK
		if (m_bUseMinConstraint)
			m_pReconstruction->clampMin(m_fMinValue);
		if (m_bUseMaxConstraint)
			m_pReconstruction->clampMax(m_fMaxValue);

		// beta = 1/gamma;
		beta = 1.0f / gamma;

		// gamma = dot(z,z);
		gamma = 0;
		for (i = 0; i < z->getSize(); ++i) {
			gamma += z->getData()[i] * z->getData()[i];
		}

		// beta = gamma*beta;
		beta *= gamma; 

		// p = z + beta*p;
		for (i = 0; i < z->getSize(); ++i) {
			p->getData()[i] = z->getData()[i] + beta * p->getData()[i];
		}
		
		// end timer
		m_ulTimer = CPlatformDepSystemCode::getMSCount() - m_ulTimer;

		// Compute metrics.
		computeIterationMetrics(iIteration, _iNrIterations, r);

		m_iIteration++;
	}

	ASTRA_DELETE(pForwardProjector);
	ASTRA_DELETE(pBackProjector);
	ASTRA_DELETE(pFirstForwardProjector);

	//if (m_iIteration == 0) {
	//	// r = b;
	//	r->copyData(m_pSinogram->getData());

	//	// z = A'*b;
	//	z->setData(0.0f);
	//	pBackProjector->project();
	//	// CHECK
	//	//if (m_bUseMinConstraint)
	//	//	z->clampMin(m_fMinValue);
	//	//if (m_bUseMaxConstraint)
	//	//	z->clampMax(m_fMaxValue);

	//	// p = z;
	//	p->copyData(z->getData());

	//	// gamma = dot(z,z);
	//	gamma = 0.0f;
	//	for (i = 0; i < z->getSize(); ++i) {
	//		gamma += z->getData()[i] * z->getData()[i];
	//	}
	//	
	//	m_iIteration++;
	//}


	//// start iterations
	////for (int iIteration = _iNrIterations-1; iIteration >= 0; --iIteration) {
	//for (int iIteration = 0; iIteration < _iNrIterations; ++iIteration) {
	//	// start timer
	//	m_ulTimer = CPlatformDepSystemCode::getMSCount();
	//
	//	// w = A*p;
	//	pForwardProjector->project();
	//
	//	// alpha = gamma/dot(w,w);
	//	float32 tmp = 0;
	//	for (i = 0; i < w->getSize(); ++i) {
	//		tmp += w->getData()[i] * w->getData()[i];
	//	}
	//	alpha = gamma / tmp;

	//	// x = x + alpha*p;
	//	for (i = 0; i < m_pReconstruction->getSize(); ++i) {
	//		m_pReconstruction->getData()[i] += alpha * p->getData()[i];
	//	}

	//	// r = r - alpha*w;
	//	for (i = 0; i < r->getSize(); ++i) {
	//		r->getData()[i] -= alpha * w->getData()[i];
	//	}

	//	// z = A'*r;
	//	z->setData(0.0f);
	//	pBackProjector->project();

	//	// CHECKME: should these be here?
	//	// This was z. CHECK
	//	if (m_bUseMinConstraint)
	//		m_pReconstruction->clampMin(m_fMinValue);
	//	if (m_bUseMaxConstraint)
	//		m_pReconstruction->clampMax(m_fMaxValue);

	//	// beta = 1/gamma;
	//	beta = 1.0f / gamma;

	//	// gamma = dot(z,z);
	//	gamma = 0;
	//	for (i = 0; i < z->getSize(); ++i) {
	//		gamma += z->getData()[i] * z->getData()[i];
	//	}

	//	// beta = gamma*beta;
	//	beta *= gamma; 

	//	// p = z + beta*p;
	//	for (i = 0; i < z->getSize(); ++i) {
	//		p->getData()[i] = z->getData()[i] + beta * p->getData()[i];
	//	}
	//	
	//	// end timer
	//	m_ulTotalTime += CPlatformDepSystemCode::getMSCount() - m_ulTimer;

	//	// Compute metrics.
	//	computeIterationMetrics(iIteration, _iNrIterations);

	//	m_iIteration++;
	//}

}
//----------------------------------------------------------------------------------------

} // namespace astra
