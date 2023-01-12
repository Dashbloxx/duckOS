/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright © 2016-2023 Byteduck */

#include "../tasking/Process.h"
#include "../memory/SafePointer.h"
#include "../memory/AnonymousVMObject.h"
#include "../kstd/KLog.h"

void* Process::sys_memacquire(void* addr, size_t size) {
	LOCK(m_mem_lock);
	auto object_res = AnonymousVMObject::alloc(size);
	if(object_res.is_error())
		return (void*) -ENOMEM;
	auto object = object_res.value();
	if(addr) {
		//We requested a specific address
		auto res = _vm_space->map_object(object, (VirtualAddress) addr);
		if(res.is_error())
			return (void*) -EINVAL;
		_vm_regions.push_back(res.value());
		m_used_pmem += res.value()->size();
		return (void*) res.value()->start();
	} else {
		//We didn't request a specific address
		auto res = _vm_space->map_object(object);
		if(res.is_error())
			return (void*) -ENOMEM;
		_vm_regions.push_back(res.value());
		m_used_pmem += res.value()->size();
		return (void*) res.value()->start();
	}
}

int Process::sys_memrelease(void* addr, size_t size) {
	LOCK(m_mem_lock);
	// Find the region
	for(size_t i = 0; i < _vm_regions.size(); i++) {
		if(_vm_regions[i]->start() == (VirtualAddress) addr && _vm_regions[i]->size() == size) {
			m_used_pmem -= _vm_regions[i]->size();
			_vm_regions.erase(i);
			return SUCCESS;
		}
	}
	KLog::warn("Process", "memrelease() for %s(%d) failed.", _name.c_str(), _pid);
	return ENOENT;
}

int Process::sys_shmcreate(void* addr, size_t size, UserspacePointer<struct shm> s) {
	auto object_res = AnonymousVMObject::alloc(size);
	if(object_res.is_error())
		return object_res.code();
	auto object = object_res.value();

	object->share(_pid, VMProt::RW);
	auto region_res = addr ? map_object(object, (VirtualAddress) addr, VMProt::RW) : map_object(object, VMProt::RW);
	if(region_res.is_error())
		return region_res.code();
	auto region = region_res.value();

	m_mem_lock.synced<void>([&]() {
		m_used_shmem += region->size();
	});

	shm ret;
	ret.size = region->size();
	ret.ptr = (void*) region->start();
	ret.id = object->shm_id();
	s.set(ret);

	return SUCCESS;
}

int Process::sys_shmattach(int id, void* addr, UserspacePointer<struct shm> s) {
	auto do_shmattach = [&] () -> Result {
		// Find the object in question
		auto object = TRY(AnonymousVMObject::get_shared(id));

		// Check permissions
		auto perms = TRY(object->get_shared_permissions(_pid));
		if(!perms.read)
			return Result(ENOENT);

		// Map into our space
		auto region = TRY(addr ? _vm_space->map_object(object, (VirtualAddress) addr, perms) : _vm_space->map_object(object, perms));
		LOCK(m_mem_lock);
		_vm_regions.push_back(region);

		m_used_shmem += region->size();

		// Setup the shm struct
		struct shm ret = {
				.ptr = (void*) region->start(),
				.size = region->size(),
				.id = object->shm_id()
		};
		s.set(ret);

		return Result(SUCCESS);
	};

	return do_shmattach().code();
}

int Process::sys_shmdetach(int id) {
	// Find the object in question
	auto object_res = AnonymousVMObject::get_shared(id);
	if(object_res.is_error())
		return object_res.code();
	auto object = object_res.value();

	// Remove it from our vm regions
	LOCK(m_mem_lock);
	for(size_t i = 0; i < _vm_regions.size(); i++) {
		if(_vm_regions[i]->object() == object) {
			m_used_shmem -= object->size();
			_vm_regions.erase(i);
			return SUCCESS;
		}
	}

	return ENOENT;
}

int Process::sys_shmallow(int id, pid_t pid, int perms) {
	// TODO: Sharing allowed regions that we didn't directly create
	if(perms & SHM_SHARE)
		return -EINVAL;

	if(!(perms &  (SHM_READ | SHM_WRITE)))
		return -EINVAL;
	if((perms & SHM_WRITE) && !(perms & SHM_READ))
		return -EINVAL;
	if(TaskManager::process_for_pid(pid).is_error())
		return -EINVAL;

	// Find the object in question
	auto object_res = AnonymousVMObject::get_shared(id);
	if(object_res.is_error())
		return object_res.code();

	// Set the perms
	object_res.value()->share(pid, VMProt {
			.read = (bool) (perms & SHM_READ),
			.write = (bool) (perms & SHM_WRITE),
			.execute = false,
			.cow = false
	});

	return SUCCESS;
}