

#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>
#include<uhd/convert.hpp>  
#include<boost/algorithm/string.hpp>
#include<boost/date_time/posix_time/posix_time.hpp>
#include<boost/format.hpp>
#include<boost/program_options.hpp>
#include<boost/thread/thread.hpp>
#include <chrono>
#include <complex>
#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>

/** uring io*/
extern "C"
{
#include <liburing.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <sys/time.h>
#include <string.h>
}

namespace po = boost::program_options;


struct iovec* iovs;
struct io_uring ring;

struct io_uring_sqe *sqe;
struct io_uring_cqe *cqe;
off_t offset_file;

uhd::usrp::multi_usrp::sptr usrp;


static bool stop_signal_called = false;
boost::posix_time::ptime start_time; // the time when usrp is created;

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


void sig_int_handler(int)
{
    stop_signal_called = true;
}


void send_from_file(
    uhd::tx_streamer::sptr tx_stream, const std::string& file,size_t spb, size_t sample_size, size_t num_channels)
{
    uhd::set_thread_priority_safe();
    double delay=0.5;
    uhd::tx_metadata_t md;
    md.start_of_burst = false;
    md.end_of_burst   = false;
    md.has_time_spec=true;
    md.time_spec =usrp->get_time_now() + uhd::time_spec_t(delay);
    size_t buff_size=spb*sample_size*num_channels;
    std::vector<void*>buffs;
    std::vector<std::vector<char>> channels_buffers(num_channels);
    for(size_t ch=0;ch<num_channels;++ch) // may use auto, or iterator;
    {
        channels_buffers[ch]=std::vector<char>(spb*sample_size);
        buffs.push_back(&(channels_buffers[ch].front()));   //modify this ...;
        iovs[ch]={
            .iov_base = &(channels_buffers[ch].front()),
            .iov_len = channels_buffers[ch].size(),
        };
        std::cout<<"buffersize asserts near mtu 8000, actual value is "<<channels_buffers[ch].size()<<std::endl;
    }

    std::cout<<boost::format("max num of send samples per frame:%d\n")% tx_stream->get_max_num_samps()<<std::endl;

    int fd=open(file.c_str(),O_RDONLY,0644);
    if(fd<0)
    {
        std::cout<<"open failed"<<std::endl;
        exit(-1);
    }

    std::cout<<boost::format("[%s] Testing transmit samples with %d Msps from file in %u channels") % NOW() 
    % (usrp->get_tx_rate() / 1e6) % (tx_stream->get_num_channels()) <<std::endl;

    // loop until the entire file has been read
    int ret=0;
    while (not md.end_of_burst and not stop_signal_called) {
        
        io_uring_prep_readv(sqe, fd, iovs,num_channels, offset_file);// modify
        offset_file+=buff_size ; //modify  * num_channels
        io_uring_submit(&ring);   //ret=io_uring_submit(&ring);
        // if(ret<0)
        // {
        //     std::cout<<"submit failed"<<std::endl;
        //     exit(-1);
        // }
        io_uring_wait_cqe(&ring,&cqe);
        // if(ret<0)
        // {
        //     std::cout<<"wait failed"<<std::endl;
        //     exit(-1);
        // }
        // if(cqe->res<=0)
        // {
        //     std::cout<<"End of file"<<std::endl;
        //     md.end_of_burst = true;

        //     tx_stream->send(buffs, spb, md);
        //     break;
        // }
        io_uring_cqe_seen(&ring, cqe);
        sqe = io_uring_get_sqe(&ring);
        //assert(sqe);
        

        tx_stream->send(buffs, spb, md);
        md.has_time_spec=false;
    }
    io_uring_queue_exit(&ring);
    ret= close(fd);
    if(ret<0)
    {
        std::cout<<"close failed"<<std::endl;
    }
}

int UHD_SAFE_MAIN(int argc, char* argv[])
{

    io_uring_queue_init(32, &ring, 0);

    sqe = io_uring_get_sqe(&ring);
    iovs=(struct iovec*)calloc(sizeof(struct iovec),2); // channel num is 1 now; we set it to 16 for further.
    offset_file=0;

    // variables to be set by po
    std::string args, file, ant, ref,cpufmt, wirefmt, channels;
    size_t spb,num_channels;
    double rate, freq, gain, bw, delay;
    std::vector<std::string> channel_strings;
    std::vector<size_t> channel_nums;
    // setup the program options
    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
        ("help", "help message")
        ("args", po::value<std::string>(&args)->default_value(""), "multi uhd device address args")
        ("file", po::value<std::string>(&file)->default_value("usrp_samples.dat"), "name of the file to read binary samples from")
        ("spb", po::value<size_t>(&spb)->default_value(510976), "samples per buffer")
        ("rate", po::value<double>(&rate), "rate of outgoing samples")
        ("freq", po::value<double>(&freq), "RF center frequency in Hz")
        ("gain", po::value<double>(&gain), "gain for the RF chain")
        ("ant", po::value<std::string>(&ant), "antenna selection")
        ("bw", po::value<double>(&bw)->default_value(100e6), "analog frontend filter bandwidth in Hz")
        ("ref", po::value<std::string>(&ref)->default_value("internal"), "reference source (internal, external, mimo)")
        ("cpufmt", po::value<std::string>(&cpufmt)->default_value("sc16"), "sample type: sc16,fc32")
        ("wirefmt", po::value<std::string>(&wirefmt)->default_value("sc16"), "wire format (sc8 or sc16)")
        ("delay", po::value<double>(&delay)->default_value(2.0), "specify a delay between repeated transmission of file (in seconds)")
        ("channels", po::value<std::string>(&channels)->default_value("0"), "which channels to use")
    ;
    // clang-format on
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    // print the help message
    if (vm.count("help")) {
        std::cout << boost::format("UHD TX samples from file %s") % desc << std::endl;
        return ~0;
    }



    // create a usrp device
    start_time=boost::posix_time::microsec_clock::local_time();
    std::cout<<boost::format("[%s] Creating the usrp device with: %s...")%NOW()%args<<std::endl;
    usrp = uhd::usrp::multi_usrp::make(args);

    // Lock mboard clocks
    if (vm.count("ref")) {
        usrp->set_clock_source(ref);
    }


    std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;


    boost::split(channel_strings, channels, boost::is_any_of("\"',"));

    for(size_t ch=0;ch<channel_strings.size();++ch)
    {

        size_t chan=std::stoul(channel_strings[ch]);
        //check channel limit;
        if (chan >= usrp->get_tx_num_channels()) {
            throw std::runtime_error("Invalid channel(s) specified.");
        } else {
            channel_nums.push_back(chan);
        }
    }
    num_channels=channel_nums.size();
    uhd::tune_request_t tune_request(freq, 0);

    for (auto ch: channel_nums)
    {
        // set IQ rate
        usrp->set_tx_rate(rate,ch);
        std::cout << boost::format("Ch:%d Actual TX Rate: %f Msps...") %ch % (usrp->get_tx_rate() / 1e6)<< std::endl;
        // set the center frequency
        usrp->set_tx_freq(tune_request,ch);
        std::cout << boost::format("Ch:%d Actual TX Freq: %f MHz...") %ch % (usrp->get_tx_freq() / 1e6)<< std::endl;
        // set the rf gain
        usrp->set_tx_gain(gain,ch);
        std::cout << boost::format("Ch:%d Actual TX Gain: %f dB...") %ch % usrp->get_tx_gain()<< std::endl;
        // set the analog frontend filter bandwidth
        usrp->set_tx_bandwidth(bw,ch);
        std::cout << boost::format("Ch:%d Actual TX Bandwidth: %f MHz...") %ch % (usrp->get_tx_bandwidth() / 1e6)<< std::endl;
        // set the antenna
        usrp->set_tx_antenna(ant,ch);
        std::cout << boost::format("Ch:%d Actual TX Antenna: %s...") %ch % usrp->get_tx_antenna()<< std::endl;
        // set tx_buff


    }
    if(num_channels>1)
    {
        usrp->set_time_unknown_pps(uhd::time_spec_t(0.0));
        
    }
    else
    {
        usrp->set_time_now(0.0);
    }
    // allow for some setup time:
    std::this_thread::sleep_for(std::chrono::seconds(1));
    // set sigint if user wants to exit()
    std::signal(SIGINT, &sig_int_handler);
    std::cout << "Press Ctrl + C to stop streaming..." << std::endl;






    uhd::stream_args_t stream_args(cpufmt, wirefmt);
    stream_args.channels             = channel_nums;
    uhd::tx_streamer::sptr tx_stream = usrp->get_tx_stream(stream_args);

    // send from file



    send_from_file(tx_stream, file, spb, uhd::convert::get_bytes_per_item(cpufmt) ,num_channels);




    // finished
    std::this_thread::sleep_for(std::chrono::seconds(1));
    free(iovs);
    std::cout << "[" << NOW() << "] tsff complete." << std::endl << std::endl;
    std::cout << std::endl << "Done!" << std::endl << std::endl;
    
    return EXIT_SUCCESS;
}
