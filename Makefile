.PHONY: debug clean

GimbalControl: main.cpp baudrate.c
	g++ main.cpp baudrate.c -lwiringPi -oGimbalControl

debug: main.cpp baudrate.c
	g++ -DDEBUG main.cpp baudrate.c -lwiringPi -oGimbalControl

clean:
	rm GimbalControl