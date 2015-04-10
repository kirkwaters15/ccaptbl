PGM=ccap2tbl
OBJ=ccap2tbl.o
SRC=ccap2tbl.cpp

INCLUDE = -I /san1/tcm-i/${ARCH}/include
LIB=-L /san1/tcm-i/${ARCH}/lib -lgdal
CPPFLAGS=-g -O $(INCLUDE)
CPP=g++

$(PGM): $(OBJ)
	$(CPP) $(CFLAGS) -o $(PGM) $(OBJ) $(LIB)