# TODO
- [ ] Implement scheduling properly.
- [ ] Implement ipc buffer transferring
- [ ] Implement the sender properly.
- [ ] Implement the block checker properly.
- [ ] Reorganise caps for the sender and main

# Middle man problems
The scheduler works by manipulating priorities to ensure that the thread that
would be determined as scheduled by the kernel is set to the second highest priority.
The scheduler must be the highest priority, and is event based, always on a recv.
The currently scheduled thread will be set to the second highest priority, and
when it's budget expires it will invoked the sche

(assuming we know it's blocking)
If a blocking send (msg) is done, we will receive it. Since we are higher priority, then
we will preempt the sender. We can mark the target as having a blocked message on it, so that
if the target is performing non-blocking recv's it can receive. When we schedule a thread,
we must then check if the target thread is being blocking sent to, and in that case perform a
SendRecv after performing pre-scheduling things.

In the case of non-blocking notification passing, we are guaranteed to get preempted so that's fine,
and we can just forward the notification directly without any problems.

(assuming we know it's non-blocking)
If a non-blocking send is done, then we can just emulate this by performing an NBSend to the target.

The only problem is that we cannot distinguish between IPCs from an NBSend and a Send (endpoints).

Reschedule:
1. Set the currently scheduled thread's priority back to original.
1. Choose a schedulable thread of the highest priority in a LRU order.
2. Set the chosen thread's priority to be 253.
3. If the chosen thread's receive queue is empty, then we schedule the thread and recv.
3. Record the vpmu's value
4. else perform a SendRecv. (CAVEAT: if the receiving thread never recv's, then rrer will hang)

when Recv:
1. If a msg has arrived:
   1. Add it to the corresponding tcb's recv queue.
   2. Perform a reschedule.

2. If a timeout_fault has arrived
   1. If the vpmu has not incremented, mark the current thread as blocked.
   1. perform a reschedule

3. If a notification has arrived,
   1. perform a reschedule
   2. Forward the notification.

Reasons for RRER being highest priority:
1. If lower than current
   - we won't receive timeout faults from the currently scheduled child if the child
     is using round-robin.
   - When an IPC happens, we will not preempt the child.

CAVEAT 2: If a PD performs a recv, we will never receive anything telling us to reschedule threads.

Clearly we need to greatly restructure this entire thing.
We can move the posts of what the recorder records.
Maybe we instrument a small layer above the kernel that acts like a virtual machine.
This virtual machine will allow us to intercept and monitor everything that goes
in and out of the system.
This light weight virtual machine is a very very minimal kernel.

Things to consider:
1. How do I initialise PDs?
2. How do I manage their caps?
   I could keep the caps in the VMM, but how would IPC work then?
2. How do I schedule PDs? (do i have to write my own seL4 kernel scheduler?)
3. How do I force syscalls to perform vm-exits?
4. Where do I record stuff?
4. Do I make a nano kernel?
5. What method do I use to deal with MMIO?
   - Pagefault on all memory access, record value, treating memory as black boxes.
   - somehow record accesses to memory? doesn't seem that useful...
   - virtualise shared memory as an SPSC queue?

For rr on linux, we do not actually deal with any mmio. Files are done through read syscalls,
which makes life easier to some degree, but not really.

# Active + passive combination
The active PD is used to tell the passive one if something is blocked, and will perform a normal send.
The only problem is that we would need the same vpmu cap on both. Maybe this can be requested from the active pd.

Now the only problem is that we cannot distinguish between NBSends and Sends.
What if we just accept that we have to treat all sends as blocking. That shouldn't be too much harm.
And I think that this is now the only caveat?

- Currently scheduled threads are set to the same priority as ourself.
- Allows us to poll because we can check if PMU time is progressing, and if not we can
  schedule other threads
- We can still emulate blocking sends since we can use a pre-thread recv queue.
  Which we use to perform the blocking send when scheduling
  If a thread sent we should also mark it as blocked by recv.

1. A send occurs
2. Received by dedicated receiver thread at highest priority. Receiver thread gets preempted.
3. Receiver thread can determine which thread was the sender.
4. The receiver somehow needs to detect if that was an NBSend or a blocking send.
5. The receiver marks the sender as "possibly blocked", and needs to perform a reschedule

# Active + passive v2
2 PDs:
1. Main pd:
   - Event-based
   - Does scheduling and forwards sends.
   - Does the actual recording.
   - Gets cycle count timestamp from poller (figure out IPC mechanism)
2. Poller pd:
   - Active
   - Polls the VPMU of the currently scheduled thread.
   - Checks if the current thread is blocked.
   - If so, sends a message to the main pd telling it that it's blocked.


