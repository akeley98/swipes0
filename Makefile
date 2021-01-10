server: server.cc
	$(CXX) -O2 -Wall -Wextra server.cc -I ./websocketpp -lpthread -lrt -lboost_system -lboost_thread -lboost_random -lboost_chrono -lboost_date_time -lboost_atomic -o server

client: client.cc
	$(CXX) -O2 -Wall -Wextra client.cc -I ./websocketpp -lboost_system -lpthread -o client
