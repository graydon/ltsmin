// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#include <config.h>

#include <assert.h>
#include <stdint.h>

#include <dynamic-array.h>
#include <hre/user.h>

enum state {Terminated=0,Idle=1,Dirty=2,Active=3};

struct term_msg {
    enum state status;
    uint32_t sent;
    uint32_t recv;
};

enum mode {Immediate=1,Lazy=2};

struct hre_task_queue_s {
    hre_context_t ctx;
    array_manager_t man;
    hre_task_t *task;
    size_t task_size;
    int recv_pending;
    int send_pending;
    struct term_msg term_in;
    hre_msg_t msg;
    enum state status;
    enum mode mode;
    uint32_t sent;
    uint32_t recv;
};

struct hre_task_s {
    hre_task_queue_t queue;
    int task_no;
    int comm;
    int tag;
    hre_task_exec_t call;
    void* arg;
    int len;
    int* pending;
    hre_msg_t* msg;
    hre_msg_t* buf;
    uint32_t buffer_size;
};

static void ready_decr(hre_msg_t self,void* ready_ctx){
    (void)self;
    (*((int*)ready_ctx))--;
}

static void term_action(void* context,hre_msg_t msg){
    hre_task_queue_t queue=(hre_task_queue_t)context;
    memcpy(&queue->term_in,msg->buffer,sizeof(struct term_msg));
    queue->recv_pending=0;
    HREmsgReady(msg);
}

static void TaskSend(hre_task_t task,int target){
    if (task->pending[target]) HREyieldWhile(task->queue->ctx,&task->pending[target]);
    hre_msg_t tmp=task->msg[target];
    task->msg[target]=task->buf[target];
    task->buf[target]=tmp;
    task->pending[target]=1;
    task->queue->sent++;
    HREpostSend(task->buf[target]);
    task->msg[target]->tail=0;
}

static void TaskFlush(hre_task_queue_t queue){
    int max=array_size(queue->man);
    int pos;
    int peers=HREpeers(queue->ctx);
    for(pos=0;pos<max;pos++) if (queue->task[pos]) {
        for(int i=0;i<peers;i++){
            if (queue->task[pos]->msg[i] && queue->task[pos]->msg[i]->tail){
                TaskSend(queue->task[pos],i);
            }
        }
    }
}

void TQwhile(hre_task_queue_t queue,int*condition){
    if (HREpeers(queue->ctx)==1 && (*condition)) {
        Abort("deadlock detected");
    }
    queue->mode=Immediate;
    TaskFlush(queue);
    HREyieldWhile(queue->ctx,condition);
    queue->mode=Lazy;
}

void TQwait(hre_task_queue_t queue){
    if (HREpeers(queue->ctx)==1) return;
    Debug("enter wait");
    queue->status=Dirty;
    queue->mode=Immediate;
    TaskFlush(queue);
    struct term_msg* msg_in=&queue->term_in;
    struct term_msg* msg_out=(struct term_msg*)queue->msg->buffer;
    if (HREme(queue->ctx)==0) {
        queue->status=Idle;
        int round=0;
        while(queue->status!=Terminated) {
            HREyieldWhile(queue->ctx,&queue->recv_pending);
            switch(queue->status){
            case Idle:
            case Dirty:
                if (msg_in->status==Terminated){
                    msg_in->status=Dirty;
                    queue->status=Terminated;
                    break;
                }
                queue->status=Idle;
                if (queue->send_pending) HREyieldWhile(queue->ctx,&queue->send_pending);
                queue->recv_pending=1;
                if (msg_in->status==Idle && msg_in->sent==msg_in->recv){
                    msg_out->status=Terminated;
                    Debug("termination detected in %d rounds",round);
                    queue->send_pending=1;
                    HREpostSend(queue->msg);
                    break;
                }
                msg_out->status=Idle;
                msg_out->sent=queue->sent;
                msg_out->recv=queue->recv;
                queue->status=Idle;
                round++;
                //Print(infoShort,"starting detection round %d",round);
                queue->send_pending=1;
                HREpostSend(queue->msg);
                break;
            case Active:
                Abort("should not be active in this loop")
            case Terminated:
                Abort("should not be terminated in this loop")
            default:
                Abort("missing case");
            }
        }
        queue->status=Active;
    } else {
        queue->status=Dirty;
        while(queue->status!=Terminated) {
            HREyieldWhile(queue->ctx,&queue->recv_pending);
            queue->recv_pending=1;
            switch(queue->status){
            case Idle:
                if (queue->send_pending) HREyieldWhile(queue->ctx,&queue->send_pending);
                if (msg_in->status==Terminated){
                    msg_out->status=Terminated;
                    queue->status=Terminated;
                    //Print(infoShort,"forward terminated");
                    queue->send_pending=1;
                    HREpostSend(queue->msg);
                    break;
                }
                //Print(infoShort,"forward counts");
                msg_out->status=msg_in->status;
                msg_out->sent=msg_in->sent+queue->sent;
                msg_out->recv=msg_in->recv+queue->recv;
                //Print(infoShort,"sending %d %d %d (%d)",msg_out->status,msg_out->sent,msg_out->recv,msg->tail);
                queue->send_pending=1;
                HREpostSend(queue->msg);
                break;
            case Dirty:
                if (queue->send_pending) HREyieldWhile(queue->ctx,&queue->send_pending);
                //Print(infoShort,"forward dirty");
                msg_out->status=Dirty;
                queue->status=Idle;
                queue->send_pending=1;
                HREpostSend(queue->msg);
                break;
            case Active:
                Abort("should not be active in this loop")
            case Terminated:
                Abort("should not be terminated in this loop")
            default:
                Abort("missing case");
            }
        }
        queue->status=Active;
    }
    HREbarrier(queue->ctx);
    queue->mode=Lazy;
    Debug("leave wait");
}


static void task_action(void* context,hre_msg_t msg){
    hre_task_t task=(hre_task_t)context;
    if (task->len) {
        for(unsigned int ofs=0;ofs<msg->tail;ofs+=task->len){
            task->call(task->arg,msg->source,task->len,msg->buffer+ofs);
        }
    } else {
        for(unsigned int ofs=0;ofs<msg->tail;){
            uint16_t len;
            memcpy(&len,msg->buffer+ofs,2);
            ofs+=2;
            task->call(task->arg,msg->source,len,msg->buffer+ofs);
            ofs+=len;
        }
    }
    task->queue->recv++;
    if (task->queue->status==Idle) task->queue->status=Dirty;
    HREmsgReady(msg);
}

hre_task_queue_t HREcreateQueue(hre_context_t ctx){
    hre_task_queue_t queue=HRE_NEW(NULL,struct hre_task_queue_s);
    queue->ctx=ctx;
    queue->mode=Lazy;
    queue->man=create_manager(8);
    ADD_ARRAY(queue->man,queue->task,hre_task_t);
    int peers=HREpeers(ctx);
    if (peers==1) return queue;
    // for more than 1 worker termination detection must be set up.
    int me=HREme(ctx);
    queue->status=Active;
    queue->msg=HREnewMessage(queue->ctx,sizeof(struct term_msg));
    queue->msg->source=me;
    if (me==0){
        queue->recv_pending=0;
        queue->term_in.status=Dirty;
        queue->msg->target=peers-1;
    } else {
        queue->recv_pending=1;
        queue->msg->target=me-1;
    }
    queue->msg->tail=sizeof(struct term_msg);
    queue->msg->tag=HREactionCreate(queue->ctx,0,sizeof(struct term_msg),term_action,queue);
    queue->msg->ready=ready_decr;
    queue->msg->ready_ctx=&queue->send_pending;
    HREbarrier(ctx);
    return queue;
}

void TaskSubmitFixed(hre_task_t task,int owner,void* arg){
    int me=HREme(task->queue->ctx);
    if (me==owner) {
        task->call(task->arg,me,task->len,arg);
        return;
    }
    memcpy((task->msg[owner]->buffer)+(task->msg[owner]->tail),arg,task->len);
    task->msg[owner]->tail+=task->len;
    if (
        ((task->msg[owner]->tail+task->len) > task->msg[owner]->size)
    ||
        (task->queue->mode==Immediate)
    ){
        TaskSend(task,owner);
    }
}

void TaskSubmitFlex(hre_task_t task,int owner,int len,void* arg){
    assert(len>=0);
    int me=HREme(task->queue->ctx);
    if (me==owner) {
        task->call(task->arg,me,len,arg);
        return;
    }
    if((task->msg[owner]->tail+len+2) > task->msg[owner]->size){
        if (((unsigned int)len)>task->msg[owner]->size){
            Abort("task size (%d) exceeds message size (%d)",len,task->msg[owner]->size);
        }
        if (len>0xffff) {
            Abort("task size (%d) exceeds 16 bits",len);
        }
        TaskSend(task,owner);
    }
    uint16_t msg_len=len;
    memcpy((task->msg[owner]->buffer)+(task->msg[owner]->tail),&msg_len,2);
    task->msg[owner]->tail+=2;
    memcpy((task->msg[owner]->buffer)+(task->msg[owner]->tail),arg,len);
    task->msg[owner]->tail+=len;
    if(task->queue->mode==Immediate) TaskSend(task,owner);
}

void TaskDestroy(hre_task_t task){
    hre_task_queue_t queue=task->queue;
    queue->task[task->task_no]=NULL;
    HREfree(hre_heap,task);
}

static int new_no(hre_task_queue_t queue){
    int max=array_size(queue->man);
    int pos;
    for(pos=0;pos<max;pos++){
        if (queue->task[pos]==NULL) return pos;
    }
    ensure_access(queue->man,pos);
    return pos;
}

hre_task_t TaskCreate(hre_task_queue_t queue,uint32_t prio,uint32_t buffer_size,
        hre_task_exec_t call,void * call_ctx,int arglen)
{
    hre_task_t task=HRE_NEW(NULL,struct hre_task_s);
    task->queue=queue;
    task->task_no=new_no(queue);
    task->len=arglen;
    task->arg=call_ctx;
    task->call=call;
    task->comm=prio;
    task->buffer_size=buffer_size;
    int me=HREme(queue->ctx);
    int peers=HREpeers(queue->ctx);
    if (peers>1) {
        task->tag=HREactionCreate(queue->ctx,prio,buffer_size,task_action,task);
        task->msg=(hre_msg_t*)HREmallocZero(NULL,sizeof(hre_msg_t)*peers);
        task->buf=(hre_msg_t*)HREmallocZero(NULL,sizeof(hre_msg_t)*peers);
        task->pending=(int*)HREmallocZero(NULL,sizeof(int)*peers);
        for(int i=0;i<peers;i++) if (i!=me) {
            task->msg[i]=HREnewMessage(queue->ctx,buffer_size);
            task->msg[i]->source=me;
            task->msg[i]->target=i;
            task->msg[i]->comm=prio;
            task->msg[i]->tag=task->tag;
            task->msg[i]->ready=ready_decr;
            task->msg[i]->ready_ctx=&task->pending[i];

            task->buf[i]=HREnewMessage(queue->ctx,buffer_size);
            task->buf[i]->source=me;
            task->buf[i]->target=i;
            task->buf[i]->comm=prio;
            task->buf[i]->tag=task->tag;
            task->buf[i]->ready=ready_decr;
            task->buf[i]->ready_ctx=&task->pending[i];
        }
    } else {
        task->tag=-1;
    }
    queue->task[task->task_no]=task;
    HREbarrier(queue->ctx);
    return task;
}

hre_context_t TQcontext(hre_task_queue_t queue){
    return queue->ctx;
}
