#!/usr/bin/env python
#
# This file is protected by Copyright. Please refer to the COPYRIGHT file distributed with this 
# source distribution.
# 
# This file is part of REDHAWK Basic Components TuneFilterDecimate.
# 
# REDHAWK Basic Components TuneFilterDecimate is free software: you can redistribute it and/or modify it under the terms of 
# the GNU General Public License as published by the Free Software Foundation, either 
# version 3 of the License, or (at your option) any later version.
# 
# REDHAWK Basic Components TuneFilterDecimate is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
# without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
# PURPOSE.  See the GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along with this 
# program.  If not, see http://www.gnu.org/licenses/.
#
import unittest
import ossie.utils.testing
import os
from omniORB import any
from ossie.cf import CF
from omniORB import CORBA
from ossie.utils import sb
from ossie.properties import props_to_dict
from ossie.utils.sb import domainless
import math
import time
from ossie.cf import ExtendedCF
from scipy import fftpack
import random
import cmath

enablePlotting = False
if enablePlotting:
    import matplotlib.pyplot
import scipy

DEBUG_MODE = False

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
    
    def setProps(self, TuningRF=None, FilterBW=None, DesiredOutputRate=None, filterProps=None, TuneMode=None, TuningIF=None, TuningNorm=None):
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

        if filterProps!=None:
            self.filterProps = filterProps
            myProps.append(CF.DataType(id='filterProps',value=CORBA.Any(CORBA.TypeCode("IDL:CF/Properties:1.0"),
                [ossie.cf.CF.DataType(id='FFT_size', value=CORBA.Any(CORBA.TC_ulong, filterProps[0])),
                ossie.cf.CF.DataType(id='TransitionWidth', value=CORBA.Any(CORBA.TC_double, filterProps[1])),
                ossie.cf.CF.DataType(id='Ripple', value=CORBA.Any(CORBA.TC_double, filterProps[2]))])))

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

        # Clean up unit test
        self.src.stop()
        self.sink.stop()
        self.src.releaseObject()
        self.sink.releaseObject()
        ossie.utils.testing.ScaComponentTestCase.tearDown(self)

    def setupComponent(self):
        
        self.comp = sb.launch(self.spd_file, impl=self.impl)

    def testNanOutput(self):
        """ This was a test case that produced NaN output due to floating round off error in calculating the window function for the filter
        """
        self.setProps(TuneMode="IF", TuningIF=0,FilterBW=80.0e3, DesiredOutputRate=100.0e3)
        fs= 3.125e6
        freq = .25e6
        sig = genSinWave(fs, freq, 1024*1024)
 
        out,stream = self.main(sig,sampleRate=3.125e6)
        
        self.assertTrue(len(out)>0)
        self.assertFalse(math.isnan(out[0].real) or math.isnan(out[0].imag))

    def testDrift(self):
        """ This was a test case to test the oscilator error due to floating point accumulation errors
            we pass in all "ones" with no filtering or decimation to just get out the tuner value.  
            Then we measure that the magnitude of the tunner stays near one and that it maintains minimal phase errors
        """
        fs= 100e3
        freq = 12345.6789
        self.setProps(TuneMode="IF", TuningIF=freq,FilterBW=100.0e3, DesiredOutputRate=100.0e3)
        #sig = genSinWave(fs, freq, 1024*1024)
        sig = [1]*5000000
 
        out,stream = self.main(sig,sampleRate=fs, complexData=False)
        
        self.assertTrue(len(out)>0)
        print "got %s out" %len(out)
        #print out[200]
        outSteadyState = out[500:]
        #check for phase errors
        tuner = outSteadyState[0]
        dPhase = -2*math.pi*freq/fs
        phase = cmath.phase(outSteadyState[0])
        for i, val in enumerate(outSteadyState):
            #print phase, val
            valPhase = cmath.phase(val)
            #check for plus/minus pi ambiguity here
            if valPhase <-3.1 and phase > 3.1:
               valPhase+= 2*math.pi
            elif valPhase >3.1 and phase < -3.1:
                phase+=2*math.pi
            self.assertAlmostEqual(phase, valPhase, 2)
            phase=valPhase+dPhase
            if phase<-math.pi:
                phase+=2*math.pi
        #check for magnitude errors - the abs of each output should be approximately one
        self.assertTrue(all([abs(abs(x)-1.0) <1e-3 for x in outSteadyState]))
                
    def testCxIfOutput(self):
        """Use a complex sinusoid with the tuner in If Mode.  Tune the Sinusoid to baseband to get a constant output
        """
        fs=20000
        freq= 800
        self.setProps(TuneMode="IF", TuningIF=freq,FilterBW=300.0, DesiredOutputRate=700.0)
        sig = genSinWave(fs, freq, 1024*1024)
 
        out,stream = self.main(sig,sampleRate=fs)
        self.verifyConst(out)

    def testCxNormOutput(self):
        """Use a complex sinusoid with the tuner in Norm Mode.  Tune the Sinusoid to baseband to get a constant output
        """
        fs=20000
        freq= 800
        tuningNorm = freq/2.0/fs
        self.setProps(TuneMode="NORM", TuningNorm=freq,FilterBW=300.0, DesiredOutputRate=700.0)
        sig = genSinWave(fs, freq, 1024*1024)
 
        out,stream = self.main(sig,sampleRate=fs)
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
        
        out,stream = self.main(sig, fs, colRF=colRF)
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
        
        out,stream = self.main(sig, fs, colRF=colRF,complexData=False)
        self.verifyConst(out)            
 
    def testOutputBW(self):
        """Send white noise through the system and verify the fitler BW is appropriate
        """
        fBW= 2000.0
        T=10000
        self.setProps(TuningRF=0,FilterBW=fBW, DesiredOutputRate=T)
        input = [] 
        for i in xrange(1024*1024):
            input.append(random.random()-.5)
        #    input.append(0.0)
        

        out,stream = self.main(input,sampleRate=T)
        self.assertTrue(len(out)>0)
        sri = self.sink.sri() 
        steadyState = out[100:]
        fftNum = 4096
        numAvgs = min(len(steadyState)/fftNum, 20)
        print "numAvgs", numAvgs
        #take multiple ffts and sum them together to get a better picture of the filtering shape involved
        fSum = None
        #have an overlap of half the fft size
        shift = fftNum/2
        for i in xrange(numAvgs):
            f = scipy.fftpack.fftshift(fftpack.fft(steadyState[i*shift:],fftNum))
            fDb = [20*math.log10(abs(x)) for x in f]
            if fSum:
                fSum = [x+ y for x, y in zip(fSum,fDb)]
            else:
                fSum = fDb
        freqs = scipy.fftpack.fftshift(fftpack.fftfreq(fftNum,self.sink.sri().xdelta))
        fCutoff = fBW/2.0
        print "fCutoff = %s" %fCutoff
        passBand = [y for x,y in zip(freqs,fSum) if abs(x) <=fCutoff]
        stopBand = [y for x,y in zip(freqs,fSum) if abs(x) >1.5*fCutoff]

        if enablePlotting:
            matplotlib.pyplot.plot(freqs, fSum)
            matplotlib.pyplot.show()
        
        minPB = min(passBand)
        maxPB = max(passBand)
        
        maxStopBand = max(stopBand)
               
        self.assertTrue(maxStopBand<minPB)


    def testSigInOut(self):
        """Generate an input signal and verify the output is as expected
        """
        f= 800
        T=20000
        self.setProps(TuningIF=f,FilterBW=500.0, DesiredOutputRate=5000.0, TuneMode="IF")
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
        out,stream = self.main(input,T)
        
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
    
    def testSimpleResponse(self):
       fs=20000
       fsOut = fs#/10.0
       self.setProps(TuneMode="IF", TuningIF=0,FilterBW=8000, DesiredOutputRate=fsOut)
       self.doImpulseResponse(fs, cmplx=False)

    def testSimpleResponseCx(self):
       fs=20000
       fsOut = fs#/10.0
       self.setProps(TuneMode="IF", TuningIF=0,FilterBW=8000, DesiredOutputRate=fsOut)
       self.doImpulseResponse(fs, cmplx=True)

    def getFilterProps(self):
        """ get the filter properties from the component
        """
        props = self.comp.query([])
        d = props_to_dict(props)
        return d['filterProps']
      
    def doImpulseResponse(self, fs, cmplx=True):
        print "doImpulseResponse, cx = ", cmplx
        sig = [1]
        sig.extend([0]*(1024*1024-1))
        out,stream = self.main(sig,sampleRate=fs,complexData=cmplx)
        filtLen = int(self.comp.taps)
        fftNum = 2**int(math.ceil(math.log(filtLen, 2))+1)
        freqResponse = [20*math.log(max(abs(x),1e-9),10) for x in scipy.fftpack.fftshift(scipy.fftpack.fft(out,fftNum))]
        fsOut = 1.0/self.sink.sri().xdelta
        freqAxis =  scipy.fftpack.fftshift(scipy.fftpack.fftfreq(fftNum,1.0/fsOut))
        
        filterProps= self.getFilterProps()       
        stopThreshold = 20*math.log(filterProps['Ripple'],10)
        plusPassThreshold = 20*math.log(1+filterProps['Ripple'],10)
        minusPassThreshold = 20*math.log(1-filterProps['Ripple'],10)
        delta = filterProps['TransitionWidth']
        
        f1 = self.comp.FilterBW/2.0
        passband = [(-f1+delta, f1-delta)]
        stopband = [(-fs/2.0,-f1-delta), (f1+delta, fs/2.0)]
        
        for freq, val in zip(freqAxis, freqResponse):
            inPassband = False
            for fmin, fmax in passband:
                if fmin<=freq<=fmax:
                    #print "pb, freq = ", freq, "val = ", val, "abs(val) = ", abs(val), "threshold = ", minusPassThreshold, plusPassThreshold
                    self.assertTrue(minusPassThreshold<val<plusPassThreshold)
                    inPassband = True
                    break
            if not inPassband:
                for fmin, fmax in stopband:
                    if fmin<=freq<=fmax:
                        #print "sb, freq = ", freq, "val = ", val, "threshold = ", stopThreshold
                        self.assertTrue(val<stopThreshold)
                        break
        
        if enablePlotting:
            matplotlib.pyplot.plot(freqAxis, freqResponse)
            matplotlib.pyplot.show()

    def testLowTaps(self): # Test that the minimum of 25 taps is in place
       fBW = 2000
       inpRate = 5000
       #normalFL = 1.6
       delta = 0.1778
       #A = 15
       dw = 222.816
       fs = 2500
       #tw = 0.28
       # M = 20, M-1 = 20

       sig = genSinWave(inpRate, 1000, 1024*1024)
       self.setProps(FilterBW=fBW, DesiredOutputRate=fs, filterProps=[128,dw,delta])
       out,stream = self.main(sig,inpRate);

       props = self.comp.query([])
       propDict = dict((x.id, any.from_any(x.value)) for x in props)
       tapCount = propDict['taps']

       self.assertTrue(tapCount == 25)
       
    def testCalcTapsA(self): # Test that the calculation for number of taps with actual stopband attenuation < 20.96 is correct
       fBW = 2000
       inpRate = 5000
       #normalFL = 1.6
       fft = 128
       delta = 0.0562
       #A = 25
       dw = 180.64
       fs = 2500
       #tw = 0.227
       # M = 33, M+1 = 34

       sig = genSinWave(inpRate, 1000, 1024*1024)
       self.setProps(FilterBW=fBW, DesiredOutputRate=fs, filterProps=[fft,dw,delta])
       out,stream = self.main(sig,inpRate);

       props = self.comp.query([])
       propDict = dict((x.id, any.from_any(x.value)) for x in props)
       tapCount = propDict['taps']
       self.assertTrue(tapCount == 34)


    def testCalcTapsB(self): # Test that the calculation for number of taps withi stopband attenuation >= 20.96 is correct
       fBW = 2000
       inpRate = 5000
       #normalFL = 1.6
       fft = 128
       delta = 0.1778
       #A = 15
       dw = 140.056
       fs = 2500
       #tw = 0.176
       # M = 33, M+1 = 34

       sig = genSinWave(inpRate, 1000, 1024*1024)
       self.setProps(FilterBW=fBW, DesiredOutputRate=fs, filterProps=[fft,dw,delta])
       out,stream = self.main(sig,inpRate);

       props = self.comp.query([])
       propDict = dict((x.id, any.from_any(x.value)) for x in props)
       tapCount = propDict['taps']

       self.assertTrue(tapCount == 34)

    def testHighTaps(self): # Test that the FFT_size/2 cap works 
       fBW = 2000
       inpRate = 5000
       #normalFL = 1.6
       fft = 128
       delta = 0.1778
       #A = 15
       dw = 65.254
       fs = 2500
       #tw = 0.082
       # M = 71, M+1 = 72

       sig = genSinWave(inpRate, 1000, 1024*1024)
       self.setProps(FilterBW=fBW, DesiredOutputRate=fs, filterProps=[fft,dw,delta])
       out,stream = self.main(sig,inpRate);

       props = self.comp.query([])
       propDict = dict((x.id, any.from_any(x.value)) for x in props)
       tapCount = propDict['taps']
       filterPropDict = dict((x['id'], x['value']) for x in propDict['filterProps'])
       self.assertTrue(tapCount <= filterPropDict['FFT_size']/2)
       #make sure 2* taps is the closest power of two
       self.assertTrue(2**math.ceil(math.log(tapCount*2,2.0))== filterPropDict['FFT_size'])

    def testManyConfigure(self):
        """Configure the filter settings over and over again in a tight loop to ensure the class can handle
           the "angry operator" scinaro in which user constantly changes the settings
        """
        fs = 10000
        freq = 3000
        colRF = 100e3
        tuneRf = colRF+freq-fs/4.0
        self.setProps(TuningRF=int(tuneRf),FilterBW=200.0, DesiredOutputRate=1000.0)
        sig = genSinWave(fs, freq, 1024*1024, cx=False)

        count=0
        fft=2048
        dw=32.627
        delta=0.1778
        fBW=fs/10
        fftSelections=[2**i for i in xrange(0,31)]
        
        startTime= time.time()
        while count<1000:
            self.src.push(sig,
                      complexData = False,
                      sampleRate=fs,
                      SRIKeywords = [sb.io_helpers.SRIKeyword('COL_RF',colRF, 'double')])
            try:
                self.setProps(filterProps=[random.choice(fftSelections),dw,delta])
            except Exception, e:
                print "\n\n!!!!!!you got an exception", e
                print "\n\n"
                print "count = ", count
                raise e
            count +=1
            if time.time()-startTime > 60.0:
                break
            time.sleep(.001)

    def testIFReal(self):
        self.tuneModeTest("IF", False)

    def testIFComplex(self):
        self.tuneModeTest("IF", True)

    def testNormReal(self):
        self.tuneModeTest("NORM", False)

    def testNormComplex(self):
        self.tuneModeTest("NORM", True)

    def testNormKwCx(self):
        self.tuneModeKwTest("NORM", True)

    def testNormKwReal(self):
        self.tuneModeKwTest("NORM", False)

    def testRfKwCx(self):
        self.tuneModeKwTest("RF", True)

    def testRfKwReal(self):
        self.tuneModeKwTest("RF", False)

    def testIfKwCx(self):
        self.tuneModeKwTest("IF", True)

    def testIfKwReal(self):
        self.tuneModeKwTest("IF", False)

    def testNormKwCx2(self):
        self.tuneModeKwTest("NORM", True, colRfType='double', chanRfType='double')

    def testNormKwReal2(self):
        self.tuneModeKwTest("NORM", False, colRfType='double', chanRfType='double')

    def testRfKwCx2(self):
        self.tuneModeKwTest("RF", True, colRfType='double', chanRfType='double')

    def testRfKwReal2(self):
        self.tuneModeKwTest("RF", False, colRfType='double', chanRfType='double')

    def testIfKwCx2(self):
        self.tuneModeKwTest("IF", True, colRfType='double', chanRfType='double')

    def testIfKwReal2(self):
        self.tuneModeKwTest("IF", False, colRfType='double', chanRfType='double')

    def testRfReal(self):
        self.tuneModeTest("RF", False)

    def testRfComplex(self):
        self.tuneModeTest("RF", True)

    def testRfRealTwo(self):
        self.tuneModeTest("RF", False,'double')

    def testRfComplexTwo(self):
        self.tuneModeTest("RF", True, 'double')

    def testRfRealThree(self):
        self.tuneModeTest("RF", False,'float')

    def testRfComplexThree(self):
        self.tuneModeTest("RF", True, 'float')

    def tuneModeTest(self,tuneMode, cmplx=False, colRfType='double'):
        inpRate = 1e6
        colRF= 1e9
        sig = genSinWave(inpRate, 1000, 1024*1024)
        
        self.setProps(DesiredOutputRate=inpRate)
        
        if cmplx:
            cfIF = 0.0
        else:
            cfIF = inpRate / 4.0

        tuneIF = inpRate/8.0
        tuneRF = tuneIF - cfIF + colRF
        tuneNorm = float(tuneIF)/inpRate
        
        self.comp.TuneMode = tuneMode
        if tuneMode== "NORM":
            self.comp.TuningNorm = tuneNorm
            self.assertEqual(tuneNorm, self.comp.TuningNorm)
        elif tuneMode== "IF":
            print "set IF value"
            self.comp.TuningIF = tuneIF
            self.assertEqual(self.comp.TuningIF, tuneIF)
            print "IF value set"
        elif tuneMode== "RF":
            self.comp.TuningRF = tuneRF
            self.assertEqual(self.comp.TuningRF, tuneRF)
        else:
            raise RuntimeError("invalid tune mode")
        
        out,stream = self.main(sig,inpRate, colRF=colRF, complexData=cmplx, colRfType=colRfType)

        if DEBUG_MODE:
            self.comp.api()

        self.assertEqual(tuneMode, self.comp.TuneMode)
        self.assertEqual(inpRate, self.comp.InputRate)
        self.assertEqual(colRF, self.comp.InputRF)

        self.assertEqual(tuneIF, self.comp.TuningIF)
        self.assertEqual(tuneRF, self.comp.TuningRF)
        self.assertEqual(tuneNorm, self.comp.TuningNorm)

    def tuneModeKwTest(self,tuneMode, cmplx=False, colRfType='double', chanRfType=None):
        inpRate = 1e6
        colRF= 1e9
        chanRF= colRF if chanRfType == None else colRF+inpRate*0.2
        sig = genSinWave(inpRate, 1000, 1024*1024)
        
        self.setProps(DesiredOutputRate=inpRate)
        
        if cmplx:
            cfIF = 0.0
        else:
            cfIF = inpRate / 4.0

        tuneIF = inpRate/8.0
        tuneRF = tuneIF - cfIF + chanRF
        tuneNorm = float(tuneIF)/inpRate
        
        self.comp.TuneMode = tuneMode
        if tuneMode== "NORM":
            self.comp.TuningNorm = tuneNorm
            self.assertEqual(tuneNorm, self.comp.TuningNorm)
        elif tuneMode== "IF":
            print "set IF value"
            self.comp.TuningIF = tuneIF
            self.assertEqual(self.comp.TuningIF, tuneIF)
            print "IF value set"
        elif tuneMode== "RF":
            self.comp.TuningRF = tuneRF
            self.assertEqual(self.comp.TuningRF, tuneRF)
        else:
            raise RuntimeError("invalid tune mode")
        
        out = self.checkKeywords(sig,inpRate, colRF=colRF, complexData=cmplx, colRfType=colRfType, expectedChanRf=tuneRF, chanRfType=chanRfType, chanRF=chanRF)

        if DEBUG_MODE:
            self.comp.api()

        self.assertEqual(tuneMode, self.comp.TuneMode)
        self.assertEqual(inpRate, self.comp.InputRate)
        self.assertEqual(chanRF, self.comp.InputRF)

        self.assertEqual(tuneIF, self.comp.TuningIF)
        self.assertEqual(tuneRF, self.comp.TuningRF)
        self.assertEqual(tuneNorm, self.comp.TuningNorm)

    def testNoEOS(self):
        inpRate = 2e6
        desiredOutRate = 1e6
        
        numSamples=16384
        
        sig = [1000*random.random() for _ in xrange(numSamples)]
        
        self.comp.TuneMode ="IF"
        self.comp.TuningIF = 3.6e6
        self.comp.FilterBW = 8e5
        self.comp.DesiredOutputRate = 1e6
        

        out,stream = self.main(sig,inpRate, complexData=False, pktSize=1)

        #stream = self.sink.getCurrentStream(10)

        if not stream:
            print "Did not find an output stream"
            self.assertFalse("No Output Stream")    
        count = 0
        while True:
            if stream.eos():
                break
            time.sleep(.01)
            count+=1
            if count==500:
                break
        self.assertNotEqual(count,500)


    def testMultiStream(self):
        
        fileSize =2.70e6
        floatSize = 1
        numSamples = int(fileSize/floatSize)/2*2
        sig = [1000*random.random() for _ in xrange(numSamples)]
            
        inpRate = 25e6
        desiredOutRate = 2e6
    
        self.comp.TuneMode ="IF"
        self.comp.TuningIF = 3.6e6
        self.comp.FilterBW = 2.05e6
        self.comp.DesiredOutputRate = 2e6
        
        outA,stream = self.main(sig,inpRate, complexData=True, streamID="tfd-stream-outA")
        self.src.reset()
        self.sink.reset()

        outB,stream = self.main(sig,inpRate, complexData=True, streamID="tfd-stream-outB")

        print "input lenght %s" %len(sig)
        print "got output %s, %s" %(len(outA), len(outB))

        for i, (a, b) in enumerate(zip(outA,outB)):
            try:
                self.assertAlmostEqual(a.real, b.real, 2)
                self.assertAlmostEqual(a.imag, b.imag, 2)
            except:
                print "problem with sample %s" %i
                print a, b
                print abs(a), abs(b)
                raise



    def checkKeywords(self,inData, sampleRate, colRF=0.0, complexData = True, colRfType='double', pktSize=8192, checkOutputSize=True, streamID="tfd-stream-1", expectedChanRf=0.0, chanRfType=None, chanRF=None):
        """ Check Keywords CHAN_RF and COL_RF
           As applicable
        """
        out=[]
        count=0
        keywords = [sb.io_helpers.SRIKeyword('COL_RF',colRF, colRfType)]
        if chanRfType != None and chanRF != None:
            keywords.append(sb.io_helpers.SRIKeyword('CHAN_RF',chanRF, chanRfType))
        numPushes = (len(inData)+pktSize-1)/pktSize
        lastPush = numPushes-1
        for i in xrange(numPushes):
            eos = i==lastPush
            self.src.push(inData[i*pktSize:(i+1)*pktSize],
                          streamID=streamID,
                          complexData = complexData, 
                          sampleRate=sampleRate,
                          SRIKeywords = keywords,
                          EOS=eos)
            newOut = self.sink.getData()
            if newOut:
                count += 1
                out.extend(newOut)
                sri_keywords = self.sink.sri().keywords
                for sri_kw in sri_keywords:
                    if sri_kw.id == 'COL_RF':
                        self.assertAlmostEqual(sri_kw.value.value(),colRF)
                    elif sri_kw.id == 'CHAN_RF':
                        self.assertAlmostEqual(sri_kw.value.value(),expectedChanRf)

    def main(self,inData, sampleRate, colRF=0.0, complexData = True, colRfType='double', pktSize=8192, checkOutputSize=True, streamID="tfd-stream-1"):
        """The main engine for all the test cases - configure the equation, push data, and get output
           As applicable
        """
        #data processing is asynchronos - so wait until the data is all processed
        count=0
        keywords = [sb.io_helpers.SRIKeyword('COL_RF',colRF, colRfType)]
        numPushes = (len(inData)+pktSize-1)/pktSize
        lastPush = numPushes-1
        for i in xrange(numPushes):
            eos = i==lastPush
            self.src.push(inData[i*pktSize:(i+1)*pktSize],
                          streamID=streamID,
                          complexData = complexData, 
                          sampleRate=sampleRate,
                          SRIKeywords = keywords,
                          EOS=eos)
        out=[]
        count=0
        while True:
            stream = self.sink.getCurrentStream(5)
            dataBlock = None
            if stream:
                dataBlock = stream.read()
            else:
                count+=1
                continue
            if dataBlock:    
                newOut = dataBlock.data()
                out.extend(newOut)
                count=0
            elif stream.eos():
                break
            elif count==200:
                print "Count reached 200, gave up trying to get data"
                break
            time.sleep(.01)
            count+=1
        
        sri = self.sink.sri()
        outputRate = 1.0/ sri.xdelta
        expectedDecimation = math.floor(sampleRate/self.comp.DesiredOutputRate)
        expectedOutputRate = sampleRate/expectedDecimation
        self.assertAlmostEqual(expectedOutputRate,outputRate, places=2)
        self.assertAlmostEqual(self.comp.ActualOutputRate,outputRate, places=2)
        
        outCx = toCx(out)
        if checkOutputSize:
            frameSize = self.comp.filterProps.FFT_size-self.comp.taps+1
            inDataNum = len(inData)
            if complexData:
                inDataNum/=2
            expectedCompleteFrames = inDataNum/frameSize # int, truncated if incomplete last frame
            outDataNum = math.ceil(expectedCompleteFrames*frameSize/expectedDecimation)
                # round up if there is a fractional number of expected output samples
            self.assertEqual(outDataNum, len(outCx))

        
        return outCx,stream
    
if __name__ == "__main__":
    ossie.utils.testing.main("../TuneFilterDecimate.spd.xml") # By default tests all implementations
