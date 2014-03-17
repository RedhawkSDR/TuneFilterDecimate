/*
 * This file is protected by Copyright. Please refer to the COPYRIGHT file distributed with this
 * source distribution.
 *
 * This file is part of REDHAWK Basic Components TuneFilterDecimate.
 *
 * REDHAWK Basic Components TuneFilterDecimate is free software: you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * REDHAWK Basic Components TuneFilterDecimate is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this
 * program.  If not, see http://www.gnu.org/licenses/.
 */
/**************************************************************************

    This is the component code. This file contains the child class where
    custom functionality can be added to the component. Custom
    functionality to the base class can be extended here. Access to
    the ports can also be done from this class

 **************************************************************************/

#include "TuneFilterDecimate.h"

//set allowed bounds here for static members to make the compilers happy
const size_t TuneFilterDecimate_i::MIN_NUM_TAPS= 25;
const size_t TuneFilterDecimate_i::MAX_NUM_TAPS= 4*1024*1024;
const size_t TuneFilterDecimate_i::MIN_FFT_SIZE= 64;
const size_t TuneFilterDecimate_i::MAX_FFT_SIZE= 8*1024*1024;

//find the power of 2 greater then or equal to the input number
size_t pow2ge(size_t n)
{
	size_t out=2;
	while (n>out)
		out*=2;
	return out;
};

PREPARE_LOGGING(TuneFilterDecimate_i)

TuneFilterDecimate_i::TuneFilterDecimate_i(const char *uuid, const char *label) :
TuneFilterDecimate_base(uuid, label)
{
	LOG_TRACE(TuneFilterDecimate_i, "TuneFilterDecimate() constructor entry");

	// Initialize processing classes
	tuner = NULL;
	filter = NULL;
	decimate = NULL;

	// Initialize private variables
	chan_if = 0;
	TuningRFChanged = false;
	RemakeFilter = false;
	inputComplex = true;

	// Initialize provides port maxQueueDepth
	dataFloat_In->setMaxQueueDepth(1000);

	setPropertyChangeListener(static_cast<std::string>("TuningRF"), this, &TuneFilterDecimate_i::configureTuner);
	setPropertyChangeListener(static_cast<std::string>("TuningIF"), this, &TuneFilterDecimate_i::configureTuner);
	setPropertyChangeListener(static_cast<std::string>("TuningNorm"), this, &TuneFilterDecimate_i::configureTuner);
	setPropertyChangeListener(static_cast<std::string>("FilterBW"), this, &TuneFilterDecimate_i::configureFilter);
	setPropertyChangeListener(static_cast<std::string>("DesiredOutputRate"), this, &TuneFilterDecimate_i::configureFilter);
	setPropertyChangeListener(static_cast<std::string>("filterProps"), this, &TuneFilterDecimate_i::configureFilter);
}

TuneFilterDecimate_i::~TuneFilterDecimate_i()
{
	if (tuner)
		delete tuner;
	if (filter)
		delete filter;
	if (decimate)
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
			TuningRF = TuningIF - chan_if + InputRF;
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
			TuningRF = TuningIF - chan_if + InputRF;
		} else {
			TuningRF = 0;
		}
	} else if ((propid == "TuningRF") && (this->TuneMode == "RF")) {
		TuningIF = TuningRF + chan_if - InputRF;
		if (InputRate > 0) {
			TuningNorm = (TuningIF / InputRate);
		} else {
			TuningNorm = 0.0;
		}
	}

	LOG_DEBUG(TuneFilterDecimate_i, "Tuner Settings"
			<< " mode: " << this->TuneMode
			<< " propid: "<< propid
			<< " InputRate: "<<InputRate
			<< " InputRF: "<<InputRF
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

void TuneFilterDecimate_i::start() throw (CORBA::SystemException, CF::Resource::StartError) {
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

int TuneFilterDecimate_i::serviceFunction() {
	bulkio::InFloatPort::dataTransfer *pkt = dataFloat_In->getPacket(0.0); // non-blocking
	if(pkt == NULL) return NOOP;

	if(pkt->inputQueueFlushed)
	{
		LOG_WARN(TuneFilterDecimate_i, "Input queue has been flushed.  Data has been lost");
		RemakeFilter = true; // flush filter
	}

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
	if(pkt->sriChanged || RemakeFilter || TuningRFChanged || (dataFloat_Out->getCurrentSRI().count(pkt->streamID)==0)) {
		LOG_DEBUG(TuneFilterDecimate_i, "Reconfiguring TFD");
		configureTFD(pkt->SRI); // Process and/or update the SRI
		dataFloat_Out->pushSRI(pkt->SRI); // Push the new SRI to the next component
		TuningRFChanged = false;
	}

	if ((tuner == NULL) || (filter == NULL) || (decimate == NULL)) {
		LOG_TRACE(TuneFilterDecimate_i, "TFD cannot complete work, dropping data");
		delete pkt;
		return NOOP;
	}

	size_t iInc; // Increment to parse packet into tunerInput
	size_t buffLen_0; // Length of initial buffer
	if(inputComplex) {
		iInc = 2;
		buffLen_0 = pkt->dataBuffer.size()/2; // pkt->dataBuffer.size() will never be odd (or it shouldn't be)
	}
	else {
		iInc = 1;
		buffLen_0 = pkt->dataBuffer.size();
	}
	tunerInput.resize(buffLen_0);
	f_complexIn.resize(buffLen_0);

	// Process dataBuffer vector
	int inputIndex = 0;
	for(size_t i=0; i < pkt->dataBuffer.size(); i+=iInc) { // dataBuffer must be even (and should be for complex data)
		if(inputComplex)
			// Convert to the tunerInput complex data type
			tunerInput[inputIndex++] = Complex(pkt->dataBuffer[i], pkt->dataBuffer[i+1]);
		else
			tunerInput[inputIndex++] = Complex(pkt->dataBuffer[i], 0);
	}

	// Run Tuner: fills up f_<type>In vector
	tuner->run();

	// Run Filter: fills up f_<type>Out vector
	filter->newComplexData(f_complexIn); // Tuner always outputs complex data in current implementation.

	size_t buffLen_1 = f_complexOut.size(); // Size the rest of the buffers according to the filtered data.
	decimateOutput.reserve((buffLen_1+DecimationFactor-1)/DecimationFactor);

	// Run Decimation: fills up decimateOutput vector
	if(decimate->run()) {
		floatBuffer.reserve(decimateOutput.size());
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

	if (pkt->EOS) {
		LOG_DEBUG(TuneFilterDecimate_i, "Received EOS for stream: '" << pkt->streamID << "'");
		streamID = ""; // Reset streamID on EOS to allow processing of new stream
		RemakeFilter = true; // Ensure filter is remade on next received packet
	}

	delete pkt; // Must delete the dataTransfer object when no longer needed

	return NORMAL;
}

void TuneFilterDecimate_i::configureTFD(BULKIO::StreamSRI &sri) {
	LOG_TRACE(TuneFilterDecimate_i, "Configuring SRI: "
			<< "sri.xdelta = " << sri.xdelta
			<< "sri.streamID = " << sri.streamID)
					Real tmpInputSampleRate = 1 / sri.xdelta; // Calculate sample rate received from SRI
	bool lastInputComplex = inputComplex;
	if (sri.mode==1)
	{
		inputComplex=true;
		chan_if =0.0; //center of band is 0 if we are complex
	}
	else
	{
		inputComplex = false;
		chan_if = tmpInputSampleRate/4.0; //center of band is fs/4 if we are real
		//output is always complex even if input is real
		sri.mode=1;
	}

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
	bool inputComplexChanged = lastInputComplex ^ inputComplex;

	if (tmpInputRF != InputRF) {
		LOG_DEBUG(TuneFilterDecimate_i, "Input RF changed " << tmpInputRF);
		InputRF = tmpInputRF;

		// If the TuneMode is RF, we actually need to retune
		if (TuneMode == "RF") {
			configureTuner("TuningRF");
		}
		TuningRF = InputRF + TuningIF - chan_if;
		LOG_DEBUG(TuneFilterDecimate_i, "Tuning RF: " << TuningRF);
	}
	else if (TuneMode == "RF" && inputComplexChanged)
	{
		//the RF didn't change but the IF is changing because we have switched between real and complex data
		configureTuner("TuningRF");
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

		tuner = new Tuner(tunerInput, f_complexIn, TuningNorm);
	}

	if ((filter==NULL) || sampleRateChanged || RemakeFilter) {
		LOG_DEBUG(TuneFilterDecimate_i, "Remaking filter");

		if((filter != NULL) || (decimate != NULL)) {
			delete filter;
			delete decimate;
		}
/*
 *                    ASCII ART to explain the filter design
 *
 *  Lowpass (Real filter)
 *  ---------|
 *           \
 *            \
 *             \
 *              |
 *  ----------------------------------
 *  0        FL FL+tw   fsOut/2   fsIn/2
 *
 *  The basic gist is we tune first, then filter.
 *  Then we filter
 *  Then we decimate
 *
 *  This means the filter is a LOWPASS filter which must be designed given the INPUT sampling frequency.
 *  The transition frequency happens at 1/2 the requested tune bandwidth.  Thus FL = FilterBW / 2.0.
 *  We need the transition region to "finish" by fsOut/2 to avoid aliasing, so we do a check for that too.
 *
 */
		Real FL = FilterBW / 2.0;

		//calculate the transition frequency necessary to avoid aliasing
		Real maxTW = (outputSampleRate/2.0)-FL;
		if (maxTW > 0 && maxTW <  filterProps.TransitionWidth)
		{
			LOG_WARN(TuneFilterDecimate_i, "input transition width "<< filterProps.TransitionWidth<<"  too large - replacing with "<< maxTW);
			filterProps.TransitionWidth = maxTW;
		}

		// We generate our FIR filter taps here. The read-only property 'taps' is set.
		// 	- We use the transition width and ripple specified by the user to create the filter taps.
		// 	- Normalized lowpass cutoff frequency is the only one we need; upper cutoff not used
		RealVector tmpVec;
		taps = filterdesigner_.wdfirHz(tmpVec, FIRFilter::lowpass, filterProps.Ripple, filterProps.TransitionWidth,
				FL, 0, InputRate, MIN_NUM_TAPS, MAX_NUM_TAPS);
		filterCoeff.clear();
		filterCoeff.reserve(tmpVec.size());
		for (RealVector::iterator i = tmpVec.begin(); i!=tmpVec.end(); i++)
			filterCoeff.push_back(*i);

		// Minimum FFT_size implemented
		size_t minFftSize = std::max(MIN_FFT_SIZE, pow2ge(2*taps));
		if(filterProps.FFT_size < minFftSize) {
			LOG_DEBUG(TuneFilterDecimate_i, "FFT_size too small, set to " << minFftSize);
			filterProps.FFT_size = minFftSize;
		} else if (filterProps.FFT_size > MAX_FFT_SIZE)
		{
			LOG_DEBUG(TuneFilterDecimate_i, "FFT_size too large, set to " << MAX_FFT_SIZE);
			filterProps.FFT_size = MAX_FFT_SIZE;
		}
		filter = new firfilter(filterProps.FFT_size, f_realOut, f_complexOut, filterCoeff);
		decimate = new Decimate(f_complexOut, decimateOutput, DecimationFactor);
		RemakeFilter = false;	
	}

	LOG_TRACE(TuneFilterDecimate_i, "Exit configureSRI()");
}
