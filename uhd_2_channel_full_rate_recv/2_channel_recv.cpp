/***
 * 
 * RX 200Msps write to file for multi-channel usrp-rio.
 * 
 * author: Li Jie
 * 
 * module: recv_thread, and write_file thread;
 */


// import C liburing.h to provide aync IO 
// and low level write file method(file descriptor) to remove buffering of high level method like fwrite or ofstream. 
// and malloc to allocate large buffer(allocate once in the program).

extern "C"
{
    #include<liburing.h>
    #include<stdlib.h>
    #include<unistd.h>
    #include<stdio.h>
    #include<fcntl.h>
    #include<malloc.h>
}
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

// domain model: RX write received data(transport to host via ethernet) to host buffer. host write buffer to file.

// naive solution:
    // loop 
    //     Rx write data(1996 samples) to buffer;
    //     host write buffer to file;
    // end
// issues:
// 1. write to file is an  IO operation. The well known knowledge is that IO speed for aligned data size(num of PAGE_SIZE) is fast.
// 2. we find that probability of 0.1 or less, the Rx write operation is fast then host write to file operation. Maybe because IO system call is interrupted.

// strategies to solve issues:
// 1. PG_SIZE is 4096B, 1996 samples(1996*8 byte) times 128 is multiple of 4096. So we allocate a buffer chunk for 128 times RX write, then host write to file.
// 2. Normal case SDD is faster than RX write, to reduce error, we add redundancy. we allocate a buffer with RED_NUM buffer chunks as (1) strategy.
//     issues to solve:
//      a. SDD should not write faster than RX.
//      b. check that your RED_NUM works.
//     solve:
//      synchronization problem: bounded buffer read and writer, and lock.

// activity model:
//  1. RX init
//  2. init shared buffer and lock
//  3. RX thread
//  4. file thread

// 1. RX init procedure
//  find and creat usrp, configure channels,sample rate, freq,gain,antenna.

//  2 init shared buffer and lock
// allocate buffers contain RED_NUM buffer chunks.
//initally, set all buffer chunks lock to only RX write available

//  3. RX thread
//  init write address to the begining of the begining of first buffer chunk.
//
//
//  check buffer chunk is RX write availble)
//  write data to buffer chunk, update write address. if buffer chunk is full, set chunk is file write available. address to next buffer chunk.

//  4. file thread
//  init write address to the begining of the begining of first buffer chunk.
//
//
//  wait until chunk is file write available.
//  write buffer chunk to file.
//  update write address


//  data model:
//  buffer: continue memory buffer is best here, size is (sizeof(chunk)*RED_NUM)
//  locks:  because  the buffer chunk is aligned continuos at buffer. just allocate bool array, array size is RED_NUM. (1 is RX write, 0 is file write).
//  write address:  we set the start address to buffer start address(also the first chunk). and an offset. alocate offsets for RX and file.
//  



namespace po=boost::program_options; // use program_options namespaces;




//data model variable declare;

size_t buff_chunk_size;// buffer chunk size (in byte)
const size_t buff_chunks_num=8;// the number of buff_chunks;
char* buffer;           // buffer start address;
size_t buffer_size; //  buffer size (in byte)
int buff_locks[buff_chunks_num];//  locks array, 1 for RX, 0 for file;
char* start_addr;       //start_addr of RX and file; //eaual to buffer address.
size_t offset_rx_chunks;  // offset address of rx relative to start address.
size_t offset_file_chunks; // offset address of file in unit of buffer chunk.
size_t num_recv_per_chunk;  // align chunk size to multiple PAGE SIZE(4096 B).

// variables to init and configure usrp para;

std::string args,rx_subdev, rx_otw,rx_cpu,mode, ref, pps,channel_list, rx_channel_list,priority,ant; ////specify usrp dev // stream_args sample data type in the link layer: over_the_wire foramt:  // stream_args sample data type in host desktop: cpu foramt;
double duration,rx_rate, rx_delay,freq,gain,bw;    // program run time; sample rate, boot delay
bool elevate_priority=false; //check thread priority
size_t channel_nums;// num of specified channels
size_t max_samps_per_packet; //sample num in a packet;
size_t sample_size;// in byte;

uhd::usrp::multi_usrp::sptr usrp; //usrp poiner; // conf usrp;
uhd::rx_streamer::sptr rx_stream; //userp_stream pointer // issue stream, recv;

//variables for file;
////we may not use liburing here.
// struct iovec* iovs;
// struct io_uring ring;
// struct io_uring_sqe *sqe;
off_t off_file;  //file offset when call write();
int fd; 
//variables for threads;
boost::thread_group thread_group;
std::atomic<bool> burst_timer_elapsed(false); //check time duration


//variables for parse_command_line;
po::options_description desc("Allowed argument");
po::variables_map vm; //store <options,value> pair;


//variable and routines for usrp transmission statistics;
unsigned long long num_overruns      = 0;
unsigned long long num_rx_samps      = 0;
unsigned long long num_dropped_samps = 0;
unsigned long long num_seq_errors    = 0;
unsigned long long num_seqrx_errors  = 0; // "D"s
unsigned long long num_late_commands = 0;
unsigned long long num_timeouts_rx   = 0;
boost::posix_time::ptime start_time; // the time when usrp is created;

size_t num_buff_chunk_writes=0; //number of chunks RX write;
size_t num_buff_chunk_reads=0;  //number of chunks file write;

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


//declare the RX thread and file thread main routine.
void Rx_recv_to_buffer();
void write_buffer_to_file();


static void parse_cmd_line_arguments(int argc, char* argv[]);
static void init_and_conf_usrp();
static void init_file();
static void init_buffer_and_locks();




int UHD_SAFE_MAIN(int argc, char* argv[])
{
   

    parse_cmd_line_arguments(argc, argv);

    init_and_conf_usrp();
    init_buffer_and_locks();// this part is following the init usrp part to determine the sample data type size;


    auto rx_thread=thread_group.create_thread([&]{   // change varibale value by reference
            Rx_recv_to_buffer();
     });

    auto file_thread=thread_group.create_thread([&]{
        write_buffer_to_file();
    });

    //終止線程
    const int64_t secs = int64_t(duration);
    const int64_t usecs = int64_t((duration - secs) * 1e6);
    std::this_thread::sleep_for(
        std::chrono::seconds(secs) + std::chrono::microseconds(usecs));

    burst_timer_elapsed=true;
    std::cout<<"before join"<<std::endl;
    thread_group.join_all();
    std::cout<<"After join"<<std::endl;
    
    std::cout<<"reads:"<<num_buff_chunk_reads<<std::endl;
    std::cout<<"writes:"<<num_buff_chunk_writes<<std::endl;

    for(size_t chn=0;chn<buff_chunks_num;chn++)
    {
        std::cout<<boost::format("{%d,%d}")%chn %buff_locks[chn]<<std::endl;
    }
    free(buffer);
    //print summary statistics;
    return 0;
}


static void parse_cmd_line_arguments(int argc, char* argv[])
{
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
         // NOTE: delay to wait tx to fill buffer completely.
        ("rx_delay", po::value<double>(&rx_delay)->default_value(0.05), "delay before starting RX in seconds")
        ("priority", po::value<std::string>(&priority)->default_value("high"), "thread priority (high, normal)")
        ;

    // c. parse arguments.
    
    po::store(po::parse_command_line(argc,argv,desc),vm);
    po::notify(vm);

    //test/help  message 
    if(vm.count("help") or (vm.count("rx_rate")==0))
    {
        std::cout << boost::format("UHD Benchmark Rate %s") % desc << std::endl;
        std::cout << "    Specify --rx_rate for a receive-only test.\n"
                     "    Specify --tx_rate for a transmit-only test.\n"
                     "    Specify both options for a full-duplex test.\n"
                  << std::endl;
        exit(-1);
    }

    if (priority == "high") {
        uhd::set_thread_priority_safe();
        elevate_priority = true;
    }

    std::cout<<"/****************************************************/\n"
    "parse_cmd_line_arguments\n\n"<<std::endl;
}


static void init_and_conf_usrp()
{
    /**
     * 1. find and open usrp dev
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
    start_time=boost::posix_time::microsec_clock::local_time();  //set up start_time for print time stamp;
    std::cout<<boost::format("[%s] Creating the usrp device with: %s...")%NOW()%args<<std::endl;

    usrp=uhd::usrp::multi_usrp::make(args); //make make new multi_impl and call make function in uhd device.cpp

    
    //alwasy select the sub dev first; the channel mapping affect other things;
    if (vm.count("rx_subdev")) {
        usrp->set_rx_subdev_spec(rx_subdev);
    }


    std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;

    /**
     * 2. configure usrp parameters
     * time_source, clock_source,channels, rate,freq,gain;
     */

    //ignore time and clock_source for now: ref,pps.

    /** set channels
     * because struct stream_args_t owns std::vector<size_t> channels feature, we shall split channels(string type) to chars first;
     */ 
    std::vector<std::string> channel_strings;
    std::vector<size_t> rx_channel_nums;
    rx_channel_list=channel_list;
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

    //set time now(); usrp-rio time stamp //ignore mimo;
    if(rx_channel_nums.size()>1)
    {
        usrp->set_time_unknown_pps(uhd::time_spec_t(0.0));
    }
    else
    {
        usrp->set_time_now(0.0);
    }

    // set the center frequency
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

    // set the rf gain for channels
    std::cout << boost::format("Setting RX Gain: %f dB...") % gain << std::endl;
        for(size_t ch=0;ch<rx_channel_nums.size();++ch)
    {
        usrp->set_rx_gain(gain, ch);
            std::cout << boost::format("Actual RX Gain: %f dB...")
                        % usrp->get_rx_gain(ch)
                << std::endl
                << std::endl;
    }

    // set the IF filter bandwidth for channels
 
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


    // set the antenna for all channels;
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
        

    // set rx sample rate;
    usrp->set_rx_rate(rx_rate);

    uhd::stream_args_t stream_args(rx_cpu,rx_otw);
    stream_args.channels=rx_channel_nums;

/********************************************************************/
    //create stream;
    rx_stream=usrp->get_rx_stream(stream_args);
    channel_nums=rx_stream->get_num_channels();  //acquire channel nums;
    max_samps_per_packet= rx_stream->get_max_num_samps(); //acquire max_sams;
    sample_size=uhd::convert::get_bytes_per_item(rx_cpu);// acquire samp size;
    std::cout<<"max_sams_per_packet:"<<max_samps_per_packet<<std::endl;
    std::cout<<"sample size:"<<sample_size<<std::endl;

    std::cout<<"/****************************************************/\n"
    "usrp_init_conf_complete\n\n"<<std::endl;
}


static void init_buffer_and_locks()
{
    //allocate buffer;
    //init locks;

    num_recv_per_chunk= 256;  // align chunk size to multiple PAGE SIZE(4096 B).
    buff_chunk_size=channel_nums*max_samps_per_packet*sample_size*num_recv_per_chunk;
    buffer_size= buff_chunk_size*buff_chunks_num;

    buffer =(char*)malloc(buffer_size);
    start_addr=buffer;

   
    // offset_file_chunks=0;   // does this place here is better than place at file_thread?
    // offset_rx_chunks=0; // the same question;

    // init buffer locks to RX writable;
    for(size_t bl=0;bl<buff_chunks_num;++bl)
    {
        buff_locks[bl]=1;
    }

    std::cout<<"/****************************************************/\n"
    "init_buffer_and_locks\n\n"<<std::endl;
}








void Rx_recv_to_buffer()
{
    //1. configure thread priority;
    //2. configure rx stream
    
    //3. check lock;
    //4. write data to buffer; 
    //5. handle recv exceptions;
    //6 . update buffer_offset,write_addr, summary inormation; 


    offset_rx_chunks=0; // target to the 0th lock inittially;
    size_t old_chunk_offset=0;//  for (6). update lock;
    //1. thread priority;
    if(elevate_priority) 
    {
        uhd::set_thread_priority_safe();
    }
    //2. configure rx stream
    //a. print receiver motivation and state information;
    std::cout<<boost::format("[%s] Testing receive rate %f Msamps in %u channels") % NOW() 
    % (usrp->get_rx_rate() / 1e6) % (rx_stream->get_num_channels()) <<std::endl;
    //b. define receive meta  variables which will be assigned at recv(); and used at handle recv exceptions;
    uhd::rx_metadata_t md; 
    bool had_an_overflow=false; // overflow indicator;
    uhd::time_spec_t last_time;
    const double rate = usrp->get_rx_rate(); //rate for  burst_pkt_time

    //c. declare rx recv buffer and specify packet size related to buffer chunk and buffer.

    std::vector<void*>rx_recv_buffs; // rx recv buffers for channels;
                                    // initially aligned recv_buff with buffer start address;
    size_t packet_size=max_samps_per_packet*sample_size;
    size_t offset_buff_in_packets=0;
    size_t packet_num_in_buff= buff_chunks_num*num_recv_per_chunk*channel_nums;
    for(size_t ch=0;ch<channel_nums;++ch)
    {
        rx_recv_buffs.push_back((void*)(start_addr+ch*packet_size));
    }

    //offset_buff_in_packets=(offset_buff_in_packets+channel_nums)%packet_num_in_buff;

    //c. issue rx stream cmd
    uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    cmd.time_spec=usrp->get_time_now()+uhd::time_spec_t(rx_delay);
    cmd.stream_now=false;
    rx_stream->issue_stream_cmd(cmd); // specify usrp receiver start running time;

    const float burst_pkt_time =
        std::max<float>(0.100f, (2 * max_samps_per_packet / rate));
    float recv_timeout = burst_pkt_time + rx_delay;// the initial recv_timeout;
    while(true)
    {

        
        //check lock rx write;
        assert(buff_locks[offset_rx_chunks]==1);
        // 

        //terminate condition and close RX stream;

        if(burst_timer_elapsed)
        {
            rx_stream->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
            return;
        }
        //4. write data to buffer
        try
        {   //std::cout<<"before recv"<<std::endl;
            size_t num_recv_byte=rx_stream->recv(rx_recv_buffs, max_samps_per_packet, md, recv_timeout)
                            * rx_stream->get_num_channels()*8; //I,Q all are float;
            //std::cout<<"After recv"<<std::endl;
            num_rx_samps += num_recv_byte/8;
            recv_timeout = burst_pkt_time;
        }
        catch (uhd::io_error& e) {
            std::cerr << "[" << NOW() << "] Caught an IO exception. " << std::endl;
            std::cerr << e.what() << std::endl;
            return;
        }

        //5. handle usrp recv exceptions;
                // handle the error codes
        switch (md.error_code) 
        {
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
                break;

            // ERROR_CODE_OVERFLOW can indicate overflow or sequence error
            case uhd::rx_metadata_t::ERROR_CODE_OVERFLOW:
                last_time       = md.time_spec;
                had_an_overflow = true;
                // check out_of_sequence flag to see if it was a sequence error or
                // overflow
                if (!md.out_of_sequence) {
                    num_overruns++;
                    std::cout<<"Overflow:"<<num_overruns<<std::endl;
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
                cmd.stream_now = (rx_recv_buffs.size() == 1);
                rx_stream->issue_stream_cmd(cmd);
                break;

            case uhd::rx_metadata_t::ERROR_CODE_TIMEOUT:
                if (burst_timer_elapsed) {
                    return;
                }
                std::cerr << "[" << NOW() << "] Receiver error: " << md.strerror()
                          << ", Timeout!\ncontinuing..." << std::endl;
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
        
        //6. update lock/ buffer,chunks and rx_recv buffer addr;
        offset_buff_in_packets=(offset_buff_in_packets+channel_nums)%packet_num_in_buff;
        //std::cout<<"offset_packets:"<<offset_buff_in_packets <<std::endl;
        if(offset_buff_in_packets%(num_recv_per_chunk*channel_nums)==0) // check current buff_chunk is full; 
        {   
            old_chunk_offset=offset_rx_chunks;
            
            num_buff_chunk_writes++; 
            //std::cout<<boost::format("offset_rx_chunks:%d,buff_lock is %d\n")%offset_rx_chunks %buff_locks[offset_rx_chunks]<<std::endl;
            offset_rx_chunks=(offset_rx_chunks+1)%buff_chunks_num;
            for(size_t ch=0;ch<channel_nums;++ch)
            {
                rx_recv_buffs.push_back((void*)(start_addr+(offset_buff_in_packets+ch)*packet_size));
            }

            buff_locks[ old_chunk_offset]=0;
        }
        else
        {
            for(size_t ch=0;ch<channel_nums;++ch)
            {
                rx_recv_buffs.push_back((void*)(start_addr+(offset_buff_in_packets+ch)*packet_size));
            }
        }
        
        


    }
}



void write_buffer_to_file()
{   
    //configure thread priority;
    // check lock;
    // write buffer to file;
    // update buffer_offset and file_offset;
    fd=open("usrp_samples.dat",O_CREAT|O_TRUNC|O_WRONLY,0644);
    if(fd<0)
    {
        perror("open file");
        exit(-1);
    }
    off_file=0; //write(,offset)  of file init to 0;

    offset_file_chunks=0;
    size_t old_offset_chunks=0;
    int ret=0;
    std::cout<<"fd:"<<fd<<std::endl;
    if(elevate_priority)   //thread priority;
    {
        uhd::set_thread_priority_safe();
        std::cout<<"\n\n\n/************************file thread init******************/\n\n\n\n"<<std::endl;
    }
    while(true)  //replace with another termination conditions;
    {   
        if(burst_timer_elapsed)//terminate
        {   
            std::cout<<"close file\n"<<std::endl;
            close(fd);
            return;
        }
        //check lock file readable
        if(buff_locks[offset_file_chunks]==0) //
        {   
            size_t offset_buff=offset_file_chunks*buff_chunk_size;
            ret=write(fd,(start_addr+offset_buff), buff_chunk_size);// write to file;
            if(ret<0)
            {
                perror("write file error\n");
                exit(-1);
            }
            old_offset_chunks=offset_file_chunks;
            offset_file_chunks=(offset_file_chunks+1)%buff_chunks_num; //update chunk_offset;
            off_file+=buff_chunk_size;   // update file offset;

            num_buff_chunk_reads++;
            buff_locks[old_offset_chunks]=1; //set lock RX writable
            //std::cout<<"reads:"<<num_buff_chunk_reads<<std::endl;
        }
        


        //else: busy wait;
    }
}
