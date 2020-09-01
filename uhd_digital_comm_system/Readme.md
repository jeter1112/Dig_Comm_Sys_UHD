# Digital communication System(USRP platform)


## Done and To be done


- [x] GUI version
      - start with matplotlib gui widget. -> take up too much resoure.
      - turn to pyqt5. -> a coarse GUI.
      - 
    - [ ] fine GUI
        

## Structure

```mod_demod.py``` is for QAM modulation and demodulation.
```filter.py``` provides pulse shaping and matched filter.
```frame.py``` and ```ofdm_frame.py``` to form frames with training and payload.

```qamtx_uhd.py``` and ```qamtx_uhd_gui.py``` are the top blocks.

``` IQ_extract.py``` and ```offline...py``` analyse waveform and do backend(timing,frequency,channel estimation)



