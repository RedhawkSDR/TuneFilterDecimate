/*
 * This file is protected by Copyright. Please refer to the COPYRIGHT file distributed with this 
 * source distribution.
 * 
 * This file is part of REDHAWK Basic Components.
 * 
 * REDHAWK Basic Components is free software: you can redistribute it and/or modify it under the terms of 
 * the GNU Lesser General Public License as published by the Free Software Foundation, either 
 * version 3 of the License, or (at your option) any later version.
 * 
 * REDHAWK Basic Components is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
 * PURPOSE.  See the GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License along with this 
 * program.  If not, see http://www.gnu.org/licenses/.
 */

/**************************************************************************

    This is the component code. This file contains the child class where
    custom functionality can be added to the component. Custom
    functionality to the base class can be extended here. Access to
    the ports can also be done from this class

 	Source: TuneFilterDecimate.spd.xml
 	Generated on: Mon Jul 16 18:37:11 EDT 2012
 	Redhawk IDE
 	Version:T.1.8.X
 	Build id: v201207131522-r8855

**************************************************************************/

#include "TuneFilterDecimate.h"

PREPARE_LOGGING(TuneFilterDecimate_i)

TuneFilterDecimate_i::TuneFilterDecimate_i(const char *uuid, const char *label) : 
    TuneFilterDecimate_base(uuid, label)
{
    LOG_TRACE(TuneFilterDecimate_i, "TuneFilterDecimate() constructor entry");
    // Initialize processing buffers
    tunerInput.resize(BUFFER_LENGTH, Complex(0.0,0.0));
    filterInput.resize(BUFFER_LENGTH, Complex(0.0,0.0));
    decimateInput.resize(BUFFER_LENGTH, Complex(0.0,0.0));
    decimateOutput.reserve(BUFFER_LENGTH);

    // Twice BUFFER_LENGTH because BUFFER_LENGTH is of complex samples (2 floats/sample)
    // and the pushPacket buffer stores 1 float per index
    floatBuffer.reserve(BUFFER_LENGTH * 2);

    // Initialize processing classes
    tuner = NULL;
    filter = NULL;
    decimate = NULL;

    // Initialize private variables
    inputIndex = 0;
    chan_rf = 0;
    TuningRFChanged = false;
    RemakeFilter = false;

    // Initialize provides port maxQueueDepth
    dataFloat_In->setMaxQueueDepth(1000);

    setPropertyChangeListener(static_cast<std::string>("TuningRF"), this, &TuneFilterDecimate_i::configureTuner);
    setPropertyChangeListener(static_cast<std::string>("TuningIF"), this, &TuneFilterDecimate_i::configureTuner);
    setPropertyChangeListener(static_cast<std::string>("TuningNorm"), this, &TuneFilterDecimate_i::configureTuner);
    setPropertyChangeListener(static_cast<std::string>("FilterBW"), this, &TuneFilterDecimate_i::configureFilter);
    setPropertyChangeListener(static_cast<std::string>("DesiredOutputRate"), this, &TuneFilterDecimate_i::configureFilter);
}

TuneFilterDecimate_i::~TuneFilterDecimate_i()
{
	delete tuner;
	delete filter;
	delete decimate;
}

void TuneFilterDecimate_i::configureFilter(const std::string& propid) {
    LOG_DEBUG(TuneFilterDecimate_i, "Triggering filter remake");
    RemakeFilter = true;
}

void TuneFilterDecimate_i::configureTuner(const std::string& propid) {

    if ((propid == "TuningNorm")  && (this->TuneMode == "NORM")) {
        if (TuningNorm < -0.5) {
            LOG_WARN(TuneFilterDecimate_i, "Tuning norm less than 0.0; adjusting to minimum.")
            TuningNorm = -0.5;
        } else if (TuningNorm > 0.5) {
            LOG_WARN(TuneFilterDecimate_i, "Tuning greater than 1.0; adjusting to maximum.")
            TuningNorm = 0.5;
        }
        TuningIF = InputRate * TuningNorm;
        if (InputRF != 0) {
            TuningRF = TuningIF + InputRF;
        } else {
            TuningRF = 0;
        }
    } else if ((propid == "TuningIF") && (this->TuneMode == "IF")) {
        if (InputRate > 0) {
            TuningNorm = (TuningIF / InputRate);
        } else {
            TuningNorm = 0.0;
        }
        if (InputRF != 0) {
            TuningRF = TuningIF + InputRF;
        } else {
            TuningRF = 0;
        }
    } else if ((propid == "TuningRF") && (this->TuneMode == "RF")) {
        TuningIF = TuningRF - InputRF;
        if (InputRate > 0) {
            TuningNorm = (TuningIF / InputRate);
        } else {
            TuningNorm = 0.0;
        }
    }

    LOG_DEBUG(TuneFilterDecimate_i, "Tuner Settings"
                                    << " Norm: " << TuningNorm
                                    << " IF: " << TuningIF
                                    << " RF: " << TuningRF);

    if (tuner != NULL) {
        LOG_DEBUG(TuneFilterDecimate_i, "Retuning Tuner");
        tuner->retune(TuningNorm);
        TuningRFChanged = true;
    } else {
        LOG_DEBUG(TuneFilterDecimate_i, "Skipping tuner configuration because SRI hasn't been received");
    }
}

void TuneFilterDecimate_i::start() throw (CORBA::SystemException, CF::Resource::StartError)
{
    if (this->started()) { return; }

	// Process the SRI and create an initial filter if one is not already created

	if ((*(dataFloat_In->activeSRIs())).length() > 0 ){
	    if ((*(dataFloat_In->activeSRIs())).length() > 1 ) {
	        LOG_WARN(TuneFilterDecimate_i, "Input has more than one active SRI, using first one");
	    }
		configureTFD((*(dataFloat_In->activeSRIs()))[0]);
	}

	// Call Base Class Start which will start serviceFunction thread
	TuneFilterDecimate_base::start();

}


int TuneFilterDecimate_i::serviceFunction()
{
	BULKIO_dataFloat_In_i::dataTransfer *pkt = dataFloat_In->getPacket(0.0); // non-blocking
	if(pkt == NULL) return NOOP;

    if (streamID!=pkt->streamID)
    {
    	if (streamID=="")
    		streamID=pkt->streamID;
    	else
    	{
    		LOG_WARN(TuneFilterDecimate_i, "Dropping data from unknown stream ID.  Expected "
    		                                  << streamID << " received " << pkt->streamID);
    		delete pkt; //must delete the dataTransfer object when no longer needed
    		return NORMAL;
    	}
    }

	// Check if SRI has been changed
	if(pkt->sriChanged || RemakeFilter || TuningRFChanged || (dataFloat_Out->currentSRIs.count(pkt->streamID)==0)) {
		LOG_DEBUG(TuneFilterDecimate_i, "Reconfiguring TFD");
	    configureTFD(pkt->SRI); // Process and/or update the SRI
		dataFloat_Out->pushSRI(pkt->SRI); // Push the new SRI to the next component
		TuningRFChanged = false;
	}
	if(pkt->inputQueueFlushed)
	    LOG_WARN(TuneFilterDecimate_i, "Input queue has been flushed.  Data has been lost");

	if ((tuner == NULL) || (filter == NULL) || (decimate == NULL)) {
	    LOG_TRACE(TuneFilterDecimate_i, "TFD cannot complete work, dropping data");
		delete pkt;
		return NOOP;
	}

	// Process dataBuffer vector
	for(size_t i=0; i < pkt->dataBuffer.size(); i+=2) { // dataBuffer must be even (and should be for complex data)
		// Convert to the tunerInput complex data type
		tunerInput[inputIndex++] = Complex(pkt->dataBuffer[i], pkt->dataBuffer[i+1]);

		if(inputIndex == BUFFER_LENGTH) {
			inputIndex = 0; // Reset index

			// Run Tuner
			tuner->run();
			// Run Filter
			for(size_t j=0; j < filterInput.size(); j++)
				decimateInput[j] = filter->run(filterInput[j]);

			// Run Decimation
			if(decimate->run()) {
				// Buffer is full, so place the data into the floatBuffer
				for(size_t j=0; j< decimateOutput.size(); j++) {
					floatBuffer.push_back(decimateOutput[j].real());
					floatBuffer.push_back(decimateOutput[j].imag());
				}
				decimateOutput.clear();

				// Push the data to the next component
				dataFloat_Out->pushPacket(floatBuffer, pkt->T, pkt->EOS, pkt->streamID);
				floatBuffer.clear();
			}
		}
	}

	delete pkt; // Must delete the dataTransfer object when no longer needed

	return NORMAL;
}

void TuneFilterDecimate_i::configureTFD(BULKIO::StreamSRI &sri)
{
   LOG_TRACE(TuneFilterDecimate_i, "Configuring SRI: "
                                    << "sri.xdelta = " << sri.xdelta
                                    << "sri.streamID = " << sri.streamID)

	if (sri.mode!=1)
	{
	    LOG_WARN(TuneFilterDecimate_i, "Treating real data as if it were complex");
	}

	Real tmpInputSampleRate = 1 / sri.xdelta; // Calculate sample rate received from SRI
	DecimationFactor = floor(tmpInputSampleRate/DesiredOutputRate);
	LOG_DEBUG(TuneFilterDecimate_i, "DecimationFactor = " << DecimationFactor);
	if (DecimationFactor <1) {
	    LOG_WARN(TuneFilterDecimate_i, "Decimation less than 1, setting to minimum")
		DecimationFactor=1;
	}

	bool sampleRateChanged =(InputRate != tmpInputSampleRate);
	if (sampleRateChanged)
	{
		InputRate = tmpInputSampleRate;
		LOG_DEBUG(TuneFilterDecimate_i, "Sample rate changed: InputRate = " << InputRate);
	}

	// Calculate new output sample rate & modify the referenced SRI structure
	outputSampleRate = InputRate / DecimationFactor;
	sri.xdelta = 1 / outputSampleRate;
	LOG_DEBUG(TuneFilterDecimate_i, "Output xdelta = " << sri.xdelta
	                                << " Output Sample Rate " << outputSampleRate);

	if (outputSampleRate < FilterBW)
	    LOG_WARN(TuneFilterDecimate_i, "outputSampleRate " << outputSampleRate << " is less than FilterBW " << FilterBW);

	// Retrieve the front-end collected RF to determine the IF
    bool validCollectionRF = false;
    bool validChannelRF = false;
    long collection_rf = getKeywordByID<CORBA::Long>(sri, "COL_RF", validCollectionRF);
    long channel_rf = getKeywordByID<CORBA::Long>(sri, "CHAN_RF", validChannelRF);
    double tmpInputRF;
    if ((validCollectionRF) && (validChannelRF)) {
        LOG_WARN(TuneFilterDecimate_i, "Input SRI contains both COL_RF and CHAN_RF, using CHAN_RF");
        tmpInputRF = channel_rf;
    } else if (validCollectionRF) {
        tmpInputRF = collection_rf;
    } else if (validChannelRF) {
        tmpInputRF = channel_rf;
    } else {
        if (TuneMode == "RF") {
            LOG_WARN(TuneFilterDecimate_i, "Input SRI lacks RF keyword.  RF tuning cannot be performed.");
            return;
        }
        tmpInputRF = 0;
    }
    if (tmpInputRF != InputRF) {
        LOG_DEBUG(TuneFilterDecimate_i, "Input RF changed " << tmpInputRF);
        InputRF = tmpInputRF;

        // If the TuneMode is RF, we actually need to retune
        if (TuneMode == "RF") {
            configureTuner("TuningRF");
        }
        TuningRF = InputRF + TuningIF;
        LOG_DEBUG(TuneFilterDecimate_i, "Tuning RF: " << TuningRF);
    }

    // Add the CHAN_RF keyword to the SRI if we know the input RF
    if (InputRF != 0) {
        if(!setKeywordByID<CORBA::Long>(sri, "CHAN_RF", TuningRF))
            LOG_WARN(TuneFilterDecimate_i, "SRI Keyword CHAN_RF could not be set.");
    }

	// Reconfigure the tuner classes only if the sample rate has changed
	if ((tuner== NULL) || sampleRateChanged) {
	    LOG_DEBUG(TuneFilterDecimate_i, "Remaking tuner");

		if (tuner != NULL) {
			delete tuner;
		}

		if (TuneMode == "NORM") {
		    configureTuner("TuningNorm");
		} else if (TuneMode == "IF") {
		    configureTuner("TuningIF");
		} else if (TuneMode == "RF") {
            configureTuner("TuningRF");
        }

		tuner = new Tuner(tunerInput, filterInput, TuningNorm);
	}

	if ((filter==NULL) || sampleRateChanged || RemakeFilter) {
	    LOG_DEBUG(TuneFilterDecimate_i, "Remaking filter");

	    if((filter != NULL) || (decimate != NULL)) {
            delete filter;
            delete decimate;
	    }

	    Real normFl = FilterBW / InputRate; // Fixed normalized LPF cutoff frequency
	    LOG_DEBUG(TuneFilterDecimate_i, "FilterBW " << FilterBW
	                                    << " InputRate " << InputRate
	                                    << " FilterNorm " << normFl);

        filter = new FIRFilter(filterInput, decimateInput, FIRFilter::lowpass, 70, normFl);
        decimate = new Decimate(decimateInput, decimateOutput, DecimationFactor);
        RemakeFilter = false;
	}


	LOG_TRACE(TuneFilterDecimate_i, "Exit configureSRI()");
}
