# Digital Communication System with UHD(usrp driver)

## Background

Our team requires to develop a MIMO system with usrprio with more than 100M sampling rate. We find that UHD is better than GUNradio,Labview and Matlab to meet the high sampling rate. UHD provides C++ interface to access and configure
USRP. There is a library called ```pybind11``` which can export c++11 interface to Python interface. The development divides into two phase.

1. Transmit and Receive waveform with 100Msps from/to file.
2. Digital communication system design and implementation(QAM,OFDM).

## Done and To be done

1. Transmit and Receive waveform with 100Msps from/to file(USRPRio platform).

   - [x] receive waveform to file(1 channel)
         - start with UHD C++ example code.
         - be familiar with UHD C++ interface(configure freq, antenna,rate,receive/send)
   - [x] receive waveform to file with 100Msps(1 channel)
         - start with C native IO file operation
         - use DPDK, then We find that MTU set to 9000 can achieve.
    - [x] receive waveform to file with 100Msps(2 channel)
         - start with C native IO file operation
         - use ```libaio``` to accelerate file operation ,finally we choose to use ```liburing``` IO to finish 100Msps.
         - [ ] phase and amplitude correction.

2. Digital communication System(USRP platform)

   - [x] QAM modluation,demodulation
         - start with C++ -> be annoyed with no library and trouble grammar
         - turn to Python, I use numpy and scipy to complete.
         - 
   - [x] Pulse shaping and matched filter
         - start with Python,using numpy -> good simulate result,but It is likely that USRP received with double rate.
         - Turn to C++, USRP works fine  -> ```xtensor``` library is worse than numpy. C++ is annoyed......
         - Turn to Python, I find that double rate is due to data type conversion( complex float32 to complex double64).
    - [x] carrier Frequency correction, time synchronization and channel estimation,OFDM
         - start with Robert Health book in wireless communication LAB. -> I implement FFT, I found some property of OFDM,finally I use ```numpy FFT```.
         - I start Receiver process with first save waveform file, then decode offline. <- energy detection should be done to check received buffer
         contains information.
         - [ ] Real time Receiver.

## Structure

The first Phase is in ```uhd_2_channel_full_rate_recv``` directory.
The second Phase is in ```uhd_digital_comm_system``` directory.


## Installation
Prerequistes: UHD3.15.0,numpy,matplotlib, PyQt5 lib and Boost lib(C++ lib).

- others:
  PyQt5 contains a lot of components.
  ```
  $sudo apt-get install python3-pip  libboost-all-dev
  $pip3 install numpy mako requests six pyqt5 pyqtgraph
  ```
- UHD3.15.0: 
    - Download Source file:https://github.com/EttusResearch/uhd/archive/v3.15.0.0.tar.gz
    - Cmake configure: 
    ```
    $ cmake -DENABLE_PYTHON3=ON -DENABLE_TESTS=OFF -DENABLE_RFNOC=OFF -DENABLE_C_API=OFF -DENABLE_X300=OFF -DENABLE_USRP2=ON -DENABLE_N230=OFF -DENABLE_N300=OFF -DENABLE_E320=OFF -DENABLE_OCTOCLOCK=OFF -DENABLE_MANUAL=OFF -DCMAKE_INSTALL_PREFIX=/usr ..

    ```
    - Install
    ```   
    $  make and sudo make install ``` 