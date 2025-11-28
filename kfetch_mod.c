#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/utsname.h>
#include <linux/cpumask.h>
#include <linux/sysinfo.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>

#define KFETCH_NUM_INFO 6
#define KFETCH_RELEASE   (1 << 0)
#define KFETCH_NUM_CPUS  (1 << 1)
#define KFETCH_CPU_MODEL (1 << 2)
#define KFETCH_MEM       (1 << 3)
#define KFETCH_UPTIME    (1 << 4)
#define KFETCH_NUM_PROCS (1 << 5)
#define KFETCH_FULL_INFO ((1 << KFETCH_NUM_INFO) - 1)
#define DEVICE_NAME "kfetch"

static dev_t dev_num;
static struct class *cl = NULL;
static struct cdev kfetch_cdev;
static DEFINE_MUTEX(kfetch_mutex);
static char kfetch_buf[1024];

static unsigned int mask = KFETCH_FULL_INFO;


static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char __user *, size_t, loff_t *);

const static struct file_operations kfetch_ops = 
{
    .owner   = THIS_MODULE,
    .read    = device_read,
    .write   = device_write,
    .open    = device_open,
    .release = device_release,
};

static int __init kfetch_init(void)
{
    int ret;

    // 初始化互斥鎖
    mutex_init(&kfetch_mutex);

    // 分配設備號
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) 
    {
        //printk(KERN_ALERT "kfetch: Failed to allocate major number\n");
        return ret;
    }
    //printk(KERN_INFO "kfetch: Registered correctly with major number %d\n", MAJOR(dev_num));

    // 初始化 cdev 結構
    cdev_init(&kfetch_cdev, &kfetch_ops);
    kfetch_cdev.owner = THIS_MODULE;

    // 將 cdev 添加到系統中
    ret = cdev_add(&kfetch_cdev, dev_num, 1);
    if (ret < 0) 
    {
        unregister_chrdev_region(dev_num, 1);
        //printk(KERN_ALERT "kfetch: Failed to add cdev\n");
        return ret;
    }

    // 創建設備類
    cl = class_create(THIS_MODULE, DEVICE_NAME);
    if (IS_ERR(cl)) 
    {
        cdev_del(&kfetch_cdev);
        unregister_chrdev_region(dev_num, 1);
        //printk(KERN_ALERT "kfetch: Failed to create class\n");
        return PTR_ERR(cl);
    }

    // 創建設備
    if (IS_ERR(device_create(cl, NULL, dev_num, NULL, DEVICE_NAME))) {
        class_destroy(cl);
        cdev_del(&kfetch_cdev);
        unregister_chrdev_region(dev_num, 1);
        //printk(KERN_ALERT "kfetch: Failed to create device\n");
        return PTR_ERR(cl);
    }

    //printk(KERN_INFO "kfetch: Device class created correctly\n");
    return 0;
}
static void __exit kfetch_exit(void)
{
    device_destroy(cl, dev_num);
    class_destroy(cl);
    cdev_del(&kfetch_cdev);
    unregister_chrdev_region(dev_num, 1);
    mutex_destroy(&kfetch_mutex);
    //printk(KERN_INFO "kfetch: Module unloaded and resources freed\n");
}
static int device_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&kfetch_mutex)) {
        //printk(KERN_ALERT "kfetch: Device in use by another process\n");
        return -EBUSY;
    }
    //printk(KERN_INFO "kfetch: Device opened\n");
    return 0;
}

static int device_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&kfetch_mutex);
    //printk(KERN_INFO "kfetch: Device closed\n");
    return 0;
}

static ssize_t device_write(struct file *filp, const char __user *buffer, size_t len, loff_t *offset)
{
    int mask_info;

    if (copy_from_user(&mask_info, buffer, len)) {
        //printk(KERN_ALERT "kfetch: Failed to copy data from user\n");
        return -EFAULT;
    }

    // 驗證 mask
    if (mask_info > KFETCH_FULL_INFO) {
        //printk(KERN_ALERT "kfetch: Invalid mask value\n");
        return -EINVAL;
    }

    mask = mask_info;
    // printk(KERN_INFO "kfetch: Info mask set to 0x%x\n", mask);

    return 0;
}

static ssize_t device_read(struct file *filp, char __user *buffer, size_t len, loff_t *offset)
{
    size_t len_written = 0;
    struct sysinfo si;
    char *logo[8] = {
     "                     ",
     "          .-.        ",
     "         (.. |       ",
     "         <>  |       ",
     "        / --- \\      ",
     "       ( |   | |     ",
     "     |\\\\_)___/\\)/\\   ",
     "    <__)------(__/   "};
    struct task_struct *task;
    unsigned long uptime_seconds;
    unsigned long uptime_minutes;
    int total_procs = 0;
    int ret;
    char char_buf[200];
    strcpy(kfetch_buf, "");
    
    // 獲取系統信息
    struct new_utsname *uts = utsname();
    si_meminfo(&si);
    uptime_seconds = ktime_divns(ktime_get_coarse_boottime(), NSEC_PER_SEC);
    uptime_minutes = uptime_seconds / 60;
    
    // 計算進程數量
    for_each_process(task)
        total_procs++;


    // 迭代每一行 Logo 並添加對應資訊
    for(int i = 0; i < 8; ++i) {
        // 清空 padded_logo 並填充空格
        strcat(kfetch_buf, logo[i]);
        // 添加對應的資訊
        switch(i) {
            case 0:
                // 主機名稱是必須的
                strcat(kfetch_buf, uts->nodename);
                strcat(kfetch_buf, "\n");
                break;
            case 1:
                // 分隔線，長度等於主機名稱
                {
                    int hostname_len = strlen(uts->nodename);
                    for(int j = 0; j < hostname_len; j++) {
                        strcat(kfetch_buf, "-");
                    }
                    strcat(kfetch_buf, "\n");
                }
                break;
            case 2:
                if(mask & KFETCH_RELEASE) 
                {
                    strcat(kfetch_buf, "Kernel: ");
                    strcat(kfetch_buf, uts->release);
                    strcat(kfetch_buf, "\n");
                    
                }
                else
                {
                    strcat(kfetch_buf, "\n");
                } 
                break;
            case 3:
                if(mask & KFETCH_CPU_MODEL) {
                    // 從 cpu_info 獲取 CPU 型號名稱
                    strcat(kfetch_buf, "CPU:    ");
                    strcat(kfetch_buf, cpu_data(0).x86_model_id);
                    strcat(kfetch_buf, "\n");
                }
                else
                {
                    strcat(kfetch_buf, "\n");
                }  
                break;
            case 4:
                if(mask & KFETCH_NUM_CPUS) {
                    sprintf(char_buf, "CPUs:   %d",num_online_cpus()); 
                    strcat(kfetch_buf, char_buf);
                    strcat(kfetch_buf, " / ");
                    sprintf(char_buf, "%d",num_present_cpus());
                    strcat(kfetch_buf, "\n");
                }
                else
                {
                    strcat(kfetch_buf, "\n");
                } 
                break;
            case 5:
                if(mask & KFETCH_MEM) {
                    unsigned long free_memory = (si.freeram * si.mem_unit) / (1024 * 1024);
                    unsigned long total_memory = (si.totalram * si.mem_unit) / (1024 * 1024);
                    sprintf(char_buf, "Mem:    %lu",free_memory);
    		        strcat(kfetch_buf, char_buf);
    		        strcat(kfetch_buf, " MB / ");
    		        sprintf(char_buf, "%lu",total_memory);
    		        strcat(kfetch_buf, char_buf);
    		        strcat(kfetch_buf, " MB");
                    strcat(kfetch_buf, "\n");
                }
                else
                {
                    strcat(kfetch_buf, "\n");
                } 
                break;
            case 6:
                if(mask & KFETCH_NUM_PROCS) {
                    sprintf(char_buf, "Procs:  %d",total_procs);
                    strcat(kfetch_buf, char_buf);                   
                    strcat(kfetch_buf, "\n");
                }
                else
                {
                    strcat(kfetch_buf, "\n");
                }  
                break;
            case 7:
                if(mask & KFETCH_UPTIME) {
                    sprintf(char_buf, "uptime: %lu",uptime_minutes);
    		        strcat(kfetch_buf, char_buf);
    		        strcat(kfetch_buf, " mins");
                    strcat(kfetch_buf, "\n");
                }
                else
                {
                    strcat(kfetch_buf, "\n");
                    strcat(kfetch_buf, "\n");                  
                }  
                break;
            default:
                strcat(kfetch_buf, "\n");
                strcat(kfetch_buf, "\n");
                break;
        }
    }
    // 獲取緩衝區長度
    size_t kfetch_buf_len = strlen(kfetch_buf);
    // 複製數據到user space
    if (copy_to_user(buffer, kfetch_buf, kfetch_buf_len)) {
        //printk(KERN_ALERT "kfetch: Failed to copy data to user\n");
        return -EFAULT;
    }
    
    return sizeof(kfetch_buf);
}



module_init(kfetch_init);
module_exit(kfetch_exit);

MODULE_LICENSE("GPL");

