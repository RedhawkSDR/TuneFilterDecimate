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
    std::cout << "TFD::TuneFilterDecimate() constructor entry"<<std::endl;
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
    inputSampleRate = 0.0;
    inputIndex = 0;
    col_rf = 0;
    chan_rf = 0;
    TuningRFChanged = false;
    RemakeFilter = false;

    // Initialize provides port maxQueueDepth
    dataFloat_In->setMaxQueueDepth(1000);
}

TuneFilterDecimate_i::~TuneFilterDecimate_i()
{
	delete tuner;
	delete filter;
	delete decimate;
}

void TuneFilterDecimate_i::configure(const CF::Properties& props) throw (CORBA::SystemException, CF::PropertySet::InvalidConfiguration, CF::PropertySet::PartialConfiguration)
{
	std::cout << "TFD::configure() entry"<<std::endl;
	TuneFilterDecimate_base::configure(props);

    // Update the tuner only if the component has already been configured by SRI
    if(tuner != NULL) {
    	// Convert TuningRF to IF
    	for (CORBA::ULong i=0; i< props.length(); ++i) {
			const std::string id = (const char*) props[i].id;
			PropertyInterface* property = getPropertyFromId(id);
			if (property->id=="TuningRF") {
				long TuningIF = TuningRF - col_rf;
				//bsg - I think it should be OK if this is less than 0
				if(TuningIF < 0) {
					std::cout << "ERROR @ TFD::configure() - Calculated IF < 0."<<std::endl;
					return;
				}
				// Convert IF to normalized IF and re-tune the tuner
				Real normFc = TuningIF / inputSampleRate;
				std::cout << "TFD::configure() - Retuning tuner"<<std::endl;
				tuner->retune(normFc);
				TuningRFChanged = true;
			}
			else if (property->id=="FilterBW" || property->id=="DesiredOutputRate") {
				RemakeFilter = true;
			}
		}
    }
    else
    	std::cout << "WARNING @ TFD::configure() - tuner is NULL"<<std::endl;

    std::cout << "TFD::configure() - TuningRF = " << TuningRF << std::endl;
}

/***********************************************************************************************

    Basic functionality:

        The service function is called by the serviceThread object (of type ProcessThread).
        This call happens immediately after the previous call if the return value for
        the previous call was NORMAL.
        If the return value for the previous call was NOOP, then the serviceThread waits
        an amount of time defined in the serviceThread's constructor.
        
    SRI:
        To create a StreamSRI object, use the following code:
        	stream_id = "";
	    	sri = BULKIO::StreamSRI();
	    	sri.hversion = 1;
	    	sri.xstart = 0.0;
	    	sri.xdelta = 0.0;
	    	sri.xunits = BULKIO::UNITS_TIME;
	    	sri.subsize = 0;
	    	sri.ystart = 0.0;
	    	sri.ydelta = 0.0;
	    	sri.yunits = BULKIO::UNITS_NONE;
	    	sri.mode = 0;
	    	sri.streamID = this->stream_id.c_str();

	Time:
	    To create a PrecisionUTCTime object, use the following code:
	        struct timeval tmp_time;
	        struct timezone tmp_tz;
	        gettimeofday(&tmp_time, &tmp_tz);
	        double wsec = tmp_time.tv_sec;
	        double fsec = tmp_time.tv_usec / 1e6;;
	        BULKIO::PrecisionUTCTime tstamp = BULKIO::PrecisionUTCTime();
	        tstamp.tcmode = BULKIO::TCM_CPU;
	        tstamp.tcstatus = (short)1;
	        tstamp.toff = 0.0;
	        tstamp.twsec = wsec;
	        tstamp.tfsec = fsec;
        
    Ports:

        Data is passed to the serviceFunction through the getPacket call (BULKIO only).
        The dataTransfer class is a port-specific class, so each port implementing the
        BULKIO interface will have its own type-specific dataTransfer.

        The argument to the getPacket function is a floating point number that specifies
        the time to wait in seconds. A zero value is non-blocking. A negative value
        is blocking.

        Each received dataTransfer is owned by serviceFunction and *MUST* be
        explicitly deallocated.

        To send data using a BULKIO interface, a convenience interface has been added 
        that takes a std::vector as the data input

        NOTE: If you have a BULKIO dataSDDS port, you must manually call 
              "port->updateStats()" to update the port statistics when appropriate.

        Example:
            // this example assumes that the component has two ports:
            //  A provides (input) port of type BULKIO::dataShort called short_in
            //  A uses (output) port of type BULKIO::dataFloat called float_out
            // The mapping between the port and the class is found
            // in the component base class header file

            BULKIO_dataShort_In_i::dataTransfer *tmp = short_in->getPacket(-1);
            if (not tmp) { // No data is available
                return NOOP;
            }

            std::vector<float> outputData;
            outputData.resize(tmp->dataBuffer.size());
            for (unsigned int i=0; i<tmp->dataBuffer.size(); i++) {
                outputData[i] = (float)tmp->dataBuffer[i];
            }

            // NOTE: You must make at least one valid pushSRI call
            if (tmp->sriChanged) {
                float_out->pushSRI(tmp->SRI);
            }
            float_out->pushPacket(outputData, tmp->T, tmp->EOS, tmp->streamID);

            delete tmp; // IMPORTANT: MUST RELEASE THE RECEIVED DATA BLOCK
            return NORMAL;

        Interactions with non-BULKIO ports are left up to the component developer's discretion

    Properties:
        
        Properties are accessed directly as member variables. For example, if the
        property name is "baudRate", it may be accessed within member functions as
        "baudRate". Unnamed properties are given a generated name of the form
        "prop_n", where "n" is the ordinal number of the property in the PRF file.
        Property types are mapped to the nearest C++ type, (e.g. "string" becomes
        "std::string"). All generated properties are declared in the base class
        (TuneFilterDecimate_base).
    
        Simple sequence properties are mapped to "std::vector" of the simple type.
        Struct properties, if used, are mapped to C++ structs defined in the
        generated file "struct_props.h". Field names are taken from the name in
        the properties file; if no name is given, a generated name of the form
        "field_n" is used, where "n" is the ordinal number of the field.
        
        Example:
            // This example makes use of the following Properties:
            //  - A float value called scaleValue
            //  - A boolean called scaleInput
            
            if (scaleInput) {
                dataOut[i] = dataIn[i] * scaleValue;
            } else {
                dataOut[i] = dataIn[i];
            }
            


        
************************************************************************************************/

void TuneFilterDecimate_i::start() throw (CORBA::SystemException, CF::Resource::StartError)
{

	// Process the SRI and create an initial filter if one is not already created

	if ((*(dataFloat_In->activeSRIs())).length() > 0 ){
		configureSRI((*(dataFloat_In->activeSRIs()))[0]);
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
    		std::cout<<"TFD::WARNING -- pkt streamID "<<pkt->streamID<<" differs from streamID "<< streamID<<". Throw the data on the floor"<<std::endl;
    		delete pkt; //must delete the dataTransfer object when no longer needed
    		return NORMAL;
    	}
    }

	// Check if SRI has been changed
	if(pkt->sriChanged || TuningRFChanged || (dataFloat_Out->currentSRIs.count(pkt->streamID)==0)) {
		configureSRI(pkt->SRI); // Process and/or update the SRI
		dataFloat_Out->pushSRI(pkt->SRI); // Push the new SRI to the next component
		TuningRFChanged = false;
	}
	if(pkt->inputQueueFlushed)
		std::cout << "ERROR @ TFD::serviceFunction - Input queue is flushing!"<<std::endl;
	if(tuner == NULL) {
		std::cout << "WARNING @ TFD::serviceFunction() - Tuner not configured."<<std::endl;
		delete pkt;
		return NOOP;
	}

	// Process dataBuffer vector
	for(size_t i=0; i < pkt->dataBuffer.size(); i+=2) { // dataBuffer must be event (and should be for complex data)
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

void TuneFilterDecimate_i::configureSRI(BULKIO::StreamSRI &sri)
{
	std::cout << "TFD::configureSRI() entry\n"
			  << "\tsri.xdelta = " << sri.xdelta << "\n"
			  << "\tsri.streamID = " << sri.streamID <<std::endl;
	if (sri.mode!=1)
	{
		std::cout << "WARNING - TFD::configureSRI() - Input data is real but TuneFilterDecimate requires compelx data."<<std::endl;
		std::cout << "treating data as if it were complex."<<std::endl;
	}

	Real tmpInputSampleRate = 1 / sri.xdelta; // Calculate sample rate received from SRI
	size_t decimationFactor = floor(tmpInputSampleRate/DesiredOutputRate);
	std::cout << "TFD::decimationFactor = " << decimationFactor << std::endl;
	if (decimationFactor <1)
		decimationFactor=1;

	bool sampleRateChanged =(inputSampleRate != tmpInputSampleRate);
	if (sampleRateChanged)
	{
		inputSampleRate = tmpInputSampleRate;
		std::cout << "TFD::configureSRI() - Sample rate changed: inputSampleRate = " << inputSampleRate << std::endl;
	}

	// Calculate new output sample rate & modify the referenced SRI structure
	outputSampleRate = inputSampleRate / decimationFactor;
	sri.xdelta = 1 / outputSampleRate;
	std::cout<<"TFD::configureSRI() output xdelta "<<sri.xdelta<<std::endl;

	if (outputSampleRate < FilterBW)
		std::cout<<"TFD::configureSRI() WARNING - outputSampleRate "<<outputSampleRate<<" is less than FilterBW "<<FilterBW<<std::endl;


	// Reconfigure the processing classes only if the sample rate has changed
	if(sampleRateChanged || RemakeFilter) {
		std::cout << "TFD::configureSRI() - Remake filter "<<std::endl;

		if((tuner != NULL) || (filter != NULL) || (decimate != NULL)) {
			delete tuner;
			delete filter;
			delete decimate;
		}

		// Retrieve the front-end collected RF to determine the IF
		bool valid = false;
		col_rf = getKeywordByID<CORBA::Long>(sri, "COL_RF", valid);
		if(!valid) {
			std::cout << "ERROR @ TFD::configureSRI() : Invalid SRI Keyword 'COL_RF'."<<std::endl;;
			return;
		}
		// Convert TuningRF to the appropriate IF
		long TuningIF = TuningRF - col_rf;
		if(TuningIF < 0) {
			std::cout << "ERROR @ TFD::configureSRI() : Calculated IF < 0."<<std::endl;
			return;
		}
		std::cout << "TFD::configureSRI() - COL_RF = " << col_rf << std::endl
				  << "TFD::configureSRI() - TuningRF = " << TuningRF << std::endl
				  << "TFD::configureSRI() - TuningIF = " << TuningIF << std::endl;
		// Convert IF to normalized IF and configure the tuner
		Real normFc = TuningIF / inputSampleRate; // Normalized tuning frequency
		tuner = new Tuner(tunerInput, filterInput, normFc);
		Real normFl = FilterBW / inputSampleRate; // Fixed normalized LPF cutoff frequency
		filter = new FIRFilter(filterInput, decimateInput, FIRFilter::lowpass, 70, normFl);
		decimate = new Decimate(decimateInput, decimateOutput, decimationFactor);
		RemakeFilter = false;
	}

	// Always add the CHAN_RF keyword to the SRI
	if(!setKeywordByID<CORBA::Long>(sri, "CHAN_RF", TuningRF))
		std::cout << "TFD::configureSRI() - SRI Keyword CHAN_RF could not be set."<<std::endl;
	std::cout << "TFD::done configureSRI()"<<std::endl;
}
