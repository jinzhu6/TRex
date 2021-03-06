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

#include "astra/SartAlgorithm.h"

#include <boost/lexical_cast.hpp>

#include "astra/AstraObjectManager.h"
#include "astra/DataProjectorPolicies.h"

using namespace std;

namespace astra {

#include "astra/Projector2DImpl.inl"

// type of the algorithm, needed to register with CAlgorithmFactory
std::string CSartAlgorithm::type = "SART";


//---------------------------------------------------------------------------------------
// Clear - Constructors
void CSartAlgorithm::_clear()
{
	CReconstructionAlgorithm2D::_clear();
	m_piProjectionOrder = NULL;
	m_iProjectionCount = 0;
	m_iCurrentProjection = 0;
	m_bIsInitialized = false;
	m_iIterationCount = 0;
	m_fAlpha = 1.0f;
	m_bUseBSSART = false;
	//m_pPreconditioner = NULL;
}

//---------------------------------------------------------------------------------------
// Clear - Public
void CSartAlgorithm::clear()
{
	CReconstructionAlgorithm2D::clear();
	if (m_piProjectionOrder) {
		delete[] m_piProjectionOrder;
		m_piProjectionOrder = NULL;
	}
	m_iProjectionCount = 0;
	m_iCurrentProjection = 0;
	m_bIsInitialized = false;
	m_iIterationCount = 0;

	ASTRA_DELETE(m_pTotalRayLength);
	ASTRA_DELETE(m_pTotalPixelWeight);
	ASTRA_DELETE(m_pDiffSinogram);
}

//----------------------------------------------------------------------------------------
// Constructor
CSartAlgorithm::CSartAlgorithm() 
{
	_clear();
}

//----------------------------------------------------------------------------------------
// Constructor
CSartAlgorithm::CSartAlgorithm(CProjector2D* _pProjector, 
							   CFloat32ProjectionData2D* _pSinogram, 
							   CFloat32VolumeData2D* _pReconstruction) 
{
	_clear();
	initialize(_pProjector, _pSinogram, _pReconstruction);
}

//----------------------------------------------------------------------------------------
// Constructor
CSartAlgorithm::CSartAlgorithm(CProjector2D* _pProjector, 
							   CFloat32ProjectionData2D* _pSinogram, 
							   CFloat32VolumeData2D* _pReconstruction,
							   int* _piProjectionOrder, 
							   int _iProjectionCount)
{
	_clear();
	initialize(_pProjector, _pSinogram, _pReconstruction, _piProjectionOrder, _iProjectionCount);
}

//----------------------------------------------------------------------------------------
// Destructor
CSartAlgorithm::~CSartAlgorithm() 
{
	clear();
}

//---------------------------------------------------------------------------------------
// Initialize - Config
bool CSartAlgorithm::initialize(const Config& _cfg)
{
	assert(_cfg.self);
	ConfigStackCheck<CAlgorithm> CC("SartAlgorithm", this, _cfg);
	
	// if already initialized, clear first
	if (m_bIsInitialized) {
		clear();
	}

	// initialization of parent class
	if (!CReconstructionAlgorithm2D::initialize(_cfg)) {
		return false;
	}

	// projection order
	m_iCurrentProjection = 0;
	m_iProjectionCount = m_pProjector->getProjectionGeometry()->getProjectionAngleCount();
	string projOrder = _cfg.self.getOption("ProjectionOrder", "sequential");
	CC.markOptionParsed("ProjectionOrder");
	if (projOrder == "sequential") {
		m_piProjectionOrder = new int[m_iProjectionCount];
		for (int i = 0; i < m_iProjectionCount; i++) {
			m_piProjectionOrder[i] = i;
		}
	} else if (projOrder == "random") {
		srand(123);
		m_piProjectionOrder = new int[m_iProjectionCount];
		for (int i = 0; i < m_iProjectionCount; i++) {
			m_piProjectionOrder[i] = i;
		}
		for (int i = 0; i < m_iProjectionCount-1; i++) {
			int k = (rand() % (m_iProjectionCount - i));
			int t = m_piProjectionOrder[i];
			m_piProjectionOrder[i] = m_piProjectionOrder[i + k];
			m_piProjectionOrder[i + k] = t;
		}
	} else if (projOrder == "custom") {
		vector<float32> projOrderList = _cfg.self.getOptionNumericalArray("ProjectionOrderList");
		m_piProjectionOrder = new int[projOrderList.size()];
		for (int i = 0; i < m_iProjectionCount; i++) {
			m_piProjectionOrder[i] = static_cast<int>(projOrderList[i]);
		}
		CC.markOptionParsed("ProjectionOrderList");
	}

	// Alpha
	m_fAlpha = _cfg.self.getOptionNumerical("Alpha", m_fAlpha);
	CC.markOptionParsed("Alpha");

	// BSSART.
	m_bUseBSSART = _cfg.self.getOptionBool("UseBSSART", m_bUseBSSART);
	CC.markOptionParsed("UseBSSART");

	// create data objects
	m_pTotalRayLength = new CFloat32ProjectionData2D(m_pProjector->getProjectionGeometry());
	m_pTotalPixelWeight = new CFloat32VolumeData2D(m_pProjector->getVolumeGeometry());
	m_pDiffSinogram = new CFloat32ProjectionData2D(m_pProjector->getProjectionGeometry());

	//// Preconditioner
	//int id = _cfg.self.getOptionNumerical("PreconditionerId");
	//m_pPreconditioner = dynamic_cast<CFloat32VolumeData2D*>(CData2DManager::getSingleton().get(id));
	//CC.markOptionParsed("PreconditionerId");
	//ASTRA_CONFIG_CHECK(m_pPreconditioner == NULL || 
	//	m_pPreconditioner->getGeometry()->isEqual(m_pReconstruction->getGeometry()),
	//	"SART", "Error in Preconditioner");

	// success
	m_bIsInitialized = _check();
	return m_bIsInitialized;
}

//---------------------------------------------------------------------------------------
// Initialize - C++
bool CSartAlgorithm::initialize(CProjector2D* _pProjector, 
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

	// ray order
	m_iCurrentProjection = 0;
	m_iProjectionCount = _pProjector->getProjectionGeometry()->getProjectionAngleCount();
	m_piProjectionOrder = new int[m_iProjectionCount];
	for (int i = 0; i < m_iProjectionCount; i++) {
		m_piProjectionOrder[i] = i;
	}

	// create data objects
	m_pTotalRayLength = new CFloat32ProjectionData2D(m_pProjector->getProjectionGeometry());
	m_pTotalPixelWeight = new CFloat32VolumeData2D(m_pProjector->getVolumeGeometry());
	m_pDiffSinogram = new CFloat32ProjectionData2D(m_pProjector->getProjectionGeometry());

	// success
	m_bIsInitialized = _check();
	return m_bIsInitialized;
}

//---------------------------------------------------------------------------------------
// Initialize - C++
bool CSartAlgorithm::initialize(CProjector2D* _pProjector, 
								CFloat32ProjectionData2D* _pSinogram, 
								CFloat32VolumeData2D* _pReconstruction,
								int* _piProjectionOrder, 
								int _iProjectionCount)
{
	// required classes
	m_pProjector = _pProjector;
	m_pSinogram = _pSinogram;
	m_pReconstruction = _pReconstruction;

	// ray order
	m_iCurrentProjection = 0;
	m_iProjectionCount = _iProjectionCount;
	m_piProjectionOrder = new int[m_iProjectionCount];
	for (int i = 0; i < m_iProjectionCount; i++) {
		m_piProjectionOrder[i] = _piProjectionOrder[i];
	}

	// create data objects
	m_pTotalRayLength = new CFloat32ProjectionData2D(m_pProjector->getProjectionGeometry());
	m_pTotalPixelWeight = new CFloat32VolumeData2D(m_pProjector->getVolumeGeometry());
	m_pDiffSinogram = new CFloat32ProjectionData2D(m_pProjector->getProjectionGeometry());

	// success
	m_bIsInitialized = _check();
	return m_bIsInitialized;
}

//----------------------------------------------------------------------------------------
bool CSartAlgorithm::_check()
{
	// check base class
	ASTRA_CONFIG_CHECK(CReconstructionAlgorithm2D::_check(), "SART", "Error in ReconstructionAlgorithm2D initialization");

	// check projection order all within range
	for (int i = 0; i < m_iProjectionCount; ++i) {
		ASTRA_CONFIG_CHECK(0 <= m_piProjectionOrder[i] && m_piProjectionOrder[i] < m_pProjector->getProjectionGeometry()->getProjectionAngleCount(), "SART", "Projection Order out of range.");
	}

	return true;
}

//---------------------------------------------------------------------------------------
// Information - All
map<string,boost::any> CSartAlgorithm::getInformation() 
{
	map<string, boost::any> res;
	res["ProjectionOrder"] = getInformation("ProjectionOrder");
	return mergeMap<string,boost::any>(CReconstructionAlgorithm2D::getInformation(), res);
};

//---------------------------------------------------------------------------------------
// Information - Specific
boost::any CSartAlgorithm::getInformation(std::string _sIdentifier) 
{
	if (_sIdentifier == "ProjectionOrder") {
		vector<float32> res;
		for (int i = 0; i < m_iProjectionCount; i++) {
			res.push_back(m_piProjectionOrder[i]);
		}
		return res;
	}
	return CAlgorithm::getInformation(_sIdentifier);
};

//----------------------------------------------------------------------------------------
// Iterate
void CSartAlgorithm::run(int _iNrIterations)
{
	// check initialized
	ASTRA_ASSERT(m_bIsInitialized);

	m_bShouldAbort = false;

	// data projectors
	CDataProjectorInterface* pForwardProjector;
	CDataProjectorInterface* pBackProjector;
	CDataProjectorInterface* pFirstForwardProjector;

	// Initialize m_pReconstruction to zero.
	if (m_bClearReconstruction) {
		m_pReconstruction->setData(0.f);
	}

	// backprojection data projector
	pBackProjector = dispatchDataProjector(
			m_pProjector, 
			SinogramMaskPolicy(m_pSinogramMask),														// sinogram mask
			ReconstructionMaskPolicy(m_pReconstructionMask),											// reconstruction mask
			SIRTBPPolicy(m_pReconstruction, m_pDiffSinogram, 
			m_pTotalPixelWeight, m_pTotalRayLength, m_fAlpha),  // SIRT backprojection
			m_bUseSinogramMask, m_bUseReconstructionMask, true // options on/off
		); 

	// If BSSART
	if (m_bUseBSSART) {
		// Don't compute PixelWeight at every iteration, since it is computed
		// at the beginning.
		pForwardProjector = dispatchDataProjector(
				m_pProjector, 
				SinogramMaskPolicy(m_pSinogramMask),														// sinogram mask
				ReconstructionMaskPolicy(m_pReconstructionMask),											// reconstruction mask
				DiffFPPolicy(m_pReconstruction, m_pDiffSinogram, m_pSinogram),								// forward projection with difference calculation
				m_bUseSinogramMask, m_bUseReconstructionMask, true											 // options on/off
			);

		// first time forward projection data projector,
		// If BSSART, compute both RayLength and PixelWeight
		pFirstForwardProjector = dispatchDataProjector(
				m_pProjector, 
				SinogramMaskPolicy(m_pSinogramMask),														// sinogram mask
				ReconstructionMaskPolicy(m_pReconstructionMask),											// reconstruction mask
				CombinePolicy<TotalRayLengthPolicy, TotalPixelWeightPolicy>(
					TotalRayLengthPolicy(m_pTotalRayLength),													// calculate the total ray lengths
					TotalPixelWeightPolicy(m_pTotalPixelWeight)),
				m_bUseSinogramMask, m_bUseReconstructionMask, true											 // options on/off
			);
	} else {
		// also computes total pixel weight.
		pForwardProjector = dispatchDataProjector(
				m_pProjector, 
				SinogramMaskPolicy(m_pSinogramMask),														// sinogram mask
				ReconstructionMaskPolicy(m_pReconstructionMask),											// reconstruction mask
				CombinePolicy<DiffFPPolicy, TotalPixelWeightPolicy>(					// 2 basic operations
					DiffFPPolicy(m_pReconstruction, m_pDiffSinogram, m_pSinogram),								// forward projection with difference calculation
					TotalPixelWeightPolicy(m_pTotalPixelWeight)),												// calculate the total pixel weights
				m_bUseSinogramMask, m_bUseReconstructionMask, true											 // options on/off
			);

		// computes total ray length at the start.
		pFirstForwardProjector = dispatchDataProjector(
				m_pProjector, 
				SinogramMaskPolicy(m_pSinogramMask),														// sinogram mask
				ReconstructionMaskPolicy(m_pReconstructionMask),											// reconstruction mask
				TotalRayLengthPolicy(m_pTotalRayLength),													// calculate the total ray lengths
				m_bUseSinogramMask, m_bUseReconstructionMask, true											 // options on/off
			);
	}

	// Perform the first forward projection to compute ray lengths
	m_pTotalRayLength->setData(0.0f);
	m_pTotalPixelWeight->setData(0.0f);
	pFirstForwardProjector->project();

	//// end of init.
	//ttime = CPlatformDepSystemCode::getMSCount() - timer;

	// iteration loop, each iteration loops over all available projections
	for (int iIteration = 0; iIteration < _iNrIterations && !m_bShouldAbort; ++iIteration) {
		// start timer
		m_ulTimer = CPlatformDepSystemCode::getMSCount();

		//ASTRA_INFO("Iteration %d", iIteration);
		// Clear RayLength before another loop over projections. This is needed so that
		// RayLength is correct, because updating RayLength with the forward projection
		// again will multiply the RayLength when processing the same ray in the next
		// iteration.
		//if (m_bClearRayLength) {
		//	m_pTotalRayLength->setData(0.f);
		//}

		// loop over projections
		for (int iP = 0; iP < m_iProjectionCount; ++iP) {
			// projection id
			// int iProjection = m_piProjectionOrder[m_iIterationCount % m_iProjectionCount];
			int iProjection = m_piProjectionOrder[iP % m_iProjectionCount];
			//ASTRA_INFO(" Projection %d", iProjection);

			// forward projection and difference calculation
			if (!m_bUseBSSART) {
				// Clear PixelWeight if computing it at every iteration.
				m_pTotalPixelWeight->setData(0.0f);
			}
			pForwardProjector->projectSingleProjection(iProjection);
			// backprojection
			pBackProjector->projectSingleProjection(iProjection);
			// update iteration count
			m_iIterationCount++;

			// We need to check here, as checking inside the BP (as in ART)
			// is not correct.
			if (m_bUseMinConstraint)
				m_pReconstruction->clampMin(m_fMinValue);
			if (m_bUseMaxConstraint)
				m_pReconstruction->clampMax(m_fMaxValue);
		}

		// end timer
		m_ulTimer = CPlatformDepSystemCode::getMSCount() - m_ulTimer;

		// Compute metrics.
		computeIterationMetrics(iIteration, _iNrIterations, m_pDiffSinogram);
	}

	ASTRA_DELETE(pForwardProjector);
	ASTRA_DELETE(pBackProjector);
	ASTRA_DELETE(pFirstForwardProjector);


	//// check initialized
 // 	ASTRA_ASSERT(m_bIsInitialized);

	//// variables
	//int iIteration, iDetector;
	//int baseIndex, iPixel;
	//float32* pfGamma = new float32[m_pReconstruction->getSize()];
	//float32* pfBeta = new float32[m_pProjector->getProjectionGeometry()->getDetectorCount()];
	//float32* pfProjectionDiff = new float32[m_pProjector->getProjectionGeometry()->getDetectorCount()];

	//// ITERATE
	//for (iIteration = _iNrIterations-1; iIteration >= 0; --iIteration) {
	//
	//	// reset gamma	
	//	memset(pfGamma, 0, sizeof(float32) * m_pReconstruction->getSize());
	//	memset(pfBeta, 0, sizeof(float32) * m_pProjector->getProjectionGeometry()->getDetectorCount());
	//
	//	// get current projection angle
	//	int iProjection = m_piProjectionOrder[m_iCurrentProjection];
	//	m_iCurrentProjection = (m_iCurrentProjection + 1) % m_iProjectionCount;
	//	int iProjectionWeightCount = m_pProjector->getProjectionWeightsCount(iProjection);
	//
	//	// allocate memory for the pixel buffer
	//	SPixelWeight* pPixels = new SPixelWeight[m_pProjector->getProjectionWeightsCount(iProjection) * m_pProjector->getProjectionGeometry()->getDetectorCount()];
	//	int* piRayStoredPixelCount = new int[m_pProjector->getProjectionGeometry()->getDetectorCount()];

	//	// compute weights for this projection
	//	m_pProjector->computeProjectionRayWeights(iProjection, pPixels, piRayStoredPixelCount);
	//
	//	// calculate projection difference in each detector
	//	for (iDetector = m_pProjector->getProjectionGeometry()->getDetectorCount()-1; iDetector >= 0; --iDetector) {

	//		if (m_bUseSinogramMask && m_pSinogramMask->getData2D()[iProjection][iDetector] == 0) continue;	

	//		// index base of the pixel in question
	//		baseIndex = iDetector * iProjectionWeightCount;
	//
	//		// set the initial projection difference to the sinogram value
	//		pfProjectionDiff[iDetector] = m_pSinogram->getData2DConst()[iProjection][iDetector];
	//
	//		// update projection difference, beta and gamma
	//		for (iPixel = piRayStoredPixelCount[iDetector]-1; iPixel >= 0; --iPixel) {

	//			// pixel must be loose
	//			if (m_bUseReconstructionMask && m_pReconstructionMask->getData()[pPixels[baseIndex+iPixel].m_iIndex] == 0) continue;

	//			// subtract projection value from projection difference 
	//			pfProjectionDiff[iDetector] -= 
	//				pPixels[baseIndex+iPixel].m_fWeight * m_pReconstruction->getDataConst()[pPixels[baseIndex+iPixel].m_iIndex];
	//				
	//			// update beta and gamma if this pixel lies inside a loose part
	//			pfBeta[iDetector] += pPixels[baseIndex+iPixel].m_fWeight;
	//			pfGamma[pPixels[baseIndex+iPixel].m_iIndex] += pPixels[baseIndex+iPixel].m_fWeight;
	//		}
	//
	//	}
	//
	//	// back projection
	//	for (iDetector = m_pProjector->getProjectionGeometry()->getDetectorCount()-1; iDetector >= 0; --iDetector) {
	//		
	//		if (m_bUseSinogramMask && m_pSinogramMask->getData2D()[iProjection][iDetector] == 0) continue;	

	//		// index base of the pixel in question
	//		baseIndex = iDetector * iProjectionWeightCount;

	//		// update pixel values
	//		for (iPixel = piRayStoredPixelCount[iDetector]-1; iPixel >= 0; --iPixel) {

	//
	//			// pixel must be loose
	//			if (m_bUseReconstructionMask && m_pReconstructionMask->getData()[pPixels[baseIndex+iPixel].m_iIndex] == 0) continue;

	//			

	//			// update reconstruction volume
	//			float32 fGammaBeta = pfGamma[pPixels[baseIndex+iPixel].m_iIndex] * pfBeta[iDetector];
	//			if ((fGammaBeta > 0.01f) || (fGammaBeta < -0.01f)) {	
	//				m_pReconstruction->getData()[pPixels[baseIndex+iPixel].m_iIndex] += 
	//					pPixels[baseIndex+iPixel].m_fWeight * pfProjectionDiff[iDetector] / fGammaBeta;
	//			}

	//			// constraints
	//			if (m_bUseMinConstraint && m_pReconstruction->getData()[pPixels[baseIndex+iPixel].m_iIndex] < m_fMinValue) {
	//				m_pReconstruction->getData()[pPixels[baseIndex+iPixel].m_iIndex] = m_fMinValue;
	//			}
	//			if (m_bUseMaxConstraint && m_pReconstruction->getData()[pPixels[baseIndex+iPixel].m_iIndex] > m_fMaxValue) {
	//				m_pReconstruction->getData()[pPixels[baseIndex+iPixel].m_iIndex] = m_fMaxValue;
	//			}
	//		}
	//	}
	//
	//	// garbage disposal
	//	delete[] pPixels;
	//	delete[] piRayStoredPixelCount;
	//}

	//// garbage disposal
	//delete[] pfGamma;
	//delete[] pfBeta;
	//delete[] pfProjectionDiff;

	//// update statistics
	//m_pReconstruction->updateStatistics();
}
//----------------------------------------------------------------------------------------

} // namespace astra
