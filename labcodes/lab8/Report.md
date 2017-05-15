# LAB8 report
2014011426 计45 邵韵秋

## 实验内容
### 练习1: 完成读文件操作的实现

> 首先了解打开文件的处理流程，然后参考本实验后续的文件读写操作的过程分析，编写在sfs_inode.c中sfs_io_nolock读文件中数据的实现代码。

需要补充`kern/sfs/sfs_inode.c`文件中的`sfs_io_nblock`函数中的内容。主要实现分块读取文件内容。
具体流程分成三部分：
1. 读取文件头，此处需要注意文件头是否对其的问题。代码如下：
```
		// read the first block
    	if((blkoff = offset % SFS_BLKSIZE) != 0){
	        size = (nblks != 0) ? (SFS_BLKSIZE - blkoff) : (endpos - offset) ; //size 为要读写的长度
	        if((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0){ //获取磁盘号的实际编号
	            goto out ;
        }
        if((ret = sfs_buf_op(sfs, buf, size, ino, blkoff)) != 0) { //实际读取文件内容
            goto out ;
        }
        alen += size ;
        if(nblks == 0){
            goto out ;
        }
        buf += size ;
        ++ blkno ;
        -- nblks ;
    }
```

2. 按块依次读取文件内容，代码如下：
```
	//read the blocks
    size = SFS_BLKSIZE ;
    if(nblks > 0){ // 对于nblks个块分别读取，此处一次性读取nblks个块
        if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino))) //获取磁盘号的实际编号
        {
            goto out ;
        }
        if((ret = sfs_block_op(sfs, buf, ino, nblks))){ //读取nblks个磁盘块内容
            goto out ;
        }
        alen += (size*nblks) ;
        buf += (size*nblks) ;
        blkno += nblks ; //更新大小
    }
```

3. 读取文件最后一块的内容，读到文件末尾（注意文件末尾不一定按照块对其）。代码如下：
```
// read the end data
   if((size = endpos % SFS_BLKSIZE) != 0){ //根据对齐方式读取最后一块内容，与1中处理类似
        if((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino))){
            goto out ;
        }
        if((ret = sfs_buf_op(sfs, buf, size, ino, 0)) != 0){
            goto out ;
        }
        alen += size ;
        buf += size ;
    }
```	

> 请在实验报告中给出设计实现”UNIX的PIPE机制“的概要设方案

使用VFS提供的接口，设计一个pipe机制文件的系统pipefs, 在调用时，创建两个file,一个只读，一个只写，并将这两个file连接到同一个inode上， inode中有一个buffer数据缓冲区，使用VFS提供的read/write接口，在缓冲区内读写数据。使用管程机制，实现进程间的同步管理。当缓冲满时，写进程需要等待，等到缓冲区有空闲时写入，当缓冲区为空时，读进程等待，直到缓冲区有内容。

### 练习2: 完成基于文件系统的执行程序机制的实现

> 改写proc.c中的load_icode函数和其他相关函数，实现基于文件系统的执行程序机制。执行：make qemu。如果能看看到sh用户程序的执行界面，则基本成功了。如果在sh用户界面上可以执行”ls”,”hello”等其他放置在sfs文件系统中的其他执行程序，则可以认为本实验基本成功。

该练习比较复杂，主要是需要完成`kern/process/proc.c`中的load_icode函数，同时需要对一些函数做出更新。
首先需要更新的函数为`kern/process/proc.c`中的`alloc_proc`函数和`do_fork`函数
在`alloc_page`函数中，需要对proc的filesp变量进行初始化，在最后加入一行代码`proc->filesp = NULL`即可。
在`do_fork`函数中，需要复制父进程的文件资源，即加入如下代码：
```
if(copy_files(clone_flags, proc) != 0){
        cprintf("do_fork---> copy_files failed\n") ;
        goto bad_fork_cleanup_fs ;
    } // for lab8 
```

以上为需要更新的部分，之后主要完成`load_icode`函数即可，大致分为以下步骤：
1. 为当前进程分配一个内存控制块mm
```
	int ret = -E_NO_MEM ;
     //(1) create a new mm for current process
	struct mm_struct *mm ;
    if((mm = mm_create()) == NULL){
        cprintf("load_icode--> mm_create failed\n") ;
        goto bad_mm ;
    }
```
2. 根据mm建立对应的内存控制块pdt
```
//(2) create a new PDT, and mm->pgdir= kernel virtual addr of PDT
    if(setup_pgdir(mm) != 0){
        cprintf("load_icode-->setup_pgdir failed\n") ;
        goto bad_pgdir_cleanup_mm ;
    }
```
3. 将代码段，数据段以及BSS段复制到内存
```
	struct Page* page ;
    struct elfhdr __elf, *elf = &__elf ;
    //(3.1) read raw data content in file and resolve elfhdr
    if((ret = load_icode_read(fd, elf, sizeof(struct elfhdr), 0)) != 0){
        cprintf("load_icode--> load_icode_read elfhdr failed\n") ;
        goto bad_elf_cleanup_pgdir ;
    }
    if(elf->e_magic != ELF_MAGIC){ // 判断elf文件格式是否合法
        cprintf("load_icode-->elf_magic error") ;
        ret = -E_INVAL_ELF;
        goto bad_elf_cleanup_pgdir ;
    }
    //(3.2) read raw data content in file and resolve proghdr based on info in elfhdr
    struct proghdr __ph, *ph = &__ph ;
    uint32_t vm_flags, perm;
    for(int i = 0 ; i < elf->e_phnum ; i ++){ // 依次读取所有程序代码到内存
        off_t phoff = elf->e_phoff + sizeof(struct proghdr) * i ; //找到每个程序的头信息
        if((ret = load_icode_read(fd, ph, sizeof(struct proghdr), phoff)) != 0){ //将程序头信息读入内存
            cprintf("load_icode-->load_icode_read progh failed\n") ;
            goto bad_cleanup_mmap ;
        }
        if(ph->p_type != ELF_PT_LOAD){ //不允许load, 则跳过
            continue ; 
        }
        if (ph->p_filesz > ph->p_memsz) { // if the filsz is legal?
            ret = -E_INVAL_ELF;
            goto bad_cleanup_mmap;
        }
        if (ph->p_filesz == 0) {
            continue ;
        }
        // 设置读写可运行等标志位
        vm_flags = 0 ;
        perm = PTE_U ;
        if(ph->p_flags & ELF_PF_X) vm_flags |= VM_EXEC ;
        if(ph->p_flags & ELF_PF_W) vm_flags |= VM_WRITE ;
        if(ph->p_flags & ELF_PF_R) vm_flags |= VM_READ ;
        if(vm_flags & VM_WRITE) perm |= PTE_W ; // set uo flags
        //(3.3) call mm_map to build vma related to TEXT/DATA
        if((ret = mm_map(mm, ph->p_va, ph->p_memsz, vm_flags, NULL)) != 0){ //建立物理地和虚拟地址的映射
            cprintf("load_icode--> mm_map failed\n") ;
            goto bad_cleanup_mmap ;
        }

        //(3.4) callpgdir_alloc_page to allocate page for TEXT/DATA, read contents in file and copy them into the new allocated pages
        off_t offset = ph->p_offset ;
        size_t off, size ;
        uintptr_t start = ph->p_va , end, la = ROUNDDOWN(start, PGSIZE) ; // in def,h
        ret = -E_NO_MEM ;
        end = ph->p_va + ph->p_filesz ;
        // from (start, end) copy content to (la , la+end) vm， 读取代码段和数据段， 按照PGSIZE读取
        while(start < end){
            if((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL){
                cprintf("load_icode-->pgdir_alloc_page failed\n") ;
                ret = -E_NO_MEM ;
                goto bad_cleanup_mmap ;
            }
            off = start - la ;
            size = PGSIZE - off ;
            la += PGSIZE ;
            if(end < la){
                size = size - (la - end) ;
            }
            if((ret = load_icode_read(fd, page2kva(page)+off, size, offset)) != 0){
                goto bad_cleanup_mmap ;
            }
            start += size ;
            offset += size ;  
        }

        //(3.5) callpgdir_alloc_page to allocate pages for BSS
        //需要解决对其问题
        end = ph->p_va + ph->p_memsz ;

        if(start < la){
            if(start == end){
                continue ;
            }
            off = start + PGSIZE - la;
            size = PGSIZE - off ;
            if(end < la){
                size = size - (la-end) ;
            }
            memset(page2kva(page)+off, 0, size) ;
            start += size ;
            assert((end < la && start == end) || (end >= la && start == la));
        }

        while(start < end){
            if((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL){
                cprintf("load_icode-->pgdir_alloc_page failed\n") ;
                ret = -E_NO_MEM ;
                goto bad_cleanup_mmap ;
            }
            off = start - la ;
            size = PGSIZE - off ;
            la += PGSIZE ;
            if (end < la) {
                size -= la - end;
            }
            memset(page2kva(page) + off, 0, size);
            start += size;
        }
    }

    sysfile_close(fd) ;
```
这一部分代码较长，具体部分已在注释中标出。

4. 建立用户栈
```
    //(4) call mm_map to setup user stack, and put parameters into user stack
    vm_flags = VM_READ | VM_WRITE | VM_STACK;
    if ((ret = mm_map(mm, USTACKTOP - USTACKSIZE, USTACKSIZE, vm_flags, NULL)) != 0) {
        goto bad_cleanup_mmap;
    }
    //alloc_page
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-PGSIZE , PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-2*PGSIZE , PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-3*PGSIZE , PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-4*PGSIZE , PTE_USER) != NULL);
```

5. 设立cr3
```
//(5) setup current process's mm, cr3, reset pgidr (using lcr3 MARCO)
    mm_count_inc(mm);
    current->mm = mm;
    current->cr3 = PADDR(mm->pgdir);
    lcr3(PADDR(mm->pgdir));
```

6. 设立栈中的参数和参数个数
```
//(6) setup uargc and uargv in user stacks
    uint32_t argv_size = 0 ;
    for(int i = 0 ;i < argc; i ++){
        argv_size += strnlen(kargv[i],EXEC_MAX_ARG_LEN + 1)+1; //计算设置栈顶参数所需要的空间
    }

    uintptr_t stacktop = USTACKTOP - (argv_size/sizeof(long)+1)*sizeof(long); //找到放置完参数后的栈顶位置
    char** uargv=(char **)(stacktop  - argc * sizeof(char *)); //分配放置参数的指针，uargv

    argv_size = 0;
    for (int i = 0; i < argc; i ++) {
        uargv[i] = strcpy((char *)(stacktop + argv_size ), kargv[i]); //把每一个参数复制到相应的位置
        argv_size +=  strnlen(kargv[i],EXEC_MAX_ARG_LEN + 1)+1; 
    }

    stacktop = (uintptr_t)uargv - sizeof(int); //留出放argc的位置
    *(int *)stacktop = argc; //将argc放到栈顶
```
此处代码较为难理解，具体见代码后的注释。

7. 设置trapframe
```
//(7) setup trapframe for user environment
    struct trapframe *tf = current->tf;
    memset(tf, 0, sizeof(struct trapframe));
    tf->tf_cs = USER_CS;
    tf->tf_ds = tf->tf_es = tf->tf_ss = USER_DS;
    tf->tf_esp = stacktop;
    tf->tf_eip = elf->e_entry;
    tf->tf_eflags = FL_IF;
    ret = 0;
```

> 请在实验报告中给出设计实现基于”UNIX的硬链接和软链接机制“的概要设方案

1. `vfs.h`文件中预留了硬链接的接口：`int vfs_link(char *old_path, char *new_path);`,使用时只需将new_path指向新建的file, 并将其inode指向old_path的inode,并将该inode的引用计数加1，在unlink时将引用计数-1即可。

2. 软连接相当于一种特殊的文件”快捷方式“。可以在创建软连接时新建一个文件，将old_path的路径复制到新的文件中去，并对该类文件的类型进行标记，然后通过访问该文件中存储的路径访问共享的文件。删除快捷方式时相当于删除一个普通的文件，并不会对原来的文件产生影响，只有将文件实体删除时，才能真的删除文件。

## 与答案的区别：
1. 练习一部分简化了答案的代码，做出了一些改进，答案中从硬盘读代码到内存中时是一块一块的读入，但实际上可以根据数据块的数目一起读入内存，这样就不用使用while循环每次加1，简化了实现。
2. 练习二的实现较为复杂，特别是在读代码段数据段和BSS段以及栈参数的设置部分，自己很难想到，是在理解答案的基础上完成了，实际与答案差别不大。主要就是多了对于具体代码的注释以及功能的分类。答案在一开始对于mm变量进行检查，确定在load代码之前内存空闲可用，保证了代码的安全。而我因为方便调试，在许多非法访问退出的条件下都加了一些输出信息。

## 涉及到的知识点：
1. 文件系统的架构
2. 读文件的过程
3. 基于文件系统执行程序
5. 内存与磁盘的交互
4. vfs接口的抽象
5. sfs系统的实现
6. 软硬链接

## 未涉及到的知识点：
1. 文件数据块的多种组织形式，分配方式
2. 写文件的实现
3. 对不同外设的不同接口的处理和抽象，此处只涉及到了磁盘
4. 文件的目录，路径以及循环检测相关内容








