import struct
import numpy
import matplotlib.pyplot as pl
data=''
filename="usrp_samps.dat"

with open(filename,'rb') as f:
    data=f.read(12288)




# file  data structure
#-----------------|------------------|------------------|...
#len(frame2)=1996 | len(frame2)=1996 |len(frame3)=1996  |...
#    ch1                 ch2               ch1             ...

# frame data structure

#-----------------|------------------|------------------|..
#    I                    Q                I
# len(I)=float or Int16

##
## procedure:
# 1. map data to raw IQ sequence.
# 2. split IQ seuence to frame chunks
# 3. join and reduce chunks to channel IQ
# 4. filter each channel I/Q
# 5. plot
frame_len=1996*2 # I and Q;
channel_nums=2
print(len(data))

#1. map data to raw IQ sequence.
channels_raw_IQ=struct.unpack('f'*(len(data)//4),data)

len_raw_IQ=len(channels_raw_IQ)//20

#2. split IQ seuence to frame chunks
chunks_raw_IQ=[ channels_raw_IQ[i:i+frame_len] for i in range(0,len_raw_IQ,frame_len)]

#3. join and reduce chunks to channel IQ
len_chunks_raw_IQ=len(chunks_raw_IQ)
ch1_IQ = [chunks_raw_IQ[i][j] for i in range(0,len_chunks_raw_IQ,channel_nums) for j in range(0,len(chunks_raw_IQ[i]))]
ch2_IQ = [chunks_raw_IQ[i][j] for i in range(1,len_chunks_raw_IQ,channel_nums) for j in range(0,len(chunks_raw_IQ[i]))]

# 4. filter each channel I/Q
ch1_I=[ch1_IQ[i] for i in range(0,len(ch1_IQ),2)]
ch1_Q=[ch1_IQ[i] for i in range(1,len(ch1_IQ),2)]

# 5. plot
pl.plot(ch1_I)
pl.plot(ch1_Q)
# pl.plot(ch1_I_frame2)

pl.show()