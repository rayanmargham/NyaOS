#include "sched.h"
#include "fs/vfs.h"
#include "lib/kpanic.h"
#include "main.h"
#include "drivers/serial.h"
#include "vmm.h"
#include "pmm.h"
#include "drivers/ps2.h"
#include <ACPI/acpi.h>
#include <elf/elf.h>
#include <fs/tmpfs.h>
#include <fpu/xsave.h>

struct thread_t *start_of_queue;
struct thread_t *current_thread;
char e[64];
void switch_context(struct StackFrame *frame, struct cpu_context_t *ctx)
{
    *frame = ctx->frame;
    xrstore((uint64_t)ctx->fpu_state);
}
void save_context(struct StackFrame *frame, struct cpu_context_t *ctx)
{
    ctx->frame = *frame;
    xsave((uint64_t)ctx->fpu_state);
}
void switch_task(struct StackFrame *frame)
{
    if (current_thread != NULL)
    {
        
        if (current_thread->next)
        {
            
            if (current_thread->context->frame.cs == (0x40 | (3)))
            {
                
                uint64_t kernel_gs_base = 0;
                readmsr(0xC0000101, &kernel_gs_base);
                current_thread->gs_base = (struct per_thread_cpu_info_t*)kernel_gs_base;
                uint64_t fs = 0;
                readmsr(0xC0000100, &fs);;
                current_thread->fs = fs;
            }

            save_context(frame, current_thread->context);
            current_thread = current_thread->next;
        }
        else
        {
            if (start_of_queue != NULL)
            {
                if (current_thread->context->frame.cs == (0x40 | (3)))
                {
                    uint64_t kernel_gs_base = 0;
                    readmsr(0xC0000101, &kernel_gs_base);
                    current_thread->gs_base = (struct per_thread_cpu_info_t*)kernel_gs_base;
                    uint64_t fs = 0;
                    readmsr(0xC0000100, &fs);;
                    current_thread->fs = fs;
                }
                save_context(frame, current_thread->context);
                
                current_thread = start_of_queue;
            }
            else
            {
                serial_print("NOTHING TO SWITCH TO LOL, RETTING\n");
                return; // QUEUES EMPTY, RETURN AND DO NOTHING
            }
        }
        switch_context(frame, current_thread->context);
        if (current_thread->context->frame.cs == (0x40 | (3)))
        {
            writemsr(0xC0000101, (uint64_t)current_thread->gs_base);
            writemsr(0xC0000100, current_thread->fs);
        }
        update_cr3((uint64_t)current_thread->info->pagemap->pml4);
    }
    else
    {
        if (start_of_queue)
        {
            current_thread = start_of_queue;
            
            switch_context(frame, current_thread->context);
            if (current_thread->context->frame.cs == (0x40 | (3)))
            {
                writemsr(0xC0000101, (uint64_t)current_thread->gs_base);
                writemsr(0xC0000100, current_thread->fs);
            }
            update_cr3((uint64_t)current_thread->info->pagemap->pml4);
            return;
        }
        serial_print_color("Scheduler: Nothing to switch to!\n", 2);
        return;
    }

}
struct cpu_context_t *new_context(uint64_t entry_func, uint64_t rsp, bool user)

{
    if (!user)
    {
        struct cpu_context_t *new_guy = kmalloc(sizeof(struct cpu_context_t));
        new_guy->frame.rip = entry_func;
        new_guy->frame.rsp = rsp;
        new_guy->frame.rbp = rsp;
        new_guy->frame.rflags = 0x202;
        new_guy->frame.cs = 0x28; // KERNEL CODE
        new_guy->frame.ss = 0x30; // KERNEL DATA
        new_guy->fpu_state = kmalloc(align_up(xsave_size, 4096));
        memset(new_guy->fpu_state, 0, xsave_size);
        struct fpu_state_t *state = new_guy->fpu_state;
        // As per SYS V abi spec
        state->fcw = (1 << 0) |    // IM
                    (1 << 1) |    // DM
                    (1 << 2) |    // ZM
                    (1 << 3) |    // OM
                    (1 << 4) |    // UM
                    (1 << 5) |    // PM
                    (0b11 << 8);  // PC
        // stolen from managarm for funny
        state->mxcsr = 0b1111110000000;
        return new_guy;

    }
    else
    {
        // UNIMPLMENTED LOL
        struct cpu_context_t *new_guy = kmalloc(sizeof(struct cpu_context_t));
        new_guy->frame.rip = entry_func;
        new_guy->frame.rsp = rsp;
        new_guy->frame.rbp = rsp;
        new_guy->frame.rflags = 0x202;
        new_guy->frame.cs = 0x40 | (3); // USER CODE
        new_guy->frame.ss = 0x38 | (3); // USER DATA
        new_guy->fpu_state = kmalloc(align_up(xsave_size, 4096));
        memset(new_guy->fpu_state, 0, xsave_size);
        struct fpu_state_t *state = new_guy->fpu_state;
        // As per SYS V abi spec
        state->fcw = (0xFF << 1);
        state->mxcsr = 0xFC;
        return new_guy;
    }
}
struct thread_t *create_thread(struct cpu_context_t *it)
{
    struct thread_t *thread = kmalloc(sizeof(struct thread_t));
    thread->context = it;
    thread->next = NULL;
    return thread;
}
struct process_info *make_process_info(char *name, int pid)
{
    struct process_info *new_process = kmalloc(sizeof(struct process_info));
    
    strcpy(new_process->name, name);
    new_process->pid = pid;
    new_process->pagemap = &kernel_pagemap; // just set it to the kernels pagemap for now.
    new_process->root_vfs = root->list;
    new_process->cur_working_directory = root->list;

    return new_process;
}
void kthread_start()
{
    while (true)
    {
        if (event_count > 0)
        {
            event_count--;
            struct KeyboardEvent event = events[event_count];
            if (event.printable)
            {
                kprintf("Key Pressed %c\n", event.printable);
            }
            else
            {
                switch (event.key)
                {
                case KEY_ESCAPE:
                    kprintf("KEY ESCAPE PRESSED!\n", event.key);
                    break;
                
                default:
                    break;
                }
            }
            
            
        }
    }
}
struct process_info *get_cur_process_info()
{
    if (current_thread)
    {
        return current_thread->info;
    }
    else {
        return NULL;
    }
    
}
struct pagemap *get_current_pagemap_process()
{
    if (current_thread)
    {
        struct process_info *c = current_thread->info;
        if (c->pagemap)
        {
            return c->pagemap;
        }
        else {
            return NULL;
        }
    }
    else {
        return NULL;
    }
}
void sched_init()
{
    // uint64_t *new_poop = (uint64_t)(((uint64_t)pmm_alloc_singlep() + hhdm_request.response->offset) + 4096);
    struct process_info *e = make_process_info("Keyboard clearer thing", 0);
    uint64_t *new_kstack = (uint64_t*)((uint64_t)(((uint64_t)pmm_alloc_singlep() + hhdm_request.response->offset) + 4096)); // CAUSE STACK GROWS DOWNWARDS
    struct cpu_context_t *ctx = new_context((uint64_t)kthread_start, (uint64_t)new_kstack, false);
    // struct cpu_context_t *timeforpoop = new_context((uint64_t)kthread_poop, (uint64_t)new_poop, pml4, false);
    // serial_print("Created New CPU Context for Kernel Thread\n");
    // serial_print("attempting to create kthread...\n");
    struct thread_t *kthread = create_thread(ctx);
    kthread->info = e;
    
    start_of_queue = kthread;
    kthread->gs_base = kmalloc(sizeof(struct per_thread_cpu_info_t));
    kthread->gs_base->kernel_stack_ptr = (uint64_t)new_kstack;
    kthread->gs_base->ptr = kthread->gs_base;
    struct vnode *pro = vnode_path_lookup(root->list, "/usr/bin/bash", false, NULL);
    if (!pro)
    {
        kpanic("frick u\n", NULL);
    }
    struct pagemap *for_elf = new_pagemap();
    kprintf("Addr of file data from found vnode: %p\n", ((struct tmpfs_node*)pro->data)->data);
    struct thread_t *fr = load_elf_program(for_elf, 0, pro, 0, 0, 0, 0, 0, 0, 0);
    if (fr)
    {
        fr->next = start_of_queue;
        start_of_queue = fr;
    }
    else {
        kprintf("Failed to load elf program!\n");
    }
    asm ("sti");
    // // lets create another thread trollage
    // struct thread_t *pooper = create_thread(timeforpoop);
    // kthread->next = pooper;
}