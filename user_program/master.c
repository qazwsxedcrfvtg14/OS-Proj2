#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>

#define master_IOCTL_CREATESOCK 0x12345677
#define master_IOCTL_MMAP 0x12345678
#define master_IOCTL_EXIT 0x12345679

#define PAGE_SIZE 4096
#define BUF_SIZE 512
#define MAP_SIZE (PAGE_SIZE * 10)
size_t get_filesize(const char* filename);//get the size of the input file


int main (int argc, char* argv[])
{
	char buf[BUF_SIZE];
	int i, dev_fd, file_fd;// the fd for the device and the fd for the input file
	size_t file_size, offset = 0, tmp;
	ssize_t ret;
	char file_name[50], method[20];
	char *kernel_address = NULL, *file_address = NULL;
	struct timeval start;
	struct timeval end;
	double trans_time; //calulate the time between the device is opened and it is closed
	void *mapped_mem, *kernel_mem;


	strcpy(file_name, argv[1]);
	strcpy(method, argv[2]);


	if( (dev_fd = open("/dev/master_device", O_RDWR)) < 0)
	{
		perror("failed to open /dev/master_device\n");
		return 1;
	}
	gettimeofday(&start ,NULL);
	if( (file_fd = open (file_name, O_RDWR)) < 0 )
	{
		perror("failed to open input file\n");
		return 1;
	}

	if( (file_size = get_filesize(file_name)) < 0)
	{
		perror("failed to get filesize\n");
		return 1;
	}


	if(ioctl(dev_fd, master_IOCTL_CREATESOCK) == -1) //master_IOCTL_CREATESOCK : create socket and accept the connection from the slave
	{
		perror("ioclt server create socket error\n");
		return 1;
	}


	switch(method[0])
	{
		case 'f': //fcntl : read()/write()
			do
			{
				ret = read(file_fd, buf, sizeof(buf)); // read from the input file
				while(write(dev_fd, buf, ret)<0&&errno==EAGAIN);//write to the the device
			}while(ret > 0);
			break;
		case 'm':
			kernel_mem = mmap(NULL, MAP_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, dev_fd, 0);
			for(i=0;i*MAP_SIZE<file_size;i++)
			{
				tmp = file_size-i*MAP_SIZE;
				if(tmp>MAP_SIZE) tmp = MAP_SIZE;
				mapped_mem = mmap(NULL, tmp, PROT_READ, MAP_SHARED, file_fd, i*MAP_SIZE);
				memcpy(kernel_mem, mapped_mem, tmp);
				munmap(mapped_mem, tmp);
				while(ioctl(dev_fd, master_IOCTL_MMAP, tmp)<0&&errno==EAGAIN);
			}
			if (ioctl(dev_fd, 0x111, kernel_mem) == -1)
			{
				perror("ioclt server error\n");
				return 1;
			}
			munmap(kernel_mem,MAP_SIZE);
			break;
	}

	while(ioctl(dev_fd, master_IOCTL_EXIT)<0&&errno==EAGAIN); // end sending data, close the connection
	gettimeofday(&end, NULL);
	trans_time = (end.tv_sec - start.tv_sec)*1000 + (end.tv_usec - start.tv_usec)*0.0001;
	printf("Master: Transmission time: %lf ms, File size: %ld bytes\n", trans_time, file_size );

	close(file_fd);
	close(dev_fd);

	return 0;
}

size_t get_filesize(const char* filename)
{
    struct stat st;
    stat(filename, &st);
    return st.st_size;
}
