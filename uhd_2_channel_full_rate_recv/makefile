
tsff.o:
	g++ -fvisibility=hidden -fvisibility-inlines-hidden -O3    -Wall -Wextra -Wsign-compare -std=gnu++17 -c tsff.cpp
tsff:tsff.o
	g++ -fvisibility=hidden -fvisibility-inlines-hidden -O3    tsff.o  -o tsff  /usr/lib/x86_64-linux-gnu/libboost_chrono.so /usr/lib/x86_64-linux-gnu/libboost_date_time.so /usr/lib/x86_64-linux-gnu/libboost_filesystem.so /usr/lib/x86_64-linux-gnu/libboost_program_options.so /usr/lib/x86_64-linux-gnu/libboost_regex.so /usr/lib/x86_64-linux-gnu/libboost_unit_test_framework.so /usr/lib/x86_64-linux-gnu/libboost_serialization.so /usr/lib/x86_64-linux-gnu/libboost_thread.so /usr/lib/x86_64-linux-gnu/libboost_system.so /usr/lib/x86_64-linux-gnu/libboost_atomic.so -lpthread -lpthread -lrt -ldl -luhd -ldpdk 


run_tx: tsff
	./tsff --args "addr=192.168.30.2"   --tx_rate 1e6 --duration 1 --channels "0,1" --freq 2e9 

clean:
	rm -rf tsff tsff.o