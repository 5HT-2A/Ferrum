#include <paging.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Kernel's Page Directory
page_directory_t *kernel_directory;

// Current Directory (Null at start!)
page_directory_t *current_directory = NULL;

void page_fault(struct regs *regs)
{
    uint32_t err_code = regs->err_code;
    bool protection_err = (bool)(err_code & PAGE_FAULT_PROTECTION), write_err = (bool)(err_code & PAGE_FAULT_WRITE),
        user_err = (bool)(err_code & PAGE_FAULT_USER), reserved_err = (bool)(err_code & PAGE_FAULT_RESERVED),
        exec_err = (bool)(err_code & PAGE_FAULT_EXEC);
    physaddr_t bad_addr;
//    asm volatile("mov %%cr2, %0": "=r"(bad_addr));
    printk("Page fault for address 0x%x: %s%s%s, %s, %s\n", bad_addr, reserved_err ? "reserved, " : "", exec_err ? "execution, " : "",
            protection_err ? "protection" : "non-present", write_err ? "write" : "read", user_err ? "user" : "supervisor");
    asm volatile("cli; hlt");
}

page_frame_t *get_page(physaddr_t address, int make, page_directory_t *dir)
{
  // Turn the address into an index.
  uintptr_t table_idx = PAGE_DIRECTORY_INDEX(address);
  uintptr_t page_idx = PAGE_TABLE_INDEX(address);

#ifdef DEBUG_PAGING
  printk("get_page: t: %d, p: %d 0x%x, 0x%x, 0x%x", table_idx, page_idx,  address, make, dir->tables[table_idx]);
#endif

  uintptr_t *table = (uintptr_t *) &(dir->tables[table_idx]);

  bool table_exist = false;

  if(table != NULL && dir->physical[table_idx].present != 0)
  // If this table is already assigned *(uintptr_t *)(&dir->tables[table_idx])!=0
  {
    table_exist = true;
    page_frame_t* page = &(dir->tables[table_idx]->pages[page_idx]);
#ifdef DEBUG_PAGING
    printk("\nTable exist: 0x%x : 0x%x ", dir->physical[table_idx], page);
#endif
    if (page != NULL && page->present != 0)
    {
#ifdef DEBUG_PAGING
      printk("\nAddress %x Already: 0x%x ", address, *page);
#endif
      return page;
    }
  }

  if(make)
  {
    page_table_t * page_table;
    if (!table_exist)
    {
      // Obtain a memory location for the page tables
      physaddr_t pt_phy;
      // Allocate memory for the table
      page_table = (page_table_t *) kmalloc_ap(sizeof(page_table_t), &pt_phy);
      // Clean the structure
      memset((physaddr_t *) page_table, 0, sizeof(page_table_t));
      dir->physical[table_idx].frame = pt_phy >> 12;
      dir->physical[table_idx].present = 1;
      dir->physical[table_idx].rw = 1;
      dir->physical[table_idx].user = 1;
      dir->tables[table_idx] = page_table;
#ifdef DEBUG_PAGING
      printk("\nNew Table: 0x%x : 0x%x", dir->physical[table_idx], dir->tables[table_idx]);
#endif
    } else {
      page_table = dir->tables[table_idx];
    }
    // Create the page
    page_frame_t * page = &page_table->pages[page_idx];
    alloc_frame_int(page, true, true, true, true, false, NULL);
#ifdef DEBUG_PAGING
    printk("\nReturn 0x%x : 0x%x : 0x%x\n", page_table, page, (page->frame << 12));
#endif
    return page;
  }

  return NULL;
}

page_frame_t *get_page_default(physaddr_t address, int make)
{
  return get_page(address, make, kernel_directory);
}

uintptr_t to_physical_addr(uintptr_t virtual, page_directory_t *dir)
{
  uintptr_t remaining = virtual % 0x1000;
  uintptr_t frame = virtual / 0x1000;
  uintptr_t table = frame / 1024;
  uintptr_t subframe = frame % 1024;
  if (dir->tables[table] != NULL)
  {
    page_frame_t *p = &dir->tables[table]->pages[subframe];
    if (p != NULL)
    {
      return (p->frame << 12) + remaining;
    }
  }
  return NULL;
}

extern uint32_t memory_management_region_start, memory_management_region_end;

void map_region(virtaddr_t *from, virtaddr_t *to)
{
  // TODO-HACK: Before we manage to reimplement a mapping solution that takes into account the temp kmalloc_ap of the kernel dir, we need to manually
  // add a size to the virt_addr to in order to have a bit more memory mapping available
  to = to + 0x0001FFFF;
#ifdef DEBUG_PAGING
  printk("to: 0x%x\n", to);
#endif
  for(uint32_t i = from; i <= to; i += PAGE_SIZE)
  {
#ifdef DEBUG_PAGING
    if (fisrtime == true)
    {
      printk("i: 0x%x\n", i);
      fisrtime = false;
    }
    if (i == to)
    {
      printk("i_end: 0x%x\n", i);
    }
#endif
    page_frame_t *pg = get_page(i, 1, kernel_directory);
    free_frame(pg);
    alloc_frame_int(pg, true, true, true, true, true, i - KERNEL_VIRTUAL_BASE);
  }
}

void map_vregion(virtaddr_t *from, virtaddr_t *to)
{
  // TODO-HACK: Before we manage to reimplement a mapping solution that takes into account the temp kmalloc_ap of the kernel dir, we need to manually
  // add a size to the virt_addr to in order to have a bit more memory mapping available
  to = to + 0x0001FFFF;
#ifdef DEBUG_PAGING
  printk("to: 0x%x\n", to);
#endif
  for(uint32_t i = from; i <= to; i += PAGE_SIZE)
  {
#ifdef DEBUG_PAGING
    if (fisrtime == true)
    {
      printk("i: 0x%x\n", i);
      fisrtime = false;
    }
    if (i == to)
    {
      printk("i_end: 0x%x\n", i);
    }
#endif
    page_frame_t *pg = get_page(i, 1, kernel_directory);
    free_frame(pg);
    alloc_frame_int(pg, true, true, true, true, true, i);
  }
}

/*
 TODO: usable_memory is only the available memory from the portion passed to the function by the mmap routines (Which, FOR NOW
 only support one memory region that is available and is larger than 1MB, in future, make a list of available regions and concatenate them for
 use in this function).
 */
void init_paging(size_t usable_memory, uintptr_t virtual_base_ptr, uintptr_t virtual_top_ptr, 
  uintptr_t physical_base_ptr, uintptr_t physical_top_ptr)
{
  // Check if there is usable memory!
	if (!usable_memory)
	{
		printk("No memory available for paging! Halting...");
   		__asm__ __volatile__ ("cli; hlt");
	}

  register_interrupt_handler(14, page_fault);

#ifdef DEBUG
  printk("Available memory size: %d B, %d KB, %d MB\n", usable_memory, usable_memory/1024, usable_memory/1024/1024);
#endif

#ifdef DEBUG_PAGING
asm volatile("xchg %bx, %bx");
#endif

  // We sized down the algorithm so that it doesn't need to use the stack as often
  kernel_directory = kmalloc_ap(sizeof(page_directory_t), kernel_directory);
  memset(kernel_directory, 0, sizeof(page_directory_t));

  uint32_t kernel_space_end = ((uint32_t) kernel_directory) + sizeof(page_directory_t);

#ifdef DEBUG_PAGING
  printk("kernel_space_end: 0x%x, kernel_directory: 0x%x\n", kernel_space_end, ((uint32_t) kernel_directory));
asm volatile("xchg %bx, %bx");
  bool fisrtime = true;
#endif

  // Map the region containing the kernel
  // 0x00100000 -> 0xC0100000
  // TODO: kernel_space_end is not a good way of mapping the kernel-containing memory region!
#ifdef DEBUG_PAGING
  printk("sizeof: 0x%x, kern_dir: 0x%x\n", sizeof(page_directory_t), (uint32_t) kernel_directory);
#endif
  map_region(KERNEL_VIRTUAL_BASE, kernel_space_end);
  //map_vregion(0x0000000, 0x00100000);
  // Map
  current_directory = kernel_directory;
  set_page_directory((page_directory_t *) (uintptr_t) VIRTUAL_TO_PHYSICAL((uintptr_t) current_directory));

  tlb_flush();

#ifdef DEBUG
  printk("pd_phys_addr: 0x%x, kern_dir: 0x%x\n", VIRTUAL_TO_PHYSICAL((uintptr_t) kernel_directory), (uintptr_t) kernel_directory);
#endif

}

/*void switch_page_directory(uintptr_t *pd)
{
  asm volatile("mov %0, %%cr3" ::"r" (to_physical_addr(pd)));
  current_directory = pd;
}

page_table_t *copyTable(page_table_t *src, physaddr_t *phy)
{
  page_table_t *newPD = (page_directory_t *)kmalloc(sizeof(page_directory_t));
  *phy = ((uint32_t) to_physical_addr((uint32_t) &newPD, kernel_directory));

  for(int i= 0; i < 1024; i++)
  {
    memcpy(&(newPD->pages[i]), &(src->pages[i]), sizeof(page_frame_t));
  }

  return newPD;
} 
page_directory_t * create_page_directory()
{
  page_directory_t *newPD = (page_directory_t *)kmalloc(sizeof(page_directory_t));
  memset(newPD, 0, sizeof(page_directory_t));

  for(int i = KERNEL_PAGE; i < 1024; i++)
  {
    newPD->tables[i] = kernel_directory->tables[i];
    //newPD->physical_tables[i] = kernel_directory->physical_tables[i];
  }

  return newPD;
}

page_directory_t * copy_page_directory(page_directory_t *src)
{
  page_directory_t *newPD = (page_directory_t *)kmalloc(sizeof(page_directory_t));
  memset(newPD, 0, sizeof(page_directory_t));
  for(int i = 0; i < 1024; i++)
  {
    if(i >= KERNEL_PAGE)
    {
      newPD->tables[i] = src->tables[i];
      //newPD->physical_tables[i] = src->physical_tables[i];
    } else if (src->tables[i] != NULL){
      uint32_t phy = 0;
      newPD->tables[i] = copyTable(src->tables[i], &phy);
      //newPD->physical_tables[i] = (uintptr_t)phy;
    }
  }
  
  return newPD;
}*/