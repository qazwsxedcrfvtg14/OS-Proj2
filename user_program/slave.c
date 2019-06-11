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

#define slave_IOCTL_CREATESOCK 0x12345677
#define slave_IOCTL_MMAP 0x12345678
#define slave_IOCTL_EXIT 0x12345679

#define PAGE_SIZE 4096
#define BUF_SIZE 512
#define MAP_SIZE (PAGE_SIZE * 10)

int main (int argc, char* argv[])
{
	char buf[BUF_SIZE];
	int i, dev_fd, file_fd;// the fd for the device and the fd for the input file
	size_t file_size = 0, data_size = -1;
	ssize_t ret;
	char file_name[50];
	char method[20];
	char ip[20];
	struct timeval start;
	struct timeval end;
	double trans_time; //calulate the time between the device is opened and it is closed
	char *kernel_address, *file_address;
	void *mapped_mem, *kernel_mem;


	strcpy(file_name, argv[1]);
	strcpy(method, argv[2]);
	strcpy(ip, argv[3]);

	if( (dev_fd = open("/dev/slave_device", O_RDWR)) < 0)//should be O_RDWR for PROT_WRITE when mmap()
	{
		perror("failed to open /dev/slave_device\n");
		return 1;
	}
	gettimeofday(&start ,NULL);
	if( (file_fd = open (file_name, O_RDWR | O_CREAT | O_TRUNC)) < 0)
	{
		perror("failed to open input file\n");
		return 1;
	}

	if(ioctl(dev_fd, slave_IOCTL_CREATESOCK, ip) == -1)	//slave_IOCTL_CREATESOCK : connect to master in the device
	{
		perror("ioclt create slave socket error\n");
		return 1;
	}

    write(1, "ioctl success\n", 14);

	switch(method[0])
	{
		case 'f'://fcntl : read()/write()
			do
			{
				while((ret = read(dev_fd, buf, sizeof(buf)))<0&&errno==EAGAIN); // read from the the device
				write(file_fd, buf, ret); //write to the input file
				file_size += ret;
			}while(ret > 0);
			break;
		case 'm'://mmap
			kernel_mem = mmap(NULL, MAP_SIZE, PROT_READ, MAP_SHARED, dev_fd, 0);
			while(1)
			{
				while((ret = ioctl(dev_fd, slave_IOCTL_MMAP))<0&&errno==EAGAIN);
				if(ret==0)
					break;
				else if(ret<0)
				{
					perror("ioclt error\n");
					return 1;
				}
				posix_fallocate(file_fd, file_size, ret);
				size_t offset = (file_size/PAGE_SIZE)*PAGE_SIZE;
				size_t offlen = file_size-offset;
				mapped_mem = mmap(NULL, offlen+ret, PROT_WRITE, MAP_SHARED, file_fd, offset);
				memcpy(mapped_mem+offlen, kernel_mem, ret);
				munmap(mapped_mem,offlen+ret);
				file_size += ret;
			}
			ftruncate(file_fd, file_size);
			if(ioctl(dev_fd, 0x111, kernel_mem) == -1)
			{
				perror("ioclt error\n");
				return 1;
			}
			munmap(kernel_mem,MAP_SIZE);
			break;
	}



	if(ioctl(dev_fd, slave_IOCTL_EXIT) == -1)// end receiving data, close the connection
	{
		perror("ioclt client exits error\n");
		return 1;
	}
	gettimeofday(&end, NULL);
	trans_time = (end.tv_sec - start.tv_sec)*1000 + (end.tv_usec - start.tv_usec)*0.0001;
	printf("Slave: Transmission time: %lf ms, File size: %ld bytes\n", trans_time, file_size );


	close(file_fd);
	close(dev_fd);
	return 0;
}


