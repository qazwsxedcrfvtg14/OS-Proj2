#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/net.h>
#include <net/sock.h>
#include <asm/processor.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <linux/kthread.h>
#include <linux/mutex.h>


#ifndef VM_RESERVED
#define VM_RESERVED   (VM_DONTEXPAND | VM_DONTDUMP)
#endif

#define slave_IOCTL_CREATESOCK 0x12345677
#define slave_IOCTL_MMAP 0x12345678
#define slave_IOCTL_EXIT 0x12345679


#define BUF_SIZE 512
#define MAP_SIZE (PAGE_SIZE * 10)
#define ASYNC_BUF_SIZE (32*MAP_SIZE)




struct dentry  *file1;//debug file

typedef struct socket * ksocket_t;

//functions about kscoket are exported,and thus we use extern here
extern ksocket_t ksocket(int domain, int type, int protocol);
extern int kconnect(ksocket_t socket, struct sockaddr *address, int address_len);
extern ssize_t krecv(ksocket_t socket, void *buffer, size_t length, int flags);
extern int kclose(ksocket_t socket);
extern unsigned int inet_addr(char* ip);
extern char *inet_ntoa(struct in_addr *in); //DO NOT forget to kfree the return pointer

static int __init slave_init(void);
static void __exit slave_exit(void);

int slave_close(struct inode *inode, struct file *filp);
int slave_open(struct inode *inode, struct file *filp);
static long slave_ioctl(struct file *file, unsigned int ioctl_num, unsigned long ioctl_param);
ssize_t receive_msg(struct file *filp, char *buf, size_t count, loff_t *offp );
static int my_mmap(struct file *filp, struct vm_area_struct *vma);

static mm_segment_t old_fs;
static ksocket_t sockfd_cli;//socket to the master server
static struct sockaddr_in addr_srv; //address of the master server

void mmap_open(struct vm_area_struct *vma) { /* do nothing */ }
void mmap_close(struct vm_area_struct *vma) { /* do nothing */ }

//file operations
static struct file_operations slave_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = slave_ioctl,
	.open = slave_open,
	.read = receive_msg,
	.mmap = my_mmap,
	.release = slave_close
};

//device info
static struct miscdevice slave_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "slave_device",
	.fops = &slave_fops
};

struct vm_operations_struct mmap_vm_ops = {
	.open = mmap_open,
	.close = mmap_close,
};

struct data_queue{
	char data[MAP_SIZE];
	int begin,end;
	int finish;
	char async_buf[ASYNC_BUF_SIZE];
};

static int __init slave_init(void)
{
	int ret;
	file1 = debugfs_create_file("slave_debug", 0644, NULL, NULL, &slave_fops);

	//register the device
	if( (ret = misc_register(&slave_dev)) < 0){
		printk(KERN_ERR "misc_register failed!\n");
		return ret;
	}

	printk(KERN_INFO "slave has been registered!\n");

	return 0;
}

static void __exit slave_exit(void)
{
	misc_deregister(&slave_dev);
	printk(KERN_INFO "slave exited!\n");
	debugfs_remove(file1);
}

struct mutex async_mutex;
struct task_struct *async_kthread;

int slave_close(struct inode *inode, struct file *filp)
{
	wake_up_process(async_kthread);
	kthread_stop(async_kthread);
	return 0;
}

static int async_recv(void *private_data)
{
	struct data_queue* data=private_data;
	int begin,end,len;
	while(!kthread_should_stop()){
		mutex_lock(&async_mutex);
		begin=data->begin;
		end=data->end;
		if((begin-end-1+ASYNC_BUF_SIZE)%ASYNC_BUF_SIZE == 0 || data->finish==1)
		{
			mutex_unlock(&async_mutex);
			set_current_state(TASK_INTERRUPTIBLE);
			if(kthread_should_stop())break;
			continue;
		}
		else
		{
			mutex_unlock(&async_mutex);
		}
		if(begin<=end)
			len = krecv(sockfd_cli, (data->async_buf)+end, begin==0?ASYNC_BUF_SIZE-1-end:ASYNC_BUF_SIZE-end , 0);
		else
			len = krecv(sockfd_cli, (data->async_buf)+end, begin-end-1 , 0);
		mutex_lock(&async_mutex);
		data->end=(end+len)%ASYNC_BUF_SIZE;
		if(len==0)data->finish=1;
		mutex_unlock(&async_mutex);
	}
	kfree(private_data);
	return 0;
}

int slave_open(struct inode *inode, struct file *filp)
{
	filp->private_data = kmalloc(sizeof(struct data_queue), GFP_KERNEL);
	((struct data_queue*)(filp->private_data))->begin=0;
	((struct data_queue*)(filp->private_data))->end=0;
	((struct data_queue*)(filp->private_data))->finish=0;
	mutex_init(&async_mutex);
	async_kthread=kthread_create(async_recv,filp->private_data,"slave_async_recv");
	return 0;
}
static ssize_t async_recv_msg(struct file *file, void *msg,size_t count){
	int begin,end,fin;
	long ret;
	int size;
	mutex_lock(&async_mutex);
	begin = ((struct data_queue*)(file->private_data))->begin;
	end = ((struct data_queue*)(file->private_data))->end;
	fin = ((struct data_queue*)(file->private_data))->finish;
	mutex_unlock(&async_mutex);
	if(begin==end)
	{
		if(fin==1)
			ret=0;
		else
			ret=-EAGAIN;
	}
	else
	{
		size=count;
		if(begin<end)
		{
			if(size>end-begin)
				size=end-begin;
		}
		else
		{
			if(size>ASYNC_BUF_SIZE-begin)
				size=ASYNC_BUF_SIZE-begin;
		}
		memcpy(msg,((struct data_queue*)(file->private_data))->async_buf+begin,size);
		mutex_lock(&async_mutex);
		((struct data_queue*)(file->private_data))->begin=(begin+size)%ASYNC_BUF_SIZE;
		mutex_unlock(&async_mutex);
		ret = size;
		wake_up_process(async_kthread);
	}
	return ret;
}

static long slave_ioctl(struct file *file, unsigned int ioctl_num, unsigned long ioctl_param)
{
	long ret = -EINVAL;

	int addr_len ;
	unsigned int i;
	size_t len, data_size = 0;
	char *tmp, ip[20], buf[BUF_SIZE];
	struct page *p_print;
	unsigned char *px;

    pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
    pte_t *ptep, pte;
	old_fs = get_fs();
	set_fs(KERNEL_DS);

    printk("slave device ioctl");

	switch(ioctl_num){
		case slave_IOCTL_CREATESOCK:// create socket and connect to master
			printk("slave device ioctl create socket");

			if(copy_from_user(ip, (char*)ioctl_param, sizeof(ip)))
				return -ENOMEM;

			sprintf(current->comm, "ksktcli");

			memset(&addr_srv, 0, sizeof(addr_srv));
			addr_srv.sin_family = AF_INET;
			addr_srv.sin_port = htons(2325);
			addr_srv.sin_addr.s_addr = inet_addr(ip);
			addr_len = sizeof(struct sockaddr_in);

			sockfd_cli = ksocket(AF_INET, SOCK_STREAM, 0);
			printk("sockfd_cli = 0x%p  socket is created\n", sockfd_cli);
			if (sockfd_cli == NULL)
			{
				printk("socket failed\n");
				return -1;
			}
			if (kconnect(sockfd_cli, (struct sockaddr*)&addr_srv, addr_len) < 0)
			{
				printk("connect failed\n");
				return -1;
			}
			tmp = inet_ntoa(&addr_srv.sin_addr);
			printk("connected to : %s %d\n", tmp, ntohs(addr_srv.sin_port));
			kfree(tmp);
			printk("kfree(tmp)");
			wake_up_process(async_kthread);
			ret = 0;
			break;
		case slave_IOCTL_MMAP:
			ret = async_recv_msg(file,((struct data_queue*)(file->private_data))->data,MAP_SIZE);
			break;

		case slave_IOCTL_EXIT:
			if(kclose(sockfd_cli) == -1)
			{
				printk("kclose cli error\n");
				return -1;
			}
			ret = 0;
			break;
		default:
            pgd = pgd_offset(current->mm, ioctl_param);
			p4d = p4d_offset(pgd, ioctl_param);
			pud = pud_offset(p4d, ioctl_param);
			pmd = pmd_offset(pud, ioctl_param);
			ptep = pte_offset_kernel(pmd , ioctl_param);
			pte = *ptep;
			printk("slave: %lX\n", pte);
			ret = 0;
			break;
	}
    set_fs(old_fs);

	return ret;
}

ssize_t receive_msg(struct file *filp, char *buf, size_t count, loff_t *offp )
{
//call when user is reading from this device
	char msg[BUF_SIZE];
	size_t len;
	len=async_recv_msg(filp,msg,sizeof(msg));
	if(copy_to_user(buf, msg, len))
		return -ENOMEM;
	return len;
}


static int my_mmap(struct file *filp, struct vm_area_struct *vma)
{
	vma->vm_pgoff = virt_to_phys(filp->private_data)>>PAGE_SHIFT;
	if(remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, vma->vm_end - vma->vm_start, vma->vm_page_prot) < 0)
	{
		pr_err("could not map the address area\n");
		return -EIO;
	}
	vma->vm_ops = &mmap_vm_ops;
	vma->vm_flags |= VM_RESERVED;
	vma->vm_private_data = filp->private_data;
	return 0;
}


module_init(slave_init);
module_exit(slave_exit);
MODULE_LICENSE("GPL");
