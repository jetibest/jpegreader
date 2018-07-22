#include <iostream>
#include <string>
#include <csignal>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <jpeglib.h>

using namespace std;

struct buffer
{
	void * start;
	size_t length;
};
static int fd = -1;
static buffer* buffers = nullptr;
static size_t n_buffers = 0;

static bool running = true;

static size_t width = 1920;
static size_t height = 1080;
static string devfile = "/dev/video0";

static void int_handler(int sig)
{
	cerr << "SIGINT received: " << sig << endl;
	running = false;
}

static void errno_exit(const char* s)
{
	cerr << s << " error: " << errno << endl;
	exit(EXIT_FAILURE);
}

static int do_ioctl(int fd, int request, void* argp)
{
	int r;
	while(running)
	{
		r = ioctl(fd, request, argp);
		
		if(errno != EINTR || r != -1)
		{
			return r;
		}
	}
	return -1;
}

static void loop()
{
	while(running)
	{
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		
		struct timeval tv;
		tv.tv_sec = 2;
		tv.tv_usec = 0;
		
		int r = select(fd + 1, &fds, NULL, NULL, &tv);
		if(r == -1)
		{
			if(errno == EINTR)
			{
				continue;
			}
			errno_exit("select");
		}
		if(r == 0)
		{
			cerr << "select timeout" << endl;
			exit(EXIT_FAILURE);
		}
		

		struct v4l2_buffer buf = {0};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		
		if(do_ioctl(fd, VIDIOC_DQBUF, &buf) == -1)
		{
			if(errno == EAGAIN)
			{
				continue;
			}
			else if(errno != EIO)
			{
				errno_exit("VIDIOC_DQBUF");
			}
		}
		
		if(buf.index < n_buffers)
		{
			unsigned char *b = static_cast<unsigned char*>(mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset));
			// cerr << "buffer length: " << buf.length << endl;
			// cerr << "image length:" << buf.bytesused << endl;
			// cerr << "pointer address: " << b << endl;
			fprintf(stdout, "\x01%d\n", buf.length);
			fwrite(b, 1, buf.length, stdout);
			
			if(do_ioctl(fd, VIDIOC_QBUF, &buf) == -1)
			{
				errno_exit("VIDIOC_QBUF");
			}
		}
	}
}

static void opendev()
{
	// OPEN DEVICE HANDLE
	struct stat st;
	if(stat(devfile.c_str(), &st) == -1)
	{
		cerr << "cannot identify '" << devfile << "': " << errno << endl;
		exit(EXIT_FAILURE);
	}
	
	if(!S_ISCHR(st.st_mode))
	{
		cerr << devfile << " is not a device" << endl;
		exit(EXIT_FAILURE);
	}
	
	fd = open(devfile.c_str(), O_RDWR | O_NONBLOCK, 0);
	
	if(fd == -1)
	{
		cerr << "could not open device '" << devfile << "': " << errno << endl;
		exit(EXIT_FAILURE);
	}
	
	// SETUP DEVICE CONFIGURATION
	struct v4l2_capability cap;
	if(do_ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1)
	{
		if(errno == EINVAL)
		{
			cerr << devfile << " does not support V4L2" << endl;
			exit(EXIT_FAILURE);
		}
		else
		{
			errno_exit("VIDIOC_QUERYCAP");
		}
	}
	
	if(!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
	{
		cerr << devfile << " cannot capture video" << endl;
		exit(EXIT_FAILURE);
	}
	
	struct v4l2_crop crop = {0};
	struct v4l2_cropcap cropcap = {0};
	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	
	if(do_ioctl(fd, VIDIOC_CROPCAP, &cropcap) == 0)
	{
		crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		crop.c = cropcap.defrect;
		
		if(do_ioctl(fd, VIDIOC_S_CROP, &crop) == -1)
		{
			// check if errno is EINVAL
			// ignore that cropping is not supported
		}
	}
	
	struct v4l2_format fmt = {0};
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = width;
	fmt.fmt.pix.height = height;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
	fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
	
	if(do_ioctl(fd, VIDIOC_S_FMT, &fmt) == -1)
	{
		errno_exit("VIDIOC_S_FMT");
	}
	
	if(fmt.fmt.pix.width != width || fmt.fmt.pix.height != height)
	{
		width = fmt.fmt.pix.width;
		height = fmt.fmt.pix.height;
		
		cerr << "device set resolution to " << width << "x" << height << endl;
	}
	
	size_t min = fmt.fmt.pix.width * 2;
	if(fmt.fmt.pix.bytesperline < min)
	{
		fmt.fmt.pix.bytesperline = min;
	}
	min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
	if(fmt.fmt.pix.sizeimage < min)
	{
		fmt.fmt.pix.sizeimage = min;
	}
	
	// INIT MMAP
	struct v4l2_requestbuffers req = {0};
	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	
	if(do_ioctl(fd, VIDIOC_REQBUFS, &req) == -1)
	{
		if(errno == EINVAL)
		{
			cerr << devfile << " does not support memory mapping" << endl;
			exit(EXIT_FAILURE);
		}
		else
		{
			errno_exit("VIDIOC_REQBUFS");
		}
	}
	
	if(req.count < 2)
	{
		cerr << "insufficient buffer memory in " << devfile << endl;
		exit(EXIT_FAILURE);
	}
	
	buffers = new buffer[req.count]();
	
	if(!buffers)
	{
		cerr << "out of memory" << endl;
		exit(EXIT_FAILURE);
	}
	
	for(n_buffers=0;n_buffers<req.count;++n_buffers)
	{
		struct v4l2_buffer buf = {0};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n_buffers;
		
		if(do_ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1)
		{
			errno_exit("VIDIOC_QUERYBUF");
		}
		
		buffers[n_buffers].length = buf.length;
		buffers[n_buffers].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
		
		if(buffers[n_buffers].start == MAP_FAILED)
		{
			errno_exit("mmap");
		}
	}

	// START CAPTURE
	for(size_t i=0;i<n_buffers;++i)
	{
		struct v4l2_buffer buf = {0};
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		
		if(do_ioctl(fd, VIDIOC_QBUF, &buf) == -1)
		{
			errno_exit("VIDIOC_QBUF");
		}
	}
	
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if(do_ioctl(fd, VIDIOC_STREAMON, &type) == -1)
	{
		errno_exit("VIDIOC_STREAMON");
	}
}

static void closedev()
{
	// STOP CAPTURING VIDEO
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if(do_ioctl(fd, VIDIOC_STREAMOFF, &type) == -1)
	{
		errno_exit("VIDIOC_STREAMOFF");
	}
	
	// FREE BUFFERS
	for(size_t i=0;i<n_buffers;++i)
	{
		if(munmap(buffers[i].start, buffers[i].length) == -1)
		{
			errno_exit("munmap");
		}
	}
	free(buffers);
	
	// CLOSE HANDLE
	if(close(fd) == -1)
	{
		errno_exit("close");
	}
	
	fd = -1;
}

int main(int argc, char* argv[])
{
	signal(SIGINT, int_handler);
	
	if(argc == 2)
	{
		string arg(argv[1]);
		if(arg == "--help" || arg == "-h")
		{
			cout <<
				"The jpegreader allows streaming JPEG-frames from a V4L2-device using MJPEG.\n"
				"The advantage of this method over alternatives (e.g. FFMpeg -f mpjpeg) is \n"
				"that neither MJPEG nor JFIF nor JPEG contains any header with the byte length\n"
				"of the JPEG data. This means that a consumer always needs to compare every\n"
				"single byte to look for the \\xff\\xd8 marker that marks the start of an\n"
				"image packet. With jpegreader, you can skip over the image packet in order\n"
				"to separate the JPEG frames without any significant CPU usage.\n"
				"\n"
				"Example usage:\n"
				"  jpegreader -s 1920x1080 -i /dev/video0 | use_jpeg_frames\n"
				"\n"
				"The standard output contains a continuous stream of packets like:\n"
				"  \\x01<frame-length>\\nJPEG_FRAMEDATA\\x01<frame-length>\\nJPEG_FRAMEDATA...\n"
				"\n"
				"To parse individual JPEG frames is very simple:\n"
				"  - Find the SOH-marker (Start of Heading: \\x01, ^A)\n"
				"  - Read number N until LF-marker (Line Feed: \\n, \\x0a, ^J)\n"
				"  - Read N bytes after skipping the LF-marker.\n"
				"  - If the next byte is not a SOH-marker, maybe discard the frame.\n"
				"\n"
			;
			return EXIT_SUCCESS;
		}
	}
	
	for(int i=1;i<argc;++i)
	{
		string arg(argv[i]);
		
		if(i+1 < argc && (arg == "--resolution" || arg == "-r" || arg == "-s"))
		{
			++i;
			string val(argv[i]);
			
			for(size_t j=0;j<val.length();++j)
			{
				if(val[j] == 'x')
				{
					width = atoi(val.substr(0, j).c_str());
					height = atoi(val.substr(j + 1).c_str());
					break;
				}
			}
		}
		else if(i+1 < argc && (arg == "--device" || arg == "-d" || arg == "-i"))
		{
			++i;
			string val(argv[i]);
			
			devfile = val;
		}
	}
	
	opendev();
	
	loop();
	
	closedev();
	
	return EXIT_SUCCESS;
}

