CPPFLAGS = -Wno-multichar

isosign: isosign.o
	g++ -o $@ $<
isosign.o: ../../src/isosign.cpp ../../src/siso.h
	gcc -c $(CPPFLAGS) $<

