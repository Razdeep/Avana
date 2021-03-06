#include "../tasking.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "Memory/mem.h"
#include "stdint.h"

#include "Scheduler.h"
#include "Arch/x86/DescriptorTables/desc.h"
#include "Arch/x86/cpu/cpu.h"
#include "../CancerTherapy/CancerCure.h"

#include "Arch/x86.h"
#include "Arch/x86/custom_defs.h"

/*
Symbol Map for Switcher->
    _cur_dir        ->        0x4284ACD1
    system_dir      ->        0x4284ACD2
    current_task    ->        0x4284ACD3
    old_task        ->        0x4284ACD4
    Scheduler       ->        0x4284ACD5
    time_slice      ->        0x4284ACD6
    Core_Main_Lock  ->        0x4284BCD1

Symbol Table for Scheduler->
      top_queue           ->        0x4284ABD1
      reached_bottom      ->        0x4284ABD2
      bottom_task         ->        0x4284ABD3
      current_task        ->        0x4284ABD4
      LAST_QUEUE          ->        0x4284ABD5
      time_slice          ->        0x4284ABD6
      Core_Main_Lock      ->        0x4284BCD1

COMMON Symbols->
      current_task
      time_slice
*/

DECLARE_LOCK(CORE_INFO);
uint32_t sz_switcher;
extern uintptr_t ap_gdts;
extern uintptr_t ap_idts;

void Init_Schedulers()
{
  //Create copies of Scheduler function and switcher functions based on number of CPU cores.
  //UNLOCK(CORE_INFO);
  sz_switcher = switcher_end_t - switcher_ksp_t;

  MotherSpace = (uint32_t *)pmem_4k(1); //phy_alloc4K();
  memset(MotherSpace, 0, 4096);
  SchedulerKits_t *kits = (SchedulerKits_t *)MotherSpace;

  tss_entries = kmalloc(sizeof(tss_struct_t *) * (total_CPU_Cores - 1));

  for (int i = 0; i < total_CPU_Cores; i++)
  {
    Init_Core(i, &kits[i]);
    printf("\nScheduler for Core %d Created...", i);
  }

  KitList = (SchedulerKits_t *)MotherSpace;
}

void Init_Schedulers_common(uint32_t core_id, uintptr_t Scheduler_start, uintptr_t Scheduler_end, SchedulerKits_t *kits)
{
  uint32_t sz_Scheduler = Scheduler_end - Scheduler_start;

  uint32_t *nspace = (uint32_t *)pmem_4k(1);
  memset((void *)nspace, 0, 4096);
  kits->identity = core_id;
  kits->tasks = 0;

  uint8_t *nptr1;
  nptr1 = (uint8_t *)nspace;
  memcpy((void *)nptr1, switcher_ksp_t, sz_switcher);
  kits->switcher = (uint32_t *)nptr1;

  nptr1 += sz_switcher;
  nptr1 = ROUNDUP(nptr1, 4) + 32;
  memcpy((void *)nptr1, Scheduler_t, sz_Scheduler);
  kits->scheduler = (uint32_t *)nptr1;

  nptr1 += sz_Scheduler;
  nptr1 = ROUNDUP(nptr1, 4) + 32;

  uint32_t *temporary_stack = (uint32_t *)pmem_4k(1);
  memset((void *)*temporary_stack, 0, 4096);
  kits->stack_top = temporary_stack;

  temporary_stack += 512;
  kits->stack = temporary_stack;

  ByteSequence_Replace(0x42843333, 4, (uint32_t)&kits->Spurious_task, 4, kits->switcher, kits->switcher + sz_switcher); // Spurious Task Struct
  ByteSequence_Replace(0x4284ACD1, 4, (uint32_t)&kits->current_pdir, 4, kits->switcher, kits->switcher + sz_switcher);  //_cur_dir
  kits->current_pdir = (uint32_t *)system_dir;
  ByteSequence_Replace(0x4284ACD2, 4, (uint32_t)&system_dir, 4, kits->switcher, kits->switcher + sz_switcher);              //system_dir
  ByteSequence_Replace(0x4284ACD3, 4, (uint32_t)&kits->current_task, 4, kits->switcher, kits->switcher + sz_switcher);      //current_task
  ByteSequence_Replace(0x4284ABD4, 4, (uint32_t)&kits->current_task, 4, kits->scheduler, kits->scheduler + sz_Scheduler);   //current_task
  ByteSequence_Replace(0x4284ACD4, 4, (uint32_t)&kits->old_task, 4, kits->switcher, kits->switcher + sz_switcher);          //old_task
  ByteSequence_Replace(0x4284ACD5, 4, (uint32_t)&kits->scheduler, 4, kits->switcher, kits->switcher + sz_switcher);         //Scheduler
  ByteSequence_Replace(0x4284ACD6, 4, (uint32_t)&kits->timeslice, 4, kits->switcher, kits->switcher + sz_switcher);         //time_slice
  ByteSequence_Replace(0x4284CD00, 4, (uint32_t)temporary_stack, 4, kits->switcher, kits->switcher + sz_switcher);          //Temporary Stack
  ByteSequence_Replace(0x4284ABD6, 4, (uint32_t)&kits->timeslice, 4, kits->scheduler, kits->scheduler + sz_Scheduler);      //current_task
  ByteSequence_Replace(0x4284ABD1, 4, (uint32_t)&kits->top_queue, 4, kits->scheduler, kits->scheduler + sz_Scheduler);      //top_queue
  ByteSequence_Replace(0x4284ABD2, 4, (uint32_t)&kits->reached_bottom, 4, kits->scheduler, kits->scheduler + sz_Scheduler); //reached_bottom
  ByteSequence_Replace(0x4284ABD3, 4, (uint32_t)&kits->bottom_task, 4, kits->scheduler, kits->scheduler + sz_Scheduler);    //bottom_task
  ByteSequence_Replace(0x4284ABD5, 4, (uint32_t)&kits->queue_last, 4, kits->scheduler, kits->scheduler + sz_Scheduler);     //LAST_QUEUE

  ByteSequence_Replace(0x4284BCD1, 4, (uint32_t)&kits->Core_Main_Lock, 4, kits->switcher, kits->switcher + sz_switcher);    //CORE_MAIN_LOCK
  ByteSequence_Replace(0x4284BCD1, 4, (uint32_t)&kits->Core_Main_Lock, 4, kits->scheduler, kits->scheduler + sz_Scheduler); //CORE_MAIN_LOCK

  uint32_t pmode_code_addr = 0x3000 + AP_startup_Code_sz + 16;
  if (core_id)
    ByteSequence_Replace(0x42842222, 4, (uint32_t)&Spawner_Task, 4, (uint32_t *)pmode_code_addr, (uint32_t *)(pmode_code_addr + pmode_code_size));
  kits->Core_Main_Lock = 0;
  kits->queue_start = (uint32_t *)pmem_4k(40); //kmalloc(4096*40);//mtalloc(20);//33554432;
  memset((void *)kits->queue_start, 0, 4096 * 40);
  kits->queue_last = (uint32_t *)(((uint32_t)kits->queue_start) + (4096 * 20));
  kits->top_queue = kits->queue_last;
  kits->reached_bottom = 0;
  kits->bottom_queue = kits->queue_last;
  kits->bottom_task = 1;

  // Setup a Cleaner Better Descriptors for all Processors

  uintptr_t gdt_new = (uintptr_t)iden_alloc(64); //desc_malloc(64);
  kits->gdt = gdt_new;
  General_GDT_Setup((uint32_t *)(gdt_new + 8), (uint32_t *)gdt_new, (uint32_t*)iden_alloc(sizeof(tss_struct_t)));

  if (core_id)
  {
    ap_gdts = gdt_new;
  }
  else
  {
    lgdt((void *)gdt_new);
    tss_flush();
  }

  //pmode_TSS_ltr(i, 0x2B);
  uintptr_t idt_new = (uintptr_t)iden_alloc(960); //desc_malloc(960);
  kits->idt = idt_new;
  SimpleIDT_Setup((uint32_t *)(idt_new + 8), (uint32_t *)idt_new);

  uint32_t *idt_ptr = (uint32_t *)(kits->idt);
  idt_ptr += 2;
  idtSetEntry(50, (uint32_t)kits->switcher, 0x08, makeFlagByte(1, USER_MODE), (uint64_t *)idt_ptr);
  idtSetEntry(51, (uint32_t)kits->switcher, 0x08, makeFlagByte(1, KERNEL_MODE), (uint64_t *)idt_ptr);

  if (core_id)
    ap_idts = idt_new;
  else
    lidt((void *)idt_new);

  if (core_id)
  {
    *(uint32_t *)(0x3000 + 8 + AP_startup_Code_sz) = 0x3240;
    *(uint32_t *)(0x3000 + 4 + AP_startup_Code_sz) = core_id;
    while (*(uint32_t *)(0x3000 + 8 + AP_startup_Code_sz) == 0x3240)
    {
      delay_hpet(10);
    }
  }

  printf("\nCore #%x Initialized", core_id);
}

void Init_Core(int i, SchedulerKits_t *kits)
{
  Init_Schedulers_common(i, (uintptr_t)Scheduler_t, (uintptr_t)Scheduler_end_t, &kits[0]);
}

DECLARE_LOCK(CORE_INFO);

extern void syscall_vector();
extern void SchedulerTimerEvaluator();

void SchedulerTimerEvaluatorHandle()
{
  printf("\nThe LVT Timers are working Great! %d", Get_Scheduler()->identity);
  while (1)
    ;
  /*/*/
  /* SchedulerKits_t* kits = Get_Scheduler();
  uint32_t* idt_ptr = (uint32_t*)(kits->idt);
  idt_ptr += 2;
  idtSetEntry(50, (uint32_t)&kits->switcher, 0x08, makeFlagByte(1, USER_MODE), (uint64_t*)idt_ptr);
  lidt(kits->idt);//*/
}

SchedulerKits_t *__attribute__((optimize("O0"))) FindLightestScheduler()
{
  SchedulerKits_t *tmp = (SchedulerKits_t *)MotherSpace;
  SchedulerKits_t *tmp2 = tmp;
  //  ++tmp;
  for (uint32_t i = 0; i < total_CPU_Cores; i++)
  {
    if (tmp2->tasks > tmp->tasks)
      tmp2 = tmp;
    ++tmp;
  }
  return tmp2;
}

SchedulerKits_t *__attribute__((optimize("O0"))) Get_Scheduler()
{
  LOCK(CORE_INFO);
  uint32_t i;
  i = *(uint32_t *)(0xfee00020);
  i = i >> 24;
  UNLOCK(CORE_INFO);
  return &KitList[i];
}

//TODO: Extend Base Queue size so that it can store 20480 tasks.

/*********************************** SCHEDULER ASSISTANCE SYSTEM ***********************************/

void __attribute__((optimize("O0"))) SAS_init()
{

  uint32_t *SAS_booster;
  uint32_t *SAS_eraser;

  uint32_t *SAS_booster_func;
  uint32_t *SAS_eraser_func;

  uint32_t *nspace;

  uint32_t SAS_size = SAS_end_t - SAS_void_eraser_t;
  //uint32_t SAS_booster_sz = SAS_task_booster_end - SAS_task_booster_t;
  uint32_t SAS_eraser_sz = SAS_end_t - SAS_void_eraser_t;

  uint32_t *kit_ptr;
  uint32_t *sas_cleaning_complete;
  uint32_t *highest_task_entry;
  uint32_t *last_highest_priority;

  for (uint32_t i = 0; i < total_CPU_Cores; i++)
  {
    nspace = (uint32_t *)phy_alloc4K();
    memset((void *)nspace, 0, 4096);
    SAS_eraser = nspace;

    sas_cleaning_complete = nspace + 2;
    highest_task_entry = nspace + 3;
    kit_ptr = nspace + 4;
    last_highest_priority = nspace + 5;

    SAS_eraser_func = nspace + 6;

    memcpy(SAS_eraser_func, SAS_void_eraser_t, SAS_eraser_sz);

    ByteSequence_Replace(0x4284CDA1, 4, (uint32_t)sas_cleaning_complete, 4, SAS_eraser_func, SAS_eraser_func + SAS_size);
    ByteSequence_Replace(0x4284CDA2, 4, (uint32_t)highest_task_entry, 4, SAS_eraser_func, SAS_eraser_func + SAS_size);
    ByteSequence_Replace(0x4284CDA3, 4, (uint32_t)kit_ptr, 4, SAS_eraser_func, SAS_eraser_func + SAS_size);
    ByteSequence_Replace(0x4284CDA4, 4, (uint32_t)last_highest_priority, 4, SAS_eraser_func, SAS_eraser_func + SAS_size);
    ByteSequence_Replace(0x4284AFF1, 4, (uint32_t)&printf, 4, SAS_eraser_func, SAS_eraser_func + SAS_size);
    //    ByteSequence_Replace(0x4284CDA5, 4, KitList[i].Spurious_task, 4, SAS_eraser_func, SAS_eraser_func + SAS_size);

    *sas_cleaning_complete = 1;
    *highest_task_entry = 0;
    *kit_ptr = (uint32_t)&KitList[i];
    *last_highest_priority = 0;

    printf("\n[%x]", KitList[i].identity);

    *SAS_eraser = (uint32_t)create_task("SAS_Maintainance", (func_t)SAS_eraser_func, 1, 0x202, SAS_proc);

    //if(i==1)  continue;

    // Activate_task_direct_SP((task_t*)*SAS_booster, (SchedulerKits_t*)&KitList[i]);
    Activate_task_direct_SP((task_t *)*SAS_eraser, (SchedulerKits_t *)&KitList[i]);

    // ((task_t*)*SAS_booster)->special = 1;
    ((task_t *)*SAS_eraser)->special = 1;
  }
  // while(1);
}

/*
    SYMBOL MAP:
    0x4284CDA1    sas_cleaning_complete
    0x4284CDA2    highest_task_entry
    0x4284CDA3    kit
    0x4284CDA4    last_highest_priority
    0x4284CDA5    Spurious_task
*/

void __attribute__((optimize("O0"))) SAS_void_eraser_t()
{
  uint32_t tasks_searched = 1;
  SchedulerKits_t *kit = (SchedulerKits_t *)(*(uint32_t *)(0x4284CDA3));
  //  func_t pf = (func_t)0x4284AFF1;
  while (1)
  {
    // asm volatile("cli");
    // if(!(*(uint32_t*)(0x4284CDA1)))
    {
      // kit->Core_Main_Lock = 1;
      if (*(kit->bottom_queue) >= tasks_searched)
      {
        uint32_t *tmp = kit->bottom_queue + tasks_searched;
        if (!*tmp || *tmp == (uint32_t)((task_t *)kit->Spurious_task)->ThreadTable_entry)
        {
          uint32_t *last_task = kit->bottom_queue + *(kit->bottom_queue);
          //  if(kit->identity != 0)
          //pf("  Dx-%s-%s-%x-%x  ", ((task_t*)*tmp)->name, ((task_t*)*last_task)->name, kit->bottom_task,(*(kit->bottom_queue)));
          //  pf(" D-%x ", tmp);
          *tmp = *last_task;
          *last_task = 0;
          ((ThreadTableEntry_t *)*tmp)->task->active = (uint32_t)tmp; //(((task_t*)tmp)->ThreadTable_entry);
          --(*(kit->bottom_queue));
          if (kit->bottom_task > (*kit->bottom_queue))
            kit->bottom_task = 1;
        }
        else if (!((ThreadTableEntry_t *)*tmp)->task->special)
        {
          if (((ThreadTableEntry_t *)*tmp)->task->tokens > (*(uint32_t *)(0x4284CDA4)))
          {
            (*(uint32_t *)(0x4284CDA2)) = (uint32_t)*tmp;
            (*(uint32_t *)(0x4284CDA4)) = ((ThreadTableEntry_t *)*tmp)->task->tokens;
          }
          ((ThreadTableEntry_t *)*tmp)->task->tokens += 1;
        }
        ++tasks_searched;
      }
      else
      {
        //     (*(uint32_t*)(0x4284CDA1)) = 1;
        tasks_searched = 1;
        (*(uint32_t *)(0x4284CDA4)) = 0;

        if ((*(uint32_t *)(0x4284CDA2)) && ((ThreadTableEntry_t *)(*(uint32_t *)(0x4284CDA2)))->task->active)
        {
          //if(kit->identity != 0) goto out;//
          //asm volatile("hlt");
          task_t *task = ((ThreadTableEntry_t *)(*(uint32_t *)(0x4284CDA2)))->task;
          task->tokens = task->priority;
          //if(kit->identity != 0)
          //f("\nAx-%s",task->name);

          uint32_t *place_holder = (uint32_t *)task->active;
          *place_holder = (uint32_t)((task_t *)(kit->Spurious_task))->ThreadTable_entry; //Spurious task is a task which would kill itself to remove the void.

          uint32_t *_q = (uint32_t *)kit->queue_last;

          _q -= 4096; ///Get into the queue required
          ++(*_q);    ///Create a new entry, extend the queue

          uint32_t *q_entry = _q + (*_q); ///Get the pointer to the new entry
          //printf(" Dx%x ",q_entry);
          *q_entry = (*(uint32_t *)(0x4284CDA2)); ///Fill in the Entry with the address of the new Task!!!
          task->active = (uint32_t)q_entry;

          if (_q < kit->top_queue) //If the Top most queue earlier was below this queue,
            kit->top_queue = _q;   //Put it as the top most queue

          //if(reached_bottom)
          kit->reached_bottom = 0;
          (*(uint32_t *)(0x4284CDA2)) = 0; //*/
        }                                  //*/
                                           //   (*(uint32_t*)(0x4284CDA1)) = 0;
      }
      //  kit->Core_Main_Lock = 0;
    }
    asm volatile("int $50");
  }
}

void __attribute__((optimize("O0"))) SAS_end_t()
{
  return;
}

//TODO: CREATE AN ADVANCED INTEGRATED SYSTEM WHICH WOULD CONTROL THE WHOLE MULTITASKING SYSTEM AND THE SCHEDULER, The Timer, the Interrupts etc Literally!. It should act as the BOSS of the Scheduler!!!

/*
    //TODO: Create a System which can transfer threads from one processor to other if processor loads are uneven.
*/
