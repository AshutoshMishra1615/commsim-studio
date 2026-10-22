CXX ?= g++
CXXFLAGS ?= -std=c++17 -O3 -march=native -Iinclude -pthread
APPS = ber_sweep coding_gain cdma_demo pcm_demo eye_diagram selftest
all: $(APPS)
%: apps/%.cpp $(wildcard include/commsim/*.hpp)
	$(CXX) $(CXXFLAGS) $< -o $@
run: all
	mkdir -p results && ./selftest && ./ber_sweep && ./coding_gain && ./cdma_demo && ./pcm_demo && ./eye_diagram && python3 scripts/plot.py
clean:
	rm -f $(APPS)
