







#include<uhd/usrp/multi_usrp.hpp>     // main usrp class: open,configure ,run usrp device
#include<uhd/convert.hpp>             // OTW, CPU data format
#include<uhd/utils/safe_main.hpp>     // throw back error, not segmenttation dump
#include<uhd/utils/thread.hpp>        //boost thread, configure priority;



/*** boost libray:*/

#include<boost/algorithm/string.hpp>
#include<boost/date_time/posix_time/posix_time.hpp>
#include<boost/format.hpp>
#include<boost/program_options.hpp>
#include<boost/thread/thread.hpp>


/***std library*/
#include<atomic>
#include<chrono>
#include<complex>
#include<cstdlib>
#include<iostream>
#include<thread>
#include<fstream>
/** uring io*/
extern "C"
{
#include<fcntl.h>
#include<malloc.h>
#include<unistd.h>
#include <stdio.h>
#include<stdlib.h>
#include<liburing.h>
}


/*****************************************/
/*********evaluation variable*************/
unsigned long long num_overruns      = 0;
unsigned long long num_underruns     = 0;
unsigned long long num_rx_samps      = 0;
unsigned long long num_tx_samps      = 0;
unsigned long long num_dropped_samps = 0;
unsigned long long num_seq_errors    = 0;
unsigned long long num_seqrx_errors  = 0; // "D"s
unsigned long long num_late_commands = 0;
unsigned long long num_timeouts_rx   = 0;
unsigned long long num_timeouts_tx   = 0;

struct iovec* iovs;
struct io_uring ring;

struct io_uring_sqe *sqe;
off_t offset_file;

int num_recv_byte;

namespace po=boost::program_options; // use program_options namespaces;


inline boost::posix_time::time_duration time_delta(
    const boost::posix_time::ptime& ref_time)
{
    return boost::posix_time::microsec_clock::local_time() - ref_time;
}

inline std::string time_delta_str(const boost::posix_time::ptime& ref_time)
{
    return boost::posix_time::to_simple_string(time_delta(ref_time));
}

#define NOW() (time_delta_str(start_time))





void benchmark_rx_rate(uhd::usrp::multi_usrp::sptr usrp,const std::string& rx_cpu,uhd::rx_streamer::sptr rx_stream,bool random_nsamps,
const boost::posix_time::ptime& start_time, std::atomic<bool>&burst_timer_elapsed, bool elevate_priority,double rx_delay )
{
    // 1. increase thread priority, improve performance.
    // 2. set rx receive buffer;
    // 3. issue stream cmd; and receive
    // 4. terminate stream conditions

    //1.
    if(elevate_priority)
    {
        uhd::set_thread_priority_safe();
    }

    //print receiver motivation and state information;
    std::cout<<boost::format("[%s] Testing receive rate %f Msamps in %u channels") % NOW() 
    % (usrp->get_rx_rate() / 1e6) % (rx_stream->get_num_channels()) <<std::endl;

    //2. receive state variables setting and allocate receive buffer: buffersize should be aligned with specified sample per packet.

        //define receive variables which will be assigned at recv();
    uhd::rx_metadata_t md;
    const size_t max_samps_per_packet= rx_stream->get_max_num_samps();
    std::cout<<max_samps_per_packet<<std::endl;
        //receive buffer is vector<void*> : here we first wrap void* use vector<char> then get void* use &vector.font()


    std::vector<std::vector<char>> channels_buffers(rx_stream->get_num_channels());
    for(int i=0;i<channels_buffers.size();++i) // may use auto, or iterator;
    {
        channels_buffers[i]=std::vector<char>(max_samps_per_packet*uhd::convert::get_bytes_per_item(rx_cpu));
        std::cout<<"buffersize"<<channels_buffers[i].size()<<std::endl;
    }

    std::vector<void*>buffs;

    //write to file section (add this part to analyse IQ, and channel.)

    int fd = open("usrp_samples.dat", O_WRONLY | O_CREAT,0600);

        //assign each channel the same buff space;
    for(size_t ch=0;ch<rx_stream->get_num_channels();ch++)
    {
        buffs.push_back(&(channels_buffers[ch].front()));
    }
    
            //assign iovs;
    for(size_t ch=0;ch<rx_stream->get_num_channels();ch++)
    {
        iovs[ch]={
            .iov_base = &(channels_buffers[ch].front()),
            .iov_len = max_samps_per_packet*uhd::convert::get_bytes_per_item(rx_cpu),
        };
    }
    // default 2 channel
    // buffs.push_back(&buff.front());
    // buffs.push_back(&buff2.front());

    // overflow state;

    bool had_an_overflow=false;

    uhd::time_spec_t last_time;


    const double rate = usrp->get_rx_rate();


    // 3. issue stream cmd (control usrp work state to continuous receiving), host receive data to allocated buffer.

        //issue usrp_work state;
    
    uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    cmd.time_spec=usrp->get_time_now()+uhd::time_spec_t(rx_delay);
    cmd.stream_now=false;
    rx_stream->issue_stream_cmd(cmd); // specify usrp receiver start running time;

    const float burst_pkt_time =
        std::max<float>(0.100f, (2 * max_samps_per_packet / rate));
    float recv_timeout = burst_pkt_time + rx_delay;// the initial recv_timeout;

    bool stop_called=false;

    while (true) {
        // if (burst_timer_elapsed.load(boost::memory_order_relaxed) and not stop_called)
        // {
        if (burst_timer_elapsed and not stop_called) {
            rx_stream->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
            stop_called = true;
            close(fd);
        }
        if (random_nsamps) {
            cmd.num_samps = rand() % max_samps_per_packet;
            rx_stream->issue_stream_cmd(cmd);
        }
        try {
            num_recv_byte=rx_stream->recv(buffs, max_samps_per_packet, md, recv_timeout)
                            * rx_stream->get_num_channels()*8; //I,Q all are float;
            num_rx_samps += num_recv_byte/8;
            recv_timeout = burst_pkt_time;
        } catch (uhd::io_error& e) {
            std::cerr << "[" << NOW() << "] Caught an IO exception. " << std::endl;
            std::cerr << e.what() << std::endl;
            return;
        }

        // handle the error codes
        switch (md.error_code) {
            case uhd::rx_metadata_t::ERROR_CODE_NONE:
                if (had_an_overflow) {
                    had_an_overflow          = false;
                    const long dropped_samps = (md.time_spec - last_time).to_ticks(rate);
                    if (dropped_samps < 0) {
                        std::cerr << "[" << NOW()
                                  << "] Timestamp after overrun recovery "
                                     "ahead of error timestamp! Unable to calculate "
                                     "number of dropped samples."
                                     "(Delta: "
                                  << dropped_samps << " ticks)\n";
                    }
                    num_dropped_samps += std::max<long>(1, dropped_samps);
                }
                if ((burst_timer_elapsed or stop_called) and md.end_of_burst) {
                   
                    return;
                }
                break;

            // ERROR_CODE_OVERFLOW can indicate overflow or sequence error
            case uhd::rx_metadata_t::ERROR_CODE_OVERFLOW:
                last_time       = md.time_spec;
                had_an_overflow = true;
                // check out_of_sequence flag to see if it was a sequence error or
                // overflow
                if (!md.out_of_sequence) {
                    num_overruns++;
                } else {
                    num_seqrx_errors++;
                    std::cerr << "[" << NOW() << "] Detected Rx sequence error."
                              << std::endl;
                }
                break;

            case uhd::rx_metadata_t::ERROR_CODE_LATE_COMMAND:
                std::cerr << "[" << NOW() << "] Receiver error: " << md.strerror()
                          << ", restart streaming..." << std::endl;
                num_late_commands++;
                // Radio core will be in the idle state. Issue stream command to restart
                // streaming.
                cmd.time_spec  = usrp->get_time_now() + uhd::time_spec_t(0.05);
                cmd.stream_now = (buffs.size() == 1);
                rx_stream->issue_stream_cmd(cmd);
                break;

            case uhd::rx_metadata_t::ERROR_CODE_TIMEOUT:
                if (burst_timer_elapsed) {
                    return;
                }
                std::cerr << "[" << NOW() << "] Receiver error: " << md.strerror()
                          << ", continuing..." << std::endl;
                num_timeouts_rx++;
                break;

                // Otherwise, it's an error
            default:
                std::cerr << "[" << NOW() << "] Receiver error: " << md.strerror()
                          << std::endl;
                std::cerr << "[" << NOW() << "] Unexpected error on recv, continuing..."
                          << std::endl;
                break;
        }
        
            // for(size_t ch=0;ch<rx_stream->get_num_channels();ch++)
            // {
            //     outf.write((const char*)&(channels_buffers[ch].front()), max_samps_per_packet*uhd::convert::get_bytes_per_item(rx_cpu));
            // }

             io_uring_prep_writev(sqe, fd, iovs, 2, offset_file);
             offset_file+=num_recv_byte;
             io_uring_submit(&ring);
             sqe = io_uring_get_sqe(&ring);
    }
}

    
    



int UHD_SAFE_MAIN(int argc, char* argv[])
{

    // 0. init liburing IO to speed up write file.
    // 1. parse argv values  to defined tokens
    // 2. open usrp(multiple_usrp pointer) according to args token.
    // 3. configure usrp parameters (data type;fc32... freq..., sample rate, sps...,gain, channeld, sub_dev...)
    // 4. issue usrp_stream in tx or rx thread.
    // 5. close thread, usrp.

    // 0. init liburing IO to speed up write file.
   
    io_uring_queue_init(32, &ring, 0);

    sqe = io_uring_get_sqe(&ring);
    iovs=(struct iovec*)calloc(sizeof(struct iovec),2); // channel num is 2 now;
    offset_file=0;
    //variables: token variables, proram_state_variable;
    std::string args,rx_subdev, rx_otw,rx_cpu,mode, ref, pps,channel_list, rx_channel_list,priority,ant; ////specify usrp dev // stream_args sample data type in the link layer: over_the_wire foramt:  // stream_args sample data type in host desktop: cpu foramt;

    double duration,rx_rate, rx_delay,freq,gain,bw;    // program run time; sample rate, boot delay

    bool random_nsamps=false,elevate_priority=false; //check thread priority

    std::atomic<bool> burst_timer_elapsed(false); //check time duration
    size_t overrun_threshold, underrun_threshold, drop_threshold, seq_threshold; //define packet tx/rx states;


    /**
     * 1. parse_argv values to defined tokens.
     *    use boost program_options define tokens, and assign command line argv to token values.
     *      a. setup program_options; (make sure namespace uses proram_options)
     *      b. define token rules;
     *      c. parse command line argument( argv) to defined tokens accoring to token rules;
     */

    //a. setup program_options; (make sure namespace uses proram_options)  
        //here is tutorial: https://www.boost.org/doc/libs/1_58_0/doc/html/program_options/tutorial.html
    po::options_description desc("Allowed argument");

    //b. define token argument rules;  ( which is similar to python argparse.add_argument)

    desc.add_options()
        ("help","help message")
        ("args,a",po::value<std::string>(&args)->default_value(""),"single uhd device args")
        ("duration",po::value<double>(&duration)->default_value(10)," the program run time")
        ("rx_subdev",po::value<std::string>(&rx_subdev),"sepcify the subdev for RX.")
        ("rx_rate",po::value<double>(&rx_rate),"specify rx sample rate")
        ("freq",po::value<double>(&freq)->default_value(2.0e9),"specify rx center frequency")
        ("gain",po::value<double>(&gain)->default_value(0.0),"specify RF gain")
        ("ant", po::value<std::string>(&ant)->default_value("RX2"), "antenna selection")
        ("bw", po::value<double>(&bw)->default_value(100e6), "analog frontend filter bandwidth in Hz")
        ("rx_otw",po::value<std::string>(&rx_otw)->default_value("sc16"),"specify the over-the-wire sample mode for RX")
        ("rx_cpu",po::value<std::string>(&rx_cpu)->default_value("fc32"),"specify cpu/host sample data type for RX")
        ("ref",po::value<std::string>(&ref),"clock reference(internal,external,mimo,gpsdo")
        ("pps", po::value<std::string>(&pps), "PPS source (internal, external, mimo, gpsdo)")
        ("random", "Run with random values of samples in send() and recv() to stress-test the I/O.")
        ("channels,c",po::value<std::string>(&channel_list)->default_value("0"),"which channel(s) to use (specify \"0\", \"1\", \"0,1\", etc)")
        ("rx_channels", po::value<std::string>(&rx_channel_list), "which RX channel(s) to use (specify \"0\", \"1\", \"0,1\", etc)")
        ("overrun-threshold", po::value<size_t>(&overrun_threshold),
         "Number of overruns (O) which will declare the benchmark a failure.")
        ("underrun-threshold", po::value<size_t>(&underrun_threshold),
         "Number of underruns (U) which will declare the benchmark a failure.")
        ("drop-threshold", po::value<size_t>(&drop_threshold),
         "Number of dropped packets (D) which will declare the benchmark a failure.")
        ("seq-threshold", po::value<size_t>(&seq_threshold),
         "Number of dropped packets (D) which will declare the benchmark a failure.")
         // NOTE: delay to wait tx to fill buffer completely.

        ("rx_delay", po::value<double>(&rx_delay)->default_value(0.05), "delay before starting RX in seconds")
        ("priority", po::value<std::string>(&priority)->default_value("high"), "thread priority (high, normal)")
        ;

    // c. parse arguments.
    
    po::variables_map vm; //store <options,value> pair;
    po::store(po::parse_command_line(argc,argv,desc),vm);
    po::notify(vm);

    //test/help  message 
    if(vm.count("help") or (vm.count("rx_rate")+vm.count("tx_rate"))==0)
    {
        std::cout << boost::format("UHD Benchmark Rate %s") % desc << std::endl;
        std::cout << "    Specify --rx_rate for a receive-only test.\n"
                     "    Specify --tx_rate for a transmit-only test.\n"
                     "    Specify both options for a full-duplex test.\n"
                  << std::endl;
        return ~0;
    }

    if (priority == "high") {
        uhd::set_thread_priority_safe();
        elevate_priority = true;
    }

    // Random number of samples?
    if (vm.count("random")) {
        std::cout << "Using random number of samples in send() and recv() calls."
                  << std::endl;
        random_nsamps = true;
    }

    
    
    /**
     * 2 find and open usrp dev
     * 
     *  find device addr;
     *  check usrp1;
     *  
     *  create usrp dev now;
     * 
     */

    std::cout <<"-------------create usrp dev--------------"<< std::endl;

    //find device addr;
    uhd::device_addrs_t device_addrs=uhd::device::find(args,uhd::device::USRP);  //usrp address_t  Ex, to find a usrp2: my_dev_addr["addr"] = [resolvable_hostname_or_ip]
    std::cout << "device addr:"<<std::endl;
    //usrp1 check ; igore

    // create usrp dev now;
    boost::posix_time::ptime start_time(boost::posix_time::microsec_clock::local_time());
    std::cout<<boost::format("[%s] Creating the usrp device with: %s...")%NOW()%args<<std::endl;

    uhd::usrp::multi_usrp::sptr usrp=uhd::usrp::multi_usrp::make(args); //make make new multi_impl and call make function in uhd device.cpp

    
    //alwasy select the sub dev first; the channel mapping affect other things;
    if (vm.count("rx_subdev")) {
        usrp->set_rx_subdev_spec(rx_subdev);
    }


    std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;
    int num_mboards = usrp->get_num_mboards();




    /**
     * 3. configure usrp parameters
     * time_source, clock_source,channels, rate,freq,gain;
     */

    //ignore time and clock_source for now: ref,pps.

    //set tx and rx channels;

    /**
     * because struct stream_args_t owns std::vector<size_t> channels feature, we shall split channels(string type) to chars first;
     */
        
    std::vector<std::string> channel_strings;
    std::vector<size_t> rx_channel_nums;
    if(vm.count("rx_rate"))
    {
        if(!vm.count("rx_channels"))
        {
            rx_channel_list=channel_list;
        }
        //split channel to channel nums;
        boost::split(channel_strings, rx_channel_list, boost::is_any_of("\"',"));

        for(size_t ch=0;ch<channel_strings.size();++ch)
        {
            size_t chan=std::stoul(channel_strings[ch]);
            //check channel limit;
            if (chan >= usrp->get_rx_num_channels()) {
                throw std::runtime_error("Invalid channel(s) specified.");
            } else {
                rx_channel_nums.push_back(chan);
            }
        }


    }




    //set time now(); usrp-rio time stamp //ignore mimo;
    if(rx_channel_nums.size()>1)
    {
        usrp->set_time_unknown_pps(uhd::time_spec_t(0.0));
    }
    else
    {
        usrp->set_time_now(0.0);
    }
     
     // create rx or tx  stream thread ;
    boost::thread_group thread_group;

    // set the center frequency
    if (vm.count("freq")) { // with default of 0.0 this will always be true
        std::cout << boost::format("Setting RX Freq: %f MHz...") % (freq / 1e6)
                  << std::endl;
       
        uhd::tune_request_t tune_request(freq, 0);

        for(size_t ch=0;ch<rx_channel_nums.size();++ch)
        {
            usrp->set_rx_freq(tune_request, ch);
             std::cout << boost::format("Actual RX Freq: %f MHz...")
                         % (usrp->get_rx_freq(ch) / 1e6)
                  << std::endl
                  << std::endl;
        }

       
    }

    // set the rf gain
    if (vm.count("gain")) {
        std::cout << boost::format("Setting RX Gain: %f dB...") % gain << std::endl;
         for(size_t ch=0;ch<rx_channel_nums.size();++ch)
        {
            usrp->set_rx_gain(gain, ch);
             std::cout << boost::format("Actual RX Gain: %f dB...")
                         % usrp->get_rx_gain(ch)
                  << std::endl
                  << std::endl;
        }
        
    }

    // set the IF filter bandwidth
    if (vm.count("bw")) {
        std::cout << boost::format("Setting RX Bandwidth: %f MHz...") % (bw / 1e6)
                  << std::endl;
        for(size_t ch=0;ch<rx_channel_nums.size();++ch)
        {
            usrp->set_rx_bandwidth(bw, ch);
            std::cout << boost::format("Actual RX Bandwidth: %f MHz...")
                         % (usrp->get_rx_bandwidth(ch) / 1e6)
                  << std::endl
                  << std::endl;
        }

    }

    // set the antenna
    if (vm.count("ant"))
    {
        std::cout << boost::format("Setting RX Bandwidth: %f MHz...") % (bw / 1e6)
                  << std::endl;
        for(size_t ch=0;ch<rx_channel_nums.size();++ch)
        {
            usrp->set_rx_antenna(ant, ch);
            std::cout << boost::format("Actual RX Antenna: %s ...")
                         % (usrp->get_rx_antenna(ch))
                  << std::endl
                  << std::endl;
        }
    }
        

    if(vm.count("rx_rate")) //spaw rx thread;
    {
        usrp->set_rx_rate(rx_rate);
        //todo: add freq , gain, antenna;

        uhd::stream_args_t stream_args(rx_cpu,rx_otw);
        stream_args.channels=rx_channel_nums;

        //acquire rx_stream shared pointer;
        uhd::rx_streamer::sptr rx_stream=usrp->get_rx_stream(stream_args);

        auto rx_thread=thread_group.create_thread([=,&burst_timer_elapsed]{
            benchmark_rx_rate(
                usrp, rx_cpu, rx_stream, random_nsamps, start_time, burst_timer_elapsed, elevate_priority, rx_delay);
        });

    }


    const int64_t secs = int64_t(duration);
    const int64_t usecs = int64_t((duration - secs) * 1e6);
    std::this_thread::sleep_for(
        std::chrono::seconds(secs) + std::chrono::microseconds(usecs));

    burst_timer_elapsed=true;
    thread_group.join_all();
    std::cout << "[" << NOW() << "] Benchmark complete." << std::endl << std::endl;


    std::cout << std::endl
              << boost::format("Benchmark rate summary:\n"
                               "  Num received samples:     %u\n"
                               "  Num dropped samples:      %u\n"
                               "  Num overruns detected:    %u\n"
                               "  Num transmitted samples:  %u\n"
                               "  Num sequence errors (Tx): %u\n"
                               "  Num sequence errors (Rx): %u\n"
                               "  Num underruns detected:   %u\n"
                               "  Num late commands:        %u\n"
                               "  Num timeouts (Tx):        %u\n"
                               "  Num timeouts (Rx):        %u\n")
                     % num_rx_samps % num_dropped_samps % num_overruns % num_tx_samps
                     % num_seq_errors % num_seqrx_errors % num_underruns
                     % num_late_commands % num_timeouts_tx % num_timeouts_rx
              << std::endl;
    // finished
    std::cout << std::endl << "Done!" << std::endl << std::endl;

    
    return EXIT_SUCCESS;

    
}
