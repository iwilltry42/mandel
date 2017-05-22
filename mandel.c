#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>		
#include <pthread.h>
#include <sys/time.h>

#define BMPHEADER_SIZE 54

//#define DEBUG
#ifdef DEBUG
# define DEBUG_PRINT(x) fprintf x
#else
# define DEBUG_PRINT(x) do {} while (0)
#endif

// there will be a low level I/O function from the operating system
extern long write(int, const char *, unsigned long);

float zoom      = 1.5;
float quadLimit = 3.0;
char colorLimit = 40;

struct thread_args_struct {
    int width;
    int height;
    float imageRelation;
    char * img_buffer;
    char * span;
    int spanBytes;
};

pthread_mutex_t global_mutex;
int running_threads = 0;
int thread_lines;
int line_end = 0;
int leftover_lines;

typedef struct Complex_s {
	float re;
	float im;
} Complex;

// bad, but fast !!!
int intFloor(double x) {
	return (int)(x+100000) - 100000;
}

// count chars until \0 or space or "to long"
int len(char * str) {
	int ddorf=0;
	while (str[ddorf] != '\0' && str[ddorf] != ' ' && ddorf != 40225) ++ddorf;
	return ddorf;
}

// NEW check if input is numeric
int isNumeric (const char * s) {
    if (s == NULL || *s == '\0' || isspace(*s))
      return 0;		// false
    char * p;
    strtod (s, &p);
    return *p == '\0';	// true
}

// read a positive number from a char array
int str2num(char * str) {
	if(!(isNumeric(str))) return -1;	// NEW
	int result = 0;
	int b = 1;
	int l = len(str);
	int i;
	for(i=1; i<l; ++i) b *= 10;
	for(i=0; i<l; ++i) {
		result += b * (int)(str[i] - '0');
		b /= 10;
	}
	return result;
}

void toRGB(int id, char * blueGreenRed) {
	blueGreenRed[0] = 0;
	blueGreenRed[1] = 0;
	blueGreenRed[2] = 0;
	if ( id == colorLimit ) return;

	float hi,q,t,coeff;

	coeff = 7.0 * (id/(float)colorLimit);
	hi = intFloor(coeff);
	t = coeff - hi;
	q = 1 - t;
	if (hi == 0.0) {
		blueGreenRed[2] = 0;
		blueGreenRed[1] = t*255; //immer mehr green und blau -> dunkelblau zu cyan
		blueGreenRed[0] = t*127 + 128;
	} else if (hi == 1.0) {
		blueGreenRed[2] = t*255; //immer mehr rot -> cyan zu weiss
		blueGreenRed[1] = 255;
		blueGreenRed[0] = 255;
	} else if (hi == 2.0) {
		blueGreenRed[2] = 255;
		blueGreenRed[1] = 255;
		blueGreenRed[0] = q*255; // immer weniger blau -> weiss zu gelb
	} else if (hi == 3.0) {
		blueGreenRed[2] = 255;
		blueGreenRed[1] = q*127 + 128; // immer weniger green -> gelb zu orange
		blueGreenRed[0] = 0;
	} else if (hi == 4.0) {
		blueGreenRed[2] = q*127 + 128; // orange wird dunkler -> orange zu braun
		blueGreenRed[1] = q*63 + 64;
		blueGreenRed[0] = 0;
	} else if (hi == 5.0) {
		blueGreenRed[2] = 128;
		blueGreenRed[1] = 64;
		blueGreenRed[0] = t*128; // mehr blau -> braun zu violett
	} else if (hi == 6.0) {
		blueGreenRed[2] = q*128; // weniger rot und green -> violett wird dunkelblau
		blueGreenRed[1] = q*64;
		blueGreenRed[0] = 128;
	}
}

void calcPixel(char * blueGreenRed, int x, int y, int width, int height, float imageRelation, Complex * c, Complex * newz) {
	// calcPixel()
	// global: zoom, quadLimit, colorLimit
	Complex z = {0,0};
	float quad = 0;
	char iterate = 0;

	// change e.g. c.re to c->re when using a pointer to a struct instance
	// use . for instance
	c->re = (float) (zoom * (-1.0 + imageRelation * ((x - 1.0) / (width - 1.0)) ));
	c->im = (float) (zoom * (0.5 - (y - 1.0) / (height - 1.0) ));

	// iterate
	for ( iterate=1; iterate < colorLimit && quad < quadLimit; ++iterate ) {
		quad = z.re * z.re + z.im * z.im;

		newz->re = (z.re * z.re) - (z.im * z.im) + c->re;
		newz->im = (float) (z.re * z.im * 2.0 + c->im);

		z = *newz;
	}
	toRGB(iterate, blueGreenRed);
}

void calcLine(int y, char span[4], int spanBytes, int width, int height, float imageRelation, char * img_buffer, int line_index) {
	
	Complex c    = {0,0};
	Complex newz = {0,0};
	
	char blueGreenRed [3];	// a single pixel => one buffer for every line/thread
	int column_index = 0;
	
	for (int x=1; x <= width; x++) {
		calcPixel(blueGreenRed, x, y, width, height, imageRelation, &c, &newz);		// calculate a single pixel
        pthread_mutex_lock(&global_mutex);      // lock mutex before writing to shared img_buffer
		memcpy(&img_buffer[0]+line_index*width*3+column_index*3+line_index*spanBytes, &blueGreenRed, sizeof(char)*3);	// append new pixel to line
        pthread_mutex_unlock(&global_mutex);
		column_index++;	// move on to next pixel in line
	}
	// BMP lines must be of lengths divisible by 4
    pthread_mutex_lock(&global_mutex);
	memcpy(&img_buffer[0]+line_index*width*3+column_index*3+line_index*spanBytes, span, (size_t) spanBytes);
    pthread_mutex_unlock(&global_mutex);
}

void * calcImg(void * args) {    // args: char span[4], int spanBytes, int width, int height, float imageRelation, char img_buffer[height] [width*3], int line_index
    pthread_mutex_lock(&global_mutex);      // grab mutex to prevent changes to arguments struct

    running_threads++;                                  // increment counter for active threads
    int lines = thread_lines;                           // lines to be calculated by this thread
    if(leftover_lines > 0) {                            // if there are any surplus lines, grab one
        lines++;
        leftover_lines--;
    }
    int line_index = line_end;                          // line index for current thread
    line_end = line_end + lines;                        // indicator for other threads where this thread will stop calculating lines
    int line_limit = line_index + lines;

    struct thread_args_struct * arguments = args;
    DEBUG_PRINT((stderr, "Created thread for <%d> lines, starting from line <%d> \n", lines, line_index));    // DEBUG
    pthread_mutex_unlock(&global_mutex);

	int y;
	for (y = 1 + line_index; y <= line_limit; ++y) {
		calcLine(y, arguments->span, arguments->spanBytes, arguments->width, arguments->height, arguments->imageRelation, arguments->img_buffer, line_index);
		line_index++;	//increment line_index to indicate start of a new line
	}
    return NULL;
}

int main(int argc, char ** argv, char ** envp) {
	
	if(argc != 4) {	// the function takes exactly three arguments, one for width, one for height and one for the count of threads used
		fprintf(stderr, "Arguments given: %d \n", argc-1);	// -1, as the function call is an argument itself
		fprintf(stderr, "Please provide three arguments (width, height, threads). \n");
		return -1;	// return != 0 => failed
	}	
	
	int width  = str2num(argv[1]);
	int height = str2num(argv[2]);
	int count_threads = str2num(argv[3]);

    if (height <= count_threads) {
        fprintf(stderr, "INFO: more lines than threads specified -> reducing number of threads.");
        count_threads = height;
    }

	int lines_per_thread = (height/count_threads);
    thread_lines = lines_per_thread;
    leftover_lines = height % count_threads;

	
	// check if values are negative or zero (= input negative or not a number)
	if(width <= 0 || height <= 0 || count_threads <= 0) {
		fprintf(stderr, "Bad input values! Values for width, height and number of threads must be integers >= 0.");
		return -1;
	}

	// starttime
	struct timeval t0;
	gettimeofday(&t0, 0);

	float imageRelation = (float)width/(float)height;

	unsigned char info[BMPHEADER_SIZE] = {
		          //size
		'B','M',  0,0,0,0, 0,0, 0,0, 54,0,0,0,
		          //width  //height
		40,0,0,0, 0,0,0,0, 0,0,0,0,  1,0, 24,0,
		          // datasize
		0,0,0,0,  0,0,0,0
	};
	
	// BMP lines must be of lengths divisible by 4
	int spanBytes = 4 - ((width * 3) % 4);
	if (spanBytes == 4) spanBytes = 0;
	int psize = ((width * 3) + spanBytes) * height;
	
	*( (int*) &info[2])  = BMPHEADER_SIZE + psize;
	*( (int*) &info[18]) = width;
	*( (int*) &info[22]) = height;
	*( (int*) &info[34]) = psize;

	/***** PREPARE data and variables to be used by calculating threads *****/
	char * img_buffer = malloc(sizeof(char)*height*width*3+height*spanBytes);  // buffer two hold image data (pixels)
    DEBUG_PRINT((stderr, "Allocating <%d> bytes of memory for the image...", (int)(sizeof(char)*height*width*3+height*spanBytes)));   // DEBUG

    // prepare (static) arguments for threads
	struct thread_args_struct thread_arguments;
    thread_arguments.width = width;
    thread_arguments.height = height;
    thread_arguments.imageRelation = imageRelation;
	thread_arguments.span = (char*) "\0\0\0\0";
    thread_arguments.spanBytes = spanBytes;
	thread_arguments.img_buffer = img_buffer;

	pthread_t threads [height];                 // array that holds all the threads (except the main thread)
    pthread_mutex_init(&global_mutex, NULL);    // initialize globally defined mutex to synchronize threads

	DEBUG_PRINT((stderr, "lines per thread: %d, leftover_lines: %d\n", lines_per_thread, leftover_lines));        // DEBUG-information

	/***** CREATE THREADS *****/
	for(int i = 0; i < count_threads-1; i++) {      // count_threads-1, because one thread is this main thread
		if(pthread_create(&threads[i], NULL, &calcImg, (void*) &thread_arguments) != 0) {	// create threads and check for failures
			fprintf(stderr, "Error creating threads!");
			exit(EXIT_FAILURE);
		}
	}
	calcImg(&thread_arguments);


    /***** JOIN THREADS ******/
    for (int j = 0; j < count_threads-1; j++) {
        if (pthread_join(threads[j], NULL)) {
            fprintf(stderr, "ERROR: Error joining threads\n");
            return -1;
        }
    }
	
	/***** WRITE IMAGE to stdout line by line *****/
	write(1, (char *) info, BMPHEADER_SIZE);	// write BMP-Header
	int i;
	int current_address = 0;
	for (i = 0; i < height; i++) {
        write(1, &img_buffer[0]+current_address, sizeof(char)*width*3+spanBytes);
		current_address = current_address + width*3+spanBytes;
	}

	/***** CLEANUP *****/
	pthread_mutex_destroy(&global_mutex);
	free(img_buffer);

	// endtime
	struct timeval t1;
	gettimeofday(&t1, 0);

	// computation time in microseconds
	long comptime = (t1.tv_sec - t0.tv_sec)*1000000 + t1.tv_usec - t0.tv_usec;
	fprintf(stderr, "\n****************** DONE ******************\n"
                      "* Computation time: %ld microseconds  \n"
                      "******************************************\n", comptime);
	
	return 0;
}
