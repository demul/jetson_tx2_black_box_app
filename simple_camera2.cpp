// simple_camera.cpp
// MIT License
// Copyright (c) 2019 JetsonHacks
// See LICENSE for OpenCV license and additional information
// Using a CSI camera (such as the Raspberry Pi Version 2) connected to a 
// NVIDIA Jetson Nano Developer Kit using OpenCV
// Drivers for the camera and OpenCV are included in the base image

// #include <iostream>
#include <opencv2/opencv.hpp>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <spawn.h>

#define BUF_SIZE 50
#define SAVE_RATE 60

#define START() do{\
	x = clock();\
}while(0)

#define END() do{\
	printf("ptime : %lf\n", (double)(clock()- x)/CLOCKS_PER_SEC);\
}while(0)

using namespace cv;
using namespace std;

VideoWriter* writer;
VideoWriter* writer_standby;
sem_t sem_make_writer;

extern char** environ;
const int capture_width = 640 ;
const int capture_height = 480 ;
const int display_width = 640 ;
const int display_height = 480 ;
const int framerate = 25;
const int flip_method = 0 ;
bool esc = 1;
	
void* th_make_writer_main(void*);
void* th_mngmem_main(void*);
void get_date_dir(char*, time_t);
void get_date(char*, time_t);
bool check_available_disk_size(void);
void delete_oldest_folder(void);

string gstreamer_pipeline (int capture_width, int capture_height, int display_width, int display_height, int framerate, int flip_method) 
{
    return "nvarguscamerasrc ! video/x-raw(memory:NVMM), width=(int)" + std::to_string(capture_width) + ", height=(int)" +
           std::to_string(capture_height) + ", format=(string)NV12, framerate=(fraction)" + std::to_string(framerate) +
           "/1 ! nvvidconv flip-method=" + std::to_string(flip_method) + " ! video/x-raw, width=(int)" + std::to_string(display_width) + ", height=(int)" +
           std::to_string(display_height) + ", format=(string)BGRx ! videoconvert ! video/x-raw, format=(string)BGR ! appsink";
}

int main()
{
	// make semaphore for block thread-make_writer
	sem_init(&sem_make_writer, 0, 1);

	// make thread for making writer
	pthread_t th_make_writer;

	if(pthread_create(&th_make_writer, NULL, th_make_writer_main, (void*)NULL) !=0)
	{
		printf("pthread_create() error!\n");
		exit(-1);
	}
	pthread_detach(th_make_writer);
	//////////////////////////////////////

	// make thread for manage memory
	pthread_t th_mngmem;

	if(pthread_create(&th_mngmem, NULL, th_mngmem_main, (void*)NULL) !=0)
	{
		printf("pthread_create() error!\n");
		exit(-1);
	}
	pthread_detach(th_mngmem);
	//////////////////////////////////////
	
	std::string pipeline = gstreamer_pipeline(capture_width,
			capture_height,
			display_width,
			display_height,
			framerate,
			flip_method);
	std::cout << "Using pipeline: \n\t" << pipeline << "\n";

	cv::VideoCapture cap(pipeline, cv::CAP_GSTREAMER);
	if(!cap.isOpened()) 
	{
		std::cout<<"Failed to open camera."<<std::endl;
		return (-1);
	}

    int delay = cvRound(1000 / framerate);

    cv::namedWindow("frame", cv::WINDOW_AUTOSIZE);
    std::cout << "Hit ESC to exit" << "\n" ;

    int t = 0;
	clock_t start_time = 0;
    //clock_t x;
    cv::Mat img;

	clock_t delay_clock = delay * CLOCKS_PER_SEC / 1000;

	sleep(10);
	//START();
	while(esc)
	{
		VideoWriter* tmpw = writer_standby;
		writer_standby = writer;
		writer = tmpw;
		sem_post(&sem_make_writer);

		while(t<framerate * SAVE_RATE && esc)
		{
			t++;
			start_time = clock();
			if (!cap.read(img)) 
			{
				std::cout<<"Capture read error"<<std::endl;
				return(-1);
			}

			writer->write(img);
			imshow("frame", img);

			if(waitKey(1) == 27)
				esc = 0;

			while(1)
				if(clock() - start_time >= delay_clock)
					break;
		}
    	t = 0;
		writer->release();
	}
	//END();

	sem_destroy(&sem_make_writer);
    cap.release();
    cv::destroyAllWindows() ;
    return 0;
}



void* th_make_writer_main(void* arg)
{	
	int fourcc = VideoWriter::fourcc('D', 'I', 'V', 'X');
	char dirname[BUF_SIZE];
	char filename[BUF_SIZE];
	char pathname[BUF_SIZE];
	int iter = 0;

	writer = new VideoWriter;
	writer_standby = new VideoWriter;
	
	while(esc)
	{
		get_date_dir(dirname, time(NULL) + SAVE_RATE);
		mkdir(dirname, 0755);
		
		for(iter =0; esc && iter< SAVE_RATE; iter++)
		{
			sem_wait(&sem_make_writer);
			get_date(filename, time(NULL) + SAVE_RATE);
			sprintf(pathname, "%s/%s", dirname, filename);
			writer_standby->open(pathname, fourcc, framerate, Size(capture_width, capture_height));
		}
	}

	delete(writer);
	delete(writer_standby);
	return (void*)NULL;
}

void* th_mngmem_main(void* arg)
{
	while(esc)
	{
		if(check_available_disk_size())
			delete_oldest_folder();
		sleep(1000);
	}
	return (void*)NULL;
}

void get_date_dir(char* str_date, time_t t)
{
	struct tm* date_ = localtime(&t);
	strftime(str_date, BUF_SIZE, "%4Y%2m%d_%2H", date_);
	return;
}
void get_date(char* str_date, time_t t)
{
	struct tm* date_ = localtime(&t);
	strftime(str_date, BUF_SIZE, "%4Y%2m%d_%2H%2M%2S.avi", date_);
	return;
}


bool check_available_disk_size(void)
{
	struct statfs fs;
	statfs("/", &fs);

	int avail_size = (fs.f_bavail * fs.f_bsize) / (1024 * 1024);

	printf("size available : %dMB\n", avail_size);

	if(avail_size <= 2 * 1024) // if available disk size is under 2GB
		return 1;
	else
		return 0;
}

void delete_oldest_folder(void)
{
	struct dirent** namelist; //scandir 내부적으로 동적할당을 함.
	int count;
	int i;
	char* filename;
	if((count = scandir("/home/demul/black_box/", &namelist, NULL, alphasort)) == -1) //scandir을 쓸 때는 우리가 직접 free해줘야 함.
    {
        printf("scandir() error\n");
        return;
    }

	for (i = 0; i < count; i++)
	{
		filename = namelist[i]->d_name;
		
		if(filename[0] != '2')
			continue;
		else
		{
			pid_t pid;
			int status;
			char* argv[] = {"rm", "-rf", filename, NULL};

			posix_spawn(&pid, "/bin/rm", NULL, NULL, argv, environ);
			waitpid(pid, &status, WNOHANG);
			printf("%s removed \n", filename);
			break;
		}
	}

	for (i = 0; i < count; i++)
		free(namelist[i]);
}
