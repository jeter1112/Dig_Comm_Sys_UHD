"""
frame

=========

provides:
1. frame creation and payload extraction
2. frame syncronization

frame structure:

#--------------------------#
| header|       payload    |
#--------------------------#


           header
#--------------------------#
| PN | PN | ..........| PN |
#--------------------------#
    Repeat N times
"""
import numpy as np
from  mod_demod import modulate,demodulate
import filter
import matplotlib.pyplot as pl
barker_code_lut={
    2: np.array([1.0+0j,-1]),
    3: np.array([1.0+0j,1,-1]),
    4: np.array([1.0+0j,1,-1,1]),
    5: np.array([1.0+0j,1,1,-1,1]),
    7: np.array([1.0+0j,1,1,-1,-1,1,-1]),
    11: np.array([1.0+0j,1,1,-1,-1,-1,1,-1,-1,1,-1]),
    13: np.array([1.0+0j,1,1,1,1,-1,-1,1,1,-1,1,-1,1])
}


class frame:
    def __init__(self,bin_payload:np.array,order=4,num_PN=13,num_rpt_PN=6):
        assert num_PN in barker_code_lut.keys(), "invalid number of PN sequence, please specify PN in:"+str(barker_code_lut.keys())
        bar=barker_code_lut[num_PN]
        # # barmod=(np.real(bar)+1j*np.real(bar))/np.sqrt(2)
        # self.preamble=np.tile(barmod,num_rpt_PN)
        self.preamble=np.tile(bar,num_rpt_PN)
        
        self.payload = modulate(bin_payload,order)
        self.paclen=self.preamble.size+self.payload.size
        print("pac len:",self.paclen)
    def frame_to_uhd_buffer(self,spb=120):
        # buf=np.zeros(spb*4,dtype=np.complex64)
        
        # pad=20
        # padding=np.array([0.2+0.2j,0.1+0.1j,-0.2j-0.2j,0.1j+0.1j,0.3j+0.3j,-0.3j-0.3j,0.1j+0.1j,0.3j+0.3j,-0.3j-0.3j,0.3j+0.3j],dtype=np.complex64)
        # buf[pad//2:pad]=padding
        # buf[pad:self.paclen+pad]= np.concatenate((self.preamble,self.payload))
        
        # buf[self.paclen+pad:self.paclen+pad+pad//2]=padding
        
        # buffer=filter.pulse_shaping(buf,4,0.9,2)
        
        # norm=np.max(np.absolute(buffer))
        # buffer=buffer/norm
        # buffer=buffer[:363*4]
        # buffer=buffer*np.exp(-2j*np.pi*80000/12500000*np.arange(0,363*4,dtype=np.float32))
        # buffer=buffer.reshape(1,-1)
        # buffer.dtype=np.complex64
        # print(buffer.dtype)
        # print(buffer.size)
        # # buffer=filter.matched_filter(buffer,4,0.1,4)
        # # pl.show()
        # # buffer=np.zeros(spb,dtype=np.complex64)
        # # buffer[:(spb//2)]=np.cos(np.arange(0,spb//2)*2*np.pi/spb)/2
        # # print(buffer)
        buffer=np.zeros(363,dtype=np.complex64)
        buffer[:]= np.exp(2j*(np.pi*np.arange(363)/8))/1.414
        
        return buffer


# f=frame(np.array([1,0,0,1]))
# b=f.frame_to_uhd_buffer(200)
