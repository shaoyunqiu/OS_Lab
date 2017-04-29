# Lab6 Report
2014011426 计45 邵韵秋

## 实验内容
### 练习0：填写已有的实验报告
之前都是采取全文件搜索LAB，将之前每一次的代码依次复制粘贴进去，但感觉随着lab的增多，每次粘贴代码变得很繁琐，且容易出错，在韦毅龙同学的提示下，采取git diff和patch命令完成自动合并。
具体命令格式为：`git diff <.. ..> --relative=labcodes/lab5 | patch -d labcodes/lab6 -p1` 
其中`<.. ..>`填写两个版本号，在本次实验中应填写Lab5开始之前和Lab5结束之后的两个文件夹，在根目录中运行该命令即可。会有自动合并失败的部分，记录会分别存储在.rej文件中，再通过手动合并完成，且此次出现rej的地方都是Lab6需要进行一些更新的地方。
一个为`proc.c`文件中的`alloc_proc`函数，需要对一些新增的变量进行初始化，主要是之后Round Robin和Stride算法会用到，更新代码如下：
```
proc->rq = NULL ;
list_init(&(proc->run_link)) ;
proc->time_slice = 0 ;
skew_heap_init(&(proc->lab6_run_pool)) ;
proc->lab6_stride = 0 ;
proc->lab6_priority = 0 ;
```
另一处是在`trap.c`文件中对时钟中断的处理处，此时对于时钟中断的处理交给sched_class_proc_tick函数，而不需要自己单独处理，修改代码如下：
```
		ticks ++;
        assert(current != NULL) ;
        /*if (ticks % TICK_NUM == 0) {
            assert(current != NULL);
            current->need_resched = 1;
        }*/
        /* LAB6 YOUR CODE */
        /* you should upate you lab5 code
         * IMPORTANT FUNCTIONS:
	     * sched_class_proc_tick
         */
         sched_class_proc_tick(current) ;
        break;
```
需要注意的是，直接这么修改会产生编译问题，此处的解决方式是将`sched.c`文件中sched_class_proc_tick的static修饰去掉。

现在运行`make grade`命令，可以发现除了priority测试无法通过外，其余测试均可以通过。

### 练习1：使用 Round Robin 调度算法（不需要编码）

> 请理解并分析sched_class中各个函数指针的用法，并结合Round Robin 调度算法描ucore的调度执行过程

sched_class在文件`kern/scheduel/sched.h`中定义，内容如下：
```
struct sched_class {
    // the name of sched_class
    const char *name;
    // Init the run queue
    void (*init)(struct run_queue *rq);
    // put the proc into runqueue, and this function must be called with rq_lock
    void (*enqueue)(struct run_queue *rq, struct proc_struct *proc);
    // get the proc out runqueue, and this function must be called with rq_lock
    void (*dequeue)(struct run_queue *rq, struct proc_struct *proc);
    // choose the next runnable task
    struct proc_struct *(*pick_next)(struct run_queue *rq);
    // dealer of the time-tick
    void (*proc_tick)(struct run_queue *rq, struct proc_struct *proc);
};
```
name: 调度算法的名称
init: 对就绪队列进行初始化
enqueue: 入队，将一个进程(PCB)放入就绪队列中
dequeue: 出队，将选出的一个进程(PCB)从就绪队列中取出
pick_next: 从就绪队列中选择下一个运行的进程，返回其进程控制块(PCB)
proc_tick: 处理时钟中断

ucore调度算法的执行过程：调度的框架以及调度点与之前lab4/5中没有太大差别，主要是负责调度的schedule函数使用了Round Robin算法。
ucore主要使用schedule函数进行进程调度，调度点在`trap.c`的trap()函数，`proc.c`中do_exit(),do_wait(),init_main()和cpu_idle()中，分别对应时间片用完，进程退出，当前进程进入等待阻塞状态和init_main,cpu_idle()这几个调度点，最后两个主要是在初始化和所有进程退出后回收资源，以及寻找就绪进程时会用到。
schedule()函数，具体流程为：
将当前need_sched的标志置0，如果当前进程仍未RUNABLE（即就绪）状态，将其加入就绪队列中，然后从就绪队列中选取下一个进程，如果找到非空，将其从就绪队列中取出，如果找不到就绪进程，则将之后将运行idle,运行新的进程。
对于Round Robin算法而言，就绪队列采用双向链表的方式存储，入队时，将进程加入队头，将就绪进程的数目加1，并将该进程的时间片复位。出队时就将特定元素从双向链表中删除，选择下一个运行进程就取当前链表中指针指向的下一个位置对应的元素。对于时钟中断的处理，每产生一次时钟中断，就将进程块对应的时间片-1，若时间片小于0，就将need_sched的标志置1.

> 请在实验报告中简要说明如何设计实现”多级反馈队列调度算法“

将struct run_queue中的就绪队列从一个改为多个，并在时钟中断处理函数proc_tick中对优先级进行调整，如果一个时间片内无法执行完成则降低优先级，如果其等待时间很长，则升高优先级。

### 练习2: 实现 Stride Scheduling 调度算法
> 首先需要换掉RR调度器的实现，即用default_sched_stride_c覆盖default_sched.c。然后根据此文件和后续文档对Stride度器的相关描述，完成Stride调度算法的实现。

实现时将文件`default_sched_stride_c`文件中的内容全部拷贝且覆盖掉`default_sched.c`中的内容。使用skew_heap的数据结构实现。
首先需要考虑的是BIG_STRIDE的取值，根据实验指导书基本思路中的提示，已知有`STRIDE_MAX – STRIDE_MIN <= BIG_STRIDE`,所以我们需要取合适的BIG_STRIDE,使得两数只差始终保持在机器整数表示的范围之内，因此可以去32位无符号整数的最大值`2^32-1`即可。

`stride_init`: 初始化函数，rq对应的lab6_run_pool设为NULL, proc_num初始化为0.
`stride_enqueue`: 将进程块proc入堆：使用skew_heap_insert函数将进程插入，并更新proc的time_slice数值，将rq的计数器proc_num加1。
`stride_dequeue`: 将进程块从堆中删除：使用skew_heap_remove函数即可，需要更新rq计数器proc_num的值，将其减1即可。
`stride_pick_next`: 选择下一个占用cpu的进程：先判断堆是否为空，若为空，返回NULL, 否则通过le2proc函数获得堆顶对应的proc, 更新其lab_stride值，具体方法为加上步长，其中步长与priority呈反比，此处用BIG_STRIDE/priority计算而得。
`stride_proc_tick`: 处理时钟中断的函数：如果当前时间片的值大于0，将时间片减1，如果时间片的值为0，将need_resched的赋值为1，表明可以再被调度。

最后，运行`make grade`可以发现所有测试全部OK，包括priority的测试。

## 与答案的区别：
只有练习2有codes_answer，主要的区别在于答案考虑了使用双向队列和skew_heap两种实现方式，而此处我只实现了使用skew_heap这种方式。因为对于stride的算法而言，使用skew_heap的结构可以有效降低时间复杂度到log(n).

在大数的选取方面，答案使用的是0x7FFFFFFF表示，而我使用的是((1U<<31)-1)的表示，二者等价。

有一点特别不能理解，在`trap.c`中对时间中断的处理上，答案中只有两句代码：
```
ticks ++;
assert(current != NULL);
break;
```
而没有调用sched_class_proc_tick函数，在我的代码中，是通过调用这个函数来进行时钟中断的处理，并且为了使`trap.c`能顺利调用这个函数，将`sched.c`中其static属性去掉了，不知道这样对后续试验会不会有什么影响，也不是很清楚如果答案不调用这个函数，那么他是在哪里处理时钟中断的。

## 实验涉及到的知识点：
1. Round Robin算法
2. Stride 算法
3. 进程调度的框架

## 实验未涉及到的知识点：
1. 调度算法性能的比较
2. 不可抢占的算法（例如：短进程优先算法，最高相应比优先算法等）
3. 实时调度，多处理机调度方面的知识。
4. 优先级反置的问题以及可能的解决方式。
