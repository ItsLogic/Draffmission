CUBIOMES_SRC := $(addprefix cubiomes/,biomenoise.c biomes.c finders.c generator.c layers.c noise.c)

LARGE_BIOMES ?= 0
UNBOUND ?= 1
PRINT_INTERVAL ?= 4096
override CFLAGS += -O3 -march=native
override CXXFLAGS += -O3 -std=c++20 -I asio/asio/include -DOMISSION_LARGE_BIOMES=$(LARGE_BIOMES) -DOMISSION_UNBOUND=$(UNBOUND) -DPRINT_INTERVAL=$(PRINT_INTERVAL)

ifndef GPU_ARCH
  GPU_NAMES := $(shell nvidia-smi --query-gpu=name --format=csv,noheader)
  ifneq (,$(findstring RTX 50,$(GPU_NAMES)))
    GPU_ARCH := sm_120
  else ifneq (,$(findstring RTX 40,$(GPU_NAMES)))
    GPU_ARCH := sm_89
  else
    GPU_ARCH := native
  endif
endif

$(info Using GPU_ARCH = $(GPU_ARCH))

override NVCC_FLAGS += $(CXXFLAGS) --expt-relaxed-constexpr --default-stream per-thread -arch=$(GPU_ARCH) -use_fast_math -ccbin $(CXX)

MAIN_SRC := src/main.cpp
MAIN_DEP := $(MAIN_SRC) src/common.h

ifndef NO_GPU
	MAIN_SRC += gpu.o
	MAIN_DEP += gpu.o src/gpu.h
	MAIN_CXX := nvcc
	ifeq ($(SPLIT),1)
		# SPLIT: skip device link to preserve compute_89 PTX from gpu_compat.o
		# Without -nodlink, nvcc compiles the PTX to bad sm_120 cubin at link time
		MAIN_CXXFLAGS += $(filter-out -arch=%,$(NVCC_FLAGS)) -nodlink
	else
		MAIN_CXXFLAGS += $(NVCC_FLAGS)
	endif
else
	MAIN_CXX := $(CXX)
	MAIN_CXXFLAGS += $(CXXFLAGS) -DNO_GPU
endif

ifndef NO_CPU
	MAIN_SRC += cpu.o cubiomes.o libcubiomes.a
	MAIN_DEP += cpu.o cubiomes.o libcubiomes.a src/cpu.h
else
	MAIN_CXXFLAGS += -DNO_CPU
endif

ifndef NO_NET
	MAIN_SRC += client.o server.o
	MAIN_DEP += client.o server.o src/client.h src/server.h
else
	MAIN_CXXFLAGS += -DNO_NET
endif

MAIN_SRC += uploader.o
MAIN_DEP += uploader.o src/uploader.h

# Split compilation: SPLIT=1 enables gpu_compat.o (compute_89 JIT for Filter2_0A/0B)
ifeq ($(SPLIT),1)
	MAIN_SRC += gpu_compat.o
	MAIN_DEP += gpu_compat.o
endif

all: main

# usb/sb: split compilation (sm_120 + compute_89 JIT)
usb:
	$(MAKE) LARGE_BIOMES=0 UNBOUND=1 SPLIT=1 clean main

sb:
	$(MAKE) LARGE_BIOMES=0 UNBOUND=0 SPLIT=1 clean main

# lb/ulb: split compilation (sm_120 native + compute_89 JIT for filter2)
lb:
	$(MAKE) LARGE_BIOMES=1 UNBOUND=0 SPLIT=1 clean main

ulb:
	$(MAKE) LARGE_BIOMES=1 UNBOUND=1 SPLIT=1 clean main

.PHONY: sizecheck
sizecheck: src/sizecheck.cpp cpu.o cubiomes.o libcubiomes.a src/cpu.h src/cubiomes.h
	$(CXX) src/sizecheck.cpp cpu.o cubiomes.o libcubiomes.a -o sizecheck $(CXXFLAGS)

.PHONY: seedstats
seedstats: src/seedstats.cpp libcubiomes.a
	$(CXX) -O3 -std=c++17 src/seedstats.cpp libcubiomes.a -o seedstats -I. -lm -lpthread

.PHONY: probe
probe: src/probe.cpp libcubiomes.a
	$(CXX) -O3 -std=c++17 src/probe.cpp libcubiomes.a -o probe -I. -lm -lpthread

.PHONY: basinprobe
basinprobe: src/basinprobe.cpp libcubiomes.a
	$(CXX) -O3 -std=c++17 src/basinprobe.cpp libcubiomes.a -o basinprobe -I. -lm -lpthread

.PHONY: bench
bench: bench/bench.cu bench/bench_compat.cu src/Random.h src/kernel_0A.h src/kernel_0B.h
	nvcc -c bench/bench.cu -o bench/bench_main.o -O3 -std=c++20 --expt-relaxed-constexpr \
	  --default-stream per-thread -arch=$(GPU_ARCH) -use_fast_math -ccbin $(CXX) -Xptxas -v \
	  -DBENCH_LARGE_BIOMES=$(LARGE_BIOMES) -DBENCH_UNBOUND=$(UNBOUND)
	nvcc -c bench/bench_compat.cu -o bench/bench_compat.o -O3 -std=c++20 --expt-relaxed-constexpr \
	  --default-stream per-thread -arch=compute_89 -use_fast_math -ccbin $(CXX) \
	  -DBENCH_LARGE_BIOMES=$(LARGE_BIOMES)
	nvcc bench/bench_main.o bench/bench_compat.o -o bench/bench -O3 -std=c++20 \
	  --expt-relaxed-constexpr --default-stream per-thread -use_fast_math -ccbin $(CXX) -nodlink

clean:
	rm -f main sizecheck bench/bench bench/*.o libcubiomes.a biomenoise.o biomes.o finders.o generator.o layers.o noise.o cubiomes.o gpu.o gpu_compat.o cpu.o client.o server.o uploader.o

libcubiomes.a:
	$(CC) -c $(CUBIOMES_SRC) -fwrapv $(CFLAGS)
	$(AR) rcs libcubiomes.a biomenoise.o biomes.o finders.o generator.o layers.o noise.o

cubiomes.o: src/cubiomes.c src/cubiomes.h
	$(CC) -c $< -o $@ -fwrapv $(CFLAGS)

gpu.o: src/gpu.cu src/gpu.h src/common.h src/Random.h
	nvcc -c $< -o $@ $(NVCC_FLAGS) $(if $(filter 1,$(SPLIT)),-DSPLIT)

gpu_compat.o: src/gpu_compat.cu src/gpu_types.h src/common.h src/kernel_0A.h src/kernel_0B.h
	nvcc -c $< -o $@ $(CXXFLAGS) --expt-relaxed-constexpr --default-stream per-thread -use_fast_math -ccbin $(CXX) -arch=compute_89

cpu.o: src/cpu.cpp src/cpu.h src/common.h src/cubiomes.h
	$(CXX) -c $< -o $@ $(CXXFLAGS)

client.o: src/client.cpp src/client.h src/common.h
	$(CXX) -c $< -o $@ $(CXXFLAGS)

server.o: src/server.cpp src/server.h src/common.h
	$(CXX) -c $< -o $@ $(CXXFLAGS)

uploader.o: src/uploader.cpp src/uploader.h src/common.h
	$(CXX) -c $< -o $@ $(CXXFLAGS)

main: $(MAIN_DEP)
	$(MAIN_CXX) $(MAIN_SRC) -o $@ $(MAIN_CXXFLAGS)
