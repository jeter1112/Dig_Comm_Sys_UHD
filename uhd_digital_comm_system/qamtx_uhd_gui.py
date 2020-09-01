import argparse
import sys
import time
import threading
import logging
import numpy as np
import uhd
import matplotlib.pyplot as pl
import matplotlib.widgets as wg
import frame as fr
import uhd_conf as ucf
import datetime
from PyQt5 import QtWidgets, QtCore

import pyqtgraph as pg




sample_size=363*4
recv_buffer = np.zeros(sample_size, dtype=np.complex64)
rss=0.0





CLOCK_TIMEOUT = 1000  # 1000mS timeout for external clock locking
INIT_DELAY = 1  # 50mS initial delay before transmit

def parse_args():
    """Parse the command line arguments"""
    parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("-a", "--args", default="addr=192.168.1.4", type=str, help="single uhd device address args")
    parser.add_argument("-d", "--duration", default=18.0, type=float,
                        help="duration for the test in seconds")
    parser.add_argument("--rate", type=float, default=25e6,help="IQ rate(sps)") 
    parser.add_argument("--gain", type=float, default=0,help="gain") 
    parser.add_argument("--tx", type=bool, default=False,help="enableTX")
    parser.add_argument("--rx", type=bool, default=False,help="enableRX")
    return parser.parse_args()


def tx_host(usrp, tx_streamer, timer_elapsed_event):
    """Benchmark the transmit chain"""
    print(" TX freq:%.3f GHz, IQ rate %.3f Msps, gain:%.1f, ant: %s, bandwidth:%.3f MHz,spb:%d",
                 usrp.get_tx_freq()/1e9,usrp.get_tx_rate() / 1e6,usrp.get_tx_gain(),usrp.get_tx_antenna(),usrp.get_tx_bandwidth()/1e6,tx_streamer.get_max_num_samps())

    # Make a transmit buffer
    # spb = tx_streamer.get_max_num_samps()
    spb=200
    buf_length=120
    filename="bits.txt"
    sdata=None
    with open(filename,"rb") as f:
        sdata=np.fromfile(f,dtype="int8",count=buf_length)
    # f=fr.frame(sdata)
    # transmit_buffer=f.frame_to_uhd_buffer(spb)
    f=fr.frame(sdata)
    transmit_buffer=f.frame_to_uhd_buffer(spb)
    metadata = uhd.types.TXMetadata()
    metadata.time_spec = uhd.types.TimeSpec(usrp.get_time_now().get_real_secs() + INIT_DELAY)
    metadata.has_time_spec = False
    print('begin transmit')
    while not timer_elapsed_event.is_set():
        tx_streamer.send(transmit_buffer, metadata)
    # Send a mini EOB packet
    metadata.end_of_burst = True
    tx_streamer.send(np.zeros((1,0), dtype=np.complex64), metadata)


def rx_host(usrp,rx_streamer,timer_elapsed_event):
    print(" RX freq:%.3f GHz, IQ rate %.3f Msps, gain:%.1f, ant: %s, bandwidth:%.3f MHz,spb:%d",
                 usrp.get_rx_freq()/1e9,usrp.get_rx_rate() / 1e6,usrp.get_rx_gain(),usrp.get_rx_antenna(),usrp.get_rx_bandwidth()/1e6,rx_streamer.get_max_num_samps())

    metadata = uhd.types.RXMetadata()
    # Craft and send the Stream Command
    stream_cmd = uhd.types.StreamCMD(uhd.types.StreamMode.start_cont)
    stream_cmd.stream_now = True
    stream_cmd.time_spec = uhd.types.TimeSpec(usrp.get_time_now().get_real_secs() + INIT_DELAY)
    rx_streamer.issue_stream_cmd(stream_cmd)

    
    
    print('begin receive')
    global rss
    global recv_buffer

        
    while not timer_elapsed_event.is_set():
        rx_streamer.recv(recv_buffer, metadata)
        power=np.mean(np.power(np.absolute(recv_buffer),2))+0.00000000001
        rss=10*np.log10(power/50.0)
        # recv_buffer.tofile(f)
    # with open("usrp_samples.dat","w") as f:
    #     while not timer_elapsed_event.is_set():
    #         rx_streamer.recv(recv_buffer, metadata)
    #         rss=10*np.log10(np.mean(np.power(np.absolute(recv_buffer),2))/50.0)
    #         recv_buffer.tofile(f)
    # After we get the signal to stop, issue a stop command
    rx_streamer.issue_stream_cmd(uhd.types.StreamCMD(uhd.types.StreamMode.stop_cont))
    print('RX Exit')

def rss_save(a,timer_elapsed_event):
    pass
    # s = socket(AF_INET, SOCK_DGRAM)
    # addr = ("192.168.1.20", 5211)
    # dat="angle".encode()
    # time.sleep(1)
    # pass
    # global rss
    # dt = datetime.datetime.now()
    # with open(str(dt),"w") as f:
    #     for i in range(0,360):
    #         f.write("%.2f," %time.time()+"%.2f\n" %rss)
    #         time.sleep(0.02)
    #         s.sendto(dat, addr)
    #         time.sleep(0.1)

    # s.close()        
    # #     rsslabel.set_val("%.2f" % rss)
    # #     pl.pause(0.5)

def rss_recv_gui(a,timer_elapsed_event):
    pass
    global rss
    global recv_buffer
    # print('GUI')
    # figure, ax = pl.subplots(figsize=(6,4))
    # h,= ax.plot(recv_buffer.imag)
    # z,= ax.plot(recv_buffer.real)
    # pl.ion()
    # pl.title("recvd IQ samples",fontsize=18)
    # pl.ylabel("Amplitute",fontsize=12)
    # pl.ylim([-0.5,0.5])
    # rsslabel  = wg.TextBox(pl.axes([0.7, 0.01, 0.2, 0.04]),'RSS: ')
    # rsslabel.set_val("%.2f" % 1.3)

    # pl.draw()
    # while not timer_elapsed_event.is_set():    
    #     rsslabel.set_val("%.2f" % rss)
    #     h.set_ydata(recv_buffer.imag)
    #     z.set_ydata(recv_buffer.real)

    #     pl.pause(1.0)

    # class MainWindow(QtWidgets.QMainWindow):

    #     def __init__(self, *args, **kwargs):
    #         super(MainWindow, self).__init__(*args, **kwargs)

    #         self.graphWidget = pg.PlotWidget()
    #         self.setCentralWidget(self.graphWidget)

    #         self.x = np.array(list(range(sample_size)))  


    #         self.graphWidget.setBackground('w')

            
    #         self.real =  self.graphWidget.plot(self.x, recv_buffer.real, pen=pg.mkPen('b'))
    #         self.imag =  self.graphWidget.plot(self.x, recv_buffer.imag, pen=pg.mkPen('r'))
    #         self.timer = QtCore.QTimer()
            
    #         self.timer.setInterval(500)
    #         self.timer.timeout.connect(self.update_plot_data)
    #         self.timer.start()

    #     def update_plot_data(self):

    #         self.real.setData(self.x, recv_buffer.real) 
    #         self.imag.setData(self.x, recv_buffer.imag) 
    # app = QtWidgets.QApplication([])
    # w = MainWindow()
    # w.show()
    # app.exec_()
def main():
    """Run the benchmarking tool"""
    args = parse_args()
    usrp,st_args=ucf.uhd_builder(args.args,args.gain,args.rate)
    
    print("..........build......\n")
    threads=[]
    tx_quit_event = threading.Event()
    rx_quit_event = threading.Event()
    rss_quit_event = threading.Event()
    gui_quit_event = threading.Event()
    if args.tx:

        
        tx_streamer = usrp.get_tx_stream(st_args)
        tx_thread = threading.Thread(target=tx_host,
                                    args=(usrp, tx_streamer, tx_quit_event))
        threads.append(tx_thread)
        tx_thread.start()
        tx_thread.setName("tx_stream")
    if args.rx:

        
        rx_streamer = usrp.get_rx_stream(st_args)
        rx_thread = threading.Thread(target=rx_host,args=(usrp, rx_streamer, rx_quit_event))
        threads.append(rx_thread)
        rx_thread.start()
        rx_thread.setName("rx_stream")
    
    a=1

    rss_thread = threading.Thread(target=rss_save,args=(a,rss_quit_event))
    threads.append(rss_thread)
    rss_thread.start()
    rss_thread.setName("rss_save")

    gui_thread = threading.Thread(target=rss_recv_gui,args=(a,gui_quit_event))
    threads.append(gui_thread)
    gui_thread.start()
    gui_thread.setName("rss_save")
    time.sleep(args.duration)
    # Interrupt and join the threads
    print("Sending signal to stop!")
    tx_quit_event.set()
    rx_quit_event.set()
    rss_quit_event.set()
    gui_quit_event.set()

    for thr in threads:
        thr.join()

    return True

if __name__ == "__main__":
    main()
