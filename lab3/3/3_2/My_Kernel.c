#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <asm/current.h>

#define procfs_name "Mythread_info"
#define BUFSIZE  1024
char buf[BUFSIZE]; //kernel buffer

static ssize_t Mywrite(struct file *fileptr, const char __user *ubuf, size_t buffer_len, loff_t *offset){
    /*Your code here*/

    ssize_t len;
    
    if(*offset > 0){
        return 0;
    }

    copy_from_user(buf, ubuf, buf_len);
    
    len = sprintf(buf+buf_len, "PID: %d, TID: %d, time: %lld\n", 
                        current->tgid, current->pid, 
                        current->utime/100/1000);
    // printk("My_Kernel: Data from the user: %s\n", buf);
    *offset += buf_len;
    *offset += len;
    buf_len += len;
    return len;

    /****************/
}


static ssize_t Myread(struct file *fileptr, char __user *ubuf, size_t buffer_len, loff_t *offset){
    /*Your code here*/
    
    if(*offset > 0 ){
        // only one read
        return 0;
    }

    ssize_t len = procfs_buffer_size;
    if (len > buffer_len){
        len = buffer_len;
    }

    if (copy_to_user(ubuf, buf, len)){
        return -EFAULT;
    }

    *offset += len;
    return len;

    /****************/
}

static struct proc_ops Myops = {
    .proc_read = Myread,
    .proc_write = Mywrite,
};

static int My_Kernel_Init(void){
    proc_create(procfs_name, 0644, NULL, &Myops);   
    pr_info("My kernel says Hi");
    return 0;
}

static void My_Kernel_Exit(void){
    pr_info("My kernel says GOODBYE");
}

module_init(My_Kernel_Init);
module_exit(My_Kernel_Exit);

MODULE_LICENSE("GPL");
