# LAB7 REPORT
2014011426 计45 邵韵秋

## 实验内容
### 练习0：填写已有实验
沿用lab6的做法，采取git diff和patch命令完成自动合并。
具体命令格式为：`git diff <.. ..> --relative=labcodes/lab6 | patch -d labcodes/lab7 -p1` 
其中`<.. ..>`填写两个版本号，在本次实验中应填写Lab6开始之前和Lab6结束之后的两个版本号(本实验中为8934和1a96)，在根目录中运行该命令即可。会有自动合并失败的部分，记录会分别存储在.rej文件中，再通过手动合并完成，且此次出现rej的地方都是Lab7需要进行一些更新的地方。
本次出现rej的地方在`kern\trap.c`文件的trap_dispatch()函数对于时钟中断的处理部分，修改后的代码如下：
```
	case IRQ_OFFSET + IRQ_TIMER:
        /* LAB7 YOUR CODE */
        /* you should upate you lab6 code
         * IMPORTANT FUNCTIONS:
	     * run_timer_list
         */
        ticks ++ ;
        assert(current != NULL) ;
        run_timer_list() ;
        break;
```
因为Lab7中单独实现了timer_t结构用来完成计时器的相关功能，其中run_timer_list()函数用于更新当前系统时间点，遍历当前所有处在系统管理内的计时器，找出所有应该激活的计数器，并激活它们。该过程在且只在每次计时器中断时被调用。所以产生时钟中断时，应调用run_timer_list()函数进行处理，而不必使用之前手动处理的方式。

### 练习1: 理解内核级信号量的实现和基于内核级信号量的哲学家就餐问题

> 请在实验报告中给出内核级信号量的设计描述，并说其大致执行流流程。

本实验中信号量的定义在`kern\sem.h`中，通过结构变量semaphore_t表示，包括信号量的当前值value和信号量对应的等待队列wait_queue.
原理中信号量的P(), V()对应实验中的down()和up()函数，而这两个函数具体的是由`__down(semaphore_t *sem, uint32_t wait_state)`和`__up(semaphore_t *sem, uint32_t wait_state)`这两个函数实现。
对比原理，对相应操作的实现解释如下：
原理伪代码：(P操作)
```
Semaphore::P(){
	sem -- ;
	if(sem < 0){
		Add thread t to q ;
		block(t) ;
	}
}
```
ucore实现代码：
```
static __noinline uint32_t __down(semaphore_t *sem, uint32_t wait_state) {
    bool intr_flag;
    local_intr_save(intr_flag);
    if (sem->value > 0) {
        sem->value --;
        local_intr_restore(intr_flag);
        return 0;
    }
    wait_t __wait, *wait = &__wait;
    wait_current_set(&(sem->wait_queue), wait, wait_state);
    local_intr_restore(intr_flag);

    schedule();

    local_intr_save(intr_flag);
    wait_current_del(&(sem->wait_queue), wait);
    local_intr_restore(intr_flag);

    if (wait->wakeup_flags != wait_state) {
        return wait->wakeup_flags;
    }
    return 0;
}
```
与原理所示过程基本相同，但在具体实现时有更多需要考虑的细节。因为涉及到对共享变量value的操作，所以一开始需要用`local_intr_save(intr_flag)`指令关中断，保证互斥。如果value > 0对其执行-1操作，且-1之后的值必定>=0,所以直接开中断退出即可。否则，对应于原理中sem<0条件满足后的流程，将当前进程通过	`wait_current_set`函数加入等待队列中去，开中断，执行`schedule()`进行进程调度。直到被V操作唤醒，将当前进程从等待队列中删除(注：此处也需要关闭中断，保证操作的原子性)。最后判断当前被唤醒的进程是否是因为信号量进入等待状态，若不是，则将异常值返回，否则返回0正常退出。

原理伪代码：(V操作)
```
Semaphore::V(){
	sem ++ ;
	if(sem <= 0){
		Remove thread t from q ;
		wakeup(t) ;
	}
}
```
ucore 实现代码：
```
static __noinline void __up(semaphore_t *sem, uint32_t wait_state) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        wait_t *wait;
        if ((wait = wait_queue_first(&(sem->wait_queue))) == NULL) {
            sem->value ++;
        }
        else {
            assert(wait->proc->wait_state == wait_state);
            wakeup_wait(&(sem->wait_queue), wait, wait_state, 1);
        }
    }
    local_intr_restore(intr_flag);
}
```
V操作的实现与原理所示也基本相同。同样的，因为涉及到对共享变量的访问和修改，所以处理之前需要关中断。此处先判断由于信号量进入等待状态的等待队列是否为空，若为空，说明没有进程处于等待状态，资源空闲则将信号量value+1，直接开中断退出，否则将等待队列中的第一个进程唤醒，打开中断并退出。由于在V()操作唤醒一个等待进程之后，进程自己会将等待队列中的对应项删除，所以此处不需要要__up函数中删除等待队列的节点。

> 请在实验报告中给出给用户态进程/线程提供信号量机制的设计方案，并比较说明给内核级提供信号量机制的异同

用户态进程间信号量的处理应该与内核级信号量的机制相同，通过系统调用来包装使用内核级信号量机制的处理即可。
而对于用户线程，与线程的具体实现有关。因为线程本身就是共享进程的地址空间资源等，所以共享的内容很多，首先对于不同的共享内容设置不同的信号量，可以再将他们包装成一个结构体。如果用户线程的调度是通过内核来实现的，此时可以沿用内核级的信号量机制，通过系统调用来实现。但如果线程间的调度是由进程自身来完成的，那么此时再通过内核的转换就会使开销增大，且管理混乱，应由进程自身完成对信号量机制的实现，对于各种信号量组成的结构体实现P(),V()操作。基本机制相同，差别在于管理和实现方以及数目的多少。

### 练习2: 完成内核级条件变量和基于内核级条件变量的哲学家就餐问题

> 请在实验报告中给出内核级条件变量的设计描述，并说其大致执行流流程。

ucore条件变量是基于练习1中的信号量实现的，条件量的定义在`kern\sync\monitor.h`中的convar_t结构体中，这个结构包含一个信号量sem, 一个计数器count记录等待该条件的等待者数量，以及指向其所属管程的指针。
管程通过`kern\sync\monitor.h`中的monitor_t给出定义，包括互斥锁（信号量)mutex, 记录发出signal()的进程的计数器next_count,以及相应的信号量next, 条件变量数组的首地址。

conditional wait的实现在`kern\sync\monitor.c`中的`cond_wait()`
代码如下：
```
void
cond_wait (condvar_t *cvp) {
    //LAB7 EXERCISE1: YOUR CODE
    cprintf("cond_wait begin:  cvp %x, cvp->count %d, cvp->owner->next_count %d\n", cvp, cvp->count, cvp->owner->next_count);
    cvp->count ++ ;
    if(cvp->owner->next_count > 0){
      up(&(cvp->owner->next)) ;
    }
    else{
      up(&(cvp->owner->mutex)) ;
    }
    down(&(cvp->sem)) ;
    cvp->count -- ;
    cprintf("cond_wait end:  cvp %x, cvp->count %d, cvp->owner->next_count %d\n", cvp, cvp->count, cvp->owner->next_count);
}
```
conditional signal的实现在`kern\sync\monitor.c`中的`cond_signal()`
代码如下：
```
void 
cond_signal (condvar_t *cvp) {
   //LAB7 EXERCISE1: YOUR CODE
   cprintf("cond_signal begin: cvp %x, cvp->count %d, cvp->owner->next_count %d\n", cvp, cvp->count, cvp->owner->next_count); 
   if(cvp->count > 0){
      cvp->owner->next_count ++ ;
      up(&(cvp->sem)) ;
      down(&(cvp->owner->next)) ;
      cvp->owner->next_count -- ;
   }
   cprintf("cond_signal end: cvp %x, cvp->count %d, cvp->owner->next_count %d\n", cvp, cvp->count, cvp->owner->next_count);
}
```
将signal和wait连起来看运行过程，首先若线程A进入cond_wait, 则条件变量的计数器加1，表示等待的线程增加1，此时若管程的next_count>0说明有其他运行signal的线程处于睡眠状态，阻塞在了next信号量上，此时应该释放next信号量，使该发出signal的线程唤醒运行，而A阻塞在了该条件变量上，等待其他线程发出signal将其唤醒。若唤醒，则将该条件变量的计数器减一，说明A已经可以继续执行，等待此条件变量的线程数减少。
若管程的next_cout <= 0，说明此时没有发出signal()的线程还未退出，说明其他线程若处于等待状态都是在等待互斥锁mutex, 此时A释放管程的互斥锁mutex,而A本身阻塞在了该条件变量上。因为释放了互斥锁，其他线程在一开始的时候有机会获得mutex，进行运行，若其他进程获得Mutex修改条件变量并且执行cond_signal(),则A被唤醒，将计数器减1，说明等待此条件变量的线程数减少一个，A可以继续执行。

为了保证这个管程机制的顺利运行，每个进程进入和退出管程都有一段固定的代码，如下所示：(以phi_put_forks_convar为例)
```
void phi_put_forks_condvar(int i) {
     down(&(mtp->mutex));
//--------into routine in monitor--------------

//--------leave routine in monitor--------------
     if(mtp->next_count>0)
        up(&(mtp->next));
     else
        up(&(mtp->mutex));
}
```
首先线程执行之前都要获得管程的mutex,而在退出时判断如果有执行cond_signal()的线程阻塞在next信号量上，则将next信号量释放，否则释放mutex信号量。

> 请在实验报告中给出给用户态进程/线程提供条件变量机制的设计方案，并比较说明给内核级提供条件变量机制的异同。

与练习1类似，用户进程可以通过系统调用利用内核线程的管程机制。
而对于用户线程，如果是由内核完成调度，可以通过系统调用来使用内核的管程机制，若是在用户态由应用程序实现调度，则应由用户态进程实现管程机制来进行同步互斥的控制，原理是基本相同的。需要注意的是，此处管程实现使用了信号量，对于用户态的进程/线程而言，其信号量也要发生对应的转变。
在管程的原理中，本身没有很多用到内核的地方，但因为实现中使用了信号量机制，而信号量是基于时钟中断机制的，所以需要在此处与内核打交道。
事实上，管程使用了面向对象的方式，在操作系统中用的较少，反而在高级语言，应用程序中使用的较多。

## 与答案的不同
本实验只有练习2涉及到编码，参考注释的伪代码，主体部分与答案的实现并没有区别，只是答案输出了更多显示哲学家状态的信息。在执行`make qemu`时，因为打印信息的不同，而打印是很耗时的导致时间差，使得输出信息略有差异。
但总体上`make grade`可以完全通过
执行`make qemu`可以完成对于同步互斥部分的检测，正确输出哲学家就餐的状态。

## 实验涉及到的知识点
1. 信号量的原理和实验
2. 管程，条件变量的原理和实现
3. 利用信号量机制以及管程机制解决经典同步互斥问题：哲学家就餐问题

## 实验未涉及到的知识点
1. 多种管程的实现方式及其对比
2. 对临界区代码的处理只使用了禁用中断的方式，实际上还有多种方式可以处理临界区代码，例如基于软件的同步解决方法。