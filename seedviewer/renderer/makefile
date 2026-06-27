CXX ?= g++
CC ?= gcc
CFLAGS := -O3 -fwrapv -I../../cubiomes
CXXFLAGS := -O3 -std=c++17 -I../../cubiomes

CUBIOMES_SRC := $(addprefix ../../cubiomes/,biomenoise.c biomes.c finders.c generator.c layers.c noise.c util.c)
CUBIOMES_OBJ := $(notdir $(CUBIOMES_SRC:.c=.o))

render_map: render_map.cpp libcubiomes_view.a
	$(CXX) $(CXXFLAGS) render_map.cpp libcubiomes_view.a -o render_map -lm

libcubiomes_view.a: $(CUBIOMES_SRC)
	$(CC) -c $(CUBIOMES_SRC) $(CFLAGS)
	$(AR) rcs libcubiomes_view.a biomenoise.o biomes.o finders.o generator.o layers.o noise.o util.o

clean:
	rm -f render_map libcubiomes_view.a biomenoise.o biomes.o finders.o generator.o layers.o noise.o util.o

.PHONY: clean
