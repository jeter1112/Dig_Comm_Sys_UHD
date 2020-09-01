
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
unsigned long long num_tx_samps      = 0;
unsigned long long num_dropped_samps = 0;
unsigned long long num_seq_errors    = 0;
unsigned long long num_late_commands = 0;
unsigned long long num_timeouts_tx   = 0;

struct iovec* iovs;
struct io_uring ring;

struct io_uring_sqe *sqe;
struct io_uring_cqe *cqe;
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





void benchmark_tx_rate(uhd::usrp::multi_usrp::sptr usrp,const std::string& tx_cpu,uhd::tx_streamer::sptr tx_stream,bool random_nsamps,
const boost::posix_time::ptime& start_time, std::atomic<bool>&burst_timer_elapsed, bool elevate_priority,double tx_delay,size_t spb)
{
    // 1. increase thread priority
    // 2. set tx send buffer;
    // 3. issue stream cmd; and send
    // 4. terminate stream conditions

    //1.
    if(elevate_priority)
    {
        uhd::set_thread_priority_safe();
    }

    //print tx state information;
    std::cout<<boost::format("[%s] Testing send rate %f Msamps in %u channels") % NOW() 
    % (usrp->get_tx_rate() / 1e6) % (tx_stream->get_num_channels()) <<std::endl;

    //2. tx state variables setting and allocate tx buffer: buffersize should be aligned with specified sample per packet.

        //define receive variables which will be assigned at send();
    
     std::cout<<"spb:"<<spb<<std::endl;
        //send buffer is vector<void*> : here we first wrap void* use vector<char> then get void* use &vector.font()


    std::vector<std::vector<char>> channels_buffers(tx_stream->get_num_channels());
    for(size_t i=0;i<channels_buffers.size();++i) // may use auto, or iterator;
    {
        channels_buffers[i]=std::vector<char>(spb*uhd::convert::get_bytes_per_item(tx_cpu));
        std::cout<<"buffersize"<<channels_buffers[i].size()<<std::endl;
    }

    std::vector<void*>buffs;

    //write to file section (add this part to analyse IQ, and channel.)

    int fd = open("usrp_samples.dat", O_RDONLY,0644);

        //assign each channel the same buff space;
    for(size_t ch=0;ch<tx_stream->get_num_channels();ch++)
    {
        buffs.push_back(&(channels_buffers[ch].front()));
    }
    
            //assign iovs;
    for(size_t ch=0;ch<tx_stream->get_num_channels();ch++)
    {
        iovs[ch]={
            .iov_base = &(channels_buffers[ch].front()),
            .iov_len = spb*uhd::convert::get_bytes_per_item(tx_cpu),
        };
    }



    uhd::time_spec_t last_time;



    // 3. issue stream cmd (control usrp work state to continuous receiving), host receive data to allocated buffer.

        //issue usrp_work state;
    uhd::tx_metadata_t md;
    md.has_time_spec = (buffs.size() != 1);
    md.time_spec     = usrp->get_time_now() + uhd::time_spec_t(tx_delay);

    const float timeout = 1.0;


    while (not burst_timer_elapsed) 
    {

        io_uring_prep_readv(sqe, fd, iovs, 1, offset_file);
        offset_file+=2*iovs[0].iov_len;
        io_uring_submit(&ring);
        io_uring_wait_cqe(&ring, &cqe);
        sqe = io_uring_get_sqe(&ring);
        assert(sqe);
        tx_stream->send(buffs,spb, md, timeout);
        // const size_t num_tx_samps_sent_now =
        //     tx_stream->send(buffs,spb, md, timeout) * tx_stream->get_num_channels();
        // num_tx_samps += num_tx_samps_sent_now;
        // if (num_tx_samps_sent_now == 0) {
        //     num_timeouts_tx++;
        //     if ((num_timeouts_tx % 10000) == 1) {
        //         std::cerr << "[" << NOW() << "] Tx timeouts: " << num_timeouts_tx
        //                     << std::endl;
        //     }
        // }
        md.has_time_spec = false;
    }


    // send a mini EOB packet
    md.end_of_burst = true;
    tx_stream->send(buffs, 0, md);


    
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
    assert(sqe);
    iovs=(struct iovec*)calloc(sizeof(struct iovec),2); // channel num is 2 now;
    offset_file=0;
    //variables: token variables, proram_state_variable;
    std::string args,tx_subdev, tx_otw,tx_cpu,mode, ref, pps,channel_list, tx_channel_list,priority,ant; ////specify usrp dev // stream_args sample data type in the link layer: over_the_wire foramt:  // stream_args sample data type in host desktop: cpu foramt;

    double duration,tx_rate, tx_delay,freq,gain,bw;    // program run time; sample rate, boot delay

    bool random_nsamps=false,elevate_priority=false; //check thread priority

    std::atomic<bool> burst_timer_elapsed(false); //check time duration
    size_t spb, overrun_threshold, underrun_threshold, drop_threshold, seq_threshold; //define samples_per_buffer, packet tx/rx states;


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
        ("freq",po::value<double>(&freq)->default_value(2.0e9),"specify tx center frequency")
        ("gain",po::value<double>(&gain)->default_value(0.0),"specify RF gain")
        ("ant", po::value<std::string>(&ant)->default_value("TX/RX"), "antenna selection")
        ("bw", po::value<double>(&bw)->default_value(100e6), "analog frontend filter bandwidth in Hz")
        ("spb", po::value<size_t>(&spb)->default_value(4096),
         "samples per send buffer")
        ("tx_subdev",po::value<std::string>(&tx_subdev), "tx subdev")
        ("tx_rate",po::value<double>(&tx_rate), "tx sample rate")
        ("tx_otw",po::value<std::string>(&tx_otw)->default_value("sc16"),"specify the over-the-wire sample mode for TX")
        ("tx_cpu",po::value<std::string>(&tx_cpu)->default_value("sc16"),"specify cpu/host sample data type for TX")
        ("ref",po::value<std::string>(&ref),"clock reference(internal,external,mimo,gpsdo")
        ("pps", po::value<std::string>(&pps), "PPS source (internal, external, mimo, gpsdo)")
        ("channels,c",po::value<std::string>(&channel_list)->default_value("0"),"which channel(s) to use (specify \"0\", \"1\", \"0,1\", etc)")
        ("overrun-threshold", po::value<size_t>(&overrun_threshold),
         "Number of overruns (O) which will declare the benchmark a failure.")
        ("underrun-threshold", po::value<size_t>(&underrun_threshold),
         "Number of underruns (U) which will declare the benchmark a failure.")
        ("drop-threshold", po::value<size_t>(&drop_threshold),
         "Number of dropped packets (D) which will declare the benchmark a failure.")
        ("seq-threshold", po::value<size_t>(&seq_threshold),
         "Number of dropped packets (D) which will declare the benchmark a failure.")
         // NOTE: delay to wait tx to fill buffer completely.
        ("priority", po::value<std::string>(&priority)->default_value("high"), "thread priority (high, normal)")
        ("tx_delay",po::value<double>(&tx_delay)->default_value(0.5f), "tx delay")
        ;

    // c. parse arguments.
    
    po::variables_map vm; //store <options,value> pair;
    po::store(po::parse_command_line(argc,argv,desc),vm);
    po::notify(vm);

    //test/help  message 
    if(vm.count("help") or (vm.count("tx_rate"))==0)
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
    if (vm.count("tx_subdev")) {
        usrp->set_tx_subdev_spec(tx_subdev);
    }

    std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;




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
    std::vector<size_t> tx_channel_nums;
    if(vm.count("tx_rate"))
    {
        if(!vm.count("tx_channels"))
        {
            tx_channel_list=channel_list;
        }
        //split channel to channel nums;
        boost::split(channel_strings, tx_channel_list, boost::is_any_of("\"',"));

        for(size_t ch=0;ch<channel_strings.size();++ch)
        {
            size_t chan=std::stoul(channel_strings[ch]);
            //check channel limit;
            if (chan >= usrp->get_tx_num_channels()) {
                throw std::runtime_error("Invalid channel(s) specified.");
            } else {
                tx_channel_nums.push_back(chan);
            }
        }


    }




    //set time now(); usrp-rio time stamp //ignore mimo;
    if(tx_channel_nums.size()>1)
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
        std::cout << boost::format("Setting TX Freq: %f MHz...") % (freq / 1e6)
                  << std::endl;
       
        uhd::tune_request_t tune_request(freq, 0);

        for(size_t ch=0;ch<tx_channel_nums.size();++ch)
        {
            usrp->set_tx_freq(tune_request, ch);
             std::cout << boost::format("Actual TX Freq: %f MHz...")
                         % (usrp->get_tx_freq(ch) / 1e6)
                  << std::endl
                  << std::endl;
        }

       
    }

    // set the rf gain
    if (vm.count("gain")) {
        std::cout << boost::format("Setting TX Gain: %f dB...") % gain << std::endl;
         for(size_t ch=0;ch<tx_channel_nums.size();++ch)
        {
            usrp->set_tx_gain(gain, ch);
             std::cout << boost::format("Actual TX Gain: %f dB...")
                         % usrp->get_tx_gain(ch)
                  << std::endl
                  << std::endl;
        }
        
    }

    // set the IF filter bandwidth
    if (vm.count("bw")) {
        std::cout << boost::format("Setting TX Bandwidth: %f MHz...") % (bw / 1e6)
                  << std::endl;
        for(size_t ch=0;ch<tx_channel_nums.size();++ch)
        {
            usrp->set_tx_bandwidth(bw, ch);
            std::cout << boost::format("Actual TX Bandwidth: %f MHz...")
                         % (usrp->get_tx_bandwidth(ch) / 1e6)
                  << std::endl
                  << std::endl;
        }

    }

    // set the antenna
    if (vm.count("ant"))
    {
        std::cout << boost::format("Setting TX Bandwidth: %f MHz...") % (bw / 1e6)
                  << std::endl;
        for(size_t ch=0;ch<tx_channel_nums.size();++ch)
        {
            usrp->set_tx_antenna(ant, ch);
            std::cout << boost::format("Actual TX Antenna: %s ...")
                         % (usrp->get_tx_antenna(ch))
                  << std::endl
                  << std::endl;
        }
    }
        

    if(vm.count("tx_rate")) //spaw tx thread;
    {
        usrp->set_tx_rate(tx_rate);
        //todo: add freq , gain, antenna;

        uhd::stream_args_t stream_args(tx_cpu,tx_otw);
        stream_args.channels=tx_channel_nums;

        //acquire tx_stream shared pointer;
        uhd::tx_streamer::sptr tx_stream=usrp->get_tx_stream(stream_args);

        auto tx_thread=thread_group.create_thread([=,&burst_timer_elapsed]{
            benchmark_tx_rate(
                usrp, tx_cpu, tx_stream, random_nsamps, start_time, burst_timer_elapsed, elevate_priority, tx_delay,spb);
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
                     % num_tx_samps % num_dropped_samps % num_overruns % num_tx_samps
                     % num_seq_errors % num_seq_errors % num_underruns
                     % num_late_commands % num_timeouts_tx % num_timeouts_tx
              << std::endl;
    // finished
    std::cout << std::endl << "Done!" << std::endl << std::endl;

    
    return EXIT_SUCCESS;

    
}
