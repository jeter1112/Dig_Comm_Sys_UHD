# UHD 2 Channel Full Rate Receiver





## Done and To be done


- [x] receive waveform to file with 100Msps(2 channel)
    - start with C native IO file operation
    - use ```libaio``` to accelerate file operation ,finally we choose to use ```liburing``` IO to finish 100Msps.

## structure
   ```testio.c```, ```libaiotest.c``` and ```testuring.c``` present Linux native IO, ```libaio```,```liburing``` file operation speed. In my computer, ```liburing``` is the fastest and 2 time faster than native IO.
   
   ```recv.cpp``` and ```trans.cpp``` are the codes which adapted from uhd example with IO operation,etc.
   ``` 2_channel_recv.cpp``` and ```tsff.cpp`` are rewritten.
   ``` IQ_extract.py``` analyse receivd waveform.

