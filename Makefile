CC = g++
CXXFLAGS = -O2 -std=c++17 -I. -I$(HOME)/work/rapidjson/include
LDFLAGS  = -lcurl

all: seq_client level_client

# ---- Sequential build ----
seq_client: seq.o
	$(CC) $< -o $@ $(LDFLAGS)

seq.o: seq.cpp
	$(CC) $(CXXFLAGS) -c $< -o $@

# ---- Parallel build ----
level_client: client.o
	$(CC) $< -o $@ $(LDFLAGS) -pthread

client.o: client.cpp blocking_queue.hpp
	$(CC) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f seq_client level_client *.o
