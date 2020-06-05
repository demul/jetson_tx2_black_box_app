simple_camera2 : simple_camera2.cpp
	g++ -std=c++11 -Wall -I/usr/lib/openvc simple_camera2.cpp -L/usr/lib -lopencv_core -lopencv_highgui -lopencv_videoio -lpthread -o simple_camera2
