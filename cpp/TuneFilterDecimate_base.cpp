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

#include "TuneFilterDecimate_base.h"

/*******************************************************************************************

    AUTO-GENERATED CODE. DO NOT MODIFY
    
 	Source: TuneFilterDecimate.spd.xml
 	Generated on: Wed Feb 27 17:57:02 EST 2013
 	Redhawk IDE
 	Version:R.1.8.2
 	Build id: v201212041901

*******************************************************************************************/

/******************************************************************************************

    The following class functions are for the base class for the component class. To
    customize any of these functions, do not modify them here. Instead, overload them
    on the child class

******************************************************************************************/
 
TuneFilterDecimate_base::TuneFilterDecimate_base(const char *uuid, const char *label) :
                                     Resource_impl(uuid, label), serviceThread(0) {
    construct();
}

void TuneFilterDecimate_base::construct()
{
    Resource_impl::_started = false;
    loadProperties();
    serviceThread = 0;
    
    PortableServer::ObjectId_var oid;
    dataFloat_In = new BULKIO_dataFloat_In_i("dataFloat_In", this);
    oid = ossie::corba::RootPOA()->activate_object(dataFloat_In);
    dataFloat_Out = new BULKIO_dataFloat_Out_i("dataFloat_Out", this);
    oid = ossie::corba::RootPOA()->activate_object(dataFloat_Out);

    registerInPort(dataFloat_In);
    registerOutPort(dataFloat_Out, dataFloat_Out->_this());
}

/*******************************************************************************************
    Framework-level functions
    These functions are generally called by the framework to perform housekeeping.
*******************************************************************************************/
void TuneFilterDecimate_base::initialize() throw (CF::LifeCycle::InitializeError, CORBA::SystemException)
{
}

void TuneFilterDecimate_base::start() throw (CORBA::SystemException, CF::Resource::StartError)
{
    boost::mutex::scoped_lock lock(serviceThreadLock);
    if (serviceThread == 0) {
        dataFloat_In->unblock();
        serviceThread = new ProcessThread<TuneFilterDecimate_base>(this, 0.1);
        serviceThread->start();
    }
    
    if (!Resource_impl::started()) {
    	Resource_impl::start();
    }
}

void TuneFilterDecimate_base::stop() throw (CORBA::SystemException, CF::Resource::StopError)
{
    boost::mutex::scoped_lock lock(serviceThreadLock);
    // release the child thread (if it exists)
    if (serviceThread != 0) {
        dataFloat_In->block();
        if (!serviceThread->release(2)) {
            throw CF::Resource::StopError(CF::CF_NOTSET, "Processing thread did not die");
        }
        serviceThread = 0;
    }
    
    if (Resource_impl::started()) {
    	Resource_impl::stop();
    }
}

CORBA::Object_ptr TuneFilterDecimate_base::getPort(const char* _id) throw (CORBA::SystemException, CF::PortSupplier::UnknownPort)
{

    std::map<std::string, Port_Provides_base_impl *>::iterator p_in = inPorts.find(std::string(_id));
    if (p_in != inPorts.end()) {

        if (!strcmp(_id,"dataFloat_In")) {
            BULKIO_dataFloat_In_i *ptr = dynamic_cast<BULKIO_dataFloat_In_i *>(p_in->second);
            if (ptr) {
                return BULKIO::dataFloat::_duplicate(ptr->_this());
            }
        }
    }

    std::map<std::string, CF::Port_var>::iterator p_out = outPorts_var.find(std::string(_id));
    if (p_out != outPorts_var.end()) {
        return CF::Port::_duplicate(p_out->second);
    }

    throw (CF::PortSupplier::UnknownPort());
}

void TuneFilterDecimate_base::releaseObject() throw (CORBA::SystemException, CF::LifeCycle::ReleaseError)
{
    // This function clears the component running condition so main shuts down everything
    try {
        stop();
    } catch (CF::Resource::StopError& ex) {
        // TODO - this should probably be logged instead of ignored
    }

    // deactivate ports
    releaseInPorts();
    releaseOutPorts();

    delete(dataFloat_In);
    delete(dataFloat_Out);
 
    Resource_impl::releaseObject();
}

void TuneFilterDecimate_base::loadProperties()
{
    addProperty(TuneMode,
                "NORM", 
               "TuneMode",
               "",
               "readwrite",
               "",
               "external",
               "execparam");

    addProperty(TuningNorm,
                0.0, 
               "TuningNorm",
               "",
               "readwrite",
               "",
               "external",
               "configure");

    addProperty(TuningIF,
                0, 
               "TuningIF",
               "",
               "readwrite",
               "Hz",
               "external",
               "configure");

    addProperty(TuningRF,
                0, 
               "TuningRF",
               "",
               "readwrite",
               "Hz",
               "external",
               "configure");

    addProperty(FilterBW,
                8000, 
               "FilterBW",
               "",
               "readwrite",
               "Hz",
               "external",
               "configure");

    addProperty(DesiredOutputRate,
                10000, 
               "DesiredOutputRate",
               "",
               "readwrite",
               "Hz",
               "external",
               "configure");

    addProperty(InputRF,
                0.0, 
               "InputRF",
               "",
               "readonly",
               "Hz",
               "external",
               "configure");

    addProperty(InputRate,
                0.0, 
               "InputRate",
               "",
               "readonly",
               "Hz",
               "external",
               "configure");

    addProperty(DecimationFactor,
               "DecimationFactor",
               "",
               "readonly",
               "",
               "external",
               "configure");

}
