make:	
		gcc -pthread -o s-talk UDPComms4Threads.c list.o

clean:	
		rm -f *o s-talk