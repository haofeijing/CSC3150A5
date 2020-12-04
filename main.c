#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include "ioc_hw5.h"

MODULE_LICENSE("GPL");

#define PREFIX_TITLE "OS_AS5"
// typedef irqreturn_t (*irq_handler_t)(int, void *);
#define IRQ_NUM 1

// DMA
#define DMA_BUFSIZE 64
#define DMASTUIDADDR 0x0        // Student ID
#define DMARWOKADDR 0x4         // RW function complete
#define DMAIOCOKADDR 0x8        // ioctl function complete
#define DMAIRQOKADDR 0xc        // ISR function complete
#define DMACOUNTADDR 0x10       // interrupt count function complete
#define DMAANSADDR 0x14         // Computation answer
#define DMAREADABLEADDR 0x18    // READABLE variable for synchronize
#define DMABLOCKADDR 0x1c       // Blocking or non-blocking IO
#define DMAOPCODEADDR 0x20      // data.a opcode
#define DMAOPERANDBADDR 0x21    // data.b operand1
#define DMAOPERANDCADDR 0x25    // data.c operand2
void *dma_buf;

// Declaration for file operations
static ssize_t drv_read(struct file *filp, char __user *buffer, size_t, loff_t*);
static int drv_open(struct inode*, struct file*);
static ssize_t drv_write(struct file *filp, const char __user *buffer, size_t, loff_t*);
static int drv_release(struct inode*, struct file*);
static long drv_ioctl(struct file *, unsigned int , unsigned long );

// cdev file_operations
static struct file_operations fops = {
      owner: THIS_MODULE,
      read: drv_read,
      write: drv_write,
      unlocked_ioctl: drv_ioctl,
      open: drv_open,
      release: drv_release,
};

// in and out function
void myoutc(unsigned char data,unsigned short int port);
void myouts(unsigned short data,unsigned short int port);
void myouti(unsigned int data,unsigned short int port);
unsigned char myinc(unsigned short int port);
unsigned short myins(unsigned short int port);
unsigned int myini(unsigned short int port);

// Work routine
static struct work_struct *work_routine;

// For input data structure
struct DataIn {
    char a;
    int b;
    short c;
} *dataIn;

static int dev_major;
static int dev_minor;
static struct cdev *dev_cdevp;

static struct work_struct *work;

static int IRQ_Count = 0;
static irqreturn_t irq_handler(int irq, void *dev_id);


// Arithmetic funciton
static void drv_arithmetic_routine(struct work_struct* ws);


// Input and output data from/to DMA
void myoutc(unsigned char data,unsigned short int port) {
    *(volatile unsigned char*)(dma_buf+port) = data;
}
void myouts(unsigned short data,unsigned short int port) {
    *(volatile unsigned short*)(dma_buf+port) = data;
}
void myouti(unsigned int data,unsigned short int port) {
    *(volatile unsigned int*)(dma_buf+port) = data;
}
unsigned char myinc(unsigned short int port) {
    return *(volatile unsigned char*)(dma_buf+port);
}
unsigned short myins(unsigned short int port) {
    return *(volatile unsigned short*)(dma_buf+port);
}
unsigned int myini(unsigned short int port) {
    return *(volatile unsigned int*)(dma_buf+port);
}


static int drv_open(struct inode* ii, struct file* ff) {
	try_module_get(THIS_MODULE);
    	printk("%s:%s(): device open\n", PREFIX_TITLE, __func__);
	return 0;
}
static int drv_release(struct inode* ii, struct file* ff) {
	module_put(THIS_MODULE);
    	printk("%s:%s(): device close\n", PREFIX_TITLE, __func__);
	return 0;
}
static ssize_t drv_read(struct file *filp, char __user *buffer, size_t ss, loff_t* lo) {
	/* Implement read operation for your device */
	int readable = myini(DMAREADABLEADDR);
	while (readable == 0) {
		msleep(1000);
		readable = myini(DMAREADABLEADDR);
	}
	int answer = myini(DMAANSADDR);
	printk("%s:%s(): ans = %d\n", PREFIX_TITLE, __func__, answer);
	put_user(myini(DMAANSADDR), (int *)buffer);
	myouti(0, DMAREADABLEADDR);
	return 0;
}
static ssize_t drv_write(struct file *filp, const char __user *buffer, size_t ss, loff_t* lo) {
	/* Implement write operation for your device */
	int IOMode = myini(DMABLOCKADDR);
	// printk("%s:%s(): IO Mode is %d\n", PREFIX_TITLE, __func__, IOMode);

	struct DataIn data;
	get_user(data.a,(char *)buffer);
	get_user(data.b,(int *)buffer + 1);
	get_user(data.c,(int *)buffer + 2);
	
	myoutc(data.a,DMAOPCODEADDR);
	myouti(data.b,DMAOPERANDBADDR);
	myouts(data.c,DMAOPERANDCADDR);

	INIT_WORK(work, drv_arithmetic_routine);
	printk("%s:%s(): queue work\n", PREFIX_TITLE, __func__);

	// Decide io mode
	if(IOMode) {
		// Blocking IO
		printk("%s:%s(): block\n", PREFIX_TITLE, __func__);
		schedule_work(work);
		flush_scheduled_work();
    	} 
	else {
		// Non-locking IO
		printk("%s,%s(): non-blocking\n",PREFIX_TITLE, __func__);
		schedule_work(work);
   	 }
	return 0;
}
static long drv_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
	/* Implement ioctl setting for your device */
	int value;
	get_user(value, (int *)arg);
	int readable = myini(DMAREADABLEADDR);
	switch (cmd) {
		case HW5_IOCSETSTUID:
			myouti(value, DMASTUIDADDR);
			printk("%s,%s(): My STUID is = %d\n",PREFIX_TITLE, __func__, value);
			break;
		case HW5_IOCSETRWOK: 
			myouti(value, DMARWOKADDR);	
			if (value == 1) {
			printk("%s,%s(): RW OK \n",PREFIX_TITLE, __func__);
			}
			break;
		case HW5_IOCSETIOCOK: 
			myouti(value, DMAIOCOKADDR);
			if (value == 1) {
				printk("%s,%s(): IOC OK \n",PREFIX_TITLE, __func__);	
			}
			break;
		case HW5_IOCSETIRQOK: 
		// for bonus
			break;
		case HW5_IOCSETBLOCK:
			myouti(value, DMABLOCKADDR);
			if (value == 1) {
				printk("%s,%s(): Blocking IO \n",PREFIX_TITLE, __func__);	
			} else {
				printk("%s,%s(): Non-Blocking IO \n",PREFIX_TITLE, __func__);
			}
			break;
		case HW5_IOCWAITREADABLE:				
			while(readable == 0){
				msleep(5000);
				readable = myini(DMAREADABLEADDR);
			}
			printk("%s:%s(): Wait readable: %d",PREFIX_TITLE, __func__, readable);
			put_user(readable, (int *)arg);
			break;	
	}

	return 0;
}

static int prime(int base, short nth){
	int fnd=0;
    int i, num, isPrime;

    num = base;
    while(fnd != nth) {
        isPrime=1;
        num++;
        for(i=2;i<=num/2;i++) {
            if(num%i == 0) {
                isPrime=0;
                break;
            }
        }
        
        if(isPrime) {
            fnd++;
        }
    }
    return num;
}

static void drv_arithmetic_routine(struct work_struct* ws) {
	/* Implement arthemetic routine */
	struct DataIn data;
    int ans;

    data.a = myinc(DMAOPCODEADDR);
    data.b = myini(DMAOPERANDBADDR);
    data.c = myins(DMAOPERANDCADDR);

    switch(data.a) {
        case '+':
            ans=data.b+data.c;
            break;
        case '-':
            ans=data.b-data.c;
            break;
        case '*':
            ans=data.b*data.c;
            break;
        case '/':
            ans=data.b/data.c;
            break;
        case 'p':
            ans = prime(data.b, data.c);
            break;
        default:
            ans=0;
    }

	myouti(ans, DMAANSADDR);
	myouti(1, DMAREADABLEADDR);
	printk("%s:%s():%d %c %d = %d\n\n",PREFIX_TITLE, __func__, data.b, data.a, data.c, ans);
}

static irqreturn_t irq_handler(int irq, void *dev_id) {
	// printk("%s:%s(): IQR occurred \n",PREFIX_TITLE, __func__);
	IRQ_Count += 1;
	return IRQ_HANDLED;
}

static int __init init_modules(void) {
    
	printk("%s:%s():...............Start...............\n", PREFIX_TITLE, __func__);

	/* Request IRQ */
	free_irq(IRQ_NUM, NULL);
	int irq = request_irq(IRQ_NUM, irq_handler, IRQF_SHARED, "myirq", (void *)(irq_handler));
	printk("%s:%s(): request irq %d return %d\n",PREFIX_TITLE,__FUNCTION__, IRQ_NUM, irq);


	/* Register chrdev */ 
	dev_t dev;
	int ret = 0;

	ret = alloc_chrdev_region(&dev, 0, 1, "mydev");
	if(ret)
	{
		printk("Cannot alloc chrdev\n");
		return ret;
	}
	
	dev_major = MAJOR(dev);
	dev_minor = MINOR(dev);
	printk("%s:%s():register chrdev(%d,%d)\n",PREFIX_TITLE,__FUNCTION__,dev_major,dev_minor);

	/* Init cdev and make it alive */

	dev_cdevp = cdev_alloc();

	cdev_init(dev_cdevp, &fops);
	dev_cdevp->owner = THIS_MODULE;
	ret = cdev_add(dev_cdevp, MKDEV(dev_major, dev_minor), 1);
	if(ret < 0)
	{
		printk("Add chrdev failed\n");
		return ret;
	}

	/* Allocate DMA buffer */

	dma_buf = kzalloc(DMA_BUFSIZE, GFP_KERNEL);
	printk("%s:%s():allocate dma buffer\n",PREFIX_TITLE,__FUNCTION__);


	/* Allocate work routine */
	work = kmalloc(sizeof(typeof(*work)), GFP_KERNEL);

	return 0;
}

static void __exit exit_modules(void) {

	/* Free IRQ */
	free_irq(IRQ_NUM, (void *)(irq_handler));
	printk("%s:%s(): interrupt count = %d\n", PREFIX_TITLE, __FUNCTION__, IRQ_Count);

	/* Free DMA buffer when exit modules */
	kfree(dma_buf);
	printk("%s:%s():free dma buffer\n",PREFIX_TITLE, __FUNCTION__);


	/* Delete character device */

	dev_t dev;
	
	dev = MKDEV(dev_major, dev_minor);
	cdev_del(dev_cdevp);

	printk("%s:%s():unregister chrdev\n",PREFIX_TITLE,__FUNCTION__);
	unregister_chrdev_region(dev, 1);


	/* Free work routine */
	kfree(work);


	printk("%s:%s():..............End..............\n", PREFIX_TITLE, __func__);
}

module_init(init_modules);
module_exit(exit_modules);
