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
#include "TuneFilterDecimate_base.h"

/*******************************************************************************************

    AUTO-GENERATED CODE. DO NOT MODIFY

    The following class functions are for the base class for the component class. To
    customize any of these functions, do not modify them here. Instead, overload them
    on the child class

******************************************************************************************/

TuneFilterDecimate_base::TuneFilterDecimate_base(const char *uuid, const char *label) :
    Resource_impl(uuid, label),
    ThreadedComponent()
{
    loadProperties();

    dataFloat_In = new bulkio::InFloatPort("dataFloat_In");
    addPort("dataFloat_In", dataFloat_In);
    dataFloat_Out = new bulkio::OutFloatPort("dataFloat_Out");
    addPort("dataFloat_Out", dataFloat_Out);
}

TuneFilterDecimate_base::~TuneFilterDecimate_base()
{
    delete dataFloat_In;
    dataFloat_In = 0;
    delete dataFloat_Out;
    dataFloat_Out = 0;
}

/*******************************************************************************************
    Framework-level functions
    These functions are generally called by the framework to perform housekeeping.
*******************************************************************************************/
void TuneFilterDecimate_base::start() throw (CORBA::SystemException, CF::Resource::StartError)
{
    Resource_impl::start();
    ThreadedComponent::startThread();
}

void TuneFilterDecimate_base::stop() throw (CORBA::SystemException, CF::Resource::StopError)
{
    Resource_impl::stop();
    if (!ThreadedComponent::stopThread()) {
        throw CF::Resource::StopError(CF::CF_NOTSET, "Processing thread did not die");
    }
}

void TuneFilterDecimate_base::releaseObject() throw (CORBA::SystemException, CF::LifeCycle::ReleaseError)
{
    // This function clears the component running condition so main shuts down everything
    try {
        stop();
    } catch (CF::Resource::StopError& ex) {
        // TODO - this should probably be logged instead of ignored
    }

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
                "execparam,configure");

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
                0LL,
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

    addProperty(taps,
                "taps",
                "",
                "readonly",
                "",
                "external",
                "configure");

    addProperty(filterProps,
                filterProps_struct(),
                "filterProps",
                "",
                "readwrite",
                "",
                "external",
                "configure");

}


