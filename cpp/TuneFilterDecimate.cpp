/*
 * This file is protected by Copyright. Please refer to the COPYRIGHT file distributed with this
 * source distribution.
 *
 * This file is part of REDHAWK Basic Components TuneFilterDecimate.
 *
 * REDHAWK Basic Components TuneFilterDecimate is free software: you can redistribute it and/or modify it under the terms of
 * the GNU Lesser General Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * REDHAWK Basic Components TuneFilterDecimate is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
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

 **************************************************************************/

#include "TuneFilterDecimate.h"

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
	if(pkt->inputQueueFlushed)
		LOG_WARN(TuneFilterDecimate_i, "Input queue has been flushed.  Data has been lost");

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
	filter->newComplexData(); // Tuner always outputs complex data in current implementation.

	size_t buffLen_1 = f_complexOut.size(); // Size the rest of the buffers according to the filtered data.
	decimateOutput.reserve(buffLen_1);
	floatBuffer.reserve(buffLen_1);

	// Run Decimation: fills up decimateOutput vector
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

		Real normFl = FilterBW / InputRate; // Fixed normalized LPF cutoff frequency.
		LOG_DEBUG(TuneFilterDecimate_i, "FilterBW " << FilterBW
				<< " InputRate " << InputRate
				<< " FilterNorm " << normFl);

		// Minimum FFT_size implemented
		FFT_size_int = filterProps.FFT_size;
		if(FFT_size_int < 64) {
			LOG_DEBUG(TuneFilterDecimate_i, "FFT_size too small, set to 64");
			filterProps.FFT_size = 64;
			FFT_size_int = 64;
		}
		
		filter = new firfilter(FFT_size_int, f_realIn, f_complexIn, f_realOut, f_complexOut);
		// We generate our FIR filter taps here. The read-only property 'taps' is set.
		// 	- Output sampling rate is 1.0/xdelta divided by the decimation factor.
		// 		** sri.xdelta was already modified according to the output sampling rate above.
		// 	- We use the transition width and ripple specified by the user to create the filter taps.
		// 	- Normalized lowpass cutoff frequency is the only one we need; upper cutoff removed.
		taps = generateTaps((1.0/sri.xdelta),filterProps.TransitionWidth,filterProps.Ripple,normFl);
		filter->setTaps(filterCoeff);
		decimate = new Decimate(f_complexOut, decimateOutput, DecimationFactor);
		RemakeFilter = false;	
	}

	LOG_TRACE(TuneFilterDecimate_i, "Exit configureSRI()");
}

int TuneFilterDecimate_i::generateTaps(const double& sFreq, const double& dOmega, const double& delta, Real fl) {
	// This function implements the Kaiser window filter design method as a function of:
	// Parameters:
	// 	- sFreq: the output sampling frequency
	//	- dOmega: the desired transition region width
	//	- delta: ripple, or the maximum bound on error in the pass and stop bands
	// Output:
	// 	- t: returns the number of taps generated
	// ** Based off of information found at: http://www.labbookpages.co.uk/audio/firWindowing.html#kaiser
	double atten = -20.0*log10(delta); // actual stopband attenuation
	double tw = 2.0*M_PI*dOmega/sFreq;
	int M; // filter order
	
	if(atten >= 20.96)
		M = ceil((atten-7.95)/(2.285*tw));
	else // if(atten < 20.96)
		M = ceil(5.79/tw);

	size_t t = M-1; // M-1 = number of taps
	if(t < 25) // 25 is the minimum tap count
		t = 25;
	if(t > filterProps.FFT_size/2.0) // FFT_size/2 is the max for the number of taps
		t = filterProps.FFT_size/2.0;
	filterCoeff.resize(t);

	size_t ieo, nh;
	Real c, c1, c3, xn, beta;
	ieo = filterCoeff.size() % 2;

	// Window coefficients
	const size_t sz = filterCoeff.size();
	RealArray winCoef(sz);

	// Constructs the initial filter, based on a sinc function ( sin(x)/x ).
	// We choose to only compute half of the array, since sinc is an even function => we reflect the data.
	c1 = fl;
	nh = (1 + sz) / 2;
	if (ieo == 1)
		filterCoeff[nh - 1] = 2.0 * c1;

	for (size_t ii = ieo; ii < nh; ii++) {
		xn = ii;
		if (ieo == 0)
			xn += 0.5;

		c = M_PI * xn;
		c3 = c * c1 * 2.0;

		size_t jj = nh + ii - ieo;
		filterCoeff[jj] = sin (c3) / c;
		filterCoeff[nh - ii - 1] = filterCoeff[jj];
	}

	// Calculates the value of parameter beta based on actual stopband attenuation.
	if (atten > 50.0) {
		beta = 0.1102 * (atten - 8.7);
	}
	else if (atten >= 20.96 && atten <= 50.0) {
		Real tmp = atten - 20.96;
		beta = 0.58417 * pow (tmp, Real(0.4)) + 0.07886 * tmp;
	}
	else { // if (atten < 20.96)
		beta = Real(0);
	}
	kaiser (winCoef, beta); // builds the window weights
	
	for(size_t j=0; j < filterCoeff.size(); j++) {
		filterCoeff[j] *= winCoef[j]; // applies the Kaiser window weights
	}
	
	return t;
}

void TuneFilterDecimate_i::kaiser(RealArray &w, Real beta) {
	// Based on values for beta, this function constructs actual window weights with the Kaiser window equation.
	// Makes use of the 0th order Bessel function.
	size_t n = (1 + w.size()) / 2, ieo = w.size() % 2;
	Real bes = in0 (beta);
	Real xind = pow ((w.size() - 1.0), 2);

	for (size_t ii = 0; ii < n; ii++) {
		Real xi = ii;
		if (ieo == 0)
			xi += 0.5;
		size_t jj = n + ii - ieo;
		w[n - 1 - ii] =
			w[jj] = in0 (beta * sqrt(1.0 - 4.0 * xi * xi / xind)) / bes;
	}
}

Real TuneFilterDecimate_i::in0(Real x) {
	// Implements the Taylor expansion of a 0th order Bessel function (I0)
	// 	- I0(x) = Sum[(x/2)^(2i)/(i!)^2 , {i, 0, Infinity}]
	// 		= 1 + (x/2)^2/(1!)^2 + (x/2)^4/(2!)^2 + (x/2)^6/(3!)^2 + ..
	// Because the denominator becomes quickly large, we only need calculate I0 up to around x = 25.
	const Real t = 1.0e-7;
	Real e = 1, de = 1;
	Real y = 0.5 * x;
	Real xi, sde;

	for (size_t ii = 1; ii < 25; ++ii) {
		xi = ii;
		de *= y / xi;
		sde = de * de;
		e += sde;
		if (e * t - sde > Real(0))
			return e;
	}
	return e;
} 
