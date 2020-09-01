
import uhd
import logging
##uhd parameter configure

def uhd_builder(args="addr=192.168.10.3",gain=0.0,rate=1e6,otw="sc16",cpu="fc32"):
    usrp=uhd.usrp.MultiUSRP(args)
    print("usrp created")
    usrp.set_clock_source("internal")
    usrp.set_time_source("none")
    logging.info("Setting device timestamp to 0...")
    usrp.set_time_now(uhd.types.TimeSpec(0.0))
    usrp.set_tx_rate(rate)
    usrp.set_tx_gain(0)
    usrp.set_tx_antenna("TX/RX")
    usrp.set_tx_bandwidth(40e6)
    
    usrp.set_tx_freq(uhd.libpyuhd.types.tune_request(0), 0)

    usrp.set_rx_rate(rate)
    usrp.set_rx_gain(0)
    usrp.set_rx_antenna("RX2")
    usrp.set_rx_bandwidth(40e6)
    usrp.set_rx_freq(uhd.libpyuhd.types.tune_request(0), 0)
    
    
    st_args = uhd.usrp.StreamArgs(cpu, otw)
    st_args.channels=[0]
    
    return[usrp,st_args]
