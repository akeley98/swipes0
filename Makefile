server: server.cc
	c++ -O2 -Wall -Wextra server.cc -I ./websocketpp -lpthread -lrt -lboost_system -lboost_thread -lboost_random -lboost_chrono -lboost_date_time -lboost_atomic -o server
