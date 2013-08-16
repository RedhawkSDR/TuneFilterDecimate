#!/usr/bin/env python
#
# This file is protected by Copyright. Please refer to the COPYRIGHT file distributed with this 
# source distribution.
# 
# This file is part of REDHAWK Basic Components.
# 
# REDHAWK Basic Components is free software: you can redistribute it and/or modify it under the terms of 
# the GNU Lesser General Public License as published by the Free Software Foundation, either 
# version 3 of the License, or (at your option) any later version.
# 
# REDHAWK Basic Components is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
# without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
# PURPOSE.  See the GNU Lesser General Public License for more details.
# 
# You should have received a copy of the GNU Lesser General Public License along with this 
# program.  If not, see http://www.gnu.org/licenses/.
#
import unittest
import ossie.utils.testing
import os
from omniORB import any
from ossie.cf import CF
from omniORB import CORBA
from ossie.utils import sb
from ossie.utils.sb import domainless
import math
import time
from ossie.cf import ExtendedCF
from scipy import fftpack
import random

def genSinWave(fs, freq, numPts, cx=True, startTime=0, amp=1):
    xd = 1.0/fs
    phase =  2*math.pi*startTime
    phaseInc = 2*math.pi*freq/fs
    output = []
    for i in xrange(numPts): 
        output.append(amp*math.cos(phase))
        if cx:
            output.append(amp*math.sin(phase))
        phase+=phaseInc
    return output

def toCx(input):
    output =[]
    for i in xrange(len(input)/2):
        output.append(complex(input[2*i], input[2*i+1]))
    return output 

class ComponentTests(ossie.utils.testing.ScaComponentTestCase):
    """Test for all component implementations in TuneFilterDecimate"""

    def verifyConst(self, out):
        self.assertTrue(len(out)>0)
        steadyState = out[100:]
        r = [x.real for x in steadyState]
        i = [x.imag for x in steadyState]
        dR = max(r)-min(r)
        dI = max(i)-min(i)
        self.assertTrue(dR<1)
        self.assertTrue(dI<1)    
    
    def setProps(self, TuningRF=None, FilterBW=None, DesiredOutputRate=None, TuneMode=None, TuningIF=None, TuningNorm=None):
        myProps=[]
        if TuningRF!=None:
            self.TuningRF = TuningRF
            myProps.append(CF.DataType(id='TuningRF',value=CORBA.Any(CORBA.TC_ulonglong, self.TuningRF)))

        if FilterBW!=None:
            self.FilterBW = FilterBW
            myProps.append(CF.DataType(id='FilterBW',value=CORBA.Any(CORBA.TC_float, self.FilterBW)))
        
        if DesiredOutputRate!=None:
            self.DesiredOutputRate = DesiredOutputRate
            myProps.append(CF.DataType(id='DesiredOutputRate',value=CORBA.Any(CORBA.TC_float, self.DesiredOutputRate)))

        if TuneMode!=None:
            self.TuneMode = TuneMode
            myProps.append(CF.DataType(id='TuneMode',value=CORBA.Any(CORBA.TC_string, TuneMode)))
            
        if TuningIF!=None:
            self.TuningIF = TuningIF
            myProps.append(CF.DataType(id='TuningIF',value=CORBA.Any(CORBA.TC_double, TuningIF)))

        if TuningNorm!=None:
            self.TuningNorm = TuningNorm
            myProps.append(CF.DataType(id='TuningNorm',value=CORBA.Any(CORBA.TC_double, TuningNorm)))

        if myProps:
            print "configuring with ", myProps
            #configure it
            self.comp.configure(myProps)
            print self.comp.query([])

    def setUp(self):
        """Set up the unit test - this is run before every method that starts with test
        """
        ossie.utils.testing.ScaComponentTestCase.setUp(self)
        self.src = sb.DataSource()
        self.sink = sb.DataSink()
        
        #setup my components
        self.setupComponent()
        
        self.setProps(TuningRF=100)
        
        self.comp.start()
        self.src.start()
        self.sink.start()
        
        #do the connections
        self.src.connect(self.comp)        
        self.comp.connect(self.sink)
        
    def tearDown(self):
        """Finish the unit test - this is run after every method that starts with test
        """
        self.comp.stop()
        #######################################################################
        # Simulate regular component shutdown
        self.comp.releaseObject()
        self.sink.stop()      
        ossie.utils.testing.ScaComponentTestCase.tearDown(self)

    def setupComponent(self):
        """Standard start-up for testing the component
        """
        #######################################################################
        # Launch the component with the default execparams
        execparams = self.getPropertySet(kinds=("execparam",), modes=("readwrite", "writeonly"), includeNil=False)
        execparams = dict([(x.id, any.from_any(x.value)) for x in execparams])
        self.launch(execparams)
        
        #######################################################################
        # Verify the basic state of the component
        self.assertNotEqual(self.comp, None)
        self.assertEqual(self.comp.ref._non_existent(), False)
        self.assertEqual(self.comp.ref._is_a("IDL:CF/Resource:1.0"), True)
        #self.assertEqual(self.spd.get_id(), self.comp.ref._get_identifier())
        
        #######################################################################
        # Simulate regular component startup
        # Verify that initialize nor configure throw errors
        #self.comp.initialize()
        configureProps = self.getPropertySet(kinds=("configure",), modes=("readwrite", "writeonly"), includeNil=False)
        self.comp.configure(configureProps)
        
        #######################################################################
        # Validate that query returns all expected parameters
        # Query of '[]' should return the following set of properties
        expectedProps = []
        expectedProps.extend(self.getPropertySet(kinds=("configure", "execparam"), modes=("readwrite", "readonly"), includeNil=True))
        expectedProps.extend(self.getPropertySet(kinds=("allocate",), action="external", includeNil=True))
        props = self.comp.query([])
        props = dict((x.id, any.from_any(x.value)) for x in props)
        # Query may return more than expected, but not less
        for expectedProp in expectedProps:
            self.assertEquals(props.has_key(expectedProp.id), True)
        
        #######################################################################
        # Verify that all expected ports are available
        for port in self.scd.get_componentfeatures().get_ports().get_uses():
            port_obj = self.comp.getPort(str(port.get_usesname()))
            self.assertNotEqual(port_obj, None)
            self.assertEqual(port_obj._non_existent(), False)
            self.assertEqual(port_obj._is_a("IDL:CF/Port:1.0"),  True)
            
        for port in self.scd.get_componentfeatures().get_ports().get_provides():
            port_obj = self.comp.getPort(str(port.get_providesname()))
            self.assertNotEqual(port_obj, None)
            self.assertEqual(port_obj._non_existent(), False)
            self.assertEqual(port_obj._is_a(port.get_repid()),  True)

                
    def testCxIfOutput(self):
        """Use a complex sinusoid with the tuner in If Mode.  Tune the Sinusoid to baseband to get a constant output
        """
        fs=20000
        freq= 800
        self.setProps(TuneMode="IF", TuningIF=freq,FilterBW=300.0, DesiredOutputRate=700.0)
        sig = genSinWave(fs, freq, 1024*1024)
 
        out = self.main(sig,sampleRate=fs)
        self.verifyConst(out)

    def testCxNormOutput(self):
        """Use a complex sinusoid with the tuner in Norm Mode.  Tune the Sinusoid to baseband to get a constant output
        """
        fs=20000
        freq= 800
        tuningNorm = freq/2.0/fs
        self.setProps(TuneMode="NORM", TuningNorm=freq,FilterBW=300.0, DesiredOutputRate=700.0)
        sig = genSinWave(fs, freq, 1024*1024)
 
        out = self.main(sig,sampleRate=fs)
        self.verifyConst(out)

    def testCxRfTune(self):
        """Use a complex sinusoid with the tuner in RfMode.  Tune the Sinusoid to baseband to get a constant output
        """
        fs = 10000
        freq = 3000
        colRF = 100e3
        tuneRf = colRF+freq
        self.setProps(TuningRF=int(tuneRf),FilterBW=200.0, DesiredOutputRate=1000.0)
        sig = genSinWave(fs, freq, 1024*1024)
        
        out = self.main(sig, fs, colRF=colRF)
        self.verifyConst(out)

    def testRealRfTune(self):
        """Use a real sinusoid with the tuner in RfMode.  Tune the Sinusoid to baseband to get a constant output
        """
        fs = 10000
        freq = 3000
        colRF = 100e3
        tuneRf = colRF+freq-fs/4.0
        self.setProps(TuningRF=int(tuneRf),FilterBW=200.0, DesiredOutputRate=1000.0)
        sig = genSinWave(fs, freq, 1024*1024, cx=False)
        
        out = self.main(sig, fs, colRF=colRF,complexData=False)
        self.verifyConst(out)            
 
    def testOutputBW(self):
        """Send white noise threw the system and verify the fitler BW is appropriate
        """
        fBW= 2000.0
        T=10000
        self.setProps(TuningRF=0,FilterBW=fBW, DesiredOutputRate=T)
        input = [] 
        for i in xrange(1024*1024):
            input.append(random.random()-.5)
        #    input.append(0.0)
        

        out = self.main(input,sampleRate=T)
        self.assertTrue(len(out)>0)
        sri = self.sink.sri() 
        steadyState = out[100:]
        fftNum = 4096
        f = fftpack.fft(steadyState,fftNum)
        fDb = [20*math.log10(abs(x)) for x in f]
        freqs = fftpack.fftfreq(fftNum,self.sink.sri().xdelta)
        passBand = [y for x,y in zip(freqs,fDb) if abs(x) <=fBW]
        stopBand = [y for x,y in zip(freqs,fDb) if abs(x) >1.5*fBW]
        
        minPB = min(passBand)
        maxPB = max(passBand)
        
        maxStopBand = max(stopBand)
               
        self.assertTrue(maxStopBand<minPB)


    def testSigInOut(self):
        """Generate an input signal and verify the output is as expected
        """
        f= 800
        T=20000
        self.setProps(TuningRF=f,FilterBW=500.0, DesiredOutputRate=5000.0)
        delF = 2*math.pi*f/T 
        inputBBCX = []
        delta = .01 
        overSample = int(T/self.DesiredOutputRate)
        #create an input siganl consisting of sawtooth wave in opposite directions for real and imaginary parts...
        #because we can and it is easy to generate
        val = 0
        print overSample
        expectedOutput=[]
        for i in xrange(256*1024):
            if abs(val) > 2:
               delta*=-1      
            val+=delta    
            #shove the same guy in there multiple times for our input because we are going to be down sampling him in the ftd
            myVal = complex(val,-val)
            for _ in xrange(overSample):
                inputBBCX.append(myVal)
            expectedOutput.append(myVal)
       
        #modulate this bad boy up to the tune freq
        inputCx = []
        delF = 2*math.pi*f/T
        print delF
        phase=0 
        for val in inputBBCX:
            phase +=delF
            inputCx.append(val*complex(math.cos(phase),math.sin(phase)))
        #unpack the complex input singal into real,iminganary values to pass him into the ftd
        input=[]
        for val in inputCx:
            input.append(val.real)
            input.append(val.imag)

        #now we have an expected input and output run the puppy
        out = self.main(input,T)
        
        #git rid of the first few points because of steady state error
        steadyState = out[100:]
        expectedSteadyState = expectedOutput[100:]
        #filter introduces a gain which is different for the real and imaginary parts
        #this is OK - but lets compensate for it before we compare results
        #also - there is a wie bit of delay introduced by the filtering - but for the purposes of our test this is all OK
        realGain = sum([x.real/y.real for (x,y) in zip(expectedSteadyState,steadyState)])/len(steadyState)
        imagGain = sum([x.imag/y.imag for (x,y) in zip(expectedSteadyState,steadyState)])/len(steadyState)
        
        scaled=  [complex(x.real*realGain,x.imag*imagGain) for x in steadyState]
        
        error = sum([abs(x-y) for x, y in zip(scaled, expectedSteadyState)])/len(steadyState)
        self.assertTrue(error<2)
        
    def main(self,inData, sampleRate, colRF=0.0, complexData = True):
        """The main engine for all the test cases - configure the equation, push data, and get output
           As applicable
        """
        #data processing is asynchronos - so wait until the data is all processed
        count=0
        self.src.push(inData,
                      complexData = complexData, 
                      sampleRate=sampleRate,
                      SRIKeywords = [sb.io_helpers.SRIKeyword('COL_RF',colRF, 'long')])
        out=[]
        while True:
            newOut = self.sink.getData()
            if newOut:
                out.extend(newOut)
                count=0
            elif count==100:
                break
            time.sleep(.01)
            count+=1
        
        sri = self.sink.sri()
        outputRate = 1.0/ sri.xdelta
        expectedDecimation = math.floor(sampleRate/self.DesiredOutputRate)
        expectedOutputRate = sampleRate/expectedDecimation
        self.assertAlmostEqual(expectedOutputRate,outputRate, places=2)
        
        return toCx(out)
    
if __name__ == "__main__":
    ossie.utils.testing.main("../TuneFilterDecimate.spd.xml") # By default tests all implementations
