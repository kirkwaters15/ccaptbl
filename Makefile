PGM=ccap2tbl
OBJ=ccap2tbl.o
SRC=ccap2tbl.cpp

INCLUDE = -I /san1/tcm-i/${ARCH}/include
LIB=-L /san1/tcm-i/${ARCH}/lib -lgdal
CPPFLAGS=-g -O $(INCLUDE) -D OGR_ENABLED
CPP=g++

all: ccap2bivar ccap_summarize

ccap_summarize: ccap_summarize.o
	$(CPP) $(CFLAGS) -o ccap_summarize ccap_summarize.o $(LIB)

ccap2bivar: ccap2bivar.o
	$(CPP) $(CFLAGS) -o ccap2bivar ccap2bivar.o $(LIB)
