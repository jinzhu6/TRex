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

#include "astra/BlockSimplifiedSartAlgorithm.h"

#include <boost/lexical_cast.hpp>

#include "astra/AstraObjectManager.h"
#include "astra/DataProjectorPolicies.h"

using namespace std;

namespace astra {

#include "astra/Projector2DImpl.inl"

// type of the algorithm, needed to register with CAlgorithmFactory
std::string CBlockSimplifiedSartAlgorithm::type = "BSSART";


//---------------------------------------------------------------------------------------
// Clear - Constructors
void CBlockSimplifiedSartAlgorithm::_clear()
{
    CSartAlgorithm::_clear();
}

//---------------------------------------------------------------------------------------
// Clear - Public
void CBlockSimplifiedSartAlgorithm::clear()
{
    CSartAlgorithm::clear();
}

//----------------------------------------------------------------------------------------
// Constructor
CBlockSimplifiedSartAlgorithm::CBlockSimplifiedSartAlgorithm() 
{
    _clear();
}

//----------------------------------------------------------------------------------------
// Constructor
CBlockSimplifiedSartAlgorithm::CBlockSimplifiedSartAlgorithm(CProjector2D* _pProjector, 
                               CFloat32ProjectionData2D* _pSinogram, 
                               CFloat32VolumeData2D* _pReconstruction) 
{
    CSartAlgorithm::CSartAlgorithm(_pProjector, _pSinogram, _pReconstruction);
}

//----------------------------------------------------------------------------------------
// Constructor
CBlockSimplifiedSartAlgorithm::CBlockSimplifiedSartAlgorithm(CProjector2D* _pProjector, 
                               CFloat32ProjectionData2D* _pSinogram, 
                               CFloat32VolumeData2D* _pReconstruction,
                               int* _piProjectionOrder, 
                               int _iProjectionCount)
{
    CSartAlgorithm::CSartAlgorithm(_pProjector, _pSinogram, _pReconstruction,
        _piProjectionOrder, _iProjectionCount);
}

//----------------------------------------------------------------------------------------
// Destructor
CBlockSimplifiedSartAlgorithm::~CBlockSimplifiedSartAlgorithm() 
{
    clear();
}

//---------------------------------------------------------------------------------------
// Initialize - Config
bool CBlockSimplifiedSartAlgorithm::initialize(const Config& _cfg)
{
    assert(_cfg.self);
    ConfigStackCheck<CAlgorithm> CC("BSSART Algorithm", this, _cfg);
    
    // if already initialized, clear first
    if (m_bIsInitialized) {
        clear();
    }

    // initialization of parent class
    if (!CSartAlgorithm::initialize(_cfg)) {
        return false;
    }

    // success
    m_bIsInitialized = _check();
    return m_bIsInitialized;
}


//----------------------------------------------------------------------------------------
bool CBlockSimplifiedSartAlgorithm::_check()
{
    // check base class
    ASTRA_CONFIG_CHECK(CSartAlgorithm::_check(), "BSSART", 
        "Error in ReconstructionAlgorithm2D initialization");

    return true;
}

//---------------------------------------------------------------------------------------
// Information - All
map<string,boost::any> CBlockSimplifiedSartAlgorithm::getInformation() 
{
    map<string, boost::any> res;
    res["ProjectionOrder"] = getInformation("ProjectionOrder");
    return mergeMap<string,boost::any>(CReconstructionAlgorithm2D::getInformation(), res);
};

//---------------------------------------------------------------------------------------
// Information - Specific
boost::any CBlockSimplifiedSartAlgorithm::getInformation(std::string _sIdentifier) 
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
void CBlockSimplifiedSartAlgorithm::run(int _iNrIterations)
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

    // also computes total pixel weight and total ray length
    pForwardProjector = dispatchDataProjector(
            m_pProjector, 
            SinogramMaskPolicy(m_pSinogramMask),														// sinogram mask
            ReconstructionMaskPolicy(m_pReconstructionMask),											// reconstruction mask
            DiffFPPolicy(m_pReconstruction, m_pDiffSinogram, m_pSinogram),								// forward projection with difference calculation
            m_bUseSinogramMask, m_bUseReconstructionMask, true											 // options on/off
        );

    // first time forward projection data projector,
    // computes total ray length (sum of rows) and total pixel weights (sum of columns)
    pFirstForwardProjector = dispatchDataProjector(
            m_pProjector, 
            SinogramMaskPolicy(m_pSinogramMask),														// sinogram mask
            ReconstructionMaskPolicy(m_pReconstructionMask),											// reconstruction mask
            CombinePolicy<TotalRayLengthPolicy, TotalPixelWeightPolicy>(
                TotalRayLengthPolicy(m_pTotalRayLength),													// calculate the total ray lengths
                TotalPixelWeightPolicy(m_pTotalPixelWeight, false)),
            m_bUseSinogramMask, m_bUseReconstructionMask, true 											 // options on/off
        );

    // Perform the first forward projection to compute ray lengths and pixel weights.
	// Not updated after that.
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
        computeIterationMetrics(iIteration, _iNrIterations);
    }

    ASTRA_DELETE(pForwardProjector);
    ASTRA_DELETE(pBackProjector);
    ASTRA_DELETE(pFirstForwardProjector);
}
//----------------------------------------------------------------------------------------

} // namespace astra
